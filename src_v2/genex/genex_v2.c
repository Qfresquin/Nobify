#include "genex_v2.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    String_View target_name;
    String_View property_name;
} Genex_V2_Target_Property_Stack_Entry;

typedef struct {
    Genex_V2_Target_Property_Stack_Entry entries[128];
    size_t count;
} Genex_V2_Target_Property_Stack;

static String_View gxv2_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena || !sv.data || sv.count == 0) return nob_sv_from_cstr("");
    char *dup = arena_strndup(arena, sv.data, sv.count);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
}

static String_View gxv2_copy_cstr_to_arena(Arena *arena, const char *s) {
    if (!arena || !s) return nob_sv_from_cstr("");
    size_t n = strlen(s);
    char *dup = arena_strndup(arena, s, n);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
}

static String_View gxv2_trim(String_View sv) {
    size_t b = 0;
    size_t e = sv.count;
    while (b < e && isspace((unsigned char)sv.data[b])) b++;
    while (e > b && isspace((unsigned char)sv.data[e - 1])) e--;
    return nob_sv_from_parts(sv.data + b, e - b);
}

static bool gxv2_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

static bool gxv2_sv_ends_with_ci(String_View sv, String_View suffix) {
    if (suffix.count > sv.count) return false;
    return gxv2_sv_eq_ci(nob_sv_from_parts(sv.data + sv.count - suffix.count, suffix.count), suffix);
}

static bool gxv2_sv_contains_genex(String_View input) {
    for (size_t i = 0; i + 1 < input.count; i++) {
        if (input.data[i] == '$' && input.data[i + 1] == '<') return true;
    }
    return false;
}

static bool gxv2_cmake_string_is_false(String_View value) {
    String_View v = gxv2_trim(value);
    if (v.count == 0) return true;
    if (nob_sv_eq(v, nob_sv_from_cstr("0"))) return true;
    if (gxv2_sv_eq_ci(v, nob_sv_from_cstr("FALSE"))) return true;
    if (gxv2_sv_eq_ci(v, nob_sv_from_cstr("OFF"))) return true;
    if (gxv2_sv_eq_ci(v, nob_sv_from_cstr("NO"))) return true;
    if (gxv2_sv_eq_ci(v, nob_sv_from_cstr("N"))) return true;
    if (gxv2_sv_eq_ci(v, nob_sv_from_cstr("IGNORE"))) return true;
    if (gxv2_sv_eq_ci(v, nob_sv_from_cstr("NOTFOUND"))) return true;
    if (gxv2_sv_ends_with_ci(v, nob_sv_from_cstr("-NOTFOUND"))) return true;
    return false;
}

static size_t gxv2_split_top_level(String_View input, char delimiter, String_View *out, size_t out_cap, bool *out_overflow) {
    size_t count = 0;
    size_t start = 0;
    size_t genex_depth = 0;

    if (out_overflow) *out_overflow = false;

    for (size_t i = 0; i <= input.count; i++) {
        bool at_end = (i == input.count);
        if (!at_end) {
            if (input.data[i] == '$' && (i + 1) < input.count && input.data[i + 1] == '<') {
                genex_depth++;
                i++;
                continue;
            }
            if (input.data[i] == '>' && genex_depth > 0) {
                genex_depth--;
                continue;
            }
        }
        if (!at_end && !(input.data[i] == delimiter && genex_depth == 0)) continue;

        if (count < out_cap) {
            out[count] = gxv2_trim(nob_sv_from_parts(input.data + start, i - start));
        } else if (out_overflow) {
            *out_overflow = true;
        }
        count++;
        start = i + 1;
    }

    return count;
}

static bool gxv2_find_top_level_colon(String_View body, size_t *out_colon) {
    if (!out_colon) return false;
    size_t genex_depth = 0;
    for (size_t i = 0; i < body.count; i++) {
        if (body.data[i] == '$' && (i + 1) < body.count && body.data[i + 1] == '<') {
            genex_depth++;
            i++;
            continue;
        }
        if (body.data[i] == '>' && genex_depth > 0) {
            genex_depth--;
            continue;
        }
        if (body.data[i] == ':' && genex_depth == 0) {
            *out_colon = i;
            return true;
        }
    }
    return false;
}

