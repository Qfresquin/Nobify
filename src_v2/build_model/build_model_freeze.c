#include "build_model_internal.h"

static bool bm_freeze_check_invariants(const Build_Model_Draft *draft, Diag_Sink *sink) {
    if (draft->has_semantic_entities && draft->root_directory_id == BM_DIRECTORY_ID_INVALID) {
        bm_diag_error(sink,
                      (BM_Provenance){0},
                      "build_model_freeze",
                      "freeze",
                      "draft is missing root directory",
                      "run validation before freeze and fix root directory reconstruction");
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(draft->directories); ++i) {
        if (draft->directories[i].id != (BM_Directory_Id)i) {
            bm_diag_error(sink,
                          draft->directories[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "directory ids are not contiguous",
                          "freeze requires validated contiguous ids");
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->targets); ++i) {
        if (draft->targets[i].id != (BM_Target_Id)i) {
            bm_diag_error(sink,
                          draft->targets[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "target ids are not contiguous",
                          "freeze requires validated contiguous ids");
            return false;
        }
        if (draft->targets[i].owner_directory_id != BM_DIRECTORY_ID_INVALID &&
            (size_t)draft->targets[i].owner_directory_id >= arena_arr_len(draft->directories)) {
            bm_diag_error(sink,
                          draft->targets[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "target owner_directory_id is invalid",
                          "freeze requires validated entity ownership");
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->build_steps); ++i) {
        if (draft->build_steps[i].id != (BM_Build_Step_Id)i) {
            bm_diag_error(sink,
                          draft->build_steps[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "build step ids are not contiguous",
                          "freeze requires validated contiguous build step ids");
            return false;
        }
        if (draft->build_steps[i].owner_directory_id != BM_DIRECTORY_ID_INVALID &&
            (size_t)draft->build_steps[i].owner_directory_id >= arena_arr_len(draft->directories)) {
            bm_diag_error(sink,
                          draft->build_steps[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "build step owner_directory_id is invalid",
                          "freeze requires validated build step ownership");
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->replay_actions); ++i) {
        if (draft->replay_actions[i].id != (BM_Replay_Action_Id)i) {
            bm_diag_error(sink,
                          draft->replay_actions[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "replay action ids are not contiguous",
                          "freeze requires validated contiguous replay action ids");
            return false;
        }
        if (draft->replay_actions[i].owner_directory_id != BM_DIRECTORY_ID_INVALID &&
            (size_t)draft->replay_actions[i].owner_directory_id >= arena_arr_len(draft->directories)) {
            bm_diag_error(sink,
                          draft->replay_actions[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "replay action owner_directory_id is invalid",
                          "freeze requires validated replay action ownership");
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->exports); ++i) {
        if (draft->exports[i].id != (BM_Export_Id)i) {
            bm_diag_error(sink,
                          draft->exports[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "export ids are not contiguous",
                          "freeze requires validated contiguous export ids");
            return false;
        }
        if (draft->exports[i].owner_directory_id != BM_DIRECTORY_ID_INVALID &&
            (size_t)draft->exports[i].owner_directory_id >= arena_arr_len(draft->directories)) {
            bm_diag_error(sink,
                          draft->exports[i].provenance,
                          "build_model_freeze",
                          "freeze",
                          "export owner_directory_id is invalid",
                          "freeze requires validated export ownership");
            return false;
        }
    }

    return true;
}

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

static bool bm_clone_link_item_array(Arena *arena, BM_Link_Item_View **dest, const BM_Link_Item_View *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_Link_Item_View item = src[i];
        if (!bm_copy_string(arena, src[i].value, &item.value) ||
            !bm_copy_string(arena, src[i].target_name, &item.target_name) ||
            !bm_clone_provenance(arena, &item.provenance, src[i].provenance) ||
            !arena_arr_push(arena, *dest, item)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_build_step_dependencies(Arena *arena,
                                             BM_Build_Step_Dependency_Record **dest,
                                             const BM_Build_Step_Dependency_Record *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_Build_Step_Dependency_Record record = src[i];
        record.target_id = BM_TARGET_ID_INVALID;
        record.producer_step_id = BM_BUILD_STEP_ID_INVALID;
        record.file_dependency = (String_View){0};
        if (!bm_copy_string(arena, src[i].raw_token, &record.raw_token) ||
            !bm_copy_string(arena, src[i].target_name, &record.target_name) ||
            !arena_arr_push(arena, *dest, record)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_imported_config_maps(Arena *arena,
                                          BM_Imported_Config_Map_Record **dest,
                                          const BM_Imported_Config_Map_Record *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_Imported_Config_Map_Record record = {0};
        if (!bm_copy_string(arena, src[i].config, &record.config) ||
            !bm_clone_string_array(arena, &record.mapped_configs, src[i].mapped_configs) ||
            !arena_arr_push(arena, *dest, record)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_imported_configs(Arena *arena,
                                      BM_Imported_Config_Record **dest,
                                      const BM_Imported_Config_Record *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_Imported_Config_Record record = {0};
        if (!bm_copy_string(arena, src[i].config, &record.config) ||
            !bm_copy_string(arena, src[i].effective_file, &record.effective_file) ||
            !bm_copy_string(arena, src[i].effective_linker_file, &record.effective_linker_file) ||
            !bm_clone_string_array(arena, &record.link_languages, src[i].link_languages) ||
            !arena_arr_push(arena, *dest, record)) {
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

static bool bm_clone_target_source_records(Arena *arena,
                                           BM_Target_Source_Record **dest,
                                           const BM_Target_Source_Record *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_Target_Source_Record record = src[i];
        record.generated = src[i].generated;
        record.header_file_only = src[i].header_file_only;
        record.producer_step_id = BM_BUILD_STEP_ID_INVALID;
        if (!bm_copy_string(arena, src[i].raw_path, &record.raw_path) ||
            !bm_copy_string(arena, src[i].effective_path, &record.effective_path) ||
            !bm_copy_string(arena, src[i].file_set_name, &record.file_set_name) ||
            !bm_copy_string(arena, src[i].language, &record.language) ||
            !bm_clone_item_array(arena, &record.compile_definitions, src[i].compile_definitions) ||
            !bm_clone_item_array(arena, &record.compile_options, src[i].compile_options) ||
            !bm_clone_item_array(arena, &record.include_directories, src[i].include_directories) ||
            !bm_clone_raw_properties(arena, &record.raw_properties, src[i].raw_properties) ||
            !bm_clone_provenance(arena, &record.provenance, src[i].provenance) ||
            !arena_arr_push(arena, *dest, record)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_target_file_sets(Arena *arena,
                                      BM_Target_File_Set_Record **dest,
                                      const BM_Target_File_Set_Record *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_Target_File_Set_Record record = src[i];
        record.source_indices = NULL;
        record.raw_files = NULL;
        record.effective_files = NULL;
        if (!bm_copy_string(arena, src[i].name, &record.name) ||
            !bm_clone_string_array(arena, &record.base_dirs, src[i].base_dirs) ||
            !bm_clone_provenance(arena, &record.provenance, src[i].provenance) ||
            !arena_arr_push(arena, *dest, record)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_build_step_commands(Arena *arena,
                                         BM_Build_Step_Command_Record **dest,
                                         const BM_Build_Step_Command_Record *src) {
    if (!dest) return false;
    *dest = NULL;
    for (size_t i = 0; i < arena_arr_len(src); ++i) {
        BM_Build_Step_Command_Record command = {0};
        if (!bm_clone_string_array(arena, &command.argv, src[i].argv) ||
            !arena_arr_push(arena, *dest, command)) {
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
           bm_clone_link_item_array(arena, &dest->link_libraries, src->link_libraries) &&
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
            !bm_clone_link_item_array(arena, &record.link_libraries, draft->directories[i].link_libraries) ||
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
            !bm_clone_target_source_records(arena, &target.source_records, src->source_records) ||
            !bm_clone_target_file_sets(arena, &target.file_sets, src->file_sets) ||
            !bm_clone_string_array(arena, &target.explicit_dependency_names, src->explicit_dependency_names) ||
            !bm_clone_link_item_array(arena, &target.link_libraries, src->link_libraries) ||
            !bm_clone_item_array(arena, &target.link_options, src->link_options) ||
            !bm_clone_item_array(arena, &target.link_directories, src->link_directories) ||
            !bm_clone_item_array(arena, &target.include_directories, src->include_directories) ||
            !bm_clone_item_array(arena, &target.compile_definitions, src->compile_definitions) ||
            !bm_clone_item_array(arena, &target.compile_options, src->compile_options) ||
            !bm_clone_item_array(arena, &target.compile_features, src->compile_features) ||
            !bm_clone_imported_config_maps(arena, &target.imported_config_maps, src->imported_config_maps) ||
            !bm_clone_imported_configs(arena, &target.imported_configs, src->imported_configs) ||
            !bm_clone_raw_properties(arena, &target.raw_properties, src->raw_properties)) {
            return false;
        }

        if (!bm_string_view_is_empty(src->alias_of_name)) {
            target.alias_of_id = bm_draft_find_target_id(draft, src->alias_of_name);
            if (target.alias_of_id == BM_TARGET_ID_INVALID) {
                bm_diag_error(sink, src->provenance, "build_model_freeze", "freeze", "alias target could not be resolved during freeze", "fix unresolved alias targets before freeze");
                return false;
            }
            target.kind = draft->targets[target.alias_of_id].kind;
            target.alias_global = draft->targets[target.alias_of_id].imported
                ? draft->targets[target.alias_of_id].imported_global
                : true;
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

static bool bm_clone_build_steps(const Build_Model_Draft *draft, Build_Model *model, Arena *arena) {
    for (size_t i = 0; i < arena_arr_len(draft->build_steps); ++i) {
        const BM_Build_Step_Record *src = &draft->build_steps[i];
        BM_Build_Step_Record step = *src;
        step.owner_target_id = BM_TARGET_ID_INVALID;
        step.resolved_target_dependencies = NULL;
        step.resolved_producer_dependencies = NULL;
        step.resolved_file_dependencies = NULL;
        if (!bm_copy_string(arena, src->step_key, &step.step_key) ||
            !bm_copy_string(arena, src->owner_target_name, &step.owner_target_name) ||
            !bm_copy_string(arena, src->working_directory, &step.working_directory) ||
            !bm_copy_string(arena, src->comment, &step.comment) ||
            !bm_copy_string(arena, src->main_dependency, &step.main_dependency) ||
            !bm_copy_string(arena, src->depfile, &step.depfile) ||
            !bm_copy_string(arena, src->job_pool, &step.job_pool) ||
            !bm_copy_string(arena, src->job_server_aware, &step.job_server_aware) ||
            !bm_clone_string_array(arena, &step.raw_outputs, src->raw_outputs) ||
            !bm_clone_string_array(arena, &step.raw_byproducts, src->raw_byproducts) ||
            !bm_clone_string_array(arena, &step.raw_dependency_tokens, src->raw_dependency_tokens) ||
            !bm_clone_build_step_dependencies(arena, &step.dependencies, src->dependencies) ||
            !bm_clone_build_step_commands(arena, &step.commands, src->commands) ||
            !bm_clone_provenance(arena, &step.provenance, src->provenance) ||
            !arena_arr_push(arena, model->build_steps, step)) {
            return false;
        }
    }
    return true;
}

static bool bm_clone_replay_actions(const Build_Model_Draft *draft, Build_Model *model, Arena *arena) {
    for (size_t i = 0; i < arena_arr_len(draft->replay_actions); ++i) {
        const BM_Replay_Action_Record *src = &draft->replay_actions[i];
        BM_Replay_Action_Record action = *src;
        action.action_key = (String_View){0};
        if (!bm_copy_string(arena, src->working_directory, &action.working_directory) ||
            !bm_clone_string_array(arena, &action.inputs, src->inputs) ||
            !bm_clone_string_array(arena, &action.outputs, src->outputs) ||
            !bm_clone_string_array(arena, &action.argv, src->argv) ||
            !bm_clone_string_array(arena, &action.environment, src->environment) ||
            !bm_clone_provenance(arena, &action.provenance, src->provenance) ||
            !arena_arr_push(arena, model->replay_actions, action)) {
            return false;
        }
    }
    return true;
}

static bool bm_resolve_build_step_effective_paths(const Build_Model_Draft *draft,
                                                  Build_Model *model,
                                                  Arena *arena,
                                                  Diag_Sink *sink) {
    for (size_t i = 0; i < arena_arr_len(model->build_steps); ++i) {
        BM_Build_Step_Record *step = &model->build_steps[i];
        const BM_Directory_Record *owner_directory = NULL;
        if ((size_t)step->owner_directory_id >= arena_arr_len(model->directories)) return false;
        owner_directory = &model->directories[step->owner_directory_id];
        if (!bm_string_view_is_empty(step->owner_target_name)) {
            step->owner_target_id = bm_draft_find_target_id(draft, step->owner_target_name);
            if (step->owner_target_id == BM_TARGET_ID_INVALID) {
                bm_diag_error(sink,
                              step->provenance,
                              "build_model_freeze",
                              "freeze",
                              "build step owner target could not be resolved during freeze",
                              "declare the owner target before emitting target hook steps");
                return false;
            }
        }
        if (!bm_string_view_is_empty(step->working_directory) &&
            !bm_path_rebase(arena, owner_directory->binary_dir, step->working_directory, &step->working_directory)) {
            return false;
        }
        if (!bm_string_view_is_empty(step->depfile) &&
            !bm_path_rebase(arena, owner_directory->binary_dir, step->depfile, &step->depfile)) {
            return false;
        }
        for (size_t output = 0; output < arena_arr_len(step->raw_outputs); ++output) {
            String_View effective = {0};
            if (!bm_path_rebase(arena, owner_directory->binary_dir, step->raw_outputs[output], &effective) ||
                !arena_arr_push(arena, step->effective_outputs, effective)) {
                return false;
            }
        }
        for (size_t byproduct = 0; byproduct < arena_arr_len(step->raw_byproducts); ++byproduct) {
            String_View effective = {0};
            if (!bm_path_rebase(arena, owner_directory->binary_dir, step->raw_byproducts[byproduct], &effective) ||
                !arena_arr_push(arena, step->effective_byproducts, effective)) {
                return false;
            }
        }
    }
    return true;
}

static bool bm_promote_custom_target_kinds(Build_Model *model) {
    if (!model) return false;
    for (size_t i = 0; i < arena_arr_len(model->build_steps); ++i) {
        const BM_Build_Step_Record *step = &model->build_steps[i];
        BM_Target_Record *target = NULL;
        if (step->kind != BM_BUILD_STEP_CUSTOM_TARGET) continue;
        if (!bm_target_id_is_valid(step->owner_target_id)) continue;
        if ((size_t)step->owner_target_id >= arena_arr_len(model->targets)) return false;
        target = &model->targets[step->owner_target_id];
        if (!target->alias && !target->imported && target->kind == BM_TARGET_UNKNOWN_LIBRARY) {
            target->kind = BM_TARGET_UTILITY;
        }
    }
    return true;
}

static BM_Build_Step_Id bm_find_producer_step_id_by_path(const Build_Model *model, String_View effective_path) {
    if (!model) return BM_BUILD_STEP_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->build_steps); ++i) {
        const BM_Build_Step_Record *step = &model->build_steps[i];
        for (size_t output = 0; output < arena_arr_len(step->effective_outputs); ++output) {
            if (nob_sv_eq(step->effective_outputs[output], effective_path)) return step->id;
        }
        for (size_t byproduct = 0; byproduct < arena_arr_len(step->effective_byproducts); ++byproduct) {
            if (nob_sv_eq(step->effective_byproducts[byproduct], effective_path)) return step->id;
        }
    }
    return BM_BUILD_STEP_ID_INVALID;
}

static const BM_Source_Generated_Mark_Record *bm_find_generated_source_mark(const Build_Model_Draft *draft,
                                                                            String_View effective_path) {
    if (!draft) return NULL;
    for (size_t i = 0; i < arena_arr_len(draft->generated_source_marks); ++i) {
        const BM_Source_Generated_Mark_Record *mark = &draft->generated_source_marks[i];
        if (nob_sv_eq(mark->path, effective_path)) return mark;
    }
    return NULL;
}

static bool bm_sv_has_path_prefix(String_View value, String_View prefix) {
    if (prefix.count == 0 || value.count < prefix.count) return false;
    if (memcmp(value.data, prefix.data, prefix.count) != 0) return false;
    return value.count == prefix.count || value.data[prefix.count] == '/';
}

static bool bm_path_rebase_if_needed(Arena *arena,
                                     String_View base,
                                     String_View value,
                                     String_View *out) {
    String_View normalized_value = {0};
    String_View normalized_base = {0};
    if (!arena || !out) return false;
    if (!bm_normalize_path(arena, value, &normalized_value)) return false;
    if (bm_path_is_abs(value)) {
        *out = normalized_value;
        return true;
    }
    if (!bm_normalize_path(arena, base, &normalized_base)) return false;
    if (bm_sv_has_path_prefix(normalized_value, normalized_base)) {
        *out = normalized_value;
        return true;
    }
    return bm_path_rebase(arena, base, value, out);
}

static bool bm_path_strip_prefix(Arena *arena,
                                 String_View value,
                                 String_View prefix,
                                 String_View *out) {
    String_View normalized_value = {0};
    String_View normalized_prefix = {0};
    if (!arena || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!bm_normalize_path(arena, value, &normalized_value) ||
        !bm_normalize_path(arena, prefix, &normalized_prefix)) {
        return false;
    }
    if (!bm_sv_has_path_prefix(normalized_value, normalized_prefix)) return true;
    if (normalized_value.count == normalized_prefix.count) {
        *out = nob_sv_from_cstr(".");
        return true;
    }
    *out = nob_sv_from_parts(normalized_value.data + normalized_prefix.count + 1,
                             normalized_value.count - normalized_prefix.count - 1);
    return true;
}

static BM_Target_Id bm_freeze_resolve_alias_target_id(const Build_Model *model, BM_Target_Id id) {
    size_t remaining = model ? arena_arr_len(model->targets) : 0;
    BM_Target_Id current = id;
    while (remaining-- > 0 && bm_target_id_is_valid(current)) {
        const BM_Target_Record *target = &model->targets[current];
        if (!target->alias) return current;
        current = target->alias_of_id;
    }
    return BM_TARGET_ID_INVALID;
}

static bool bm_resolve_link_item_target_ids(const Build_Model_Draft *draft, Build_Model *model) {
    if (!draft || !model) return false;

    for (size_t i = 0; i < arena_arr_len(model->targets); ++i) {
        for (size_t item_index = 0; item_index < arena_arr_len(model->targets[i].link_libraries); ++item_index) {
            BM_Link_Item_View *item = &model->targets[i].link_libraries[item_index];
            BM_Target_Id target_id = BM_TARGET_ID_INVALID;
            if (item->kind != BM_LINK_ITEM_TARGET_REF || item->target_name.count == 0) continue;
            target_id = bm_draft_find_target_id(draft, item->target_name);
            if (bm_target_id_is_valid(target_id)) target_id = bm_freeze_resolve_alias_target_id(model, target_id);
            item->target_id = target_id;
        }
    }

    for (size_t i = 0; i < arena_arr_len(model->directories); ++i) {
        for (size_t item_index = 0; item_index < arena_arr_len(model->directories[i].link_libraries); ++item_index) {
            BM_Link_Item_View *item = &model->directories[i].link_libraries[item_index];
            BM_Target_Id target_id = BM_TARGET_ID_INVALID;
            if (item->kind != BM_LINK_ITEM_TARGET_REF || item->target_name.count == 0) continue;
            target_id = bm_draft_find_target_id(draft, item->target_name);
            if (bm_target_id_is_valid(target_id)) target_id = bm_freeze_resolve_alias_target_id(model, target_id);
            item->target_id = target_id;
        }
    }

    for (size_t i = 0; i < arena_arr_len(model->global_properties.link_libraries); ++i) {
        BM_Link_Item_View *item = &model->global_properties.link_libraries[i];
        BM_Target_Id target_id = BM_TARGET_ID_INVALID;
        if (item->kind != BM_LINK_ITEM_TARGET_REF || item->target_name.count == 0) continue;
        target_id = bm_draft_find_target_id(draft, item->target_name);
        if (bm_target_id_is_valid(target_id)) target_id = bm_freeze_resolve_alias_target_id(model, target_id);
        item->target_id = target_id;
    }

    return true;
}

static bool bm_resolve_build_step_dependencies(const Build_Model_Draft *draft,
                                               Build_Model *model,
                                               Arena *arena) {
    for (size_t i = 0; i < arena_arr_len(model->build_steps); ++i) {
        BM_Build_Step_Record *step = &model->build_steps[i];
        const BM_Directory_Record *owner_directory = &model->directories[step->owner_directory_id];
        for (size_t dep = 0; dep < arena_arr_len(step->dependencies); ++dep) {
            BM_Build_Step_Dependency_Record *record = &step->dependencies[dep];
            String_View token = record->raw_token;
            if (record->kind == BM_BUILD_STEP_DEP_TARGET_REF) {
                BM_Target_Id target_id = bm_draft_find_target_id(draft, record->target_name);
                if (bm_target_id_is_valid(target_id)) target_id = bm_freeze_resolve_alias_target_id(model, target_id);
                record->target_id = target_id;
                if (!bm_target_id_is_valid(target_id)) continue;
                if (!arena_arr_push(arena, step->resolved_target_dependencies, target_id)) return false;
                continue;
            }

            String_View normalized_token = {0};
            String_View effective = {0};
            BM_Build_Step_Id producer_id = BM_BUILD_STEP_ID_INVALID;
            if (!bm_normalize_path(arena, token, &normalized_token)) return false;
            producer_id = bm_find_producer_step_id_by_path(model, normalized_token);
            if (producer_id != BM_BUILD_STEP_ID_INVALID) {
                record->producer_step_id = producer_id;
                if (!arena_arr_push(arena, step->resolved_producer_dependencies, producer_id)) return false;
                continue;
            }
            if (!bm_path_rebase_if_needed(arena, owner_directory->source_dir, token, &effective)) return false;
            producer_id = bm_find_producer_step_id_by_path(model, effective);
            if (producer_id != BM_BUILD_STEP_ID_INVALID) {
                record->producer_step_id = producer_id;
                if (!arena_arr_push(arena, step->resolved_producer_dependencies, producer_id)) return false;
                continue;
            }
            record->file_dependency = effective;
            if (!arena_arr_push(arena, step->resolved_file_dependencies, effective)) return false;
        }
    }
    return true;
}

static BM_Imported_Config_Record *bm_imported_config_ensure(Arena *arena,
                                                            BM_Target_Record *target,
                                                            String_View config) {
    BM_Imported_Config_Record record = {0};
    if (!arena || !target) return NULL;
    for (size_t i = 0; i < arena_arr_len(target->imported_configs); ++i) {
        if (nob_sv_eq(target->imported_configs[i].config, config)) return &target->imported_configs[i];
    }
    if (!bm_copy_string(arena, config, &record.config) ||
        !arena_arr_push(arena, target->imported_configs, record)) {
        return NULL;
    }
    return &arena_arr_last(target->imported_configs);
}

static BM_Imported_Config_Map_Record *bm_imported_config_map_ensure(Arena *arena,
                                                                    BM_Target_Record *target,
                                                                    String_View config) {
    BM_Imported_Config_Map_Record record = {0};
    if (!arena || !target) return NULL;
    for (size_t i = 0; i < arena_arr_len(target->imported_config_maps); ++i) {
        if (nob_sv_eq(target->imported_config_maps[i].config, config)) return &target->imported_config_maps[i];
    }
    if (!bm_copy_string(arena, config, &record.config) ||
        !arena_arr_push(arena, target->imported_config_maps, record)) {
        return NULL;
    }
    return &arena_arr_last(target->imported_config_maps);
}

static bool bm_imported_config_set_path(Arena *arena,
                                        String_View source_dir,
                                        String_View raw_value,
                                        String_View *dest) {
    if (!dest) return false;
    if (dest->count > 0) return true;
    return bm_path_rebase(arena, source_dir, raw_value, dest);
}

static bool bm_materialize_imported_target_metadata(Build_Model *model, Arena *arena) {
    if (!model || !arena) return false;

    for (size_t i = 0; i < arena_arr_len(model->targets); ++i) {
        BM_Target_Record *target = &model->targets[i];
        String_View source_dir = bm_query_directory_source_dir(model, target->owner_directory_id);
        if (!target->imported) continue;

        for (size_t prop_index = 0; prop_index < arena_arr_len(target->raw_properties); ++prop_index) {
            const BM_Raw_Property_Record *prop = &target->raw_properties[prop_index];
            String_View name = prop->name;
            String_View value = arena_arr_len(prop->items) > 0 ? prop->items[0] : nob_sv_from_cstr("");
            const char *config_suffix = NULL;
            BM_Imported_Config_Record *config_record = NULL;
            BM_Imported_Config_Map_Record *map_record = NULL;

            if (bm_sv_eq_ci_lit(name, "IMPORTED_LOCATION")) {
                config_record = bm_imported_config_ensure(arena, target, nob_sv_from_cstr(""));
                if (!config_record || !bm_imported_config_set_path(arena, source_dir, value, &config_record->effective_file)) {
                    return false;
                }
                continue;
            }
            if (bm_sv_eq_ci_lit(name, "IMPORTED_IMPLIB")) {
                config_record = bm_imported_config_ensure(arena, target, nob_sv_from_cstr(""));
                if (!config_record ||
                    !bm_imported_config_set_path(arena, source_dir, value, &config_record->effective_linker_file)) {
                    return false;
                }
                continue;
            }
            if (bm_sv_eq_ci_lit(name, "IMPORTED_LINK_INTERFACE_LANGUAGES")) {
                config_record = bm_imported_config_ensure(arena, target, nob_sv_from_cstr(""));
                if (!config_record) return false;
                for (size_t item_index = 0; item_index < arena_arr_len(prop->items); ++item_index) {
                    String_View *langs = NULL;
                    if (!bm_split_cmake_list(arena, prop->items[item_index], &langs)) return false;
                    for (size_t lang_index = 0; lang_index < arena_arr_len(langs); ++lang_index) {
                        String_View copy = {0};
                        if (!bm_copy_string(arena, langs[lang_index], &copy) ||
                            !arena_arr_push(arena, config_record->link_languages, copy)) {
                            return false;
                        }
                    }
                }
                continue;
            }

            if (name.count > strlen("IMPORTED_LOCATION_") &&
                memcmp(name.data, "IMPORTED_LOCATION_", strlen("IMPORTED_LOCATION_")) == 0) {
                config_suffix = "IMPORTED_LOCATION_";
                config_record = bm_imported_config_ensure(
                    arena,
                    target,
                    nob_sv_from_parts(name.data + strlen(config_suffix), name.count - strlen(config_suffix)));
                if (!config_record ||
                    !bm_imported_config_set_path(arena, source_dir, value, &config_record->effective_file)) {
                    return false;
                }
                continue;
            }
            if (name.count > strlen("IMPORTED_IMPLIB_") &&
                memcmp(name.data, "IMPORTED_IMPLIB_", strlen("IMPORTED_IMPLIB_")) == 0) {
                config_suffix = "IMPORTED_IMPLIB_";
                config_record = bm_imported_config_ensure(
                    arena,
                    target,
                    nob_sv_from_parts(name.data + strlen(config_suffix), name.count - strlen(config_suffix)));
                if (!config_record ||
                    !bm_imported_config_set_path(arena, source_dir, value, &config_record->effective_linker_file)) {
                    return false;
                }
                continue;
            }
            if (name.count > strlen("IMPORTED_LINK_INTERFACE_LANGUAGES_") &&
                memcmp(name.data,
                       "IMPORTED_LINK_INTERFACE_LANGUAGES_",
                       strlen("IMPORTED_LINK_INTERFACE_LANGUAGES_")) == 0) {
                config_suffix = "IMPORTED_LINK_INTERFACE_LANGUAGES_";
                config_record = bm_imported_config_ensure(
                    arena,
                    target,
                    nob_sv_from_parts(name.data + strlen(config_suffix), name.count - strlen(config_suffix)));
                if (!config_record) return false;
                for (size_t item_index = 0; item_index < arena_arr_len(prop->items); ++item_index) {
                    String_View *langs = NULL;
                    if (!bm_split_cmake_list(arena, prop->items[item_index], &langs)) return false;
                    for (size_t lang_index = 0; lang_index < arena_arr_len(langs); ++lang_index) {
                        String_View copy = {0};
                        if (!bm_copy_string(arena, langs[lang_index], &copy) ||
                            !arena_arr_push(arena, config_record->link_languages, copy)) {
                            return false;
                        }
                    }
                }
                continue;
            }
            if (name.count > strlen("MAP_IMPORTED_CONFIG_") &&
                memcmp(name.data, "MAP_IMPORTED_CONFIG_", strlen("MAP_IMPORTED_CONFIG_")) == 0) {
                config_suffix = "MAP_IMPORTED_CONFIG_";
                map_record = bm_imported_config_map_ensure(
                    arena,
                    target,
                    nob_sv_from_parts(name.data + strlen(config_suffix), name.count - strlen(config_suffix)));
                if (!map_record) return false;
                for (size_t item_index = 0; item_index < arena_arr_len(prop->items); ++item_index) {
                    String_View *configs = NULL;
                    if (!bm_split_cmake_list(arena, prop->items[item_index], &configs)) return false;
                    for (size_t cfg_index = 0; cfg_index < arena_arr_len(configs); ++cfg_index) {
                        String_View copy = {0};
                        if (!bm_copy_string(arena, configs[cfg_index], &copy) ||
                            !arena_arr_push(arena, map_record->mapped_configs, copy)) {
                            return false;
                        }
                    }
                }
                continue;
            }
        }
    }
    return true;
}

static bool bm_resolve_target_source_records(const Build_Model_Draft *draft,
                                             Build_Model *model,
                                             Arena *arena) {
    for (size_t i = 0; i < arena_arr_len(model->targets); ++i) {
        BM_Target_Record *target = &model->targets[i];
        const BM_Directory_Record *owner_directory = &model->directories[target->owner_directory_id];
        for (size_t source_index = 0; source_index < arena_arr_len(target->source_records); ++source_index) {
            BM_Target_Source_Record *source = &target->source_records[source_index];
            String_View direct_effective = {0};
            String_View source_effective = {0};
            String_View binary_effective = {0};
            String_View source_stripped_effective = {0};
            BM_Build_Step_Id direct_producer_id = BM_BUILD_STEP_ID_INVALID;
            BM_Build_Step_Id source_producer_id = BM_BUILD_STEP_ID_INVALID;
            BM_Build_Step_Id binary_producer_id = BM_BUILD_STEP_ID_INVALID;
            BM_Build_Step_Id stripped_producer_id = BM_BUILD_STEP_ID_INVALID;
            const BM_Source_Generated_Mark_Record *direct_mark = NULL;
            const BM_Source_Generated_Mark_Record *source_mark = NULL;
            const BM_Source_Generated_Mark_Record *binary_mark = NULL;
            const BM_Source_Generated_Mark_Record *stripped_mark = NULL;

            if (!bm_normalize_path(arena, source->raw_path, &direct_effective) ||
                !bm_path_rebase_if_needed(arena, owner_directory->source_dir, source->raw_path, &source_effective) ||
                !bm_path_rebase_if_needed(arena, owner_directory->binary_dir, source->raw_path, &binary_effective) ||
                !bm_path_strip_prefix(arena, source_effective, owner_directory->source_dir, &source_stripped_effective)) {
                return false;
            }

            direct_producer_id = bm_find_producer_step_id_by_path(model, direct_effective);
            source_producer_id = bm_find_producer_step_id_by_path(model, source_effective);
            binary_producer_id = bm_find_producer_step_id_by_path(model, binary_effective);
            if (source_stripped_effective.count > 0 &&
                !nob_sv_eq(source_stripped_effective, nob_sv_from_cstr("."))) {
                stripped_producer_id = bm_find_producer_step_id_by_path(model, source_stripped_effective);
            }
            direct_mark = bm_find_generated_source_mark(draft, direct_effective);
            source_mark = bm_find_generated_source_mark(draft, source_effective);
            binary_mark = bm_find_generated_source_mark(draft, binary_effective);
            if (source_stripped_effective.count > 0 &&
                !nob_sv_eq(source_stripped_effective, nob_sv_from_cstr("."))) {
                stripped_mark = bm_find_generated_source_mark(draft, source_stripped_effective);
            }

            source->effective_path = source_effective;
            if (direct_producer_id != BM_BUILD_STEP_ID_INVALID || direct_mark != NULL) {
                source->effective_path = direct_effective;
            } else if (stripped_producer_id != BM_BUILD_STEP_ID_INVALID || stripped_mark != NULL) {
                source->effective_path = source_stripped_effective;
            } else if (binary_producer_id != BM_BUILD_STEP_ID_INVALID || binary_mark != NULL) {
                source->effective_path = binary_effective;
            }

            if (direct_producer_id != BM_BUILD_STEP_ID_INVALID) {
                source->generated = true;
                source->producer_step_id = direct_producer_id;
            } else if (stripped_producer_id != BM_BUILD_STEP_ID_INVALID) {
                source->generated = true;
                source->producer_step_id = stripped_producer_id;
            } else if (binary_producer_id != BM_BUILD_STEP_ID_INVALID) {
                source->generated = true;
                source->producer_step_id = binary_producer_id;
            } else if (source_producer_id != BM_BUILD_STEP_ID_INVALID) {
                source->generated = true;
                source->producer_step_id = source_producer_id;
            } else if (direct_mark != NULL) {
                source->generated = direct_mark->generated;
            } else if (stripped_mark != NULL) {
                source->generated = stripped_mark->generated;
            } else if (binary_mark != NULL) {
                source->generated = binary_mark->generated;
            } else if (source_mark != NULL) {
                source->generated = source_mark->generated;
            }
        }
    }
    return true;
}

static bool bm_apply_generated_source_marks(const Build_Model_Draft *draft, Build_Model *model) {
    for (size_t mark_index = 0; mark_index < arena_arr_len(draft->generated_source_marks); ++mark_index) {
        const BM_Source_Generated_Mark_Record *mark = &draft->generated_source_marks[mark_index];
        for (size_t target_index = 0; target_index < arena_arr_len(model->targets); ++target_index) {
            BM_Target_Record *target = &model->targets[target_index];
            for (size_t source_index = 0; source_index < arena_arr_len(target->source_records); ++source_index) {
                BM_Target_Source_Record *source = &target->source_records[source_index];
                if (!nob_sv_eq(source->effective_path, mark->path)) continue;
                source->generated = mark->generated || source->generated;
            }
        }
    }
    return true;
}

static bool bm_sv_eq_ci_freeze(String_View lhs, String_View rhs) {
    if (lhs.count != rhs.count) return false;
    for (size_t i = 0; i < lhs.count; ++i) {
        if (tolower((unsigned char)lhs.data[i]) != tolower((unsigned char)rhs.data[i])) return false;
    }
    return true;
}

static bool bm_source_candidates_for_target(Arena *arena,
                                            const BM_Directory_Record *owner_directory,
                                            const BM_Target_Source_Record *source,
                                            String_View *direct_effective,
                                            String_View *source_effective,
                                            String_View *binary_effective,
                                            String_View *stripped_effective) {
    if (!arena || !owner_directory || !source ||
        !direct_effective || !source_effective || !binary_effective || !stripped_effective) {
        return false;
    }
    if (!bm_normalize_path(arena, source->raw_path, direct_effective) ||
        !bm_path_rebase_if_needed(arena, owner_directory->source_dir, source->raw_path, source_effective) ||
        !bm_path_rebase_if_needed(arena, owner_directory->binary_dir, source->raw_path, binary_effective) ||
        !bm_path_strip_prefix(arena, *source_effective, owner_directory->source_dir, stripped_effective)) {
        return false;
    }
    return true;
}

static bool bm_source_property_candidates(Arena *arena,
                                          const BM_Source_Property_Mutation_Record *record,
                                          String_View *direct_effective,
                                          String_View *source_effective,
                                          String_View *binary_effective,
                                          String_View *stripped_effective) {
    if (!arena || !record ||
        !direct_effective || !source_effective || !binary_effective || !stripped_effective) {
        return false;
    }
    if (!bm_normalize_path(arena, record->path, direct_effective) ||
        !bm_path_rebase_if_needed(arena, record->directory_source_dir, record->path, source_effective) ||
        !bm_path_rebase_if_needed(arena, record->directory_binary_dir, record->path, binary_effective) ||
        !bm_path_strip_prefix(arena, *source_effective, record->directory_source_dir, stripped_effective)) {
        return false;
    }
    return true;
}

static bool bm_source_property_matches_target_source(Arena *arena,
                                                     const BM_Directory_Record *owner_directory,
                                                     const BM_Target_Source_Record *source,
                                                     const BM_Source_Property_Mutation_Record *mutation) {
    String_View source_direct = {0};
    String_View source_source = {0};
    String_View source_binary = {0};
    String_View source_stripped = {0};
    String_View mutation_direct = {0};
    String_View mutation_source = {0};
    String_View mutation_binary = {0};
    String_View mutation_stripped = {0};
    String_View source_candidates[4];
    String_View mutation_candidates[4];
    if (!bm_source_candidates_for_target(arena,
                                         owner_directory,
                                         source,
                                         &source_direct,
                                         &source_source,
                                         &source_binary,
                                         &source_stripped) ||
        !bm_source_property_candidates(arena,
                                       mutation,
                                       &mutation_direct,
                                       &mutation_source,
                                       &mutation_binary,
                                       &mutation_stripped)) {
        return false;
    }

    source_candidates[0] = source_direct;
    source_candidates[1] = source_source;
    source_candidates[2] = source_binary;
    source_candidates[3] = source_stripped;
    mutation_candidates[0] = mutation_direct;
    mutation_candidates[1] = mutation_source;
    mutation_candidates[2] = mutation_binary;
    mutation_candidates[3] = mutation_stripped;

    for (size_t i = 0; i < NOB_ARRAY_LEN(source_candidates); ++i) {
        if (bm_string_view_is_empty(source_candidates[i]) ||
            nob_sv_eq(source_candidates[i], nob_sv_from_cstr("."))) {
            continue;
        }
        for (size_t j = 0; j < NOB_ARRAY_LEN(mutation_candidates); ++j) {
            if (bm_string_view_is_empty(mutation_candidates[j]) ||
                nob_sv_eq(mutation_candidates[j], nob_sv_from_cstr("."))) {
                continue;
            }
            if (nob_sv_eq(source_candidates[i], mutation_candidates[j])) return true;
        }
    }
    return false;
}

static bool bm_source_property_apply_item_mutation(Arena *arena,
                                                   BM_String_Item_View **dest,
                                                   const BM_Source_Property_Mutation_Record *mutation,
                                                   const String_View *values,
                                                   size_t value_count) {
    BM_String_Item_View *items = NULL;
    uint32_t flags = mutation->op == EVENT_PROPERTY_MUTATE_PREPEND_LIST ? BM_ITEM_FLAG_BEFORE : BM_ITEM_FLAG_NONE;
    if (!arena || !dest || !mutation) return false;
    for (size_t i = 0; i < value_count; ++i) {
        BM_String_Item_View item = {0};
        if (!bm_copy_string(arena, values[i], &item.value)) return false;
        item.visibility = BM_VISIBILITY_PRIVATE;
        item.flags = flags;
        item.provenance = mutation->provenance;
        if (!arena_arr_push(arena, items, item)) return false;
    }
    return bm_apply_item_mutation(arena, dest, items, arena_arr_len(items), mutation->op);
}

static bool bm_source_property_apply(const BM_Source_Property_Mutation_Record *mutation,
                                     BM_Target_Source_Record *source,
                                     Arena *arena) {
    String_View *values = NULL;
    String_View single_value[1];
    size_t value_count = 1;
    if (!mutation || !source || !arena) return false;

    single_value[0] = mutation->value;
    if (!bm_record_raw_property(arena,
                                &source->raw_properties,
                                mutation->key,
                                mutation->op,
                                0,
                                single_value,
                                1,
                                mutation->provenance)) {
        return false;
    }

    if (bm_sv_eq_ci_freeze(mutation->key, nob_sv_from_cstr("GENERATED"))) {
        source->generated = bm_sv_truthy(mutation->value);
        return true;
    }
    if (bm_sv_eq_ci_freeze(mutation->key, nob_sv_from_cstr("HEADER_FILE_ONLY"))) {
        source->header_file_only = bm_sv_truthy(mutation->value);
        return true;
    }
    if (bm_sv_eq_ci_freeze(mutation->key, nob_sv_from_cstr("LANGUAGE"))) {
        return bm_copy_string(arena, mutation->value, &source->language);
    }

    if (mutation->op == EVENT_PROPERTY_MUTATE_APPEND_STRING) {
        values = single_value;
        value_count = 1;
    } else {
        if (!bm_split_cmake_list(arena, mutation->value, &values)) return false;
        if (values) {
            value_count = arena_arr_len(values);
        } else {
            value_count = 0;
        }
    }

    if (bm_sv_eq_ci_freeze(mutation->key, nob_sv_from_cstr("COMPILE_DEFINITIONS"))) {
        return bm_source_property_apply_item_mutation(arena,
                                                      &source->compile_definitions,
                                                      mutation,
                                                      values,
                                                      value_count);
    }
    if (bm_sv_eq_ci_freeze(mutation->key, nob_sv_from_cstr("COMPILE_OPTIONS"))) {
        return bm_source_property_apply_item_mutation(arena,
                                                      &source->compile_options,
                                                      mutation,
                                                      values,
                                                      value_count);
    }
    if (bm_sv_eq_ci_freeze(mutation->key, nob_sv_from_cstr("INCLUDE_DIRECTORIES"))) {
        return bm_source_property_apply_item_mutation(arena,
                                                      &source->include_directories,
                                                      mutation,
                                                      values,
                                                      value_count);
    }
    return true;
}

static bool bm_apply_source_property_mutations(const Build_Model_Draft *draft,
                                               Build_Model *model,
                                               Arena *arena) {
    if (!draft || !model || !arena) return false;
    for (size_t mutation_index = 0; mutation_index < arena_arr_len(draft->source_property_mutations); ++mutation_index) {
        const BM_Source_Property_Mutation_Record *mutation = &draft->source_property_mutations[mutation_index];
        for (size_t target_index = 0; target_index < arena_arr_len(model->targets); ++target_index) {
            BM_Target_Record *target = &model->targets[target_index];
            const BM_Directory_Record *owner_directory = &model->directories[target->owner_directory_id];
            for (size_t source_index = 0; source_index < arena_arr_len(target->source_records); ++source_index) {
                BM_Target_Source_Record *source = &target->source_records[source_index];
                if (!bm_source_property_matches_target_source(arena, owner_directory, source, mutation)) continue;
                if (!bm_source_property_apply(mutation, source, arena)) return false;
            }
        }
    }
    return true;
}

static bool bm_populate_target_file_sets(Build_Model *model, Arena *arena) {
    if (!model || !arena) return false;
    for (size_t target_index = 0; target_index < arena_arr_len(model->targets); ++target_index) {
        BM_Target_Record *target = &model->targets[target_index];
        for (size_t file_set_index = 0; file_set_index < arena_arr_len(target->file_sets); ++file_set_index) {
            BM_Target_File_Set_Record *file_set = &target->file_sets[file_set_index];
            for (size_t source_index = 0; source_index < arena_arr_len(target->source_records); ++source_index) {
                const BM_Target_Source_Record *source = &target->source_records[source_index];
                if (!nob_sv_eq(source->file_set_name, file_set->name)) continue;
                if (!arena_arr_push(arena, file_set->source_indices, source_index) ||
                    !arena_arr_push(arena, file_set->raw_files, source->raw_path) ||
                    !arena_arr_push(arena, file_set->effective_files, source->effective_path)) {
                    return false;
                }
            }
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
            !bm_clone_string_array(arena, &test.configurations, draft->tests[i].configurations) ||
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
            !bm_copy_string(arena, src->component, &rule.component) ||
            !bm_copy_string(arena, src->namelink_component, &rule.namelink_component) ||
            !bm_copy_string(arena, src->export_name, &rule.export_name) ||
            !bm_copy_string(arena, src->archive_destination, &rule.archive_destination) ||
            !bm_copy_string(arena, src->library_destination, &rule.library_destination) ||
            !bm_copy_string(arena, src->runtime_destination, &rule.runtime_destination) ||
            !bm_copy_string(arena, src->includes_destination, &rule.includes_destination) ||
            !bm_copy_string(arena, src->public_header_destination, &rule.public_header_destination) ||
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

static bool bm_clone_exports(const Build_Model_Draft *draft, Build_Model *model, Arena *arena, Diag_Sink *sink) {
    for (size_t i = 0; i < arena_arr_len(draft->exports); ++i) {
        const BM_Export_Record *src = &draft->exports[i];
        BM_Export_Record record = *src;
        if (!bm_copy_string(arena, src->export_key, &record.export_key) ||
            !bm_copy_string(arena, src->name, &record.name) ||
            !bm_copy_string(arena, src->export_namespace, &record.export_namespace) ||
            !bm_copy_string(arena, src->destination, &record.destination) ||
            !bm_copy_string(arena, src->file_name, &record.file_name) ||
            !bm_copy_string(arena, src->component, &record.component) ||
            !bm_copy_string(arena, src->output_file_path, &record.output_file_path) ||
            !bm_copy_string(arena, src->cxx_modules_directory, &record.cxx_modules_directory) ||
            !bm_copy_string(arena, src->registry_prefix, &record.registry_prefix) ||
            !bm_clone_string_array(arena, &record.target_names, src->target_names) ||
            !bm_clone_provenance(arena, &record.provenance, src->provenance)) {
            return false;
        }
        record.target_ids = NULL;
        if (src->kind == BM_EXPORT_INSTALL) {
            for (size_t rule_index = 0; rule_index < arena_arr_len(model->install_rules); ++rule_index) {
                const BM_Install_Rule_Record *rule = &model->install_rules[rule_index];
                if (rule->kind != BM_INSTALL_RULE_TARGET || !nob_sv_eq(rule->export_name, src->name)) continue;
                if (rule->resolved_target_id == BM_TARGET_ID_INVALID) {
                    bm_diag_error(sink,
                                  rule->provenance,
                                  "build_model_freeze",
                                  "freeze",
                                  "install export target could not be resolved during freeze",
                                  "fix unresolved install target names before freezing exports");
                    return false;
                }
                if (!arena_arr_push(arena, record.target_ids, rule->resolved_target_id)) return false;
            }
        } else if (src->kind == BM_EXPORT_BUILD_TREE) {
            for (size_t target_index = 0; target_index < arena_arr_len(src->target_names); ++target_index) {
                BM_Target_Id target_id = bm_draft_find_target_id(draft, src->target_names[target_index]);
                if (target_id == BM_TARGET_ID_INVALID) {
                    bm_diag_error(sink,
                                  src->provenance,
                                  "build_model_freeze",
                                  "freeze",
                                  "build-tree export target could not be resolved during freeze",
                                  "fix unresolved export target names before freezing standalone exports");
                    return false;
                }
                if (!arena_arr_push(arena, record.target_ids, target_id)) return false;
            }
        }
        if (!arena_arr_push(arena, model->exports, record)) return false;
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

    for (size_t i = 0; i < arena_arr_len(draft->cpack_packages); ++i) {
        const BM_CPack_Package_Record *src = &draft->cpack_packages[i];
        BM_CPack_Package_Record record = *src;
        if (!bm_copy_string(arena, src->package_key, &record.package_key) ||
            !bm_copy_string(arena, src->package_name, &record.package_name) ||
            !bm_copy_string(arena, src->package_version, &record.package_version) ||
            !bm_copy_string(arena, src->package_file_name, &record.package_file_name) ||
            !bm_copy_string(arena, src->package_directory, &record.package_directory) ||
            !bm_clone_string_array(arena, &record.generators, src->generators) ||
            !bm_clone_string_array(arena, &record.components_all, src->components_all) ||
            !bm_clone_provenance(arena, &record.provenance, src->provenance) ||
            !arena_arr_push(arena, model->cpack_packages, record)) {
            return false;
        }
    }

    return true;
}

const Build_Model *bm_freeze_draft(const Build_Model_Draft *draft,
                                   Arena *out_arena,
                                   Diag_Sink *sink) {
    Build_Model *model = NULL;
    Arena *validate_arena = NULL;
    bool had_error = false;
    if (!draft || !out_arena) return NULL;
    if (!bm_freeze_check_invariants(draft, sink)) return NULL;

    model = arena_alloc_zero(out_arena, sizeof(*model));
    if (!model) return NULL;

    model->arena = out_arena;
    model->testing_enabled = draft->testing_enabled;
    model->root_directory_id = draft->root_directory_id;

    if (!bm_clone_project(out_arena, &model->project, &draft->project) ||
        !bm_clone_global_state(out_arena, &model->global_properties, &draft->global_properties) ||
        !bm_clone_directories(draft, model, out_arena) ||
        !bm_clone_targets(draft, model, out_arena, sink) ||
        !bm_clone_build_steps(draft, model, out_arena) ||
        !bm_clone_replay_actions(draft, model, out_arena) ||
        !bm_clone_tests(draft, model, out_arena) ||
        !bm_clone_install_rules(draft, model, out_arena, sink) ||
        !bm_clone_exports(draft, model, out_arena, sink) ||
        !bm_clone_packages(draft, model, out_arena) ||
        !bm_clone_cpack(draft, model, out_arena, sink)) {
        return NULL;
    }

    if (!bm_resolve_build_step_effective_paths(draft, model, out_arena, sink) ||
        !bm_resolve_link_item_target_ids(draft, model) ||
        !bm_materialize_imported_target_metadata(model, out_arena) ||
        !bm_promote_custom_target_kinds(model) ||
        !bm_resolve_build_step_dependencies(draft, model, out_arena) ||
        !bm_resolve_target_source_records(draft, model, out_arena) ||
        !bm_apply_generated_source_marks(draft, model) ||
        !bm_apply_source_property_mutations(draft, model, out_arena) ||
        !bm_populate_target_file_sets(model, out_arena)) {
        return NULL;
    }

    validate_arena = arena_create(2 * 1024 * 1024);
    if (!validate_arena) return NULL;
    if (!bm_validate_execution_graph(model, validate_arena, sink, &had_error)) {
        arena_destroy(validate_arena);
        return NULL;
    }
    arena_destroy(validate_arena);
    if (had_error) return NULL;

    return model;
}
