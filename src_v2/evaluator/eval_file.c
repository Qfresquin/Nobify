#include "eval_file.h"
#include "eval_file_internal.h"

Eval_Result eval_handle_file(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || arena_arr_len(args) == 0) return eval_result_from_ctx(ctx);

    String_View subcmd = args[0];

    if (eval_sv_eq_ci_lit(subcmd, "GLOB")) {
        eval_file_handle_glob(ctx, node, args, false);
    } else if (eval_sv_eq_ci_lit(subcmd, "GLOB_RECURSE")) {
        eval_file_handle_glob(ctx, node, args, true);
    } else if (eval_sv_eq_ci_lit(subcmd, "READ")) {
        eval_file_handle_read(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "STRINGS")) {
        eval_file_handle_strings(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "COPY")) {
        eval_file_handle_copy(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "WRITE")) {
        eval_file_handle_write(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "MAKE_DIRECTORY")) {
        eval_file_handle_make_directory(ctx, node, args);
    } else if (eval_file_handle_fsops(ctx, node, args)) {
        // handled in eval_file_fsops.c
    } else if (eval_file_handle_transfer(ctx, node, args)) {
        // handled in eval_file_transfer.c
    } else if (eval_file_handle_generate_lock_archive(ctx, node, args)) {
        // handled in eval_file_generate_lock_archive.c
    } else if (eval_file_handle_extra(ctx, node, args)) {
        // handled in eval_file_extra.c
    } else {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_UNSUPPORTED_OPERATION, "eval_file", nob_sv_from_cstr("Unsupported file() subcommand"), subcmd);
    }

    return eval_result_from_ctx(ctx);
}
