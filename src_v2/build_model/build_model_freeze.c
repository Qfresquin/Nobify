#include "build_model_freeze.h"

#include "build_model_validate.h"

#include "stb_ds.h"

#include <string.h>

typedef struct {
    char *key;
    char *value;
} Build_Model_Freeze_Intern_Entry;

typedef struct {
    Arena *arena;
    Build_Model_Freeze_Intern_Entry *intern_map;
} Build_Model_Freeze_Ctx;

static String_View bm_freeze_intern(Build_Model_Freeze_Ctx *ctx, String_View sv) {
    if (!ctx || !ctx->arena || !sv.data || sv.count == 0) return sv_from_cstr("");

    Build_Model_Freeze_Intern_Entry *entry = stbds_shgetp_null(ctx->intern_map, nob_temp_sv_to_cstr(sv));
    if (entry) return sv_from_cstr(entry->value);

    char *owned = arena_strndup(ctx->arena, sv.data, sv.count);
    if (!owned) return sv_from_cstr("");

    stbds_shput(ctx->intern_map, owned, owned);
    return sv_from_cstr(owned);
}

static Logic_Node *bm_freeze_clone_logic_node(Build_Model_Freeze_Ctx *ctx, const Logic_Node *src) {
    if (!ctx || !src) return NULL;

    Logic_Node *dst = arena_alloc_zero(ctx->arena, sizeof(*dst));
    if (!dst) return NULL;

    dst->type = src->type;
    dst->left = bm_freeze_clone_logic_node(ctx, src->left);
    dst->right = bm_freeze_clone_logic_node(ctx, src->right);

    switch (src->type) {
        case LOGIC_OP_COMPARE:
            dst->as.cmp.op = src->as.cmp.op;
            dst->as.cmp.lhs.token = bm_freeze_intern(ctx, src->as.cmp.lhs.token);
            dst->as.cmp.lhs.quoted = src->as.cmp.lhs.quoted;
            dst->as.cmp.rhs.token = bm_freeze_intern(ctx, src->as.cmp.rhs.token);
            dst->as.cmp.rhs.quoted = src->as.cmp.rhs.quoted;
            break;

        case LOGIC_OP_BOOL:
        case LOGIC_OP_DEFINED:
            dst->as.operand.token = bm_freeze_intern(ctx, src->as.operand.token);
            dst->as.operand.quoted = src->as.operand.quoted;
            break;

        case LOGIC_OP_AND:
        case LOGIC_OP_OR:
        case LOGIC_OP_NOT:
        case LOGIC_OP_LITERAL_TRUE:
        case LOGIC_OP_LITERAL_FALSE:
            break;
    }

    return dst;
}

static bool bm_freeze_copy_string_list(Build_Model_Freeze_Ctx *ctx,
                                       const String_List *src,
                                       String_List *dst) {
    if (!dst) return false;
    memset(dst, 0, sizeof(*dst));
    if (!src || src->count == 0) return true;

    dst->items = arena_alloc_array(ctx->arena, String_View, src->count);
    if (!dst->items) return false;

    dst->count = src->count;
    dst->capacity = src->count;
    for (size_t i = 0; i < src->count; i++) {
        dst->items[i] = bm_freeze_intern(ctx, src->items[i]);
    }
    return true;
}

static bool bm_freeze_copy_property_list(Build_Model_Freeze_Ctx *ctx,
                                         const Property_List *src,
                                         Property_List *dst) {
    if (!dst) return false;
    memset(dst, 0, sizeof(*dst));
    if (!src || src->count == 0) return true;

    dst->items = arena_alloc_array(ctx->arena, Property, src->count);
    if (!dst->items) return false;

    dst->count = src->count;
    dst->capacity = src->count;
    for (size_t i = 0; i < src->count; i++) {
        dst->items[i].name = bm_freeze_intern(ctx, src->items[i].name);
        dst->items[i].value = bm_freeze_intern(ctx, src->items[i].value);
    }
    return true;
}

static void bm_freeze_build_property_index(const Property_List *list, Build_Property_Index_Entry **out_index) {
    if (!out_index) return;
    *out_index = NULL;
    if (!list || !list->items) return;

    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].name.count == 0 || !list->items[i].name.data) continue;
        stbds_shput(*out_index, (char*)list->items[i].name.data, (int)i);
    }
}

