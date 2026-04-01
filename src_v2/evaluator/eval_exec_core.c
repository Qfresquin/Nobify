#include "eval_exec_core.h"

#include "eval_dispatcher.h"
#include "eval_expr.h"
#include "eval_flow.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool exec_core_parse_long(String_View sv, long *out) {
    if (!out || sv.count == 0) return false;
    char buf[128];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out = v;
    return true;
}

static bool exec_core_sv_list_push(Arena *arena, SV_List *list, String_View sv) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, sv);
}

static bool exec_core_parse_positive_size(String_View sv, size_t *out) {
    if (!out || sv.count == 0) return false;
    char buf[64];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';

    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(buf, &end, 10);
    if (errno != 0 || !end || *end != '\0' || v == 0 || v > (unsigned long long)((size_t)-1)) return false;
    *out = (size_t)v;
    return true;
}

static Eval_Result eval_while_iteration_limit(EvalExecContext *ctx,
                                              const Node *node,
                                              size_t *out_limit) {
    const size_t default_limit = 10000;
    if (!out_limit) return eval_result_fatal();
    *out_limit = default_limit;
    if (!ctx) return eval_result_ok();

    String_View raw = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_WHILE_MAX_ITERATIONS));
    if (raw.count == 0) return eval_result_ok();

    size_t parsed = 0;
    if (exec_core_parse_positive_size(raw, &parsed)) {
        *out_limit = parsed;
        return eval_result_ok();
    }

    Cmake_Event_Origin origin = node ? eval_origin_from_node(ctx, node) : (Cmake_Event_Origin){0};
    return eval_emit_diag_with_severity(ctx,
                                        EV_DIAG_WARNING,
                                        EVAL_DIAG_INVALID_VALUE,
                                        nob_sv_from_cstr("while"),
                                        nob_sv_from_cstr("while"),
                                        origin,
                                        nob_sv_from_cstr("CMAKE_NOBIFY_WHILE_MAX_ITERATIONS must be a positive integer"),
                                        nob_sv_from_cstr("Unset it or set it to a decimal value greater than 0"));
}

static Eval_Result eval_if(EvalExecContext *ctx, const Node *node);
static Eval_Result eval_foreach(EvalExecContext *ctx, const Node *node);
static Eval_Result eval_while(EvalExecContext *ctx, const Node *node);
static Eval_Result eval_node(EvalExecContext *ctx, const Node *node);

static Eval_Result eval_if(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    bool cond = eval_condition(ctx, &node->as.if_stmt.condition);
    if (ctx->oom) return eval_result_fatal();
    if (!eval_emit_flow_if_eval(ctx, origin, cond)) return eval_result_fatal();
    if (cond) {
        if (!eval_emit_flow_branch_taken(ctx, origin, nob_sv_from_cstr("then"))) return eval_result_fatal();
        return eval_execute_node_list(ctx, &node->as.if_stmt.then_block);
    }

    for (size_t i = 0; i < arena_arr_len(node->as.if_stmt.elseif_clauses); i++) {
        const ElseIf_Clause *cl = &node->as.if_stmt.elseif_clauses[i];
        bool elseif_cond = eval_condition(ctx, &cl->condition);
        if (ctx->oom) return eval_result_fatal();
        if (!eval_emit_flow_if_eval(ctx, origin, elseif_cond)) return eval_result_fatal();
        if (elseif_cond) {
            if (!eval_emit_flow_branch_taken(ctx, origin, nob_sv_from_cstr("elseif"))) return eval_result_fatal();
            return eval_execute_node_list(ctx, &cl->block);
        }
    }

    if (arena_arr_len(node->as.if_stmt.else_block) > 0) {
        if (!eval_emit_flow_branch_taken(ctx, origin, nob_sv_from_cstr("else"))) return eval_result_fatal();
    }
    return eval_execute_node_list(ctx, &node->as.if_stmt.else_block);
}

