#include "build_model_internal.h"

typedef enum {
    BM_EFFECTIVE_INCLUDE_DIRECTORIES = 0,
    BM_EFFECTIVE_COMPILE_DEFINITIONS,
    BM_EFFECTIVE_LINK_LIBRARIES,
} BM_Effective_Query_Kind;

static const BM_Directory_Record *bm_model_directory(const Build_Model *model, BM_Directory_Id id) {
    if (!model || id == BM_DIRECTORY_ID_INVALID || (size_t)id >= arena_arr_len(model->directories)) return NULL;
    return &model->directories[id];
}

static const BM_Target_Record *bm_model_target(const Build_Model *model, BM_Target_Id id) {
    if (!model || id == BM_TARGET_ID_INVALID || (size_t)id >= arena_arr_len(model->targets)) return NULL;
    return &model->targets[id];
}

static const BM_Test_Record *bm_model_test(const Build_Model *model, BM_Test_Id id) {
    if (!model || id == BM_TEST_ID_INVALID || (size_t)id >= arena_arr_len(model->tests)) return NULL;
    return &model->tests[id];
}

static const BM_Install_Rule_Record *bm_model_install_rule(const Build_Model *model, BM_Install_Rule_Id id) {
    if (!model || id == BM_INSTALL_RULE_ID_INVALID || (size_t)id >= arena_arr_len(model->install_rules)) return NULL;
    return &model->install_rules[id];
}

static const BM_Package_Record *bm_model_package(const Build_Model *model, BM_Package_Id id) {
    if (!model || id == BM_PACKAGE_ID_INVALID || (size_t)id >= arena_arr_len(model->packages)) return NULL;
    return &model->packages[id];
}

static const BM_CPack_Install_Type_Record *bm_model_cpack_install_type(const Build_Model *model,
                                                                       BM_CPack_Install_Type_Id id) {
    if (!model || id == BM_CPACK_INSTALL_TYPE_ID_INVALID || (size_t)id >= arena_arr_len(model->cpack_install_types)) {
        return NULL;
    }
    return &model->cpack_install_types[id];
}

static const BM_CPack_Component_Group_Record *bm_model_cpack_component_group(const Build_Model *model,
                                                                             BM_CPack_Component_Group_Id id) {
    if (!model || id == BM_CPACK_COMPONENT_GROUP_ID_INVALID || (size_t)id >= arena_arr_len(model->cpack_component_groups)) {
        return NULL;
    }
    return &model->cpack_component_groups[id];
}

static const BM_CPack_Component_Record *bm_model_cpack_component(const Build_Model *model,
                                                                 BM_CPack_Component_Id id) {
    if (!model || id == BM_CPACK_COMPONENT_ID_INVALID || (size_t)id >= arena_arr_len(model->cpack_components)) {
        return NULL;
    }
    return &model->cpack_components[id];
}

static bool bm_collect_item_values(Arena *scratch,
                                   String_View **out,
                                   const BM_String_Item_View *items,
                                   bool public_only) {
    if (!scratch || !out) return false;
    for (size_t i = 0; i < arena_arr_len(items); ++i) {
        if (public_only && items[i].visibility == BM_VISIBILITY_PRIVATE) continue;
        if (!arena_arr_push(scratch, *out, items[i].value)) return false;
    }
    return true;
}

