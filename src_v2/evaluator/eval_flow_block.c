#include "eval_flow_internal.h"

static bool block_frame_push(EvalExecContext *ctx, Block_Frame frame) {
    if (!ctx) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->scope_state.block_frames, frame);
}

static size_t block_active_exec_depth(EvalExecContext *ctx) {
    if (!ctx) return 0;
    size_t depth = 0;
    for (size_t i = arena_arr_len(ctx->exec_contexts); i-- > 0;) {
        if (ctx->exec_contexts[i].kind != EVAL_EXEC_CTX_BLOCK) break;
        depth++;
    }
    return depth;
}

static bool block_parse_options(EvalExecContext *ctx,
                                const Node *node,
                                SV_List args,
                                Block_Frame *out_frame) {
    if (!ctx || !node || !out_frame) return false;

    Block_Frame frame = {
        .variable_scope_pushed = true,
        .policy_scope_pushed = true,
        .propagate_vars = NULL,
        .propagate_on_return = false,
    };

    size_t i = 0;
    if (i < arena_arr_len(args) && eval_sv_eq_ci_lit(args[i], "SCOPE_FOR")) {
        frame.variable_scope_pushed = false;
        frame.policy_scope_pushed = false;
        i++;

        bool has_scope_item = false;
        while (i < arena_arr_len(args)) {
            if (eval_sv_eq_ci_lit(args[i], "VARIABLES")) {
                frame.variable_scope_pushed = true;
                has_scope_item = true;
                i++;
                continue;
            }
            if (eval_sv_eq_ci_lit(args[i], "POLICIES")) {
                frame.policy_scope_pushed = true;
                has_scope_item = true;
                i++;
                continue;
            }
            break;
        }

        if (!has_scope_item) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("block(SCOPE_FOR ...) requires VARIABLES and/or POLICIES"), nob_sv_from_cstr("Usage: block([SCOPE_FOR VARIABLES POLICIES] [PROPAGATE <vars...>])"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
    }

    if (i < arena_arr_len(args) && eval_sv_eq_ci_lit(args[i], "PROPAGATE")) {
        i++;
        if (i >= arena_arr_len(args)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("block(PROPAGATE ...) requires at least one variable name"), nob_sv_from_cstr("Usage: block(PROPAGATE <var1> <var2> ...)"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }

        if (!arena_arr_reserve(ctx->event_arena, frame.propagate_vars, arena_arr_len(args) - i)) {
            return ctx_oom(ctx);
        }
        for (; i < arena_arr_len(args); i++) {
            if (!eval_sv_arr_push_event(ctx, &frame.propagate_vars, args[i])) return false;
        }
    }

    if (i < arena_arr_len(args) && !eval_sv_eq_ci_lit(args[i], "PROPAGATE")) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("block() received unsupported argument"), args[i]);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (frame.propagate_vars && !frame.variable_scope_pushed) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("block(PROPAGATE ...) requires variable scope"), nob_sv_from_cstr("Use SCOPE_FOR VARIABLES (or omit SCOPE_FOR) when using PROPAGATE"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    *out_frame = frame;
    return true;
}

static bool block_propagate_to_parent_scope(EvalExecContext *ctx, const Block_Frame *frame) {
    if (!ctx || !frame || !frame->propagate_vars) return true;
    if (!frame->variable_scope_pushed) return true;
    if (eval_scope_visible_depth(ctx) <= 1) return true;

    for (size_t i = 0; i < arena_arr_len(frame->propagate_vars); i++) {
        String_View key = frame->propagate_vars[i];
        if (!eval_var_defined_current(ctx, key)) continue;

        String_View value = eval_var_get_visible(ctx, key);
        size_t saved_depth = 0;
        if (!eval_scope_use_parent_view(ctx, &saved_depth)) return false;
        bool ok = eval_var_set_current(ctx, key, value);
        eval_scope_restore_view(ctx, saved_depth);
        if (!ok) return false;
    }

    return true;
}

static bool block_pop_frame(EvalExecContext *ctx, const Node *node, bool for_return, Block_Frame *out_frame) {
    if (!ctx || arena_arr_len(ctx->scope_state.block_frames) == 0) return true;
    if (!eval_exec_current(ctx) || eval_exec_current(ctx)->kind != EVAL_EXEC_CTX_BLOCK) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_INVALID_CONTEXT,
                                 nob_sv_from_cstr("flow"),
                                 node ? node->as.cmd.name : nob_sv_from_cstr("return"),
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("block() frame mismatch across execution contexts"),
                                 nob_sv_from_cstr("Ensure endblock() and return() only unwind blocks in the active execution context"));
        return false;
    }

    Block_Frame frame = ctx->scope_state.block_frames[arena_arr_len(ctx->scope_state.block_frames) - 1];
    arena_arr_set_len(ctx->scope_state.block_frames, arena_arr_len(ctx->scope_state.block_frames) - 1);
    if (out_frame) *out_frame = frame;

    bool should_propagate = !for_return || frame.propagate_on_return;
    if (should_propagate) {
        if (for_return && arena_arr_len(ctx->scope_state.return_propagate_vars) > 0) {
            Block_Frame ret_frame = frame;
            ret_frame.propagate_vars = ctx->scope_state.return_propagate_vars;
            if (!block_propagate_to_parent_scope(ctx, &ret_frame)) return false;
        } else if (!block_propagate_to_parent_scope(ctx, &frame)) {
            return false;
        }
    }

    if (frame.policy_scope_pushed && !eval_policy_pop(ctx)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("flow"), node ? node->as.cmd.name : nob_sv_from_cstr("return"), eval_origin_from_node(ctx, node), nob_sv_from_cstr("Failed to restore policy scope"), nob_sv_from_cstr("Ensure policy stack is balanced inside block()"));
        return false;
    }
    if (frame.variable_scope_pushed) eval_scope_pop(ctx);
    if (eval_exec_current(ctx) && eval_exec_current(ctx)->kind == EVAL_EXEC_CTX_BLOCK) {
        eval_exec_pop(ctx);
    }
    return true;
}

