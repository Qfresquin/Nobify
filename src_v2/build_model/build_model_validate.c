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

static bool bm_validate_directory_chain(const Build_Model_Draft *draft,
                                        const BM_Directory_Record *directory,
                                        Diag_Sink *sink,
                                        bool *had_error) {
    BM_Directory_Id current = directory ? directory->parent_id : BM_DIRECTORY_ID_INVALID;
    size_t steps = 0;

    while (current != BM_DIRECTORY_ID_INVALID) {
        if ((size_t)current >= arena_arr_len(draft->directories)) {
            *had_error = true;
            bm_diag_error(sink,
                          directory ? directory->provenance : (BM_Provenance){0},
                          "build_model_validate",
                          "structural",
                          "directory parent chain is invalid",
                          "fix parent ids in directory records");
            return false;
        }

        current = draft->directories[current].parent_id;
        steps++;
        if (steps > arena_arr_len(draft->directories)) {
            *had_error = true;
            bm_diag_error(sink,
                          directory ? directory->provenance : (BM_Provenance){0},
                          "build_model_validate",
                          "structural",
                          "directory parent chain contains a cycle",
                          "ensure directory parent ids form a tree");
            return false;
        }
    }

    return true;
}

static void bm_validate_contiguous_id(bool matches,
                                      BM_Provenance provenance,
                                      Diag_Sink *sink,
                                      bool *had_error,
                                      const char *cause,
                                      const char *hint) {
    if (matches) return;
    *had_error = true;
    bm_diag_error(sink, provenance, "build_model_validate", "structural", cause, hint);
}

static bool bm_validate_parse_count_token(String_View token, size_t *out_value) {
    size_t value = 0;
    if (!out_value || token.count == 0) return false;
    for (size_t i = 0; i < token.count; ++i) {
        unsigned char ch = (unsigned char)token.data[i];
        if (ch < '0' || ch > '9') return false;
        value = value * 10u + (size_t)(ch - '0');
    }
    *out_value = value;
    return true;
}

static bool bm_validate_replay_opcode_kind(const BM_Replay_Action_Record *action,
                                           Diag_Sink *sink,
                                           bool *had_error) {
    if (!action) return false;
    if (action->opcode == BM_REPLAY_OPCODE_NONE) return true;

    if ((action->opcode == BM_REPLAY_OPCODE_FS_MKDIR ||
         action->opcode == BM_REPLAY_OPCODE_FS_WRITE_TEXT ||
         action->opcode == BM_REPLAY_OPCODE_FS_APPEND_TEXT ||
         action->opcode == BM_REPLAY_OPCODE_FS_COPY_FILE) &&
        action->kind == BM_REPLAY_ACTION_FILESYSTEM) {
        return true;
    }

    if ((action->opcode == BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL ||
         action->opcode == BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR ||
         action->opcode == BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR ||
         action->opcode == BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE ||
         action->opcode == BM_REPLAY_OPCODE_HOST_LOCK_RELEASE) &&
        action->kind == BM_REPLAY_ACTION_HOST_EFFECT) {
        return true;
    }

    if ((action->opcode == BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE ||
         action->opcode == BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT ||
         action->opcode == BM_REPLAY_OPCODE_PROBE_TRY_RUN) &&
        action->kind == BM_REPLAY_ACTION_PROBE) {
        return true;
    }

    if ((action->opcode == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR ||
         action->opcode == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE) &&
        action->kind == BM_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION) {
        return true;
    }

    if ((action->opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY ||
         action->opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL ||
         action->opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF ||
         action->opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF ||
         action->opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST ||
         action->opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP ||
         action->opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL ||
         action->opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL) &&
        action->kind == BM_REPLAY_ACTION_TEST_DRIVER) {
        return true;
    }

    *had_error = true;
    bm_diag_error(sink,
                  action->provenance,
                  "build_model_validate",
                  "structural",
                  "replay action opcode is incompatible with replay action kind",
                  "emit filesystem opcodes only for filesystem actions and host opcodes only for host-effect actions");
    return false;
}

