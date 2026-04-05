#include "build_model_query_internal.h"

static String_View bm_query_genex_target_property_cb(void *userdata,
                                                     String_View target_name,
                                                     String_View property_name) {
    BM_Query_Genex_Context *ctx = (BM_Query_Genex_Context*)userdata;
    BM_Target_Id id = BM_TARGET_ID_INVALID;
    String_View out = nob_sv_from_cstr("");
    if (!ctx || !ctx->model) return nob_sv_from_cstr("");
    id = bm_find_target_by_name_id(ctx->model, target_name);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    id = bm_resolve_alias_target_id(ctx->model, id);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    if (!bm_query_target_property_value(ctx->model, id, property_name, ctx->scratch, &out)) {
        return nob_sv_from_cstr("");
    }
    return out;
}

static String_View bm_query_genex_target_file_cb(void *userdata, String_View target_name) {
    BM_Query_Genex_Context *ctx = (BM_Query_Genex_Context*)userdata;
    BM_Target_Id id = BM_TARGET_ID_INVALID;
    String_View out = nob_sv_from_cstr("");
    if (!ctx || !ctx->model) return nob_sv_from_cstr("");
    id = bm_find_target_by_name_id(ctx->model, target_name);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    if (!bm_query_target_effective_file_internal(ctx->model, id, ctx->eval_ctx, false, ctx->scratch, &out)) {
        return nob_sv_from_cstr("");
    }
    return out;
}

static String_View bm_query_genex_target_linker_file_cb(void *userdata, String_View target_name) {
    BM_Query_Genex_Context *ctx = (BM_Query_Genex_Context*)userdata;
    BM_Target_Id id = BM_TARGET_ID_INVALID;
    String_View out = nob_sv_from_cstr("");
    if (!ctx || !ctx->model) return nob_sv_from_cstr("");
    id = bm_find_target_by_name_id(ctx->model, target_name);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    if (!bm_query_target_effective_file_internal(ctx->model, id, ctx->eval_ctx, true, ctx->scratch, &out)) {
        return nob_sv_from_cstr("");
    }
    return out;
}

static bool bm_eval_item_span(const Build_Model *model,
                              BM_Target_Id owner_target_id,
                              const BM_Query_Eval_Context *ctx,
                              Arena *scratch,
                              BM_String_Item_Span raw_items,
                              BM_String_Item_Span *out) {
    BM_String_Item_View *items = NULL;
    BM_Query_Genex_Context gx_userdata = {0};
    BM_Query_Eval_Context default_ctx = {0};
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!scratch) return false;

    default_ctx = ctx ? *ctx : bm_default_query_eval_context(owner_target_id, BM_QUERY_USAGE_COMPILE);
    gx_userdata.model = model;
    gx_userdata.eval_ctx = &default_ctx;
    gx_userdata.scratch = scratch;
    gx_userdata.consumer_target_id = owner_target_id;
    gx_userdata.current_target_id = bm_target_id_is_valid(default_ctx.current_target_id)
        ? default_ctx.current_target_id
        : owner_target_id;

    for (size_t i = 0; i < raw_items.count; ++i) {
        Genex_Context gx = {0};
        Genex_Result gx_result = {0};
        gx.arena = scratch;
        gx.config = default_ctx.config;
        gx.platform_id = default_ctx.platform_id;
        gx.compile_language = default_ctx.compile_language;
        gx.current_target_name = bm_query_target_name(model, gx_userdata.current_target_id);
        gx.link_only_active = default_ctx.usage_mode == BM_QUERY_USAGE_LINK;
        gx.build_interface_active = default_ctx.build_interface_active;
        gx.install_interface_active = default_ctx.install_interface_active;
        gx.target_name_case_insensitive = false;
        gx.max_depth = 128;
        gx.max_target_property_depth = 64;
        gx.read_target_property = bm_query_genex_target_property_cb;
        gx.read_target_file = bm_query_genex_target_file_cb;
        gx.read_target_linker_file = bm_query_genex_target_linker_file_cb;
        gx.userdata = &gx_userdata;

        gx_result = genex_eval(&gx, raw_items.items[i].value);
        if (gx_result.status != GENEX_OK) return false;
        if (!bm_append_split_values(scratch, &items, raw_items.items[i], gx_result.value)) return false;
    }

    out->items = items;
    out->count = arena_arr_len(items);
    return true;
}

