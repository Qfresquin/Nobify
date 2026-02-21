#include "eval_flow.h"

#include "evaluator_internal.h"

bool h_break(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
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

bool h_continue(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
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

bool h_return(Evaluator_Context *ctx, const Node *node) {
    (void)node;
    if (!ctx || eval_should_stop(ctx)) return false;
    ctx->return_requested = true;
    return true;
}
