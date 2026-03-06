#include "eval_stdlib.h"
#include "eval_list_internal.h"

#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum {
    LIST_TRANSFORM_APPEND = 0,
    LIST_TRANSFORM_PREPEND,
    LIST_TRANSFORM_TOLOWER,
    LIST_TRANSFORM_TOUPPER,
    LIST_TRANSFORM_STRIP,
    LIST_TRANSFORM_GENEX_STRIP,
    LIST_TRANSFORM_REPLACE,
} List_Transform_Action;

Eval_Result eval_handle_list(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || arena_arr_len(a) < 1) return eval_result_from_ctx(ctx);

    if (eval_sv_eq_ci_lit(a[0], "APPEND") || eval_sv_eq_ci_lit(a[0], "PREPEND")) {
        if (arena_arr_len(a) < 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(APPEND/PREPEND) requires list variable name"), nob_sv_from_cstr("Usage: list(APPEND|PREPEND <list> [elements...])"));
            return eval_result_from_ctx(ctx);
        }

        bool is_append = eval_sv_eq_ci_lit(a[0], "APPEND");
        String_View var = a[1];
        if (arena_arr_len(a) == 2) return eval_result_from_ctx(ctx);

        String_View existing = eval_var_get_visible(ctx, var);
        String_View incoming = eval_sv_join_semi_temp(ctx, &a[2], arena_arr_len(a) - 2);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        if (existing.count == 0) {
            (void)eval_var_set_current(ctx, var, incoming);
            if (!(is_append ? eval_emit_list_append(ctx, o, var) : eval_emit_list_prepend(ctx, o, var))) return eval_result_fatal();
            return eval_result_from_ctx(ctx);
        }

        String_View left = is_append ? existing : incoming;
        String_View right = is_append ? incoming : existing;
        size_t total = left.count + 1 + right.count;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
        memcpy(buf, left.data, left.count);
        buf[left.count] = ';';
        memcpy(buf + left.count + 1, right.data, right.count);
        buf[total] = '\0';
        (void)eval_var_set_current(ctx, var, nob_sv_from_cstr(buf));
        if (!(is_append ? eval_emit_list_append(ctx, o, var) : eval_emit_list_prepend(ctx, o, var))) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "INSERT")) {
        if (arena_arr_len(a) < 4) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(INSERT) requires list variable, index and at least one element"), nob_sv_from_cstr("Usage: list(INSERT <list> <index> <element> [<element> ...])"));
            return eval_result_from_ctx(ctx);
        }

        String_View var = a[1];
        long long raw_index = 0;
        if (!list_sv_parse_i64(a[2], &raw_index) || raw_index < 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "list", nob_sv_from_cstr("list(INSERT) index must be a non-negative integer"), a[2]);
            return eval_result_from_ctx(ctx);
        }

        SV_List items = NULL;
        if (!list_load_var_items(ctx, var, &items)) return eval_result_from_ctx(ctx);

        if ((size_t)raw_index > arena_arr_len(items)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "list", nob_sv_from_cstr("list(INSERT) index out of range"), a[2]);
            return eval_result_from_ctx(ctx);
        }

        size_t index = (size_t)raw_index;
        size_t insert_count = arena_arr_len(a) - 3;
        size_t out_count = arena_arr_len(items) + insert_count;
        String_View *out_items = arena_alloc_array(eval_temp_arena(ctx), String_View, out_count);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_items, eval_result_fatal());

        size_t off = 0;
        for (size_t i = 0; i < index; i++) out_items[off++] = items[i];
        for (size_t i = 0; i < insert_count; i++) out_items[off++] = a[3 + i];
        for (size_t i = index; i < arena_arr_len(items); i++) out_items[off++] = items[i];

        (void)list_set_var_from_items(ctx, var, out_items, out_count);
        if (!eval_emit_list_insert(ctx, o, var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "REMOVE_ITEM")) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(REMOVE_ITEM) requires list variable and at least one item"), nob_sv_from_cstr("Usage: list(REMOVE_ITEM <list> <item>...)"));
            return eval_result_from_ctx(ctx);
        }

        String_View var = a[1];
        String_View current = eval_var_get_visible(ctx, var);
        if (current.count == 0) return eval_result_from_ctx(ctx);

        size_t keep_count = 0;
        size_t keep_total = 0;

        const char *p = current.data;
        const char *end = current.data + current.count;
        while (p <= end) {
            const char *q = p;
            while (q < end && *q != ';') q++;
            String_View item = nob_sv_from_parts(p, (size_t)(q - p));
            if (!list_item_in_set(item, &a[2], arena_arr_len(a) - 2)) {
                keep_count++;
                keep_total += item.count;
            }
            if (q == end) break;
            p = q + 1;
        }

        if (keep_count == 0) {
            (void)eval_var_set_current(ctx, var, nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        size_t total = keep_total + (keep_count - 1);
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());

        size_t off = 0;
        size_t emitted = 0;
        p = current.data;
        while (p <= end) {
            const char *q = p;
            while (q < end && *q != ';') q++;
            String_View item = nob_sv_from_parts(p, (size_t)(q - p));
            if (!list_item_in_set(item, &a[2], arena_arr_len(a) - 2)) {
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
        (void)eval_var_set_current(ctx, var, nob_sv_from_cstr(buf));
        if (!eval_emit_list_remove(ctx, o, var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "REMOVE_AT")) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(REMOVE_AT) requires list variable and at least one index"), nob_sv_from_cstr("Usage: list(REMOVE_AT <list> <index> [<index> ...])"));
            return eval_result_from_ctx(ctx);
        }

        String_View var = a[1];
        SV_List items = NULL;
        if (!list_load_var_items(ctx, var, &items)) return eval_result_from_ctx(ctx);

        bool *remove_mask = arena_alloc_array_zero(eval_temp_arena(ctx), bool, arena_arr_len(items));
        if (arena_arr_len(items) > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, remove_mask, eval_result_fatal());
        }

        for (size_t i = 2; i < arena_arr_len(a); i++) {
            long long raw_idx = 0;
            size_t idx = 0;
            if (!list_sv_parse_i64(a[i], &raw_idx) || !list_normalize_index(arena_arr_len(items), raw_idx, false, &idx)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "list", nob_sv_from_cstr("list(REMOVE_AT) index out of range"), a[i]);
                return eval_result_from_ctx(ctx);
            }
            remove_mask[idx] = true;
        }

        String_View *out_items = arena_alloc_array(eval_temp_arena(ctx), String_View, arena_arr_len(items));
        if (arena_arr_len(items) > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, out_items, eval_result_fatal());
        }

        size_t out_count = 0;
        for (size_t i = 0; i < arena_arr_len(items); i++) {
            if (!remove_mask[i]) out_items[out_count++] = items[i];
        }

        (void)list_set_var_from_items(ctx, var, out_items, out_count);
        if (!eval_emit_list_remove(ctx, o, var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "REMOVE_DUPLICATES")) {
        if (arena_arr_len(a) != 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_DUPLICATE_ARGUMENT, "list", nob_sv_from_cstr("list(REMOVE_DUPLICATES) requires only the list variable"), nob_sv_from_cstr("Usage: list(REMOVE_DUPLICATES <list>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View var = a[1];
        bool var_defined = eval_var_defined_visible(ctx, var);
        SV_List items = NULL;
        if (!list_load_var_items(ctx, var, &items)) return eval_result_from_ctx(ctx);
        if (arena_arr_len(items) == 0 && !var_defined) return eval_result_from_ctx(ctx);

        String_View *out_items = arena_alloc_array(eval_temp_arena(ctx), String_View, arena_arr_len(items));
        if (arena_arr_len(items) > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, out_items, eval_result_fatal());
        }

        size_t out_count = 0;
        for (size_t i = 0; i < arena_arr_len(items); i++) {
            bool dup = false;
            for (size_t j = 0; j < out_count; j++) {
                if (eval_sv_key_eq(items[i], out_items[j])) {
                    dup = true;
                    break;
                }
            }
            if (!dup) out_items[out_count++] = items[i];
        }

        (void)list_set_var_from_items(ctx, var, out_items, out_count);
        if (!eval_emit_list_remove(ctx, o, var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "LENGTH")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(LENGTH) requires list variable and output variable"), nob_sv_from_cstr("Usage: list(LENGTH <list> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View list_var = a[1];
        String_View out_var = a[2];
        String_View list_value = eval_var_get_visible(ctx, list_var);
        size_t len = list_count_items(list_value);

        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%zu", len);
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(num_buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "GET")) {
        if (arena_arr_len(a) < 4) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(GET) requires list variable, index(es) and output variable"), nob_sv_from_cstr("Usage: list(GET <list> <index> [<index> ...] <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View list_var = a[1];
        String_View out_var = a[arena_arr_len(a) - 1];
        String_View list_value = eval_var_get_visible(ctx, list_var);

        SV_List list_items = NULL;
        if (!list_split_semicolon_preserve_empty(ctx, list_value, &list_items)) return eval_result_from_ctx(ctx);

        SV_List picked = NULL;
        for (size_t i = 2; i + 1 < arena_arr_len(a); i++) {
            long long raw_idx = 0;
            if (!list_sv_parse_i64(a[i], &raw_idx)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "list", nob_sv_from_cstr("list(GET) index is not a valid integer"), a[i]);
                return eval_result_from_ctx(ctx);
            }

            size_t idx = 0;
            if (!list_normalize_index(arena_arr_len(list_items), raw_idx, false, &idx)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "list", nob_sv_from_cstr("list(GET) index out of range"), a[i]);
                return eval_result_from_ctx(ctx);
            }

            if (!svu_list_push_temp(ctx, &picked, list_items[idx])) return eval_result_from_ctx(ctx);
        }

        String_View out = eval_sv_join_semi_temp(ctx, picked, arena_arr_len(picked));
        (void)eval_var_set_current(ctx, out_var, out);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "FIND")) {
        if (arena_arr_len(a) != 4) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(FIND) requires list variable, value and output variable"), nob_sv_from_cstr("Usage: list(FIND <list> <value> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View list_var = a[1];
        String_View needle = a[2];
        String_View out_var = a[3];
        String_View list_value = eval_var_get_visible(ctx, list_var);

        SV_List list_items = NULL;
        if (!list_split_semicolon_preserve_empty(ctx, list_value, &list_items)) return eval_result_from_ctx(ctx);

        long long found = -1;
        for (size_t i = 0; i < arena_arr_len(list_items); i++) {
            if (eval_sv_key_eq(list_items[i], needle)) {
                found = (long long)i;
                break;
            }
        }

        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%lld", found);
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(num_buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "JOIN")) {
        if (arena_arr_len(a) != 4) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(JOIN) requires list variable, glue and output variable"), nob_sv_from_cstr("Usage: list(JOIN <list> <glue> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View list_var = a[1];
        String_View glue = a[2];
        String_View out_var = a[3];

        SV_List list_items = NULL;
        if (!list_load_var_items(ctx, list_var, &list_items)) return eval_result_from_ctx(ctx);

        Nob_String_Builder sb = {0};
        for (size_t i = 0; i < arena_arr_len(list_items); i++) {
            if (i > 0 && glue.count > 0) nob_sb_append_buf(&sb, glue.data, glue.count);
            if (list_items[i].count > 0) nob_sb_append_buf(&sb, list_items[i].data, list_items[i].count);
        }

        char *out_buf = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
        if (!out_buf) {
            nob_sb_free(sb);
            EVAL_OOM_RETURN_IF_NULL(ctx, out_buf, eval_result_fatal());
        }
        if (sb.count) memcpy(out_buf, sb.items, sb.count);
        out_buf[sb.count] = '\0';
        nob_sb_free(sb);

        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(out_buf));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "SUBLIST")) {
        if (arena_arr_len(a) != 5) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(SUBLIST) requires list variable, begin, length and output variable"), nob_sv_from_cstr("Usage: list(SUBLIST <list> <begin> <length> <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View list_var = a[1];
        String_View out_var = a[4];

        long long begin = 0;
        long long length = 0;
        if (!list_sv_parse_i64(a[2], &begin) || !list_sv_parse_i64(a[3], &length)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "list", nob_sv_from_cstr("list(SUBLIST) begin/length must be integers"), nob_sv_from_cstr("Use numeric begin and length (length can be -1 for until end)"));
            return eval_result_from_ctx(ctx);
        }
        if (begin < 0 || length < -1) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "list", nob_sv_from_cstr("list(SUBLIST) begin must be >= 0 and length >= -1"), nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        SV_List list_items = NULL;
        if (!list_load_var_items(ctx, list_var, &list_items)) return eval_result_from_ctx(ctx);

        if ((size_t)begin > arena_arr_len(list_items)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "list", nob_sv_from_cstr("list(SUBLIST) begin index out of range"), a[2]);
            return eval_result_from_ctx(ctx);
        }

        size_t start = (size_t)begin;
        if (start == arena_arr_len(list_items) || length == 0) {
            (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        size_t end = arena_arr_len(list_items);
        if (length >= 0) {
            size_t len = (size_t)length;
            if (start + len < end) end = start + len;
        }

        String_View out = list_join_items_temp(ctx, &list_items[start], end - start);
        (void)eval_var_set_current(ctx, out_var, out);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "POP_BACK") || eval_sv_eq_ci_lit(a[0], "POP_FRONT")) {
        if (arena_arr_len(a) < 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(POP_BACK/POP_FRONT) requires list variable"), nob_sv_from_cstr("Usage: list(POP_BACK|POP_FRONT <list> [out-var ...])"));
            return eval_result_from_ctx(ctx);
        }

        bool pop_front = eval_sv_eq_ci_lit(a[0], "POP_FRONT");
        String_View var = a[1];
        size_t pop_n = (arena_arr_len(a) == 2) ? 1 : (arena_arr_len(a) - 2);

        SV_List items = NULL;
        if (!list_load_var_items(ctx, var, &items)) return eval_result_from_ctx(ctx);
        if (arena_arr_len(items) < pop_n) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "list", nob_sv_from_cstr("list(POP_BACK/POP_FRONT) cannot pop more items than list size"), nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        for (size_t oi = 0; oi + 2 < arena_arr_len(a); oi++) {
            String_View out_var = a[oi + 2];
            String_View popped = pop_front
                ? items[oi]
                : items[arena_arr_len(items) - 1 - oi];
            (void)eval_var_set_current(ctx, out_var, popped);
        }

        size_t remain_count = arena_arr_len(items) - pop_n;
        if (remain_count == 0) {
            (void)eval_var_set_current(ctx, var, nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        String_View *remain_items = pop_front ? &items[pop_n] : items;
        (void)list_set_var_from_items(ctx, var, remain_items, remain_count);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "FILTER")) {
        if (arena_arr_len(a) != 5) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(FILTER) requires list variable, mode and regex"), nob_sv_from_cstr("Usage: list(FILTER <list> INCLUDE|EXCLUDE REGEX <regex>)"));
            return eval_result_from_ctx(ctx);
        }
        if (!eval_sv_eq_ci_lit(a[3], "REGEX")) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "list", nob_sv_from_cstr("list(FILTER) currently supports only REGEX mode"), nob_sv_from_cstr("Usage: list(FILTER <list> INCLUDE|EXCLUDE REGEX <regex>)"));
            return eval_result_from_ctx(ctx);
        }

        bool include_mode = false;
        if (eval_sv_eq_ci_lit(a[2], "INCLUDE")) {
            include_mode = true;
        } else if (eval_sv_eq_ci_lit(a[2], "EXCLUDE")) {
            include_mode = false;
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "list", nob_sv_from_cstr("list(FILTER) mode must be INCLUDE or EXCLUDE"), a[2]);
            return eval_result_from_ctx(ctx);
        }

        String_View var = a[1];
        bool var_defined = eval_var_defined_visible(ctx, var);
        SV_List items = NULL;
        if (!list_load_var_items(ctx, var, &items)) return eval_result_from_ctx(ctx);
        if (arena_arr_len(items) == 0 && !var_defined) return eval_result_from_ctx(ctx);

        regex_t re;
        if (!list_compile_regex(ctx, node, o, a[4], &re)) return eval_result_from_ctx(ctx);

        String_View *out_items = arena_alloc_array(eval_temp_arena(ctx), String_View, arena_arr_len(items));
        if (arena_arr_len(items) > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, out_items, eval_result_fatal());
        }
        size_t out_count = 0;

        for (size_t i = 0; i < arena_arr_len(items); i++) {
            char *item_c = eval_sv_to_cstr_temp(ctx, items[i]);
            if (!item_c) {
                regfree(&re);
                return eval_result_from_ctx(ctx);
            }
            bool match = regexec(&re, item_c, 0, NULL, 0) == 0;
            if ((include_mode && match) || (!include_mode && !match)) {
                out_items[out_count++] = items[i];
            }
        }
        regfree(&re);

        (void)list_set_var_from_items(ctx, var, out_items, out_count);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "REVERSE")) {
        if (arena_arr_len(a) != 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(REVERSE) requires only the list variable"), nob_sv_from_cstr("Usage: list(REVERSE <list>)"));
            return eval_result_from_ctx(ctx);
        }

        String_View var = a[1];
        bool var_defined = eval_var_defined_visible(ctx, var);
        SV_List items = NULL;
        if (!list_load_var_items(ctx, var, &items)) return eval_result_from_ctx(ctx);
        if (arena_arr_len(items) == 0 && !var_defined) return eval_result_from_ctx(ctx);

        for (size_t i = 0; i < arena_arr_len(items) / 2; i++) {
            String_View tmp = items[i];
            items[i] = items[arena_arr_len(items) - 1 - i];
            items[arena_arr_len(items) - 1 - i] = tmp;
        }
        (void)list_set_var_from_items(ctx, var, items, arena_arr_len(items));
        if (!eval_emit_list_sort(ctx, o, var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "SORT")) {
        if (arena_arr_len(a) < 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(SORT) requires list variable"), nob_sv_from_cstr("Usage: list(SORT <list> [COMPARE <mode>] [CASE <mode>] [ORDER <mode>])"));
            return eval_result_from_ctx(ctx);
        }

        List_Sort_Options opt = {
            .compare = LIST_SORT_COMPARE_STRING,
            .case_mode = LIST_SORT_CASE_SENSITIVE,
            .order = LIST_SORT_ORDER_ASCENDING,
        };

        for (size_t i = 2; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "COMPARE")) {
                if (i + 1 >= arena_arr_len(a)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(SORT COMPARE) missing value"), nob_sv_from_cstr("Expected STRING, FILE_BASENAME or NATURAL"));
                    return eval_result_from_ctx(ctx);
                }
                i++;
                if (eval_sv_eq_ci_lit(a[i], "STRING")) opt.compare = LIST_SORT_COMPARE_STRING;
                else if (eval_sv_eq_ci_lit(a[i], "FILE_BASENAME")) opt.compare = LIST_SORT_COMPARE_FILE_BASENAME;
                else if (eval_sv_eq_ci_lit(a[i], "NATURAL")) opt.compare = LIST_SORT_COMPARE_NATURAL;
                else {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "list", nob_sv_from_cstr("list(SORT COMPARE) received unsupported value"), a[i]);
                    return eval_result_from_ctx(ctx);
                }
                continue;
            }

            if (eval_sv_eq_ci_lit(a[i], "CASE")) {
                if (i + 1 >= arena_arr_len(a)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(SORT CASE) missing value"), nob_sv_from_cstr("Expected SENSITIVE or INSENSITIVE"));
                    return eval_result_from_ctx(ctx);
                }
                i++;
                if (eval_sv_eq_ci_lit(a[i], "SENSITIVE")) opt.case_mode = LIST_SORT_CASE_SENSITIVE;
                else if (eval_sv_eq_ci_lit(a[i], "INSENSITIVE")) opt.case_mode = LIST_SORT_CASE_INSENSITIVE;
                else {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "list", nob_sv_from_cstr("list(SORT CASE) received unsupported value"), a[i]);
                    return eval_result_from_ctx(ctx);
                }
                continue;
            }

            if (eval_sv_eq_ci_lit(a[i], "ORDER")) {
                if (i + 1 >= arena_arr_len(a)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(SORT ORDER) missing value"), nob_sv_from_cstr("Expected ASCENDING or DESCENDING"));
                    return eval_result_from_ctx(ctx);
                }
                i++;
                if (eval_sv_eq_ci_lit(a[i], "ASCENDING")) opt.order = LIST_SORT_ORDER_ASCENDING;
                else if (eval_sv_eq_ci_lit(a[i], "DESCENDING")) opt.order = LIST_SORT_ORDER_DESCENDING;
                else {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "list", nob_sv_from_cstr("list(SORT ORDER) received unsupported value"), a[i]);
                    return eval_result_from_ctx(ctx);
                }
                continue;
            }

            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "list", nob_sv_from_cstr("list(SORT) received unsupported option token"), a[i]);
            return eval_result_from_ctx(ctx);
        }

        String_View var = a[1];
        bool var_defined = eval_var_defined_visible(ctx, var);
        SV_List items = NULL;
        if (!list_load_var_items(ctx, var, &items)) return eval_result_from_ctx(ctx);
        if (arena_arr_len(items) == 0 && !var_defined) return eval_result_from_ctx(ctx);

        for (size_t i = 1; i < arena_arr_len(items); i++) {
            String_View key = items[i];
            size_t j = i;
            while (j > 0 && list_sort_item_cmp(items[j - 1], key, &opt) > 0) {
                items[j] = items[j - 1];
                j--;
            }
            items[j] = key;
        }

        (void)list_set_var_from_items(ctx, var, items, arena_arr_len(items));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "TRANSFORM")) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM) requires list variable and action"), nob_sv_from_cstr("Usage: list(TRANSFORM <list> <ACTION> [selector] [OUTPUT_VARIABLE <out-var>])"));
            return eval_result_from_ctx(ctx);
        }

        String_View var = a[1];
        bool var_defined = eval_var_defined_visible(ctx, var);
        SV_List items = NULL;
        if (!list_load_var_items(ctx, var, &items)) return eval_result_from_ctx(ctx);

        List_Transform_Action action = LIST_TRANSFORM_TOLOWER;
        size_t next = 3;
        String_View action_arg1 = nob_sv_from_cstr("");
        String_View action_arg2 = nob_sv_from_cstr("");

        if (eval_sv_eq_ci_lit(a[2], "APPEND")) {
            action = LIST_TRANSFORM_APPEND;
            if (next >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM APPEND) missing suffix"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            action_arg1 = a[next++];
        } else if (eval_sv_eq_ci_lit(a[2], "PREPEND")) {
            action = LIST_TRANSFORM_PREPEND;
            if (next >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM PREPEND) missing prefix"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            action_arg1 = a[next++];
        } else if (eval_sv_eq_ci_lit(a[2], "TOLOWER")) {
            action = LIST_TRANSFORM_TOLOWER;
        } else if (eval_sv_eq_ci_lit(a[2], "TOUPPER")) {
            action = LIST_TRANSFORM_TOUPPER;
        } else if (eval_sv_eq_ci_lit(a[2], "STRIP")) {
            action = LIST_TRANSFORM_STRIP;
        } else if (eval_sv_eq_ci_lit(a[2], "GENEX_STRIP")) {
            action = LIST_TRANSFORM_GENEX_STRIP;
        } else if (eval_sv_eq_ci_lit(a[2], "REPLACE")) {
            action = LIST_TRANSFORM_REPLACE;
            if (next + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM REPLACE) requires regex and replacement"), nob_sv_from_cstr("Usage: list(TRANSFORM <list> REPLACE <regex> <replace> [selector] [OUTPUT_VARIABLE <out-var>])"));
                return eval_result_from_ctx(ctx);
            }
            action_arg1 = a[next++];
            action_arg2 = a[next++];
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "list", nob_sv_from_cstr("Unsupported list(TRANSFORM) action"), a[2]);
            return eval_result_from_ctx(ctx);
        }

        bool *selected = arena_alloc_array_zero(eval_temp_arena(ctx), bool, arena_arr_len(items));
        if (arena_arr_len(items) > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, selected, eval_result_fatal());
        }

        String_View out_var = nob_sv_from_cstr("");
        bool has_output_var = false;

        if (next == arena_arr_len(a)) {
            for (size_t i = 0; i < arena_arr_len(items); i++) selected[i] = true;
        } else if (eval_sv_eq_ci_lit(a[next], "OUTPUT_VARIABLE")) {
            if (next + 2 != arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM OUTPUT_VARIABLE) expects exactly one output variable"), nob_sv_from_cstr("Usage: list(TRANSFORM <list> <ACTION> [selector] [OUTPUT_VARIABLE <out-var>])"));
                return eval_result_from_ctx(ctx);
            }
            has_output_var = true;
            out_var = a[next + 1];
            for (size_t i = 0; i < arena_arr_len(items); i++) selected[i] = true;
        } else if (eval_sv_eq_ci_lit(a[next], "AT")) {
            size_t end = arena_arr_len(a);
            for (size_t i = next + 1; i < arena_arr_len(a); i++) {
                if (!eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) continue;
                end = i;
                if (i + 2 != arena_arr_len(a)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM OUTPUT_VARIABLE) expects exactly one output variable"), nob_sv_from_cstr("Usage: list(TRANSFORM <list> <ACTION> [selector] [OUTPUT_VARIABLE <out-var>])"));
                    return eval_result_from_ctx(ctx);
                }
                has_output_var = true;
                out_var = a[i + 1];
                break;
            }

            if (next + 1 >= end) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM AT) requires at least one index"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            for (size_t i = next + 1; i < end; i++) {
                long long raw_idx = 0;
                size_t idx = 0;
                if (!list_sv_parse_i64(a[i], &raw_idx) || !list_normalize_index(arena_arr_len(items), raw_idx, false, &idx)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "list", nob_sv_from_cstr("list(TRANSFORM AT) index out of range"), a[i]);
                    return eval_result_from_ctx(ctx);
                }
                selected[idx] = true;
            }
        } else if (eval_sv_eq_ci_lit(a[next], "FOR")) {
            size_t end = arena_arr_len(a);
            for (size_t i = next + 1; i < arena_arr_len(a); i++) {
                if (!eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) continue;
                end = i;
                if (i + 2 != arena_arr_len(a)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM OUTPUT_VARIABLE) expects exactly one output variable"), nob_sv_from_cstr("Usage: list(TRANSFORM <list> <ACTION> [selector] [OUTPUT_VARIABLE <out-var>])"));
                    return eval_result_from_ctx(ctx);
                }
                has_output_var = true;
                out_var = a[i + 1];
                break;
            }

            if (end < next + 3 || end > next + 4) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM FOR) expects start stop [step]"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            long long start = 0;
            long long stop = 0;
            long long step = 1;
            if (!list_sv_parse_i64(a[next + 1], &start) || !list_sv_parse_i64(a[next + 2], &stop)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "list", nob_sv_from_cstr("list(TRANSFORM FOR) start/stop must be integers"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            if (end == next + 4 && !list_sv_parse_i64(a[next + 3], &step)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "list", nob_sv_from_cstr("list(TRANSFORM FOR) step must be an integer"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            if (step <= 0) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "list", nob_sv_from_cstr("list(TRANSFORM FOR) step must be greater than zero"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            size_t start_idx = 0;
            size_t stop_idx = 0;
            if (!list_normalize_index(arena_arr_len(items), start, false, &start_idx) ||
                !list_normalize_index(arena_arr_len(items), stop, false, &stop_idx)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "list", nob_sv_from_cstr("list(TRANSFORM FOR) range out of bounds"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            if (start_idx <= stop_idx) {
                for (size_t i = start_idx; i <= stop_idx; i += (size_t)step) {
                    selected[i] = true;
                    if (stop_idx - i < (size_t)step) break;
                }
            } else {
                for (size_t i = start_idx;; i -= (size_t)step) {
                    selected[i] = true;
                    if (i <= stop_idx || i - stop_idx < (size_t)step) break;
                }
            }
        } else if (eval_sv_eq_ci_lit(a[next], "REGEX")) {
            size_t end = arena_arr_len(a);
            for (size_t i = next + 2; i < arena_arr_len(a); i++) {
                if (!eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) continue;
                end = i;
                if (i + 2 != arena_arr_len(a)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM OUTPUT_VARIABLE) expects exactly one output variable"), nob_sv_from_cstr("Usage: list(TRANSFORM <list> <ACTION> [selector] [OUTPUT_VARIABLE <out-var>])"));
                    return eval_result_from_ctx(ctx);
                }
                has_output_var = true;
                out_var = a[i + 1];
                break;
            }

            if (end != next + 2) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "list", nob_sv_from_cstr("list(TRANSFORM REGEX) expects exactly one regex argument"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            regex_t sel_re;
            if (!list_compile_regex(ctx, node, o, a[next + 1], &sel_re)) return eval_result_from_ctx(ctx);
            for (size_t i = 0; i < arena_arr_len(items); i++) {
                char *item_c = eval_sv_to_cstr_temp(ctx, items[i]);
                if (!item_c) {
                    regfree(&sel_re);
                    return eval_result_from_ctx(ctx);
                }
                if (regexec(&sel_re, item_c, 0, NULL, 0) == 0) selected[i] = true;
            }
            regfree(&sel_re);
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "list", nob_sv_from_cstr("list(TRANSFORM) received unsupported selector"), a[next]);
            return eval_result_from_ctx(ctx);
        }

        regex_t replace_re;
        bool replace_ready = false;
        if (action == LIST_TRANSFORM_REPLACE) {
            if (!list_compile_regex(ctx, node, o, action_arg1, &replace_re)) return eval_result_from_ctx(ctx);
            replace_ready = true;
        }

        for (size_t i = 0; i < arena_arr_len(items); i++) {
            if (!selected[i]) continue;
            String_View curr = items[i];
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
            } else if (action == LIST_TRANSFORM_GENEX_STRIP) {
                curr = list_genex_strip_temp(ctx, curr);
            } else if (action == LIST_TRANSFORM_REPLACE) {
                if (!list_regex_replace_one_temp(ctx, &replace_re, action_arg2, curr, &curr)) {
                    if (replace_ready) regfree(&replace_re);
                    return eval_result_from_ctx(ctx);
                }
            }
            if (eval_should_stop(ctx)) {
                if (replace_ready) regfree(&replace_re);
                return eval_result_from_ctx(ctx);
            }
            items[i] = curr;
        }

        if (replace_ready) regfree(&replace_re);
        if (has_output_var) {
            (void)list_set_var_from_items(ctx, out_var, items, arena_arr_len(items));
            if (!eval_emit_list_transform(ctx, o, var)) return eval_result_fatal();
            return eval_result_from_ctx(ctx);
        }
        if (arena_arr_len(items) == 0 && !var_defined) return eval_result_from_ctx(ctx);
        (void)list_set_var_from_items(ctx, var, items, arena_arr_len(items));
        if (!eval_emit_list_transform(ctx, o, var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "list", nob_sv_from_cstr("Unsupported list() subcommand"), nob_sv_from_cstr("Implemented: APPEND, PREPEND, INSERT, REMOVE_ITEM, REMOVE_AT, REMOVE_DUPLICATES, LENGTH, GET, FIND, JOIN, SUBLIST, POP_BACK, POP_FRONT, FILTER, TRANSFORM, REVERSE, SORT"));
    eval_request_stop_on_error(ctx);
    return eval_result_from_ctx(ctx);
}
