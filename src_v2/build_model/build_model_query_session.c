#include "stb_ds.h"

typedef struct {
    char *key;
    BM_String_Item_Span value;
} BM_Query_Session_Effective_Item_Entry;

typedef struct {
    char *key;
    BM_Link_Item_Span value;
} BM_Query_Session_Effective_Link_Item_Entry;

typedef struct {
    char *key;
    BM_String_Span value;
} BM_Query_Session_Effective_Value_Entry;

typedef struct {
    char *key;
    String_View value;
} BM_Query_Session_Target_File_Entry;

typedef struct {
    char *key;
    BM_String_Span value;
} BM_Query_Session_Imported_Link_Lang_Entry;

struct BM_Query_Session {
    Arena *arena;
    const Build_Model *model;
    BM_Query_Session_Stats stats;
    BM_Query_Session_Effective_Item_Entry *effective_item_cache;
    BM_Query_Session_Effective_Link_Item_Entry *effective_link_item_cache;
    BM_Query_Session_Effective_Value_Entry *effective_value_cache;
    BM_Query_Session_Target_File_Entry *target_file_cache;
    BM_Query_Session_Imported_Link_Lang_Entry *imported_link_lang_cache;
};

static BM_Query_Eval_Context bm_query_session_normalize_effective_ctx(BM_Target_Id id,
                                                                      BM_Query_Usage_Mode usage_mode,
                                                                      const BM_Query_Eval_Context *ctx) {
    BM_Query_Eval_Context normalized = ctx ? *ctx : bm_default_query_eval_context(id, usage_mode);
    if (!ctx) return normalized;
    if (!bm_target_id_is_valid(normalized.current_target_id)) {
        normalized.current_target_id = id;
    }
    return normalized;
}

static BM_Query_Eval_Context bm_query_session_normalize_generic_ctx(const BM_Query_Eval_Context *ctx) {
    return ctx ? *ctx : (BM_Query_Eval_Context){0};
}

static void bm_query_session_cleanup(void *userdata) {
    BM_Query_Session *session = (BM_Query_Session*)userdata;
    if (!session) return;
    stbds_shfree(session->effective_item_cache);
    stbds_shfree(session->effective_link_item_cache);
    stbds_shfree(session->effective_value_cache);
    stbds_shfree(session->target_file_cache);
    stbds_shfree(session->imported_link_lang_cache);
}

static void bm_query_session_key_append_u64(Nob_String_Builder *sb, uint64_t value) {
    if (!sb) return;
    nob_sb_append_cstr(sb, nob_temp_sprintf("%llu|", (unsigned long long)value));
}

static void bm_query_session_key_append_sv(Nob_String_Builder *sb, String_View value) {
    if (!sb) return;
    nob_sb_append_cstr(sb, nob_temp_sprintf("%zu:", value.count));
    nob_sb_append_buf(sb, value.data ? value.data : "", value.count);
    nob_sb_append(sb, '|');
}

static void bm_query_session_finalize_key(Nob_String_Builder *sb) {
    if (!sb) return;
    nob_sb_append_null(sb);
}

static bool bm_query_session_copy_string_span(Arena *arena, BM_String_Span in, BM_String_Span *out) {
    String_View *items = NULL;
    if (!out) return false;
    *out = (BM_String_Span){0};
    if (!arena) return false;
    for (size_t i = 0; i < in.count; ++i) {
        String_View copy = {0};
        if (!bm_query_copy_sv(arena, in.items[i], &copy) || !arena_arr_push(arena, items, copy)) {
            return false;
        }
    }
    *out = bm_string_span(items);
    return true;
}

