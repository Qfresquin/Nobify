#include "eval_try_compile_internal.h"

#include <string.h>

static bool try_run_clear_run_outputs(Evaluator_Context *ctx, const Try_Run_Request *req) {
    if (!ctx || !req) return false;
    if (req->run_output_var.count > 0 && !eval_var_set_current(ctx, req->run_output_var, nob_sv_from_cstr(""))) return false;
    if (req->run_stdout_var.count > 0 && !eval_var_set_current(ctx, req->run_stdout_var, nob_sv_from_cstr(""))) return false;
    if (req->run_stderr_var.count > 0 && !eval_var_set_current(ctx, req->run_stderr_var, nob_sv_from_cstr(""))) return false;
    return true;
}

static String_View try_run_merge_stdout_stderr_temp(Evaluator_Context *ctx,
                                                    String_View stdout_text,
                                                    String_View stderr_text) {
    if (!ctx) return nob_sv_from_cstr("");
    Nob_String_Builder merged = {0};
    if (stdout_text.count > 0) nob_sb_append_buf(&merged, stdout_text.data, stdout_text.count);
    if (stderr_text.count > 0) nob_sb_append_buf(&merged, stderr_text.data, stderr_text.count);
    String_View out = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(merged.items ? merged.items : "", merged.count));
    nob_sb_free(merged);
    return out;
}

static bool try_run_set_legacy_output(Evaluator_Context *ctx,
                                      const Try_Run_Request *req,
                                      String_View compile_output,
                                      String_View run_output) {
    if (!ctx || !req || req->legacy_output_var.count == 0) return true;

    Nob_String_Builder merged = {0};
    if (compile_output.count > 0) nob_sb_append_buf(&merged, compile_output.data, compile_output.count);
    if (run_output.count > 0) nob_sb_append_buf(&merged, run_output.data, run_output.count);
    String_View combined = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(merged.items ? merged.items : "", merged.count));
    nob_sb_free(merged);
    return eval_var_set_current(ctx, req->legacy_output_var, combined);
}

static String_View try_run_cache_key_with_suffix_temp(Evaluator_Context *ctx,
                                                      String_View base,
                                                      const char *suffix) {
    if (!ctx || !suffix) return nob_sv_from_cstr("");
    size_t suffix_len = strlen(suffix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), base.count + suffix_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (base.count > 0) memcpy(buf, base.data, base.count);
    memcpy(buf + base.count, suffix, suffix_len);
    buf[base.count + suffix_len] = '\0';
    return nob_sv_from_parts(buf, base.count + suffix_len);
}

static bool try_run_cache_set_and_emit(Evaluator_Context *ctx,
                                       Cmake_Event_Origin origin,
                                       String_View key,
                                       String_View value,
                                       String_View doc) {
    if (!ctx || key.count == 0) return false;
    if (!eval_cache_set(ctx, key, value, nob_sv_from_cstr("STRING"), doc)) return false;
    return eval_emit_var_set_cache(ctx, origin, key, value);
}

static void try_run_sb_append_cmake_quoted(Nob_String_Builder *sb, String_View value) {
    if (!sb) return;
    nob_sb_append(sb, '"');
    for (size_t i = 0; i < value.count; i++) {
        char c = value.data[i];
        if (c == '\\' || c == '"') nob_sb_append(sb, '\\');
        nob_sb_append(sb, c);
    }
    nob_sb_append(sb, '"');
}

static void try_run_sb_append_cache_line(Nob_String_Builder *sb,
                                         String_View key,
                                         String_View value,
                                         const char *doc) {
    if (!sb || !doc) return;
    nob_sb_append_cstr(sb, "set(");
    nob_sb_append_buf(sb, key.data, key.count);
    nob_sb_append(sb, ' ');
    try_run_sb_append_cmake_quoted(sb, value);
    nob_sb_append_cstr(sb, " CACHE STRING ");
    try_run_sb_append_cmake_quoted(sb, nob_sv_from_cstr(doc));
    nob_sb_append_cstr(sb, " FORCE)\n");
}

static String_View try_run_results_file_path(Evaluator_Context *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View top_bin = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"));
    if (top_bin.count == 0) top_bin = ctx->binary_dir;
    return eval_sv_path_join(eval_temp_arena(ctx), top_bin, nob_sv_from_cstr("TryRunResults.cmake"));
}

static bool try_run_write_results_file(Evaluator_Context *ctx, const Try_Run_Request *req) {
    if (!ctx || !req) return false;
    String_View placeholder_run = nob_sv_from_cstr("PLEASE_FILL_OUT-FAILED_TO_RUN");
    String_View placeholder_output = nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND");
    String_View combined_key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT");
    String_View stdout_key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDOUT");
    String_View stderr_key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDERR");
    if (eval_should_stop(ctx)) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "# Generated by evaluator try_run() for cross-compiling.\n");
    try_run_sb_append_cache_line(&sb, req->run_result_var, placeholder_run, "Result from try_run()");
    if (req->run_output_var.count > 0 || req->legacy_output_var.count > 0) {
        try_run_sb_append_cache_line(&sb, combined_key, placeholder_output, "Output from try_run()");
    }
    if (req->run_stdout_var.count > 0) {
        try_run_sb_append_cache_line(&sb, stdout_key, placeholder_output, "Stdout from try_run()");
    }
    if (req->run_stderr_var.count > 0) {
        try_run_sb_append_cache_line(&sb, stderr_key, placeholder_output, "Stderr from try_run()");
    }

    bool ok = eval_write_text_file(ctx,
                                   try_run_results_file_path(ctx),
                                   nob_sv_from_parts(sb.items ? sb.items : "", sb.count),
                                   true);
    nob_sb_free(sb);
    return ok;
}

