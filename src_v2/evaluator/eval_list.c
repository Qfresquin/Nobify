#include "eval_stdlib.h"

#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pcre2posix.h>

static size_t list_count_items(String_View list_sv) {
    if (list_sv.count == 0) return 0;
    size_t count = 1;
    for (size_t i = 0; i < list_sv.count; i++) {
        if (list_sv.data[i] == ';') count++;
    }
    return count;
}

static bool list_item_in_set(String_View item, String_View *set, size_t set_count) {
    for (size_t i = 0; i < set_count; i++) {
        if (eval_sv_key_eq(item, set[i])) return true;
    }
    return false;
}

static bool list_split_semicolon_preserve_empty(Evaluator_Context *ctx, String_View input, SV_List *out) {
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

static bool sv_parse_i64(String_View sv, long long *out) {
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

typedef enum {
    LIST_SORT_COMPARE_STRING = 0,
    LIST_SORT_COMPARE_FILE_BASENAME,
    LIST_SORT_COMPARE_NATURAL,
} List_Sort_Compare;

typedef enum {
    LIST_SORT_CASE_SENSITIVE = 0,
    LIST_SORT_CASE_INSENSITIVE,
} List_Sort_Case;

typedef enum {
    LIST_SORT_ORDER_ASCENDING = 0,
    LIST_SORT_ORDER_DESCENDING,
} List_Sort_Order;

typedef struct {
    List_Sort_Compare compare;
    List_Sort_Case case_mode;
    List_Sort_Order order;
} List_Sort_Options;

typedef enum {
    LIST_TRANSFORM_APPEND = 0,
    LIST_TRANSFORM_PREPEND,
    LIST_TRANSFORM_TOLOWER,
    LIST_TRANSFORM_TOUPPER,
    LIST_TRANSFORM_STRIP,
    LIST_TRANSFORM_REPLACE,
} List_Transform_Action;

static String_View list_join_items_temp(Evaluator_Context *ctx, String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    return eval_sv_join_semi_temp(ctx, items, count);
}

static bool list_set_var_from_items(Evaluator_Context *ctx, String_View var, String_View *items, size_t count) {
    if (!ctx) return false;
    String_View joined = list_join_items_temp(ctx, items, count);
    if (eval_should_stop(ctx)) return false;
    return eval_var_set(ctx, var, joined);
}

static bool list_load_var_items(Evaluator_Context *ctx, String_View var, SV_List *out) {
    if (!ctx || !out) return false;
    *out = (SV_List){0};
    return list_split_semicolon_preserve_empty(ctx, eval_var_get(ctx, var), out);
}

static bool list_normalize_index(size_t item_count, long long raw_index, bool allow_end, size_t *out_index) {
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

static String_View list_concat_temp(Evaluator_Context *ctx, String_View a, String_View b) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), a.count + b.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (a.count) memcpy(buf, a.data, a.count);
    if (b.count) memcpy(buf + a.count, b.data, b.count);
    buf[a.count + b.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View list_to_case_temp(Evaluator_Context *ctx, String_View in, bool upper) {
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

static String_View list_strip_ws_view(String_View in) {
    size_t b = 0;
    while (b < in.count && isspace((unsigned char)in.data[b])) b++;

    size_t e = in.count;
    while (e > b && isspace((unsigned char)in.data[e - 1])) e--;

    return nob_sv_from_parts(in.data + b, e - b);
}

static bool list_compile_regex(Evaluator_Context *ctx,
                               const Node *node,
                               Cmake_Event_Origin o,
                               String_View pattern,
                               regex_t *out_re) {
    if (!ctx || !node || !out_re) return false;
    char *pat_c = eval_sv_to_cstr_temp(ctx, pattern);
    if (!pat_c) return false;
    if (regcomp(out_re, pat_c, REG_EXTENDED) != 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("list"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Invalid regex pattern"),
                       pattern);
        return false;
    }
    return true;
}

static bool list_regex_replace_one_temp(Evaluator_Context *ctx,
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

static int list_sort_item_cmp(String_View a, String_View b, const List_Sort_Options *opt) {
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

bool eval_handle_list(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return !eval_should_stop(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    if (eval_sv_eq_ci_lit(a.items[0], "APPEND") || eval_sv_eq_ci_lit(a.items[0], "PREPEND")) {
        if (a.count < 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(APPEND/PREPEND) requires list variable name"),
                           nob_sv_from_cstr("Usage: list(APPEND|PREPEND <list> [elements...])"));
            return !eval_should_stop(ctx);
        }

        bool is_append = eval_sv_eq_ci_lit(a.items[0], "APPEND");
        String_View var = a.items[1];
        if (a.count == 2) return !eval_should_stop(ctx);

        String_View existing = eval_var_get(ctx, var);
        String_View incoming = eval_sv_join_semi_temp(ctx, &a.items[2], a.count - 2);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        if (existing.count == 0) {
            (void)eval_var_set(ctx, var, incoming);
            return !eval_should_stop(ctx);
        }

        String_View left = is_append ? existing : incoming;
        String_View right = is_append ? incoming : existing;
        size_t total = left.count + 1 + right.count;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));
        memcpy(buf, left.data, left.count);
        buf[left.count] = ';';
        memcpy(buf + left.count + 1, right.data, right.count);
        buf[total] = '\0';
        (void)eval_var_set(ctx, var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "INSERT")) {
        if (a.count < 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(INSERT) requires list variable, index and at least one element"),
                           nob_sv_from_cstr("Usage: list(INSERT <list> <index> <element> [<element> ...])"));
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        long long raw_index = 0;
        if (!sv_parse_i64(a.items[2], &raw_index) || raw_index < 0) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(INSERT) index must be a non-negative integer"),
                           a.items[2]);
            return !eval_should_stop(ctx);
        }

        SV_List items = {0};
        if (!list_load_var_items(ctx, var, &items)) return !eval_should_stop(ctx);

        if ((size_t)raw_index > items.count) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(INSERT) index out of range"),
                           a.items[2]);
            return !eval_should_stop(ctx);
        }

        size_t index = (size_t)raw_index;
        size_t insert_count = a.count - 3;
        size_t out_count = items.count + insert_count;
        String_View *out_items = arena_alloc_array(eval_temp_arena(ctx), String_View, out_count);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_items, !eval_should_stop(ctx));

        size_t off = 0;
        for (size_t i = 0; i < index; i++) out_items[off++] = items.items[i];
        for (size_t i = 0; i < insert_count; i++) out_items[off++] = a.items[3 + i];
        for (size_t i = index; i < items.count; i++) out_items[off++] = items.items[i];

        (void)list_set_var_from_items(ctx, var, out_items, out_count);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REMOVE_ITEM")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(REMOVE_ITEM) requires list variable and at least one item"),
                           nob_sv_from_cstr("Usage: list(REMOVE_ITEM <list> <item>...)"));
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        String_View current = eval_var_get(ctx, var);
        if (current.count == 0) return !eval_should_stop(ctx);

        size_t keep_count = 0;
        size_t keep_total = 0;

        const char *p = current.data;
        const char *end = current.data + current.count;
        while (p <= end) {
            const char *q = p;
            while (q < end && *q != ';') q++;
            String_View item = nob_sv_from_parts(p, (size_t)(q - p));
            if (!list_item_in_set(item, &a.items[2], a.count - 2)) {
                keep_count++;
                keep_total += item.count;
            }
            if (q == end) break;
            p = q + 1;
        }

        if (keep_count == 0) {
            (void)eval_var_set(ctx, var, nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        size_t total = keep_total + (keep_count - 1);
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));

        size_t off = 0;
        size_t emitted = 0;
        p = current.data;
        while (p <= end) {
            const char *q = p;
            while (q < end && *q != ';') q++;
            String_View item = nob_sv_from_parts(p, (size_t)(q - p));
            if (!list_item_in_set(item, &a.items[2], a.count - 2)) {
                if (emitted > 0) buf[off++] = ';';
                if (item.count) {
                    memcpy(buf + off, item.data, item.count);
                    off += item.count;
                }
                emitted++;
            }
            if (q == end) break;
            p = q + 1;
        }
        buf[off] = '\0';
        (void)eval_var_set(ctx, var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REMOVE_AT")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(REMOVE_AT) requires list variable and at least one index"),
                           nob_sv_from_cstr("Usage: list(REMOVE_AT <list> <index> [<index> ...])"));
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        SV_List items = {0};
        if (!list_load_var_items(ctx, var, &items)) return !eval_should_stop(ctx);

        bool *remove_mask = arena_alloc_array_zero(eval_temp_arena(ctx), bool, items.count);
        if (items.count > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, remove_mask, !eval_should_stop(ctx));
        }

        for (size_t i = 2; i < a.count; i++) {
            long long raw_idx = 0;
            size_t idx = 0;
            if (!sv_parse_i64(a.items[i], &raw_idx) || !list_normalize_index(items.count, raw_idx, false, &idx)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(REMOVE_AT) index out of range"),
                               a.items[i]);
                return !eval_should_stop(ctx);
            }
            remove_mask[idx] = true;
        }

        String_View *out_items = arena_alloc_array(eval_temp_arena(ctx), String_View, items.count);
        if (items.count > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, out_items, !eval_should_stop(ctx));
        }

        size_t out_count = 0;
        for (size_t i = 0; i < items.count; i++) {
            if (!remove_mask[i]) out_items[out_count++] = items.items[i];
        }

        (void)list_set_var_from_items(ctx, var, out_items, out_count);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REMOVE_DUPLICATES")) {
        if (a.count != 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(REMOVE_DUPLICATES) requires only the list variable"),
                           nob_sv_from_cstr("Usage: list(REMOVE_DUPLICATES <list>)"));
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        bool var_defined = eval_var_defined(ctx, var);
        SV_List items = {0};
        if (!list_load_var_items(ctx, var, &items)) return !eval_should_stop(ctx);
        if (items.count == 0 && !var_defined) return !eval_should_stop(ctx);

        String_View *out_items = arena_alloc_array(eval_temp_arena(ctx), String_View, items.count);
        if (items.count > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, out_items, !eval_should_stop(ctx));
        }

        size_t out_count = 0;
        for (size_t i = 0; i < items.count; i++) {
            bool dup = false;
            for (size_t j = 0; j < out_count; j++) {
                if (eval_sv_key_eq(items.items[i], out_items[j])) {
                    dup = true;
                    break;
                }
            }
            if (!dup) out_items[out_count++] = items.items[i];
        }

        (void)list_set_var_from_items(ctx, var, out_items, out_count);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "LENGTH")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(LENGTH) requires list variable and output variable"),
                           nob_sv_from_cstr("Usage: list(LENGTH <list> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View list_var = a.items[1];
        String_View out_var = a.items[2];
        String_View list_value = eval_var_get(ctx, list_var);
        size_t len = list_count_items(list_value);

        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%zu", len);
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(num_buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "GET")) {
        if (a.count < 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(GET) requires list variable, index(es) and output variable"),
                           nob_sv_from_cstr("Usage: list(GET <list> <index> [<index> ...] <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View list_var = a.items[1];
        String_View out_var = a.items[a.count - 1];
        String_View list_value = eval_var_get(ctx, list_var);

        SV_List list_items = {0};
        if (!list_split_semicolon_preserve_empty(ctx, list_value, &list_items)) return !eval_should_stop(ctx);

        SV_List picked = {0};
        for (size_t i = 2; i + 1 < a.count; i++) {
            long long raw_idx = 0;
            if (!sv_parse_i64(a.items[i], &raw_idx)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(GET) index is not a valid integer"),
                               a.items[i]);
                return !eval_should_stop(ctx);
            }

            size_t idx = 0;
            if (!list_normalize_index(list_items.count, raw_idx, false, &idx)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(GET) index out of range"),
                               a.items[i]);
                return !eval_should_stop(ctx);
            }

            if (!svu_list_push_temp(ctx, &picked, list_items.items[idx])) return !eval_should_stop(ctx);
        }

        String_View out = eval_sv_join_semi_temp(ctx, picked.items, picked.count);
        (void)eval_var_set(ctx, out_var, out);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "FIND")) {
        if (a.count != 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(FIND) requires list variable, value and output variable"),
                           nob_sv_from_cstr("Usage: list(FIND <list> <value> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View list_var = a.items[1];
        String_View needle = a.items[2];
        String_View out_var = a.items[3];
        String_View list_value = eval_var_get(ctx, list_var);

        SV_List list_items = {0};
        if (!list_split_semicolon_preserve_empty(ctx, list_value, &list_items)) return !eval_should_stop(ctx);

        long long found = -1;
        for (size_t i = 0; i < list_items.count; i++) {
            if (eval_sv_key_eq(list_items.items[i], needle)) {
                found = (long long)i;
                break;
            }
        }

        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%lld", found);
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(num_buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "JOIN")) {
        if (a.count != 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(JOIN) requires list variable, glue and output variable"),
                           nob_sv_from_cstr("Usage: list(JOIN <list> <glue> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View list_var = a.items[1];
        String_View glue = a.items[2];
        String_View out_var = a.items[3];

        SV_List list_items = {0};
        if (!list_load_var_items(ctx, list_var, &list_items)) return !eval_should_stop(ctx);

        Nob_String_Builder sb = {0};
        for (size_t i = 0; i < list_items.count; i++) {
            if (i > 0 && glue.count > 0) nob_sb_append_buf(&sb, glue.data, glue.count);
            if (list_items.items[i].count > 0) nob_sb_append_buf(&sb, list_items.items[i].data, list_items.items[i].count);
        }

        char *out_buf = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
        if (!out_buf) {
            nob_sb_free(sb);
            EVAL_OOM_RETURN_IF_NULL(ctx, out_buf, !eval_should_stop(ctx));
        }
        if (sb.count) memcpy(out_buf, sb.items, sb.count);
        out_buf[sb.count] = '\0';
        nob_sb_free(sb);

        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(out_buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "SUBLIST")) {
        if (a.count != 5) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(SUBLIST) requires list variable, begin, length and output variable"),
                           nob_sv_from_cstr("Usage: list(SUBLIST <list> <begin> <length> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View list_var = a.items[1];
        String_View out_var = a.items[4];

        long long begin = 0;
        long long length = 0;
        if (!sv_parse_i64(a.items[2], &begin) || !sv_parse_i64(a.items[3], &length)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(SUBLIST) begin/length must be integers"),
                           nob_sv_from_cstr("Use numeric begin and length (length can be -1 for until end)"));
            return !eval_should_stop(ctx);
        }
        if (begin < 0 || length < -1) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(SUBLIST) begin must be >= 0 and length >= -1"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        SV_List list_items = {0};
        if (!list_load_var_items(ctx, list_var, &list_items)) return !eval_should_stop(ctx);

        if ((size_t)begin > list_items.count) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(SUBLIST) begin index out of range"),
                           a.items[2]);
            return !eval_should_stop(ctx);
        }

        size_t start = (size_t)begin;
        if (start == list_items.count || length == 0) {
            (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        size_t end = list_items.count;
        if (length >= 0) {
            size_t len = (size_t)length;
            if (start + len < end) end = start + len;
        }

        String_View out = list_join_items_temp(ctx, &list_items.items[start], end - start);
        (void)eval_var_set(ctx, out_var, out);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "POP_BACK") || eval_sv_eq_ci_lit(a.items[0], "POP_FRONT")) {
        if (a.count < 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(POP_BACK/POP_FRONT) requires list variable"),
                           nob_sv_from_cstr("Usage: list(POP_BACK|POP_FRONT <list> [out-var ...])"));
            return !eval_should_stop(ctx);
        }

        bool pop_front = eval_sv_eq_ci_lit(a.items[0], "POP_FRONT");
        String_View var = a.items[1];
        size_t pop_n = (a.count == 2) ? 1 : (a.count - 2);

        SV_List items = {0};
        if (!list_load_var_items(ctx, var, &items)) return !eval_should_stop(ctx);
        if (items.count < pop_n) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(POP_BACK/POP_FRONT) cannot pop more items than list size"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        for (size_t oi = 0; oi + 2 < a.count; oi++) {
            String_View out_var = a.items[oi + 2];
            String_View popped = pop_front
                ? items.items[oi]
                : items.items[items.count - 1 - oi];
            (void)eval_var_set(ctx, out_var, popped);
        }

        size_t remain_count = items.count - pop_n;
        if (remain_count == 0) {
            (void)eval_var_set(ctx, var, nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        String_View *remain_items = pop_front ? &items.items[pop_n] : items.items;
        (void)list_set_var_from_items(ctx, var, remain_items, remain_count);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "FILTER")) {
        if (a.count != 5) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(FILTER) requires list variable, mode and regex"),
                           nob_sv_from_cstr("Usage: list(FILTER <list> INCLUDE|EXCLUDE REGEX <regex>)"));
            return !eval_should_stop(ctx);
        }
        if (!eval_sv_eq_ci_lit(a.items[3], "REGEX")) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(FILTER) currently supports only REGEX mode"),
                           nob_sv_from_cstr("Usage: list(FILTER <list> INCLUDE|EXCLUDE REGEX <regex>)"));
            return !eval_should_stop(ctx);
        }

        bool include_mode = false;
        if (eval_sv_eq_ci_lit(a.items[2], "INCLUDE")) {
            include_mode = true;
        } else if (eval_sv_eq_ci_lit(a.items[2], "EXCLUDE")) {
            include_mode = false;
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(FILTER) mode must be INCLUDE or EXCLUDE"),
                           a.items[2]);
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        bool var_defined = eval_var_defined(ctx, var);
        SV_List items = {0};
        if (!list_load_var_items(ctx, var, &items)) return !eval_should_stop(ctx);
        if (items.count == 0 && !var_defined) return !eval_should_stop(ctx);

        regex_t re;
        if (!list_compile_regex(ctx, node, o, a.items[4], &re)) return !eval_should_stop(ctx);

        String_View *out_items = arena_alloc_array(eval_temp_arena(ctx), String_View, items.count);
        if (items.count > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, out_items, !eval_should_stop(ctx));
        }
        size_t out_count = 0;

        for (size_t i = 0; i < items.count; i++) {
            char *item_c = eval_sv_to_cstr_temp(ctx, items.items[i]);
            if (!item_c) {
                regfree(&re);
                return !eval_should_stop(ctx);
            }
            bool match = regexec(&re, item_c, 0, NULL, 0) == 0;
            if ((include_mode && match) || (!include_mode && !match)) {
                out_items[out_count++] = items.items[i];
            }
        }
        regfree(&re);

        (void)list_set_var_from_items(ctx, var, out_items, out_count);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REVERSE")) {
        if (a.count != 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(REVERSE) requires only the list variable"),
                           nob_sv_from_cstr("Usage: list(REVERSE <list>)"));
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        bool var_defined = eval_var_defined(ctx, var);
        SV_List items = {0};
        if (!list_load_var_items(ctx, var, &items)) return !eval_should_stop(ctx);
        if (items.count == 0 && !var_defined) return !eval_should_stop(ctx);

        for (size_t i = 0; i < items.count / 2; i++) {
            String_View tmp = items.items[i];
            items.items[i] = items.items[items.count - 1 - i];
            items.items[items.count - 1 - i] = tmp;
        }
        (void)list_set_var_from_items(ctx, var, items.items, items.count);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "SORT")) {
        if (a.count < 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(SORT) requires list variable"),
                           nob_sv_from_cstr("Usage: list(SORT <list> [COMPARE <mode>] [CASE <mode>] [ORDER <mode>])"));
            return !eval_should_stop(ctx);
        }

        List_Sort_Options opt = {
            .compare = LIST_SORT_COMPARE_STRING,
            .case_mode = LIST_SORT_CASE_SENSITIVE,
            .order = LIST_SORT_ORDER_ASCENDING,
        };

        for (size_t i = 2; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "COMPARE")) {
                if (i + 1 >= a.count) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("list(SORT COMPARE) missing value"),
                                   nob_sv_from_cstr("Expected STRING, FILE_BASENAME or NATURAL"));
                    return !eval_should_stop(ctx);
                }
                i++;
                if (eval_sv_eq_ci_lit(a.items[i], "STRING")) opt.compare = LIST_SORT_COMPARE_STRING;
                else if (eval_sv_eq_ci_lit(a.items[i], "FILE_BASENAME")) opt.compare = LIST_SORT_COMPARE_FILE_BASENAME;
                else if (eval_sv_eq_ci_lit(a.items[i], "NATURAL")) opt.compare = LIST_SORT_COMPARE_NATURAL;
                else {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("list(SORT COMPARE) received unsupported value"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                continue;
            }

            if (eval_sv_eq_ci_lit(a.items[i], "CASE")) {
                if (i + 1 >= a.count) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("list(SORT CASE) missing value"),
                                   nob_sv_from_cstr("Expected SENSITIVE or INSENSITIVE"));
                    return !eval_should_stop(ctx);
                }
                i++;
                if (eval_sv_eq_ci_lit(a.items[i], "SENSITIVE")) opt.case_mode = LIST_SORT_CASE_SENSITIVE;
                else if (eval_sv_eq_ci_lit(a.items[i], "INSENSITIVE")) opt.case_mode = LIST_SORT_CASE_INSENSITIVE;
                else {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("list(SORT CASE) received unsupported value"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                continue;
            }

            if (eval_sv_eq_ci_lit(a.items[i], "ORDER")) {
                if (i + 1 >= a.count) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("list(SORT ORDER) missing value"),
                                   nob_sv_from_cstr("Expected ASCENDING or DESCENDING"));
                    return !eval_should_stop(ctx);
                }
                i++;
                if (eval_sv_eq_ci_lit(a.items[i], "ASCENDING")) opt.order = LIST_SORT_ORDER_ASCENDING;
                else if (eval_sv_eq_ci_lit(a.items[i], "DESCENDING")) opt.order = LIST_SORT_ORDER_DESCENDING;
                else {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("list(SORT ORDER) received unsupported value"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                continue;
            }

            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(SORT) received unsupported option token"),
                           a.items[i]);
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        bool var_defined = eval_var_defined(ctx, var);
        SV_List items = {0};
        if (!list_load_var_items(ctx, var, &items)) return !eval_should_stop(ctx);
        if (items.count == 0 && !var_defined) return !eval_should_stop(ctx);

        for (size_t i = 1; i < items.count; i++) {
            String_View key = items.items[i];
            size_t j = i;
            while (j > 0 && list_sort_item_cmp(items.items[j - 1], key, &opt) > 0) {
                items.items[j] = items.items[j - 1];
                j--;
            }
            items.items[j] = key;
        }

        (void)list_set_var_from_items(ctx, var, items.items, items.count);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "TRANSFORM")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(TRANSFORM) requires list variable and action"),
                           nob_sv_from_cstr("Usage: list(TRANSFORM <list> <ACTION> ... [AT|FOR|REGEX selector])"));
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        bool var_defined = eval_var_defined(ctx, var);
        SV_List items = {0};
        if (!list_load_var_items(ctx, var, &items)) return !eval_should_stop(ctx);

        List_Transform_Action action = LIST_TRANSFORM_TOLOWER;
        size_t next = 3;
        String_View action_arg1 = nob_sv_from_cstr("");
        String_View action_arg2 = nob_sv_from_cstr("");

        if (eval_sv_eq_ci_lit(a.items[2], "APPEND")) {
            action = LIST_TRANSFORM_APPEND;
            if (next >= a.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM APPEND) missing suffix"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            action_arg1 = a.items[next++];
        } else if (eval_sv_eq_ci_lit(a.items[2], "PREPEND")) {
            action = LIST_TRANSFORM_PREPEND;
            if (next >= a.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM PREPEND) missing prefix"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            action_arg1 = a.items[next++];
        } else if (eval_sv_eq_ci_lit(a.items[2], "TOLOWER")) {
            action = LIST_TRANSFORM_TOLOWER;
        } else if (eval_sv_eq_ci_lit(a.items[2], "TOUPPER")) {
            action = LIST_TRANSFORM_TOUPPER;
        } else if (eval_sv_eq_ci_lit(a.items[2], "STRIP")) {
            action = LIST_TRANSFORM_STRIP;
        } else if (eval_sv_eq_ci_lit(a.items[2], "REPLACE")) {
            action = LIST_TRANSFORM_REPLACE;
            if (next + 1 >= a.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM REPLACE) requires regex and replacement"),
                               nob_sv_from_cstr("Usage: list(TRANSFORM <list> REPLACE <regex> <replace> [selector])"));
                return !eval_should_stop(ctx);
            }
            action_arg1 = a.items[next++];
            action_arg2 = a.items[next++];
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("Unsupported list(TRANSFORM) action"),
                           a.items[2]);
            return !eval_should_stop(ctx);
        }

        bool *selected = arena_alloc_array_zero(eval_temp_arena(ctx), bool, items.count);
        if (items.count > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, selected, !eval_should_stop(ctx));
        }

        if (next == a.count) {
            for (size_t i = 0; i < items.count; i++) selected[i] = true;
        } else if (eval_sv_eq_ci_lit(a.items[next], "AT")) {
            if (next + 1 >= a.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM AT) requires at least one index"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            for (size_t i = next + 1; i < a.count; i++) {
                long long raw_idx = 0;
                size_t idx = 0;
                if (!sv_parse_i64(a.items[i], &raw_idx) || !list_normalize_index(items.count, raw_idx, false, &idx)) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("list(TRANSFORM AT) index out of range"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                selected[idx] = true;
            }
        } else if (eval_sv_eq_ci_lit(a.items[next], "FOR")) {
            if (a.count < next + 3 || a.count > next + 4) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM FOR) expects start stop [step]"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            long long start = 0;
            long long stop = 0;
            long long step = 1;
            if (!sv_parse_i64(a.items[next + 1], &start) || !sv_parse_i64(a.items[next + 2], &stop)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM FOR) start/stop must be integers"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            if (a.count == next + 4 && !sv_parse_i64(a.items[next + 3], &step)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM FOR) step must be an integer"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            if (start < 0 || stop < 0 || step <= 0 || start > stop) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM FOR) expects non-negative range with positive step"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            if (items.count > 0 && ((size_t)start >= items.count || (size_t)stop >= items.count)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM FOR) range out of bounds"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            for (long long i = start; i <= stop && items.count > 0; i += step) {
                selected[(size_t)i] = true;
            }
        } else if (eval_sv_eq_ci_lit(a.items[next], "REGEX")) {
            if (a.count != next + 2) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                               nob_sv_from_cstr("list(TRANSFORM REGEX) expects exactly one regex argument"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            regex_t sel_re;
            if (!list_compile_regex(ctx, node, o, a.items[next + 1], &sel_re)) return !eval_should_stop(ctx);
            for (size_t i = 0; i < items.count; i++) {
                char *item_c = eval_sv_to_cstr_temp(ctx, items.items[i]);
                if (!item_c) {
                    regfree(&sel_re);
                    return !eval_should_stop(ctx);
                }
                if (regexec(&sel_re, item_c, 0, NULL, 0) == 0) selected[i] = true;
            }
            regfree(&sel_re);
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(TRANSFORM) received unsupported selector"),
                           a.items[next]);
            return !eval_should_stop(ctx);
        }

        regex_t replace_re;
        bool replace_ready = false;
        if (action == LIST_TRANSFORM_REPLACE) {
            if (!list_compile_regex(ctx, node, o, action_arg1, &replace_re)) return !eval_should_stop(ctx);
            replace_ready = true;
        }

        for (size_t i = 0; i < items.count; i++) {
            if (!selected[i]) continue;
            String_View curr = items.items[i];
            if (action == LIST_TRANSFORM_APPEND) {
                curr = list_concat_temp(ctx, curr, action_arg1);
            } else if (action == LIST_TRANSFORM_PREPEND) {
                curr = list_concat_temp(ctx, action_arg1, curr);
            } else if (action == LIST_TRANSFORM_TOLOWER) {
                curr = list_to_case_temp(ctx, curr, false);
            } else if (action == LIST_TRANSFORM_TOUPPER) {
                curr = list_to_case_temp(ctx, curr, true);
            } else if (action == LIST_TRANSFORM_STRIP) {
                curr = list_strip_ws_view(curr);
            } else if (action == LIST_TRANSFORM_REPLACE) {
                if (!list_regex_replace_one_temp(ctx, &replace_re, action_arg2, curr, &curr)) {
                    if (replace_ready) regfree(&replace_re);
                    return !eval_should_stop(ctx);
                }
            }
            if (eval_should_stop(ctx)) {
                if (replace_ready) regfree(&replace_re);
                return !eval_should_stop(ctx);
            }
            items.items[i] = curr;
        }

        if (replace_ready) regfree(&replace_re);
        if (items.count == 0 && !var_defined) return !eval_should_stop(ctx);
        (void)list_set_var_from_items(ctx, var, items.items, items.count);
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                   nob_sv_from_cstr("Unsupported list() subcommand"),
                   nob_sv_from_cstr("Implemented: APPEND, PREPEND, INSERT, REMOVE_ITEM, REMOVE_AT, REMOVE_DUPLICATES, LENGTH, GET, FIND, JOIN, SUBLIST, POP_BACK, POP_FRONT, FILTER, TRANSFORM, REVERSE, SORT"));
    eval_request_stop_on_error(ctx);
    return !eval_should_stop(ctx);
}