static bool bm_freeze_copy_conditional_list(Build_Model_Freeze_Ctx *ctx,
                                            const Conditional_Property_List *src,
                                            Conditional_Property_List *dst) {
    if (!dst) return false;
    memset(dst, 0, sizeof(*dst));
    if (!src || src->count == 0) return true;

    dst->items = arena_alloc_array(ctx->arena, Conditional_Property, src->count);
    if (!dst->items) return false;

    dst->count = src->count;
    dst->capacity = src->count;
    for (size_t i = 0; i < src->count; i++) {
        dst->items[i].value = bm_freeze_intern(ctx, src->items[i].value);
        dst->items[i].condition = bm_freeze_clone_logic_node(ctx, src->items[i].condition);
    }
    return true;
}

static bool bm_freeze_copy_custom_command(Build_Model_Freeze_Ctx *ctx,
                                          const Custom_Command *src,
                                          Custom_Command *dst) {
    if (!src || !dst) return false;
    memset(dst, 0, sizeof(*dst));

    dst->type = src->type;
    dst->command = bm_freeze_intern(ctx, src->command);
    if (!bm_freeze_copy_string_list(ctx, &src->commands, &dst->commands)) return false;
    dst->main_dependency = bm_freeze_intern(ctx, src->main_dependency);
    dst->depfile = bm_freeze_intern(ctx, src->depfile);
    dst->working_dir = bm_freeze_intern(ctx, src->working_dir);
    dst->comment = bm_freeze_intern(ctx, src->comment);
    dst->echo = src->echo;
    dst->append = src->append;
    dst->verbatim = src->verbatim;
    dst->uses_terminal = src->uses_terminal;
    dst->command_expand_lists = src->command_expand_lists;
    dst->depends_explicit_only = src->depends_explicit_only;
    dst->codegen = src->codegen;

    if (!bm_freeze_copy_string_list(ctx, &src->outputs, &dst->outputs)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->byproducts, &dst->byproducts)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->inputs, &dst->inputs)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->depends, &dst->depends)) return false;

    return true;
}

static bool bm_freeze_copy_custom_command_array(Build_Model_Freeze_Ctx *ctx,
                                                const Custom_Command *src_items,
                                                size_t src_count,
                                                Custom_Command **dst_items,
                                                size_t *dst_count,
                                                size_t *dst_capacity) {
    if (dst_items) *dst_items = NULL;
    if (dst_count) *dst_count = 0;
    if (dst_capacity) *dst_capacity = 0;
    if (!dst_items || !dst_count || !dst_capacity) return false;
    if (!src_items || src_count == 0) return true;

    Custom_Command *items = arena_alloc_array(ctx->arena, Custom_Command, src_count);
    if (!items) return false;
    memset(items, 0, sizeof(*items) * src_count);

    for (size_t i = 0; i < src_count; i++) {
        if (!bm_freeze_copy_custom_command(ctx, &src_items[i], &items[i])) return false;
    }

    *dst_items = items;
    *dst_count = src_count;
    *dst_capacity = src_count;
    return true;
}

