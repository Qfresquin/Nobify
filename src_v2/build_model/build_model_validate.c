#include "build_model_internal.h"

static bool bm_validate_owner_directory(const Build_Model_Draft *draft,
                                        BM_Directory_Id owner_directory_id,
                                        BM_Provenance provenance,
                                        const char *entity_kind,
                                        Diag_Sink *sink,
                                        bool *had_error) {
    if (owner_directory_id != BM_DIRECTORY_ID_INVALID &&
        (size_t)owner_directory_id < arena_arr_len(draft->directories)) {
        return true;
    }

    *had_error = true;
    return bm_diag_error(sink,
                         provenance,
                         "build_model_validate",
                         "structural",
                         "entity owner_directory_id is invalid",
                         entity_kind);
}

static bool bm_validate_structural_pass(const Build_Model_Draft *draft, Diag_Sink *sink, bool *had_error) {
    if (draft->has_semantic_entities && draft->root_directory_id == BM_DIRECTORY_ID_INVALID) {
        *had_error = true;
        bm_diag_error(sink,
                      (BM_Provenance){0},
                      "build_model_validate",
                      "structural",
                      "missing root directory for semantic model",
                      "ensure the event stream emits a directory root before other semantic entities");
    }

    for (size_t i = 0; i < arena_arr_len(draft->directories); ++i) {
        const BM_Directory_Record *directory = &draft->directories[i];
        if (directory->id != (BM_Directory_Id)i) {
            *had_error = true;
            bm_diag_error(sink, directory->provenance, "build_model_validate", "structural", "directory id mismatch", "directory ids must be contiguous");
        }
        if (directory->owner_directory_id != directory->id) {
            *had_error = true;
            bm_diag_error(sink, directory->provenance, "build_model_validate", "structural", "directory owner_directory_id must equal directory id", "fix directory ownership");
        }
        if (directory->parent_id != BM_DIRECTORY_ID_INVALID &&
            (size_t)directory->parent_id >= arena_arr_len(draft->directories)) {
            *had_error = true;
            bm_diag_error(sink, directory->provenance, "build_model_validate", "structural", "directory parent_id is invalid", "fix directory stack reconstruction");
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->targets); ++i) {
        const BM_Target_Record *target = &draft->targets[i];
        if (target->id != (BM_Target_Id)i) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "structural", "target id mismatch", "target ids must be contiguous");
        }
        if (bm_string_view_is_empty(target->name)) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "structural", "target has empty name", "ensure EVENT_TARGET_DECLARE carries a target name");
        }
        if (target->kind > BM_TARGET_UTILITY) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "structural", "target kind is invalid", "map every target type to a canonical BM_Target_Kind");
        }
        bm_validate_owner_directory(draft, target->owner_directory_id, target->provenance, "target", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->tests); ++i) {
        const BM_Test_Record *test = &draft->tests[i];
        if (bm_string_view_is_empty(test->name) || bm_string_view_is_empty(test->command)) {
            *had_error = true;
            bm_diag_error(sink, test->provenance, "build_model_validate", "structural", "test is missing required fields", "tests require name and command");
        }
        bm_validate_owner_directory(draft, test->owner_directory_id, test->provenance, "test", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->install_rules); ++i) {
        const BM_Install_Rule_Record *rule = &draft->install_rules[i];
        if (bm_string_view_is_empty(rule->item) || bm_string_view_is_empty(rule->destination)) {
            *had_error = true;
            bm_diag_error(sink, rule->provenance, "build_model_validate", "structural", "install rule is missing required fields", "install rules require item and destination");
        }
        bm_validate_owner_directory(draft, rule->owner_directory_id, rule->provenance, "install rule", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->packages); ++i) {
        const BM_Package_Record *package = &draft->packages[i];
        if (bm_string_view_is_empty(package->package_name)) {
            *had_error = true;
            bm_diag_error(sink, package->provenance, "build_model_validate", "structural", "package result is missing package name", "package results require package_name");
        }
        bm_validate_owner_directory(draft, package->owner_directory_id, package->provenance, "package", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_install_types); ++i) {
        const BM_CPack_Install_Type_Record *record = &draft->cpack_install_types[i];
        if (bm_string_view_is_empty(record->name)) {
            *had_error = true;
            bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "CPack install type is missing name", "install types require a name");
        }
        bm_validate_owner_directory(draft, record->owner_directory_id, record->provenance, "cpack install type", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_component_groups); ++i) {
        const BM_CPack_Component_Group_Record *record = &draft->cpack_component_groups[i];
        if (bm_string_view_is_empty(record->name)) {
            *had_error = true;
            bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "CPack component group is missing name", "component groups require a name");
        }
        bm_validate_owner_directory(draft, record->owner_directory_id, record->provenance, "cpack component group", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_components); ++i) {
        const BM_CPack_Component_Record *record = &draft->cpack_components[i];
        if (bm_string_view_is_empty(record->name)) {
            *had_error = true;
            bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "CPack component is missing name", "components require a name");
        }
        bm_validate_owner_directory(draft, record->owner_directory_id, record->provenance, "cpack component", sink, had_error);
    }

    return true;
}

