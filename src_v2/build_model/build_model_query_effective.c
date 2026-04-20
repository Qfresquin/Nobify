#include "build_model_query_internal.h"

static bool bm_query_sv_in_ci_list(const String_View *items, size_t count, String_View needle) {
    String_View trimmed = nob_sv_trim(needle);
    if (trimmed.count == 0) return false;
    for (size_t i = 0; i < count; ++i) {
        if (bm_sv_eq_ci_query(nob_sv_trim(items[i]), trimmed)) return true;
    }
    return false;
}

static bool bm_query_semantic_matches_context(const Event_Link_Item_Metadata *semantic,
                                              const BM_Query_Eval_Context *ctx) {
    bool is_debug = false;
    if (!semantic || !ctx) return true;

    if (semantic->interface_filter == EVENT_USAGE_INTERFACE_BUILD && !ctx->build_interface_active) return false;
    if (semantic->interface_filter == EVENT_USAGE_INTERFACE_INSTALL && !ctx->install_interface_active) return false;
    if (semantic->link_only && ctx->usage_mode != BM_QUERY_USAGE_LINK) return false;

    is_debug = bm_sv_eq_ci_query(nob_sv_trim(ctx->config), nob_sv_from_cstr("Debug"));
    switch (semantic->config_filter) {
        case EVENT_LINK_ITEM_CONFIG_ALL:
            break;
        case EVENT_LINK_ITEM_CONFIG_DEBUG_ONLY:
            if (!is_debug) return false;
            break;
        case EVENT_LINK_ITEM_CONFIG_NONDEBUG_ONLY:
            if (is_debug) return false;
            break;
        case EVENT_LINK_ITEM_CONFIG_MATCH_LIST:
            if (!bm_query_sv_in_ci_list(semantic->configurations,
                                        semantic->configuration_count,
                                        ctx->config)) {
                return false;
            }
            break;
    }

    if (semantic->compile_language_count > 0 &&
        !bm_query_sv_in_ci_list(semantic->compile_languages,
                                semantic->compile_language_count,
                                ctx->compile_language)) {
        return false;
    }
    if (semantic->platform_id_count > 0 &&
        !bm_query_sv_in_ci_list(semantic->platform_ids,
                                semantic->platform_id_count,
                                ctx->platform_id)) {
        return false;
    }

    return true;
}

typedef struct {
    BM_Target_Id target_id;
    BM_Effective_Query_Kind kind;
    String_View property_name;
} BM_Query_Target_Property_Frame;

typedef struct {
    BM_Query_Target_Property_Frame frames[64];
    size_t count;
} BM_Query_Target_Property_Stack;

static bool bm_query_target_property_stack_contains(const BM_Query_Target_Property_Stack *stack,
                                                    BM_Target_Id target_id,
                                                    BM_Effective_Query_Kind kind,
                                                    String_View property_name) {
    if (!stack) return false;
    for (size_t i = 0; i < stack->count; ++i) {
        if (stack->frames[i].target_id != target_id || stack->frames[i].kind != kind) continue;
        if (bm_sv_eq_ci_query(stack->frames[i].property_name, property_name)) return true;
    }
    return false;
}

static bool bm_query_target_property_stack_push(BM_Query_Target_Property_Stack *stack,
                                                BM_Target_Id target_id,
                                                BM_Effective_Query_Kind kind,
                                                String_View property_name) {
    if (!stack || stack->count >= NOB_ARRAY_LEN(stack->frames)) return false;
    stack->frames[stack->count++] = (BM_Query_Target_Property_Frame){
        .target_id = target_id,
        .kind = kind,
        .property_name = property_name,
    };
    return true;
}

static void bm_query_target_property_stack_pop(BM_Query_Target_Property_Stack *stack) {
    if (stack && stack->count > 0) stack->count--;
}

static BM_Target_Id bm_query_semantic_target_id(const Build_Model *model,
                                                BM_Target_Id owner_target_id,
                                                const BM_Query_Eval_Context *ctx,
                                                const Event_Link_Item_Metadata *semantic) {
    BM_Target_Id target_id = BM_TARGET_ID_INVALID;
    if (!model || !ctx || !semantic) return BM_TARGET_ID_INVALID;
    if (semantic->kind == EVENT_LINK_ITEM_TARGET_PROPERTY_IMPLICIT) {
        target_id = bm_target_id_is_valid(ctx->current_target_id) ? ctx->current_target_id : owner_target_id;
    } else if (semantic->kind == EVENT_LINK_ITEM_TARGET_PROPERTY_EXPLICIT) {
        target_id = bm_find_target_by_name_id(model, semantic->target_name);
    }
    if (!bm_target_id_is_valid(target_id)) return BM_TARGET_ID_INVALID;
    target_id = bm_resolve_alias_target_id(model, target_id);
    return bm_target_id_is_valid(target_id) ? target_id : BM_TARGET_ID_INVALID;
}

