#include "build_model_builder.h"

#include "../diagnostics/diagnostics.h"
#include "arena_dyn.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    String_List include_dirs;
    String_List link_dirs;
    String_List compile_defs;
    String_List compile_options;
} Build_Model_Builder_Scope;

struct Build_Model_Builder {
    Arena *arena;
    Build_Model *model;
    Build_Model_Builder_Scope current_scope;
    size_t *directory_stack;
    size_t directory_stack_count;
    size_t directory_stack_capacity;
    size_t current_directory_index;
    bool has_fatal_error;
    bool warned_before_after_limitation;
    void *diagnostics;
};

typedef bool (*Builder_List_Item_Fn)(String_View item, void *userdata);

typedef struct {
    Build_Target *target;
    Arena *arena;
    Visibility visibility;
} Builder_Target_Value_Ctx;

typedef struct {
    Build_Model_Builder *builder;
    const Cmake_Event *ev;
    Build_Target *target;
    Visibility visibility;
} Builder_Link_Value_Ctx;

typedef struct {
    Build_Model_Builder *builder;
    String_View package_name;
} Builder_Found_Package_Ctx;

typedef struct {
    CPack_Component *component;
    Arena *arena;
} Builder_CPack_Component_Ctx;

typedef struct {
    String_List *list;
    Arena *arena;
} Builder_String_List_Ctx;

static bool builder_append_project_language(String_View item, void *userdata);
static void builder_warn(const Cmake_Event *ev, const char *cause, const char *hint);
static bool builder_push_directory_scope(Build_Model_Builder *builder, String_View source_dir, String_View binary_dir);
static bool builder_pop_directory_scope(Build_Model_Builder *builder);

static void builder_warn_before_after_once(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || builder->warned_before_after_limitation) return;
    builder->warned_before_after_limitation = true;
    builder_warn(
        ev,
        "BEFORE/AFTER ordering is not fully materialized in build model v2",
        "event order is preserved by append; explicit precedence modeling is deferred"
    );
}

static const char *builder_event_command_name(Cmake_Event_Kind kind) {
    switch (kind) {
        case EV_DIAGNOSTIC: return "diagnostic";
        case EV_PROJECT_DECLARE: return "project";
        case EV_VAR_SET: return "set";
        case EV_SET_CACHE_ENTRY: return "set(cache)";
        case EV_TARGET_DECLARE: return "add_target";
        case EV_TARGET_ADD_SOURCE: return "target_sources";
        case EV_TARGET_PROP_SET: return "set_target_properties";
        case EV_TARGET_INCLUDE_DIRECTORIES: return "target_include_directories";
        case EV_TARGET_COMPILE_DEFINITIONS: return "target_compile_definitions";
        case EV_TARGET_COMPILE_OPTIONS: return "target_compile_options";
        case EV_TARGET_LINK_LIBRARIES: return "target_link_libraries";
        case EV_TARGET_LINK_OPTIONS: return "target_link_options";
        case EV_TARGET_LINK_DIRECTORIES: return "target_link_directories";
        case EV_CUSTOM_COMMAND_TARGET: return "add_custom_command(TARGET)";
        case EV_CUSTOM_COMMAND_OUTPUT: return "add_custom_command(OUTPUT)";
        case EV_DIR_PUSH: return "dir_push";
        case EV_DIR_POP: return "dir_pop";
        case EV_DIRECTORY_INCLUDE_DIRECTORIES: return "include_directories";
        case EV_DIRECTORY_LINK_DIRECTORIES: return "link_directories";
        case EV_GLOBAL_COMPILE_DEFINITIONS: return "add_compile_definitions";
        case EV_GLOBAL_COMPILE_OPTIONS: return "add_compile_options";
        case EV_GLOBAL_LINK_OPTIONS: return "add_link_options";
        case EV_GLOBAL_LINK_LIBRARIES: return "link_libraries";
        case EV_TESTING_ENABLE: return "enable_testing";
        case EV_TEST_ADD: return "add_test";
        case EV_INSTALL_ADD_RULE: return "install";
        case EV_CPACK_ADD_INSTALL_TYPE: return "cpack_add_install_type";
        case EV_CPACK_ADD_COMPONENT_GROUP: return "cpack_add_component_group";
        case EV_CPACK_ADD_COMPONENT: return "cpack_add_component";
        case EV_FIND_PACKAGE: return "find_package";
    }
    return "builder";
}

static String_View builder_trim_ws(String_View sv) {
    size_t begin = 0;
    size_t end = sv.count;

    while (begin < end && isspace((unsigned char)sv.data[begin])) begin++;
    while (end > begin && isspace((unsigned char)sv.data[end - 1])) end--;

    return nob_sv_from_parts(sv.data + begin, end - begin);
}

static bool builder_sv_eq_ci_lit(String_View sv, const char *lit) {
    if (!lit) return false;
    size_t n = strlen(lit);
    if (sv.count != n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = (char)tolower((unsigned char)sv.data[i]);
        char b = (char)tolower((unsigned char)lit[i]);
        if (a != b) return false;
    }
    return true;
}

static bool builder_sv_ends_with_ci_lit(String_View sv, const char *lit) {
    if (!lit) return false;
    size_t n = strlen(lit);
    if (sv.count < n) return false;

    size_t start = sv.count - n;
    for (size_t i = 0; i < n; i++) {
        char a = (char)tolower((unsigned char)sv.data[start + i]);
        char b = (char)tolower((unsigned char)lit[i]);
        if (a != b) return false;
    }
    return true;
}

static bool builder_for_each_semicolon_item(String_View raw,
                                            bool trim_ws,
                                            Builder_List_Item_Fn fn,
                                            void *userdata) {
    if (!fn) return false;
    if (raw.count == 0 || !raw.data) return true;

    size_t start = 0;
    for (size_t i = 0; i <= raw.count; i++) {
        bool is_sep = (i == raw.count) || (raw.data[i] == ';');
        if (!is_sep) continue;

        String_View item = nob_sv_from_parts(raw.data + start, i - start);
        if (trim_ws) item = builder_trim_ws(item);
        if (item.count > 0 && !fn(item, userdata)) return false;

        start = i + 1;
    }

    return true;
}

