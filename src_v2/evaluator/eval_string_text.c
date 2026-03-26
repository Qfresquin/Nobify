#include "eval_string_internal.h"

#include <stdio.h>

static String_View list_strip_ws_view(String_View in) {
    size_t b = 0;
    while (b < in.count && isspace((unsigned char)in.data[b])) b++;

    size_t e = in.count;
    while (e > b && isspace((unsigned char)in.data[e - 1])) e--;

    return nob_sv_from_parts(in.data + b, e - b);
}

static int string_sv_cmp(String_View a, String_View b) {
    size_t n = a.count < b.count ? a.count : b.count;
    if (n > 0) {
        int c = memcmp(a.data, b.data, n);
        if (c != 0) return c < 0 ? -1 : 1;
    }
    if (a.count < b.count) return -1;
    if (a.count > b.count) return 1;
    return 0;
}

static bool string_append_sv(Nob_String_Builder *sb, String_View sv) {
    if (!sb) return false;
    if (sv.count > 0) nob_sb_append_buf(sb, sv.data, sv.count);
    return true;
}

static bool string_append_sv_escaped_quotes(Nob_String_Builder *sb, String_View sv, bool escape_quotes) {
    if (!sb) return false;
    if (!escape_quotes) return string_append_sv(sb, sv);
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '"') nob_sb_append(sb, '\\');
        nob_sb_append(sb, c);
    }
    return true;
}

static bool string_configure_expand_temp(EvalExecContext *ctx,
                                         String_View input,
                                         bool at_only,
                                         bool escape_quotes,
                                         String_View *out) {
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    Nob_String_Builder sb = {0};
    for (size_t i = 0; i < input.count;) {
        if (input.data[i] == '@') {
            size_t j = i + 1;
            while (j < input.count && input.data[j] != '@') j++;
            if (j < input.count && j > i + 1) {
                String_View key = nob_sv_from_parts(input.data + i + 1, j - (i + 1));
                String_View val = eval_var_get_visible(ctx, key);
                (void)string_append_sv_escaped_quotes(&sb, val, escape_quotes);
                i = j + 1;
                continue;
            }
        }

        if (!at_only && input.data[i] == '$' && i + 1 < input.count && input.data[i + 1] == '{') {
            size_t j = i + 2;
            while (j < input.count && input.data[j] != '}') j++;
            if (j < input.count) {
                String_View key = nob_sv_from_parts(input.data + i + 2, j - (i + 2));
                String_View val = nob_sv_from_cstr("");
                if (key.count > 5 && memcmp(key.data, "ENV{", 4) == 0 && key.data[key.count - 1] == '}') {
                    String_View env_key = nob_sv_from_parts(key.data + 4, key.count - 5);
                    char *env_buf = (char*)arena_alloc(eval_temp_arena(ctx), env_key.count + 1);
                    if (!env_buf) {
                        nob_sb_free(sb);
                        EVAL_OOM_RETURN_IF_NULL(ctx, env_buf, false);
                    }
                    memcpy(env_buf, env_key.data, env_key.count);
                    env_buf[env_key.count] = '\0';
                    const char *env_val = eval_getenv_temp(ctx, env_buf);
                    if (env_val) val = nob_sv_from_cstr(env_val);
                } else {
                    val = eval_var_get_visible(ctx, key);
                }
                (void)string_append_sv_escaped_quotes(&sb, val, escape_quotes);
                i = j + 1;
                continue;
            }
        }

        nob_sb_append(&sb, input.data[i]);
        i++;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
    if (!buf) {
        nob_sb_free(sb);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    }
    if (sb.count > 0) memcpy(buf, sb.items, sb.count);
    buf[sb.count] = '\0';
    nob_sb_free(sb);
    *out = nob_sv_from_cstr(buf);
    return true;
}

static String_View string_genex_strip_temp(EvalExecContext *ctx, String_View in) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t out = 0;
    for (size_t i = 0; i < in.count; i++) {
        if (i + 1 < in.count && in.data[i] == '$' && in.data[i + 1] == '<') {
            size_t j = i + 2;
            size_t depth = 1;
            while (j < in.count && depth > 0) {
                if (j + 1 < in.count && in.data[j] == '$' && in.data[j + 1] == '<') {
                    depth++;
                    j += 2;
                    continue;
                }
                if (in.data[j] == '>') {
                    depth--;
                    j++;
                    continue;
                }
                j++;
            }
            if (j == 0) break;
            i = j - 1;
            continue;
        }
        buf[out++] = in.data[i];
    }

    buf[out] = '\0';
    return nob_sv_from_cstr(buf);
}