static Eval_Result eval_foreach(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.foreach_stmt.args);
    if (ctx->oom) return eval_result_fatal();
    if (arena_arr_len(a) == 0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_MISSING_REQUIRED,
                                 nob_sv_from_cstr("flow"),
                                 nob_sv_from_cstr("foreach"),
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("foreach() expects at least one argument"),
                                 nob_sv_from_cstr("Usage: foreach(<loop_var> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View var = a[0];
    size_t idx = 1;
    SV_List items = NULL;
    String_View *items_ptr = NULL;
    size_t items_count = 0;
    bool cmp0124_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0124);

    if (idx < arena_arr_len(a) && eval_sv_eq_ci_lit(a[idx], "RANGE")) {
        idx++;
        if (arena_arr_len(a) - idx < 1 || arena_arr_len(a) - idx > 3) {
            (void)EVAL_DIAG_EMIT_SEV(ctx,
                                     EV_DIAG_ERROR,
                                     EVAL_DIAG_MISSING_REQUIRED,
                                     nob_sv_from_cstr("flow"),
                                     nob_sv_from_cstr("foreach"),
                                     eval_origin_from_node(ctx, node),
                                     nob_sv_from_cstr("foreach(RANGE ...) expects 1..3 numeric arguments"),
                                     nob_sv_from_cstr("Usage: foreach(v RANGE stop) or RANGE start stop [step]"));
            return eval_result_from_ctx(ctx);
        }
        long start = 0, stop = 0, step = 1;
        if (arena_arr_len(a) - idx == 1) {
            if (!exec_core_parse_long(a[idx], &stop)) return eval_result_fatal();
        } else {
            if (!exec_core_parse_long(a[idx], &start)) return eval_result_fatal();
            if (!exec_core_parse_long(a[idx + 1], &stop)) return eval_result_fatal();
            if (arena_arr_len(a) - idx == 3 && !exec_core_parse_long(a[idx + 2], &step)) return eval_result_fatal();
        }
        if (step == 0) {
            (void)EVAL_DIAG_EMIT_SEV(ctx,
                                     EV_DIAG_ERROR,
                                     EVAL_DIAG_INVALID_VALUE,
                                     nob_sv_from_cstr("flow"),
                                     nob_sv_from_cstr("foreach"),
                                     eval_origin_from_node(ctx, node),
                                     nob_sv_from_cstr("foreach(RANGE ...) step must be non-zero"),
                                     nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }
        if (arena_arr_len(a) - idx == 1) start = 0;
        if ((step > 0 && start > stop) || (step < 0 && start < stop)) return eval_result_ok();
        for (long v = start;; v += step) {
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "%ld", v);
            String_View item = {0};
            if (n < 0 || (size_t)n >= sizeof(buf)) {
                (void)ctx_oom(ctx);
                return eval_result_fatal();
            }
            item = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(buf));
            if (ctx->oom) return eval_result_fatal();
            if (!exec_core_sv_list_push(ctx->arena, &items, item)) {
                (void)ctx_oom(ctx);
                return eval_result_fatal();
            }
            if ((step > 0 && v + step > stop) || (step < 0 && v + step < stop)) break;
        }
        items_ptr = items;
        items_count = arena_arr_len(items);
    } else if (idx < arena_arr_len(a) && eval_sv_eq_ci_lit(a[idx], "IN")) {
        idx++;
        if (idx < arena_arr_len(a) && eval_sv_eq_ci_lit(a[idx], "ITEMS")) {
            idx++;
            items_ptr = &a[idx];
            items_count = arena_arr_len(a) - idx;
        } else if (idx < arena_arr_len(a) && eval_sv_eq_ci_lit(a[idx], "LISTS")) {
            idx++;
            for (; idx < arena_arr_len(a); idx++) {
                String_View list_txt = eval_var_get_visible(ctx, a[idx]);
                if (list_txt.count == 0) continue;
                if (!eval_sv_split_semicolon_genex_aware(ctx->arena, list_txt, &items)) {
                    (void)ctx_oom(ctx);
                    return eval_result_fatal();
                }
            }
            items_ptr = items;
            items_count = arena_arr_len(items);
        } else if (idx < arena_arr_len(a) && eval_sv_eq_ci_lit(a[idx], "ZIP_LISTS")) {
            idx++;
            size_t list_count = arena_arr_len(a) - idx;
            String_View **zip_items = arena_alloc_array(eval_temp_arena(ctx), String_View*, list_count);
            size_t *zip_counts = arena_alloc_array(eval_temp_arena(ctx), size_t, list_count);
            EVAL_OOM_RETURN_IF_NULL(ctx, zip_items, eval_result_fatal());
            EVAL_OOM_RETURN_IF_NULL(ctx, zip_counts, eval_result_fatal());
            size_t max_len = 0;
            for (size_t li = 0; li < list_count; li++, idx++) {
                String_View list_txt = eval_var_get_visible(ctx, a[idx]);
                SV_List one = NULL;
                if (list_txt.count > 0 && !eval_sv_split_semicolon_genex_aware(ctx->arena, list_txt, &one)) {
                    (void)ctx_oom(ctx);
                    return eval_result_fatal();
                }
                if (arena_arr_len(one) > max_len) max_len = arena_arr_len(one);
                zip_items[li] = one;
                zip_counts[li] = arena_arr_len(one);
            }
            for (size_t row = 0; row < max_len; row++) {
                Nob_String_Builder sb = {0};
                for (size_t li = 0; li < list_count; li++) {
                    String_View cur = (row < zip_counts[li]) ? zip_items[li][row] : nob_sv_from_cstr("");
                    if (li > 0) nob_sb_append(&sb, ';');
                    if (cur.count > 0) nob_sb_append_buf(&sb, cur.data, cur.count);
                }
                String_View zipped = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
                nob_sb_free(sb);
                if (!exec_core_sv_list_push(ctx->arena, &items, zipped)) {
                    (void)ctx_oom(ctx);
                    return eval_result_fatal();
                }
            }
            items_ptr = items;
            items_count = arena_arr_len(items);
        } else {
            items_ptr = &a[idx];
            items_count = arena_arr_len(a) - idx;
        }
    } else {
        items_ptr = &a[idx];
        items_count = arena_arr_len(a) - idx;
    }

    String_View loop_old = eval_var_get_visible(ctx, var);
    bool loop_old_defined = eval_var_defined_visible(ctx, var);
    size_t iter_count = 0;
    Eval_Result aggregate = eval_result_ok();

    if (!eval_emit_flow_loop_begin(ctx, origin, nob_sv_from_cstr("foreach"))) return eval_result_fatal();
    Eval_Exec_Context *exec = eval_exec_current(ctx);
    if (!exec) return eval_result_fatal();
    exec->loop_depth++;
    for (size_t ii = 0; ii < items_count; ii++) {
        iter_count++;
        if (!eval_var_set_current(ctx, var, items_ptr[ii])) {
            exec->loop_depth--;
            return eval_result_fatal();
        }
        Eval_Result body = eval_execute_node_list(ctx, &node->as.foreach_stmt.body);
        if (eval_result_is_fatal(body)) {
            exec->loop_depth--;
            return body;
        }
        aggregate = eval_result_merge(aggregate, body);
        if (exec->pending_flow == EVAL_FLOW_RETURN) {
            exec->loop_depth--;
            return aggregate;
        }
        if (exec->pending_flow == EVAL_FLOW_CONTINUE) {
            exec->pending_flow = EVAL_FLOW_NONE;
            continue;
        }
        if (exec->pending_flow == EVAL_FLOW_BREAK) {
            exec->pending_flow = EVAL_FLOW_NONE;
            break;
        }
    }
    exec->loop_depth--;
    if (!eval_emit_flow_loop_end(ctx, origin, nob_sv_from_cstr("foreach"), (uint32_t)iter_count)) return eval_result_fatal();
    if (cmp0124_new) {
        if (loop_old_defined) {
            if (!eval_var_set_current(ctx, var, loop_old)) return eval_result_fatal();
        } else if (!eval_var_unset_current(ctx, var)) {
            return eval_result_fatal();
        }
    } else {
        if (loop_old_defined) {
            if (!eval_var_set_current(ctx, var, loop_old)) return eval_result_fatal();
        } else if (!eval_var_set_current(ctx, var, nob_sv_from_cstr(""))) {
            return eval_result_fatal();
        }
    }
    return eval_result_merge(aggregate, eval_result_ok_if_running(ctx));
}

