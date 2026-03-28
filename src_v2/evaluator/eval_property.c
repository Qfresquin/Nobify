#include "evaluator_internal.h"
#include "eval_expr.h"

#include "stb_ds.h"
#include "sv_utils.h"

#include <ctype.h>
#include <string.h>

static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";
static const char *k_global_link_opts_var = "NOBIFY_GLOBAL_LINK_OPTIONS";

static String_View merge_property_value_temp(EvalExecContext *ctx,
                                             String_View current,
                                             String_View incoming,
                                             Cmake_Target_Property_Op op) {
    if (op == EV_PROP_SET) return incoming;
    if (incoming.count == 0) return current;
    if (current.count == 0) return incoming;

    bool with_semicolon = (op == EV_PROP_APPEND_LIST || op == EV_PROP_PREPEND_LIST);
    size_t total = current.count + incoming.count + (with_semicolon ? 1 : 0);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    if (op == EV_PROP_PREPEND_LIST) {
        memcpy(buf + off, incoming.data, incoming.count);
        off += incoming.count;
        if (with_semicolon) buf[off++] = ';';
        memcpy(buf + off, current.data, current.count);
        off += current.count;
    } else {
        memcpy(buf + off, current.data, current.count);
        off += current.count;
        if (with_semicolon) buf[off++] = ';';
        memcpy(buf + off, incoming.data, incoming.count);
        off += incoming.count;
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool is_current_directory_object(EvalExecContext *ctx, String_View object_id) {
    if (!ctx) return false;
    if (object_id.count == 0) return true;
    if (eval_sv_eq_ci_lit(object_id, ".")) return true;

    String_View cur_src = eval_current_source_dir(ctx);
    String_View cur_bin = eval_current_binary_dir(ctx);
    if (svu_eq_ci_sv(object_id, cur_src)) return true;
    if (svu_eq_ci_sv(object_id, cur_bin)) return true;
    return false;
}

static bool set_output_var_value(EvalExecContext *ctx, String_View out_var, String_View value) {
    return eval_var_set_current(ctx, out_var, value);
}

static bool set_output_var_notfound(EvalExecContext *ctx, String_View out_var) {
    String_View suffix = nob_sv_from_cstr("-NOTFOUND");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), out_var.count + suffix.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memcpy(buf, out_var.data, out_var.count);
    memcpy(buf + out_var.count, suffix.data, suffix.count);
    buf[out_var.count + suffix.count] = '\0';
    return eval_var_set_current(ctx, out_var, nob_sv_from_cstr(buf));
}

static bool set_output_var_literal_notfound(EvalExecContext *ctx, String_View out_var) {
    return eval_var_set_current(ctx, out_var, nob_sv_from_cstr("NOTFOUND"));
}

static String_View property_ascii_lower_temp(EvalExecContext *ctx, String_View value) {
    if (!ctx || value.count == 0) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), value.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < value.count; i++) {
        buf[i] = (char)tolower((unsigned char)value.data[i]);
    }
    buf[value.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static Event_Property_Mutate_Op property_mutate_op_from_legacy(Cmake_Target_Property_Op op) {
    switch (op) {
        case EV_PROP_SET: return EVENT_PROPERTY_MUTATE_SET;
        case EV_PROP_APPEND_LIST: return EVENT_PROPERTY_MUTATE_APPEND_LIST;
        case EV_PROP_APPEND_STRING: return EVENT_PROPERTY_MUTATE_APPEND_STRING;
        case EV_PROP_PREPEND_LIST: return EVENT_PROPERTY_MUTATE_PREPEND_LIST;
    }
    return EVENT_PROPERTY_MUTATE_SET;
}

static bool emit_property_write_semantic_event(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               String_View scope_upper,
                                               String_View object_id,
                                               String_View property_upper,
                                               String_View value,
                                               Cmake_Target_Property_Op op) {
    if (!ctx) return false;

    bool is_directory_scope =
        eval_sv_eq_ci_lit(scope_upper, "DIRECTORY") && is_current_directory_object(ctx, object_id);
    bool is_global_scope = eval_sv_eq_ci_lit(scope_upper, "GLOBAL");
    if (!is_directory_scope && !is_global_scope) return true;

    SV_List items = {0};
    if (value.count > 0 && !eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), value, &items)) return false;

    Event_Property_Mutate_Op semantic_op = property_mutate_op_from_legacy(op);
    if (is_directory_scope) {
        return eval_emit_directory_property_mutate(ctx,
                                                   origin,
                                                   property_upper,
                                                   semantic_op,
                                                   EVENT_PROPERTY_MODIFIER_NONE,
                                                   items,
                                                   arena_arr_len(items));
    }

    return eval_emit_global_property_mutate(ctx,
                                            origin,
                                            property_upper,
                                            semantic_op,
                                            EVENT_PROPERTY_MODIFIER_NONE,
                                            items,
                                            arena_arr_len(items));
}

static bool target_get_declared_dir_temp(EvalExecContext *ctx, String_View target_name, String_View *out_dir) {
    if (!ctx || !out_dir) return false;
    return eval_target_declared_dir(ctx, target_name, out_dir);
}

static bool property_append_unique_temp(EvalExecContext *ctx, SV_List *list, String_View value);

typedef struct {
    bool has_directory_scope;
    String_View directory;
    String_View object_name;
} Property_Directory_Scoped_Object;

static String_View property_bool_value(bool value) {
    return value ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
}

