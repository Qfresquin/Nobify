#include "nob_codegen_internal.h"

static bool cg_validate_install_rule(CG_Context *ctx, BM_Install_Rule_Id id) {
    BM_Install_Rule_Kind kind = bm_query_install_rule_kind(ctx->model, id);
    String_View item = bm_query_install_rule_item_raw(ctx->model, id);
    if (!ctx) return false;

    if ((kind == BM_INSTALL_RULE_FILE || kind == BM_INSTALL_RULE_PROGRAM || kind == BM_INSTALL_RULE_DIRECTORY) &&
        !cg_check_no_genex("install rule item", item)) {
        return false;
    }
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
        if (target_kind == BM_TARGET_OBJECT_LIBRARY || target_kind == BM_TARGET_UTILITY) {
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
        BM_String_Span generators = bm_query_cpack_package_generators(ctx->model, id);

        if (package_name.count == 0 || file_name.count == 0 || output_dir.count == 0) {
            nob_log(NOB_ERROR,
                    "codegen: CPack package plan is incomplete for '%.*s'",
                    (int)package_name.count,
                    package_name.data ? package_name.data : "");
            return false;
        }

        if (bm_query_cpack_package_archive_component_install(ctx->model, id)) {
            nob_log(NOB_ERROR,
                    "codegen: CPACK_ARCHIVE_COMPONENT_INSTALL=ON is not supported in the package backend yet");
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

    for (size_t step_index = 0; step_index < ctx->build_step_count; ++step_index) {
        BM_Build_Step_Id id = (BM_Build_Step_Id)step_index;
        if (bm_query_build_step_append(ctx->model, id)) {
            nob_log(NOB_ERROR, "codegen: APPEND custom-command steps are not supported yet");
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
                                                 sources[source_index].lang,
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