static bool bm_freeze_copy_target(Build_Model_Freeze_Ctx *ctx,
                                  Build_Model *dst_model,
                                  const Build_Target *src,
                                  Build_Target *dst) {
    if (!ctx || !dst_model || !src || !dst) return false;
    memset(dst, 0, sizeof(*dst));

    dst->name = bm_freeze_intern(ctx, src->name);
    dst->type = src->type;
    dst->owner_model = dst_model;
    dst->owner_directory_index = src->owner_directory_index;

    dst->output_name = bm_freeze_intern(ctx, src->output_name);
    dst->output_directory = bm_freeze_intern(ctx, src->output_directory);
    dst->runtime_output_directory = bm_freeze_intern(ctx, src->runtime_output_directory);
    dst->archive_output_directory = bm_freeze_intern(ctx, src->archive_output_directory);
    dst->prefix = bm_freeze_intern(ctx, src->prefix);
    dst->suffix = bm_freeze_intern(ctx, src->suffix);

    dst->win32_executable = src->win32_executable;
    dst->macosx_bundle = src->macosx_bundle;
    dst->exclude_from_all = src->exclude_from_all;
    dst->imported = src->imported;
    dst->alias = src->alias;

    if (!bm_freeze_copy_string_list(ctx, &src->sources, &dst->sources)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->source_groups, &dst->source_groups)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->dependencies, &dst->dependencies)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->object_dependencies, &dst->object_dependencies)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->interface_dependencies, &dst->interface_dependencies)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->link_libraries, &dst->link_libraries)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->interface_libs, &dst->interface_libs)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->interface_compile_definitions, &dst->interface_compile_definitions)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->interface_compile_options, &dst->interface_compile_options)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->interface_include_directories, &dst->interface_include_directories)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->interface_link_options, &dst->interface_link_options)) return false;
    if (!bm_freeze_copy_string_list(ctx, &src->interface_link_directories, &dst->interface_link_directories)) return false;

    if (!bm_freeze_copy_conditional_list(ctx, &src->conditional_compile_definitions, &dst->conditional_compile_definitions)) return false;
    if (!bm_freeze_copy_conditional_list(ctx, &src->conditional_compile_options, &dst->conditional_compile_options)) return false;
    if (!bm_freeze_copy_conditional_list(ctx, &src->conditional_include_directories, &dst->conditional_include_directories)) return false;
    if (!bm_freeze_copy_conditional_list(ctx, &src->conditional_link_libraries, &dst->conditional_link_libraries)) return false;
    if (!bm_freeze_copy_conditional_list(ctx, &src->conditional_link_options, &dst->conditional_link_options)) return false;
    if (!bm_freeze_copy_conditional_list(ctx, &src->conditional_link_directories, &dst->conditional_link_directories)) return false;

    if (!bm_freeze_copy_property_list(ctx, &src->custom_properties, &dst->custom_properties)) return false;
    bm_freeze_build_property_index(&dst->custom_properties, &dst->custom_property_index);

    if (!bm_freeze_copy_custom_command_array(ctx,
                                             src->pre_build_commands,
                                             src->pre_build_count,
                                             &dst->pre_build_commands,
                                             &dst->pre_build_count,
                                             &dst->pre_build_capacity)) {
        return false;
    }
    if (!bm_freeze_copy_custom_command_array(ctx,
                                             src->post_build_commands,
                                             src->post_build_count,
                                             &dst->post_build_commands,
                                             &dst->post_build_count,
                                             &dst->post_build_capacity)) {
        return false;
    }

    return true;
}

static bool bm_freeze_copy_directory_nodes(Build_Model_Freeze_Ctx *ctx,
                                           const Build_Model *src_model,
                                           Build_Model *dst_model) {
    if (!ctx || !src_model || !dst_model) return false;
    dst_model->directory_node_count = src_model->directory_node_count;
    dst_model->directory_node_capacity = src_model->directory_node_count;
    dst_model->root_directory_index = src_model->root_directory_index;

    if (src_model->directory_node_count == 0) {
        dst_model->directory_nodes = NULL;
        return true;
    }

    dst_model->directory_nodes = arena_alloc_array(ctx->arena, Build_Directory_Node, src_model->directory_node_count);
    if (!dst_model->directory_nodes) return false;
    memset(dst_model->directory_nodes, 0, sizeof(*dst_model->directory_nodes) * src_model->directory_node_count);

    for (size_t i = 0; i < src_model->directory_node_count; i++) {
        const Build_Directory_Node *src = &src_model->directory_nodes[i];
        Build_Directory_Node *dst = &dst_model->directory_nodes[i];
        dst->index = src->index;
        dst->parent_index = src->parent_index;
        dst->source_dir = bm_freeze_intern(ctx, src->source_dir);
        dst->binary_dir = bm_freeze_intern(ctx, src->binary_dir);
        if (!bm_freeze_copy_string_list(ctx, &src->include_dirs, &dst->include_dirs)) return false;
        if (!bm_freeze_copy_string_list(ctx, &src->system_include_dirs, &dst->system_include_dirs)) return false;
        if (!bm_freeze_copy_string_list(ctx, &src->link_dirs, &dst->link_dirs)) return false;
    }

    return true;
}