static String_View property_target_type_value(Cmake_Target_Type target_type) {
    switch (target_type) {
        case EV_TARGET_EXECUTABLE:
            return nob_sv_from_cstr("EXECUTABLE");
        case EV_TARGET_LIBRARY_STATIC:
            return nob_sv_from_cstr("STATIC_LIBRARY");
        case EV_TARGET_LIBRARY_SHARED:
            return nob_sv_from_cstr("SHARED_LIBRARY");
        case EV_TARGET_LIBRARY_MODULE:
            return nob_sv_from_cstr("MODULE_LIBRARY");
        case EV_TARGET_LIBRARY_INTERFACE:
            return nob_sv_from_cstr("INTERFACE_LIBRARY");
        case EV_TARGET_LIBRARY_OBJECT:
            return nob_sv_from_cstr("OBJECT_LIBRARY");
        case EV_TARGET_LIBRARY_UNKNOWN:
        default:
            return nob_sv_from_cstr("UNKNOWN_LIBRARY");
    }
}

static bool property_parse_directory_scoped_object(String_View object_id,
                                                   Property_Directory_Scoped_Object *out_parts) {
    static const char prefix[] = "DIRECTORY::";
    if (!out_parts) return false;
    memset(out_parts, 0, sizeof(*out_parts));
    if (object_id.count <= (sizeof(prefix) - 1)) return true;
    if (memcmp(object_id.data, prefix, sizeof(prefix) - 1) != 0) return true;

    size_t rest_start = sizeof(prefix) - 1;
    for (size_t i = rest_start; i + 1 < object_id.count; i++) {
        if (object_id.data[i] != ':' || object_id.data[i + 1] != ':') continue;
        out_parts->has_directory_scope = true;
        out_parts->directory = nob_sv_from_parts(object_id.data + rest_start, i - rest_start);
        out_parts->object_name = nob_sv_from_parts(object_id.data + i + 2, object_id.count - (i + 2));
        return true;
    }

    return true;
}

static const Eval_Directory_Node *property_directory_node_const(EvalExecContext *ctx, String_View source_dir) {
    if (!ctx || source_dir.count == 0) return NULL;
    Eval_Directory_Graph *graph = &ctx->semantic_state.directories;
    for (size_t i = 0; i < arena_arr_len(graph->nodes); i++) {
        if (svu_eq_ci_sv(graph->nodes[i].source_dir, source_dir)) return &graph->nodes[i];
    }
    return NULL;
}

static bool property_selected_directory_is_current(EvalExecContext *ctx, String_View source_dir) {
    if (!ctx || source_dir.count == 0) return false;
    String_View current = eval_current_source_dir_for_paths(ctx);
    if (current.count == 0) return false;
    current = eval_sv_path_normalize_temp(ctx, current);
    if (eval_should_stop(ctx)) return false;
    return svu_eq_ci_sv(current, source_dir);
}

static bool property_collect_current_macro_names_temp(EvalExecContext *ctx, SV_List *out_names) {
    if (!ctx || !out_names) return false;
    *out_names = NULL;

    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = 0; i < arena_arr_len(commands->user_commands); i++) {
        if (commands->user_commands[i].kind != USER_CMD_MACRO) continue;
        if (!property_append_unique_temp(ctx, out_names, commands->user_commands[i].name)) return false;
    }

    return true;
}

static bool property_collect_current_listfile_stack_temp(EvalExecContext *ctx, SV_List *out_stack) {
    if (!ctx || !out_stack) return false;
    *out_stack = NULL;

    for (size_t i = 0; i < arena_arr_len(ctx->exec_contexts); i++) {
        const Eval_Exec_Context *exec = &ctx->exec_contexts[i];
        if (!exec->current_file) continue;
        if (!property_append_unique_temp(ctx, out_stack, nob_sv_from_cstr(exec->current_file))) return false;
    }

    if (arena_arr_len(*out_stack) == 0 && ctx->current_file) {
        if (!property_append_unique_temp(ctx, out_stack, nob_sv_from_cstr(ctx->current_file))) return false;
    }
    return true;
}

static bool property_collect_directory_variables_temp(EvalExecContext *ctx,
                                                      String_View source_dir,
                                                      SV_List *out_names) {
    if (!ctx || !out_names) return false;
    *out_names = NULL;

    if (property_selected_directory_is_current(ctx, source_dir)) {
        SV_List visible_names = NULL;
        if (!eval_var_collect_visible_names(ctx, &visible_names)) return false;
        for (size_t i = 0; i < arena_arr_len(visible_names); i++) {
            if (!property_append_unique_temp(ctx, out_names, visible_names[i])) return false;
        }
        return true;
    }

    const Eval_Directory_Node *node = property_directory_node_const(ctx, source_dir);
    if (!node) return true;
    for (size_t i = 0; i < arena_arr_len(node->definition_bindings); i++) {
        if (!property_append_unique_temp(ctx, out_names, node->definition_bindings[i].key)) return false;
    }
    return true;
}

static bool property_collect_directory_macros_temp(EvalExecContext *ctx,
                                                   String_View source_dir,
                                                   SV_List *out_names) {
    if (!ctx || !out_names) return false;
    *out_names = NULL;

    if (property_selected_directory_is_current(ctx, source_dir)) {
        return property_collect_current_macro_names_temp(ctx, out_names);
    }

    const Eval_Directory_Node *node = property_directory_node_const(ctx, source_dir);
    if (!node) return true;
    for (size_t i = 0; i < arena_arr_len(node->macro_names); i++) {
        if (!property_append_unique_temp(ctx, out_names, node->macro_names[i])) return false;
    }
    return true;
}

static bool property_collect_directory_listfile_stack_temp(EvalExecContext *ctx,
                                                           String_View source_dir,
                                                           SV_List *out_stack) {
    if (!ctx || !out_stack) return false;
    *out_stack = NULL;

    if (property_selected_directory_is_current(ctx, source_dir)) {
        return property_collect_current_listfile_stack_temp(ctx, out_stack);
    }

    const Eval_Directory_Node *node = property_directory_node_const(ctx, source_dir);
    if (!node) return true;
    for (size_t i = 0; i < arena_arr_len(node->listfile_stack); i++) {
        if (!property_append_unique_temp(ctx, out_stack, node->listfile_stack[i])) return false;
    }
    return true;
}

