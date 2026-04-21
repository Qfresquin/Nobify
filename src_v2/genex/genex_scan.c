#include "arena_dyn.h"
#include "genex_internal.h"

static bool gx_known_configs_push_unique_ci(Arena *arena, String_View **configs, String_View config) {
    String_View copy = {0};
    if (!arena || !configs) return false;
    config = gx_trim(config);
    if (config.count == 0) return true;
    for (size_t i = 0; i < arena_arr_len(*configs); ++i) {
        if (gx_sv_eq_ci((*configs)[i], config)) return true;
    }
    copy = gx_copy_to_arena(arena, config);
    if (config.count > 0 && copy.count != config.count) return false;
    return arena_arr_push(arena, *configs, copy);
}

static bool gx_known_configs_push_literal_list(Arena *arena, String_View **configs, String_View value) {
    size_t start = 0;
    value = gx_trim(value);
    for (size_t i = 0; i <= value.count; ++i) {
        bool at_end = (i == value.count);
        if (!at_end && value.data[i] != ';') continue;
        if (!gx_known_configs_push_unique_ci(arena,
                                             configs,
                                             nob_sv_from_parts(value.data + start, i - start))) {
            return false;
        }
        start = i + 1;
    }
    return true;
}

static bool gx_known_configs_collect_inner(const Genex_Context *ctx,
                                           String_View input,
                                           String_View **configs);

static bool gx_parse_exact_expr(String_View expr, String_View *out_op, String_View *out_args_expr) {
    size_t end = 0;
    String_View body = {0};
    String_View op = {0};
    String_View args_expr = nob_sv_from_cstr("");
    expr = gx_trim(expr);
    if (!gx_is_genex_open_at(expr, 0)) return false;
    if (!gx_find_matching_genex_end(expr, 0, &end) || end + 1 != expr.count) return false;
    body = nob_sv_from_parts(expr.data + 2, end - 2);
    op = body;
    if (gx_find_top_level_colon(body, &end)) {
        op = nob_sv_from_parts(body.data, end);
        args_expr = nob_sv_from_parts(body.data + end + 1, body.count - (end + 1));
    }
    if (out_op) *out_op = gx_trim(op);
    if (out_args_expr) *out_args_expr = args_expr;
    return true;
}

static bool gx_is_exact_config_value_expr(String_View expr) {
    String_View op = {0};
    String_View args_expr = {0};
    if (!gx_parse_exact_expr(expr, &op, &args_expr)) return false;
    return gx_sv_eq_ci(op, nob_sv_from_cstr("CONFIG")) && gx_trim(args_expr).count == 0;
}

static bool gx_known_configs_collect_body(const Genex_Context *ctx,
                                          String_View body,
                                          String_View **configs) {
    size_t colon = 0;
    String_View op = body;
    String_View args_expr = nob_sv_from_cstr("");

    if (gx_find_top_level_colon(body, &colon)) {
        op = nob_sv_from_parts(body.data, colon);
        args_expr = nob_sv_from_parts(body.data + colon + 1, body.count - (colon + 1));
    }
    op = gx_trim(op);

    if (gx_sv_contains_genex_unescaped(op) &&
        !gx_known_configs_collect_inner(ctx, op, configs)) {
        return false;
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("CONFIG"))) {
        Gx_Sv_List args = {0};
        if (args_expr.count == 0) return true;
        args = gx_split_top_level_alloc(ctx, args_expr, ',');
        if (args_expr.count > 0 && args.count == 0) return false;
        for (size_t i = 0; i < args.count; ++i) {
            if (!gx_known_configs_collect_inner(ctx, args.items[i], configs)) return false;
            if (!gx_sv_contains_genex_unescaped(args.items[i]) &&
                !gx_known_configs_push_literal_list(ctx->arena, configs, args.items[i])) {
                return false;
            }
        }
        return true;
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("STREQUAL"))) {
        Gx_Sv_List args = gx_split_top_level_alloc(ctx, args_expr, ',');
        if (args_expr.count > 0 && args.count == 0) return false;
        for (size_t i = 0; i < args.count; ++i) {
            if (!gx_known_configs_collect_inner(ctx, args.items[i], configs)) return false;
        }
        if (args.count == 2) {
            size_t literal_index = 0;
            bool lhs_is_config = gx_is_exact_config_value_expr(args.items[0]);
            bool rhs_is_config = gx_is_exact_config_value_expr(args.items[1]);
            if (lhs_is_config != rhs_is_config) {
                literal_index = lhs_is_config ? 1 : 0;
                if (!gx_sv_contains_genex_unescaped(args.items[literal_index]) &&
                    !gx_known_configs_push_unique_ci(ctx->arena, configs, args.items[literal_index])) {
                    return false;
                }
            }
        }
        return true;
    }

    if (args_expr.count > 0) return gx_known_configs_collect_inner(ctx, args_expr, configs);
    return true;
}

static bool gx_known_configs_collect_inner(const Genex_Context *ctx,
                                           String_View input,
                                           String_View **configs) {
    if (!ctx || !ctx->arena || !configs) return false;
    for (size_t i = 0; i + 1 < input.count; ++i) {
        size_t end = 0;
        String_View body = {0};
        if (!gx_is_genex_open_at(input, i)) continue;
        if (!gx_find_matching_genex_end(input, i, &end)) continue;
        body = nob_sv_from_parts(input.data + i + 2, end - (i + 2));
        if (!gx_known_configs_collect_body(ctx, body, configs)) return false;
        i = end;
    }
    return true;
}

bool genex_collect_known_configs(const Genex_Context *ctx,
                                 String_View input,
                                 String_View **out_configs) {
    if (!ctx || !ctx->arena || !out_configs) return false;
    return gx_known_configs_collect_inner(ctx, input, out_configs);
}