static bool bm_validate_replay_payload_shape(const BM_Replay_Action_Record *action,
                                             Diag_Sink *sink,
                                             bool *had_error) {
    size_t input_count = 0;
    size_t output_count = 0;
    size_t argv_count = 0;
    size_t env_count = 0;
    size_t payload_count = 0;
    bool ok = true;
    if (!action) return false;

    input_count = arena_arr_len(action->inputs);
    output_count = arena_arr_len(action->outputs);
    argv_count = arena_arr_len(action->argv);
    env_count = arena_arr_len(action->environment);

    switch (action->opcode) {
        case BM_REPLAY_OPCODE_NONE:
            return true;

        case BM_REPLAY_OPCODE_FS_MKDIR:
            ok = input_count == 0 && output_count > 0 && argv_count == 0 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_FS_WRITE_TEXT:
            ok = input_count == 0 && output_count == 1 && argv_count == 2 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_FS_APPEND_TEXT:
            ok = input_count == 0 && output_count == 1 && argv_count == 1 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_FS_COPY_FILE:
            ok = input_count == 1 && output_count == 1 && argv_count == 1 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL:
            ok = input_count == 1 && output_count == 1 && argv_count == 2 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR:
            ok = input_count > 0 && output_count == 1 && argv_count == 1 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR:
            ok = input_count == 1 && output_count == 1 && argv_count == 0 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE:
        case BM_REPLAY_OPCODE_HOST_LOCK_RELEASE:
            ok = input_count == 0 && output_count == 1 && argv_count == 0 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE:
            ok = input_count > 0 && output_count == 1 && argv_count >= 1 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT:
            ok = input_count == 1 && output_count == 1 && argv_count >= 1 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_PROBE_TRY_RUN:
            ok = input_count == 0 && output_count == 1 && argv_count >= 1 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR:
            ok = input_count == 0 && output_count == 2 && argv_count == 1 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE:
            ok = input_count == 1 && output_count == 2 && argv_count == 4 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY:
            ok = input_count == 0 && output_count == 1 && argv_count == 0 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL:
            ok = input_count == 0 && output_count == 2 && argv_count == 3 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF:
            ok = input_count == 0 && output_count == 2 && argv_count == 0 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF:
            ok = input_count == 0 && output_count == 1 && argv_count == 2 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST:
            ok = input_count == 0 && output_count == 1 && argv_count == 2 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP:
            ok = input_count == 0 && output_count == 0 && argv_count == 1 && env_count == 0;
            break;

        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL:
            ok = output_count == 1 && argv_count >= 4 && env_count == 0;
            if (ok) {
                ok = bm_validate_parse_count_token(action->argv[3], &payload_count) &&
                     argv_count == 4 + payload_count;
            }
            break;

        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL:
            ok = input_count == 2 && output_count == 2 && argv_count >= 14 && env_count == 0;
            if (ok) {
                ok = bm_validate_parse_count_token(action->argv[13], &payload_count) &&
                     argv_count == 14 + payload_count;
            }
            break;
    }

    if (ok) return true;
    *had_error = true;
    bm_diag_error(sink,
                  action->provenance,
                  "build_model_validate",
                  "structural",
                  "replay action payload shape is invalid for opcode",
                  "emit inputs/outputs/argv/environment using the frozen payload layout for the selected replay opcode");
    return false;
}

static void bm_validate_duplicate_target_names(const Build_Model_Draft *draft, Diag_Sink *sink, bool *had_error) {
    for (size_t i = 0; i < arena_arr_len(draft->targets); ++i) {
        for (size_t j = i + 1; j < arena_arr_len(draft->targets); ++j) {
            if (!nob_sv_eq(draft->targets[i].name, draft->targets[j].name)) continue;
            *had_error = true;
            bm_diag_error(sink,
                          draft->targets[j].provenance,
                          "build_model_validate",
                          "structural",
                          "duplicate target name remains in finalized draft",
                          "target names must be unique before freeze");
        }
    }
}

static bool bm_target_has_local_build_flags(const BM_Target_Record *target) {
    return target && (target->exclude_from_all || target->win32_executable || target->macosx_bundle);
}

