#include "nob_codegen_internal.h"

static bool cg_query_effective_items_cached_impl(CG_Context *ctx,
                                                 BM_Target_Id id,
                                                 const BM_Query_Eval_Context *qctx,
                                                 CG_Effective_Query_Family family,
                                                 BM_String_Item_Span *out) {
    BM_String_Item_Span items = {0};
    if (!ctx || !qctx || !out) return false;
    out->items = NULL;
    out->count = 0;

    switch (family) {
        case CG_EFFECTIVE_INCLUDE_DIRECTORIES:
            if (!bm_query_target_effective_include_directories_items_with_context(ctx->model, id, qctx, ctx->scratch, &items)) {
                return false;
            }
            break;

        case CG_EFFECTIVE_COMPILE_DEFINITIONS:
            if (!bm_query_target_effective_compile_definitions_items_with_context(ctx->model, id, qctx, ctx->scratch, &items)) {
                return false;
            }
            break;

        case CG_EFFECTIVE_COMPILE_OPTIONS:
            if (!bm_query_target_effective_compile_options_items_with_context(ctx->model, id, qctx, ctx->scratch, &items)) {
                return false;
            }
            break;

        case CG_EFFECTIVE_LINK_DIRECTORIES:
            if (!bm_query_target_effective_link_directories_items_with_context(ctx->model, id, qctx, ctx->scratch, &items)) {
                return false;
            }
            break;

        case CG_EFFECTIVE_LINK_OPTIONS:
            if (!bm_query_target_effective_link_options_items_with_context(ctx->model, id, qctx, ctx->scratch, &items)) {
                return false;
            }
            break;

        case CG_EFFECTIVE_LINK_LIBRARIES:
            if (!bm_query_target_effective_link_libraries_items_with_context(ctx->model, id, qctx, ctx->scratch, &items)) {
                return false;
            }
            break;

        case CG_EFFECTIVE_COMPILE_FEATURES:
            return false;
    }

    *out = items;
    return true;
}

bool cg_query_effective_items_cached(CG_Context *ctx,
                                     BM_Target_Id id,
                                     const BM_Query_Eval_Context *qctx,
                                     CG_Effective_Query_Family family,
                                     BM_String_Item_Span *out) {
    if (!ctx || !qctx || !out) return false;
    out->items = NULL;
    out->count = 0;

    for (size_t i = 0; i < arena_arr_len(ctx->effective_item_cache); ++i) {
        CG_Effective_Item_Cache_Entry *entry = &ctx->effective_item_cache[i];
        if (!entry->ready) continue;
        if (entry->target_id != id ||
            entry->usage_mode != qctx->usage_mode ||
            entry->family != family ||
            !nob_sv_eq(entry->config, qctx->config) ||
            !nob_sv_eq(entry->compile_language, qctx->compile_language)) {
            continue;
        }
        *out = entry->items;
        return true;
    }

    {
        CG_Effective_Item_Cache_Entry entry = {0};
        if (!cg_query_effective_items_cached_impl(ctx, id, qctx, family, &entry.items)) return false;
        entry.target_id = id;
        entry.usage_mode = qctx->usage_mode;
        entry.config = qctx->config;
        entry.compile_language = qctx->compile_language;
        entry.family = family;
        entry.ready = true;
        if (!arena_arr_push(ctx->scratch, ctx->effective_item_cache, entry)) return false;
        *out = arena_arr_last(ctx->effective_item_cache).items;
    }
    return true;
}