static void builder_emit_diag(const Cmake_Event *ev,
                              Cmake_Diag_Severity sev,
                              const char *component,
                              const char *command,
                              const char *cause,
                              const char *hint) {
    const char *source = "<event>";
    size_t line = 0;
    size_t col = 0;
    if (ev) {
        if (ev->origin.file_path.count > 0) {
            source = nob_temp_sv_to_cstr(ev->origin.file_path);
        }
        line = ev->origin.line;
        col = ev->origin.col;
    }

    diag_log(
        sev == EV_DIAG_ERROR ? DIAG_SEV_ERROR : DIAG_SEV_WARNING,
        component ? component : "build_model_builder",
        source,
        line,
        col,
        command ? command : "",
        cause ? cause : "",
        hint ? hint : ""
    );
}

static bool builder_fail(Build_Model_Builder *builder,
                         const Cmake_Event *ev,
                         const char *cause,
                         const char *hint) {
    if (builder) builder->has_fatal_error = true;
    builder_emit_diag(ev,
                      EV_DIAG_ERROR,
                      "build_model_builder",
                      builder_event_command_name(ev ? ev->kind : EV_DIAGNOSTIC),
                      cause,
                      hint);
    return false;
}

static void builder_warn(const Cmake_Event *ev, const char *cause, const char *hint) {
    builder_emit_diag(ev,
                      EV_DIAG_WARNING,
                      "build_model_builder",
                      builder_event_command_name(ev ? ev->kind : EV_DIAGNOSTIC),
                      cause,
                      hint);
}

static bool builder_push_directory_scope(Build_Model_Builder *builder, String_View source_dir, String_View binary_dir) {
    if (!builder || !builder->model || !builder->arena) return false;

    if (builder->directory_stack_count > 0 &&
        builder->current_directory_index < builder->model->directory_node_count) {
        Build_Directory_Node *current = &builder->model->directory_nodes[builder->current_directory_index];
        if (nob_sv_eq(current->source_dir, source_dir) &&
            nob_sv_eq(current->binary_dir, binary_dir)) {
            if (!arena_da_reserve(builder->arena,
                                  (void**)&builder->directory_stack,
                                  &builder->directory_stack_capacity,
                                  sizeof(*builder->directory_stack),
                                  builder->directory_stack_count + 1)) {
                return false;
            }
            builder->directory_stack[builder->directory_stack_count++] = builder->current_directory_index;
            return true;
        }
    }

    int parent_index = builder->directory_stack_count > 0
        ? (int)builder->directory_stack[builder->directory_stack_count - 1]
        : -1;
    Build_Directory_Node *node = build_model_add_directory_node(builder->model,
                                                                 builder->arena,
                                                                 source_dir,
                                                                 binary_dir,
                                                                 parent_index);
    if (!node) return false;

    if (!arena_da_reserve(builder->arena,
                          (void**)&builder->directory_stack,
                          &builder->directory_stack_capacity,
                          sizeof(*builder->directory_stack),
                          builder->directory_stack_count + 1)) {
        return false;
    }
    builder->directory_stack[builder->directory_stack_count++] = node->index;
    builder->current_directory_index = node->index;
    return true;
}

static bool builder_pop_directory_scope(Build_Model_Builder *builder) {
    if (!builder) return false;
    if (builder->directory_stack_count <= 1) return true;
    builder->directory_stack_count--;
    builder->current_directory_index = builder->directory_stack[builder->directory_stack_count - 1];
    return true;
}

static Target_Type builder_target_type_from_event(Cmake_Target_Type type) {
    switch (type) {
        case EV_TARGET_EXECUTABLE: return TARGET_EXECUTABLE;
        case EV_TARGET_LIBRARY_STATIC: return TARGET_STATIC_LIB;
        case EV_TARGET_LIBRARY_SHARED: return TARGET_SHARED_LIB;
        case EV_TARGET_LIBRARY_MODULE: return TARGET_SHARED_LIB;
        case EV_TARGET_LIBRARY_INTERFACE: return TARGET_INTERFACE_LIB;
        case EV_TARGET_LIBRARY_OBJECT: return TARGET_OBJECT_LIB;
        case EV_TARGET_LIBRARY_UNKNOWN: return TARGET_UTILITY;
    }
    return TARGET_UTILITY;
}

static Install_Rule_Type builder_install_rule_type_from_event(Cmake_Install_Rule_Type type) {
    switch (type) {
        case EV_INSTALL_RULE_TARGET: return INSTALL_RULE_TARGET;
        case EV_INSTALL_RULE_FILE: return INSTALL_RULE_FILE;
        case EV_INSTALL_RULE_PROGRAM: return INSTALL_RULE_PROGRAM;
        case EV_INSTALL_RULE_DIRECTORY: return INSTALL_RULE_DIRECTORY;
    }
    return INSTALL_RULE_FILE;
}

static Visibility builder_visibility_from_event(const Build_Target *target,
                                                Cmake_Visibility vis) {
    if (target && target->type == TARGET_INTERFACE_LIB) {
        return VISIBILITY_INTERFACE;
    }

    switch (vis) {
        case EV_VISIBILITY_PUBLIC: return VISIBILITY_PUBLIC;
        case EV_VISIBILITY_INTERFACE: return VISIBILITY_INTERFACE;
        case EV_VISIBILITY_PRIVATE: return VISIBILITY_PRIVATE;
        case EV_VISIBILITY_UNSPECIFIED: return VISIBILITY_PRIVATE;
    }
    return VISIBILITY_PRIVATE;
}

static bool builder_is_probable_target_ref(String_View item) {
    if (item.count == 0 || !item.data) return false;
    if (item.count >= 2 && item.data[0] == '$' && item.data[1] == '<') return false;

    char c0 = item.data[0];
    if (c0 == '-' || c0 == '/' || c0 == '\\' || c0 == '.') return false;

    for (size_t i = 0; i < item.count; i++) {
        char c = item.data[i];
        if (c == '/' || c == '\\' || c == ':' || isspace((unsigned char)c)) return false;
    }

    if (builder_sv_ends_with_ci_lit(item, ".a")) return false;
    if (builder_sv_ends_with_ci_lit(item, ".lib")) return false;
    if (builder_sv_ends_with_ci_lit(item, ".so")) return false;
    if (builder_sv_ends_with_ci_lit(item, ".dylib")) return false;
    if (builder_sv_ends_with_ci_lit(item, ".dll")) return false;
    if (builder_sv_ends_with_ci_lit(item, ".framework")) return false;

    return true;
}