static bool property_collect_directory_subdirs_temp(EvalExecContext *ctx,
                                                    String_View source_dir,
                                                    SV_List *out_names) {
    if (!ctx || !out_names) return false;
    *out_names = NULL;

    Eval_Directory_Graph *graph = &ctx->semantic_state.directories;
    for (size_t i = 0; i < arena_arr_len(graph->nodes); i++) {
        const Eval_Directory_Node *node = &graph->nodes[i];
        if (!svu_eq_ci_sv(node->parent_source_dir, source_dir)) continue;
        if (!property_append_unique_temp(ctx, out_names, node->source_dir)) return false;
    }
    return true;
}

static bool property_collect_directory_targets_temp(EvalExecContext *ctx,
                                                    String_View source_dir,
                                                    bool imported_only,
                                                    SV_List *out_names) {
    if (!ctx || !out_names) return false;
    *out_names = NULL;

    const Eval_Directory_Node *node = property_directory_node_const(ctx, source_dir);
    if (!node) return true;
    for (size_t i = 0; i < arena_arr_len(node->declared_targets); i++) {
        String_View target_name = node->declared_targets[i];
        bool imported = eval_target_is_imported(ctx, target_name);
        bool alias = eval_target_alias_known(ctx, target_name);
        if (imported_only) {
            if (!imported || alias) continue;
        } else {
            if (imported || alias) continue;
        }
        if (!property_append_unique_temp(ctx, out_names, target_name)) return false;
    }
    return true;
}

static bool property_collect_directory_tests_temp(EvalExecContext *ctx,
                                                  String_View source_dir,
                                                  SV_List *out_names) {
    if (!ctx || !out_names) return false;
    *out_names = NULL;

    const Eval_Directory_Node *node = property_directory_node_const(ctx, source_dir);
    if (!node) return true;
    for (size_t i = 0; i < arena_arr_len(node->declared_tests); i++) {
        if (!property_append_unique_temp(ctx, out_names, node->declared_tests[i])) return false;
    }
    return true;
}

static bool property_resolve_source_object_temp(EvalExecContext *ctx,
                                                String_View object_id,
                                                String_View inherit_directory,
                                                String_View *out_source_name,
                                                String_View *out_source_path) {
    if (out_source_name) *out_source_name = nob_sv_from_cstr("");
    if (out_source_path) *out_source_path = nob_sv_from_cstr("");
    if (!ctx) return false;

    Property_Directory_Scoped_Object parts = {0};
    if (!property_parse_directory_scoped_object(object_id, &parts)) return false;

    String_View source_name = parts.has_directory_scope ? parts.object_name : object_id;
    String_View base_dir = parts.has_directory_scope
        ? parts.directory
        : (inherit_directory.count > 0 ? inherit_directory : eval_current_source_dir_for_paths(ctx));

    String_View source_path = eval_path_resolve_for_cmake_arg(ctx, source_name, base_dir, true);
    if (eval_should_stop(ctx)) return false;
    source_path = eval_sv_path_normalize_temp(ctx, source_path);
    if (eval_should_stop(ctx)) return false;

    if (out_source_name) *out_source_name = source_name;
    if (out_source_path) *out_source_path = source_path;
    return true;
}

static bool property_source_generated_set_temp(EvalExecContext *ctx,
                                               String_View canonical_source_path,
                                               bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return false;

    Eval_Property_Engine *engine = &ctx->semantic_state.properties;
    for (size_t i = 0; i < arena_arr_len(engine->records); i++) {
        const Eval_Property_Record *record = &engine->records[i];
        if (!eval_sv_eq_ci_lit(record->scope_upper, "SOURCE")) continue;
        if (!eval_sv_eq_ci_lit(record->property_upper, "GENERATED")) continue;

        Property_Directory_Scoped_Object parts = {0};
        if (!property_parse_directory_scoped_object(record->object_id, &parts)) return false;

        String_View candidate_name = record->object_id;
        String_View candidate_base = eval_current_source_dir_for_paths(ctx);
        if (parts.has_directory_scope) {
            candidate_name = parts.object_name;
            candidate_base = parts.directory;
        } else if (eval_sv_is_abs_path(record->object_id)) {
            candidate_base = nob_sv_from_cstr("");
        }

        String_View candidate_path = parts.has_directory_scope || eval_sv_is_abs_path(candidate_name)
            ? candidate_name
            : eval_path_resolve_for_cmake_arg(ctx, candidate_name, candidate_base, true);
        if (eval_should_stop(ctx)) return false;
        if (!eval_sv_is_abs_path(candidate_path)) {
            candidate_path = eval_path_resolve_for_cmake_arg(ctx,
                                                             candidate_path,
                                                             eval_current_source_dir_for_paths(ctx),
                                                             true);
            if (eval_should_stop(ctx)) return false;
        }
        candidate_path = eval_sv_path_normalize_temp(ctx, candidate_path);
        if (eval_should_stop(ctx)) return false;
        if (!svu_eq_ci_sv(candidate_path, canonical_source_path)) continue;

        if (out_set) *out_set = eval_truthy(ctx, record->value);
        return true;
    }

    return true;
}