static bool gxv2_find_matching_genex_end(String_View input, size_t start_dollar, size_t *out_end) {
    if (!out_end || start_dollar + 1 >= input.count) return false;
    if (input.data[start_dollar] != '$' || input.data[start_dollar + 1] != '<') return false;

    size_t depth = 1;
    for (size_t i = start_dollar + 2; i < input.count; i++) {
        if (input.data[i] == '$' && (i + 1) < input.count && input.data[i + 1] == '<') {
            depth++;
            i++;
            continue;
        }
        if (input.data[i] == '>') {
            if (depth == 0) return false;
            depth--;
            if (depth == 0) {
                *out_end = i;
                return true;
            }
        }
    }
    return false;
}

static bool gxv2_tp_stack_contains(const Genex_V2_Target_Property_Stack *stack,
                                   String_View target_name,
                                   String_View property_name) {
    if (!stack) return false;
    for (size_t i = 0; i < stack->count; i++) {
        if (nob_sv_eq(stack->entries[i].target_name, target_name) &&
            gxv2_sv_eq_ci(stack->entries[i].property_name, property_name)) {
            return true;
        }
    }
    return false;
}

static bool gxv2_tp_stack_push(Genex_V2_Target_Property_Stack *stack, String_View target_name, String_View property_name) {
    if (!stack || stack->count >= (sizeof(stack->entries) / sizeof(stack->entries[0]))) return false;
    stack->entries[stack->count].target_name = target_name;
    stack->entries[stack->count].property_name = property_name;
    stack->count++;
    return true;
}

static void gxv2_tp_stack_pop(Genex_V2_Target_Property_Stack *stack) {
    if (!stack || stack->count == 0) return;
    stack->count--;
}

static Genex_V2_Result gxv2_result(Genex_V2_Status status, String_View value, String_View diag_message) {
    Genex_V2_Result out = {0};
    out.status = status;
    out.value = value;
    out.diag_message = diag_message;
    return out;
}

static Genex_V2_Result gxv2_eval_inner(const Genex_V2_Context *ctx,
                                       String_View input,
                                       size_t depth,
                                       Genex_V2_Target_Property_Stack *stack);