static bool bm_freeze_copy_targets(Build_Model_Freeze_Ctx *ctx,
                                   const Build_Model *src_model,
                                   Build_Model *dst_model) {
    if (!ctx || !src_model || !dst_model) return false;

    dst_model->target_count = src_model->target_count;
    dst_model->target_capacity = src_model->target_count;
    if (src_model->target_count == 0) {
        dst_model->targets = NULL;
        dst_model->target_index_by_name = NULL;
        return true;
    }

    dst_model->targets = arena_alloc_array(ctx->arena, Build_Target*, src_model->target_count);
    if (!dst_model->targets) return false;
    memset(dst_model->targets, 0, sizeof(*dst_model->targets) * src_model->target_count);

    for (size_t i = 0; i < src_model->target_count; i++) {
        const Build_Target *src_target = src_model->targets[i];
        if (!src_target) continue;

        Build_Target *dst_target = arena_alloc_zero(ctx->arena, sizeof(*dst_target));
        if (!dst_target) return false;
        if (!bm_freeze_copy_target(ctx, dst_model, src_target, dst_target)) return false;

        dst_model->targets[i] = dst_target;
        if (dst_target->name.count > 0) {
            stbds_shput(dst_model->target_index_by_name, (char*)dst_target->name.data, (int)i);
        }
    }

    return true;
}

static bool bm_freeze_copy_found_packages(Build_Model_Freeze_Ctx *ctx,
                                          const Build_Model *src_model,
                                          Build_Model *dst_model) {
    if (!ctx || !src_model || !dst_model) return false;
    dst_model->package_count = src_model->package_count;
    dst_model->package_capacity = src_model->package_count;
    if (src_model->package_count == 0) return true;

    dst_model->found_packages = arena_alloc_array(ctx->arena, Found_Package, src_model->package_count);
    if (!dst_model->found_packages) return false;
    memset(dst_model->found_packages, 0, sizeof(*dst_model->found_packages) * src_model->package_count);

    for (size_t i = 0; i < src_model->package_count; i++) {
        const Found_Package *src = &src_model->found_packages[i];
        Found_Package *dst = &dst_model->found_packages[i];
        dst->name = bm_freeze_intern(ctx, src->name);
        dst->found = src->found;
        dst->version = bm_freeze_intern(ctx, src->version);

        if (!bm_freeze_copy_string_list(ctx, &src->include_dirs, &dst->include_dirs)) return false;
        if (!bm_freeze_copy_string_list(ctx, &src->libraries, &dst->libraries)) return false;
        if (!bm_freeze_copy_string_list(ctx, &src->definitions, &dst->definitions)) return false;
        if (!bm_freeze_copy_string_list(ctx, &src->options, &dst->options)) return false;
        if (!bm_freeze_copy_property_list(ctx, &src->properties, &dst->properties)) return false;
    }
    return true;
}

static bool bm_freeze_copy_tests(Build_Model_Freeze_Ctx *ctx,
                                 const Build_Model *src_model,
                                 Build_Model *dst_model) {
    if (!ctx || !src_model || !dst_model) return false;
    dst_model->test_count = src_model->test_count;
    dst_model->test_capacity = src_model->test_count;
    if (src_model->test_count == 0) return true;

    dst_model->tests = arena_alloc_array(ctx->arena, Build_Test, src_model->test_count);
    if (!dst_model->tests) return false;
    memset(dst_model->tests, 0, sizeof(*dst_model->tests) * src_model->test_count);

    for (size_t i = 0; i < src_model->test_count; i++) {
        const Build_Test *src = &src_model->tests[i];
        Build_Test *dst = &dst_model->tests[i];
        dst->name = bm_freeze_intern(ctx, src->name);
        dst->command = bm_freeze_intern(ctx, src->command);
        dst->working_directory = bm_freeze_intern(ctx, src->working_directory);
        dst->command_expand_lists = src->command_expand_lists;
    }
    return true;
}

