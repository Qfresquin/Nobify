#include "genex.h"

#include <ctype.h>
#include <string.h>

#define GX_DEFAULT_MAX_CALLBACK_VALUE_LEN (1024 * 1024)

typedef struct {
    String_View target_name;
    String_View property_name;
} Genex_Target_Property_Stack_Entry;

typedef struct {
    Genex_Target_Property_Stack_Entry entries[128];
    size_t count;
} Genex_Target_Property_Stack;

typedef struct {
    String_View *items;
    size_t count;
} Gx_Sv_List;

static String_View gx_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena || !sv.data || sv.count == 0) return nob_sv_from_cstr("");
    char *dup = arena_strndup(arena, sv.data, sv.count);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
}

static String_View gx_copy_cstr_to_arena(Arena *arena, const char *s) {
    if (!arena || !s) return nob_sv_from_cstr("");
    size_t n = strlen(s);
    char *dup = arena_strndup(arena, s, n);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
}

static String_View gx_trim(String_View sv) {
    size_t b = 0;
    size_t e = sv.count;
    while (b < e && isspace((unsigned char)sv.data[b])) b++;
    while (e > b && isspace((unsigned char)sv.data[e - 1])) e--;
    return nob_sv_from_parts(sv.data + b, e - b);
}

static bool gx_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

static bool gx_sv_ends_with_ci(String_View sv, String_View suffix) {
    if (suffix.count > sv.count) return false;
    return gx_sv_eq_ci(nob_sv_from_parts(sv.data + sv.count - suffix.count, suffix.count), suffix);
}

static bool gx_is_escaped(String_View input, size_t idx) {
    if (idx == 0 || idx > input.count) return false;
    size_t bs = 0;
    for (size_t i = idx; i-- > 0;) {
        if (input.data[i] != '\\') break;
        bs++;
    }
    return (bs % 2) != 0;
}

static bool gx_is_genex_open_at(String_View input, size_t i) {
    if (i + 1 >= input.count) return false;
    if (input.data[i] != '$' || input.data[i + 1] != '<') return false;
    return !gx_is_escaped(input, i);
}

static bool gx_is_unescaped_char_at(String_View input, size_t i, char ch) {
    if (i >= input.count || input.data[i] != ch) return false;
    return !gx_is_escaped(input, i);
}

static bool gx_sv_contains_genex_unescaped(String_View input) {
    for (size_t i = 0; i + 1 < input.count; i++) {
        if (gx_is_genex_open_at(input, i)) return true;
    }
    return false;
}