static bool bm_query_session_copy_item_span(Arena *arena, BM_String_Item_Span in, BM_String_Item_Span *out) {
    BM_String_Item_View *items = NULL;
    if (!out) return false;
    *out = (BM_String_Item_Span){0};
    if (!arena) return false;
    for (size_t i = 0; i < in.count; ++i) {
        BM_String_Item_View copy = in.items[i];
        if (!bm_query_copy_sv(arena, in.items[i].value, &copy.value) || !arena_arr_push(arena, items, copy)) {
            return false;
        }
    }
    out->items = items;
    out->count = arena_arr_len(items);
    return true;
}

static bool bm_query_session_copy_link_item_span(Arena *arena, BM_Link_Item_Span in, BM_Link_Item_Span *out) {
    BM_Link_Item_View *items = NULL;
    if (!out) return false;
    *out = (BM_Link_Item_Span){0};
    if (!arena) return false;
    for (size_t i = 0; i < in.count; ++i) {
        BM_Link_Item_View copy = in.items[i];
        if (!bm_query_copy_sv(arena, in.items[i].value, &copy.value) ||
            !bm_query_copy_sv(arena, in.items[i].target_name, &copy.target_name) ||
            !arena_arr_push(arena, items, copy)) {
            return false;
        }
    }
    out->items = items;
    out->count = arena_arr_len(items);
    return true;
}

static bool bm_query_session_project_values_from_items(Arena *arena,
                                                       BM_String_Item_Span items,
                                                       BM_String_Span *out) {
    String_View *values = NULL;
    if (!out) return false;
    *out = (BM_String_Span){0};
    if (!arena) return false;
    for (size_t i = 0; i < items.count; ++i) {
        if (!arena_arr_push(arena, values, items.items[i].value)) return false;
    }
    *out = bm_string_span(values);
    return true;
}

static bool bm_query_session_project_values_from_link_items(Arena *arena,
                                                            BM_Link_Item_Span items,
                                                            BM_String_Span *out) {
    String_View *values = NULL;
    if (!out) return false;
    *out = (BM_String_Span){0};
    if (!arena) return false;
    for (size_t i = 0; i < items.count; ++i) {
        if (!arena_arr_push(arena, values, items.items[i].value)) return false;
    }
    *out = bm_string_span(values);
    return true;
}

static bool bm_query_session_copy_string(Arena *arena, String_View in, String_View *out) {
    if (!out) return false;
    *out = (String_View){0};
    if (!arena) return false;
    return bm_query_copy_sv(arena, in, out);
}

static bool bm_query_session_build_effective_key(BM_Target_Id id,
                                                 BM_Effective_Query_Kind kind,
                                                 const BM_Query_Eval_Context *ctx,
                                                 Nob_String_Builder *sb) {
    if (!ctx || !sb) return false;
    nob_sb_append_cstr(sb, "effective|");
    bm_query_session_key_append_u64(sb, (uint64_t)id);
    bm_query_session_key_append_u64(sb, (uint64_t)kind);
    bm_query_session_key_append_u64(sb, (uint64_t)ctx->usage_mode);
    bm_query_session_key_append_u64(sb, (uint64_t)ctx->current_target_id);
    bm_query_session_key_append_u64(sb, (uint64_t)(ctx->build_interface_active ? 1 : 0));
    bm_query_session_key_append_u64(sb, (uint64_t)(ctx->install_interface_active ? 1 : 0));
    bm_query_session_key_append_sv(sb, ctx->config);
    bm_query_session_key_append_sv(sb, ctx->platform_id);
    bm_query_session_key_append_sv(sb, ctx->compile_language);
    bm_query_session_finalize_key(sb);
    return true;
}