static String_View bm_query_dirname_sv(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c != '/' && c != '\\') continue;
        if (i == 0) return nob_sv_from_parts(path.data, 1);
        return nob_sv_from_parts(path.data, i);
    }
    return nob_sv_from_cstr("");
}

static bool bm_query_copy_sv(Arena *scratch, String_View value, String_View *out) {
    char *copy = NULL;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    if (value.count == 0) return true;
    copy = arena_strndup(scratch, value.data ? value.data : "", value.count);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, value.count);
    return true;
}

static bool bm_query_casefold_sv(Arena *scratch, String_View value, String_View *out) {
    char *copy = NULL;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    if (value.count == 0) return true;
    copy = arena_strndup(scratch, value.data ? value.data : "", value.count);
    if (!copy) return false;
    for (size_t i = 0; i < value.count; ++i) {
        copy[i] = (char)tolower((unsigned char)copy[i]);
    }
    *out = nob_sv_from_parts(copy, value.count);
    return true;
}

static bool bm_query_item_effective_path_key(Arena *scratch,
                                             BM_String_Item_View item,
                                             String_View *out) {
    String_View value = nob_sv_trim(item.value);
    String_View base = nob_sv_from_cstr("");
    String_View joined = value;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    if (value.count == 0) return true;
    if (!bm_sv_is_abs_path_query(value) && item.provenance.file_path.count > 0) {
        base = bm_query_dirname_sv(item.provenance.file_path);
        if (base.count > 0) {
            joined = bm_join_relative_path_query(scratch, base, value);
        }
    }
    return bm_normalize_path(scratch, joined, out);
}

static bool bm_query_make_prefixed_key(Arena *scratch,
                                       String_View prefix,
                                       String_View value,
                                       String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    nob_sb_append_buf(&sb, prefix.data ? prefix.data : "", prefix.count);
    nob_sb_append_buf(&sb, value.data ? value.data : "", value.count);
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, sb.count);
    return true;
}

static bool bm_query_effective_item_key(Arena *scratch,
                                        BM_Effective_Query_Kind kind,
                                        BM_String_Item_View item,
                                        String_View *out_key) {
    String_View value = nob_sv_trim(item.value);
    if (!scratch || !out_key) return false;
    *out_key = nob_sv_from_cstr("");

    switch (kind) {
        case BM_EFFECTIVE_INCLUDE_DIRECTORIES: {
            String_View normalized = {0};
            if (!bm_query_item_effective_path_key(scratch, item, &normalized)) return false;
            return bm_query_make_prefixed_key(scratch,
                                              (item.flags & BM_ITEM_FLAG_SYSTEM) ? nob_sv_from_cstr("S:") : nob_sv_from_cstr("N:"),
                                              normalized,
                                              out_key);
        }

        case BM_EFFECTIVE_LINK_DIRECTORIES:
            if (value.count >= 2 && value.data[0] == '-' && value.data[1] == 'L') {
                BM_String_Item_View path_item = item;
                String_View normalized = {0};
                path_item.value = nob_sv_trim(nob_sv_from_parts(value.data + 2, value.count - 2));
                if (!bm_query_item_effective_path_key(scratch, path_item, &normalized)) return false;
                return bm_query_make_prefixed_key(scratch, nob_sv_from_cstr("-L"), normalized, out_key);
            }
            return bm_query_item_effective_path_key(scratch, item, out_key);

        case BM_EFFECTIVE_COMPILE_DEFINITIONS:
        case BM_EFFECTIVE_COMPILE_OPTIONS:
        case BM_EFFECTIVE_LINK_OPTIONS:
        case BM_EFFECTIVE_LINK_LIBRARIES:
            return bm_query_copy_sv(scratch, value, out_key);

        case BM_EFFECTIVE_COMPILE_FEATURES:
            return bm_query_casefold_sv(scratch, value, out_key);
    }

    return bm_query_copy_sv(scratch, value, out_key);
}