static bool property_synthetic_value_temp(EvalExecContext *ctx,
                                          String_View scope_upper,
                                          String_View object_id,
                                          String_View prop_upper,
                                          String_View inherit_directory,
                                          String_View *out_value,
                                          bool *out_set,
                                          bool *out_known) {
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (out_set) *out_set = false;
    if (out_known) *out_known = false;
    if (!ctx) return false;

    if (eval_sv_eq_ci_lit(scope_upper, "GLOBAL")) {
        if (eval_sv_eq_ci_lit(prop_upper, "CMAKE_ROLE")) {
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) {
                if (arena_arr_len(ctx->semantic_state.package.active_find_packages) > 0) {
                    *out_value = nob_sv_from_cstr("FIND_PACKAGE");
                } else if (ctx->mode == EVAL_EXEC_MODE_SCRIPT) {
                    *out_value = nob_sv_from_cstr("SCRIPT");
                } else if (ctx->mode == EVAL_EXEC_MODE_CTEST_SCRIPT) {
                    *out_value = nob_sv_from_cstr("CTEST");
                } else {
                    *out_value = nob_sv_from_cstr("PROJECT");
                }
            }
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "IN_TRY_COMPILE")) {
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = property_bool_value(false);
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "GENERATOR_IS_MULTI_CONFIG")) {
            String_View config_types = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CONFIGURATION_TYPES"));
            bool is_multi_config = config_types.count > 0 && eval_truthy(ctx, config_types);
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = property_bool_value(is_multi_config);
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "PACKAGES_FOUND") ||
            eval_sv_eq_ci_lit(prop_upper, "PACKAGES_NOT_FOUND")) {
            Eval_Package_Model *package = &ctx->semantic_state.package;
            SV_List values =
                eval_sv_eq_ci_lit(prop_upper, "PACKAGES_FOUND")
                    ? package->found_packages
                    : package->not_found_packages;
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = eval_sv_join_semi_temp(ctx, values, arena_arr_len(values));
            return true;
        }
    }

    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) {
        const Eval_Directory_Node *node = property_directory_node_const(ctx, object_id);
        if (eval_sv_eq_ci_lit(prop_upper, "SOURCE_DIR")) {
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = object_id;
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "BINARY_DIR")) {
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = node ? node->binary_dir : nob_sv_from_cstr("");
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "PARENT_DIRECTORY")) {
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = node ? node->parent_source_dir : nob_sv_from_cstr("");
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "SUBDIRECTORIES") ||
            eval_sv_eq_ci_lit(prop_upper, "BUILDSYSTEM_TARGETS") ||
            eval_sv_eq_ci_lit(prop_upper, "IMPORTED_TARGETS") ||
            eval_sv_eq_ci_lit(prop_upper, "TESTS") ||
            eval_sv_eq_ci_lit(prop_upper, "VARIABLES") ||
            eval_sv_eq_ci_lit(prop_upper, "MACROS") ||
            eval_sv_eq_ci_lit(prop_upper, "LISTFILE_STACK")) {
            SV_List values = NULL;
            if (eval_sv_eq_ci_lit(prop_upper, "SUBDIRECTORIES")) {
                if (!property_collect_directory_subdirs_temp(ctx, object_id, &values)) return false;
            } else if (eval_sv_eq_ci_lit(prop_upper, "BUILDSYSTEM_TARGETS")) {
                if (!property_collect_directory_targets_temp(ctx, object_id, false, &values)) return false;
            } else if (eval_sv_eq_ci_lit(prop_upper, "IMPORTED_TARGETS")) {
                if (!property_collect_directory_targets_temp(ctx, object_id, true, &values)) return false;
            } else if (eval_sv_eq_ci_lit(prop_upper, "TESTS")) {
                if (!property_collect_directory_tests_temp(ctx, object_id, &values)) return false;
            } else if (eval_sv_eq_ci_lit(prop_upper, "VARIABLES")) {
                if (!property_collect_directory_variables_temp(ctx, object_id, &values)) return false;
            } else if (eval_sv_eq_ci_lit(prop_upper, "MACROS")) {
                if (!property_collect_directory_macros_temp(ctx, object_id, &values)) return false;
            } else {
                if (!property_collect_directory_listfile_stack_temp(ctx, object_id, &values)) return false;
            }

            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = eval_sv_join_semi_temp(ctx, values, arena_arr_len(values));
            return true;
        }
    }

    if (eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
        if (eval_sv_eq_ci_lit(prop_upper, "TYPE")) {
            Cmake_Target_Type target_type = EV_TARGET_LIBRARY_UNKNOWN;
            if (!eval_target_get_type(ctx, object_id, &target_type)) return false;
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = property_target_type_value(target_type);
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "IMPORTED")) {
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = property_bool_value(eval_target_is_imported(ctx, object_id));
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "IMPORTED_GLOBAL")) {
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = property_bool_value(eval_target_is_imported_global(ctx, object_id));
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "ALIASED_TARGET")) {
            if (out_known) *out_known = true;
            if (!eval_target_alias_known(ctx, object_id)) return true;
            if (out_set) *out_set = true;
            if (out_value && !eval_target_alias_of(ctx, object_id, out_value)) return false;
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "ALIAS_GLOBAL")) {
            if (out_known) *out_known = true;
            if (!eval_target_alias_known(ctx, object_id)) return true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = property_bool_value(eval_target_alias_is_global(ctx, object_id));
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "SOURCE_DIR") || eval_sv_eq_ci_lit(prop_upper, "BINARY_DIR")) {
            String_View declared_dir = nob_sv_from_cstr("");
            String_View binary_dir = nob_sv_from_cstr("");
            if (!target_get_declared_dir_temp(ctx, object_id, &declared_dir)) return false;
            if (eval_should_stop(ctx)) return false;
            if (!eval_directory_binary_dir(ctx, declared_dir, &binary_dir)) return false;
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) {
                *out_value = eval_sv_eq_ci_lit(prop_upper, "SOURCE_DIR") ? declared_dir : binary_dir;
            }
            return true;
        }
    }

    if (eval_sv_eq_ci_lit(scope_upper, "SOURCE")) {
        String_View source_name = nob_sv_from_cstr("");
        String_View source_path = nob_sv_from_cstr("");
        if (!property_resolve_source_object_temp(ctx,
                                                 object_id,
                                                 inherit_directory,
                                                 &source_name,
                                                 &source_path)) {
            return false;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "LOCATION")) {
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = source_path;
            return true;
        }
        if (eval_sv_eq_ci_lit(prop_upper, "GENERATED")) {
            bool generated = false;
            if (!property_source_generated_set_temp(ctx, source_path, &generated)) return false;
            if (out_known) *out_known = true;
            if (!generated) return true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = property_bool_value(true);
            return true;
        }
        (void)source_name;
    }

    if (eval_sv_eq_ci_lit(scope_upper, "TEST")) {
        if (eval_sv_eq_ci_lit(prop_upper, "WORKING_DIRECTORY")) {
            Property_Directory_Scoped_Object parts = {0};
            if (!property_parse_directory_scoped_object(object_id, &parts)) return false;
            String_View test_name = parts.has_directory_scope ? parts.object_name : object_id;
            String_View test_dir = parts.has_directory_scope
                ? parts.directory
                : (inherit_directory.count > 0 ? inherit_directory : eval_current_source_dir_for_paths(ctx));
            String_View working_directory = nob_sv_from_cstr("");
            if (!eval_test_working_directory(ctx, test_name, test_dir, &working_directory)) return false;
            if (out_known) *out_known = true;
            if (out_set) *out_set = true;
            if (out_value) *out_value = working_directory;
            return true;
        }
    }

    return true;
}