static const BM_Target_File_Set_Record *bm_validate_find_target_file_set(const BM_Target_Record *target,
                                                                         String_View name) {
    if (!target) return NULL;
    for (size_t i = 0; i < arena_arr_len(target->file_sets); ++i) {
        if (nob_sv_eq(target->file_sets[i].name, name)) return &target->file_sets[i];
    }
    return NULL;
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
        bm_validate_contiguous_id(directory->id == (BM_Directory_Id)i,
                                  directory->provenance,
                                  sink,
                                  had_error,
                                  "directory id mismatch",
                                  "directory ids must be contiguous");
        if (directory->owner_directory_id != directory->id) {
            *had_error = true;
            bm_diag_error(sink, directory->provenance, "build_model_validate", "structural", "directory owner_directory_id must equal directory id", "fix directory ownership");
        }
        if (directory->parent_id != BM_DIRECTORY_ID_INVALID &&
            (size_t)directory->parent_id >= arena_arr_len(draft->directories)) {
            *had_error = true;
            bm_diag_error(sink, directory->provenance, "build_model_validate", "structural", "directory parent_id is invalid", "fix directory stack reconstruction");
        }
        bm_validate_directory_chain(draft, directory, sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->targets); ++i) {
        const BM_Target_Record *target = &draft->targets[i];
        bm_validate_contiguous_id(target->id == (BM_Target_Id)i,
                                  target->provenance,
                                  sink,
                                  had_error,
                                  "target id mismatch",
                                  "target ids must be contiguous");
        if (bm_string_view_is_empty(target->name)) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "structural", "target has empty name", "ensure EVENT_TARGET_DECLARE carries a target name");
        }
        if (!target->declared) {
            *had_error = true;
            bm_diag_error(sink,
                          target->provenance,
                          "build_model_validate",
                          "structural",
                          "target placeholder was never declared",
                          "ensure target property events do not outlive a missing EVENT_TARGET_DECLARE");
        }
        if (target->kind > BM_TARGET_UNKNOWN_LIBRARY) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "structural", "target kind is invalid", "map every target type to a canonical BM_Target_Kind");
        }
        for (size_t source_index = 0; source_index < arena_arr_len(target->source_records); ++source_index) {
            const BM_Target_Source_Record *source = &target->source_records[source_index];
            if (source->kind > BM_TARGET_SOURCE_CXX_MODULE_FILE_SET) {
                *had_error = true;
                bm_diag_error(sink, source->provenance, "build_model_validate", "structural", "target source kind is invalid", "map every source membership to a canonical BM_Target_Source_Kind");
            }
            if (source->visibility > BM_VISIBILITY_INTERFACE) {
                *had_error = true;
                bm_diag_error(sink, source->provenance, "build_model_validate", "structural", "target source visibility is invalid", "map every source membership visibility to a canonical BM_Visibility");
            }
            if (source->kind == BM_TARGET_SOURCE_REGULAR && !bm_string_view_is_empty(source->file_set_name)) {
                *had_error = true;
                bm_diag_error(sink, source->provenance, "build_model_validate", "structural", "regular target source may not reference a file set name", "keep file set references only on file set members");
            }
            if (source->kind != BM_TARGET_SOURCE_REGULAR && bm_string_view_is_empty(source->file_set_name)) {
                *had_error = true;
                bm_diag_error(sink, source->provenance, "build_model_validate", "structural", "file set member is missing a file set name", "attach file set members to a declared target file set");
            }
        }
        for (size_t file_set_index = 0; file_set_index < arena_arr_len(target->file_sets); ++file_set_index) {
            const BM_Target_File_Set_Record *file_set = &target->file_sets[file_set_index];
            if (bm_string_view_is_empty(file_set->name)) {
                *had_error = true;
                bm_diag_error(sink, file_set->provenance, "build_model_validate", "structural", "target file set has empty name", "ensure file set declarations carry a stable set name");
            }
            if (file_set->kind > BM_TARGET_FILE_SET_CXX_MODULES) {
                *had_error = true;
                bm_diag_error(sink, file_set->provenance, "build_model_validate", "structural", "target file set kind is invalid", "map every file set kind to a canonical BM_Target_File_Set_Kind");
            }
            if (file_set->visibility > BM_VISIBILITY_INTERFACE) {
                *had_error = true;
                bm_diag_error(sink, file_set->provenance, "build_model_validate", "structural", "target file set visibility is invalid", "map every file set visibility to a canonical BM_Visibility");
            }
        }
        bm_validate_owner_directory(draft, target->owner_directory_id, target->provenance, "target", sink, had_error);
    }
    bm_validate_duplicate_target_names(draft, sink, had_error);

    for (size_t i = 0; i < arena_arr_len(draft->source_property_mutations); ++i) {
        const BM_Source_Property_Mutation_Record *mutation = &draft->source_property_mutations[i];
        if (bm_string_view_is_empty(mutation->path) || bm_string_view_is_empty(mutation->key)) {
            *had_error = true;
            bm_diag_error(sink,
                          mutation->provenance,
                          "build_model_validate",
                          "structural",
                          "source property mutation is missing path or property key",
                          "emit source property mutations with resolved source path and property key");
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->build_steps); ++i) {
        const BM_Build_Step_Record *step = &draft->build_steps[i];
        bm_validate_contiguous_id(step->id == (BM_Build_Step_Id)i,
                                  step->provenance,
                                  sink,
                                  had_error,
                                  "build step id mismatch",
                                  "build step ids must be contiguous");
        bm_validate_owner_directory(draft, step->owner_directory_id, step->provenance, "build step", sink, had_error);
        if (step->kind > BM_BUILD_STEP_TARGET_POST_BUILD) {
            *had_error = true;
            bm_diag_error(sink,
                          step->provenance,
                          "build_model_validate",
                          "structural",
                          "build step kind is invalid",
                          "map every build step kind to a canonical BM_Build_Step_Kind");
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->replay_actions); ++i) {
        const BM_Replay_Action_Record *action = &draft->replay_actions[i];
        bm_validate_contiguous_id(action->id == (BM_Replay_Action_Id)i,
                                  action->provenance,
                                  sink,
                                  had_error,
                                  "replay action id mismatch",
                                  "replay action ids must be contiguous");
        bm_validate_owner_directory(draft, action->owner_directory_id, action->provenance, "replay action", sink, had_error);
        if (bm_string_view_is_empty(action->action_key)) {
            *had_error = true;
            bm_diag_error(sink,
                          action->provenance,
                          "build_model_validate",
                          "structural",
                          "replay action has empty action_key",
                          "ensure EVENT_REPLAY_ACTION_DECLARE carries a stable replay key");
        }
        if (action->kind > BM_REPLAY_ACTION_HOST_EFFECT) {
            *had_error = true;
            bm_diag_error(sink,
                          action->provenance,
                          "build_model_validate",
                          "structural",
                          "replay action kind is invalid",
                          "map every replay action kind to a canonical BM_Replay_Action_Kind");
        }
        if (action->opcode > BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL) {
            *had_error = true;
            bm_diag_error(sink,
                          action->provenance,
                          "build_model_validate",
                          "structural",
                          "replay action opcode is invalid",
                          "map every replay action opcode to a canonical BM_Replay_Opcode");
        }
        if (action->phase > BM_REPLAY_PHASE_HOST_ONLY) {
            *had_error = true;
            bm_diag_error(sink,
                          action->provenance,
                          "build_model_validate",
                          "structural",
                          "replay action phase is invalid",
                          "map every replay action phase to a canonical BM_Replay_Phase");
        }
        (void)bm_validate_replay_opcode_kind(action, sink, had_error);
        (void)bm_validate_replay_payload_shape(action, sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->tests); ++i) {
        const BM_Test_Record *test = &draft->tests[i];
        bm_validate_contiguous_id(test->id == (BM_Test_Id)i,
                                  test->provenance,
                                  sink,
                                  had_error,
                                  "test id mismatch",
                                  "test ids must be contiguous");
        if (bm_string_view_is_empty(test->name) || bm_string_view_is_empty(test->command)) {
            *had_error = true;
            bm_diag_error(sink, test->provenance, "build_model_validate", "structural", "test is missing required fields", "tests require name and command");
        }
        bm_validate_owner_directory(draft, test->owner_directory_id, test->provenance, "test", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->install_rules); ++i) {
        const BM_Install_Rule_Record *rule = &draft->install_rules[i];
        bm_validate_contiguous_id(rule->id == (BM_Install_Rule_Id)i,
                                  rule->provenance,
                                  sink,
                                  had_error,
                                  "install rule id mismatch",
                                  "install rule ids must be contiguous");
        if (bm_string_view_is_empty(rule->item) ||
            (rule->kind != BM_INSTALL_RULE_TARGET &&
             bm_string_view_is_empty(rule->destination)) ||
            (rule->kind == BM_INSTALL_RULE_TARGET &&
             bm_string_view_is_empty(rule->destination) &&
             bm_string_view_is_empty(rule->archive_destination) &&
             bm_string_view_is_empty(rule->library_destination) &&
             bm_string_view_is_empty(rule->runtime_destination) &&
             bm_string_view_is_empty(rule->includes_destination) &&
             bm_string_view_is_empty(rule->public_header_destination))) {
            *had_error = true;
            bm_diag_error(sink, rule->provenance, "build_model_validate", "structural", "install rule is missing required fields", "install rules require an item and at least one destination");
        }
        bm_validate_owner_directory(draft, rule->owner_directory_id, rule->provenance, "install rule", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->exports); ++i) {
        const BM_Export_Record *record = &draft->exports[i];
        bm_validate_contiguous_id(record->id == (BM_Export_Id)i,
                                  record->provenance,
                                  sink,
                                  had_error,
                                  "export id mismatch",
                                  "export ids must be contiguous");
        switch (record->kind) {
            case BM_EXPORT_INSTALL:
                if (bm_string_view_is_empty(record->name) || bm_string_view_is_empty(record->destination)) {
                    *had_error = true;
                    bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "install export is missing required fields", "install exports require a name and destination");
                }
                break;

            case BM_EXPORT_BUILD_TREE:
                if (bm_string_view_is_empty(record->output_file_path)) {
                    *had_error = true;
                    bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "build-tree export is missing FILE", "standalone build-tree exports require an output file path");
                }
                break;

            case BM_EXPORT_PACKAGE_REGISTRY:
                if (bm_string_view_is_empty(record->name) || bm_string_view_is_empty(record->registry_prefix)) {
                    *had_error = true;
                    bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "package registry export is missing required fields", "package registry exports require package name and prefix");
                }
                break;
        }
        bm_validate_owner_directory(draft, record->owner_directory_id, record->provenance, "export", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->packages); ++i) {
        const BM_Package_Record *package = &draft->packages[i];
        bm_validate_contiguous_id(package->id == (BM_Package_Id)i,
                                  package->provenance,
                                  sink,
                                  had_error,
                                  "package id mismatch",
                                  "package ids must be contiguous");
        if (bm_string_view_is_empty(package->package_name)) {
            *had_error = true;
            bm_diag_error(sink, package->provenance, "build_model_validate", "structural", "package result is missing package name", "package results require package_name");
        }
        bm_validate_owner_directory(draft, package->owner_directory_id, package->provenance, "package", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_install_types); ++i) {
        const BM_CPack_Install_Type_Record *record = &draft->cpack_install_types[i];
        bm_validate_contiguous_id(record->id == (BM_CPack_Install_Type_Id)i,
                                  record->provenance,
                                  sink,
                                  had_error,
                                  "CPack install type id mismatch",
                                  "CPack install type ids must be contiguous");
        if (bm_string_view_is_empty(record->name)) {
            *had_error = true;
            bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "CPack install type is missing name", "install types require a name");
        }
        bm_validate_owner_directory(draft, record->owner_directory_id, record->provenance, "cpack install type", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_component_groups); ++i) {
        const BM_CPack_Component_Group_Record *record = &draft->cpack_component_groups[i];
        bm_validate_contiguous_id(record->id == (BM_CPack_Component_Group_Id)i,
                                  record->provenance,
                                  sink,
                                  had_error,
                                  "CPack component group id mismatch",
                                  "CPack component group ids must be contiguous");
        if (bm_string_view_is_empty(record->name)) {
            *had_error = true;
            bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "CPack component group is missing name", "component groups require a name");
        }
        bm_validate_owner_directory(draft, record->owner_directory_id, record->provenance, "cpack component group", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_components); ++i) {
        const BM_CPack_Component_Record *record = &draft->cpack_components[i];
        bm_validate_contiguous_id(record->id == (BM_CPack_Component_Id)i,
                                  record->provenance,
                                  sink,
                                  had_error,
                                  "CPack component id mismatch",
                                  "CPack component ids must be contiguous");
        if (bm_string_view_is_empty(record->name)) {
            *had_error = true;
            bm_diag_error(sink, record->provenance, "build_model_validate", "structural", "CPack component is missing name", "components require a name");
        }
        bm_validate_owner_directory(draft, record->owner_directory_id, record->provenance, "cpack component", sink, had_error);
    }

    for (size_t i = 0; i < arena_arr_len(draft->cpack_packages); ++i) {
        const BM_CPack_Package_Record *record = &draft->cpack_packages[i];
        bm_validate_contiguous_id(record->id == (BM_CPack_Package_Id)i,
                                  record->provenance,
                                  sink,
                                  had_error,
                                  "CPack package id mismatch",
                                  "CPack package ids must be contiguous");
        if (bm_string_view_is_empty(record->package_key)) {
            *had_error = true;
            bm_diag_error(sink,
                          record->provenance,
                          "build_model_validate",
                          "structural",
                          "CPack package snapshot is missing key",
                          "package snapshots require a stable package key");
        }
        if (bm_string_view_is_empty(record->package_name)) {
            *had_error = true;
            bm_diag_error(sink,
                          record->provenance,
                          "build_model_validate",
                          "structural",
                          "CPack package snapshot is missing package name",
                          "package snapshots require an effective package name");
        }
        if (bm_string_view_is_empty(record->package_file_name)) {
            *had_error = true;
            bm_diag_error(sink,
                          record->provenance,
                          "build_model_validate",
                          "structural",
                          "CPack package snapshot is missing package file name",
                          "package snapshots require an effective package file name");
        }
        if (arena_arr_len(record->generators) == 0) {
            *had_error = true;
            bm_diag_error(sink,
                          record->provenance,
                          "build_model_validate",
                          "structural",
                          "CPack package snapshot has no generators",
                          "package snapshots require at least one effective generator");
        }
        if (!bm_string_view_is_empty(record->components_grouping) &&
            !nob_sv_eq(record->components_grouping, nob_sv_from_cstr("ONE_PER_GROUP")) &&
            !nob_sv_eq(record->components_grouping, nob_sv_from_cstr("IGNORE")) &&
            !nob_sv_eq(record->components_grouping, nob_sv_from_cstr("ALL_COMPONENTS_IN_ONE"))) {
            *had_error = true;
            bm_diag_error(sink,
                          record->provenance,
                          "build_model_validate",
                          "structural",
                          "CPack package snapshot has unsupported CPACK_COMPONENTS_GROUPING",
                          "supported archive component groupings are ONE_PER_GROUP, IGNORE, and ALL_COMPONENTS_IN_ONE");
        }
        bm_validate_owner_directory(draft, record->owner_directory_id, record->provenance, "cpack package", sink, had_error);
    }

    return true;
}