static bool bm_query_session_build_target_file_key(BM_Target_Id id,
                                                   bool linker_file,
                                                   const BM_Query_Eval_Context *ctx,
                                                   Nob_String_Builder *sb) {
    if (!ctx || !sb) return false;
    nob_sb_append_cstr(sb, "target_file|");
    bm_query_session_key_append_u64(sb, (uint64_t)id);
    bm_query_session_key_append_u64(sb, (uint64_t)(linker_file ? 1 : 0));
    bm_query_session_key_append_u64(sb, (uint64_t)ctx->usage_mode);
    bm_query_session_key_append_u64(sb, (uint64_t)ctx->current_target_id);
    bm_query_session_key_append_u64(sb, (uint64_t)(ctx->build_interface_active ? 1 : 0));
    bm_query_session_key_append_u64(sb, (uint64_t)(ctx->install_interface_active ? 1 : 0));
    bm_query_session_key_append_sv(sb, ctx->config);
    bm_query_session_key_append_sv(sb, ctx->platform_id);
    bm_query_session_key_append_sv(sb, ctx->compile_language);
    bm_query_session_finalize_key(sb);
    return true;
}

static bool bm_query_session_build_imported_lang_key(BM_Target_Id id,
                                                     const BM_Query_Eval_Context *ctx,
                                                     Nob_String_Builder *sb) {
    if (!ctx || !sb) return false;
    nob_sb_append_cstr(sb, "imported_langs|");
    bm_query_session_key_append_u64(sb, (uint64_t)id);
    bm_query_session_key_append_u64(sb, (uint64_t)ctx->usage_mode);
    bm_query_session_key_append_u64(sb, (uint64_t)ctx->current_target_id);
    bm_query_session_key_append_u64(sb, (uint64_t)(ctx->build_interface_active ? 1 : 0));
    bm_query_session_key_append_u64(sb, (uint64_t)(ctx->install_interface_active ? 1 : 0));
    bm_query_session_key_append_sv(sb, ctx->config);
    bm_query_session_key_append_sv(sb, ctx->platform_id);
    bm_query_session_key_append_sv(sb, ctx->compile_language);
    bm_query_session_finalize_key(sb);
    return true;
}

static bool bm_query_session_effective_items_cached(BM_Query_Session *session,
                                                    BM_Target_Id id,
                                                    const BM_Query_Eval_Context *ctx,
                                                    BM_Effective_Query_Kind kind,
                                                    bool count_stats,
                                                    BM_String_Item_Span *out) {
    BM_Query_Session_Effective_Item_Entry *entry = NULL;
    BM_Query_Eval_Context normalized = {0};
    Arena *temp = NULL;
    BM_String_Item_Span computed = {0};
    BM_String_Item_Span cached = {0};
    Nob_String_Builder key = {0};
    if (!session || !out) return false;
    *out = (BM_String_Item_Span){0};

    normalized = bm_query_session_normalize_effective_ctx(
        id,
        kind == BM_EFFECTIVE_LINK_LIBRARIES ||
            kind == BM_EFFECTIVE_LINK_OPTIONS ||
            kind == BM_EFFECTIVE_LINK_DIRECTORIES
            ? BM_QUERY_USAGE_LINK
            : BM_QUERY_USAGE_COMPILE,
        ctx);
    if (!bm_query_session_build_effective_key(id, kind, &normalized, &key)) return false;

    entry = stbds_shgetp_null(session->effective_item_cache, key.items ? key.items : "");
    if (entry) {
        if (count_stats) session->stats.effective_item_hits++;
        *out = entry->value;
        nob_sb_free(key);
        return true;
    }

    if (count_stats) session->stats.effective_item_misses++;
    temp = arena_create(64 * 1024);
    if (!temp) {
        nob_sb_free(key);
        return false;
    }

    if (!bm_query_target_effective_items_common(session->model, id, &normalized, temp, &computed, kind) ||
        !bm_query_session_copy_item_span(session->arena, computed, &cached)) {
        arena_destroy(temp);
        nob_sb_free(key);
        return false;
    }

    stbds_shput(session->effective_item_cache,
                key.items ? key.items : "",
                cached);
    entry = stbds_shgetp_null(session->effective_item_cache, key.items ? key.items : "");
    arena_destroy(temp);
    nob_sb_free(key);
    if (!entry) return false;
    *out = entry->value;
    return true;
}