static String_View builder_concat_views(Arena *arena,
                                        String_View left,
                                        String_View right,
                                        bool with_semicolon) {
    if (!arena) return sv_from_cstr("");
    if (left.count == 0) return right;
    if (right.count == 0) return left;

    size_t extra = with_semicolon ? 1 : 0;
    char *buf = (char*)arena_alloc(arena, left.count + right.count + extra + 1);
    if (!buf) return sv_from_cstr("");

    size_t out = 0;
    memcpy(buf + out, left.data, left.count);
    out += left.count;
    if (with_semicolon) {
        buf[out++] = ';';
    }
    memcpy(buf + out, right.data, right.count);
    out += right.count;
    buf[out] = '\0';
    return nob_sv_from_parts(buf, out);
}

static Build_Target *builder_require_target(Build_Model_Builder *builder,
                                            const Cmake_Event *ev,
                                            String_View target_name) {
    if (!builder || !builder->model) return NULL;
    Build_Target *target = build_model_find_target(builder->model, target_name);
    if (target) return target;

    builder_fail(builder,
                 ev,
                 nob_temp_sprintf("target '"SV_Fmt"' was not declared", SV_Arg(target_name)),
                 "declare the target before mutating it");
    return NULL;
}

static bool builder_append_global_definition_item(String_View item, void *userdata) {
    Build_Model_Builder *builder = (Build_Model_Builder*)userdata;
    if (!builder || !builder->model) return false;
    build_model_add_global_definition(builder->model, builder->arena, item);
    string_list_add_unique(&builder->current_scope.compile_defs, builder->arena, item);
    return true;
}

static bool builder_append_global_compile_option_item(String_View item, void *userdata) {
    Build_Model_Builder *builder = (Build_Model_Builder*)userdata;
    if (!builder || !builder->model) return false;
    build_model_add_global_compile_option(builder->model, builder->arena, item);
    string_list_add_unique(&builder->current_scope.compile_options, builder->arena, item);
    return true;
}

static bool builder_append_target_include_item(String_View item, void *userdata) {
    Builder_Target_Value_Ctx *ctx = (Builder_Target_Value_Ctx*)userdata;
    if (!ctx || !ctx->target || !ctx->arena) return false;
    build_target_add_include_directory(ctx->target, ctx->arena, item, ctx->visibility, CONFIG_ALL);
    return true;
}

static bool builder_append_target_definition_item(String_View item, void *userdata) {
    Builder_Target_Value_Ctx *ctx = (Builder_Target_Value_Ctx*)userdata;
    if (!ctx || !ctx->target || !ctx->arena) return false;
    build_target_add_definition(ctx->target, ctx->arena, item, ctx->visibility, CONFIG_ALL);
    return true;
}

static bool builder_append_target_option_item(String_View item, void *userdata) {
    Builder_Target_Value_Ctx *ctx = (Builder_Target_Value_Ctx*)userdata;
    if (!ctx || !ctx->target || !ctx->arena) return false;
    build_target_add_compile_option(ctx->target, ctx->arena, item, ctx->visibility, CONFIG_ALL);
    return true;
}

static bool builder_append_target_link_option_item(String_View item, void *userdata) {
    Builder_Target_Value_Ctx *ctx = (Builder_Target_Value_Ctx*)userdata;
    if (!ctx || !ctx->target || !ctx->arena) return false;
    build_target_add_link_option(ctx->target, ctx->arena, item, ctx->visibility, CONFIG_ALL);
    return true;
}

static bool builder_append_target_link_directory_item(String_View item, void *userdata) {
    Builder_Target_Value_Ctx *ctx = (Builder_Target_Value_Ctx*)userdata;
    if (!ctx || !ctx->target || !ctx->arena) return false;
    build_target_add_link_directory(ctx->target, ctx->arena, item, ctx->visibility, CONFIG_ALL);
    return true;
}

static bool builder_append_target_source_item(String_View item, void *userdata) {
    Builder_Target_Value_Ctx *ctx = (Builder_Target_Value_Ctx*)userdata;
    if (!ctx || !ctx->target || !ctx->arena) return false;
    build_target_add_source(ctx->target, ctx->arena, item);
    return true;
}

static bool builder_append_found_package_location(String_View item, void *userdata) {
    Builder_Found_Package_Ctx *ctx = (Builder_Found_Package_Ctx*)userdata;
    if (!ctx || !ctx->builder || !ctx->builder->model) return false;
    Found_Package *pkg = build_model_add_package(ctx->builder->model, ctx->package_name, true);
    if (!pkg) return false;
    string_list_add_unique(&pkg->libraries, ctx->builder->arena, item);
    return true;
}

static bool builder_append_cpack_component_dependency_item(String_View item, void *userdata) {
    Builder_CPack_Component_Ctx *ctx = (Builder_CPack_Component_Ctx*)userdata;
    if (!ctx || !ctx->component || !ctx->arena) return false;
    build_cpack_component_add_dependency(ctx->component, ctx->arena, item);
    return true;
}

static bool builder_append_cpack_component_install_type_item(String_View item, void *userdata) {
    Builder_CPack_Component_Ctx *ctx = (Builder_CPack_Component_Ctx*)userdata;
    if (!ctx || !ctx->component || !ctx->arena) return false;
    build_cpack_component_add_install_type(ctx->component, ctx->arena, item);
    return true;
}

static bool builder_append_string_list_item(String_View item, void *userdata) {
    Builder_String_List_Ctx *ctx = (Builder_String_List_Ctx*)userdata;
    if (!ctx || !ctx->list || !ctx->arena) return false;
    string_list_add(ctx->list, ctx->arena, item);
    return true;
}