static bool bm_query_dedup_effective_items(Arena *scratch,
                                           BM_Effective_Query_Kind kind,
                                           BM_String_Item_Span in,
                                           BM_String_Item_Span *out) {
    BM_String_Item_View *items = NULL;
    String_View *seen_keys = NULL;
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!scratch) return false;

    for (size_t i = 0; i < in.count; ++i) {
        String_View key = {0};
        bool duplicate = false;
        if (!bm_query_effective_item_key(scratch, kind, in.items[i], &key)) return false;
        for (size_t j = 0; j < arena_arr_len(seen_keys); ++j) {
            if (!nob_sv_eq(seen_keys[j], key)) continue;
            duplicate = true;
            break;
        }
        if (duplicate) continue;
        if (!arena_arr_push(scratch, seen_keys, key) ||
            !arena_arr_push(scratch, items, in.items[i])) {
            return false;
        }
    }

    out->items = items;
    out->count = arena_arr_len(items);
    return true;
}

static bool bm_query_target_effective_items_common(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   const BM_Query_Eval_Context *ctx,
                                                   Arena *scratch,
                                                   BM_String_Item_Span *out,
                                                   BM_Effective_Query_Kind kind) {
    const BM_Target_Record *target = bm_model_target(model, id);
    BM_String_Item_View *raw_items = NULL;
    BM_String_Item_Span evaluated = {0};
    uint8_t *visited = NULL;

    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!model || !target || !scratch) return false;

    if (!bm_collect_global_effective_items(model, scratch, &raw_items, kind)) return false;
    if (!bm_collect_directory_chain_items(model, target->owner_directory_id, scratch, &raw_items, kind)) return false;
    if (!bm_collect_target_items(scratch, &raw_items, target, kind, false)) return false;

    visited = arena_alloc_array_zero(scratch, uint8_t, arena_arr_len(model->targets));
    if (!visited) return false;
    visited[id] = 1;

    if (!bm_collect_dependency_usage_from_link_items(model, target, ctx, scratch, visited, &raw_items, kind)) return false;
    if (!bm_eval_item_span(model,
                           id,
                           ctx,
                           scratch,
                           (BM_String_Item_Span){.items = raw_items, .count = arena_arr_len(raw_items)},
                           &evaluated)) {
        return false;
    }

    return bm_query_dedup_effective_items(scratch, kind, evaluated, out);
}

static bool bm_query_target_effective_values_common(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    const BM_Query_Eval_Context *ctx,
                                                    Arena *scratch,
                                                    BM_String_Span *out,
                                                    BM_Effective_Query_Kind kind) {
    BM_String_Item_Span item_span = {0};
    String_View *values = NULL;

    if (!out) return false;
    out->items = NULL;
    out->count = 0;

    if (!bm_query_target_effective_items_common(model, id, ctx, scratch, &item_span, kind)) return false;

    for (size_t i = 0; i < item_span.count; ++i) {
        if (!arena_arr_push(scratch, values, item_span.items[i].value)) return false;
    }

    out->items = values;
    out->count = arena_arr_len(values);
    return true;
}

bool bm_query_target_effective_include_directories_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         Arena *scratch,
                                                         BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_include_directories_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_include_directories_items_with_context(const Build_Model *model,
                                                                      BM_Target_Id id,
                                                                      const BM_Query_Eval_Context *ctx,
                                                                      Arena *scratch,
                                                                      BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_INCLUDE_DIRECTORIES);
}

bool bm_query_target_effective_compile_definitions_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         Arena *scratch,
                                                         BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_compile_definitions_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_compile_definitions_items_with_context(const Build_Model *model,
                                                                      BM_Target_Id id,
                                                                      const BM_Query_Eval_Context *ctx,
                                                                      Arena *scratch,
                                                                      BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_DEFINITIONS);
}

