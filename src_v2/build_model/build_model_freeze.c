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
        record.producer_step_id = BM_BUILD_STEP_ID_INVALID;
        if (!bm_copy_string(arena, src[i].raw_path, &record.raw_path) ||
            !bm_copy_string(arena, src[i].effective_path, &record.effective_path) ||
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
            !bm_clone_target_source_records(arena, &target.source_records, src->source_records) ||
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
            !bm_clone_build_step_commands(arena, &step.commands, src->commands) ||
            !bm_clone_provenance(arena, &step.provenance, src->provenance) ||
            !arena_arr_push(arena, model->build_steps, step)) {
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

static bool bm_resolve_build_step_dependencies(const Build_Model_Draft *draft,
                                               Build_Model *model,
                                               Arena *arena) {
    for (size_t i = 0; i < arena_arr_len(model->build_steps); ++i) {
        BM_Build_Step_Record *step = &model->build_steps[i];
        const BM_Directory_Record *owner_directory = &model->directories[step->owner_directory_id];
        for (size_t dep = 0; dep < arena_arr_len(step->raw_dependency_tokens); ++dep) {
            String_View token = step->raw_dependency_tokens[dep];
            BM_Target_Id target_id = bm_draft_find_target_id(draft, token);
            if (target_id != BM_TARGET_ID_INVALID) {
                if (!arena_arr_push(arena, step->resolved_target_dependencies, target_id)) return false;
                continue;
            }

            String_View normalized_token = {0};
            String_View effective = {0};
            BM_Build_Step_Id producer_id = BM_BUILD_STEP_ID_INVALID;
            if (!bm_normalize_path(arena, token, &normalized_token)) return false;
            producer_id = bm_find_producer_step_id_by_path(model, normalized_token);
            if (producer_id != BM_BUILD_STEP_ID_INVALID) {
                if (!arena_arr_push(arena, step->resolved_producer_dependencies, producer_id)) return false;
                continue;
            }
            if (!bm_path_rebase_if_needed(arena, owner_directory->source_dir, token, &effective)) return false;
            producer_id = bm_find_producer_step_id_by_path(model, effective);
            if (producer_id != BM_BUILD_STEP_ID_INVALID) {
                if (!arena_arr_push(arena, step->resolved_producer_dependencies, producer_id)) return false;
                continue;
            }
            if (!arena_arr_push(arena, step->resolved_file_dependencies, effective)) return false;
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
        if (!bm_copy_string(arena, src->name, &record.name) ||
            !bm_copy_string(arena, src->export_namespace, &record.export_namespace) ||
            !bm_copy_string(arena, src->destination, &record.destination) ||
            !bm_copy_string(arena, src->file_name, &record.file_name) ||
            !bm_copy_string(arena, src->component, &record.component) ||
            !bm_clone_provenance(arena, &record.provenance, src->provenance)) {
            return false;
        }
        record.target_ids = NULL;
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
        !bm_clone_tests(draft, model, out_arena) ||
        !bm_clone_install_rules(draft, model, out_arena, sink) ||
        !bm_clone_exports(draft, model, out_arena, sink) ||
        !bm_clone_packages(draft, model, out_arena) ||
        !bm_clone_cpack(draft, model, out_arena, sink)) {
        return NULL;
    }

    if (!bm_resolve_build_step_effective_paths(draft, model, out_arena, sink) ||
        !bm_resolve_build_step_dependencies(draft, model, out_arena) ||
        !bm_resolve_target_source_records(draft, model, out_arena) ||
        !bm_apply_generated_source_marks(draft, model)) {
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
