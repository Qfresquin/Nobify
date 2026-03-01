#include "eval_flow.h"

#include "evaluator_internal.h"
#include "arena_dyn.h"

static bool block_frame_push(Evaluator_Context *ctx, Block_Frame frame) {
    if (!ctx) return false;
    if (!arena_da_try_append(ctx->event_arena, &ctx->block_frames, frame)) return ctx_oom(ctx);
    return true;
}

static bool block_parse_options(Evaluator_Context *ctx,
                                const Node *node,
                                SV_List args,
                                Block_Frame *out_frame) {
    if (!ctx || !node || !out_frame) return false;

    Block_Frame frame = {
        .variable_scope_pushed = true,
        .policy_scope_pushed = true,
        .propagate_vars = NULL,
        .propagate_count = 0,
        .propagate_on_return = false,
    };

    size_t i = 0;
    if (i < args.count && eval_sv_eq_ci_lit(args.items[i], "SCOPE_FOR")) {
        frame.variable_scope_pushed = false;
        frame.policy_scope_pushed = false;
        i++;

        bool has_scope_item = false;
        while (i < args.count) {
            if (eval_sv_eq_ci_lit(args.items[i], "VARIABLES")) {
                frame.variable_scope_pushed = true;
                has_scope_item = true;
                i++;
                continue;
            }
            if (eval_sv_eq_ci_lit(args.items[i], "POLICIES")) {
                frame.policy_scope_pushed = true;
                has_scope_item = true;
                i++;
                continue;
            }
            break;
        }

        if (!has_scope_item) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("block(SCOPE_FOR ...) requires VARIABLES and/or POLICIES"),
                                 nob_sv_from_cstr("Usage: block([SCOPE_FOR VARIABLES POLICIES] [PROPAGATE <vars...>])"));
            return !eval_should_stop(ctx);
        }
    }

    if (i < args.count && eval_sv_eq_ci_lit(args.items[i], "PROPAGATE")) {
        i++;
        if (i >= args.count) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("block(PROPAGATE ...) requires at least one variable name"),
                                 nob_sv_from_cstr("Usage: block(PROPAGATE <var1> <var2> ...)"));
            return !eval_should_stop(ctx);
        }

        frame.propagate_count = args.count - i;
        frame.propagate_vars = arena_alloc_array(ctx->event_arena, String_View, frame.propagate_count);
        EVAL_OOM_RETURN_IF_NULL(ctx, frame.propagate_vars, false);

        for (size_t pi = 0; pi < frame.propagate_count; pi++) {
            frame.propagate_vars[pi] = sv_copy_to_event_arena(ctx, args.items[i + pi]);
            if (eval_should_stop(ctx)) return false;
        }
        i = args.count;
    }

    if (i < args.count && !eval_sv_eq_ci_lit(args.items[i], "PROPAGATE")) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("block() received unsupported argument"),
                             args.items[i]);
        return !eval_should_stop(ctx);
    }

    if (frame.propagate_count > 0 && !frame.variable_scope_pushed) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("block(PROPAGATE ...) requires variable scope"),
                             nob_sv_from_cstr("Use SCOPE_FOR VARIABLES (or omit SCOPE_FOR) when using PROPAGATE"));
        return !eval_should_stop(ctx);
    }

    *out_frame = frame;
    return true;
}

static bool block_propagate_to_parent_scope(Evaluator_Context *ctx, const Block_Frame *frame) {
    if (!ctx || !frame || frame->propagate_count == 0) return true;
    if (!frame->variable_scope_pushed) return true;
    if (ctx->scope_depth <= 1) return true;

    for (size_t i = 0; i < frame->propagate_count; i++) {
        String_View key = frame->propagate_vars[i];
        if (!eval_var_defined_in_current_scope(ctx, key)) continue;

        String_View value = eval_var_get(ctx, key);
        size_t saved_depth = ctx->scope_depth;
        ctx->scope_depth = saved_depth - 1;
        bool ok = eval_var_set(ctx, key, value);
        ctx->scope_depth = saved_depth;
        if (!ok) return false;
    }

    return true;
}

static bool block_pop_frame(Evaluator_Context *ctx, const Node *node, bool for_return) {
    if (!ctx || ctx->block_frames.count == 0) return true;

    Block_Frame frame = ctx->block_frames.items[ctx->block_frames.count - 1];
    ctx->block_frames.count--;

    bool should_propagate = !for_return || frame.propagate_on_return;
    if (should_propagate) {
        if (for_return && ctx->return_propagate_count > 0) {
            Block_Frame ret_frame = frame;
            ret_frame.propagate_vars = ctx->return_propagate_vars;
            ret_frame.propagate_count = ctx->return_propagate_count;
            if (!block_propagate_to_parent_scope(ctx, &ret_frame)) return false;
        } else if (!block_propagate_to_parent_scope(ctx, &frame)) {
            return false;
        }
    }

    if (frame.policy_scope_pushed && !eval_policy_pop(ctx)) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node ? node->as.cmd.name : nob_sv_from_cstr("return"),
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("Failed to restore policy scope"),
                             nob_sv_from_cstr("Ensure policy stack is balanced inside block()"));
        return false;
    }
    if (frame.variable_scope_pushed) eval_scope_pop(ctx);
    return true;
}