static bool bm_query_property_effective_kind(String_View property_name, BM_Effective_Query_Kind *out_kind) {
    if (!out_kind) return false;
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INCLUDE_DIRECTORIES")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_INCLUDE_DIRECTORIES")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_SYSTEM_INCLUDE_DIRECTORIES"))) {
        *out_kind = BM_EFFECTIVE_INCLUDE_DIRECTORIES;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("COMPILE_DEFINITIONS")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_COMPILE_DEFINITIONS"))) {
        *out_kind = BM_EFFECTIVE_COMPILE_DEFINITIONS;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("COMPILE_OPTIONS")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_COMPILE_OPTIONS"))) {
        *out_kind = BM_EFFECTIVE_COMPILE_OPTIONS;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("COMPILE_FEATURES")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES"))) {
        *out_kind = BM_EFFECTIVE_COMPILE_FEATURES;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("LINK_LIBRARIES")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_LINK_LIBRARIES"))) {
        *out_kind = BM_EFFECTIVE_LINK_LIBRARIES;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("LINK_OPTIONS")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_LINK_OPTIONS"))) {
        *out_kind = BM_EFFECTIVE_LINK_OPTIONS;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("LINK_DIRECTORIES")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_LINK_DIRECTORIES"))) {
        *out_kind = BM_EFFECTIVE_LINK_DIRECTORIES;
        return true;
    }
    return false;
}

static bool bm_query_same_family_property_spec(String_View property_name,
                                               BM_Effective_Query_Kind current_kind,
                                               BM_Visibility *out_min,
                                               BM_Visibility *out_max,
                                               uint32_t *out_required_flags) {
    BM_Effective_Query_Kind property_kind = BM_EFFECTIVE_INCLUDE_DIRECTORIES;
    if (!out_min || !out_max || !out_required_flags) return false;
    *out_min = BM_VISIBILITY_PRIVATE;
    *out_max = BM_VISIBILITY_INTERFACE;
    *out_required_flags = 0;

    if (!bm_query_property_effective_kind(property_name, &property_kind) || property_kind != current_kind) {
        return false;
    }

    if (nob_sv_starts_with(property_name, nob_sv_from_cstr("INTERFACE_"))) {
        *out_min = BM_VISIBILITY_PUBLIC;
        *out_max = BM_VISIBILITY_INTERFACE;
    } else {
        *out_min = BM_VISIBILITY_PRIVATE;
        *out_max = BM_VISIBILITY_PUBLIC;
    }

    if (current_kind == BM_EFFECTIVE_INCLUDE_DIRECTORIES &&
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_SYSTEM_INCLUDE_DIRECTORIES"))) {
        *out_required_flags = BM_ITEM_FLAG_SYSTEM;
    }
    return true;
}

static BM_String_Item_Span bm_query_target_string_items_for_kind(const BM_Target_Record *target,
                                                                 BM_Effective_Query_Kind kind) {
    if (!target) return (BM_String_Item_Span){0};
    switch (kind) {
        case BM_EFFECTIVE_INCLUDE_DIRECTORIES:
            return bm_item_span(target->include_directories);
        case BM_EFFECTIVE_COMPILE_DEFINITIONS:
            return bm_item_span(target->compile_definitions);
        case BM_EFFECTIVE_COMPILE_OPTIONS:
            return bm_item_span(target->compile_options);
        case BM_EFFECTIVE_COMPILE_FEATURES:
            return bm_item_span(target->compile_features);
        case BM_EFFECTIVE_LINK_OPTIONS:
            return bm_item_span(target->link_options);
        case BM_EFFECTIVE_LINK_DIRECTORIES:
            return bm_item_span(target->link_directories);
        case BM_EFFECTIVE_LINK_LIBRARIES:
            break;
    }
    return (BM_String_Item_Span){0};
}