static bool property_append_unique_temp(EvalExecContext *ctx, SV_List *list, String_View value) {
    if (!ctx || !list) return false;
    if (value.count == 0) return true;
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        if (eval_sv_key_eq((*list)[i], value)) return true;
    }
    return svu_list_push_temp(ctx, list, value);
}

static String_View eval_property_store_key_temp(EvalExecContext *ctx,
                                                String_View scope_upper,
                                                String_View object_id,
                                                String_View prop_upper) {
    static const char prefix[] = "NOBIFY_PROPERTY_";
    if (!ctx) return nob_sv_from_cstr("");

    bool has_obj = object_id.count > 0;
    size_t total = (sizeof(prefix) - 1) + scope_upper.count + 2 + prop_upper.count;
    if (has_obj) total += 2 + object_id.count;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, sizeof(prefix) - 1);
    off += sizeof(prefix) - 1;
    memcpy(buf + off, scope_upper.data, scope_upper.count);
    off += scope_upper.count;
    buf[off++] = ':';
    buf[off++] = ':';
    if (has_obj) {
        memcpy(buf + off, object_id.data, object_id.count);
        off += object_id.count;
        buf[off++] = ':';
        buf[off++] = ':';
    }
    memcpy(buf + off, prop_upper.data, prop_upper.count);
    off += prop_upper.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static Eval_Property_Record *property_engine_record(EvalExecContext *ctx,
                                                    String_View scope_upper,
                                                    String_View object_id,
                                                    String_View property_upper) {
    if (!ctx || scope_upper.count == 0 || property_upper.count == 0) return NULL;
    Eval_Property_Engine *engine = &ctx->semantic_state.properties;
    for (size_t i = 0; i < arena_arr_len(engine->records); i++) {
        Eval_Property_Record *record = &engine->records[i];
        if (!eval_sv_key_eq(record->scope_upper, scope_upper)) continue;
        if (!eval_sv_key_eq(record->object_id, object_id)) continue;
        if (!eval_sv_key_eq(record->property_upper, property_upper)) continue;
        return record;
    }
    return NULL;
}

bool eval_property_engine_set(EvalExecContext *ctx,
                              String_View scope_upper,
                              String_View object_id,
                              String_View property_upper,
                              String_View value) {
    if (!ctx || scope_upper.count == 0 || property_upper.count == 0) return false;

    String_View stable_value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;

    Eval_Property_Record *record = property_engine_record(ctx, scope_upper, object_id, property_upper);
    if (record) {
        record->value = stable_value;
        return true;
    }

    Eval_Property_Record new_record = {0};
    new_record.scope_upper = sv_copy_to_event_arena(ctx, scope_upper);
    new_record.object_id = sv_copy_to_event_arena(ctx, object_id);
    new_record.property_upper = sv_copy_to_event_arena(ctx, property_upper);
    new_record.value = stable_value;
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->semantic_state.properties.records, new_record);
}

bool eval_property_engine_get(EvalExecContext *ctx,
                              String_View scope_upper,
                              String_View object_id,
                              String_View property_upper,
                              String_View *out_value,
                              bool *out_set) {
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (out_set) *out_set = false;
    if (!ctx || scope_upper.count == 0 || property_upper.count == 0) return false;

    Eval_Property_Record *record = property_engine_record(ctx, scope_upper, object_id, property_upper);
    if (!record) return true;
    if (out_value) *out_value = record->value;
    if (out_set) *out_set = true;
    return true;
}

static const Eval_Property_Definition *eval_property_definition_find_impl(EvalExecContext *ctx,
                                                                          String_View scope_upper,
                                                                          String_View property_upper) {
    if (!ctx) return NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->property_definitions); i++) {
        const Eval_Property_Definition *def = &ctx->property_definitions[i];
        if (!eval_sv_key_eq(def->scope_upper, scope_upper)) continue;
        if (!eval_sv_key_eq(def->property_upper, property_upper)) continue;
        return def;
    }
    return NULL;
}

const Eval_Property_Definition *eval_property_definition_find(EvalExecContext *ctx,
                                                              String_View scope_upper,
                                                              String_View property_name) {
    if (!ctx) return NULL;
    String_View property_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return NULL;

    const Eval_Property_Definition *def =
        eval_property_definition_find_impl(ctx, scope_upper, property_upper);
    if (def) return def;

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
        return eval_property_definition_find_impl(ctx, nob_sv_from_cstr("CACHED_VARIABLE"), property_upper);
    }
    return NULL;
}