static Genex_V2_Result gxv2_eval_body(const Genex_V2_Context *ctx,
                                      String_View body,
                                      String_View raw_expr,
                                      size_t depth,
                                      Genex_V2_Target_Property_Stack *stack) {
    size_t colon = 0;
    String_View op = body;
    String_View args_expr = nob_sv_from_cstr("");
    if (gxv2_find_top_level_colon(body, &colon)) {
        op = nob_sv_from_parts(body.data, colon);
        args_expr = nob_sv_from_parts(body.data + colon + 1, body.count - (colon + 1));
    }
    op = gxv2_trim(op);

    if (gxv2_sv_eq_ci(op, nob_sv_from_cstr("CONFIG"))) {
        if (args_expr.count == 0) {
            return gxv2_result(GENEX_V2_OK, ctx->config, nob_sv_from_cstr(""));
        }
        String_View args[32] = {0};
        bool overflow = false;
        size_t argc = gxv2_split_top_level(args_expr, ',', args, 32, &overflow);
        if (overflow) {
            return gxv2_result(GENEX_V2_ERROR, raw_expr, gxv2_copy_cstr_to_arena(ctx->arena, "Too many CONFIG arguments"));
        }
        for (size_t i = 0; i < argc && i < 32; i++) {
            Genex_V2_Result arg_eval = gxv2_eval_inner(ctx, args[i], depth + 1, stack);
            if (arg_eval.status != GENEX_V2_OK) return gxv2_result(arg_eval.status, raw_expr, arg_eval.diag_message);
            String_View entry = arg_eval.value;
            size_t start = 0;
            for (size_t k = 0; k <= entry.count; k++) {
                bool sep = (k == entry.count) || (entry.data[k] == ';');
                if (!sep) continue;
                String_View candidate = gxv2_trim(nob_sv_from_parts(entry.data + start, k - start));
                if (candidate.count > 0 && gxv2_sv_eq_ci(candidate, ctx->config)) {
                    return gxv2_result(GENEX_V2_OK, nob_sv_from_cstr("1"), nob_sv_from_cstr(""));
                }
                start = k + 1;
            }
        }
        return gxv2_result(GENEX_V2_OK, nob_sv_from_cstr("0"), nob_sv_from_cstr(""));
    }

    if (gxv2_sv_eq_ci(op, nob_sv_from_cstr("BOOL"))) {
        Genex_V2_Result arg_eval = gxv2_eval_inner(ctx, args_expr, depth + 1, stack);
        if (arg_eval.status != GENEX_V2_OK) return gxv2_result(arg_eval.status, raw_expr, arg_eval.diag_message);
        return gxv2_result(GENEX_V2_OK,
                           gxv2_cmake_string_is_false(arg_eval.value) ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"),
                           nob_sv_from_cstr(""));
    }

    if (gxv2_sv_eq_ci(op, nob_sv_from_cstr("IF"))) {
        String_View args[3] = {0};
        bool overflow = false;
        size_t argc = gxv2_split_top_level(args_expr, ',', args, 3, &overflow);
        if (overflow || argc != 3) {
            return gxv2_result(GENEX_V2_ERROR, raw_expr, gxv2_copy_cstr_to_arena(ctx->arena, "IF expects 3 arguments"));
        }
        Genex_V2_Result cond_eval = gxv2_eval_inner(ctx, args[0], depth + 1, stack);
        if (cond_eval.status != GENEX_V2_OK) return gxv2_result(cond_eval.status, raw_expr, cond_eval.diag_message);
        bool cond = !gxv2_cmake_string_is_false(cond_eval.value);
        Genex_V2_Result branch_eval = gxv2_eval_inner(ctx, cond ? args[1] : args[2], depth + 1, stack);
        if (branch_eval.status != GENEX_V2_OK) return gxv2_result(branch_eval.status, raw_expr, branch_eval.diag_message);
        return gxv2_result(GENEX_V2_OK, branch_eval.value, nob_sv_from_cstr(""));
    }

    if (gxv2_sv_eq_ci(op, nob_sv_from_cstr("TARGET_PROPERTY"))) {
        String_View args[2] = {0};
        bool overflow = false;
        size_t argc = gxv2_split_top_level(args_expr, ',', args, 2, &overflow);
        if (overflow || argc != 2) {
            return gxv2_result(GENEX_V2_ERROR, raw_expr, gxv2_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY expects target and property"));
        }
        Genex_V2_Result target_eval = gxv2_eval_inner(ctx, args[0], depth + 1, stack);
        if (target_eval.status != GENEX_V2_OK) return gxv2_result(target_eval.status, raw_expr, target_eval.diag_message);
        Genex_V2_Result prop_eval = gxv2_eval_inner(ctx, args[1], depth + 1, stack);
        if (prop_eval.status != GENEX_V2_OK) return gxv2_result(prop_eval.status, raw_expr, prop_eval.diag_message);

        String_View target_name = gxv2_trim(target_eval.value);
        String_View property_name = gxv2_trim(prop_eval.value);
        if (target_name.count == 0 || property_name.count == 0) {
            return gxv2_result(GENEX_V2_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        }
        if (!ctx->read_target_property) {
            return gxv2_result(GENEX_V2_ERROR, raw_expr, gxv2_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY callback is not configured"));
        }
        if (stack->count >= ctx->max_target_property_depth) {
            return gxv2_result(GENEX_V2_CYCLE_GUARD_HIT, raw_expr, gxv2_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY depth guard reached"));
        }
        if (gxv2_tp_stack_contains(stack, target_name, property_name)) {
            return gxv2_result(GENEX_V2_CYCLE_GUARD_HIT, raw_expr, gxv2_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY cycle detected"));
        }
        if (!gxv2_tp_stack_push(stack, target_name, property_name)) {
            return gxv2_result(GENEX_V2_CYCLE_GUARD_HIT, raw_expr, gxv2_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY stack overflow"));
        }

        String_View raw_value = ctx->read_target_property(ctx->userdata, target_name, property_name);
        Genex_V2_Result nested = gxv2_eval_inner(ctx, raw_value, depth + 1, stack);
        gxv2_tp_stack_pop(stack);
        if (nested.status != GENEX_V2_OK) return gxv2_result(nested.status, raw_expr, nested.diag_message);
        return gxv2_result(GENEX_V2_OK, nested.value, nob_sv_from_cstr(""));
    }

    if (args_expr.count == 0 &&
        op.count >= 3 &&
        op.data[0] == '$' &&
        op.data[1] == '<' &&
        op.data[op.count - 1] == '>') {
        Genex_V2_Result nested = gxv2_eval_inner(ctx, op, depth + 1, stack);
        if (nested.status != GENEX_V2_OK) return gxv2_result(nested.status, raw_expr, nested.diag_message);
        return gxv2_result(GENEX_V2_OK, nested.value, nob_sv_from_cstr(""));
    }

    if (args_expr.count > 0 &&
        (gxv2_sv_contains_genex(op) ||
         nob_sv_eq(op, nob_sv_from_cstr("0")) ||
         nob_sv_eq(op, nob_sv_from_cstr("1")))) {
        Genex_V2_Result cond_eval = gxv2_eval_inner(ctx, op, depth + 1, stack);
        if (cond_eval.status != GENEX_V2_OK) return gxv2_result(cond_eval.status, raw_expr, cond_eval.diag_message);
        if (gxv2_cmake_string_is_false(cond_eval.value)) {
            return gxv2_result(GENEX_V2_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        }
        Genex_V2_Result value_eval = gxv2_eval_inner(ctx, args_expr, depth + 1, stack);
        if (value_eval.status != GENEX_V2_OK) return gxv2_result(value_eval.status, raw_expr, value_eval.diag_message);
        return gxv2_result(GENEX_V2_OK, value_eval.value, nob_sv_from_cstr(""));
    }

    return gxv2_result(GENEX_V2_UNSUPPORTED, raw_expr, gxv2_copy_cstr_to_arena(ctx->arena, "Unsupported generator expression operator"));
}

static Genex_V2_Result gxv2_eval_inner(const Genex_V2_Context *ctx,
                                       String_View input,
                                       size_t depth,
                                       Genex_V2_Target_Property_Stack *stack) {
    if (!ctx || !ctx->arena) return gxv2_result(GENEX_V2_ERROR, input, nob_sv_from_cstr("Invalid genex context"));
    if (depth > ctx->max_depth) {
        return gxv2_result(GENEX_V2_ERROR, input, gxv2_copy_cstr_to_arena(ctx->arena, "Generator expression max depth exceeded"));
    }
    if (input.count == 0 || !gxv2_sv_contains_genex(input)) {
        return gxv2_result(GENEX_V2_OK, input, nob_sv_from_cstr(""));
    }

    String_Builder sb = {0};
    size_t cursor = 0;
    while (cursor < input.count) {
        size_t open = cursor;
        bool found = false;
        for (; open + 1 < input.count; open++) {
            if (input.data[open] == '$' && input.data[open + 1] == '<') {
                found = true;
                break;
            }
        }
        if (!found) {
            nob_sb_append_buf(&sb, input.data + cursor, input.count - cursor);
            break;
        }

        if (open > cursor) {
            nob_sb_append_buf(&sb, input.data + cursor, open - cursor);
        }

        size_t close = 0;
        if (!gxv2_find_matching_genex_end(input, open, &close)) {
            nob_sb_free(sb);
            return gxv2_result(GENEX_V2_ERROR, input, gxv2_copy_cstr_to_arena(ctx->arena, "Unclosed generator expression"));
        }

        String_View body = nob_sv_from_parts(input.data + open + 2, close - (open + 2));
        String_View raw_expr = nob_sv_from_parts(input.data + open, close - open + 1);
        Genex_V2_Result part = gxv2_eval_body(ctx, body, raw_expr, depth, stack);
        if (part.status != GENEX_V2_OK) {
            nob_sb_free(sb);
            return gxv2_result(part.status, input, part.diag_message);
        }
        if (part.value.count > 0) {
            nob_sb_append_buf(&sb, part.value.data, part.value.count);
        }
        cursor = close + 1;
    }

    String_View out = nob_sv_from_cstr("");
    if (sb.count > 0) {
        out = gxv2_copy_to_arena(ctx->arena, nob_sv_from_parts(sb.items, sb.count));
    }
    nob_sb_free(sb);
    return gxv2_result(GENEX_V2_OK, out, nob_sv_from_cstr(""));
}

Genex_V2_Result genex_v2_eval(const Genex_V2_Context *ctx, String_View input) {
    if (!ctx || !ctx->arena) {
        return gxv2_result(GENEX_V2_ERROR, input, nob_sv_from_cstr("Invalid genex context"));
    }

    Genex_V2_Context local = *ctx;
    if (local.max_depth == 0) local.max_depth = 64;
    if (local.max_target_property_depth == 0) local.max_target_property_depth = 64;

    Genex_V2_Target_Property_Stack stack = {0};
    return gxv2_eval_inner(&local, input, 0, &stack);
}