static bool bm_collect_directory_chain(const Build_Model *model,
                                       BM_Directory_Id owner_directory_id,
                                       Arena *scratch,
                                       String_View **out,
                                       BM_Effective_Query_Kind kind) {
    BM_Directory_Id *chain = NULL;
    BM_Directory_Id current = owner_directory_id;

    while (current != BM_DIRECTORY_ID_INVALID) {
        const BM_Directory_Record *directory = bm_model_directory(model, current);
        if (!directory) break;
        if (!arena_arr_push(scratch, chain, current)) return false;
        current = directory->parent_id;
    }

    for (size_t i = arena_arr_len(chain); i > 0; --i) {
        const BM_Directory_Record *directory = bm_model_directory(model, chain[i - 1]);
        if (!directory) continue;
        switch (kind) {
            case BM_EFFECTIVE_INCLUDE_DIRECTORIES:
                if (!bm_collect_item_values(scratch, out, directory->include_directories, false) ||
                    !bm_collect_item_values(scratch, out, directory->system_include_directories, false)) {
                    return false;
                }
                break;

            case BM_EFFECTIVE_COMPILE_DEFINITIONS:
                if (!bm_collect_item_values(scratch, out, directory->compile_definitions, false)) return false;
                break;

            case BM_EFFECTIVE_LINK_LIBRARIES:
                break;
        }
    }

    return true;
}

static bool bm_collect_dependency_interface(const Build_Model *model,
                                            BM_Target_Id id,
                                            Arena *scratch,
                                            uint8_t *visited,
                                            String_View **out,
                                            BM_Effective_Query_Kind kind) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || !visited) return false;
    if (visited[id]) return true;
    visited[id] = 1;

    switch (kind) {
        case BM_EFFECTIVE_INCLUDE_DIRECTORIES:
            if (!bm_collect_item_values(scratch, out, target->include_directories, true)) return false;
            break;

        case BM_EFFECTIVE_COMPILE_DEFINITIONS:
            if (!bm_collect_item_values(scratch, out, target->compile_definitions, true)) return false;
            break;

        case BM_EFFECTIVE_LINK_LIBRARIES:
            if (!bm_collect_item_values(scratch, out, target->link_libraries, true)) return false;
            break;
    }

    for (size_t i = 0; i < arena_arr_len(target->explicit_dependency_ids); ++i) {
        if (!bm_collect_dependency_interface(model, target->explicit_dependency_ids[i], scratch, visited, out, kind)) {
            return false;
        }
    }
    return true;
}

static bool bm_query_target_effective_common(const Build_Model *model,
                                             BM_Target_Id id,
                                             Arena *scratch,
                                             BM_String_Span *out,
                                             BM_Effective_Query_Kind kind) {
    const BM_Target_Record *target = bm_model_target(model, id);
    String_View *values = NULL;
    uint8_t *visited = NULL;

    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!model || !target || !scratch) return false;

    if (kind == BM_EFFECTIVE_INCLUDE_DIRECTORIES) {
        if (!bm_collect_item_values(scratch, &values, model->global_properties.include_directories, false) ||
            !bm_collect_item_values(scratch, &values, model->global_properties.system_include_directories, false)) {
            return false;
        }
    } else if (kind == BM_EFFECTIVE_COMPILE_DEFINITIONS) {
        if (!bm_collect_item_values(scratch, &values, model->global_properties.compile_definitions, false)) return false;
    }

    if (!bm_collect_directory_chain(model, target->owner_directory_id, scratch, &values, kind)) return false;

    switch (kind) {
        case BM_EFFECTIVE_INCLUDE_DIRECTORIES:
            if (!bm_collect_item_values(scratch, &values, target->include_directories, false)) return false;
            break;

        case BM_EFFECTIVE_COMPILE_DEFINITIONS:
            if (!bm_collect_item_values(scratch, &values, target->compile_definitions, false)) return false;
            break;

        case BM_EFFECTIVE_LINK_LIBRARIES:
            if (!bm_collect_item_values(scratch, &values, target->link_libraries, false)) return false;
            break;
    }

    visited = arena_alloc_array_zero(scratch, uint8_t, arena_arr_len(model->targets));
    if (!visited) return false;
    visited[id] = 1;

    for (size_t i = 0; i < arena_arr_len(target->explicit_dependency_ids); ++i) {
        if (!bm_collect_dependency_interface(model,
                                             target->explicit_dependency_ids[i],
                                             scratch,
                                             visited,
                                             &values,
                                             kind)) {
            return false;
        }
    }

    out->items = values;
    out->count = arena_arr_len(values);
    return true;
}