static BM_Link_Item_Span bm_query_target_link_items_for_kind(const BM_Target_Record *target,
                                                             BM_Effective_Query_Kind kind) {
    if (!target || kind != BM_EFFECTIVE_LINK_LIBRARIES) return (BM_Link_Item_Span){0};
    return bm_link_item_span(target->link_libraries);
}

static bool bm_query_resolve_item_value(const Build_Model *model,
                                        const BM_Query_Eval_Context *ctx,
                                        Arena *scratch,
                                        String_View fallback_value,
                                        const Event_Link_Item_Metadata *semantic,
                                        String_View *out) {
    String_View raw = fallback_value;
    if (!out) return false;
    *out = nob_sv_from_cstr("");
    if (!semantic) {
        *out = fallback_value;
        return true;
    }
    if (semantic->value.count > 0) raw = semantic->value;
    return bm_query_resolve_string_with_context(model, ctx, scratch, raw, out);
}

static bool bm_eval_item_span_impl(const Build_Model *model,
                                   BM_Target_Id owner_target_id,
                                   const BM_Query_Eval_Context *ctx,
                                   Arena *scratch,
                                   BM_String_Item_Span raw_items,
                                   BM_Effective_Query_Kind current_kind,
                                   BM_Query_Target_Property_Stack *stack,
                                   BM_String_Item_Span *out);

static bool bm_eval_link_item_span_impl(const Build_Model *model,
                                        BM_Target_Id owner_target_id,
                                        const BM_Query_Eval_Context *ctx,
                                        Arena *scratch,
                                        BM_Link_Item_Span raw_items,
                                        BM_Query_Target_Property_Stack *stack,
                                        BM_Link_Item_Span *out);

static bool bm_query_expand_same_family_string_property(const Build_Model *model,
                                                        BM_Target_Id owner_target_id,
                                                        const BM_Query_Eval_Context *ctx,
                                                        Arena *scratch,
                                                        const Event_Link_Item_Metadata *semantic,
                                                        BM_Effective_Query_Kind current_kind,
                                                        BM_Query_Target_Property_Stack *stack,
                                                        BM_String_Item_View **out_items) {
    const BM_Target_Record *target = NULL;
    BM_String_Item_View *selected = NULL;
    BM_String_Item_Span evaluated = {0};
    BM_String_Item_Span source = {0};
    BM_Visibility min_visibility = BM_VISIBILITY_PRIVATE;
    BM_Visibility max_visibility = BM_VISIBILITY_INTERFACE;
    uint32_t required_flags = 0;
    BM_Target_Id target_id = BM_TARGET_ID_INVALID;
    if (!model || !ctx || !scratch || !semantic || !out_items) return false;
    if (!bm_query_same_family_property_spec(semantic->property_name,
                                            current_kind,
                                            &min_visibility,
                                            &max_visibility,
                                            &required_flags)) {
        return false;
    }

    target_id = bm_query_semantic_target_id(model, owner_target_id, ctx, semantic);
    if (!bm_target_id_is_valid(target_id)) return true;
    if (bm_query_target_property_stack_contains(stack, target_id, current_kind, semantic->property_name)) {
        return false;
    }
    if (!bm_query_target_property_stack_push(stack, target_id, current_kind, semantic->property_name)) {
        return false;
    }

    target = bm_model_target(model, target_id);
    source = bm_query_target_string_items_for_kind(target, current_kind);
    for (size_t i = 0; i < source.count; ++i) {
        BM_String_Item_View item = source.items[i];
        if (item.visibility < min_visibility || item.visibility > max_visibility) continue;
        if ((item.flags & required_flags) != required_flags) continue;
        if (!arena_arr_push(scratch, selected, item)) {
            bm_query_target_property_stack_pop(stack);
            return false;
        }
    }

    if (!bm_eval_item_span_impl(model,
                                target_id,
                                ctx,
                                scratch,
                                (BM_String_Item_Span){.items = selected, .count = arena_arr_len(selected)},
                                current_kind,
                                stack,
                                &evaluated)) {
        bm_query_target_property_stack_pop(stack);
        return false;
    }

    for (size_t i = 0; i < evaluated.count; ++i) {
        if (!arena_arr_push(scratch, *out_items, evaluated.items[i])) {
            bm_query_target_property_stack_pop(stack);
            return false;
        }
    }

    bm_query_target_property_stack_pop(stack);
    return true;
}

