#include "eval_list_internal.h"

#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

size_t list_count_items(String_View list_sv) {
    if (list_sv.count == 0) return 0;
    size_t count = 1;
    for (size_t i = 0; i < list_sv.count; i++) {
        if (list_sv.data[i] == ';') count++;
    }
    return count;
}

bool list_item_in_set(String_View item, String_View *set, size_t set_count) {
    for (size_t i = 0; i < set_count; i++) {
        if (eval_sv_key_eq(item, set[i])) return true;
    }
    return false;
}

bool list_split_semicolon_preserve_empty(EvalExecContext *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;
    if (input.count == 0) return true;

    const char *p = input.data;
    const char *end = input.data + input.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        if (!svu_list_push_temp(ctx, out, nob_sv_from_parts(p, (size_t)(q - p)))) return false;
        if (q == end) break;
        p = q + 1;
    }
    return true;
}

bool list_sv_parse_i64(String_View sv, long long *out) {
    if (!out || sv.count == 0) return false;
    char buf[64];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    long long v = strtoll(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out = v;
    return true;
}

String_View list_join_items_temp(EvalExecContext *ctx, String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    return eval_sv_join_semi_temp(ctx, items, count);
}

bool list_set_var_from_items(EvalExecContext *ctx, String_View var, String_View *items, size_t count) {
    if (!ctx) return false;
    String_View joined = list_join_items_temp(ctx, items, count);
    if (eval_should_stop(ctx)) return false;
    return eval_var_set_current(ctx, var, joined);
}

bool list_load_var_items(EvalExecContext *ctx, String_View var, SV_List *out) {
    if (!ctx || !out) return false;
    *out = (SV_List){0};
    return list_split_semicolon_preserve_empty(ctx, eval_var_get_visible(ctx, var), out);
}

bool list_normalize_index(size_t item_count, long long raw_index, bool allow_end, size_t *out_index) {
    if (!out_index) return false;
    if (item_count > (size_t)LLONG_MAX) return false;

    long long idx = raw_index;
    if (idx < 0) idx = (long long)item_count + idx;

    long long hi = allow_end ? (long long)item_count : (long long)item_count - 1;
    if (item_count == 0 && !allow_end) return false;
    if (idx < 0 || idx > hi) return false;

    *out_index = (size_t)idx;
    return true;
}

String_View list_concat_temp(EvalExecContext *ctx, String_View a, String_View b) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), a.count + b.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (a.count) memcpy(buf, a.data, a.count);
    if (b.count) memcpy(buf + a.count, b.data, b.count);
    buf[a.count + b.count] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View list_to_case_temp(EvalExecContext *ctx, String_View in, bool upper) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) {
        unsigned char c = (unsigned char)in.data[i];
        buf[i] = upper ? (char)toupper(c) : (char)tolower(c);
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View list_strip_ws_view(String_View in) {
    size_t b = 0;
    while (b < in.count && isspace((unsigned char)in.data[b])) b++;

    size_t e = in.count;
    while (e > b && isspace((unsigned char)in.data[e - 1])) e--;

    return nob_sv_from_parts(in.data + b, e - b);
}

String_View list_genex_strip_temp(EvalExecContext *ctx, String_View in) {
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

bool list_compile_regex(EvalExecContext *ctx,
                        const Node *node,
                        Cmake_Event_Origin o,
                        String_View pattern,
                        regex_t *out_re) {
    if (!ctx || !node || !out_re) return false;
    char *pat_c = eval_sv_to_cstr_temp(ctx, pattern);
    if (!pat_c) return false;
    if (regcomp(out_re, pat_c, REG_EXTENDED) != 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_PARSE_ERROR, "list", nob_sv_from_cstr("Invalid regex pattern"), pattern);
        return false;
    }
    return true;
}

bool list_regex_replace_one_temp(EvalExecContext *ctx,
                                 regex_t *re,
                                 String_View replacement,
                                 String_View input,
                                 String_View *out) {
    if (!ctx || !re || !out) return false;
    enum { MAX_GROUPS = 10 };

    char *in_buf = eval_sv_to_cstr_temp(ctx, input);
    if (!in_buf) return false;

    regmatch_t m[MAX_GROUPS];
    Nob_String_Builder sb = {0};
    const char *cursor = in_buf;

    for (;;) {
        int rc = regexec(re, cursor, MAX_GROUPS, m, 0);
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

    char *out_buf = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
    if (!out_buf) {
        nob_sb_free(sb);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_buf, false);
    }
    if (sb.count) memcpy(out_buf, sb.items, sb.count);
    out_buf[sb.count] = '\0';
    nob_sb_free(sb);

    *out = nob_sv_from_cstr(out_buf);
    return true;
}

static String_View list_path_basename(String_View in) {
    size_t start = 0;
    for (size_t i = in.count; i > 0; i--) {
        char c = in.data[i - 1];
        if (c == '/' || c == '\\') {
            start = i;
            break;
        }
    }
    return nob_sv_from_parts(in.data + start, in.count - start);
}

static int list_char_cmp(char a, char b, bool case_sensitive) {
    unsigned char ua = (unsigned char)a;
    unsigned char ub = (unsigned char)b;
    if (!case_sensitive) {
        ua = (unsigned char)tolower(ua);
        ub = (unsigned char)tolower(ub);
    }
    if (ua < ub) return -1;
    if (ua > ub) return 1;
    return 0;
}

static int list_sv_lex_cmp(String_View a, String_View b, bool case_sensitive) {
    size_t n = a.count < b.count ? a.count : b.count;
    for (size_t i = 0; i < n; i++) {
        int c = list_char_cmp(a.data[i], b.data[i], case_sensitive);
        if (c != 0) return c;
    }
    if (a.count < b.count) return -1;
    if (a.count > b.count) return 1;
    return 0;
}

static bool list_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int list_sv_natural_cmp(String_View a, String_View b, bool case_sensitive) {
    size_t i = 0;
    size_t j = 0;

    while (i < a.count && j < b.count) {
        bool da = list_is_digit(a.data[i]);
        bool db = list_is_digit(b.data[j]);
        if (da && db) {
            size_t ai = i;
            size_t bj = j;
            while (ai < a.count && a.data[ai] == '0') ai++;
            while (bj < b.count && b.data[bj] == '0') bj++;

            size_t ae = ai;
            size_t be = bj;
            while (ae < a.count && list_is_digit(a.data[ae])) ae++;
            while (be < b.count && list_is_digit(b.data[be])) be++;

            size_t lena = ae - ai;
            size_t lenb = be - bj;
            if (lena != lenb) return lena < lenb ? -1 : 1;

            for (size_t k = 0; k < lena; k++) {
                int c = list_char_cmp(a.data[ai + k], b.data[bj + k], true);
                if (c != 0) return c;
            }

            size_t runa = ae - i;
            size_t runb = be - j;
            if (runa != runb) return runa < runb ? -1 : 1;

            i = ae;
            j = be;
            continue;
        }

        int c = list_char_cmp(a.data[i], b.data[j], case_sensitive);
        if (c != 0) return c;
        i++;
        j++;
    }

    if (i < a.count) return 1;
    if (j < b.count) return -1;
    return 0;
}

int list_sort_item_cmp(String_View a, String_View b, const List_Sort_Options *opt) {
    bool case_sensitive = (opt->case_mode == LIST_SORT_CASE_SENSITIVE);

    if (opt->compare == LIST_SORT_COMPARE_FILE_BASENAME) {
        a = list_path_basename(a);
        b = list_path_basename(b);
    }

    int cmp = 0;
    if (opt->compare == LIST_SORT_COMPARE_NATURAL) {
        cmp = list_sv_natural_cmp(a, b, case_sensitive);
    } else {
        cmp = list_sv_lex_cmp(a, b, case_sensitive);
    }

    if (opt->order == LIST_SORT_ORDER_DESCENDING) cmp = -cmp;
    return cmp;
}