bool cg_query_effective_values_cached(CG_Context *ctx,
                                      BM_Target_Id id,
                                      const BM_Query_Eval_Context *qctx,
                                      CG_Effective_Query_Family family,
                                      BM_String_Span *out) {
    if (!ctx || !qctx || !out) return false;
    out->items = NULL;
    out->count = 0;

    for (size_t i = 0; i < arena_arr_len(ctx->effective_value_cache); ++i) {
        CG_Effective_Value_Cache_Entry *entry = &ctx->effective_value_cache[i];
        if (!entry->ready) continue;
        if (entry->target_id != id ||
            entry->usage_mode != qctx->usage_mode ||
            entry->family != family ||
            !nob_sv_eq(entry->config, qctx->config) ||
            !nob_sv_eq(entry->compile_language, qctx->compile_language)) {
            continue;
        }
        *out = entry->values;
        return true;
    }

    {
        CG_Effective_Value_Cache_Entry entry = {0};
        bool ok = false;
        switch (family) {
            case CG_EFFECTIVE_COMPILE_FEATURES:
                ok = bm_query_target_effective_compile_features(ctx->model, id, qctx, ctx->scratch, &entry.values);
                break;
            case CG_EFFECTIVE_INCLUDE_DIRECTORIES:
                ok = bm_query_target_effective_include_directories_with_context(ctx->model, id, qctx, ctx->scratch, &entry.values);
                break;
            case CG_EFFECTIVE_COMPILE_DEFINITIONS:
                ok = bm_query_target_effective_compile_definitions_with_context(ctx->model, id, qctx, ctx->scratch, &entry.values);
                break;
            case CG_EFFECTIVE_COMPILE_OPTIONS:
                ok = bm_query_target_effective_compile_options_with_context(ctx->model, id, qctx, ctx->scratch, &entry.values);
                break;
            case CG_EFFECTIVE_LINK_DIRECTORIES:
                ok = bm_query_target_effective_link_directories_with_context(ctx->model, id, qctx, ctx->scratch, &entry.values);
                break;
            case CG_EFFECTIVE_LINK_OPTIONS:
                ok = bm_query_target_effective_link_options_with_context(ctx->model, id, qctx, ctx->scratch, &entry.values);
                break;
            case CG_EFFECTIVE_LINK_LIBRARIES:
                ok = bm_query_target_effective_link_libraries_with_context(ctx->model, id, qctx, ctx->scratch, &entry.values);
                break;
        }
        if (!ok) return false;
        entry.target_id = id;
        entry.usage_mode = qctx->usage_mode;
        entry.config = qctx->config;
        entry.compile_language = qctx->compile_language;
        entry.family = family;
        entry.ready = true;
        if (!arena_arr_push(ctx->scratch, ctx->effective_value_cache, entry)) return false;
        *out = arena_arr_last(ctx->effective_value_cache).values;
    }
    return true;
}

bool cg_query_target_file_cached(CG_Context *ctx,
                                 BM_Target_Id id,
                                 const BM_Query_Eval_Context *qctx,
                                 bool linker_file,
                                 String_View *out) {
    if (!ctx || !qctx || !out) return false;
    *out = nob_sv_from_cstr("");

    for (size_t i = 0; i < arena_arr_len(ctx->target_file_cache); ++i) {
        CG_Target_File_Cache_Entry *entry = &ctx->target_file_cache[i];
        if (!entry->ready) continue;
        if (entry->target_id != id ||
            entry->linker_file != linker_file ||
            !nob_sv_eq(entry->config, qctx->config)) {
            continue;
        }
        *out = entry->path;
        return true;
    }

    {
        CG_Target_File_Cache_Entry entry = {0};
        bool ok = linker_file
            ? bm_query_target_effective_linker_file(ctx->model, id, qctx, ctx->scratch, &entry.path)
            : bm_query_target_effective_file(ctx->model, id, qctx, ctx->scratch, &entry.path);
        if (!ok) return false;
        entry.target_id = id;
        entry.config = qctx->config;
        entry.linker_file = linker_file;
        entry.ready = true;
        if (!arena_arr_push(ctx->scratch, ctx->target_file_cache, entry)) return false;
        *out = arena_arr_last(ctx->target_file_cache).path;
    }
    return true;
}

bool cg_query_imported_link_languages_cached(CG_Context *ctx,
                                             BM_Target_Id id,
                                             const BM_Query_Eval_Context *qctx,
                                             BM_String_Span *out) {
    if (!ctx || !qctx || !out) return false;
    out->items = NULL;
    out->count = 0;

    for (size_t i = 0; i < arena_arr_len(ctx->imported_link_lang_cache); ++i) {
        CG_Imported_Link_Lang_Cache_Entry *entry = &ctx->imported_link_lang_cache[i];
        if (!entry->ready) continue;
        if (entry->target_id != id || !nob_sv_eq(entry->config, qctx->config)) continue;
        *out = entry->languages;
        return true;
    }

    {
        CG_Imported_Link_Lang_Cache_Entry entry = {0};
        if (!bm_query_target_imported_link_languages(ctx->model, id, qctx, ctx->scratch, &entry.languages)) {
            return false;
        }
        entry.target_id = id;
        entry.config = qctx->config;
        entry.ready = true;
        if (!arena_arr_push(ctx->scratch, ctx->imported_link_lang_cache, entry)) return false;
        *out = arena_arr_last(ctx->imported_link_lang_cache).languages;
    }
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
        if (info->kind == BM_TARGET_STATIC_LIBRARY || info->kind == BM_TARGET_SHARED_LIBRARY) {
            out->linkable_artifact = true;
            out->rebuild_input_path = effective_linker_file.count > 0 ? effective_linker_file : effective_file;
        }
        return true;
    }

    out->effective_file = info->artifact_path;
    out->effective_linker_file = info->artifact_path;
    out->rebuild_input_path = info->artifact_path;
    out->linkable_artifact = cg_target_kind_is_linkable_artifact(info->kind);
    return true;
}