bool eval_property_define(EvalExecContext *ctx, const Eval_Property_Definition *definition) {
    if (!ctx || !definition) return false;
    if (eval_property_definition_find_impl(ctx, definition->scope_upper, definition->property_upper)) {
        return true;
    }

    Eval_Property_Definition stored = *definition;
    stored.scope_upper = sv_copy_to_event_arena(ctx, definition->scope_upper);
    stored.property_upper = sv_copy_to_event_arena(ctx, definition->property_upper);
    if (definition->has_brief_docs) stored.brief_docs = sv_copy_to_event_arena(ctx, definition->brief_docs);
    if (definition->has_full_docs) stored.full_docs = sv_copy_to_event_arena(ctx, definition->full_docs);
    if (definition->has_initialize_from_variable) {
        stored.initialize_from_variable = sv_copy_to_event_arena(ctx, definition->initialize_from_variable);
    }
    if (eval_should_stop(ctx)) return false;

    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->property_definitions, stored);
}

bool eval_property_is_defined(EvalExecContext *ctx, String_View scope_upper, String_View property_name) {
    if (!ctx) return false;
    String_View property_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return false;
    return eval_property_definition_find_impl(ctx, scope_upper, property_upper) != NULL;
}

bool eval_target_apply_defined_initializers(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View target_name) {
    if (!ctx || eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(ctx->property_definitions); i++) {
        const Eval_Property_Definition *def = &ctx->property_definitions[i];
        if (!eval_sv_eq_ci_lit(def->scope_upper, "TARGET")) continue;
        if (!def->has_initialize_from_variable) continue;
        if (!eval_var_defined_visible(ctx, def->initialize_from_variable)) continue;
        if (!eval_emit_target_prop_set(ctx,
                                       origin,
                                       target_name,
                                       def->property_upper,
                                       eval_var_get_visible(ctx, def->initialize_from_variable),
                                       EV_PROP_SET)) {
            return false;
        }
    }

    if (eval_should_stop(ctx)) return false;

    return true;
}

bool eval_property_write(EvalExecContext *ctx,
                         Event_Origin origin,
                         String_View scope_upper,
                         String_View object_id,
                         String_View property_name,
                         String_View value,
                         Cmake_Target_Property_Op op,
                         bool emit_var_event) {
    if (!ctx) return false;

    String_View prop_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return false;

    String_View store_key = eval_property_store_key_temp(ctx, scope_upper, object_id, prop_upper);
    if (eval_should_stop(ctx)) return false;

    String_View current = nob_sv_from_cstr("");
    if (!eval_property_engine_get(ctx, scope_upper, object_id, prop_upper, &current, NULL)) return false;
    String_View merged = merge_property_value_temp(ctx, current, value, op);
    if (eval_should_stop(ctx)) return false;

    if (!emit_property_write_semantic_event(ctx, origin, scope_upper, object_id, prop_upper, value, op)) {
        return false;
    }

    if (!eval_property_engine_set(ctx, scope_upper, object_id, prop_upper, merged)) return false;
    if (!eval_var_set_current(ctx, store_key, merged)) return false;
    if (emit_var_event && !eval_emit_var_set_current(ctx, origin, store_key, merged)) return false;

    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY") && is_current_directory_object(ctx, object_id)) {
        if (eval_sv_eq_ci_lit(prop_upper, "COMPILE_OPTIONS")) {
            String_View cur = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_opts_var));
            String_View next = merge_property_value_temp(ctx, cur, value, op);
            if (eval_should_stop(ctx)) return false;
            if (!eval_var_set_current(ctx, nob_sv_from_cstr(k_global_opts_var), next)) return false;
        } else if (eval_sv_eq_ci_lit(prop_upper, "COMPILE_DEFINITIONS")) {
            String_View cur = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_defs_var));
            String_View next = merge_property_value_temp(ctx, cur, value, op);
            if (eval_should_stop(ctx)) return false;
            if (!eval_var_set_current(ctx, nob_sv_from_cstr(k_global_defs_var), next)) return false;
        } else if (eval_sv_eq_ci_lit(prop_upper, "LINK_OPTIONS")) {
            String_View cur = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_link_opts_var));
            String_View next = merge_property_value_temp(ctx, cur, value, op);
            if (eval_should_stop(ctx)) return false;
            if (!eval_var_set_current(ctx, nob_sv_from_cstr(k_global_link_opts_var), next)) return false;
        }
    }

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE") && eval_sv_eq_ci_lit(prop_upper, "VALUE")) {
        if (!eval_var_set_current(ctx, object_id, merged)) return false;
        if (!eval_emit_var_set_cache(ctx, origin, object_id, merged)) return false;
    }

    return true;
}

static bool property_parent_directory_temp(EvalExecContext *ctx,
                                           String_View dir,
                                           String_View *out_parent) {
    if (!ctx || !out_parent) return false;
    *out_parent = nob_sv_from_cstr("");
    return eval_directory_parent(ctx, dir, out_parent);
}

static String_View property_value_from_store_temp(EvalExecContext *ctx,
                                                  String_View scope_upper,
                                                  String_View object_id,
                                                  String_View prop_upper,
                                                  bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE") &&
        eval_sv_eq_ci_lit(prop_upper, "VALUE") &&
        eval_cache_defined(ctx, object_id)) {
        if (out_set) *out_set = true;
        return eval_var_get_visible(ctx, object_id);
    }

    String_View value = nob_sv_from_cstr("");
    bool have = false;
    if (!eval_property_engine_get(ctx, scope_upper, object_id, prop_upper, &value, &have)) {
        return nob_sv_from_cstr("");
    }
    if (out_set) *out_set = have;
    return have ? value : nob_sv_from_cstr("");
}

static String_View property_value_from_directory_chain_temp(EvalExecContext *ctx,
                                                            String_View start_dir,
                                                            String_View prop_upper,
                                                            bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View cur = start_dir;
    while (cur.count > 0) {
        bool have = false;
        String_View value = property_value_from_store_temp(ctx,
                                                           nob_sv_from_cstr("DIRECTORY"),
                                                           cur,
                                                           prop_upper,
                                                           &have);
        if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        if (have) {
            if (out_set) *out_set = true;
            return value;
        }

        String_View parent = nob_sv_from_cstr("");
        if (!property_parent_directory_temp(ctx, cur, &parent)) return nob_sv_from_cstr("");
        if (parent.count == 0 || eval_sv_key_eq(parent, cur)) break;
        cur = parent;
    }

    bool have_global = false;
    String_View value = property_value_from_store_temp(ctx,
                                                       nob_sv_from_cstr("GLOBAL"),
                                                       nob_sv_from_cstr(""),
                                                       prop_upper,
                                                       &have_global);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (have_global && out_set) *out_set = true;
    return have_global ? value : nob_sv_from_cstr("");
}