static bool bm_validate_resolution_pass(const Build_Model_Draft *draft, Diag_Sink *sink, bool *had_error) {
    for (size_t i = 0; i < arena_arr_len(draft->targets); ++i) {
        const BM_Target_Record *target = &draft->targets[i];

        if (target->alias && bm_string_view_is_empty(target->alias_of_name)) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "resolution", "alias target is missing alias_of", "alias targets must reference a declared target");
        } else if (target->alias &&
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

        for (size_t source_index = 0; source_index < arena_arr_len(target->source_records); ++source_index) {
            const BM_Target_Source_Record *source = &target->source_records[source_index];
            const BM_Target_File_Set_Record *file_set = NULL;
            if (bm_string_view_is_empty(source->file_set_name)) continue;
            file_set = bm_validate_find_target_file_set(target, source->file_set_name);
            if (!file_set) {
                *had_error = true;
                bm_diag_error(sink, source->provenance, "build_model_validate", "resolution", "target source file set reference cannot be resolved", "declare the file set before attaching members");
                continue;
            }
            if ((source->kind == BM_TARGET_SOURCE_HEADER_FILE_SET && file_set->kind != BM_TARGET_FILE_SET_HEADERS) ||
                (source->kind == BM_TARGET_SOURCE_CXX_MODULE_FILE_SET && file_set->kind != BM_TARGET_FILE_SET_CXX_MODULES)) {
                *had_error = true;
                bm_diag_error(sink, source->provenance, "build_model_validate", "resolution", "target source file set kind does not match the referenced file set", "keep file set member kinds aligned with the declared file set");
            }
        }
    }

    for (size_t i = 0; i < arena_arr_len(draft->build_steps); ++i) {
        const BM_Build_Step_Record *step = &draft->build_steps[i];
        if (!bm_string_view_is_empty(step->owner_target_name) &&
            bm_draft_find_target_id(draft, step->owner_target_name) == BM_TARGET_ID_INVALID) {
            *had_error = true;
            bm_diag_error(sink,
                          step->provenance,
                          "build_model_validate",
                          "resolution",
                          "build step owner target cannot be resolved",
                          "declare the owner target before emitting target hook or custom-target steps");
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

    for (size_t i = 0; i < arena_arr_len(draft->exports); ++i) {
        const BM_Export_Record *record = &draft->exports[i];
        if (record->kind == BM_EXPORT_INSTALL) {
            bool found_target = false;
            for (size_t rule_index = 0; rule_index < arena_arr_len(draft->install_rules); ++rule_index) {
                const BM_Install_Rule_Record *rule = &draft->install_rules[rule_index];
                if (rule->kind != BM_INSTALL_RULE_TARGET) continue;
                if (!nob_sv_eq(rule->export_name, record->name)) continue;
                found_target = true;
                break;
            }
            if (!found_target) {
                *had_error = true;
                bm_diag_error(sink, record->provenance, "build_model_validate", "resolution", "install export has no associated install(TARGETS ... EXPORT ...) rules", "associate at least one install target rule with the export name");
            }
            continue;
        }

        if (record->kind == BM_EXPORT_BUILD_TREE) {
            for (size_t target_index = 0; target_index < arena_arr_len(record->target_names); ++target_index) {
                if (bm_draft_find_target_id(draft, record->target_names[target_index]) == BM_TARGET_ID_INVALID) {
                    *had_error = true;
                    bm_diag_error(sink,
                                  record->provenance,
                                  "build_model_validate",
                                  "resolution",
                                  "build-tree export references an unknown target",
                                  "declare standalone export targets before exporting them");
                }
            }
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
           arena_arr_len(target->compile_features) > 0 ||
           !bm_string_view_is_empty(target->output_name) ||
           !bm_string_view_is_empty(target->prefix) ||
           !bm_string_view_is_empty(target->suffix) ||
           !bm_string_view_is_empty(target->archive_output_directory) ||
           !bm_string_view_is_empty(target->library_output_directory) ||
           !bm_string_view_is_empty(target->runtime_output_directory) ||
           arena_arr_len(target->artifact_properties) > 0 ||
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
             arena_arr_len(target->source_records) > 0 ||
             arena_arr_len(target->file_sets) > 0 ||
             arena_arr_len(target->explicit_dependency_names) > 0 ||
             bm_target_has_typed_payload(target) ||
             arena_arr_len(target->raw_properties) > 0 ||
             bm_target_has_local_build_flags(target))) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "semantic", "alias target may not own sources, dependencies or build payload", "leave alias targets as lightweight references only");
        }

        if (target->imported &&
            (!bm_string_view_is_empty(target->output_name) ||
             !bm_string_view_is_empty(target->prefix) ||
             !bm_string_view_is_empty(target->suffix) ||
             !bm_string_view_is_empty(target->archive_output_directory) ||
             !bm_string_view_is_empty(target->library_output_directory) ||
             !bm_string_view_is_empty(target->runtime_output_directory) ||
             arena_arr_len(target->artifact_properties) > 0 ||
             bm_target_has_local_build_flags(target))) {
            *had_error = true;
            bm_diag_error(sink, target->provenance, "build_model_validate", "semantic", "imported target may not declare local build outputs or build-only flags", "remove local output properties and build-only flags from imported targets");
        }

        if (target->imported_global && !target->imported) {
            *had_error = true;
            bm_diag_error(sink,
                          target->provenance,
                          "build_model_validate",
                          "semantic",
                          "IMPORTED_GLOBAL may only be set on imported targets",
                          "remove IMPORTED_GLOBAL or declare the target as imported");
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
        for (size_t a = 0; a < arena_arr_len(target->file_sets); ++a) {
            for (size_t b = a + 1; b < arena_arr_len(target->file_sets); ++b) {
                if (nob_sv_eq(target->file_sets[a].name, target->file_sets[b].name)) {
                    *had_error = true;
                    bm_diag_error(sink, target->file_sets[b].provenance, "build_model_validate", "semantic", "duplicate file set name inside target", "keep file set names unique per target");
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