static bool flow_require_no_args(Evaluator_Context *ctx, const Node *node, String_View usage_hint) {
    if (!ctx || !node) return false;

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (args.count == 0) return true;

    (void)eval_emit_diag(ctx,
                         EV_DIAG_ERROR,
                         nob_sv_from_cstr("flow"),
                         node->as.cmd.name,
                         eval_origin_from_node(ctx, node),
                         nob_sv_from_cstr("Command does not accept arguments"),
                         usage_hint);
    return false;
}

bool eval_unwind_blocks_for_return(Evaluator_Context *ctx) {
    if (!ctx) return false;
    while (ctx->block_frames.count > 0) {
        if (!block_pop_frame(ctx, NULL, true)) return false;
    }
    return true;
}

bool eval_handle_break(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    if (!flow_require_no_args(ctx, node, nob_sv_from_cstr("Usage: break()"))) return !eval_should_stop(ctx);
    if (ctx->loop_depth == 0) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("break() used outside of a loop"),
                             nob_sv_from_cstr("Use break() only inside foreach()/while()"));
        return !eval_should_stop(ctx);
    }
    ctx->break_requested = true;
    return true;
}

bool eval_handle_continue(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    if (!flow_require_no_args(ctx, node, nob_sv_from_cstr("Usage: continue()"))) return !eval_should_stop(ctx);
    if (ctx->loop_depth == 0) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("continue() used outside of a loop"),
                             nob_sv_from_cstr("Use continue() only inside foreach()/while()"));
        return !eval_should_stop(ctx);
    }
    ctx->continue_requested = true;
    return true;
}

bool eval_handle_return(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (ctx->return_context == EVAL_RETURN_CTX_MACRO) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("return() cannot be used inside macro()"),
                             nob_sv_from_cstr("macro() is expanded in place; use function() if return() is required"));
        return !eval_should_stop(ctx);
    }

    ctx->return_propagate_vars = NULL;
    ctx->return_propagate_count = 0;

    String_View cmp0140 = eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0140"));
    bool cmp0140_new = eval_sv_eq_ci_lit(cmp0140, "NEW");
    if (cmp0140_new && args.count > 0) {
        if (!eval_sv_eq_ci_lit(args.items[0], "PROPAGATE")) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("return() received unsupported arguments"),
                                 nob_sv_from_cstr("Usage: return() or return(PROPAGATE <var...>)"));
            return !eval_should_stop(ctx);
        }
        if (args.count < 2) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("return(PROPAGATE ...) requires at least one variable"),
                                 nob_sv_from_cstr("Usage: return(PROPAGATE <var1> <var2> ...)"));
            return !eval_should_stop(ctx);
        }
        ctx->return_propagate_count = args.count - 1;
        ctx->return_propagate_vars = arena_alloc_array(ctx->event_arena, String_View, ctx->return_propagate_count);
        EVAL_OOM_RETURN_IF_NULL(ctx, ctx->return_propagate_vars, false);
        for (size_t i = 0; i < ctx->return_propagate_count; i++) {
            ctx->return_propagate_vars[i] = sv_copy_to_event_arena(ctx, args.items[i + 1]);
            if (eval_should_stop(ctx)) return false;
        }
    }

    if (ctx->return_propagate_count > 0) {
        for (size_t bi = ctx->block_frames.count; bi-- > 0;) ctx->block_frames.items[bi].propagate_on_return = true;
    }
    ctx->return_requested = true;
    return true;
}

bool eval_handle_block(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    Block_Frame frame = {0};
    if (!block_parse_options(ctx, node, args, &frame)) return !eval_should_stop(ctx);

    if (frame.variable_scope_pushed && !eval_scope_push(ctx)) return !eval_should_stop(ctx);
    if (frame.policy_scope_pushed && !eval_policy_push(ctx)) {
        if (frame.variable_scope_pushed) eval_scope_pop(ctx);
        return !eval_should_stop(ctx);
    }
    if (!block_frame_push(ctx, frame)) {
        if (frame.policy_scope_pushed) (void)eval_policy_pop(ctx);
        if (frame.variable_scope_pushed) eval_scope_pop(ctx);
        return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_endblock(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    if (!flow_require_no_args(ctx, node, nob_sv_from_cstr("Usage: endblock()"))) return !eval_should_stop(ctx);

    if (ctx->block_frames.count == 0) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("endblock() called without matching block()"),
                             nob_sv_from_cstr("Add block() before endblock()"));
        return !eval_should_stop(ctx);
    }

    if (!block_pop_frame(ctx, node, false)) return !eval_should_stop(ctx);

    return !eval_should_stop(ctx);
}