static bool bm_validate_resolution_pass(const Build_Model_Draft *draft, Diag_Sink *sink, bool *had_error) {
    for (size_t i = 0; i < arena_arr_len(draft->targets); ++i) {
        const BM_Target_Record *target = &draft->targets[i];

        if (target->alias &&
            !bm_string_view_is_empty(target->alias_of_name) &&
            bm_draft_find_target_id(draft, target->alias_of_name) == BM_TARGET_ID_INVALID) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "resolution", "alias target references an unknown target", "declare the aliased target before the alias");
        }

        for (size_t dep = 0; dep < arena_arr_len(target->explicit_dependency_names); ++dep) {
            if (bm_draft_find_target_id(draft, target->explicit_dependency_names[dep]) == BM_TARGET_ID_INVALID) {
                *had_error = true;
                bm_diag_error(sink, target->provenance, "build_model_validate", "resolution", "explicit target dependency cannot be resolved", "declare the dependency target before freeze");
            }
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->install_rules); ++i) {
        const BM_Install_Rule_Record *rule = &draft->install_rules[i];
        if (rule->kind == BM_INSTALL_RULE_TARGET &&
            bm_draft_find_target_id(draft, rule->item) == BM_TARGET_ID_INVALID) {
            *had_error = true;
            bm_diag_error(sink, rule->provenance, "build_model_validate", "resolution", "install target rule references an unknown target", "declare the target before installing it");
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_component_groups); ++i) {
        const BM_CPack_Component_Group_Record *group = &draft->cpack_component_groups[i];
        if (!bm_string_view_is_empty(group->parent_group_name) &&
            bm_draft_find_component_group_id(draft, group->parent_group_name) == BM_CPACK_COMPONENT_GROUP_ID_INVALID) {
            *had_error = true;
            bm_diag_error(sink, group->provenance, "build_model_validate", "resolution", "component group parent cannot be resolved", "declare the parent group before referencing it");
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_components); ++i) {
        const BM_CPack_Component_Record *component = &draft->cpack_components[i];
        if (!bm_string_view_is_empty(component->group_name) &&
            bm_draft_find_component_group_id(draft, component->group_name) == BM_CPACK_COMPONENT_GROUP_ID_INVALID) {
            *had_error = true;
            bm_diag_error(sink, component->provenance, "build_model_validate", "resolution", "component group cannot be resolved", "declare the referenced component group before the component");
        }

        for (size_t dep = 0; dep < arena_arr_len(component->dependency_names); ++dep) {
            if (bm_draft_find_component_id(draft, component->dependency_names[dep]) == BM_CPACK_COMPONENT_ID_INVALID) {
                *had_error = true;
                bm_diag_error(sink, component->provenance, "build_model_validate", "resolution", "component dependency cannot be resolved", "declare referenced components before using DEPENDS");
            }
        }

        for (size_t dep = 0; dep < arena_arr_len(component->install_type_names); ++dep) {
            if (bm_draft_find_install_type_id(draft, component->install_type_names[dep]) == BM_CPACK_INSTALL_TYPE_ID_INVALID) {
                *had_error = true;
                bm_diag_error(sink, component->provenance, "build_model_validate", "resolution", "component install type cannot be resolved", "declare referenced install types before using them");
            }
        }
    }

    return true;
}

