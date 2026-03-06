#include "eval_string_internal.h"

#include <pcre2posix.h>

Eval_Result eval_string_handle_regex(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (!ctx || !node || arena_arr_len(a) < 1 || eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 5) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(REGEX) requires mode and arguments"), nob_sv_from_cstr("Usage: string(REGEX MATCH|REPLACE|MATCHALL ...)"));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[1], "MATCH")) {
        String_View pattern = a[2];
        String_View out_var = a[3];
        String_View input = (arena_arr_len(a) > 4) ? eval_sv_join_semi_temp(ctx, &a[4], arena_arr_len(a) - 4) : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        char *pat_buf = (char*)arena_alloc(eval_temp_arena(ctx), pattern.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, pat_buf, eval_result_fatal());
        memcpy(pat_buf, pattern.data, pattern.count);
        pat_buf[pattern.count] = '\0';

        char *in_buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, in_buf, eval_result_fatal());
        memcpy(in_buf, input.data, input.count);
        in_buf[input.count] = '\0';

        regex_t re;
        if (regcomp(&re, pat_buf, REG_EXTENDED) != 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_PARSE_ERROR, "string", nob_sv_from_cstr("Invalid regex pattern"), pattern);
            return eval_result_from_ctx(ctx);
        }

        regmatch_t m[1];
        int rc = regexec(&re, in_buf, 1, m, 0);
        regfree(&re);

        if (rc == 0 && m[0].rm_so >= 0 && m[0].rm_eo >= m[0].rm_so) {
            size_t mlen = (size_t)(m[0].rm_eo - m[0].rm_so);
            (void)eval_var_set_current(ctx, out_var, nob_sv_from_parts(in_buf + m[0].rm_so, mlen));
        } else {
            (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""));
        }
        if (!eval_emit_string_regex(ctx, o, nob_sv_from_cstr("MATCH"), out_var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[1], "REPLACE")) {
        if (arena_arr_len(a) < 6) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(REGEX REPLACE) requires regex, replace, out-var and input"), nob_sv_from_cstr("Usage: string(REGEX REPLACE <regex> <replace> <out-var> <input>...)"));
            return eval_result_from_ctx(ctx);
        }

        String_View pattern = a[2];
        String_View replacement = a[3];
        String_View out_var = a[4];
        String_View input = (arena_arr_len(a) > 5) ? eval_sv_join_semi_temp(ctx, &a[5], arena_arr_len(a) - 5) : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        char *pat_buf = (char*)arena_alloc(eval_temp_arena(ctx), pattern.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, pat_buf, eval_result_fatal());
        memcpy(pat_buf, pattern.data, pattern.count);
        pat_buf[pattern.count] = '\0';

        char *in_buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, in_buf, eval_result_fatal());
        memcpy(in_buf, input.data, input.count);
        in_buf[input.count] = '\0';

        regex_t re;
        if (regcomp(&re, pat_buf, REG_EXTENDED) != 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_PARSE_ERROR, "string", nob_sv_from_cstr("Invalid regex pattern"), pattern);
            return eval_result_from_ctx(ctx);
        }

        enum { MAX_GROUPS = 10 };
        regmatch_t m[MAX_GROUPS];
        Nob_String_Builder sb = {0};
        const char *cursor = in_buf;

        for (;;) {
            int rc = regexec(&re, cursor, MAX_GROUPS, m, 0);
            if (rc != 0 || m[0].rm_so < 0 || m[0].rm_eo < m[0].rm_so) {
                nob_sb_append_cstr(&sb, cursor);
                break;
            }

            size_t prefix_len = (size_t)m[0].rm_so;
            if (prefix_len > 0) nob_sb_append_buf(&sb, cursor, prefix_len);

            for (size_t i = 0; i < replacement.count; i++) {
                char c = replacement.data[i];
                if (c == '\\' && i + 1 < replacement.count) {
                    char n = replacement.data[++i];
                    if (n >= '0' && n <= '9') {
                        size_t gi = (size_t)(n - '0');
                        if (gi < MAX_GROUPS && m[gi].rm_so >= 0 && m[gi].rm_eo >= m[gi].rm_so) {
                            size_t glen = (size_t)(m[gi].rm_eo - m[gi].rm_so);
                            if (glen > 0) nob_sb_append_buf(&sb, cursor + m[gi].rm_so, glen);
                        }
                        continue;
                    }
                    nob_sb_append(&sb, n);
                    continue;
                }
                nob_sb_append(&sb, c);
            }

            size_t adv = (size_t)m[0].rm_eo;
            if (adv == 0) {
                if (*cursor == '\0') break;
                nob_sb_append(&sb, *cursor);
                cursor++;
            } else {
                cursor += adv;
            }
        }

        regfree(&re);
        char *out_buf = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
        if (!out_buf) {
            nob_sb_free(sb);
            EVAL_OOM_RETURN_IF_NULL(ctx, out_buf, eval_result_fatal());
        }
        if (sb.count) memcpy(out_buf, sb.items, sb.count);
        out_buf[sb.count] = '\0';
        nob_sb_free(sb);
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(out_buf));
        if (!eval_emit_string_regex(ctx, o, nob_sv_from_cstr("REPLACE"), out_var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[1], "MATCHALL")) {
        String_View pattern = a[2];
        String_View out_var = a[3];
        String_View input = (arena_arr_len(a) > 4) ? eval_string_join_no_sep_temp(ctx, &a[4], arena_arr_len(a) - 4) : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        char *pat_buf = (char*)arena_alloc(eval_temp_arena(ctx), pattern.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, pat_buf, eval_result_fatal());
        memcpy(pat_buf, pattern.data, pattern.count);
        pat_buf[pattern.count] = '\0';

        char *in_buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, in_buf, eval_result_fatal());
        memcpy(in_buf, input.data, input.count);
        in_buf[input.count] = '\0';

        regex_t re;
        if (regcomp(&re, pat_buf, REG_EXTENDED) != 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_PARSE_ERROR, "string", nob_sv_from_cstr("Invalid regex pattern"), pattern);
            return eval_result_from_ctx(ctx);
        }

        SV_List matches = {0};
        const char *cursor = in_buf;
        for (;;) {
            regmatch_t m[1];
            int rc = regexec(&re, cursor, 1, m, 0);
            if (rc != 0 || m[0].rm_so < 0 || m[0].rm_eo < m[0].rm_so) break;

            String_View hit = nob_sv_from_parts(cursor + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
            if (!svu_list_push_temp(ctx, &matches, hit)) {
                regfree(&re);
                return eval_result_from_ctx(ctx);
            }

            if (m[0].rm_eo == 0) {
                if (*cursor == '\0') break;
                cursor++;
            } else {
                cursor += m[0].rm_eo;
            }
        }
        regfree(&re);

        String_View out = (arena_arr_len(matches) > 0) ? eval_sv_join_semi_temp(ctx, matches, arena_arr_len(matches)) : nob_sv_from_cstr("");
        (void)eval_var_set_current(ctx, out_var, out);
        return eval_result_from_ctx(ctx);
    }

    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("Unsupported string(REGEX) mode"), nob_sv_from_cstr("Implemented: MATCH, REPLACE, MATCHALL"));
    return eval_result_from_ctx(ctx);
}
