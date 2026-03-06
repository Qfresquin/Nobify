#include "eval_try_compile_internal.h"

static bool try_run_clear_run_outputs(Evaluator_Context *ctx, const Try_Run_Request *req) {
    if (!ctx || !req) return false;
    if (req->run_output_var.count > 0 && !eval_var_set_current(ctx, req->run_output_var, nob_sv_from_cstr(""))) return false;
    if (req->run_stdout_var.count > 0 && !eval_var_set_current(ctx, req->run_stdout_var, nob_sv_from_cstr(""))) return false;
    if (req->run_stderr_var.count > 0 && !eval_var_set_current(ctx, req->run_stderr_var, nob_sv_from_cstr(""))) return false;
    return true;
}

Eval_Result eval_handle_try_run(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Try_Run_Request req = {0};
    if (!try_run_parse_request(ctx, node, &args, &req)) {
        return eval_result_from_ctx(ctx);
    }

    Try_Compile_Execution_Result exec_res = {0};
    if (!try_compile_execute_source_request(ctx, &req.compile_req, &exec_res)) {
        return eval_result_from_ctx(ctx);
    }

    Try_Run_Result run_res = {
        .compile_ok = exec_res.ok,
        .compile_output = exec_res.output.count > 0 ? exec_res.output : nob_sv_from_cstr(""),
    };
    if (!eval_var_set_current(ctx,
                              req.compile_req.result_var,
                              run_res.compile_ok ? nob_sv_from_cstr("TRUE") : nob_sv_from_cstr("FALSE"))) {
        return eval_result_fatal();
    }
    if (req.compile_output_var.count > 0 && !eval_var_set_current(ctx, req.compile_output_var, run_res.compile_output)) return eval_result_fatal();

    if (!run_res.compile_ok) {
        if (!try_run_clear_run_outputs(ctx, &req)) return eval_result_fatal();
        if (req.run_result_var.count > 0 && !eval_var_set_current(ctx, req.run_result_var, nob_sv_from_cstr(""))) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    String_View cross_compiling = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CROSSCOMPILING"));
    if (cross_compiling.count > 0 && !try_compile_is_false(cross_compiling)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_NOT_IMPLEMENTED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run() cross-compiling answer-file workflow is not implemented yet"), nob_sv_from_cstr("This batch only supports native execution"));
        if (!try_run_clear_run_outputs(ctx, &req)) return eval_result_fatal();
        if (req.run_result_var.count > 0 && !eval_var_set_current(ctx, req.run_result_var, nob_sv_from_cstr(""))) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (exec_res.artifact_path.count == 0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run() failed to start compiled executable"), nob_sv_from_cstr("compiled artifact path is empty"));
        if (!try_run_clear_run_outputs(ctx, &req)) return eval_result_fatal();
        if (req.run_result_var.count > 0 && !eval_var_set_current(ctx, req.run_result_var, nob_sv_from_cstr(""))) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    String_View exec_path = exec_res.artifact_path;
    if (!eval_sv_is_abs_path(exec_path)) {
        String_View cwd = eval_process_cwd_temp(ctx);
        if (eval_should_stop(ctx)) return eval_result_fatal();
        if (cwd.count > 0) {
            exec_path = eval_sv_path_join(eval_temp_arena(ctx), cwd, exec_res.artifact_path);
            if (eval_should_stop(ctx)) return eval_result_fatal();
        }
    }

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, exec_path)) return eval_result_fatal();
    for (size_t i = 0; i < arena_arr_len(req.run_args); i++) {
        if (!eval_sv_arr_push_temp(ctx, &argv, req.run_args[i])) return eval_result_fatal();
    }

    String_View working_dir = req.working_directory.count > 0
        ? eval_path_resolve_for_cmake_arg(ctx, req.working_directory, req.compile_req.current_bin_dir, false)
        : req.compile_req.binary_dir;
    if (eval_should_stop(ctx)) return eval_result_fatal();

    Eval_Process_Run_Request proc_req = {
        .argv = argv,
        .working_directory = working_dir,
        .stdin_data = nob_sv_from_cstr(""),
    };
    Eval_Process_Run_Result proc_res = {0};
    if (!eval_process_run_capture(ctx, &proc_req, &proc_res)) return eval_result_fatal();

    if (!proc_res.started) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run() failed to start compiled executable"), exec_res.artifact_path);
        if (!try_run_clear_run_outputs(ctx, &req)) return eval_result_fatal();
        if (req.run_result_var.count > 0 && !eval_var_set_current(ctx, req.run_result_var, nob_sv_from_cstr(""))) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    run_res.run_invoked = true;
    run_res.run_exit_code = proc_res.exit_code;
    run_res.run_stdout = proc_res.stdout_text;
    run_res.run_stderr = proc_res.stderr_text;

    if (req.run_result_var.count > 0 && !eval_var_set_current(ctx, req.run_result_var, proc_res.result_text)) return eval_result_fatal();
    if (req.run_stdout_var.count > 0 && !eval_var_set_current(ctx, req.run_stdout_var, run_res.run_stdout)) return eval_result_fatal();
    if (req.run_stderr_var.count > 0 && !eval_var_set_current(ctx, req.run_stderr_var, run_res.run_stderr)) return eval_result_fatal();
    if (req.run_output_var.count > 0) {
        Nob_String_Builder merged = {0};
        if (run_res.run_stdout.count > 0) nob_sb_append_buf(&merged, run_res.run_stdout.data, run_res.run_stdout.count);
        if (run_res.run_stderr.count > 0) nob_sb_append_buf(&merged, run_res.run_stderr.data, run_res.run_stderr.count);
        String_View combined = nob_sv_from_cstr("");
        if (merged.count > 0) {
            char *copy = arena_strndup(ctx->arena, merged.items, merged.count);
            EVAL_OOM_RETURN_IF_NULL(ctx, copy, eval_result_fatal());
            combined = nob_sv_from_parts(copy, merged.count);
        }
        nob_sb_free(merged);
        if (!eval_var_set_current(ctx, req.run_output_var, combined)) return eval_result_fatal();
    }

    return eval_result_from_ctx(ctx);
}