bool bm_query_target_effective_compile_options_items(const Build_Model *model,
                                                     BM_Target_Id id,
                                                     Arena *scratch,
                                                     BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_compile_options_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_compile_options_items_with_context(const Build_Model *model,
                                                                  BM_Target_Id id,
                                                                  const BM_Query_Eval_Context *ctx,
                                                                  Arena *scratch,
                                                                  BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_OPTIONS);
}

bool bm_query_target_effective_link_libraries_items(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    Arena *scratch,
                                                    BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_libraries_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_libraries_items_with_context(const Build_Model *model,
                                                                 BM_Target_Id id,
                                                                 const BM_Query_Eval_Context *ctx,
                                                                 Arena *scratch,
                                                                 BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_LIBRARIES);
}

bool bm_query_target_effective_link_options_items(const Build_Model *model,
                                                  BM_Target_Id id,
                                                  Arena *scratch,
                                                  BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_options_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_options_items_with_context(const Build_Model *model,
                                                               BM_Target_Id id,
                                                               const BM_Query_Eval_Context *ctx,
                                                               Arena *scratch,
                                                               BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_OPTIONS);
}

bool bm_query_target_effective_link_directories_items(const Build_Model *model,
                                                      BM_Target_Id id,
                                                      Arena *scratch,
                                                      BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_directories_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_directories_items_with_context(const Build_Model *model,
                                                                   BM_Target_Id id,
                                                                   const BM_Query_Eval_Context *ctx,
                                                                   Arena *scratch,
                                                                   BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_DIRECTORIES);
}

bool bm_query_target_effective_include_directories(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_include_directories_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_include_directories_with_context(const Build_Model *model,
                                                                BM_Target_Id id,
                                                                const BM_Query_Eval_Context *ctx,
                                                                Arena *scratch,
                                                                BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_INCLUDE_DIRECTORIES);
}

bool bm_query_target_effective_compile_definitions(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_compile_definitions_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_compile_definitions_with_context(const Build_Model *model,
                                                                BM_Target_Id id,
                                                                const BM_Query_Eval_Context *ctx,
                                                                Arena *scratch,
                                                                BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_DEFINITIONS);
}

bool bm_query_target_effective_compile_options(const Build_Model *model,
                                               BM_Target_Id id,
                                               Arena *scratch,
                                               BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_compile_options_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_compile_options_with_context(const Build_Model *model,
                                                            BM_Target_Id id,
                                                            const BM_Query_Eval_Context *ctx,
                                                            Arena *scratch,
                                                            BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_OPTIONS);
}

bool bm_query_target_effective_link_libraries(const Build_Model *model,
                                              BM_Target_Id id,
                                              Arena *scratch,
                                              BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_libraries_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_libraries_with_context(const Build_Model *model,
                                                           BM_Target_Id id,
                                                           const BM_Query_Eval_Context *ctx,
                                                           Arena *scratch,
                                                           BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_LIBRARIES);
}

bool bm_query_target_effective_link_options(const Build_Model *model,
                                            BM_Target_Id id,
                                            Arena *scratch,
                                            BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_options_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_options_with_context(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         const BM_Query_Eval_Context *ctx,
                                                         Arena *scratch,
                                                         BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_OPTIONS);
}

bool bm_query_target_effective_link_directories(const Build_Model *model,
                                                BM_Target_Id id,
                                                Arena *scratch,
                                                BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_directories_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_directories_with_context(const Build_Model *model,
                                                             BM_Target_Id id,
                                                             const BM_Query_Eval_Context *ctx,
                                                             Arena *scratch,
                                                             BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_DIRECTORIES);
}

bool bm_query_target_effective_compile_features(const Build_Model *model,
                                                BM_Target_Id id,
                                                const BM_Query_Eval_Context *ctx,
                                                Arena *scratch,
                                                BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_FEATURES);
}