static bool bm_freeze_copy_cpack(Build_Model_Freeze_Ctx *ctx,
                                 const Build_Model *src_model,
                                 Build_Model *dst_model) {
    if (!ctx || !src_model || !dst_model) return false;

    dst_model->cpack_component_group_count = src_model->cpack_component_group_count;
    dst_model->cpack_component_group_capacity = src_model->cpack_component_group_count;
    if (src_model->cpack_component_group_count > 0) {
        dst_model->cpack_component_groups = arena_alloc_array(ctx->arena, CPack_Component_Group, src_model->cpack_component_group_count);
        if (!dst_model->cpack_component_groups) return false;
        memset(dst_model->cpack_component_groups, 0, sizeof(*dst_model->cpack_component_groups) * src_model->cpack_component_group_count);
        for (size_t i = 0; i < src_model->cpack_component_group_count; i++) {
            const CPack_Component_Group *src = &src_model->cpack_component_groups[i];
            CPack_Component_Group *dst = &dst_model->cpack_component_groups[i];
            dst->name = bm_freeze_intern(ctx, src->name);
            dst->display_name = bm_freeze_intern(ctx, src->display_name);
            dst->description = bm_freeze_intern(ctx, src->description);
            dst->parent_group = bm_freeze_intern(ctx, src->parent_group);
            dst->expanded = src->expanded;
            dst->bold_title = src->bold_title;
        }
    }

    dst_model->cpack_install_type_count = src_model->cpack_install_type_count;
    dst_model->cpack_install_type_capacity = src_model->cpack_install_type_count;
    if (src_model->cpack_install_type_count > 0) {
        dst_model->cpack_install_types = arena_alloc_array(ctx->arena, CPack_Install_Type, src_model->cpack_install_type_count);
        if (!dst_model->cpack_install_types) return false;
        memset(dst_model->cpack_install_types, 0, sizeof(*dst_model->cpack_install_types) * src_model->cpack_install_type_count);
        for (size_t i = 0; i < src_model->cpack_install_type_count; i++) {
            const CPack_Install_Type *src = &src_model->cpack_install_types[i];
            CPack_Install_Type *dst = &dst_model->cpack_install_types[i];
            dst->name = bm_freeze_intern(ctx, src->name);
            dst->display_name = bm_freeze_intern(ctx, src->display_name);
        }
    }

    dst_model->cpack_component_count = src_model->cpack_component_count;
    dst_model->cpack_component_capacity = src_model->cpack_component_count;
    if (src_model->cpack_component_count > 0) {
        dst_model->cpack_components = arena_alloc_array(ctx->arena, CPack_Component, src_model->cpack_component_count);
        if (!dst_model->cpack_components) return false;
        memset(dst_model->cpack_components, 0, sizeof(*dst_model->cpack_components) * src_model->cpack_component_count);
        for (size_t i = 0; i < src_model->cpack_component_count; i++) {
            const CPack_Component *src = &src_model->cpack_components[i];
            CPack_Component *dst = &dst_model->cpack_components[i];
            dst->name = bm_freeze_intern(ctx, src->name);
            dst->display_name = bm_freeze_intern(ctx, src->display_name);
            dst->description = bm_freeze_intern(ctx, src->description);
            dst->group = bm_freeze_intern(ctx, src->group);
            dst->archive_file = bm_freeze_intern(ctx, src->archive_file);
            dst->plist = bm_freeze_intern(ctx, src->plist);
            dst->required = src->required;
            dst->hidden = src->hidden;
            dst->disabled = src->disabled;
            dst->downloaded = src->downloaded;
            if (!bm_freeze_copy_string_list(ctx, &src->depends, &dst->depends)) return false;
            if (!bm_freeze_copy_string_list(ctx, &src->install_types, &dst->install_types)) return false;
        }
    }

    return true;
}