static String_View resolve_property_value_temp(EvalExecContext *ctx,
                                               String_View scope_upper,
                                               String_View object_id,
                                               String_View prop_name,
                                               bool allow_inherit,
                                               String_View inherit_directory,
                                               bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View prop_upper = eval_property_upper_name_temp(ctx, prop_name);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    bool have = false;
    String_View value = property_value_from_store_temp(ctx, scope_upper, object_id, prop_upper, &have);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (have) {
        if (out_set) *out_set = true;
        return value;
    }

    bool synthetic_known = false;
    String_View synthetic_value = nob_sv_from_cstr("");
    if (!property_synthetic_value_temp(ctx,
                                       scope_upper,
                                       object_id,
                                       prop_upper,
                                       inherit_directory,
                                       &synthetic_value,
                                       &have,
                                       &synthetic_known)) {
        return nob_sv_from_cstr("");
    }
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    (void)synthetic_known;
    if (have) {
        if (out_set) *out_set = true;
        return synthetic_value;
    }
    if (!allow_inherit) return nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) {
        return property_value_from_directory_chain_temp(ctx, object_id, prop_upper, out_set);
    }

    if (eval_sv_eq_ci_lit(scope_upper, "TARGET") ||
        eval_sv_eq_ci_lit(scope_upper, "SOURCE") ||
        eval_sv_eq_ci_lit(scope_upper, "TEST")) {
        String_View base_dir = inherit_directory;
        if (base_dir.count == 0 && eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
            if (!target_get_declared_dir_temp(ctx, object_id, &base_dir)) return nob_sv_from_cstr("");
            if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        }
        if (base_dir.count > 0) {
            return property_value_from_directory_chain_temp(ctx, base_dir, prop_upper, out_set);
        }
    }

    return nob_sv_from_cstr("");
}

static bool property_known_for_scope(EvalExecContext *ctx,
                                     String_View scope_upper,
                                     String_View object_id,
                                     String_View prop_name,
                                     String_View inherit_directory) {
    if (!ctx) return false;
    if (eval_property_definition_find(ctx, scope_upper, prop_name)) {
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_should_stop(ctx)) return false;

    String_View prop_upper = eval_property_upper_name_temp(ctx, prop_name);
    if (eval_should_stop(ctx)) return false;

    bool synthetic_have = false;
    bool synthetic_known = false;
    if (!property_synthetic_value_temp(ctx,
                                       scope_upper,
                                       object_id,
                                       prop_upper,
                                       inherit_directory,
                                       NULL,
                                       &synthetic_have,
                                       &synthetic_known)) {
        return false;
    }
    if (eval_should_stop(ctx)) return false;
    if (synthetic_known) return true;

    bool have = false;
    (void)resolve_property_value_temp(ctx,
                                      scope_upper,
                                      object_id,
                                      prop_name,
                                      false,
                                      inherit_directory,
                                      &have);
    if (eval_should_stop(ctx)) return false;
    return have;
}

bool eval_property_query_mode_parse(EvalExecContext *ctx,
                                    const Node *node,
                                    Event_Origin origin,
                                    String_View token,
                                    Eval_Property_Query_Mode *out_mode) {
    if (!ctx || !node || !out_mode) return false;
    if (eval_sv_eq_ci_lit(token, "SET")) {
        *out_mode = EVAL_PROP_QUERY_SET;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "DEFINED")) {
        *out_mode = EVAL_PROP_QUERY_DEFINED;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "BRIEF_DOCS")) {
        *out_mode = EVAL_PROP_QUERY_BRIEF_DOCS;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "FULL_DOCS")) {
        *out_mode = EVAL_PROP_QUERY_FULL_DOCS;
        return true;
    }

    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                   node,
                                   origin,
                                   EV_DIAG_ERROR,
                                   EVAL_DIAG_UNSUPPORTED_OPERATION,
                                   "dispatcher",
                                   nob_sv_from_cstr("get_property() received unsupported query mode"),
                                   token);
    return false;
}

