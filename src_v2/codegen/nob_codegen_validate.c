#include "nob_codegen_internal.h"

static const char *cg_replay_action_kind_name(BM_Replay_Action_Kind kind) {
    switch (kind) {
        case BM_REPLAY_ACTION_FILESYSTEM: return "filesystem";
        case BM_REPLAY_ACTION_PROCESS: return "process";
        case BM_REPLAY_ACTION_PROBE: return "probe";
        case BM_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION: return "dependency_materialization";
        case BM_REPLAY_ACTION_TEST_DRIVER: return "test_driver";
        case BM_REPLAY_ACTION_HOST_EFFECT: return "host_effect";
    }
    return "unknown";
}

static const char *cg_replay_phase_name(BM_Replay_Phase phase) {
    switch (phase) {
        case BM_REPLAY_PHASE_CONFIGURE: return "configure";
        case BM_REPLAY_PHASE_BUILD: return "build";
        case BM_REPLAY_PHASE_TEST: return "test";
        case BM_REPLAY_PHASE_INSTALL: return "install";
        case BM_REPLAY_PHASE_EXPORT: return "export";
        case BM_REPLAY_PHASE_PACKAGE: return "package";
        case BM_REPLAY_PHASE_HOST_ONLY: return "host_only";
    }
    return "unknown";
}

static const char *cg_replay_opcode_name(BM_Replay_Opcode opcode) {
    switch (opcode) {
        case BM_REPLAY_OPCODE_NONE: return "none";
        case BM_REPLAY_OPCODE_FS_MKDIR: return "fs_mkdir";
        case BM_REPLAY_OPCODE_FS_WRITE_TEXT: return "fs_write_text";
        case BM_REPLAY_OPCODE_FS_APPEND_TEXT: return "fs_append_text";
        case BM_REPLAY_OPCODE_FS_COPY_FILE: return "fs_copy_file";
        case BM_REPLAY_OPCODE_FS_COPY_TREE: return "fs_copy_tree";
        case BM_REPLAY_OPCODE_FS_REMOVE: return "fs_remove";
        case BM_REPLAY_OPCODE_FS_REMOVE_RECURSE: return "fs_remove_recurse";
        case BM_REPLAY_OPCODE_FS_RENAME: return "fs_rename";
        case BM_REPLAY_OPCODE_FS_CREATE_LINK: return "fs_create_link";
        case BM_REPLAY_OPCODE_FS_CHMOD: return "fs_chmod";
        case BM_REPLAY_OPCODE_FS_CHMOD_RECURSE: return "fs_chmod_recurse";
        case BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL: return "host_download_local";
        case BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR: return "host_archive_create_paxr";
        case BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR: return "host_archive_extract_tar";
        case BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE: return "host_lock_acquire";
        case BM_REPLAY_OPCODE_HOST_LOCK_RELEASE: return "host_lock_release";
        case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE: return "probe_try_compile_source";
        case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT: return "probe_try_compile_project";
        case BM_REPLAY_OPCODE_PROBE_TRY_RUN: return "probe_try_run";
        case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR: return "deps_fetchcontent_source_dir";
        case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE: return "deps_fetchcontent_local_archive";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY: return "test_driver_ctest_empty_binary_directory";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL: return "test_driver_ctest_start_local";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF: return "test_driver_ctest_configure_self";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF: return "test_driver_ctest_build_self";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST: return "test_driver_ctest_test";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP: return "test_driver_ctest_sleep";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL: return "test_driver_ctest_coverage_local";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL: return "test_driver_ctest_memcheck_local";
    }
    return "unknown";
}

static bool cg_validate_install_rule(CG_Context *ctx, BM_Install_Rule_Id id) {
    BM_Install_Rule_Kind kind = bm_query_install_rule_kind(ctx->model, id);
    String_View item = bm_query_install_rule_item_raw(ctx->model, id);
    if (!ctx) return false;

    if (kind == BM_INSTALL_RULE_FILE || kind == BM_INSTALL_RULE_PROGRAM) {
        if (cg_sv_has_prefix(item, "SCRIPT::") ||
            cg_sv_has_prefix(item, "CODE::") ||
            cg_sv_has_prefix(item, "EXPORT_ANDROID_MK::")) {
            nob_log(NOB_ERROR,
                    "codegen: install pseudo-item is not supported in the install backend: %.*s",
                    (int)item.count,
                    item.data ? item.data : "");
            return false;
        }
    }
    if (kind == BM_INSTALL_RULE_TARGET) {
        BM_Target_Id target_id = bm_query_install_rule_target(ctx->model, id);
        BM_Target_Kind target_kind = bm_query_target_kind(ctx->model, target_id);
        if (target_kind == BM_TARGET_OBJECT_LIBRARY ||
            target_kind == BM_TARGET_UTILITY ||
            target_kind == BM_TARGET_UNKNOWN_LIBRARY) {
            nob_log(NOB_ERROR,
                    "codegen: install(TARGETS) does not support target kind '%d' yet",
                    (int)target_kind);
            return false;
        }
    }
    return true;
}