bool bm_model_has_project(const Build_Model *model) { return model ? model->project.present : false; }
bool bm_target_id_is_valid(BM_Target_Id id) { return id != BM_TARGET_ID_INVALID; }
bool bm_directory_id_is_valid(BM_Directory_Id id) { return id != BM_DIRECTORY_ID_INVALID; }
bool bm_test_id_is_valid(BM_Test_Id id) { return id != BM_TEST_ID_INVALID; }
bool bm_package_id_is_valid(BM_Package_Id id) { return id != BM_PACKAGE_ID_INVALID; }

size_t bm_query_directory_count(const Build_Model *model) { return model ? arena_arr_len(model->directories) : 0; }
size_t bm_query_target_count(const Build_Model *model) { return model ? arena_arr_len(model->targets) : 0; }
size_t bm_query_test_count(const Build_Model *model) { return model ? arena_arr_len(model->tests) : 0; }
size_t bm_query_install_rule_count(const Build_Model *model) { return model ? arena_arr_len(model->install_rules) : 0; }
size_t bm_query_package_count(const Build_Model *model) { return model ? arena_arr_len(model->packages) : 0; }
size_t bm_query_cpack_install_type_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_install_types) : 0; }
size_t bm_query_cpack_component_group_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_component_groups) : 0; }
size_t bm_query_cpack_component_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_components) : 0; }

String_View bm_query_project_name(const Build_Model *model) { return model ? model->project.name : (String_View){0}; }
String_View bm_query_project_version(const Build_Model *model) { return model ? model->project.version : (String_View){0}; }

BM_String_Span bm_query_project_languages(const Build_Model *model) {
    BM_String_Span span = {0};
    if (!model) return span;
    span.items = model->project.languages;
    span.count = arena_arr_len(model->project.languages);
    return span;
}

BM_Directory_Id bm_query_root_directory(const Build_Model *model) {
    return model ? model->root_directory_id : BM_DIRECTORY_ID_INVALID;
}

BM_Directory_Id bm_query_directory_parent(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? directory->parent_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_directory_source_dir(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? directory->source_dir : (String_View){0};
}

String_View bm_query_directory_binary_dir(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? directory->binary_dir : (String_View){0};
}

BM_Target_Id bm_query_target_by_name(const Build_Model *model, String_View name) {
    if (!model) return BM_TARGET_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->target_name_index); ++i) {
        if (nob_sv_eq(model->target_name_index[i].name, name)) return (BM_Target_Id)model->target_name_index[i].id;
    }
    return BM_TARGET_ID_INVALID;
}

BM_Test_Id bm_query_test_by_name(const Build_Model *model, String_View name) {
    if (!model) return BM_TEST_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->test_name_index); ++i) {
        if (nob_sv_eq(model->test_name_index[i].name, name)) return (BM_Test_Id)model->test_name_index[i].id;
    }
    return BM_TEST_ID_INVALID;
}

BM_Package_Id bm_query_package_by_name(const Build_Model *model, String_View name) {
    if (!model) return BM_PACKAGE_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->package_name_index); ++i) {
        if (nob_sv_eq(model->package_name_index[i].name, name)) return (BM_Package_Id)model->package_name_index[i].id;
    }
    return BM_PACKAGE_ID_INVALID;
}

String_View bm_query_target_name(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->name : (String_View){0};
}

BM_Target_Kind bm_query_target_kind(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->kind : BM_TARGET_UTILITY;
}

BM_Directory_Id bm_query_target_owner_directory(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

BM_String_Span bm_query_target_sources_raw(const Build_Model *model, BM_Target_Id id) {
    BM_String_Span span = {0};
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target) return span;
    span.items = target->sources;
    span.count = arena_arr_len(target->sources);
    return span;
}

