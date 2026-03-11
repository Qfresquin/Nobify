#include "build_model_internal.h"

static bool bm_clone_string_array(Arena *arena, String_View **dest, const String_View *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        String_View copy = {0};
        if (!bm_copy_string(arena, src[i], &copy) || !arena_arr_push(arena, *dest, copy)) return false;
    }
    return true;
}

static bool bm_clone_provenance(Arena *arena, BM_Provenance *dest, BM_Provenance src) {
    if (!dest) return false;
    *dest = src;
    return bm_copy_string(arena, src.file_path, &dest->file_path);
}

static bool bm_clone_item_array(Arena *arena, BM_String_Item_View **dest, const BM_String_Item_View *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_String_Item_View item = src[i];
        if (!bm_copy_string(arena, src[i].value, &item.value) ||
            !bm_clone_provenance(arena, &item.provenance, src[i].provenance) ||
            !arena_arr_push(arena, *dest, item)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_raw_properties(Arena *arena,
                                    BM_Raw_Property_Record **dest,
                                    const BM_Raw_Property_Record *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_Raw_Property_Record record = {0};
        record.op = src[i].op;
        record.flags = src[i].flags;
        if (!bm_copy_string(arena, src[i].name, &record.name) ||
            !bm_clone_string_array(arena, &record.items, src[i].items) ||
            !bm_clone_provenance(arena, &record.provenance, src[i].provenance) ||
            !arena_arr_push(arena, *dest, record)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_global_state(Arena *arena,
                                  BM_Global_Property_State *dest,
                                  const BM_Global_Property_State *src) {
    return bm_clone_item_array(arena, &dest->include_directories, src->include_directories) &&
           bm_clone_item_array(arena, &dest->system_include_directories, src->system_include_directories) &&
           bm_clone_item_array(arena, &dest->link_directories, src->link_directories) &&
           bm_clone_item_array(arena, &dest->compile_definitions, src->compile_definitions) &&
           bm_clone_item_array(arena, &dest->compile_options, src->compile_options) &&
           bm_clone_item_array(arena, &dest->link_options, src->link_options) &&
           bm_clone_raw_properties(arena, &dest->raw_properties, src->raw_properties);
}

static bool bm_clone_project(Arena *arena, BM_Project_Record *dest, const BM_Project_Record *src) {
    *dest = *src;
    return bm_copy_string(arena, src->name, &dest->name) &&
           bm_copy_string(arena, src->version, &dest->version) &&
           bm_copy_string(arena, src->description, &dest->description) &&
           bm_copy_string(arena, src->homepage_url, &dest->homepage_url) &&
           bm_clone_string_array(arena, &dest->languages, src->languages) &&
           bm_clone_provenance(arena, &dest->declaration_provenance, src->declaration_provenance) &&
           bm_copy_string(arena, src->minimum_required_version, &dest->minimum_required_version) &&
           bm_clone_provenance(arena, &dest->minimum_required_provenance, src->minimum_required_provenance);
}

static bool bm_clone_directories(const Build_Model_Draft *draft, Build_Model *model, Arena *arena) {
    for (size_t i = 0; i < arena_arr_len(draft->directories); ++i) {
        BM_Directory_Record record = draft->directories[i];
        if (!bm_copy_string(arena, record.source_dir, &record.source_dir) ||
            !bm_copy_string(arena, record.binary_dir, &record.binary_dir) ||
            !bm_clone_provenance(arena, &record.provenance, draft->directories[i].provenance) ||
            !bm_clone_item_array(arena, &record.include_directories, draft->directories[i].include_directories) ||
            !bm_clone_item_array(arena, &record.system_include_directories, draft->directories[i].system_include_directories) ||
            !bm_clone_item_array(arena, &record.link_directories, draft->directories[i].link_directories) ||
            !bm_clone_item_array(arena, &record.compile_definitions, draft->directories[i].compile_definitions) ||
            !bm_clone_item_array(arena, &record.compile_options, draft->directories[i].compile_options) ||
            !bm_clone_item_array(arena, &record.link_options, draft->directories[i].link_options) ||
            !bm_clone_raw_properties(arena, &record.raw_properties, draft->directories[i].raw_properties) ||
            !arena_arr_push(arena, model->directories, record)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_targets(const Build_Model_Draft *draft, Build_Model *model, Arena *arena, Diag_Sink *sink) {
    for (size_t i = 0; i < arena_arr_len(draft->targets); ++i) {
        const BM_Target_Record *src = &draft->targets[i];
        BM_Target_Record target = *src;
        target.alias_of_id = BM_TARGET_ID_INVALID;
        target.explicit_dependency_ids = NULL;

        if (!bm_copy_string(arena, src->name, &target.name) ||
            !bm_copy_string(arena, src->alias_of_name, &target.alias_of_name) ||
            !bm_copy_string(arena, src->output_name, &target.output_name) ||
            !bm_copy_string(arena, src->prefix, &target.prefix) ||
            !bm_copy_string(arena, src->suffix, &target.suffix) ||
            !bm_copy_string(arena, src->archive_output_directory, &target.archive_output_directory) ||
            !bm_copy_string(arena, src->library_output_directory, &target.library_output_directory) ||
            !bm_copy_string(arena, src->runtime_output_directory, &target.runtime_output_directory) ||
            !bm_copy_string(arena, src->folder, &target.folder) ||
            !bm_clone_provenance(arena, &target.provenance, src->provenance) ||
            !bm_clone_string_array(arena, &target.sources, src->sources) ||
            !bm_clone_string_array(arena, &target.explicit_dependency_names, src->explicit_dependency_names) ||
            !bm_clone_item_array(arena, &target.link_libraries, src->link_libraries) ||
            !bm_clone_item_array(arena, &target.link_options, src->link_options) ||
            !bm_clone_item_array(arena, &target.link_directories, src->link_directories) ||
            !bm_clone_item_array(arena, &target.include_directories, src->include_directories) ||
            !bm_clone_item_array(arena, &target.compile_definitions, src->compile_definitions) ||
            !bm_clone_item_array(arena, &target.compile_options, src->compile_options) ||
            !bm_clone_raw_properties(arena, &target.raw_properties, src->raw_properties)) {
            return false;
        }

        if (!bm_string_view_is_empty(src->alias_of_name)) {
            target.alias_of_id = bm_draft_find_target_id(draft, src->alias_of_name);
            if (target.alias_of_id == BM_TARGET_ID_INVALID) {
                bm_diag_error(sink, src->provenance, "build_model_freeze", "freeze", "alias target could not be resolved during freeze", "fix unresolved alias targets before freeze");
                return false;
            }
        }

        for (size_t dep = 0; dep < arena_arr_len(src->explicit_dependency_names); ++dep) {
            BM_Target_Id dep_id = bm_draft_find_target_id(draft, src->explicit_dependency_names[dep]);
            if (dep_id == BM_TARGET_ID_INVALID) {
                bm_diag_error(sink, src->provenance, "build_model_freeze", "freeze", "target dependency could not be resolved during freeze", "run validation and fix unresolved explicit dependencies");
                return false;
            }
            if (!arena_arr_push(arena, target.explicit_dependency_ids, dep_id)) return false;
        }

        if (!arena_arr_push(arena, model->targets, target) ||
            !bm_add_name_index(arena, &model->target_name_index, target.name, target.id)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_tests(const Build_Model_Draft *draft, Build_Model *model, Arena *arena) {
    for (size_t i = 0; i < arena_arr_len(draft->tests); ++i) {
        BM_Test_Record test = draft->tests[i];
        if (!bm_copy_string(arena, test.name, &test.name) ||
            !bm_copy_string(arena, test.command, &test.command) ||
            !bm_copy_string(arena, test.working_dir, &test.working_dir) ||
            !bm_clone_provenance(arena, &test.provenance, draft->tests[i].provenance) ||
            !arena_arr_push(arena, model->tests, test) ||
            !bm_add_name_index(arena, &model->test_name_index, test.name, test.id)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_install_rules(const Build_Model_Draft *draft, Build_Model *model, Arena *arena, Diag_Sink *sink) {
    for (size_t i = 0; i < arena_arr_len(draft->install_rules); ++i) {
        const BM_Install_Rule_Record *src = &draft->install_rules[i];
        BM_Install_Rule_Record rule = *src;
        rule.resolved_target_id = BM_TARGET_ID_INVALID;
        if (!bm_copy_string(arena, src->item, &rule.item) ||
            !bm_copy_string(arena, src->destination, &rule.destination) ||
            !bm_clone_provenance(arena, &rule.provenance, src->provenance)) {
            return false;
        }
        if (src->kind == BM_INSTALL_RULE_TARGET) {
            rule.resolved_target_id = bm_draft_find_target_id(draft, src->item);
            if (rule.resolved_target_id == BM_TARGET_ID_INVALID) {
                bm_diag_error(sink, src->provenance, "build_model_freeze", "freeze", "install rule target could not be resolved during freeze", "fix unresolved install target names before freeze");
                return false;
            }
        }
        if (!arena_arr_push(arena, model->install_rules, rule)) return false;
    }
    return true;
}

static bool bm_clone_packages(const Build_Model_Draft *draft, Build_Model *model, Arena *arena) {
    for (size_t i = 0; i < arena_arr_len(draft->packages); ++i) {
        BM_Package_Record package = draft->packages[i];
        if (!bm_copy_string(arena, package.package_name, &package.package_name) ||
            !bm_copy_string(arena, package.mode, &package.mode) ||
            !bm_copy_string(arena, package.found_path, &package.found_path) ||
            !bm_clone_provenance(arena, &package.provenance, draft->packages[i].provenance) ||
            !arena_arr_push(arena, model->packages, package) ||
            !bm_add_name_index(arena, &model->package_name_index, package.package_name, package.id)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_cpack(const Build_Model_Draft *draft, Build_Model *model, Arena *arena, Diag_Sink *sink) {
    for (size_t i = 0; i < arena_arr_len(draft->cpack_install_types); ++i) {
        BM_CPack_Install_Type_Record record = draft->cpack_install_types[i];
        if (!bm_copy_string(arena, record.name, &record.name) ||
            !bm_copy_string(arena, record.display_name, &record.display_name) ||
            !bm_clone_provenance(arena, &record.provenance, draft->cpack_install_types[i].provenance) ||
            !arena_arr_push(arena, model->cpack_install_types, record)) {
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_component_groups); ++i) {
        const BM_CPack_Component_Group_Record *src = &draft->cpack_component_groups[i];
        BM_CPack_Component_Group_Record record = *src;
        record.parent_group_id = BM_CPACK_COMPONENT_GROUP_ID_INVALID;
        if (!bm_copy_string(arena, src->name, &record.name) ||
            !bm_copy_string(arena, src->display_name, &record.display_name) ||
            !bm_copy_string(arena, src->description, &record.description) ||
            !bm_copy_string(arena, src->parent_group_name, &record.parent_group_name) ||
            !bm_clone_provenance(arena, &record.provenance, src->provenance)) {
            return false;
        }
        if (!bm_string_view_is_empty(src->parent_group_name)) {
            record.parent_group_id = bm_draft_find_component_group_id(draft, src->parent_group_name);
            if (record.parent_group_id == BM_CPACK_COMPONENT_GROUP_ID_INVALID) {
                bm_diag_error(sink, src->provenance, "build_model_freeze", "freeze", "component group parent could not be resolved during freeze", "fix unresolved CPack parent groups before freeze");
                return false;
            }
        }
        if (!arena_arr_push(arena, model->cpack_component_groups, record)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_components); ++i) {
        const BM_CPack_Component_Record *src = &draft->cpack_components[i];
        BM_CPack_Component_Record record = *src;
        record.group_id = BM_CPACK_COMPONENT_GROUP_ID_INVALID;
        record.dependency_ids = NULL;
        record.install_type_ids = NULL;
        if (!bm_copy_string(arena, src->name, &record.name) ||
            !bm_copy_string(arena, src->display_name, &record.display_name) ||
            !bm_copy_string(arena, src->description, &record.description) ||
            !bm_copy_string(arena, src->group_name, &record.group_name) ||
            !bm_clone_string_array(arena, &record.dependency_names, src->dependency_names) ||
            !bm_clone_string_array(arena, &record.install_type_names, src->install_type_names) ||
            !bm_copy_string(arena, src->archive_file, &record.archive_file) ||
            !bm_copy_string(arena, src->plist, &record.plist) ||
            !bm_clone_provenance(arena, &record.provenance, src->provenance)) {
            return false;
        }
        if (!bm_string_view_is_empty(src->group_name)) {
            record.group_id = bm_draft_find_component_group_id(draft, src->group_name);
            if (record.group_id == BM_CPACK_COMPONENT_GROUP_ID_INVALID) {
                bm_diag_error(sink, src->provenance, "build_model_freeze", "freeze", "component group could not be resolved during freeze", "fix unresolved CPack component groups before freeze");
                return false;
            }
        }
        for (size_t dep = 0; dep < arena_arr_len(src->dependency_names); ++dep) {
            BM_CPack_Component_Id dep_id = bm_draft_find_component_id(draft, src->dependency_names[dep]);
            if (dep_id == BM_CPACK_COMPONENT_ID_INVALID) {
                bm_diag_error(sink, src->provenance, "build_model_freeze", "freeze", "component dependency could not be resolved during freeze", "fix unresolved CPack component dependencies before freeze");
                return false;
            }
            if (!arena_arr_push(arena, record.dependency_ids, dep_id)) return false;
        }
        for (size_t dep = 0; dep < arena_arr_len(src->install_type_names); ++dep) {
            BM_CPack_Install_Type_Id dep_id = bm_draft_find_install_type_id(draft, src->install_type_names[dep]);
            if (dep_id == BM_CPACK_INSTALL_TYPE_ID_INVALID) {
                bm_diag_error(sink, src->provenance, "build_model_freeze", "freeze", "component install type could not be resolved during freeze", "fix unresolved install type references before freeze");
                return false;
            }
            if (!arena_arr_push(arena, record.install_type_ids, dep_id)) return false;
        }
        if (!arena_arr_push(arena, model->cpack_components, record)) return false;
    }

    return true;
}

const Build_Model *bm_freeze_draft(const Build_Model_Draft *draft,
                                   Arena *out_arena,
                                   Diag_Sink *sink) {
    Build_Model *model = NULL;
    if (!draft || !out_arena) return NULL;

    model = arena_alloc_zero(out_arena, sizeof(*model));
    if (!model) return NULL;

    model->arena = out_arena;
    model->testing_enabled = draft->testing_enabled;
    model->root_directory_id = draft->root_directory_id;

    if (!bm_clone_project(out_arena, &model->project, &draft->project) ||
        !bm_clone_global_state(out_arena, &model->global_properties, &draft->global_properties) ||
        !bm_clone_directories(draft, model, out_arena) ||
        !bm_clone_targets(draft, model, out_arena, sink) ||
        !bm_clone_tests(draft, model, out_arena) ||
        !bm_clone_install_rules(draft, model, out_arena, sink) ||
        !bm_clone_packages(draft, model, out_arena) ||
        !bm_clone_cpack(draft, model, out_arena, sink)) {
            return NULL;
    }

    return model;
}