static bool cg_validate_export_record(CG_Context *ctx, BM_Export_Id export_id) {
    BM_Export_Kind kind = BM_EXPORT_INSTALL;
    BM_Target_Id_Span targets = {0};
    String_View name = {0};
    if (!ctx) return false;

    kind = bm_query_export_kind(ctx->model, export_id);
    name = bm_query_export_name(ctx->model, export_id);
    targets = bm_query_export_targets(ctx->model, export_id);

    switch (kind) {
        case BM_EXPORT_INSTALL:
            if (targets.count == 0) {
                nob_log(NOB_ERROR,
                        "codegen: install export '%.*s' has no associated targets",
                        (int)name.count,
                        name.data ? name.data : "");
                return false;
            }
            return true;

        case BM_EXPORT_BUILD_TREE:
            if (targets.count == 0) {
                nob_log(NOB_ERROR,
                        "codegen: standalone export '%.*s' has no associated targets",
                        (int)name.count,
                        name.data ? name.data : "");
                return false;
            }
            if (bm_query_export_append(ctx->model, export_id)) {
                nob_log(NOB_ERROR,
                        "codegen: export(APPEND ...) is not supported in the standalone export backend yet");
                return false;
            }
            if (bm_query_export_cxx_modules_directory(ctx->model, export_id).count > 0) {
                nob_log(NOB_ERROR,
                        "codegen: export(... CXX_MODULES_DIRECTORY ...) is not supported in the standalone export backend yet");
                return false;
            }
            return true;

        case BM_EXPORT_PACKAGE_REGISTRY:
            return true;
    }

    return true;
}

static bool cg_validate_package_model(CG_Context *ctx) {
    bool saw_supported_generator = false;
    if (!ctx) return false;
    if (bm_query_cpack_package_count(ctx->model) == 0) {
        return true;
    }

    for (size_t package_index = 0; package_index < bm_query_cpack_package_count(ctx->model); ++package_index) {
        BM_CPack_Package_Id id = (BM_CPack_Package_Id)package_index;
        String_View package_name = bm_query_cpack_package_name(ctx->model, id);
        String_View file_name = bm_query_cpack_package_file_name(ctx->model, id);
        String_View output_dir = bm_query_cpack_package_output_directory(ctx->model, id, ctx->scratch);
        String_View project_config_file = bm_query_cpack_package_project_config_file(ctx->model, id);
        String_View grouping = bm_query_cpack_package_components_grouping(ctx->model, id);
        BM_String_Span generators = bm_query_cpack_package_generators(ctx->model, id);

        if (package_name.count == 0 || file_name.count == 0 || output_dir.count == 0) {
            nob_log(NOB_ERROR,
                    "codegen: CPack package plan is incomplete for '%.*s'",
                    (int)package_name.count,
                    package_name.data ? package_name.data : "");
            return false;
        }

        if (project_config_file.count > 0) {
            nob_log(NOB_ERROR,
                    "codegen: CPACK_PROJECT_CONFIG_FILE is not supported in the package backend yet");
            return false;
        }

        if (grouping.count > 0 &&
            !nob_sv_eq(grouping, nob_sv_from_cstr("ONE_PER_GROUP")) &&
            !nob_sv_eq(grouping, nob_sv_from_cstr("IGNORE")) &&
            !nob_sv_eq(grouping, nob_sv_from_cstr("ALL_COMPONENTS_IN_ONE"))) {
            nob_log(NOB_ERROR,
                    "codegen: unsupported CPACK_COMPONENTS_GROUPING '%.*s'",
                    (int)grouping.count,
                    grouping.data ? grouping.data : "");
            return false;
        }

        if (generators.count == 0) {
            nob_log(NOB_ERROR,
                    "codegen: CPack package plan '%.*s' has no configured generators",
                    (int)package_name.count,
                    package_name.data ? package_name.data : "");
            return false;
        }

        for (size_t i = 0; i < generators.count; ++i) {
            if (nob_sv_eq(generators.items[i], nob_sv_from_cstr("TGZ")) ||
                nob_sv_eq(generators.items[i], nob_sv_from_cstr("TXZ")) ||
                nob_sv_eq(generators.items[i], nob_sv_from_cstr("ZIP"))) {
                saw_supported_generator = true;
                continue;
            }
            nob_log(NOB_ERROR,
                    "codegen: unsupported package generator '%.*s' (supported: TGZ, TXZ, ZIP)",
                    (int)generators.items[i].count,
                    generators.items[i].data ? generators.items[i].data : "");
            return false;
        }
    }

    if (!saw_supported_generator) {
        nob_log(NOB_ERROR, "codegen: no supported package generators are configured");
        return false;
    }
    return true;
}