static Eval_Result eval_while(EvalExecContext *ctx, const Node *node) {
    size_t max_iter = 10000;
    Eval_Result aggregate = eval_while_iteration_limit(ctx, node, &max_iter);
    if (eval_result_is_fatal(aggregate) || eval_should_stop(ctx)) return eval_result_fatal();
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    size_t iter_count = 0;
    if (!eval_emit_flow_loop_begin(ctx, origin, nob_sv_from_cstr("while"))) return eval_result_fatal();
    Eval_Exec_Context *exec = eval_exec_current(ctx);
    if (!exec) return eval_result_fatal();
    exec->loop_depth++;
    for (size_t iter = 0; iter < max_iter; iter++) {
        bool cond = eval_condition(ctx, &node->as.while_stmt.condition);
        if (ctx->oom) {
            exec->loop_depth--;
            return eval_result_fatal();
        }
        if (!eval_emit_flow_if_eval(ctx, origin, cond)) {
            exec->loop_depth--;
            return eval_result_fatal();
        }
        if (!cond) {
            exec->loop_depth--;
            if (!eval_emit_flow_loop_end(ctx, origin, nob_sv_from_cstr("while"), (uint32_t)iter_count)) return eval_result_fatal();
            return aggregate;
        }
        iter_count++;
        Eval_Result body = eval_execute_node_list(ctx, &node->as.while_stmt.body);
        if (eval_result_is_fatal(body)) {
            exec->loop_depth--;
            return body;
        }
        aggregate = eval_result_merge(aggregate, body);
        if (exec->pending_flow == EVAL_FLOW_RETURN) {
            exec->loop_depth--;
            return aggregate;
        }
        if (exec->pending_flow == EVAL_FLOW_CONTINUE) {
            exec->pending_flow = EVAL_FLOW_NONE;
            continue;
        }
        if (exec->pending_flow == EVAL_FLOW_BREAK) {
            exec->pending_flow = EVAL_FLOW_NONE;
            exec->loop_depth--;
            if (!eval_emit_flow_loop_end(ctx, origin, nob_sv_from_cstr("while"), (uint32_t)iter_count)) return eval_result_fatal();
            return aggregate;
        }
    }
    exec->loop_depth--;
    Eval_Result loop_diag = EVAL_DIAG_RESULT_SEV(ctx,
                                                 EV_DIAG_ERROR,
                                                 EVAL_DIAG_OUT_OF_RANGE,
                                                 nob_sv_from_cstr("while"),
                                                 nob_sv_from_cstr("while"),
                                                 origin,
                                                 nob_sv_from_cstr("Iteration limit exceeded"),
                                                 nob_sv_from_cstr("Infinite loop detected"));
    return eval_result_merge(eval_result_soft_error(), loop_diag);
}