static bool bm_query_expand_same_family_link_property(const Build_Model *model,
                                                      BM_Target_Id owner_target_id,
                                                      const BM_Query_Eval_Context *ctx,
                                                      Arena *scratch,
                                                      const Event_Link_Item_Metadata *semantic,
                                                      BM_Query_Target_Property_Stack *stack,
                                                      BM_Link_Item_View **out_items) {
    const BM_Target_Record *target = NULL;
    BM_Link_Item_View *selected = NULL;
    BM_Link_Item_Span evaluated = {0};
    BM_Link_Item_Span source = {0};
    BM_Visibility min_visibility = BM_VISIBILITY_PRIVATE;
    BM_Visibility max_visibility = BM_VISIBILITY_INTERFACE;
    uint32_t required_flags = 0;
    BM_Target_Id target_id = BM_TARGET_ID_INVALID;
    if (!model || !ctx || !scratch || !semantic || !out_items) return false;
    if (!bm_query_same_family_property_spec(semantic->property_name,
                                            BM_EFFECTIVE_LINK_LIBRARIES,
                                            &min_visibility,
                                            &max_visibility,
                                            &required_flags)) {
        return false;
    }
    (void)required_flags;

    target_id = bm_query_semantic_target_id(model, owner_target_id, ctx, semantic);
    if (!bm_target_id_is_valid(target_id)) return true;
    if (bm_query_target_property_stack_contains(stack,
                                                target_id,
                                                BM_EFFECTIVE_LINK_LIBRARIES,
                                                semantic->property_name)) {
        return false;
    }
    if (!bm_query_target_property_stack_push(stack,
                                             target_id,
                                             BM_EFFECTIVE_LINK_LIBRARIES,
                                             semantic->property_name)) {
        return false;
    }

    target = bm_model_target(model, target_id);
    source = bm_query_target_link_items_for_kind(target, BM_EFFECTIVE_LINK_LIBRARIES);
    for (size_t i = 0; i < source.count; ++i) {
        BM_Link_Item_View item = source.items[i];
        if (item.visibility < min_visibility || item.visibility > max_visibility) continue;
        if (!arena_arr_push(scratch, selected, item)) {
            bm_query_target_property_stack_pop(stack);
            return false;
        }
    }

    if (!bm_eval_link_item_span_impl(model,
                                     target_id,
                                     ctx,
                                     scratch,
                                     (BM_Link_Item_Span){.items = selected, .count = arena_arr_len(selected)},
                                     stack,
                                     &evaluated)) {
        bm_query_target_property_stack_pop(stack);
        return false;
    }

    for (size_t i = 0; i < evaluated.count; ++i) {
        if (!arena_arr_push(scratch, *out_items, evaluated.items[i])) {
            bm_query_target_property_stack_pop(stack);
            return false;
        }
    }

    bm_query_target_property_stack_pop(stack);
    return true;
}

static bool bm_eval_item_span_impl(const Build_Model *model,
                                   BM_Target_Id owner_target_id,
                                   const BM_Query_Eval_Context *ctx,
                                   Arena *scratch,
                                   BM_String_Item_Span raw_items,
                                   BM_Effective_Query_Kind current_kind,
                                   BM_Query_Target_Property_Stack *stack,
                                   BM_String_Item_Span *out) {
    BM_String_Item_View *items = NULL;
    BM_Query_Eval_Context default_ctx = {0};
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!scratch) return false;

    default_ctx = ctx ? *ctx : bm_default_query_eval_context(owner_target_id, BM_QUERY_USAGE_COMPILE);

    for (size_t i = 0; i < raw_items.count; ++i) {
        BM_String_Item_View item = raw_items.items[i];
        String_View resolved = nob_sv_from_cstr("");
        if (!bm_query_semantic_matches_context(&item.semantic, &default_ctx)) continue;
        if (item.semantic.kind == EVENT_LINK_ITEM_TARGET_PROPERTY_IMPLICIT ||
            item.semantic.kind == EVENT_LINK_ITEM_TARGET_PROPERTY_EXPLICIT) {
            BM_Effective_Query_Kind property_kind = BM_EFFECTIVE_INCLUDE_DIRECTORIES;
            if (bm_query_property_effective_kind(item.semantic.property_name, &property_kind) &&
                property_kind == current_kind) {
                if (!bm_query_expand_same_family_string_property(model,
                                                                 owner_target_id,
                                                                 &default_ctx,
                                                                 scratch,
                                                                 &item.semantic,
                                                                 current_kind,
                                                                 stack,
                                                                 &items)) {
                    return false;
                }
                continue;
            }
        }
        if (!bm_query_resolve_item_value(model,
                                         &default_ctx,
                                         scratch,
                                         item.value,
                                         &item.semantic,
                                         &resolved)) {
            return false;
        }
        if (resolved.count == 0) continue;
        item.value = resolved;
        if (!bm_append_split_values(scratch, &items, item, resolved)) return false;
    }

    out->items = items;
    out->count = arena_arr_len(items);
    return true;
}