bool eval_unwind_blocks_for_return(EvalExecContext *ctx) {
    if (!ctx) return false;
    while (block_active_exec_depth(ctx) > 0) {
        Block_Frame ended = {0};
        if (!block_pop_frame(ctx, NULL, true, &ended)) return false;
        if (!eval_emit_flow_block_end(ctx,
                                      (Event_Origin){0},
                                      ended.propagate_on_return,
                                      arena_arr_len(ended.propagate_vars) > 0)) {
            return false;
        }
    }
    return true;
}

Eval_Result eval_handle_break(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    if (!flow_require_no_args(ctx, node, nob_sv_from_cstr("Usage: break()"))) return eval_result_from_ctx(ctx);
    if (eval_exec_current_loop_depth(ctx) == 0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("break() used outside of a loop"), nob_sv_from_cstr("Use break() only inside foreach()/while()"));
        return eval_result_from_ctx(ctx);
    }
    if (!eval_exec_request_break(ctx)) return eval_result_fatal();
    if (!eval_emit_flow_break(ctx,
                              eval_origin_from_node(ctx, node),
                              (uint32_t)eval_exec_current_loop_depth(ctx))) {
        return eval_result_fatal();
    }
    return eval_result_ok();
}

Eval_Result eval_handle_continue(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    if (!flow_require_no_args(ctx, node, nob_sv_from_cstr("Usage: continue()"))) return eval_result_from_ctx(ctx);
    if (eval_exec_current_loop_depth(ctx) == 0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("continue() used outside of a loop"), nob_sv_from_cstr("Use continue() only inside foreach()/while()"));
        return eval_result_from_ctx(ctx);
    }
    if (!eval_exec_request_continue(ctx)) return eval_result_fatal();
    if (!eval_emit_flow_continue(ctx,
                                 eval_origin_from_node(ctx, node),
                                 (uint32_t)eval_exec_current_loop_depth(ctx))) {
        return eval_result_fatal();
    }
    return eval_result_ok();
}