static Eval_Result eval_node(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();
    // Compatibility variables are refreshed exactly once per command cycle here.
    // Command handlers and diagnostics then read the stable snapshot already stored
    // on the context instead of re-reading CMAKE_NOBIFY_* mid-cycle.
    eval_refresh_runtime_compat(ctx);
    if (eval_should_stop(ctx)) return eval_result_fatal();

    {
        char line_buf[32];
        int n = snprintf(line_buf, sizeof(line_buf), "%zu", node->line);
        if (n < 0 || (size_t)n >= sizeof(line_buf)) {
            (void)ctx_oom(ctx);
            return eval_result_fatal();
        }
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_LINE"), nob_sv_from_cstr(line_buf))) {
            return eval_result_fatal();
        }
    }

    Arena_Mark mark = arena_mark(ctx->arena);
    Eval_Result result = eval_result_ok();
    switch (node->kind) {
        case NODE_COMMAND:
            if (node->as.cmd.name.count > 0) {
                result = eval_dispatch_command(ctx, node);
            }
            break;
        case NODE_IF:
            result = eval_if(ctx, node);
            break;
        case NODE_FOREACH:
            result = eval_foreach(ctx, node);
            break;
        case NODE_WHILE:
            result = eval_while(ctx, node);
            break;
        case NODE_FUNCTION:
        case NODE_MACRO:
            if (!eval_user_cmd_register(ctx, node)) {
                result = eval_result_from_ctx(ctx);
                if (!eval_result_is_fatal(result)) result = eval_result_fatal();
                break;
            }
            result = eval_result_from_ctx(ctx);
            break;
        default:
            break;
    }

    arena_rewind(ctx->arena, mark);
    return eval_result_merge(result, eval_result_ok_if_running(ctx));
}

Eval_Result eval_execute_node_list(EvalExecContext *ctx, const Node_List *list) {
    if (!ctx || !list) return eval_result_fatal();
    Eval_Result aggregate = eval_result_ok();
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        Eval_Result node_result = eval_node(ctx, &(*list)[i]);
        if (eval_result_is_fatal(node_result)) return node_result;
        aggregate = eval_result_merge(aggregate, node_result);
        Eval_Flow_Signal pending_flow = eval_exec_pending_flow(ctx);
        if (pending_flow == EVAL_FLOW_RETURN) {
            if (!eval_unwind_blocks_for_return(ctx)) return eval_result_fatal();
            return aggregate;
        }
        if (pending_flow == EVAL_FLOW_BREAK || pending_flow == EVAL_FLOW_CONTINUE) return aggregate;
    }
    return aggregate;
}