static bool builder_append_target_link_item(String_View item, void *userdata) {
    Builder_Link_Value_Ctx *ctx = (Builder_Link_Value_Ctx*)userdata;
    if (!ctx || !ctx->builder || !ctx->target || !ctx->builder->model) return false;

    if (builder_sv_eq_ci_lit(item, "debug") ||
        builder_sv_eq_ci_lit(item, "optimized") ||
        builder_sv_eq_ci_lit(item, "general")) {
        return true;
    }

    Build_Target *dep = build_model_find_target(ctx->builder->model, item);
    if (dep) {
        if (ctx->visibility == VISIBILITY_PRIVATE || ctx->visibility == VISIBILITY_PUBLIC) {
            build_target_add_dependency(ctx->target, ctx->builder->arena, dep->name);
        }
        if (ctx->visibility == VISIBILITY_INTERFACE || ctx->visibility == VISIBILITY_PUBLIC) {
            build_target_add_interface_dependency(ctx->target, ctx->builder->arena, dep->name);
        }
        return true;
    }

    build_target_add_library(ctx->target, ctx->builder->arena, item, ctx->visibility);
    if (builder_is_probable_target_ref(item)) {
        builder_warn(ctx->ev,
                     nob_temp_sprintf("link item '"SV_Fmt"' looks like a target but is not declared", SV_Arg(item)),
                     "declare the dependency target before linking, or use a full path / linker flag");
    }
    return true;
}

static bool builder_add_target_dependencies_from_list(Build_Model_Builder *builder,
                                                      Build_Target *target,
                                                      String_View depends) {
    if (!builder || !builder->model || !target) return false;
    if (depends.count == 0) return true;

    const char *p = depends.data;
    const char *end = depends.data + depends.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View item = builder_trim_ws(nob_sv_from_parts(p, (size_t)(q - p)));
        if (item.count > 0) {
            Build_Target *dep = build_model_find_target(builder->model, item);
            if (dep) {
                build_target_add_dependency(target, builder->arena, dep->name);
            }
        }
        if (q >= end) break;
        p = q + 1;
    }
    return true;
}

static bool builder_collect_semicolon_list(Build_Model_Builder *builder,
                                           String_View raw,
                                           String_List *out) {
    if (!out) return false;
    string_list_init(out);
    if (!builder || raw.count == 0) return true;

    Builder_String_List_Ctx ctx = {
        .list = out,
        .arena = builder->arena,
    };
    return builder_for_each_semicolon_item(raw,
                                           true,
                                           builder_append_string_list_item,
                                           &ctx);
}

static String_View builder_first_semicolon_item(String_View raw) {
    size_t i = 0;
    while (i < raw.count && raw.data[i] != ';') i++;
    return builder_trim_ws(nob_sv_from_parts(raw.data, i));
}

static bool builder_fill_custom_command_from_event(Build_Model_Builder *builder,
                                                   const Cmake_Event *ev,
                                                   Custom_Command *cmd,
                                                   String_View outputs,
                                                   String_View byproducts,
                                                   String_View depends) {
    if (!builder || !ev || !cmd) return false;

    String_List outputs_list = {0};
    String_List byproducts_list = {0};
    String_List depends_list = {0};

    if (!builder_collect_semicolon_list(builder, outputs, &outputs_list)) {
        return builder_fail(builder, ev, "failed to parse custom command outputs", "check event payload formatting");
    }
    if (!builder_collect_semicolon_list(builder, byproducts, &byproducts_list)) {
        return builder_fail(builder, ev, "failed to parse custom command byproducts", "check event payload formatting");
    }
    if (!builder_collect_semicolon_list(builder, depends, &depends_list)) {
        return builder_fail(builder, ev, "failed to parse custom command depends", "check event payload formatting");
    }

    build_custom_command_add_outputs(cmd, builder->arena, &outputs_list);
    build_custom_command_add_byproducts(cmd, builder->arena, &byproducts_list);
    build_custom_command_add_depends(cmd, builder->arena, &depends_list);
    return true;
}

static bool builder_handle_event_diagnostic(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    const char *component = ev->as.diag.component.count > 0
        ? nob_temp_sv_to_cstr(ev->as.diag.component)
        : "evaluator";
    const char *command = ev->as.diag.command.count > 0
        ? nob_temp_sv_to_cstr(ev->as.diag.command)
        : builder_event_command_name(ev->kind);
    const char *cause = ev->as.diag.cause.count > 0
        ? nob_temp_sv_to_cstr(ev->as.diag.cause)
        : "diagnostic event";
    const char *hint = ev->as.diag.hint.count > 0
        ? nob_temp_sv_to_cstr(ev->as.diag.hint)
        : "";

    builder_emit_diag(ev, ev->as.diag.severity, component, command, cause, hint);
    if (ev->as.diag.severity == EV_DIAG_ERROR) {
        builder->has_fatal_error = true;
        return false;
    }
    return true;
}

static bool builder_handle_event_project_declare(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;

    build_model_set_project_info(builder->model,
                                 ev->as.project_declare.name,
                                 ev->as.project_declare.version);
    builder->model->project_description = build_model_copy_string(
        builder->arena,
        ev->as.project_declare.description
    );

    builder->model->project_languages.count = 0;
    return builder_for_each_semicolon_item(
        ev->as.project_declare.languages,
        true,
        builder_append_project_language,
        builder
    );
}

static bool builder_append_project_language(String_View item, void *userdata) {
    Build_Model_Builder *builder = (Build_Model_Builder*)userdata;
    if (!builder || !builder->model) return false;
    build_model_enable_language(builder->model, builder->arena, item);
    return true;
}

static bool builder_handle_event_var_set(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    build_model_set_env_var(builder->model,
                            builder->arena,
                            ev->as.var_set.key,
                            ev->as.var_set.value);
    return true;
}

static bool builder_handle_event_set_cache_entry(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    build_model_set_cache_variable(builder->model,
                                   ev->as.cache_entry.key,
                                   ev->as.cache_entry.value,
                                   sv_from_cstr("STRING"),
                                   sv_from_cstr(""));
    return true;
}

static bool builder_handle_event_target_declare(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;

    String_View name = builder_trim_ws(ev->as.target_declare.name);
    if (name.count == 0) {
        return builder_fail(builder, ev, "target declaration has an empty name", "provide a non-empty target name");
    }

    if (build_model_find_target(builder->model, name) != NULL) {
        return builder_fail(builder,
                            ev,
                            nob_temp_sprintf("duplicate target declaration for '"SV_Fmt"'", SV_Arg(name)),
                            "target names must be unique");
    }

    Target_Type type = builder_target_type_from_event(ev->as.target_declare.type);
    Build_Target *target = build_model_add_target(builder->model, name, type);
    if (!target) {
        return builder_fail(builder,
                            ev,
                            nob_temp_sprintf("failed to create target '"SV_Fmt"'", SV_Arg(name)),
                            "check target type consistency and memory allocation");
    }
    target->owner_directory_index = builder->current_directory_index;
    return true;
}