static bool bm_query_session_effective_link_items_cached(BM_Query_Session *session,
                                                         BM_Target_Id id,
                                                         const BM_Query_Eval_Context *ctx,
                                                         bool count_stats,
                                                         BM_Link_Item_Span *out) {
    BM_Query_Session_Effective_Link_Item_Entry *entry = NULL;
    BM_Query_Eval_Context normalized = {0};
    Arena *temp = NULL;
    BM_Link_Item_Span computed = {0};
    BM_Link_Item_Span cached = {0};
    Nob_String_Builder key = {0};
    if (!session || !out) return false;
    *out = (BM_Link_Item_Span){0};

    normalized = bm_query_session_normalize_effective_ctx(id, BM_QUERY_USAGE_LINK, ctx);
    if (!bm_query_session_build_effective_key(id, BM_EFFECTIVE_LINK_LIBRARIES, &normalized, &key)) return false;

    entry = stbds_shgetp_null(session->effective_link_item_cache, key.items ? key.items : "");
    if (entry) {
        if (count_stats) session->stats.effective_item_hits++;
        *out = entry->value;
        nob_sb_free(key);
        return true;
    }

    if (count_stats) session->stats.effective_item_misses++;
    temp = arena_create(64 * 1024);
    if (!temp) {
        nob_sb_free(key);
        return false;
    }

    if (!bm_query_target_effective_link_items_common(session->model, id, &normalized, temp, &computed) ||
        !bm_query_session_copy_link_item_span(session->arena, computed, &cached)) {
        arena_destroy(temp);
        nob_sb_free(key);
        return false;
    }

    stbds_shput(session->effective_link_item_cache,
                key.items ? key.items : "",
                cached);
    entry = stbds_shgetp_null(session->effective_link_item_cache, key.items ? key.items : "");
    arena_destroy(temp);
    nob_sb_free(key);
    if (!entry) return false;
    *out = entry->value;
    return true;
}

static bool bm_query_session_effective_values_cached(BM_Query_Session *session,
                                                     BM_Target_Id id,
                                                     const BM_Query_Eval_Context *ctx,
                                                     BM_Effective_Query_Kind kind,
                                                     bool count_stats,
                                                     BM_String_Span *out) {
    BM_Query_Session_Effective_Value_Entry *entry = NULL;
    BM_Query_Eval_Context normalized = {0};
    BM_String_Span cached = {0};
    Nob_String_Builder key = {0};
    if (!session || !out) return false;
    *out = (BM_String_Span){0};

    normalized = bm_query_session_normalize_effective_ctx(
        id,
        kind == BM_EFFECTIVE_LINK_LIBRARIES ||
            kind == BM_EFFECTIVE_LINK_OPTIONS ||
            kind == BM_EFFECTIVE_LINK_DIRECTORIES
            ? BM_QUERY_USAGE_LINK
            : BM_QUERY_USAGE_COMPILE,
        ctx);
    if (!bm_query_session_build_effective_key(id, kind, &normalized, &key)) return false;

    entry = stbds_shgetp_null(session->effective_value_cache, key.items ? key.items : "");
    if (entry) {
        if (count_stats) session->stats.effective_value_hits++;
        *out = entry->value;
        nob_sb_free(key);
        return true;
    }

    if (count_stats) session->stats.effective_value_misses++;
    {
        if (kind == BM_EFFECTIVE_LINK_LIBRARIES) {
            BM_Link_Item_Span link_items = {0};
            if (!bm_query_session_effective_link_items_cached(session, id, &normalized, false, &link_items) ||
                !bm_query_session_project_values_from_link_items(session->arena, link_items, &cached)) {
                nob_sb_free(key);
                return false;
            }
        } else {
            BM_String_Item_Span items = {0};
            if (!bm_query_session_effective_items_cached(session, id, &normalized, kind, false, &items) ||
                !bm_query_session_project_values_from_items(session->arena, items, &cached)) {
                nob_sb_free(key);
                return false;
            }
        }
    }

    stbds_shput(session->effective_value_cache,
                key.items ? key.items : "",
                cached);
    entry = stbds_shgetp_null(session->effective_value_cache, key.items ? key.items : "");
    nob_sb_free(key);
    if (!entry) return false;
    *out = entry->value;
    return true;
}