Eval_Result eval_handle_return(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    eval_clear_return_state(ctx);

    bool cmp0140_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0140);
    if (cmp0140_new && arena_arr_len(args) > 0) {
        if (!eval_sv_eq_ci_lit(args[0], "PROPAGATE")) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("return() received unsupported arguments"), nob_sv_from_cstr("Usage: return() or return(PROPAGATE <var...>)"));
            return eval_result_from_ctx(ctx);
        }
        if (arena_arr_len(args) < 2) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("return(PROPAGATE ...) requires at least one variable"), nob_sv_from_cstr("Usage: return(PROPAGATE <var1> <var2> ...)"));
            return eval_result_from_ctx(ctx);
        }
        if (!arena_arr_reserve(ctx->event_arena, ctx->scope_state.return_propagate_vars, arena_arr_len(args) - 1)) {
            (void)ctx_oom(ctx);
            return eval_result_fatal();
        }
        for (size_t i = 1; i < arena_arr_len(args); i++) {
            if (!eval_sv_arr_push_event(ctx, &ctx->scope_state.return_propagate_vars, args[i])) return eval_result_fatal();
        }
    }

    if (arena_arr_len(ctx->scope_state.return_propagate_vars) > 0) {
        size_t active_block_count = block_active_exec_depth(ctx);
        size_t total_blocks = arena_arr_len(ctx->scope_state.block_frames);
        size_t begin = active_block_count <= total_blocks ? (total_blocks - active_block_count) : 0;
        for (size_t bi = total_blocks; bi-- > begin;) {
            ctx->scope_state.block_frames[bi].propagate_on_return = true;
        }
    }
    if (!eval_exec_request_return(ctx)) return eval_result_fatal();
    return eval_result_ok();
}

Eval_Result eval_handle_block(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Block_Frame frame = {0};
    if (!block_parse_options(ctx, node, args, &frame)) return eval_result_from_ctx(ctx);

    if (frame.variable_scope_pushed && !eval_scope_push(ctx)) return eval_result_from_ctx(ctx);
    if (frame.policy_scope_pushed && !eval_policy_push(ctx)) {
        if (frame.variable_scope_pushed) eval_scope_pop(ctx);
        return eval_result_from_ctx(ctx);
    }
    if (!block_frame_push(ctx, frame)) {
        if (frame.policy_scope_pushed) (void)eval_policy_pop(ctx);
        if (frame.variable_scope_pushed) eval_scope_pop(ctx);
        return eval_result_from_ctx(ctx);
    }
    Eval_Exec_Context exec = {0};
    exec.kind = EVAL_EXEC_CTX_BLOCK;
    exec.return_context = ctx->return_context;
    exec.source_dir = eval_current_source_dir(ctx);
    exec.binary_dir = eval_current_binary_dir(ctx);
    exec.list_dir = eval_current_list_dir(ctx);
    exec.current_file = ctx->current_file;
    if (!eval_exec_push(ctx, exec)) {
        arena_arr_set_len(ctx->scope_state.block_frames, arena_arr_len(ctx->scope_state.block_frames) - 1);
        if (frame.policy_scope_pushed) (void)eval_policy_pop(ctx);
        if (frame.variable_scope_pushed) eval_scope_pop(ctx);
        return eval_result_from_ctx(ctx);
    }
    if (!eval_emit_flow_block_begin(ctx,
                                    eval_origin_from_node(ctx, node),
                                    frame.variable_scope_pushed,
                                    frame.policy_scope_pushed,
                                    arena_arr_len(frame.propagate_vars) > 0)) {
        eval_exec_pop(ctx);
        arena_arr_set_len(ctx->scope_state.block_frames, arena_arr_len(ctx->scope_state.block_frames) - 1);
        if (frame.policy_scope_pushed) (void)eval_policy_pop(ctx);
        if (frame.variable_scope_pushed) eval_scope_pop(ctx);
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_endblock(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    if (!flow_require_no_args(ctx, node, nob_sv_from_cstr("Usage: endblock()"))) return eval_result_from_ctx(ctx);

    if (arena_arr_len(ctx->scope_state.block_frames) == 0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("endblock() called without matching block()"), nob_sv_from_cstr("Add block() before endblock()"));
        return eval_result_from_ctx(ctx);
    }

    Block_Frame ended = {0};
    if (!block_pop_frame(ctx, node, false, &ended)) return eval_result_from_ctx(ctx);
    if (!eval_emit_flow_block_end(ctx,
                                  eval_origin_from_node(ctx, node),
                                  ended.propagate_on_return,
                                  arena_arr_len(ended.propagate_vars) > 0)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}