static bool bm_target_has_typed_payload(const BM_Target_Record *target) {
    return arena_arr_len(target->link_libraries) > 0 ||
           arena_arr_len(target->link_options) > 0 ||
           arena_arr_len(target->link_directories) > 0 ||
           arena_arr_len(target->include_directories) > 0 ||
           arena_arr_len(target->compile_definitions) > 0 ||
           arena_arr_len(target->compile_options) > 0 ||
           !bm_string_view_is_empty(target->output_name) ||
           !bm_string_view_is_empty(target->prefix) ||
           !bm_string_view_is_empty(target->suffix) ||
           !bm_string_view_is_empty(target->archive_output_directory) ||
           !bm_string_view_is_empty(target->library_output_directory) ||
           !bm_string_view_is_empty(target->runtime_output_directory) ||
           !bm_string_view_is_empty(target->folder);
}

static bool bm_validate_semantic_pass(const Build_Model_Draft *draft, Diag_Sink *sink, bool *had_error) {
    for (size_t i = 0; i < arena_arr_len(draft->targets); ++i) {
        const BM_Target_Record *target = &draft->targets[i];

        if (target->kind == BM_TARGET_INTERFACE_LIBRARY && arena_arr_len(target->sources) > 0) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "semantic", "interface library may not own concrete source files", "remove sources or change the target kind");
        }

        if (target->kind == BM_TARGET_INTERFACE_LIBRARY) {
            for (size_t item = 0; item < arena_arr_len(target->link_libraries); ++item) {
                if (target->link_libraries[item].visibility == BM_VISIBILITY_PRIVATE) {
                    *had_error = true;
                    bm_diag_error(sink, target->link_libraries[item].provenance, "build_model_validate", "semantic", "interface library may not carry private link libraries", "use PUBLIC or INTERFACE visibility for interface targets");
                }
            }
        }

        if (target->alias &&
            (arena_arr_len(target->sources) > 0 ||
             arena_arr_len(target->explicit_dependency_names) > 0 ||
             bm_target_has_typed_payload(target))) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "semantic", "alias target may not own sources, dependencies or build payload", "leave alias targets as lightweight references only");
        }

        if (target->imported &&
            (!bm_string_view_is_empty(target->output_name) ||
             !bm_string_view_is_empty(target->prefix) ||
             !bm_string_view_is_empty(target->suffix) ||
             !bm_string_view_is_empty(target->archive_output_directory) ||
             !bm_string_view_is_empty(target->library_output_directory) ||
             !bm_string_view_is_empty(target->runtime_output_directory))) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "semantic", "imported target may not declare local build output properties", "remove output properties from imported targets");
        }

        if (target->kind != BM_TARGET_INTERFACE_LIBRARY) {
            for (size_t a = 0; a < arena_arr_len(target->sources); ++a) {
                for (size_t b = a + 1; b < arena_arr_len(target->sources); ++b) {
                    if (nob_sv_eq(target->sources[a], target->sources[b])) {
                        bm_diag_warn(sink, target->provenance, "build_model_validate", "semantic", "duplicate source path inside target", "deduplicate repeated source entries on the same target");
                    }
                }
            }
        }
    }
    return true;
}

bool bm_validate_draft(const Build_Model_Draft *draft, Arena *scratch, Diag_Sink *sink) {
    bool had_error = false;

    if (!draft || !scratch) {
        bm_diag_error(sink, (BM_Provenance){0}, "build_model_validate", "validate", "draft and scratch arena are required", "pass a finalized draft and a scratch arena");
        return false;
    }

    bm_validate_structural_pass(draft, sink, &had_error);
    bm_validate_resolution_pass(draft, sink, &had_error);
    if (!bm_validate_explicit_cycles(draft, scratch, sink, &had_error)) {
        bm_diag_error(sink,
                      (BM_Provenance){0},
                      "build_model_validate",
                      "cycles",
                      "cycle validation failed internally",
                      "ensure a valid scratch arena is provided for cycle detection");
        return false;
    }
    bm_validate_semantic_pass(draft, sink, &had_error);
    return !had_error;
}