static bool builder_handle_event_target_add_source(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    Build_Target *target = builder_require_target(builder, ev, ev->as.target_add_source.target_name);
    if (!target) return false;

    if (target->type == TARGET_INTERFACE_LIB) {
        return builder_fail(builder,
                            ev,
                            nob_temp_sprintf("INTERFACE target '"SV_Fmt"' cannot have source files", SV_Arg(target->name)),
                            "use only INTERFACE properties for interface libraries");
    }

    Builder_Target_Value_Ctx ctx = {
        .target = target,
        .arena = builder->arena,
        .visibility = VISIBILITY_PRIVATE,
    };
    return builder_for_each_semicolon_item(ev->as.target_add_source.path,
                                           true,
                                           builder_append_target_source_item,
                                           &ctx);
}

static bool builder_handle_event_target_prop_set(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    Build_Target *target = builder_require_target(builder, ev, ev->as.target_prop_set.target_name);
    if (!target) return false;

    String_View key = builder_trim_ws(ev->as.target_prop_set.key);
    String_View value = ev->as.target_prop_set.value;
    if (key.count == 0) {
        return builder_fail(builder, ev, "target property key is empty", "provide a valid property name");
    }

    if (ev->as.target_prop_set.op == EV_PROP_SET) {
        build_target_set_property_smart(target, builder->arena, key, value);
        return true;
    }

    String_View current = build_target_get_property(target, key);
    String_View merged = sv_from_cstr("");
    if (ev->as.target_prop_set.op == EV_PROP_APPEND_LIST) {
        merged = builder_concat_views(builder->arena, current, value, true);
    } else if (ev->as.target_prop_set.op == EV_PROP_APPEND_STRING) {
        merged = builder_concat_views(builder->arena, current, value, false);
    } else {
        merged = value;
    }

    build_target_set_property_smart(target, builder->arena, key, merged);
    return true;
}

static bool builder_handle_event_target_include_directories(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    Build_Target *target = builder_require_target(builder, ev, ev->as.target_include_directories.target_name);
    if (!target) return false;

    Visibility vis = builder_visibility_from_event(target, ev->as.target_include_directories.visibility);
    Builder_Target_Value_Ctx ctx = {
        .target = target,
        .arena = builder->arena,
        .visibility = vis,
    };

    if (ev->as.target_include_directories.is_before) {
        builder_warn_before_after_once(builder, ev);
    }

    (void)ev->as.target_include_directories.is_system;

    return builder_for_each_semicolon_item(ev->as.target_include_directories.path,
                                           true,
                                           builder_append_target_include_item,
                                           &ctx);
}

static bool builder_handle_event_target_compile_definitions(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    Build_Target *target = builder_require_target(builder, ev, ev->as.target_compile_definitions.target_name);
    if (!target) return false;

    Visibility vis = builder_visibility_from_event(target, ev->as.target_compile_definitions.visibility);
    Builder_Target_Value_Ctx ctx = {
        .target = target,
        .arena = builder->arena,
        .visibility = vis,
    };

    return builder_for_each_semicolon_item(ev->as.target_compile_definitions.item,
                                           true,
                                           builder_append_target_definition_item,
                                           &ctx);
}

static bool builder_handle_event_target_compile_options(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    Build_Target *target = builder_require_target(builder, ev, ev->as.target_compile_options.target_name);
    if (!target) return false;

    Visibility vis = builder_visibility_from_event(target, ev->as.target_compile_options.visibility);
    Builder_Target_Value_Ctx ctx = {
        .target = target,
        .arena = builder->arena,
        .visibility = vis,
    };

    return builder_for_each_semicolon_item(ev->as.target_compile_options.item,
                                           true,
                                           builder_append_target_option_item,
                                           &ctx);
}

static bool builder_handle_event_target_link_libraries(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    Build_Target *target = builder_require_target(builder, ev, ev->as.target_link_libraries.target_name);
    if (!target) return false;

    if (target->type == TARGET_INTERFACE_LIB &&
        ev->as.target_link_libraries.visibility == EV_VISIBILITY_PRIVATE) {
        return builder_fail(builder,
                            ev,
                            nob_temp_sprintf("INTERFACE target '"SV_Fmt"' cannot link PRIVATE items", SV_Arg(target->name)),
                            "use INTERFACE visibility for interface library dependencies");
    }

    Visibility vis = builder_visibility_from_event(target, ev->as.target_link_libraries.visibility);
    Builder_Link_Value_Ctx ctx = {
        .builder = builder,
        .ev = ev,
        .target = target,
        .visibility = vis,
    };

    return builder_for_each_semicolon_item(ev->as.target_link_libraries.item,
                                           true,
                                           builder_append_target_link_item,
                                           &ctx);
}

static bool builder_handle_event_global_compile_definitions(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    return builder_for_each_semicolon_item(ev->as.global_compile_definitions.item,
                                           true,
                                           builder_append_global_definition_item,
                                           builder);
}

static bool builder_handle_event_global_compile_options(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    return builder_for_each_semicolon_item(ev->as.global_compile_options.item,
                                           true,
                                           builder_append_global_compile_option_item,
                                           builder);
}

static bool builder_handle_event_target_link_options(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    Build_Target *target = builder_require_target(builder, ev, ev->as.target_link_options.target_name);
    if (!target) return false;

    Visibility vis = builder_visibility_from_event(target, ev->as.target_link_options.visibility);
    Builder_Target_Value_Ctx ctx = {
        .target = target,
        .arena = builder->arena,
        .visibility = vis,
    };
    return builder_for_each_semicolon_item(ev->as.target_link_options.item,
                                           true,
                                           builder_append_target_link_option_item,
                                           &ctx);
}

static bool builder_handle_event_target_link_directories(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    Build_Target *target = builder_require_target(builder, ev, ev->as.target_link_directories.target_name);
    if (!target) return false;

    Visibility vis = builder_visibility_from_event(target, ev->as.target_link_directories.visibility);
    Builder_Target_Value_Ctx ctx = {
        .target = target,
        .arena = builder->arena,
        .visibility = vis,
    };
    return builder_for_each_semicolon_item(ev->as.target_link_directories.path,
                                           true,
                                           builder_append_target_link_directory_item,
                                           &ctx);
}