BM_Target_Id_Span bm_query_target_dependencies_explicit(const Build_Model *model, BM_Target_Id id) {
    BM_Target_Id_Span span = {0};
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target) return span;
    span.items = target->explicit_dependency_ids;
    span.count = arena_arr_len(target->explicit_dependency_ids);
    return span;
}

BM_String_Item_Span bm_query_target_link_libraries_raw(const Build_Model *model, BM_Target_Id id) {
    BM_String_Item_Span span = {0};
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target) return span;
    span.items = target->link_libraries;
    span.count = arena_arr_len(target->link_libraries);
    return span;
}

BM_String_Item_Span bm_query_target_include_directories_raw(const Build_Model *model, BM_Target_Id id) {
    BM_String_Item_Span span = {0};
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target) return span;
    span.items = target->include_directories;
    span.count = arena_arr_len(target->include_directories);
    return span;
}

BM_String_Item_Span bm_query_target_compile_definitions_raw(const Build_Model *model, BM_Target_Id id) {
    BM_String_Item_Span span = {0};
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target) return span;
    span.items = target->compile_definitions;
    span.count = arena_arr_len(target->compile_definitions);
    return span;
}

BM_String_Item_Span bm_query_target_compile_options_raw(const Build_Model *model, BM_Target_Id id) {
    BM_String_Item_Span span = {0};
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target) return span;
    span.items = target->compile_options;
    span.count = arena_arr_len(target->compile_options);
    return span;
}

BM_String_Item_Span bm_query_target_link_options_raw(const Build_Model *model, BM_Target_Id id) {
    BM_String_Item_Span span = {0};
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target) return span;
    span.items = target->link_options;
    span.count = arena_arr_len(target->link_options);
    return span;
}

BM_String_Item_Span bm_query_target_link_directories_raw(const Build_Model *model, BM_Target_Id id) {
    BM_String_Item_Span span = {0};
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target) return span;
    span.items = target->link_directories;
    span.count = arena_arr_len(target->link_directories);
    return span;
}

String_View bm_query_target_output_name(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->output_name : (String_View){0};
}

String_View bm_query_target_prefix(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->prefix : (String_View){0};
}

String_View bm_query_target_suffix(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->suffix : (String_View){0};
}

String_View bm_query_target_archive_output_directory(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->archive_output_directory : (String_View){0};
}

String_View bm_query_target_library_output_directory(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->library_output_directory : (String_View){0};
}

String_View bm_query_target_runtime_output_directory(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->runtime_output_directory : (String_View){0};
}

String_View bm_query_target_folder(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->folder : (String_View){0};
}

bool bm_query_target_effective_include_directories(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out) {
    return bm_query_target_effective_common(model, id, scratch, out, BM_EFFECTIVE_INCLUDE_DIRECTORIES);
}

bool bm_query_target_effective_compile_definitions(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out) {
    return bm_query_target_effective_common(model, id, scratch, out, BM_EFFECTIVE_COMPILE_DEFINITIONS);
}

bool bm_query_target_effective_link_libraries(const Build_Model *model,
                                              BM_Target_Id id,
                                              Arena *scratch,
                                              BM_String_Span *out) {
    return bm_query_target_effective_common(model, id, scratch, out, BM_EFFECTIVE_LINK_LIBRARIES);
}

bool bm_query_testing_enabled(const Build_Model *model) {
    return model ? model->testing_enabled : false;
}

String_View bm_query_test_name(const Build_Model *model, BM_Test_Id id) {
    const BM_Test_Record *test = bm_model_test(model, id);
    return test ? test->name : (String_View){0};
}

String_View bm_query_test_command(const Build_Model *model, BM_Test_Id id) {
    const BM_Test_Record *test = bm_model_test(model, id);
    return test ? test->command : (String_View){0};
}

