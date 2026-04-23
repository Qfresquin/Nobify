#include "nob_codegen_internal.h"

bool cg_query_effective_items_cached(CG_Context *ctx,
                                     BM_Target_Id id,
                                     const BM_Query_Eval_Context *qctx,
                                     CG_Effective_Query_Family family,
                                     BM_String_Item_Span *out) {
    if (!ctx || !qctx || !out || !ctx->query_session) return false;
    *out = (BM_String_Item_Span){0};

    switch (family) {
        case CG_EFFECTIVE_INCLUDE_DIRECTORIES:
            return bm_query_session_target_effective_include_directories_items(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_COMPILE_DEFINITIONS:
            return bm_query_session_target_effective_compile_definitions_items(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_COMPILE_OPTIONS:
            return bm_query_session_target_effective_compile_options_items(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_LINK_DIRECTORIES:
            return bm_query_session_target_effective_link_directories_items(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_LINK_OPTIONS:
            return bm_query_session_target_effective_link_options_items(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_LINK_LIBRARIES:
            return false;
        case CG_EFFECTIVE_COMPILE_FEATURES:
            return false;
    }

    return false;
}

bool cg_query_effective_link_items_cached(CG_Context *ctx,
                                          BM_Target_Id id,
                                          const BM_Query_Eval_Context *qctx,
                                          BM_Link_Item_Span *out) {
    if (!ctx || !qctx || !out || !ctx->query_session) return false;
    *out = (BM_Link_Item_Span){0};
    return bm_query_session_target_effective_link_libraries_items(ctx->query_session, id, qctx, out);
}

bool cg_query_effective_values_cached(CG_Context *ctx,
                                      BM_Target_Id id,
                                      const BM_Query_Eval_Context *qctx,
                                      CG_Effective_Query_Family family,
                                      BM_String_Span *out) {
    if (!ctx || !qctx || !out || !ctx->query_session) return false;
    *out = (BM_String_Span){0};

    switch (family) {
        case CG_EFFECTIVE_COMPILE_FEATURES:
            return bm_query_session_target_effective_compile_features(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_INCLUDE_DIRECTORIES:
            return bm_query_session_target_effective_include_directories(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_COMPILE_DEFINITIONS:
            return bm_query_session_target_effective_compile_definitions(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_COMPILE_OPTIONS:
            return bm_query_session_target_effective_compile_options(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_LINK_DIRECTORIES:
            return bm_query_session_target_effective_link_directories(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_LINK_OPTIONS:
            return bm_query_session_target_effective_link_options(ctx->query_session, id, qctx, out);
        case CG_EFFECTIVE_LINK_LIBRARIES:
            return bm_query_session_target_effective_link_libraries(ctx->query_session, id, qctx, out);
    }

    return false;
}

bool cg_query_target_file_cached(CG_Context *ctx,
                                 BM_Target_Id id,
                                 const BM_Query_Eval_Context *qctx,
                                 bool linker_file,
                                 String_View *out) {
    if (!ctx || !qctx || !out || !ctx->query_session) return false;
    *out = nob_sv_from_cstr("");
    return linker_file
        ? bm_query_session_target_effective_linker_file(ctx->query_session, id, qctx, out)
        : bm_query_session_target_effective_file(ctx->query_session, id, qctx, out);
}

bool cg_query_target_artifact_cached(CG_Context *ctx,
                                     BM_Target_Id id,
                                     const BM_Query_Eval_Context *qctx,
                                     BM_Target_Artifact_Role role,
                                     BM_Target_Artifact_View *out) {
    if (!ctx || !qctx || !out || !ctx->query_session) return false;
    *out = (BM_Target_Artifact_View){0};
    return bm_query_session_target_effective_artifact(ctx->query_session, id, role, qctx, out);
}

bool cg_query_imported_link_languages_cached(CG_Context *ctx,
                                             BM_Target_Id id,
                                             const BM_Query_Eval_Context *qctx,
                                             BM_String_Span *out) {
    if (!ctx || !qctx || !out || !ctx->query_session) return false;
    *out = (BM_String_Span){0};
    return bm_query_session_target_imported_link_languages(ctx->query_session, id, qctx, out);
}

bool cg_query_effective_link_language_cached(CG_Context *ctx,
                                             BM_Target_Id id,
                                             const BM_Query_Eval_Context *qctx,
                                             String_View *out) {
    if (!ctx || !qctx || !out || !ctx->query_session) return false;
    *out = nob_sv_from_cstr("");
    return bm_query_session_target_effective_link_language(ctx->query_session, id, qctx, out);
}

static bool cg_rebase_artifact_view_from_cwd(CG_Context *ctx,
                                             BM_Target_Artifact_View in,
                                             BM_Target_Artifact_View *out) {
    if (!ctx || !out) return false;
    *out = in;
    if (in.path.count > 0 && !cg_rebase_path_from_cwd(ctx, in.path, &out->path)) return false;
    if (in.directory.count > 0 && !cg_rebase_path_from_cwd(ctx, in.directory, &out->directory)) return false;
    return true;
}

bool cg_resolve_target_ref(CG_Context *ctx,
                           const BM_Query_Eval_Context *qctx,
                           String_View item,
                           CG_Resolved_Target_Ref *out) {
    BM_Target_Id target_id = BM_TARGET_ID_INVALID;
    const CG_Target_Info *info = NULL;
    BM_String_Span imported_langs = {0};
    String_View effective_file = {0};
    String_View effective_linker_file = {0};
    BM_Target_Artifact_View runtime_artifact = {0};
    BM_Target_Artifact_View linker_artifact = {0};
    if (!ctx || !qctx || !out) return false;
    *out = (CG_Resolved_Target_Ref){0};
    out->original_item = item;

    target_id = bm_query_target_by_name(ctx->model, item);
    if (!bm_target_id_is_valid(target_id)) return false;
    target_id = cg_resolve_alias_target(ctx, target_id);
    if (!bm_target_id_is_valid(target_id)) return false;

    info = cg_target_info(ctx, target_id);
    if (!info) return false;

    out->target_id = target_id;
    out->resolved_target_id = info->resolved_id;
    out->kind = info->imported ? CG_RESOLVED_TARGET_IMPORTED : CG_RESOLVED_TARGET_LOCAL;
    out->target_kind = info->kind;
    out->imported = info->imported;
    out->usage_only = info->kind == BM_TARGET_INTERFACE_LIBRARY;
    out->linkable_artifact = false;

    if (info->imported) {
        if (!cg_query_target_file_cached(ctx, target_id, qctx, false, &effective_file) ||
            !cg_query_target_file_cached(ctx, target_id, qctx, true, &effective_linker_file) ||
            !cg_query_imported_link_languages_cached(ctx, target_id, qctx, &imported_langs)) {
            return false;
        }
        out->effective_file = effective_file;
        out->effective_linker_file = effective_linker_file;
        out->imported_link_languages = imported_langs;
        if (info->kind == BM_TARGET_STATIC_LIBRARY ||
            info->kind == BM_TARGET_SHARED_LIBRARY ||
            info->kind == BM_TARGET_UNKNOWN_LIBRARY) {
            out->linkable_artifact = true;
            out->rebuild_input_path = effective_linker_file.count > 0 ? effective_linker_file : effective_file;
        }
        return true;
    }

    if (!cg_query_target_artifact_cached(ctx, target_id, qctx, BM_TARGET_ARTIFACT_RUNTIME, &runtime_artifact) ||
        !cg_query_target_artifact_cached(ctx, target_id, qctx, BM_TARGET_ARTIFACT_LINKER, &linker_artifact) ||
        !cg_rebase_artifact_view_from_cwd(ctx, runtime_artifact, &runtime_artifact) ||
        !cg_rebase_artifact_view_from_cwd(ctx, linker_artifact, &linker_artifact)) {
        return false;
    }
    out->effective_file = runtime_artifact.path;
    out->effective_linker_file = linker_artifact.path.count > 0
        ? linker_artifact.path
        : runtime_artifact.path;
    out->rebuild_input_path = out->effective_linker_file.count > 0
        ? out->effective_linker_file
        : out->effective_file;
    out->linkable_artifact = cg_target_kind_is_linkable_artifact(info->kind);
    return true;
}