static bool bm_query_session_target_file_cached(BM_Query_Session *session,
                                                BM_Target_Id id,
                                                const BM_Query_Eval_Context *ctx,
                                                bool linker_file,
                                                bool count_stats,
                                                String_View *out) {
    BM_Query_Session_Target_File_Entry *entry = NULL;
    BM_Query_Eval_Context normalized = bm_query_session_normalize_generic_ctx(ctx);
    Arena *temp = NULL;
    String_View computed = {0};
    String_View cached = {0};
    Nob_String_Builder key = {0};
    if (!session || !out) return false;
    *out = (String_View){0};

    if (!bm_query_session_build_target_file_key(id, linker_file, &normalized, &key)) return false;
    entry = stbds_shgetp_null(session->target_file_cache, key.items ? key.items : "");
    if (entry) {
        if (count_stats) session->stats.target_file_hits++;
        *out = entry->value;
        nob_sb_free(key);
        return true;
    }

    if (count_stats) session->stats.target_file_misses++;
    temp = arena_create(64 * 1024);
    if (!temp) {
        nob_sb_free(key);
        return false;
    }

    if (!bm_query_target_effective_file_internal(session->model,
                                                 id,
                                                 &normalized,
                                                 linker_file,
                                                 temp,
                                                 &computed) ||
        !bm_query_session_copy_string(session->arena, computed, &cached)) {
        arena_destroy(temp);
        nob_sb_free(key);
        return false;
    }

    stbds_shput(session->target_file_cache,
                key.items ? key.items : "",
                cached);
    entry = stbds_shgetp_null(session->target_file_cache, key.items ? key.items : "");
    arena_destroy(temp);
    nob_sb_free(key);
    if (!entry) return false;
    *out = entry->value;
    return true;
}

static bool bm_query_session_imported_link_languages_cached(BM_Query_Session *session,
                                                            BM_Target_Id id,
                                                            const BM_Query_Eval_Context *ctx,
                                                            bool count_stats,
                                                            BM_String_Span *out) {
    BM_Query_Session_Imported_Link_Lang_Entry *entry = NULL;
    BM_Query_Eval_Context normalized = bm_query_session_normalize_generic_ctx(ctx);
    Arena *temp = NULL;
    BM_String_Span computed = {0};
    BM_String_Span cached = {0};
    Nob_String_Builder key = {0};
    if (!session || !out) return false;
    *out = (BM_String_Span){0};

    if (!bm_query_session_build_imported_lang_key(id, &normalized, &key)) return false;
    entry = stbds_shgetp_null(session->imported_link_lang_cache, key.items ? key.items : "");
    if (entry) {
        if (count_stats) session->stats.imported_link_language_hits++;
        *out = entry->value;
        nob_sb_free(key);
        return true;
    }

    if (count_stats) session->stats.imported_link_language_misses++;
    temp = arena_create(64 * 1024);
    if (!temp) {
        nob_sb_free(key);
        return false;
    }

    if (!bm_query_target_imported_link_languages(session->model, id, &normalized, temp, &computed) ||
        !bm_query_session_copy_string_span(session->arena, computed, &cached)) {
        arena_destroy(temp);
        nob_sb_free(key);
        return false;
    }

    stbds_shput(session->imported_link_lang_cache,
                key.items ? key.items : "",
                cached);
    entry = stbds_shgetp_null(session->imported_link_lang_cache, key.items ? key.items : "");
    arena_destroy(temp);
    nob_sb_free(key);
    if (!entry) return false;
    *out = entry->value;
    return true;
}