static bool builder_handle_event_dir_push(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    if (!builder_push_directory_scope(builder, ev->as.dir_push.source_dir, ev->as.dir_push.binary_dir)) {
        return builder_fail(builder, ev, "failed to push directory scope", "check memory allocation");
    }
    return true;
}

static bool builder_handle_event_dir_pop(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    if (!builder_pop_directory_scope(builder)) {
        return builder_fail(builder, ev, "failed to pop directory scope", "scope stack is inconsistent");
    }
    return true;
}

static bool builder_handle_event_directory_include_directories(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    if (ev->as.directory_include_directories.is_before) {
        builder_warn_before_after_once(builder, ev);
    }
    build_model_add_include_directory_scoped(builder->model,
                                             builder->arena,
                                             builder->current_directory_index,
                                             ev->as.directory_include_directories.path,
                                             ev->as.directory_include_directories.is_system);
    return true;
}

static bool builder_handle_event_directory_link_directories(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    if (ev->as.directory_link_directories.is_before) {
        builder_warn_before_after_once(builder, ev);
    }
    build_model_add_link_directory_scoped(builder->model,
                                          builder->arena,
                                          builder->current_directory_index,
                                          ev->as.directory_link_directories.path);
    return true;
}

static bool builder_handle_event_global_link_options(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    build_model_add_global_link_option(builder->model, builder->arena, ev->as.global_link_options.item);
    return true;
}

static bool builder_handle_event_global_link_libraries(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    build_model_add_global_link_library(builder->model, builder->arena, ev->as.global_link_libraries.item);
    return true;
}

static bool builder_handle_event_testing_enable(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    build_model_set_testing_enabled(builder->model, ev->as.testing_enable.enabled);
    return true;
}

static bool builder_handle_event_test_add(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    Build_Test *test = build_model_add_test(builder->model,
                                            ev->as.test_add.name,
                                            ev->as.test_add.command,
                                            ev->as.test_add.working_dir,
                                            ev->as.test_add.command_expand_lists);
    if (!test) {
        return builder_fail(builder, ev, "failed to add test from EV_TEST_ADD", "check memory allocation");
    }
    build_model_set_testing_enabled(builder->model, true);
    return true;
}

static bool builder_handle_event_install_add_rule(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    Install_Rule_Type type = builder_install_rule_type_from_event(ev->as.install_add_rule.rule_type);
    build_model_add_install_rule(builder->model,
                                 builder->arena,
                                 type,
                                 ev->as.install_add_rule.item,
                                 ev->as.install_add_rule.destination);
    build_model_set_install_enabled(builder->model, true);
    return true;
}

static bool builder_handle_event_cpack_add_install_type(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    CPack_Install_Type *install_type = build_model_ensure_cpack_install_type(builder->model,
                                                                              ev->as.cpack_add_install_type.name);
    if (!install_type) {
        return builder_fail(builder, ev, "failed to add CPack install type", "check memory allocation");
    }
    if (ev->as.cpack_add_install_type.display_name.count > 0) {
        build_cpack_install_type_set_display_name(install_type, ev->as.cpack_add_install_type.display_name);
    }
    return true;
}

static bool builder_handle_event_cpack_add_component_group(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    CPack_Component_Group *group = build_model_ensure_cpack_group(builder->model,
                                                                  ev->as.cpack_add_component_group.name);
    if (!group) {
        return builder_fail(builder, ev, "failed to add CPack component group", "check memory allocation");
    }

    if (ev->as.cpack_add_component_group.display_name.count > 0) {
        build_cpack_group_set_display_name(group, ev->as.cpack_add_component_group.display_name);
    }
    if (ev->as.cpack_add_component_group.description.count > 0) {
        build_cpack_group_set_description(group, ev->as.cpack_add_component_group.description);
    }
    if (ev->as.cpack_add_component_group.parent_group.count > 0) {
        build_cpack_group_set_parent_group(group, ev->as.cpack_add_component_group.parent_group);
    }
    build_cpack_group_set_expanded(group, ev->as.cpack_add_component_group.expanded);
    build_cpack_group_set_bold_title(group, ev->as.cpack_add_component_group.bold_title);
    return true;
}

static bool builder_handle_event_cpack_add_component(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    CPack_Component *component = build_model_ensure_cpack_component(builder->model,
                                                                    ev->as.cpack_add_component.name);
    if (!component) {
        return builder_fail(builder, ev, "failed to add CPack component", "check memory allocation");
    }

    if (ev->as.cpack_add_component.display_name.count > 0) {
        build_cpack_component_set_display_name(component, ev->as.cpack_add_component.display_name);
    }
    if (ev->as.cpack_add_component.description.count > 0) {
        build_cpack_component_set_description(component, ev->as.cpack_add_component.description);
    }
    if (ev->as.cpack_add_component.group.count > 0) {
        build_cpack_component_set_group(component, ev->as.cpack_add_component.group);
    }

    build_cpack_component_set_required(component, ev->as.cpack_add_component.required);
    build_cpack_component_set_hidden(component, ev->as.cpack_add_component.hidden);
    build_cpack_component_set_disabled(component, ev->as.cpack_add_component.disabled);
    build_cpack_component_set_downloaded(component, ev->as.cpack_add_component.downloaded);

    build_cpack_component_clear_dependencies(component);
    build_cpack_component_clear_install_types(component);

    Builder_CPack_Component_Ctx ctx = {
        .component = component,
        .arena = builder->arena,
    };

    if (!builder_for_each_semicolon_item(ev->as.cpack_add_component.depends,
                                         true,
                                         builder_append_cpack_component_dependency_item,
                                         &ctx)) {
        return builder_fail(builder, ev, "failed to append CPack component dependencies", "check memory allocation");
    }
    if (!builder_for_each_semicolon_item(ev->as.cpack_add_component.install_types,
                                         true,
                                         builder_append_cpack_component_install_type_item,
                                         &ctx)) {
        return builder_fail(builder, ev, "failed to append CPack component install types", "check memory allocation");
    }

    return true;
}