bool eval_property_query(EvalExecContext *ctx,
                         const Node *node,
                         Event_Origin origin,
                         String_View out_var,
                         String_View scope_upper,
                         String_View object_id,
                         String_View validation_object,
                         String_View property_name,
                         Eval_Property_Query_Mode mode,
                         String_View inherit_directory,
                         bool validate_object,
                         Eval_Property_Query_Missing_Behavior missing_behavior) {
    if (!ctx || !node) return false;

    const Eval_Property_Definition *def =
        eval_property_definition_find(ctx, scope_upper, property_name);
    if (eval_should_stop(ctx)) return false;
    bool allow_inherit = def && def->inherited;

    if (mode == EVAL_PROP_QUERY_DEFINED) {
        return set_output_var_value(ctx,
                                    out_var,
                                    property_known_for_scope(ctx,
                                                             scope_upper,
                                                             object_id,
                                                             property_name,
                                                             inherit_directory)
                                        ? nob_sv_from_cstr("1")
                                        : nob_sv_from_cstr("0"));
    }

    if (mode == EVAL_PROP_QUERY_BRIEF_DOCS || mode == EVAL_PROP_QUERY_FULL_DOCS) {
        if (!def) return set_output_var_literal_notfound(ctx, out_var);
        if (mode == EVAL_PROP_QUERY_BRIEF_DOCS) {
            return set_output_var_value(ctx,
                                        out_var,
                                        def->has_brief_docs ? def->brief_docs : nob_sv_from_cstr(""));
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    def->has_full_docs ? def->full_docs : nob_sv_from_cstr(""));
    }

    String_View validated_name = validation_object.count > 0 ? validation_object : object_id;
    if (validate_object && (mode == EVAL_PROP_QUERY_VALUE || mode == EVAL_PROP_QUERY_SET)) {
        if (eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
            if (!eval_target_known(ctx, validated_name)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                               node,
                                               origin,
                                               EV_DIAG_ERROR,
                                               EVAL_DIAG_NOT_FOUND,
                                               "dispatcher",
                                               nob_sv_from_cstr("get_property(TARGET ...) target was not declared"),
                                               validated_name);
                if (eval_should_stop(ctx)) return false;
                return true;
            }
        } else if (eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
            if (!eval_cache_defined(ctx, validated_name)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                               node,
                                               origin,
                                               EV_DIAG_ERROR,
                                               EVAL_DIAG_NOT_FOUND,
                                               "dispatcher",
                                               nob_sv_from_cstr("get_property(CACHE ...) cache entry does not exist"),
                                               validated_name);
                if (eval_should_stop(ctx)) return false;
                return true;
            }
        } else if (eval_sv_eq_ci_lit(scope_upper, "TEST")) {
            String_View test_dir =
                inherit_directory.count > 0 ? inherit_directory : eval_current_source_dir_for_paths(ctx);
            if (!eval_test_exists_in_directory_scope(ctx, validated_name, test_dir)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
                    ctx,
                    node,
                    origin,
                    EV_DIAG_ERROR,
                    EVAL_DIAG_NOT_FOUND,
                    "dispatcher",
                    nob_sv_from_cstr("get_property(TEST ...) test was not declared in selected directory scope"),
                    validated_name);
                if (eval_should_stop(ctx)) return false;
                return true;
            }
        }
    }

    bool have = false;
    String_View value = resolve_property_value_temp(ctx,
                                                    scope_upper,
                                                    object_id,
                                                    property_name,
                                                    allow_inherit,
                                                    inherit_directory,
                                                    &have);
    if (eval_should_stop(ctx)) return false;

    if (mode == EVAL_PROP_QUERY_SET) {
        return set_output_var_value(ctx, out_var, have ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    }

    if (!have) {
        if (missing_behavior == EVAL_PROP_QUERY_MISSING_VAR_NOTFOUND) {
            return set_output_var_notfound(ctx, out_var);
        }
        if (missing_behavior == EVAL_PROP_QUERY_MISSING_LITERAL_NOTFOUND) {
            return set_output_var_literal_notfound(ctx, out_var);
        }
        return eval_var_unset_current(ctx, out_var);
    }
    return set_output_var_value(ctx, out_var, value);
}

bool eval_property_query_cmake(EvalExecContext *ctx,
                               const Node *node,
                               Event_Origin origin,
                               String_View out_var,
                               String_View property_name) {
    (void)node;
    (void)origin;
    if (!ctx) return false;

    SV_List values = NULL;
    if (eval_sv_eq_ci_lit(property_name, "VARIABLES")) {
        SV_List visible_names = NULL;
        if (!eval_var_collect_visible_names(ctx, &visible_names)) return false;
        for (size_t i = 0; i < arena_arr_len(visible_names); i++) {
            if (!property_append_unique_temp(ctx, &values, visible_names[i])) return false;
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(property_name, "CACHE_VARIABLES")) {
        ptrdiff_t n = stbds_shlen(ctx->scope_state.cache_entries);
        for (ptrdiff_t i = 0; i < n; i++) {
            if (!ctx->scope_state.cache_entries[i].key) continue;
            if (!property_append_unique_temp(ctx,
                                             &values,
                                             nob_sv_from_cstr(ctx->scope_state.cache_entries[i].key))) {
                return false;
            }
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(property_name, "MACROS")) {
        Eval_Command_State *commands = eval_command_slice(ctx);
        for (size_t i = 0; i < arena_arr_len(commands->user_commands); i++) {
            const User_Command *cmd = &commands->user_commands[i];
            if (cmd->kind != USER_CMD_MACRO) continue;
            if (!property_append_unique_temp(ctx, &values, cmd->name)) return false;
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(property_name, "COMMANDS")) {
        Eval_Command_State *commands = eval_command_slice(ctx);
        if (ctx->registry) {
            for (size_t i = 0; i < arena_arr_len(ctx->registry->native_commands); i++) {
                String_View lowered = property_ascii_lower_temp(ctx, ctx->registry->native_commands[i].name);
                if (eval_should_stop(ctx)) return false;
                if (!property_append_unique_temp(ctx, &values, lowered)) return false;
            }
        }
        for (size_t i = 0; i < arena_arr_len(commands->user_commands); i++) {
            String_View lowered = property_ascii_lower_temp(ctx, commands->user_commands[i].name);
            if (eval_should_stop(ctx)) return false;
            if (!property_append_unique_temp(ctx, &values, lowered)) return false;
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(property_name, "COMPONENTS")) {
        Eval_Install_Model *install = &ctx->semantic_state.install;
        for (size_t i = 0; i < arena_arr_len(install->components); i++) {
            if (!property_append_unique_temp(ctx, &values, install->components[i])) return false;
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (!eval_property_query(ctx,
                             node,
                             origin,
                             out_var,
                             nob_sv_from_cstr("GLOBAL"),
                             nob_sv_from_cstr(""),
                             nob_sv_from_cstr(""),
                             property_name,
                             EVAL_PROP_QUERY_VALUE,
                             nob_sv_from_cstr(""),
                             false,
                             EVAL_PROP_QUERY_MISSING_UNSET)) {
        return false;
    }
    if (!eval_var_defined_visible(ctx, out_var)) return set_output_var_literal_notfound(ctx, out_var);
    return true;
}