BM_Query_Session *bm_query_session_create(Arena *arena, const Build_Model *model) {
    BM_Query_Session *session = NULL;
    if (!arena || !model) return NULL;
    session = arena_alloc_zero(arena, sizeof(*session));
    if (!session) return NULL;
    session->arena = arena;
    session->model = model;
    if (!arena_on_destroy(arena, bm_query_session_cleanup, session)) return NULL;
    stbds_sh_new_arena(session->effective_item_cache);
    stbds_sh_new_arena(session->effective_link_item_cache);
    stbds_sh_new_arena(session->effective_value_cache);
    stbds_sh_new_arena(session->target_file_cache);
    stbds_sh_new_arena(session->imported_link_lang_cache);
    return session;
}

const BM_Query_Session_Stats *bm_query_session_stats(const BM_Query_Session *session) {
    return session ? &session->stats : NULL;
}

bool bm_query_session_target_effective_include_directories_items(BM_Query_Session *session,
                                                                 BM_Target_Id id,
                                                                 const BM_Query_Eval_Context *ctx,
                                                                 BM_String_Item_Span *out) {
    return bm_query_session_effective_items_cached(session,
                                                   id,
                                                   ctx,
                                                   BM_EFFECTIVE_INCLUDE_DIRECTORIES,
                                                   true,
                                                   out);
}

bool bm_query_session_target_effective_compile_definitions_items(BM_Query_Session *session,
                                                                 BM_Target_Id id,
                                                                 const BM_Query_Eval_Context *ctx,
                                                                 BM_String_Item_Span *out) {
    return bm_query_session_effective_items_cached(session,
                                                   id,
                                                   ctx,
                                                   BM_EFFECTIVE_COMPILE_DEFINITIONS,
                                                   true,
                                                   out);
}

bool bm_query_session_target_effective_compile_options_items(BM_Query_Session *session,
                                                             BM_Target_Id id,
                                                             const BM_Query_Eval_Context *ctx,
                                                             BM_String_Item_Span *out) {
    return bm_query_session_effective_items_cached(session,
                                                   id,
                                                   ctx,
                                                   BM_EFFECTIVE_COMPILE_OPTIONS,
                                                   true,
                                                   out);
}

bool bm_query_session_target_effective_link_libraries_items(BM_Query_Session *session,
                                                            BM_Target_Id id,
                                                            const BM_Query_Eval_Context *ctx,
                                                            BM_Link_Item_Span *out) {
    return bm_query_session_effective_link_items_cached(session, id, ctx, true, out);
}

bool bm_query_session_target_effective_link_options_items(BM_Query_Session *session,
                                                          BM_Target_Id id,
                                                          const BM_Query_Eval_Context *ctx,
                                                          BM_String_Item_Span *out) {
    return bm_query_session_effective_items_cached(session,
                                                   id,
                                                   ctx,
                                                   BM_EFFECTIVE_LINK_OPTIONS,
                                                   true,
                                                   out);
}

bool bm_query_session_target_effective_link_directories_items(BM_Query_Session *session,
                                                              BM_Target_Id id,
                                                              const BM_Query_Eval_Context *ctx,
                                                              BM_String_Item_Span *out) {
    return bm_query_session_effective_items_cached(session,
                                                   id,
                                                   ctx,
                                                   BM_EFFECTIVE_LINK_DIRECTORIES,
                                                   true,
                                                   out);
}

bool bm_query_session_target_effective_compile_features_items(BM_Query_Session *session,
                                                              BM_Target_Id id,
                                                              const BM_Query_Eval_Context *ctx,
                                                              BM_String_Item_Span *out) {
    return bm_query_session_effective_items_cached(session,
                                                   id,
                                                   ctx,
                                                   BM_EFFECTIVE_COMPILE_FEATURES,
                                                   true,
                                                   out);
}