static bool gx_cmake_string_is_false(String_View value) {
    String_View v = gx_trim(value);
    if (v.count == 0) return true;
    if (nob_sv_eq(v, nob_sv_from_cstr("0"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("FALSE"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("OFF"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("NO"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("N"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("IGNORE"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("NOTFOUND"))) return true;
    if (gx_sv_ends_with_ci(v, nob_sv_from_cstr("-NOTFOUND"))) return true;
    return false;
}

static bool gx_list_matches_value_ci(String_View entry_list, String_View needle) {
    size_t start = 0;
    for (size_t i = 0; i <= entry_list.count; i++) {
        bool sep = (i == entry_list.count) || (entry_list.data[i] == ';');
        if (!sep) continue;
        String_View candidate = gx_trim(nob_sv_from_parts(entry_list.data + start, i - start));
        if (candidate.count > 0 && gx_sv_eq_ci(candidate, needle)) return true;
        start = i + 1;
    }
    return false;
}

static String_View gx_path_dirname(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c == '/' || c == '\\') {
            if (i == 0) return nob_sv_from_parts(path.data, 1);
            return nob_sv_from_parts(path.data, i);
        }
    }
    return nob_sv_from_cstr("");
}

static String_View gx_path_basename(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    size_t off = 0;
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c == '/' || c == '\\') {
            off = i + 1;
            break;
        }
    }
    return nob_sv_from_parts(path.data + off, path.count - off);
}

static Gx_Sv_List gx_split_top_level_alloc(const Genex_Context *ctx, String_View input, char delimiter) {
    Gx_Sv_List out = {0};
    if (!ctx || !ctx->arena) return out;

    size_t count = 0;
    size_t start = 0;
    size_t genex_depth = 0;

    for (size_t i = 0; i <= input.count; i++) {
        bool at_end = (i == input.count);
        if (!at_end) {
            if (gx_is_genex_open_at(input, i)) {
                genex_depth++;
                i++;
                continue;
            }
            if (gx_is_unescaped_char_at(input, i, '>') && genex_depth > 0) {
                genex_depth--;
                continue;
            }
        }
        if (!at_end && !(gx_is_unescaped_char_at(input, i, delimiter) && genex_depth == 0)) continue;
        count++;
        start = i + 1;
    }
    (void)start;

    if (count == 0) return out;
    String_View *items = (String_View*)arena_alloc(ctx->arena, sizeof(String_View) * count);
    if (!items) return out;

    size_t idx = 0;
    start = 0;
    genex_depth = 0;
    for (size_t i = 0; i <= input.count; i++) {
        bool at_end = (i == input.count);
        if (!at_end) {
            if (gx_is_genex_open_at(input, i)) {
                genex_depth++;
                i++;
                continue;
            }
            if (gx_is_unescaped_char_at(input, i, '>') && genex_depth > 0) {
                genex_depth--;
                continue;
            }
        }
        if (!at_end && !(gx_is_unescaped_char_at(input, i, delimiter) && genex_depth == 0)) continue;
        if (idx < count) {
            items[idx++] = gx_trim(nob_sv_from_parts(input.data + start, i - start));
        }
        start = i + 1;
    }

    out.items = items;
    out.count = idx;
    return out;
}

static bool gx_find_top_level_colon(String_View body, size_t *out_colon) {
    if (!out_colon) return false;
    size_t genex_depth = 0;
    for (size_t i = 0; i < body.count; i++) {
        if (gx_is_genex_open_at(body, i)) {
            genex_depth++;
            i++;
            continue;
        }
        if (gx_is_unescaped_char_at(body, i, '>') && genex_depth > 0) {
            genex_depth--;
            continue;
        }
        if (gx_is_unescaped_char_at(body, i, ':') && genex_depth == 0) {
            *out_colon = i;
            return true;
        }
    }
    return false;
}

static bool gx_find_matching_genex_end(String_View input, size_t start_dollar, size_t *out_end) {
    if (!out_end || start_dollar + 1 >= input.count) return false;
    if (!gx_is_genex_open_at(input, start_dollar)) return false;

    size_t depth = 1;
    for (size_t i = start_dollar + 2; i < input.count; i++) {
        if (gx_is_genex_open_at(input, i)) {
            depth++;
            i++;
            continue;
        }
        if (gx_is_unescaped_char_at(input, i, '>')) {
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

static bool gx_target_name_eq(const Genex_Context *ctx, String_View a, String_View b) {
    if (ctx && ctx->target_name_case_insensitive) return gx_sv_eq_ci(a, b);
    return nob_sv_eq(a, b);
}

static bool gx_tp_stack_contains(const Genex_Context *ctx,
                                 const Genex_Target_Property_Stack *stack,
                                 String_View target_name,
                                 String_View property_name) {
    if (!stack) return false;
    for (size_t i = 0; i < stack->count; i++) {
        if (gx_target_name_eq(ctx, stack->entries[i].target_name, target_name) &&
            gx_sv_eq_ci(stack->entries[i].property_name, property_name)) {
            return true;
        }
    }
    return false;
}

static bool gx_tp_stack_push(Genex_Target_Property_Stack *stack, String_View target_name, String_View property_name) {
    if (!stack || stack->count >= (sizeof(stack->entries) / sizeof(stack->entries[0]))) return false;
    stack->entries[stack->count].target_name = target_name;
    stack->entries[stack->count].property_name = property_name;
    stack->count++;
    return true;
}

static void gx_tp_stack_pop(Genex_Target_Property_Stack *stack) {
    if (!stack || stack->count == 0) return;
    stack->count--;
}

static Genex_Result gx_result(Genex_Status status, String_View value, String_View diag_message) {
    Genex_Result out = {0};
    out.status = status;
    out.value = value;
    out.diag_message = diag_message;
    return out;
}

static size_t gx_max_callback_value_len(const Genex_Context *ctx) {
    if (!ctx || ctx->max_callback_value_len == 0) return GX_DEFAULT_MAX_CALLBACK_VALUE_LEN;
    return ctx->max_callback_value_len;
}

static Genex_Result gx_validate_callback_value(const Genex_Context *ctx,
                                               String_View raw_value,
                                               String_View raw_expr,
                                               const char *which_callback) {
    if (raw_value.count > 0 && raw_value.data == NULL) {
        return gx_result(GENEX_ERROR,
                         raw_expr,
                         gx_copy_cstr_to_arena(ctx->arena, "Callback returned invalid String_View (NULL data with non-zero length)"));
    }
    if (raw_value.count > gx_max_callback_value_len(ctx)) {
        return gx_result(GENEX_ERROR,
                         raw_expr,
                         gx_copy_cstr_to_arena(ctx->arena, which_callback ? which_callback : "Callback returned value larger than allowed limit"));
    }
    return gx_result(GENEX_OK, raw_value, nob_sv_from_cstr(""));
}

static Genex_Result gx_eval_inner(const Genex_Context *ctx,
                                       String_View input,
                                       size_t depth,
                                       Genex_Target_Property_Stack *stack);

static Genex_Result gx_eval_arg_fast(const Genex_Context *ctx,
                                     String_View arg,
                                     size_t depth,
                                     Genex_Target_Property_Stack *stack) {
    String_View trimmed = gx_trim(arg);
    if (!gx_sv_contains_genex_unescaped(trimmed)) {
        return gx_result(GENEX_OK, trimmed, nob_sv_from_cstr(""));
    }
    return gx_eval_inner(ctx, trimmed, depth + 1, stack);
}

static Genex_Result gx_eval_body(const Genex_Context *ctx,
                                      String_View body,
                                      String_View raw_expr,
                                      size_t depth,
                                      Genex_Target_Property_Stack *stack) {
    size_t colon = 0;
    String_View op = body;
    String_View args_expr = nob_sv_from_cstr("");
    if (gx_find_top_level_colon(body, &colon)) {
        op = nob_sv_from_parts(body.data, colon);
        args_expr = nob_sv_from_parts(body.data + colon + 1, body.count - (colon + 1));
    }
    op = gx_trim(op);

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("CONFIG"))) {
        if (args_expr.count == 0) {
            return gx_result(GENEX_OK, ctx->config, nob_sv_from_cstr(""));
        }
        Gx_Sv_List args = gx_split_top_level_alloc(ctx, args_expr, ',');
        if (args_expr.count > 0 && args.count == 0) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "Out of memory while splitting CONFIG arguments"));
        }
        for (size_t i = 0; i < args.count; i++) {
            Genex_Result arg_eval = gx_eval_arg_fast(ctx, args.items[i], depth, stack);
            if (arg_eval.status != GENEX_OK) return gx_result(arg_eval.status, raw_expr, arg_eval.diag_message);
            String_View entry = arg_eval.value;
            size_t start = 0;
            for (size_t k = 0; k <= entry.count; k++) {
                bool sep = (k == entry.count) || (entry.data[k] == ';');
                if (!sep) continue;
                String_View candidate = gx_trim(nob_sv_from_parts(entry.data + start, k - start));
                if (candidate.count > 0 && gx_sv_eq_ci(candidate, ctx->config)) {
                    return gx_result(GENEX_OK, nob_sv_from_cstr("1"), nob_sv_from_cstr(""));
                }
                start = k + 1;
            }
        }
        return gx_result(GENEX_OK, nob_sv_from_cstr("0"), nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("PLATFORM_ID"))) {
        if (args_expr.count == 0) {
            return gx_result(GENEX_OK, ctx->platform_id, nob_sv_from_cstr(""));
        }
        Gx_Sv_List args = gx_split_top_level_alloc(ctx, args_expr, ',');
        if (args_expr.count > 0 && args.count == 0) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "Out of memory while splitting PLATFORM_ID arguments"));
        }
        for (size_t i = 0; i < args.count; i++) {
            Genex_Result arg_eval = gx_eval_arg_fast(ctx, args.items[i], depth, stack);
            if (arg_eval.status != GENEX_OK) return gx_result(arg_eval.status, raw_expr, arg_eval.diag_message);
            if (gx_list_matches_value_ci(arg_eval.value, ctx->platform_id)) {
                return gx_result(GENEX_OK, nob_sv_from_cstr("1"), nob_sv_from_cstr(""));
            }
        }
        return gx_result(GENEX_OK, nob_sv_from_cstr("0"), nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("COMPILE_LANGUAGE"))) {
        String_View lang = gx_trim(ctx->compile_language);
        if (lang.count == 0 || args_expr.count == 0) {
            return gx_result(GENEX_OK, nob_sv_from_cstr("0"), nob_sv_from_cstr(""));
        }
        Gx_Sv_List args = gx_split_top_level_alloc(ctx, args_expr, ',');
        if (args_expr.count > 0 && args.count == 0) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "Out of memory while splitting COMPILE_LANGUAGE arguments"));
        }
        for (size_t i = 0; i < args.count; i++) {
            Genex_Result arg_eval = gx_eval_arg_fast(ctx, args.items[i], depth, stack);
            if (arg_eval.status != GENEX_OK) return gx_result(arg_eval.status, raw_expr, arg_eval.diag_message);
            if (gx_list_matches_value_ci(arg_eval.value, lang)) {
                return gx_result(GENEX_OK, nob_sv_from_cstr("1"), nob_sv_from_cstr(""));
            }
        }
        return gx_result(GENEX_OK, nob_sv_from_cstr("0"), nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("BUILD_INTERFACE"))) {
        if (!ctx->build_interface_active) return gx_result(GENEX_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        Genex_Result val = gx_eval_inner(ctx, args_expr, depth + 1, stack);
        if (val.status != GENEX_OK) return gx_result(val.status, raw_expr, val.diag_message);
        return gx_result(GENEX_OK, val.value, nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("INSTALL_INTERFACE"))) {
        if (!ctx->install_interface_active) return gx_result(GENEX_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        Genex_Result val = gx_eval_inner(ctx, args_expr, depth + 1, stack);
        if (val.status != GENEX_OK) return gx_result(val.status, raw_expr, val.diag_message);
        return gx_result(GENEX_OK, val.value, nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("LINK_ONLY"))) {
        if (!ctx->link_only_active) return gx_result(GENEX_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        Genex_Result val = gx_eval_inner(ctx, args_expr, depth + 1, stack);
        if (val.status != GENEX_OK) return gx_result(val.status, raw_expr, val.diag_message);
        return gx_result(GENEX_OK, val.value, nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_FILE"))) {
        if (!ctx->read_target_file) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_FILE callback is not configured"));
        }
        Genex_Result target_eval = gx_eval_inner(ctx, args_expr, depth + 1, stack);
        if (target_eval.status != GENEX_OK) return gx_result(target_eval.status, raw_expr, target_eval.diag_message);
        String_View target_name = gx_trim(target_eval.value);
        if (target_name.count == 0) return gx_result(GENEX_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        String_View raw_path = ctx->read_target_file(ctx->userdata, target_name);
        Genex_Result valid = gx_validate_callback_value(ctx, raw_path, raw_expr, "TARGET_FILE callback returned an invalid or too large value");
        if (valid.status != GENEX_OK) return valid;
        return gx_result(GENEX_OK, valid.value, nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_FILE_DIR")) ||
        gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_FILE_NAME"))) {
        if (!ctx->read_target_file) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_FILE_* callback is not configured"));
        }
        Genex_Result target_eval = gx_eval_inner(ctx, args_expr, depth + 1, stack);
        if (target_eval.status != GENEX_OK) return gx_result(target_eval.status, raw_expr, target_eval.diag_message);
        String_View target_name = gx_trim(target_eval.value);
        if (target_name.count == 0) return gx_result(GENEX_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        String_View path = ctx->read_target_file(ctx->userdata, target_name);
        Genex_Result valid = gx_validate_callback_value(ctx, path, raw_expr, "TARGET_FILE callback returned an invalid or too large value");
        if (valid.status != GENEX_OK) return valid;
        path = valid.value;
        if (gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_FILE_DIR"))) return gx_result(GENEX_OK, gx_path_dirname(path), nob_sv_from_cstr(""));
        return gx_result(GENEX_OK, gx_path_basename(path), nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_LINKER_FILE")) ||
        gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_LINKER_FILE_DIR")) ||
        gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_LINKER_FILE_NAME"))) {
        if (!ctx->read_target_linker_file && !ctx->read_target_file) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_LINKER_FILE callback is not configured"));
        }
        Genex_Result target_eval = gx_eval_inner(ctx, args_expr, depth + 1, stack);
        if (target_eval.status != GENEX_OK) return gx_result(target_eval.status, raw_expr, target_eval.diag_message);
        String_View target_name = gx_trim(target_eval.value);
        if (target_name.count == 0) return gx_result(GENEX_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        String_View path = ctx->read_target_linker_file
            ? ctx->read_target_linker_file(ctx->userdata, target_name)
            : ctx->read_target_file(ctx->userdata, target_name);
        Genex_Result valid = gx_validate_callback_value(ctx, path, raw_expr, "TARGET_LINKER_FILE callback returned an invalid or too large value");
        if (valid.status != GENEX_OK) return valid;
        path = valid.value;
        if (gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_LINKER_FILE"))) return gx_result(GENEX_OK, path, nob_sv_from_cstr(""));
        if (gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_LINKER_FILE_DIR"))) return gx_result(GENEX_OK, gx_path_dirname(path), nob_sv_from_cstr(""));
        return gx_result(GENEX_OK, gx_path_basename(path), nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("BOOL"))) {
        Genex_Result arg_eval = gx_eval_inner(ctx, args_expr, depth + 1, stack);
        if (arg_eval.status != GENEX_OK) return gx_result(arg_eval.status, raw_expr, arg_eval.diag_message);
        return gx_result(GENEX_OK,
                           gx_cmake_string_is_false(arg_eval.value) ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"),
                           nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("IF"))) {
        Gx_Sv_List args = gx_split_top_level_alloc(ctx, args_expr, ',');
        if (args_expr.count > 0 && args.count == 0) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "Out of memory while splitting IF arguments"));
        }
        if (args.count != 3) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "IF expects 3 arguments"));
        }
        Genex_Result cond_eval = gx_eval_inner(ctx, args.items[0], depth + 1, stack);
        if (cond_eval.status != GENEX_OK) return gx_result(cond_eval.status, raw_expr, cond_eval.diag_message);
        bool cond = !gx_cmake_string_is_false(cond_eval.value);
        Genex_Result branch_eval = gx_eval_inner(ctx, cond ? args.items[1] : args.items[2], depth + 1, stack);
        if (branch_eval.status != GENEX_OK) return gx_result(branch_eval.status, raw_expr, branch_eval.diag_message);
        return gx_result(GENEX_OK, branch_eval.value, nob_sv_from_cstr(""));
    }

    if (gx_sv_eq_ci(op, nob_sv_from_cstr("TARGET_PROPERTY"))) {
        Gx_Sv_List args = gx_split_top_level_alloc(ctx, args_expr, ',');
        if (args_expr.count > 0 && args.count == 0) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "Out of memory while splitting TARGET_PROPERTY arguments"));
        }
        if (args.count < 1 || args.count > 2) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY expects property or target,property"));
        }
        String_View target_name = nob_sv_from_cstr("");
        String_View property_name = nob_sv_from_cstr("");
        if (args.count == 1) {
            target_name = gx_trim(ctx->current_target_name);
            Genex_Result prop_eval = gx_eval_inner(ctx, args.items[0], depth + 1, stack);
            if (prop_eval.status != GENEX_OK) return gx_result(prop_eval.status, raw_expr, prop_eval.diag_message);
            property_name = gx_trim(prop_eval.value);
            if (target_name.count == 0) {
                return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY implicit form requires current target context"));
            }
        } else {
            Genex_Result target_eval = gx_eval_inner(ctx, args.items[0], depth + 1, stack);
            if (target_eval.status != GENEX_OK) return gx_result(target_eval.status, raw_expr, target_eval.diag_message);
            Genex_Result prop_eval = gx_eval_inner(ctx, args.items[1], depth + 1, stack);
            if (prop_eval.status != GENEX_OK) return gx_result(prop_eval.status, raw_expr, prop_eval.diag_message);
            target_name = gx_trim(target_eval.value);
            property_name = gx_trim(prop_eval.value);
        }
        if (target_name.count == 0 || property_name.count == 0) {
            return gx_result(GENEX_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        }
        if (!ctx->read_target_property) {
            return gx_result(GENEX_ERROR, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY callback is not configured"));
        }
        if (stack->count >= ctx->max_target_property_depth) {
            return gx_result(GENEX_CYCLE_GUARD_HIT, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY depth guard reached"));
        }
        if (gx_tp_stack_contains(ctx, stack, target_name, property_name)) {
            return gx_result(GENEX_CYCLE_GUARD_HIT, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY cycle detected"));
        }
        if (!gx_tp_stack_push(stack, target_name, property_name)) {
            return gx_result(GENEX_CYCLE_GUARD_HIT, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "TARGET_PROPERTY stack overflow"));
        }

        String_View raw_value = ctx->read_target_property(ctx->userdata, target_name, property_name);
        Genex_Result valid = gx_validate_callback_value(ctx, raw_value, raw_expr, "TARGET_PROPERTY callback returned an invalid or too large value");
        if (valid.status != GENEX_OK) {
            gx_tp_stack_pop(stack);
            return valid;
        }
        raw_value = valid.value;
        Genex_Result nested = gx_eval_inner(ctx, raw_value, depth + 1, stack);
        gx_tp_stack_pop(stack);
        if (nested.status != GENEX_OK) return gx_result(nested.status, raw_expr, nested.diag_message);
        return gx_result(GENEX_OK, nested.value, nob_sv_from_cstr(""));
    }

    if (args_expr.count == 0 &&
        op.count >= 3 &&
        op.data[0] == '$' &&
        op.data[1] == '<' &&
        op.data[op.count - 1] == '>') {
        Genex_Result nested = gx_eval_inner(ctx, op, depth + 1, stack);
        if (nested.status != GENEX_OK) return gx_result(nested.status, raw_expr, nested.diag_message);
        return gx_result(GENEX_OK, nested.value, nob_sv_from_cstr(""));
    }

    if (args_expr.count > 0 &&
        (gx_sv_contains_genex_unescaped(op) ||
         nob_sv_eq(op, nob_sv_from_cstr("0")) ||
         nob_sv_eq(op, nob_sv_from_cstr("1")))) {
        Genex_Result cond_eval = gx_eval_inner(ctx, op, depth + 1, stack);
        if (cond_eval.status != GENEX_OK) return gx_result(cond_eval.status, raw_expr, cond_eval.diag_message);
        if (gx_cmake_string_is_false(cond_eval.value)) {
            return gx_result(GENEX_OK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
        }
        Genex_Result value_eval = gx_eval_inner(ctx, args_expr, depth + 1, stack);
        if (value_eval.status != GENEX_OK) return gx_result(value_eval.status, raw_expr, value_eval.diag_message);
        return gx_result(GENEX_OK, value_eval.value, nob_sv_from_cstr(""));
    }

    return gx_result(GENEX_UNSUPPORTED, raw_expr, gx_copy_cstr_to_arena(ctx->arena, "Unsupported generator expression operator"));
}

static Genex_Result gx_eval_inner(const Genex_Context *ctx,
                                       String_View input,
                                       size_t depth,
                                       Genex_Target_Property_Stack *stack) {
    if (!ctx || !ctx->arena) return gx_result(GENEX_ERROR, input, nob_sv_from_cstr("Invalid genex context"));
    if (depth > ctx->max_depth) {
        return gx_result(GENEX_ERROR, input, gx_copy_cstr_to_arena(ctx->arena, "Generator expression max depth exceeded"));
    }
    if (input.count == 0 || !gx_sv_contains_genex_unescaped(input)) {
        return gx_result(GENEX_OK, input, nob_sv_from_cstr(""));
    }

    String_Builder sb = {0};
    size_t cursor = 0;
    while (cursor < input.count) {
        size_t open = cursor;
        bool found = false;
        for (; open + 1 < input.count; open++) {
            if (gx_is_genex_open_at(input, open)) {
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
        if (!gx_find_matching_genex_end(input, open, &close)) {
            nob_sb_free(sb);
            return gx_result(GENEX_ERROR, input, gx_copy_cstr_to_arena(ctx->arena, "Unclosed generator expression"));
        }

        String_View body = nob_sv_from_parts(input.data + open + 2, close - (open + 2));
        String_View raw_expr = nob_sv_from_parts(input.data + open, close - open + 1);
        Genex_Result part = gx_eval_body(ctx, body, raw_expr, depth, stack);
        if (part.status != GENEX_OK) {
            nob_sb_free(sb);
            return gx_result(part.status, input, part.diag_message);
        }
        if (part.value.count > 0) {
            nob_sb_append_buf(&sb, part.value.data, part.value.count);
        }
        cursor = close + 1;
    }

    String_View out = nob_sv_from_cstr("");
    if (sb.count > 0) {
        out = gx_copy_to_arena(ctx->arena, nob_sv_from_parts(sb.items, sb.count));
    }
    nob_sb_free(sb);
    return gx_result(GENEX_OK, out, nob_sv_from_cstr(""));
}

Genex_Result genex_eval(const Genex_Context *ctx, String_View input) {
    if (!ctx || !ctx->arena) {
        return gx_result(GENEX_ERROR, input, nob_sv_from_cstr("Invalid genex context"));
    }

    Genex_Context local = *ctx;
    if (local.max_depth == 0) local.max_depth = 64;
    if (local.max_target_property_depth == 0) local.max_target_property_depth = 64;

    Genex_Target_Property_Stack stack = {0};
    return gx_eval_inner(&local, input, 0, &stack);
}