static bool builder_handle_event_custom_command_target(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;

    Build_Target *target = builder_require_target(builder, ev, ev->as.custom_command_target.target_name);
    if (!target) return false;

    if (ev->as.custom_command_target.command_count == 0 || !ev->as.custom_command_target.commands) {
        return builder_fail(builder, ev, "custom TARGET command has empty command text", "provide COMMAND in add_custom_command(TARGET ...)");
    }

    Custom_Command *cmd = build_target_add_custom_command_ex(target,
                                                             builder->arena,
                                                             ev->as.custom_command_target.pre_build,
                                                             ev->as.custom_command_target.commands[0],
                                                             ev->as.custom_command_target.working_dir,
                                                             ev->as.custom_command_target.comment);
    if (!cmd) {
        return builder_fail(builder, ev, "failed to allocate target custom command", "check memory allocation");
    }
    for (size_t i = 1; i < ev->as.custom_command_target.command_count; i++) {
        build_custom_command_append_command(cmd, builder->arena, ev->as.custom_command_target.commands[i]);
    }

    if (!builder_fill_custom_command_from_event(builder,
                                                ev,
                                                cmd,
                                                ev->as.custom_command_target.outputs,
                                                ev->as.custom_command_target.byproducts,
                                                ev->as.custom_command_target.depends)) {
        return false;
    }

    build_custom_command_set_main_dependency(cmd, ev->as.custom_command_target.main_dependency);
    build_custom_command_set_depfile(cmd, ev->as.custom_command_target.depfile);
    build_custom_command_set_flags(cmd,
                                   ev->as.custom_command_target.append,
                                   ev->as.custom_command_target.verbatim,
                                   ev->as.custom_command_target.uses_terminal,
                                   ev->as.custom_command_target.command_expand_lists,
                                   ev->as.custom_command_target.depends_explicit_only,
                                   ev->as.custom_command_target.codegen);

    if (!builder_add_target_dependencies_from_list(builder, target, ev->as.custom_command_target.depends)) {
        return builder_fail(builder, ev, "failed to link custom command target dependencies", "check dependency processing");
    }

    return true;
}

static bool builder_handle_event_custom_command_output(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;
    if (ev->as.custom_command_output.command_count == 0 || !ev->as.custom_command_output.commands) {
        return builder_fail(builder, ev, "custom OUTPUT command has empty command text", "provide COMMAND in add_custom_command(OUTPUT ...)");
    }

    Custom_Command *cmd = NULL;
    bool append_to_existing = false;

    if (ev->as.custom_command_output.append) {
        String_View first_output = builder_first_semicolon_item(ev->as.custom_command_output.outputs);
        if (first_output.count > 0) {
            cmd = build_model_find_output_custom_command_by_output(builder->model, first_output);
            append_to_existing = (cmd != NULL);
        }
        if (!cmd) {
            builder_warn(ev,
                         "add_custom_command(OUTPUT ... APPEND) had no matching previous OUTPUT",
                         "a new output custom command was created");
        }
    }

    if (!cmd) {
        cmd = build_model_add_custom_command_output_ex(builder->model,
                                                       builder->arena,
                                                       ev->as.custom_command_output.commands[0],
                                                       ev->as.custom_command_output.working_dir,
                                                       ev->as.custom_command_output.comment);
        if (!cmd) {
            return builder_fail(builder, ev, "failed to allocate output custom command", "check memory allocation");
        }
        for (size_t i = 1; i < ev->as.custom_command_output.command_count; i++) {
            build_custom_command_append_command(cmd, builder->arena, ev->as.custom_command_output.commands[i]);
        }
    } else {
        for (size_t i = 0; i < ev->as.custom_command_output.command_count; i++) {
            build_custom_command_append_command(cmd, builder->arena, ev->as.custom_command_output.commands[i]);
        }
    }

    String_List outputs_list = {0};
    String_List byproducts_list = {0};
    String_List depends_list = {0};
    if (!builder_collect_semicolon_list(builder, ev->as.custom_command_output.outputs, &outputs_list)) {
        return builder_fail(builder, ev, "failed to parse output custom command outputs", "check event payload formatting");
    }
    if (!builder_collect_semicolon_list(builder, ev->as.custom_command_output.byproducts, &byproducts_list)) {
        return builder_fail(builder, ev, "failed to parse output custom command byproducts", "check event payload formatting");
    }
    if (!builder_collect_semicolon_list(builder, ev->as.custom_command_output.depends, &depends_list)) {
        return builder_fail(builder, ev, "failed to parse output custom command depends", "check event payload formatting");
    }

    if (!append_to_existing) {
        build_custom_command_add_outputs(cmd, builder->arena, &outputs_list);
    }
    build_custom_command_add_byproducts(cmd, builder->arena, &byproducts_list);
    build_custom_command_add_depends(cmd, builder->arena, &depends_list);
    if (append_to_existing) {
        build_custom_command_set_main_dependency_if_empty(cmd, ev->as.custom_command_output.main_dependency);
        build_custom_command_set_depfile_if_empty(cmd, ev->as.custom_command_output.depfile);
        build_custom_command_merge_flags(cmd,
                                         ev->as.custom_command_output.append,
                                         ev->as.custom_command_output.verbatim,
                                         ev->as.custom_command_output.uses_terminal,
                                         ev->as.custom_command_output.command_expand_lists,
                                         ev->as.custom_command_output.depends_explicit_only,
                                         ev->as.custom_command_output.codegen);
    } else {
        build_custom_command_set_main_dependency(cmd, ev->as.custom_command_output.main_dependency);
        build_custom_command_set_depfile(cmd, ev->as.custom_command_output.depfile);
        build_custom_command_set_flags(cmd,
                                       ev->as.custom_command_output.append,
                                       ev->as.custom_command_output.verbatim,
                                       ev->as.custom_command_output.uses_terminal,
                                       ev->as.custom_command_output.command_expand_lists,
                                       ev->as.custom_command_output.depends_explicit_only,
                                       ev->as.custom_command_output.codegen);
    }
    return true;
}