static bool bm_eval_item_span(const Build_Model *model,
                              BM_Target_Id owner_target_id,
                              const BM_Query_Eval_Context *ctx,
                              Arena *scratch,
                              BM_String_Item_Span raw_items,
                              BM_Effective_Query_Kind current_kind,
                              BM_String_Item_Span *out) {
    BM_Query_Target_Property_Stack stack = {0};
    return bm_eval_item_span_impl(model, owner_target_id, ctx, scratch, raw_items, current_kind, &stack, out);
}

static bool bm_eval_link_item_span_impl(const Build_Model *model,
                                        BM_Target_Id owner_target_id,
                                        const BM_Query_Eval_Context *ctx,
                                        Arena *scratch,
                                        BM_Link_Item_Span raw_items,
                                        BM_Query_Target_Property_Stack *stack,
                                        BM_Link_Item_Span *out) {
    BM_Link_Item_View *items = NULL;
    BM_Query_Eval_Context default_ctx = {0};
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!scratch) return false;

    default_ctx = ctx ? *ctx : bm_default_query_eval_context(owner_target_id, BM_QUERY_USAGE_LINK);

    for (size_t i = 0; i < raw_items.count; ++i) {
        BM_Link_Item_View item = raw_items.items[i];
        String_View resolved = nob_sv_from_cstr("");
        if (!bm_query_semantic_matches_context(&item.semantic, &default_ctx)) continue;
        if (item.semantic.kind == EVENT_LINK_ITEM_TARGET_PROPERTY_IMPLICIT ||
            item.semantic.kind == EVENT_LINK_ITEM_TARGET_PROPERTY_EXPLICIT) {
            BM_Effective_Query_Kind property_kind = BM_EFFECTIVE_INCLUDE_DIRECTORIES;
            if (bm_query_property_effective_kind(item.semantic.property_name, &property_kind) &&
                property_kind == BM_EFFECTIVE_LINK_LIBRARIES) {
                if (!bm_query_expand_same_family_link_property(model,
                                                               owner_target_id,
                                                               &default_ctx,
                                                               scratch,
                                                               &item.semantic,
                                                               stack,
                                                               &items)) {
                    return false;
                }
                continue;
            }
        }
        if (!bm_query_resolve_item_value(model,
                                         &default_ctx,
                                         scratch,
                                         item.value,
                                         &item.semantic,
                                         &resolved)) {
            return false;
        }
        if (resolved.count == 0) continue;
        item.value = resolved;
        if (item.semantic.kind == EVENT_LINK_ITEM_TARGET_REF &&
            !bm_target_id_is_valid(item.target_id) &&
            item.semantic.target_name.count > 0) {
            BM_Target_Id target_id = bm_find_target_by_name_id(model, item.semantic.target_name);
            if (bm_target_id_is_valid(target_id)) target_id = bm_resolve_alias_target_id(model, target_id);
            item.target_id = target_id;
        }
        if (!bm_append_split_link_values(scratch, &items, item, resolved)) return false;
    }

    out->items = items;
    out->count = arena_arr_len(items);
    return true;
}