BM_Install_Rule_Kind bm_query_install_rule_kind(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->kind : BM_INSTALL_RULE_FILE;
}

BM_Directory_Id bm_query_install_rule_owner_directory(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_install_rule_item_raw(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->item : (String_View){0};
}

String_View bm_query_install_rule_destination(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->destination : (String_View){0};
}

BM_Target_Id bm_query_install_rule_target(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->resolved_target_id : BM_TARGET_ID_INVALID;
}

String_View bm_query_package_name(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->package_name : (String_View){0};
}

BM_Directory_Id bm_query_package_owner_directory(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_package_mode(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->mode : (String_View){0};
}

String_View bm_query_package_found_path(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->found_path : (String_View){0};
}

bool bm_query_package_found(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->found : false;
}

bool bm_query_package_required(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->required : false;
}

bool bm_query_package_quiet(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->quiet : false;
}

String_View bm_query_cpack_install_type_name(const Build_Model *model, BM_CPack_Install_Type_Id id) {
    const BM_CPack_Install_Type_Record *record = bm_model_cpack_install_type(model, id);
    return record ? record->name : (String_View){0};
}

String_View bm_query_cpack_install_type_display_name(const Build_Model *model, BM_CPack_Install_Type_Id id) {
    const BM_CPack_Install_Type_Record *record = bm_model_cpack_install_type(model, id);
    return record ? record->display_name : (String_View){0};
}

BM_Directory_Id bm_query_cpack_install_type_owner_directory(const Build_Model *model, BM_CPack_Install_Type_Id id) {
    const BM_CPack_Install_Type_Record *record = bm_model_cpack_install_type(model, id);
    return record ? record->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_cpack_component_group_name(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->name : (String_View){0};
}

String_View bm_query_cpack_component_group_display_name(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->display_name : (String_View){0};
}

String_View bm_query_cpack_component_group_description(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->description : (String_View){0};
}

BM_CPack_Component_Group_Id bm_query_cpack_component_group_parent(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->parent_group_id : BM_CPACK_COMPONENT_GROUP_ID_INVALID;
}

BM_Directory_Id bm_query_cpack_component_group_owner_directory(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

bool bm_query_cpack_component_group_expanded(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->expanded : false;
}

bool bm_query_cpack_component_group_bold_title(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->bold_title : false;
}

String_View bm_query_cpack_component_name(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->name : (String_View){0};
}

String_View bm_query_cpack_component_display_name(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->display_name : (String_View){0};
}

String_View bm_query_cpack_component_description(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->description : (String_View){0};
}

BM_CPack_Component_Group_Id bm_query_cpack_component_group(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->group_id : BM_CPACK_COMPONENT_GROUP_ID_INVALID;
}

BM_CPack_Component_Id_Span bm_query_cpack_component_dependencies(const Build_Model *model, BM_CPack_Component_Id id) {
    BM_CPack_Component_Id_Span span = {0};
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    if (!record) return span;
    span.items = record->dependency_ids;
    span.count = arena_arr_len(record->dependency_ids);
    return span;
}

BM_CPack_Install_Type_Id_Span bm_query_cpack_component_install_types(const Build_Model *model, BM_CPack_Component_Id id) {
    BM_CPack_Install_Type_Id_Span span = {0};
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    if (!record) return span;
    span.items = record->install_type_ids;
    span.count = arena_arr_len(record->install_type_ids);
    return span;
}

String_View bm_query_cpack_component_archive_file(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->archive_file : (String_View){0};
}

String_View bm_query_cpack_component_plist(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->plist : (String_View){0};
}

bool bm_query_cpack_component_required(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->required : false;
}

bool bm_query_cpack_component_hidden(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->hidden : false;
}

bool bm_query_cpack_component_disabled(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->disabled : false;
}

bool bm_query_cpack_component_downloaded(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->downloaded : false;
}

BM_Directory_Id bm_query_cpack_component_owner_directory(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}