const Build_Model *build_model_freeze(Build_Model_Builder *builder, Arena *out_arena) {
    if (!builder || !out_arena) return NULL;

    Build_Model *src_model = builder_finish(builder);
    if (!src_model) return NULL;
    if (!build_model_validate(src_model, NULL)) return NULL;

    Build_Model_Freeze_Ctx ctx = {0};
    ctx.arena = out_arena;
    ctx.intern_map = NULL;

    Build_Model *dst_model = build_model_create(out_arena);
    if (!dst_model) return NULL;
    dst_model->directory_nodes = NULL;
    dst_model->directory_node_count = 0;
    dst_model->directory_node_capacity = 0;
    dst_model->root_directory_index = 0;

    dst_model->project_name = bm_freeze_intern(&ctx, src_model->project_name);
    dst_model->project_version = bm_freeze_intern(&ctx, src_model->project_version);
    dst_model->project_description = bm_freeze_intern(&ctx, src_model->project_description);
    if (!bm_freeze_copy_string_list(&ctx, &src_model->project_languages, &dst_model->project_languages)) goto fail;

    if (!bm_freeze_copy_targets(&ctx, src_model, dst_model)) goto fail;

    dst_model->directories.source_dir = bm_freeze_intern(&ctx, src_model->directories.source_dir);
    dst_model->directories.binary_dir = bm_freeze_intern(&ctx, src_model->directories.binary_dir);
    if (!bm_freeze_copy_string_list(&ctx, &src_model->directories.include_dirs, &dst_model->directories.include_dirs)) goto fail;
    if (!bm_freeze_copy_string_list(&ctx, &src_model->directories.system_include_dirs, &dst_model->directories.system_include_dirs)) goto fail;
    if (!bm_freeze_copy_string_list(&ctx, &src_model->directories.link_dirs, &dst_model->directories.link_dirs)) goto fail;
    if (!bm_freeze_copy_directory_nodes(&ctx, src_model, dst_model)) goto fail;

    if (!bm_freeze_copy_property_list(&ctx, &src_model->cache_variables, &dst_model->cache_variables)) goto fail;
    bm_freeze_build_property_index(&dst_model->cache_variables, &dst_model->cache_variable_index);
    if (!bm_freeze_copy_property_list(&ctx, &src_model->environment_variables, &dst_model->environment_variables)) goto fail;
    bm_freeze_build_property_index(&dst_model->environment_variables, &dst_model->environment_variable_index);

    if (!bm_freeze_copy_found_packages(&ctx, src_model, dst_model)) goto fail;

    dst_model->config_debug = src_model->config_debug;
    dst_model->config_release = src_model->config_release;
    dst_model->config_relwithdebinfo = src_model->config_relwithdebinfo;
    dst_model->config_minsizerel = src_model->config_minsizerel;
    dst_model->default_config = bm_freeze_intern(&ctx, src_model->default_config);

    if (!bm_freeze_copy_string_list(&ctx, &src_model->global_definitions, &dst_model->global_definitions)) goto fail;
    if (!bm_freeze_copy_string_list(&ctx, &src_model->global_compile_options, &dst_model->global_compile_options)) goto fail;
    if (!bm_freeze_copy_string_list(&ctx, &src_model->global_link_options, &dst_model->global_link_options)) goto fail;
    if (!bm_freeze_copy_string_list(&ctx, &src_model->global_link_libraries, &dst_model->global_link_libraries)) goto fail;

    dst_model->language_standards.c_standard = bm_freeze_intern(&ctx, src_model->language_standards.c_standard);
    dst_model->language_standards.cxx_standard = bm_freeze_intern(&ctx, src_model->language_standards.cxx_standard);
    dst_model->language_standards.c_extensions = src_model->language_standards.c_extensions;
    dst_model->language_standards.cxx_extensions = src_model->language_standards.cxx_extensions;

    dst_model->c_compiler = bm_freeze_intern(&ctx, src_model->c_compiler);
    dst_model->cxx_compiler = bm_freeze_intern(&ctx, src_model->cxx_compiler);
    dst_model->assembler = bm_freeze_intern(&ctx, src_model->assembler);
    dst_model->linker = bm_freeze_intern(&ctx, src_model->linker);

    dst_model->is_windows = src_model->is_windows;
    dst_model->is_unix = src_model->is_unix;
    dst_model->is_apple = src_model->is_apple;
    dst_model->is_linux = src_model->is_linux;
    dst_model->enable_testing = src_model->enable_testing;
    dst_model->enable_install = src_model->enable_install;

    dst_model->install_rules.prefix = bm_freeze_intern(&ctx, src_model->install_rules.prefix);
    if (!bm_freeze_copy_string_list(&ctx, &src_model->install_rules.targets, &dst_model->install_rules.targets)) goto fail;
    if (!bm_freeze_copy_string_list(&ctx, &src_model->install_rules.files, &dst_model->install_rules.files)) goto fail;
    if (!bm_freeze_copy_string_list(&ctx, &src_model->install_rules.directories, &dst_model->install_rules.directories)) goto fail;
    if (!bm_freeze_copy_string_list(&ctx, &src_model->install_rules.programs, &dst_model->install_rules.programs)) goto fail;

    if (!bm_freeze_copy_custom_command_array(&ctx,
                                             src_model->output_custom_commands,
                                             src_model->output_custom_command_count,
                                             &dst_model->output_custom_commands,
                                             &dst_model->output_custom_command_count,
                                             &dst_model->output_custom_command_capacity)) {
        goto fail;
    }

    if (!bm_freeze_copy_tests(&ctx, src_model, dst_model)) goto fail;
    if (!bm_freeze_copy_cpack(&ctx, src_model, dst_model)) goto fail;

    stbds_shfree(ctx.intern_map);
    return dst_model;

fail:
    stbds_shfree(ctx.intern_map);
    return NULL;
}