static long long string_find_substr(String_View hay, String_View needle, bool reverse) {
    if (needle.count == 0) return reverse ? (long long)hay.count : 0;
    if (needle.count > hay.count) return -1;

    if (!reverse) {
        for (size_t i = 0; i + needle.count <= hay.count; i++) {
            if (memcmp(hay.data + i, needle.data, needle.count) == 0) return (long long)i;
        }
        return -1;
    }

    for (size_t i = hay.count - needle.count + 1; i > 0; i--) {
        size_t at = i - 1;
        if (memcmp(hay.data + at, needle.data, needle.count) == 0) return (long long)at;
    }
    return -1;
}

static String_View string_bytes_hex_temp(EvalExecContext *ctx, const unsigned char *bytes, size_t count, bool upper) {
    if (!ctx || !bytes) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), (count * 2) + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    const char *lut = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    for (size_t i = 0; i < count; i++) {
        unsigned char b = bytes[i];
        buf[i * 2 + 0] = lut[(b >> 4) & 0x0F];
        buf[i * 2 + 1] = lut[b & 0x0F];
    }
    buf[count * 2] = '\0';
    return nob_sv_from_cstr(buf);
}

Eval_Result eval_string_handle_text(EvalExecContext *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (!ctx || !node || arena_arr_len(a) < 1 || eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (eval_sv_eq_ci_lit(a[0], "APPEND") || eval_sv_eq_ci_lit(a[0], "PREPEND")) {
        if (arena_arr_len(a) < 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(APPEND/PREPEND) requires variable name"), nob_sv_from_cstr("Usage: string(APPEND|PREPEND <var> [input...])"));
            return eval_result_from_ctx(ctx);
        }
        if (arena_arr_len(a) == 2) return eval_result_from_ctx(ctx);

        bool is_append = eval_sv_eq_ci_lit(a[0], "APPEND");
        String_View var = a[1];
        String_View extra = eval_string_join_no_sep_temp(ctx, &a[2], arena_arr_len(a) - 2);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        String_View curr = eval_var_get_visible(ctx, var);
        String_View lhs = is_append ? curr : extra;
        String_View rhs = is_append ? extra : curr;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), lhs.count + rhs.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
        if (lhs.count > 0) memcpy(buf, lhs.data, lhs.count);
        if (rhs.count > 0) memcpy(buf + lhs.count, rhs.data, rhs.count);
        buf[lhs.count + rhs.count] = '\0';
        (void)eval_var_set_current(ctx, var, nob_sv_from_cstr(buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "CONCAT")) {
        if (arena_arr_len(a) < 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(CONCAT) requires output variable"), nob_sv_from_cstr("Usage: string(CONCAT <out-var> [input...])"));
            return eval_result_from_ctx(ctx);
        }
        String_View out_var = a[1];
        String_View out = (arena_arr_len(a) > 2) ? eval_string_join_no_sep_temp(ctx, &a[2], arena_arr_len(a) - 2) : nob_sv_from_cstr("");
        (void)eval_var_set_current(ctx, out_var, out);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "JOIN")) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(JOIN) requires glue and output variable"), nob_sv_from_cstr("Usage: string(JOIN <glue> <out-var> [input...])"));
            return eval_result_from_ctx(ctx);
        }

        String_View glue = a[1];
        String_View out_var = a[2];
        size_t n = (arena_arr_len(a) > 3) ? (arena_arr_len(a) - 3) : 0;
        size_t total = 0;
        for (size_t i = 0; i < n; i++) total += a[3 + i].count;
        if (n > 1) total += glue.count * (n - 1);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());

        size_t off = 0;
        for (size_t i = 0; i < n; i++) {
            if (i > 0 && glue.count > 0) {
                memcpy(buf + off, glue.data, glue.count);
                off += glue.count;
            }
            if (a[3 + i].count > 0) {
                memcpy(buf + off, a[3 + i].data, a[3 + i].count);
                off += a[3 + i].count;
            }
        }
        buf[off] = '\0';
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "LENGTH")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(LENGTH) requires input and output variable"), nob_sv_from_cstr("Usage: string(LENGTH <string> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }
        char num_buf[64];
        snprintf(num_buf, sizeof(num_buf), "%zu", a[1].count);
        (void)eval_var_set_current(ctx, a[2], nob_sv_from_cstr(num_buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "STRIP")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(STRIP) requires input and output variable"), nob_sv_from_cstr("Usage: string(STRIP <string> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }
        (void)eval_var_set_current(ctx, a[2], list_strip_ws_view(a[1]));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "FIND")) {
        if (!(arena_arr_len(a) == 4 || arena_arr_len(a) == 5)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(FIND) requires input, substring and output variable"), nob_sv_from_cstr("Usage: string(FIND <string> <substring> <out-var> [REVERSE])"));
            return eval_result_from_ctx(ctx);
        }
        bool reverse = false;
        if (arena_arr_len(a) == 5) {
            if (!eval_sv_eq_ci_lit(a[4], "REVERSE")) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("string(FIND) received unsupported option"), a[4]);
                return eval_result_from_ctx(ctx);
            }
            reverse = true;
        }
        long long idx = string_find_substr(a[1], a[2], reverse);
        char num_buf[64];
        snprintf(num_buf, sizeof(num_buf), "%lld", idx);
        (void)eval_var_set_current(ctx, a[3], nob_sv_from_cstr(num_buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "COMPARE")) {
        if (arena_arr_len(a) != 5) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(COMPARE) requires op, lhs, rhs and output variable"), nob_sv_from_cstr("Usage: string(COMPARE <LESS|GREATER|EQUAL|NOTEQUAL|LESS_EQUAL|GREATER_EQUAL> <s1> <s2> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }
        int cmp = string_sv_cmp(a[2], a[3]);
        bool ok = false;
        if (eval_sv_eq_ci_lit(a[1], "LESS")) ok = cmp < 0;
        else if (eval_sv_eq_ci_lit(a[1], "GREATER")) ok = cmp > 0;
        else if (eval_sv_eq_ci_lit(a[1], "EQUAL")) ok = cmp == 0;
        else if (eval_sv_eq_ci_lit(a[1], "NOTEQUAL")) ok = cmp != 0;
        else if (eval_sv_eq_ci_lit(a[1], "LESS_EQUAL")) ok = cmp <= 0;
        else if (eval_sv_eq_ci_lit(a[1], "GREATER_EQUAL")) ok = cmp >= 0;
        else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("string(COMPARE) received unsupported operation"), a[1]);
            return eval_result_from_ctx(ctx);
        }
        (void)eval_var_set_current(ctx, a[4], ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "ASCII")) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(ASCII) requires at least one code and output variable"), nob_sv_from_cstr("Usage: string(ASCII <code>... <out-var>)"));
            return eval_result_from_ctx(ctx);
        }
        String_View out_var = a[arena_arr_len(a) - 1];
        size_t n = arena_arr_len(a) - 2;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), n + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
        for (size_t i = 0; i < n; i++) {
            long long code = 0;
            if (!eval_string_parse_i64(a[1 + i], &code) || code < 0 || code > 255) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "string", nob_sv_from_cstr("string(ASCII) code must be an integer in range [0,255]"), a[1 + i]);
                return eval_result_from_ctx(ctx);
            }
            buf[i] = (char)((unsigned char)code);
        }
        buf[n] = '\0';
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_parts(buf, n));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "HEX")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(HEX) requires input and output variable"), nob_sv_from_cstr("Usage: string(HEX <string> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }
        String_View out = string_bytes_hex_temp(ctx, (const unsigned char*)a[1].data, a[1].count, false);
        (void)eval_var_set_current(ctx, a[2], out);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "CONFIGURE")) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(CONFIGURE) requires input and output variable"), nob_sv_from_cstr("Usage: string(CONFIGURE <string> <out-var> [@ONLY] [ESCAPE_QUOTES])"));
            return eval_result_from_ctx(ctx);
        }
        bool at_only = false;
        bool escape_quotes = false;
        for (size_t i = 3; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "@ONLY")) {
                at_only = true;
            } else if (eval_sv_eq_ci_lit(a[i], "ESCAPE_QUOTES")) {
                escape_quotes = true;
            } else {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("string(CONFIGURE) received unsupported option"), a[i]);
                return eval_result_from_ctx(ctx);
            }
        }
        String_View out = nob_sv_from_cstr("");
        if (!string_configure_expand_temp(ctx, a[1], at_only, escape_quotes, &out)) {
            return eval_result_from_ctx(ctx);
        }
        (void)eval_var_set_current(ctx, a[2], out);
        if (!eval_emit_string_configure(ctx, o, a[2])) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "MAKE_C_IDENTIFIER")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(MAKE_C_IDENTIFIER) requires input and output variable"), nob_sv_from_cstr("Usage: string(MAKE_C_IDENTIFIER <string> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }
        String_View in = a[1];
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 2);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
        size_t off = 0;
        if (in.count > 0 && isdigit((unsigned char)in.data[0])) buf[off++] = '_';
        for (size_t i = 0; i < in.count; i++) {
            unsigned char c = (unsigned char)in.data[i];
            buf[off++] = (isalnum(c) || c == '_') ? (char)c : '_';
        }
        buf[off] = '\0';
        (void)eval_var_set_current(ctx, a[2], nob_sv_from_cstr(buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "GENEX_STRIP")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(GENEX_STRIP) requires input and output variable"), nob_sv_from_cstr("Usage: string(GENEX_STRIP <string> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }
        (void)eval_var_set_current(ctx, a[2], string_genex_strip_temp(ctx, a[1]));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "REPEAT")) {
        if (arena_arr_len(a) != 4) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(REPEAT) requires input, count and output variable"), nob_sv_from_cstr("Usage: string(REPEAT <string> <count> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        unsigned long long count = 0;
        if (!eval_string_parse_u64_sv(a[2], &count)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "string", nob_sv_from_cstr("string(REPEAT) repeat count is not a non-negative integer"), a[2]);
            return eval_result_from_ctx(ctx);
        }

        String_View in = a[1];
        if (in.count > 0 && count > (SIZE_MAX / in.count)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NUMERIC_OVERFLOW, "string", nob_sv_from_cstr("string(REPEAT) result is too large"), nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        size_t total = (size_t)count * in.count;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
        size_t off = 0;
        for (size_t i = 0; i < (size_t)count; i++) {
            if (in.count > 0) {
                memcpy(buf + off, in.data, in.count);
                off += in.count;
            }
        }
        buf[off] = '\0';
        (void)eval_var_set_current(ctx, a[3], nob_sv_from_cstr(buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "REPLACE")) {
        if (arena_arr_len(a) < 4) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(REPLACE) requires match, replace, out-var and input"), nob_sv_from_cstr("Usage: string(REPLACE <match> <replace> <out-var> <input>...)"));
            return eval_result_from_ctx(ctx);
        }

        String_View match = a[1];
        String_View repl = a[2];
        String_View out_var = a[3];
        String_View input = (arena_arr_len(a) > 4) ? eval_sv_join_semi_temp(ctx, &a[4], arena_arr_len(a) - 4) : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        if (match.count == 0) {
            (void)eval_var_set_current(ctx, out_var, input);
            return eval_result_from_ctx(ctx);
        }

        size_t hits = 0;
        size_t i = 0;
        while (i + match.count <= input.count) {
            if (memcmp(input.data + i, match.data, match.count) == 0) {
                hits++;
                i += match.count;
            } else {
                i++;
            }
        }

        size_t total = input.count;
        if (repl.count >= match.count) total += hits * (repl.count - match.count);
        else total -= hits * (match.count - repl.count);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());

        size_t off = 0;
        i = 0;
        while (i < input.count) {
            if (i + match.count <= input.count && memcmp(input.data + i, match.data, match.count) == 0) {
                if (repl.count) {
                    memcpy(buf + off, repl.data, repl.count);
                    off += repl.count;
                }
                i += match.count;
            } else {
                buf[off++] = input.data[i++];
            }
        }
        buf[off] = '\0';
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(buf));
        if (!eval_emit_string_replace(ctx, o, out_var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "TOUPPER")) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(TOUPPER) requires input and output variable"), nob_sv_from_cstr("Usage: string(TOUPPER <input> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View out_var = a[arena_arr_len(a) - 1];
        String_View input = (arena_arr_len(a) == 3) ? a[1] : eval_sv_join_semi_temp(ctx, &a[1], arena_arr_len(a) - 2);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
        for (size_t i = 0; i < input.count; i++) buf[i] = (char)toupper((unsigned char)input.data[i]);
        buf[input.count] = '\0';
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "TOLOWER")) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(TOLOWER) requires input and output variable"), nob_sv_from_cstr("Usage: string(TOLOWER <input> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View out_var = a[arena_arr_len(a) - 1];
        String_View input = (arena_arr_len(a) == 3) ? a[1] : eval_sv_join_semi_temp(ctx, &a[1], arena_arr_len(a) - 2);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
        for (size_t i = 0; i < input.count; i++) buf[i] = (char)tolower((unsigned char)input.data[i]);
        buf[input.count] = '\0';
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "SUBSTRING")) {
        if (arena_arr_len(a) != 5) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(SUBSTRING) requires input, begin, length and output variable"), nob_sv_from_cstr("Usage: string(SUBSTRING <input> <begin> <length> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View input = a[1];
        long long begin = 0;
        long long length = 0;
        if (!eval_string_parse_i64(a[2], &begin) || !eval_string_parse_i64(a[3], &length)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "string", nob_sv_from_cstr("string(SUBSTRING) begin/length must be integers"), nob_sv_from_cstr("Use numeric begin and length (length can be -1 for until end)"));
            return eval_result_from_ctx(ctx);
        }
        if (begin < 0 || length < -1) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "string", nob_sv_from_cstr("string(SUBSTRING) begin must be >= 0 and length >= -1"), nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        String_View out_var = a[4];
        size_t b = (size_t)begin;
        if (b >= input.count) {
            (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        size_t end = input.count;
        if (length >= 0) {
            size_t l = (size_t)length;
            if (b + l < end) end = b + l;
        }
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_parts(input.data + b, end - b));
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}