static bool bm_eval_link_item_span(const Build_Model *model,
                                   BM_Target_Id owner_target_id,
                                   const BM_Query_Eval_Context *ctx,
                                   Arena *scratch,
                                   BM_Link_Item_Span raw_items,
                                   BM_Link_Item_Span *out) {
    BM_Query_Target_Property_Stack stack = {0};
    return bm_eval_link_item_span_impl(model, owner_target_id, ctx, scratch, raw_items, &stack, out);
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

static bool bm_query_dedup_effective_link_items(Arena *scratch,
                                                BM_Link_Item_Span in,
                                                BM_Link_Item_Span *out) {
    BM_Link_Item_View *items = NULL;
    String_View *seen_keys = NULL;
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!scratch) return false;

    for (size_t i = 0; i < in.count; ++i) {
        BM_String_Item_View key_item = {
            .value = in.items[i].value,
            .visibility = in.items[i].visibility,
            .flags = in.items[i].flags,
            .provenance = in.items[i].provenance,
        };
        String_View key = {0};
        bool duplicate = false;
        if (!bm_query_effective_item_key(scratch, BM_EFFECTIVE_LINK_LIBRARIES, key_item, &key)) return false;
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
    BM_Link_Item_Span dependency_seeds = {0};
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

    if (!bm_collect_evaluated_root_link_library_seeds(model,
                                                      target,
                                                      ctx,
                                                      scratch,
                                                      &dependency_seeds) ||
        !bm_collect_dependency_usage_from_evaluated_link_items(model,
                                                               dependency_seeds,
                                                               ctx,
                                                               scratch,
                                                               visited,
                                                               &raw_items,
                                                               kind)) {
        return false;
    }
    if (!bm_eval_item_span(model,
                           id,
                           ctx,
                           scratch,
                           (BM_String_Item_Span){.items = raw_items, .count = arena_arr_len(raw_items)},
                           kind,
                           &evaluated)) {
        return false;
    }

    return bm_query_dedup_effective_items(scratch, kind, evaluated, out);
}

static bool bm_query_target_effective_link_items_common(const Build_Model *model,
                                                        BM_Target_Id id,
                                                        const BM_Query_Eval_Context *ctx,
                                                        Arena *scratch,
                                                        BM_Link_Item_Span *out) {
    const BM_Target_Record *target = bm_model_target(model, id);
    BM_Link_Item_View *raw_items = NULL;
    BM_Link_Item_Span dependency_seeds = {0};
    BM_Link_Item_Span evaluated = {0};
    uint8_t *visited = NULL;

    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!model || !target || !scratch) return false;

    if (!bm_collect_global_effective_link_items(model, scratch, &raw_items) ||
        !bm_collect_directory_chain_link_items(model, target->owner_directory_id, scratch, &raw_items) ||
        !bm_collect_target_link_items(scratch, &raw_items, target, false)) {
        return false;
    }

    visited = arena_alloc_array_zero(scratch, uint8_t, arena_arr_len(model->targets));
    if (!visited) return false;
    visited[id] = 1;

    if (!bm_collect_evaluated_root_link_library_seeds(model, target, ctx, scratch, &dependency_seeds) ||
        !bm_collect_link_dependency_usage_from_evaluated_link_items(model,
                                                                    dependency_seeds,
                                                                    ctx,
                                                                    scratch,
                                                                    visited,
                                                                    &raw_items)) {
        return false;
    }
    if (!bm_eval_link_item_span(model,
                                id,
                                ctx,
                                scratch,
                                (BM_Link_Item_Span){.items = raw_items, .count = arena_arr_len(raw_items)},
                                &evaluated)) {
        return false;
    }

    return bm_query_dedup_effective_link_items(scratch, evaluated, out);
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
                                                    BM_Link_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_libraries_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_libraries_items_with_context(const Build_Model *model,
                                                                 BM_Target_Id id,
                                                                 const BM_Query_Eval_Context *ctx,
                                                                 Arena *scratch,
                                                                 BM_Link_Item_Span *out) {
    return bm_query_target_effective_link_items_common(model, id, ctx, scratch, out);
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

bool bm_query_target_effective_compile_features_items(const Build_Model *model,
                                                      BM_Target_Id id,
                                                      const BM_Query_Eval_Context *ctx,
                                                      Arena *scratch,
                                                      BM_String_Item_Span *out) {
    BM_Query_Eval_Context normalized = ctx ? *ctx : bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_items_common(model, id, &normalized, scratch, out, BM_EFFECTIVE_COMPILE_FEATURES);
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
    BM_Link_Item_Span item_span = {0};
    String_View *values = NULL;
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!bm_query_target_effective_link_items_common(model, id, ctx, scratch, &item_span)) return false;
    for (size_t i = 0; i < item_span.count; ++i) {
        if (!arena_arr_push(scratch, values, item_span.items[i].value)) return false;
    }
    out->items = values;
    out->count = arena_arr_len(values);
    return true;
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