static bool builder_handle_event_find_package(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !builder->model || !ev) return false;

    String_View name = builder_trim_ws(ev->as.find_package.package_name);
    if (name.count == 0) {
        return builder_fail(builder, ev, "find_package event has empty package name", "provide a package name");
    }

    Found_Package *pkg = build_model_add_package(builder->model, name, ev->as.find_package.found);
    if (!pkg) {
        return builder_fail(builder,
                            ev,
                            nob_temp_sprintf("failed to register package '"SV_Fmt"'", SV_Arg(name)),
                            "check memory allocation while adding package metadata");
    }

    pkg->found = pkg->found || ev->as.find_package.found;
    property_list_add(&pkg->properties, builder->arena, sv_from_cstr("MODE"), ev->as.find_package.mode);
    property_list_add(&pkg->properties, builder->arena, sv_from_cstr("REQUIRED"), ev->as.find_package.required ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"));

    if (ev->as.find_package.location.count > 0) {
        Builder_Found_Package_Ctx ctx = {
            .builder = builder,
            .package_name = name,
        };
        if (!builder_for_each_semicolon_item(ev->as.find_package.location,
                                             true,
                                             builder_append_found_package_location,
                                             &ctx)) {
            return builder_fail(builder,
                                ev,
                                nob_temp_sprintf("failed to save package location for '"SV_Fmt"'", SV_Arg(name)),
                                "check memory allocation for package metadata");
        }
    }

    if (ev->as.find_package.required && !ev->as.find_package.found) {
        builder_warn(ev,
                     nob_temp_sprintf("required package '"SV_Fmt"' was not found", SV_Arg(name)),
                     "evaluation should already emit an error for required package misses");
    }
    return true;
}

Build_Model_Builder *builder_create(Arena *arena, void *diagnostics) {
    if (!arena) return NULL;

    Build_Model_Builder *builder = arena_alloc_zero(arena, sizeof(*builder));
    if (!builder) return NULL;

    builder->arena = arena;
    builder->diagnostics = diagnostics;
    builder->model = build_model_create(arena);
    if (!builder->model) return NULL;

    string_list_init(&builder->current_scope.include_dirs);
    string_list_init(&builder->current_scope.link_dirs);
    string_list_init(&builder->current_scope.compile_defs);
    string_list_init(&builder->current_scope.compile_options);
    builder->has_fatal_error = false;
    builder->warned_before_after_limitation = false;
    builder->directory_stack = NULL;
    builder->directory_stack_count = 0;
    builder->directory_stack_capacity = 0;
    builder->current_directory_index = builder->model->root_directory_index;

    if (!builder_push_directory_scope(builder, sv_from_cstr(""), sv_from_cstr(""))) {
        return NULL;
    }

    return builder;
}

bool builder_apply_event(Build_Model_Builder *builder, const Cmake_Event *ev) {
    if (!builder || !ev) return false;
    if (builder->has_fatal_error) return false;

    switch (ev->kind) {
        case EV_DIAGNOSTIC:
            return builder_handle_event_diagnostic(builder, ev);

        case EV_PROJECT_DECLARE:
            return builder_handle_event_project_declare(builder, ev);

        case EV_VAR_SET:
            return builder_handle_event_var_set(builder, ev);

        case EV_SET_CACHE_ENTRY:
            return builder_handle_event_set_cache_entry(builder, ev);

        case EV_TARGET_DECLARE:
            return builder_handle_event_target_declare(builder, ev);

        case EV_TARGET_ADD_SOURCE:
            return builder_handle_event_target_add_source(builder, ev);

        case EV_TARGET_PROP_SET:
            return builder_handle_event_target_prop_set(builder, ev);

        case EV_TARGET_INCLUDE_DIRECTORIES:
            return builder_handle_event_target_include_directories(builder, ev);

        case EV_TARGET_COMPILE_DEFINITIONS:
            return builder_handle_event_target_compile_definitions(builder, ev);

        case EV_TARGET_COMPILE_OPTIONS:
            return builder_handle_event_target_compile_options(builder, ev);

        case EV_TARGET_LINK_LIBRARIES:
            return builder_handle_event_target_link_libraries(builder, ev);

        case EV_TARGET_LINK_OPTIONS:
            return builder_handle_event_target_link_options(builder, ev);

        case EV_TARGET_LINK_DIRECTORIES:
            return builder_handle_event_target_link_directories(builder, ev);

        case EV_CUSTOM_COMMAND_TARGET:
            return builder_handle_event_custom_command_target(builder, ev);

        case EV_CUSTOM_COMMAND_OUTPUT:
            return builder_handle_event_custom_command_output(builder, ev);

        case EV_DIR_PUSH:
            return builder_handle_event_dir_push(builder, ev);

        case EV_DIR_POP:
            return builder_handle_event_dir_pop(builder, ev);

        case EV_DIRECTORY_INCLUDE_DIRECTORIES:
            return builder_handle_event_directory_include_directories(builder, ev);

        case EV_DIRECTORY_LINK_DIRECTORIES:
            return builder_handle_event_directory_link_directories(builder, ev);

        case EV_GLOBAL_COMPILE_DEFINITIONS:
            return builder_handle_event_global_compile_definitions(builder, ev);

        case EV_GLOBAL_COMPILE_OPTIONS:
            return builder_handle_event_global_compile_options(builder, ev);

        case EV_GLOBAL_LINK_OPTIONS:
            return builder_handle_event_global_link_options(builder, ev);

        case EV_GLOBAL_LINK_LIBRARIES:
            return builder_handle_event_global_link_libraries(builder, ev);

        case EV_TESTING_ENABLE:
            return builder_handle_event_testing_enable(builder, ev);

        case EV_TEST_ADD:
            return builder_handle_event_test_add(builder, ev);

        case EV_INSTALL_ADD_RULE:
            return builder_handle_event_install_add_rule(builder, ev);

        case EV_CPACK_ADD_INSTALL_TYPE:
            return builder_handle_event_cpack_add_install_type(builder, ev);

        case EV_CPACK_ADD_COMPONENT_GROUP:
            return builder_handle_event_cpack_add_component_group(builder, ev);

        case EV_CPACK_ADD_COMPONENT:
            return builder_handle_event_cpack_add_component(builder, ev);

        case EV_FIND_PACKAGE:
            return builder_handle_event_find_package(builder, ev);
    }

    builder_warn(ev, "unsupported event kind ignored by builder", "extend build_model_builder.c to handle this event");
    return true;
}

bool builder_apply_stream(Build_Model_Builder *builder, const Cmake_Event_Stream *stream) {
    if (!builder || !stream) return false;
    for (size_t i = 0; i < stream->count; i++) {
        if (!builder_apply_event(builder, &stream->items[i])) return false;
    }
    return true;
}

Build_Model *builder_finish(Build_Model_Builder *builder) {
    if (!builder || builder->has_fatal_error) return NULL;
    return builder->model;
}

bool builder_has_fatal_error(const Build_Model_Builder *builder) {
    return builder ? builder->has_fatal_error : true;
}