bool cg_validate_model_for_backend(CG_Context *ctx) {
    if (!ctx) return false;

    for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
        BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
        BM_Replay_Action_Kind kind = bm_query_replay_action_kind(ctx->model, id);
        BM_Replay_Opcode opcode = bm_query_replay_action_opcode(ctx->model, id);
        BM_Replay_Phase phase = bm_query_replay_action_phase(ctx->model, id);
        bool supported = false;
        if (phase == BM_REPLAY_PHASE_CONFIGURE) {
            supported =
                (kind == BM_REPLAY_ACTION_FILESYSTEM &&
                 (opcode == BM_REPLAY_OPCODE_FS_MKDIR ||
                  opcode == BM_REPLAY_OPCODE_FS_WRITE_TEXT ||
                  opcode == BM_REPLAY_OPCODE_FS_APPEND_TEXT ||
                  opcode == BM_REPLAY_OPCODE_FS_COPY_FILE ||
                  opcode == BM_REPLAY_OPCODE_FS_COPY_TREE ||
                  opcode == BM_REPLAY_OPCODE_FS_REMOVE ||
                  opcode == BM_REPLAY_OPCODE_FS_REMOVE_RECURSE ||
                  opcode == BM_REPLAY_OPCODE_FS_RENAME ||
                  opcode == BM_REPLAY_OPCODE_FS_CREATE_LINK ||
                  opcode == BM_REPLAY_OPCODE_FS_CHMOD ||
                  opcode == BM_REPLAY_OPCODE_FS_CHMOD_RECURSE)) ||
                (kind == BM_REPLAY_ACTION_HOST_EFFECT &&
                 (opcode == BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL ||
                  opcode == BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR ||
                  opcode == BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR ||
                  opcode == BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE ||
                  opcode == BM_REPLAY_OPCODE_HOST_LOCK_RELEASE)) ||
                (kind == BM_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION &&
                 (opcode == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR ||
                  opcode == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE));
        } else if (phase == BM_REPLAY_PHASE_TEST) {
            supported =
                (kind == BM_REPLAY_ACTION_TEST_DRIVER &&
                 (opcode == BM_REPLAY_OPCODE_NONE ||
                  opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY ||
                  opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL ||
                  opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF ||
                  opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF ||
                  opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST ||
                  opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP ||
                  opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL ||
                  opcode == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL)) ||
                (kind == BM_REPLAY_ACTION_FILESYSTEM &&
                 (opcode == BM_REPLAY_OPCODE_FS_MKDIR ||
                  opcode == BM_REPLAY_OPCODE_FS_WRITE_TEXT ||
                  opcode == BM_REPLAY_OPCODE_FS_APPEND_TEXT));
        }
        if (!supported) {
            nob_log(NOB_ERROR,
                    "codegen: replay action kind '%s' opcode '%s' in phase '%s' is not supported in the generated backend",
                    cg_replay_action_kind_name(kind),
                    cg_replay_opcode_name(opcode),
                    cg_replay_phase_name(phase));
            return false;
        }
    }

    for (size_t i = 0; i < ctx->target_count; ++i) {
        const CG_Target_Info *info = &ctx->targets[i];
        if (!cg_reject_unsupported_precompile_headers(ctx, info) ||
            !cg_reject_unsupported_platform_target_properties(ctx, info)) {
            return false;
        }

        if (!info->alias && !info->imported && info->emits_artifact) {
            CG_Source_Info *sources = NULL;
            String_View *compile_args = NULL;
            String_View *link_args = NULL;
            String_View *link_rebuild_inputs = NULL;
            if (!cg_collect_compile_sources(ctx, info->id, &sources)) return false;
            for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
                String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
                for (size_t source_index = 0; source_index < arena_arr_len(sources); ++source_index) {
                    compile_args = NULL;
                    if (!cg_collect_compile_args(ctx,
                                                 info->id,
                                                 config,
                                                 &sources[source_index],
                                                 &compile_args)) {
                        return false;
                    }
                }
                link_args = NULL;
                link_rebuild_inputs = NULL;
                if (!cg_collect_link_dir_args(ctx, info->id, config, &link_args) ||
                    !cg_collect_link_option_args(ctx, info->id, config, &link_args) ||
                    !cg_collect_link_library_args(ctx, info->id, config, &link_args, &link_rebuild_inputs)) {
                    return false;
                }
            }
        }
    }

    for (size_t rule_index = 0; rule_index < bm_query_install_rule_count(ctx->model); ++rule_index) {
        if (!cg_validate_install_rule(ctx, (BM_Install_Rule_Id)rule_index)) return false;
    }

    for (size_t export_index = 0; export_index < bm_query_export_count(ctx->model); ++export_index) {
        if (!cg_validate_export_record(ctx, (BM_Export_Id)export_index)) return false;
    }

    if (!cg_validate_package_model(ctx)) return false;

    return true;
}