bool bm_query_session_target_effective_include_directories(BM_Query_Session *session,
                                                           BM_Target_Id id,
                                                           const BM_Query_Eval_Context *ctx,
                                                           BM_String_Span *out) {
    return bm_query_session_effective_values_cached(session,
                                                    id,
                                                    ctx,
                                                    BM_EFFECTIVE_INCLUDE_DIRECTORIES,
                                                    true,
                                                    out);
}

bool bm_query_session_target_effective_compile_definitions(BM_Query_Session *session,
                                                           BM_Target_Id id,
                                                           const BM_Query_Eval_Context *ctx,
                                                           BM_String_Span *out) {
    return bm_query_session_effective_values_cached(session,
                                                    id,
                                                    ctx,
                                                    BM_EFFECTIVE_COMPILE_DEFINITIONS,
                                                    true,
                                                    out);
}

bool bm_query_session_target_effective_compile_options(BM_Query_Session *session,
                                                       BM_Target_Id id,
                                                       const BM_Query_Eval_Context *ctx,
                                                       BM_String_Span *out) {
    return bm_query_session_effective_values_cached(session,
                                                    id,
                                                    ctx,
                                                    BM_EFFECTIVE_COMPILE_OPTIONS,
                                                    true,
                                                    out);
}

bool bm_query_session_target_effective_link_libraries(BM_Query_Session *session,
                                                      BM_Target_Id id,
                                                      const BM_Query_Eval_Context *ctx,
                                                      BM_String_Span *out) {
    return bm_query_session_effective_values_cached(session,
                                                    id,
                                                    ctx,
                                                    BM_EFFECTIVE_LINK_LIBRARIES,
                                                    true,
                                                    out);
}

bool bm_query_session_target_effective_link_options(BM_Query_Session *session,
                                                    BM_Target_Id id,
                                                    const BM_Query_Eval_Context *ctx,
                                                    BM_String_Span *out) {
    return bm_query_session_effective_values_cached(session,
                                                    id,
                                                    ctx,
                                                    BM_EFFECTIVE_LINK_OPTIONS,
                                                    true,
                                                    out);
}

bool bm_query_session_target_effective_link_directories(BM_Query_Session *session,
                                                        BM_Target_Id id,
                                                        const BM_Query_Eval_Context *ctx,
                                                        BM_String_Span *out) {
    return bm_query_session_effective_values_cached(session,
                                                    id,
                                                    ctx,
                                                    BM_EFFECTIVE_LINK_DIRECTORIES,
                                                    true,
                                                    out);
}

bool bm_query_session_target_effective_compile_features(BM_Query_Session *session,
                                                        BM_Target_Id id,
                                                        const BM_Query_Eval_Context *ctx,
                                                        BM_String_Span *out) {
    return bm_query_session_effective_values_cached(session,
                                                    id,
                                                    ctx,
                                                    BM_EFFECTIVE_COMPILE_FEATURES,
                                                    true,
                                                    out);
}

bool bm_query_session_target_effective_file(BM_Query_Session *session,
                                            BM_Target_Id id,
                                            const BM_Query_Eval_Context *ctx,
                                            String_View *out) {
    return bm_query_session_target_file_cached(session, id, ctx, false, true, out);
}

bool bm_query_session_target_effective_linker_file(BM_Query_Session *session,
                                                   BM_Target_Id id,
                                                   const BM_Query_Eval_Context *ctx,
                                                   String_View *out) {
    return bm_query_session_target_file_cached(session, id, ctx, true, true, out);
}

bool bm_query_session_target_imported_link_languages(BM_Query_Session *session,
                                                     BM_Target_Id id,
                                                     const BM_Query_Eval_Context *ctx,
                                                     BM_String_Span *out) {
    return bm_query_session_imported_link_languages_cached(session, id, ctx, true, out);
}