static bool try_run_publish_cross_compile_placeholders(Evaluator_Context *ctx,
                                                       const Try_Run_Request *req,
                                                       Cmake_Event_Origin origin) {
    if (!ctx || !req) return false;
    String_View placeholder_run = nob_sv_from_cstr("PLEASE_FILL_OUT-FAILED_TO_RUN");
    String_View placeholder_output = nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND");
    String_View doc = nob_sv_from_cstr("try_run() cross-compiling placeholder");
    if (!try_run_cache_set_and_emit(ctx, origin, req->run_result_var, placeholder_run, doc)) return false;

    if (req->run_output_var.count > 0 || req->legacy_output_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT");
        if (eval_should_stop(ctx)) return false;
        if (!try_run_cache_set_and_emit(ctx, origin, key, placeholder_output, doc)) return false;
    }
    if (req->run_stdout_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDOUT");
        if (eval_should_stop(ctx)) return false;
        if (!try_run_cache_set_and_emit(ctx, origin, key, placeholder_output, doc)) return false;
    }
    if (req->run_stderr_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDERR");
        if (eval_should_stop(ctx)) return false;
        if (!try_run_cache_set_and_emit(ctx, origin, key, placeholder_output, doc)) return false;
    }

    return try_run_write_results_file(ctx, req);
}

static String_View try_run_resolve_exec_path(Evaluator_Context *ctx, String_View artifact_path) {
    if (!ctx) return nob_sv_from_cstr("");
    if (artifact_path.count == 0 || eval_sv_is_abs_path(artifact_path)) return artifact_path;
    String_View cwd = eval_process_cwd_temp(ctx);
    if (eval_should_stop(ctx) || cwd.count == 0) return artifact_path;
    return eval_sv_path_join(eval_temp_arena(ctx), cwd, artifact_path);
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
    bool compile_ok = req.compile_req.signature == TRY_COMPILE_SIGNATURE_PROJECT
        ? try_compile_execute_project_request(ctx, node, &req.compile_req, &exec_res)
        : try_compile_execute_source_request(ctx, &req.compile_req, &exec_res);
    if (!compile_ok) {
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
        if (!try_run_set_legacy_output(ctx, &req, run_res.compile_output, nob_sv_from_cstr(""))) return eval_result_fatal();
        if (req.run_result_var.count > 0 && !eval_var_set_current(ctx, req.run_result_var, nob_sv_from_cstr(""))) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    String_View cross_compiling = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CROSSCOMPILING"));
    if (exec_res.artifact_path.count == 0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, origin, nob_sv_from_cstr("try_run() failed to start compiled executable"), nob_sv_from_cstr("compiled artifact path is empty"));
        if (!try_run_clear_run_outputs(ctx, &req)) return eval_result_fatal();
        if (!try_run_set_legacy_output(ctx, &req, run_res.compile_output, nob_sv_from_cstr(""))) return eval_result_fatal();
        if (req.run_result_var.count > 0 && !eval_var_set_current(ctx, req.run_result_var, nob_sv_from_cstr(""))) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    String_View exec_path = try_run_resolve_exec_path(ctx, exec_res.artifact_path);
    if (eval_should_stop(ctx)) return eval_result_fatal();

    SV_List argv = NULL;
    bool is_cross_compiling = cross_compiling.count > 0 && !try_compile_is_false(cross_compiling);
    if (is_cross_compiling) {
        String_View emulator = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CROSSCOMPILING_EMULATOR"));
        if (emulator.count > 0 && !try_compile_is_false(emulator)) {
            SV_List emulator_argv = NULL;
            if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), emulator, &emulator_argv)) {
                return eval_result_fatal();
            }
            for (size_t i = 0; i < arena_arr_len(emulator_argv); i++) {
                if (!eval_sv_arr_push_temp(ctx, &argv, emulator_argv[i])) return eval_result_fatal();
            }
        } else {
            if (!try_run_publish_cross_compile_placeholders(ctx, &req, origin)) return eval_result_fatal();
            return eval_result_from_ctx(ctx);
        }
    }

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
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, origin, nob_sv_from_cstr("try_run() failed to start compiled executable"), exec_res.artifact_path);
        if (!try_run_clear_run_outputs(ctx, &req)) return eval_result_fatal();
        if (!try_run_set_legacy_output(ctx, &req, run_res.compile_output, nob_sv_from_cstr(""))) return eval_result_fatal();
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
    String_View combined_run_output = try_run_merge_stdout_stderr_temp(ctx, run_res.run_stdout, run_res.run_stderr);
    if (req.run_output_var.count > 0) {
        if (!eval_var_set_current(ctx, req.run_output_var, combined_run_output)) return eval_result_fatal();
    }
    if (!try_run_set_legacy_output(ctx, &req, run_res.compile_output, combined_run_output)) return eval_result_fatal();

    return eval_result_from_ctx(ctx);
}
