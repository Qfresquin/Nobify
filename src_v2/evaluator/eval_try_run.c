#include "eval_try_compile_internal.h"

#include <string.h>

typedef struct {
    String_View run_result;
    String_View combined_output;
    String_View stdout_output;
    String_View stderr_output;
    bool has_run_result;
    bool has_combined_output;
    bool has_stdout_output;
    bool has_stderr_output;
} Try_Run_Cache_Answers;

static bool try_run_clear_run_outputs(EvalExecContext *ctx, const Try_Run_Request *req) {
    if (!ctx || !req) return false;
    if (req->run_output_var.count > 0 && !eval_var_set_current(ctx, req->run_output_var, nob_sv_from_cstr(""))) return false;
    if (req->run_stdout_var.count > 0 && !eval_var_set_current(ctx, req->run_stdout_var, nob_sv_from_cstr(""))) return false;
    if (req->run_stderr_var.count > 0 && !eval_var_set_current(ctx, req->run_stderr_var, nob_sv_from_cstr(""))) return false;
    return true;
}

static bool try_run_unset_run_outputs(EvalExecContext *ctx, const Try_Run_Request *req) {
    if (!ctx || !req) return false;
    if (req->run_output_var.count > 0 && !eval_var_unset_current(ctx, req->run_output_var)) return false;
    if (req->run_stdout_var.count > 0 && !eval_var_unset_current(ctx, req->run_stdout_var)) return false;
    if (req->run_stderr_var.count > 0 && !eval_var_unset_current(ctx, req->run_stderr_var)) return false;
    if (req->legacy_output_var.count > 0 && !eval_var_unset_current(ctx, req->legacy_output_var)) return false;
    return true;
}

static bool try_run_result_uses_cache(const Try_Run_Request *req) {
    return req && !req->compile_req.no_cache;
}

static String_View try_run_cache_get(EvalExecContext *ctx, String_View key) {
    if (!ctx || key.count == 0 || !ctx->scope_state.cache_entries) return nob_sv_from_cstr("");
    Eval_Cache_Entry *entry = stbds_shgetp_null(ctx->scope_state.cache_entries, nob_temp_sv_to_cstr(key));
    if (!entry) return nob_sv_from_cstr("");
    return entry->value.data;
}

static bool try_run_publish_result_var(EvalExecContext *ctx,
                                       Cmake_Event_Origin origin,
                                       String_View key,
                                       String_View value,
                                       bool use_cache,
                                       String_View type,
                                       String_View doc) {
    if (!ctx || key.count == 0) return true;
    if (!use_cache) return eval_var_set_current(ctx, key, value);
    if (!eval_cache_set(ctx, key, value, type, doc)) return false;
    return eval_emit_var_set_cache(ctx, origin, key, value);
}

static bool try_run_publish_compile_result(EvalExecContext *ctx,
                                           const Try_Run_Request *req,
                                           Cmake_Event_Origin origin,
                                           bool compile_ok) {
    if (!ctx || !req) return false;
    return try_run_publish_result_var(ctx,
                                      origin,
                                      req->compile_req.result_var,
                                      compile_ok ? nob_sv_from_cstr("TRUE") : nob_sv_from_cstr("FALSE"),
                                      try_run_result_uses_cache(req),
                                      nob_sv_from_cstr("INTERNAL"),
                                      nob_sv_from_cstr("try_run compile result"));
}

static bool try_run_publish_run_result(EvalExecContext *ctx,
                                       const Try_Run_Request *req,
                                       Cmake_Event_Origin origin,
                                       String_View run_result) {
    if (!ctx || !req) return false;
    return try_run_publish_result_var(ctx,
                                      origin,
                                      req->run_result_var,
                                      run_result,
                                      try_run_result_uses_cache(req),
                                      nob_sv_from_cstr("STRING"),
                                      nob_sv_from_cstr("try_run result"));
}

static String_View try_run_merge_stdout_stderr_temp(EvalExecContext *ctx,
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

static bool try_run_set_legacy_output(EvalExecContext *ctx,
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

static String_View try_run_cache_key_with_suffix_temp(EvalExecContext *ctx,
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

static bool try_run_cache_set_and_emit(EvalExecContext *ctx,
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

static String_View try_run_results_file_path(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View top_bin = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"));
    if (top_bin.count == 0) top_bin = ctx->binary_dir;
    return eval_sv_path_join(eval_temp_arena(ctx), top_bin, nob_sv_from_cstr("TryRunResults.cmake"));
}

static String_View try_run_cross_compile_cache_value_or_placeholder(EvalExecContext *ctx,
                                                                    String_View key,
                                                                    String_View placeholder) {
    if (!ctx || key.count == 0) return placeholder;
    if (!eval_cache_defined(ctx, key)) return placeholder;
    String_View cached = try_run_cache_get(ctx, key);
    return cached.count > 0 ? cached : nob_sv_from_cstr("");
}

static bool try_run_write_results_file(EvalExecContext *ctx, const Try_Run_Request *req) {
    if (!ctx || !req) return false;
    String_View placeholder_run = nob_sv_from_cstr("PLEASE_FILL_OUT-FAILED_TO_RUN");
    String_View placeholder_output = nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND");
    String_View combined_key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT");
    String_View stdout_key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDOUT");
    String_View stderr_key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDERR");
    if (eval_should_stop(ctx)) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "# Generated by evaluator try_run() for cross-compiling.\n");
    try_run_sb_append_cache_line(&sb,
                                 req->run_result_var,
                                 try_run_cross_compile_cache_value_or_placeholder(ctx,
                                                                                  req->run_result_var,
                                                                                  placeholder_run),
                                 "Result from try_run()");
    if (req->run_output_var.count > 0 || req->legacy_output_var.count > 0) {
        try_run_sb_append_cache_line(&sb,
                                     combined_key,
                                     try_run_cross_compile_cache_value_or_placeholder(ctx,
                                                                                      combined_key,
                                                                                      placeholder_output),
                                     "Output from try_run()");
    }
    if (req->run_stdout_var.count > 0) {
        try_run_sb_append_cache_line(&sb,
                                     stdout_key,
                                     try_run_cross_compile_cache_value_or_placeholder(ctx,
                                                                                      stdout_key,
                                                                                      placeholder_output),
                                     "Stdout from try_run()");
    }
    if (req->run_stderr_var.count > 0) {
        try_run_sb_append_cache_line(&sb,
                                     stderr_key,
                                     try_run_cross_compile_cache_value_or_placeholder(ctx,
                                                                                      stderr_key,
                                                                                      placeholder_output),
                                     "Stderr from try_run()");
    }

    bool ok = eval_write_text_file(ctx,
                                   try_run_results_file_path(ctx),
                                   nob_sv_from_parts(sb.items ? sb.items : "", sb.count),
                                   true);
    nob_sb_free(sb);
    return ok;
}

static bool try_run_publish_cross_compile_placeholders(EvalExecContext *ctx,
                                                       const Try_Run_Request *req,
                                                       Cmake_Event_Origin origin) {
    if (!ctx || !req) return false;
    if (!try_run_unset_run_outputs(ctx, req)) return false;
    String_View placeholder_run = nob_sv_from_cstr("PLEASE_FILL_OUT-FAILED_TO_RUN");
    String_View placeholder_output = nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND");
    String_View doc = nob_sv_from_cstr("try_run() cross-compiling placeholder");
    if (!eval_cache_defined(ctx, req->run_result_var) &&
        !try_run_cache_set_and_emit(ctx, origin, req->run_result_var, placeholder_run, doc)) {
        return false;
    }

    if (req->run_output_var.count > 0 || req->legacy_output_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT");
        if (eval_should_stop(ctx)) return false;
        if (!eval_cache_defined(ctx, key) &&
            !try_run_cache_set_and_emit(ctx, origin, key, placeholder_output, doc)) {
            return false;
        }
    }
    if (req->run_stdout_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDOUT");
        if (eval_should_stop(ctx)) return false;
        if (!eval_cache_defined(ctx, key) &&
            !try_run_cache_set_and_emit(ctx, origin, key, placeholder_output, doc)) {
            return false;
        }
    }
    if (req->run_stderr_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDERR");
        if (eval_should_stop(ctx)) return false;
        if (!eval_cache_defined(ctx, key) &&
            !try_run_cache_set_and_emit(ctx, origin, key, placeholder_output, doc)) {
            return false;
        }
    }

    return try_run_write_results_file(ctx, req);
}

static bool try_run_collect_cross_compile_answers(EvalExecContext *ctx,
                                                  const Try_Run_Request *req,
                                                  Try_Run_Cache_Answers *out_answers,
                                                  bool *out_complete) {
    if (!ctx || !req || !out_answers || !out_complete) return false;
    *out_answers = (Try_Run_Cache_Answers){0};
    *out_complete = true;

    out_answers->has_run_result = eval_cache_defined(ctx, req->run_result_var);
    if (out_answers->has_run_result) {
        out_answers->run_result = try_run_cache_get(ctx, req->run_result_var);
    } else {
        *out_complete = false;
    }

    if (req->run_output_var.count > 0 || req->legacy_output_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT");
        if (eval_should_stop(ctx)) return false;
        out_answers->has_combined_output = eval_cache_defined(ctx, key);
        if (out_answers->has_combined_output) {
            out_answers->combined_output = try_run_cache_get(ctx, key);
        } else {
            *out_complete = false;
        }
    }

    if (req->run_stdout_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDOUT");
        if (eval_should_stop(ctx)) return false;
        out_answers->has_stdout_output = eval_cache_defined(ctx, key);
        if (out_answers->has_stdout_output) {
            out_answers->stdout_output = try_run_cache_get(ctx, key);
        } else {
            *out_complete = false;
        }
    }

    if (req->run_stderr_var.count > 0) {
        String_View key = try_run_cache_key_with_suffix_temp(ctx, req->run_result_var, "__TRYRUN_OUTPUT_STDERR");
        if (eval_should_stop(ctx)) return false;
        out_answers->has_stderr_output = eval_cache_defined(ctx, key);
        if (out_answers->has_stderr_output) {
            out_answers->stderr_output = try_run_cache_get(ctx, key);
        } else {
            *out_complete = false;
        }
    }

    return true;
}

static bool try_run_publish_cross_compile_answers(EvalExecContext *ctx,
                                                  const Try_Run_Request *req,
                                                  Cmake_Event_Origin origin,
                                                  String_View compile_output,
                                                  const Try_Run_Cache_Answers *answers) {
    if (!ctx || !req || !answers) return false;
    if (!try_run_publish_run_result(ctx, req, origin, answers->run_result)) return false;
    if (req->run_output_var.count > 0 &&
        !eval_var_set_current(ctx, req->run_output_var, answers->combined_output)) {
        return false;
    }
    if (req->run_stdout_var.count > 0 &&
        !eval_var_set_current(ctx, req->run_stdout_var, answers->stdout_output)) {
        return false;
    }
    if (req->run_stderr_var.count > 0 &&
        !eval_var_set_current(ctx, req->run_stderr_var, answers->stderr_output)) {
        return false;
    }
    return try_run_set_legacy_output(ctx,
                                     req,
                                     compile_output,
                                     answers->has_combined_output ? answers->combined_output : nob_sv_from_cstr(""));
}

static String_View try_run_resolve_exec_path(EvalExecContext *ctx, String_View artifact_path) {
    if (!ctx) return nob_sv_from_cstr("");
    if (artifact_path.count == 0 || eval_sv_is_abs_path(artifact_path)) return artifact_path;
    String_View cwd = eval_process_cwd_temp(ctx);
    if (eval_should_stop(ctx) || cwd.count == 0) return artifact_path;
    return eval_sv_path_join(eval_temp_arena(ctx), cwd, artifact_path);
}

static bool try_run_execute_compile_phase(EvalExecContext *ctx,
                                          const Node *node,
                                          const Try_Run_Request *req,
                                          Try_Compile_Execution_Result *out_exec_res,
                                          Try_Run_Result *out_run_res) {
    if (!ctx || !node || !req || !out_exec_res || !out_run_res) return false;
    memset(out_exec_res, 0, sizeof(*out_exec_res));
    memset(out_run_res, 0, sizeof(*out_run_res));

    bool compile_ok = req->compile_req.signature == TRY_COMPILE_SIGNATURE_PROJECT
        ? try_compile_execute_project_request(ctx, node, &req->compile_req, out_exec_res)
        : try_compile_execute_source_request(ctx, &req->compile_req, out_exec_res);
    if (!compile_ok) return false;

    *out_run_res = (Try_Run_Result){
        .compile_ok = out_exec_res->ok,
        .compile_output = out_exec_res->output.count > 0 ? out_exec_res->output : nob_sv_from_cstr(""),
    };
    return true;
}

static bool try_run_publish_compile_phase(EvalExecContext *ctx,
                                          Cmake_Event_Origin origin,
                                          const Try_Run_Request *req,
                                          const Try_Run_Result *run_res) {
    if (!ctx || !req || !run_res) return false;
    if (!try_run_publish_compile_result(ctx, req, origin, run_res->compile_ok)) {
        return false;
    }
    if (req->compile_output_var.count > 0 &&
        !eval_var_set_current(ctx, req->compile_output_var, run_res->compile_output)) {
        return false;
    }
    return true;
}

static bool try_run_finish_without_run(EvalExecContext *ctx,
                                       const Try_Run_Request *req,
                                       Cmake_Event_Origin origin,
                                       String_View compile_output,
                                       String_View run_result) {
    if (!ctx || !req) return false;
    if (!try_run_clear_run_outputs(ctx, req)) return false;
    if (!try_run_set_legacy_output(ctx, req, compile_output, nob_sv_from_cstr(""))) return false;
    return try_run_publish_run_result(ctx, req, origin, run_result);
}

static bool try_run_append_exec_argv(EvalExecContext *ctx,
                                     const Try_Run_Request *req,
                                     String_View exec_path,
                                     SV_List *out_argv) {
    if (!ctx || !req || !out_argv) return false;
    if (!eval_sv_arr_push_temp(ctx, out_argv, exec_path)) return false;
    for (size_t i = 0; i < arena_arr_len(req->run_args); i++) {
        if (!eval_sv_arr_push_temp(ctx, out_argv, req->run_args[i])) return false;
    }
    return true;
}

static bool try_run_handle_cross_compile_without_emulator(EvalExecContext *ctx,
                                                          const Try_Run_Request *req,
                                                          Cmake_Event_Origin origin,
                                                          String_View compile_output,
                                                          bool *out_handled) {
    if (!ctx || !req || !out_handled) return false;
    *out_handled = false;

    Try_Run_Cache_Answers answers = {0};
    bool answers_complete = false;
    if (!try_run_collect_cross_compile_answers(ctx, req, &answers, &answers_complete)) return false;
    if (answers_complete) {
        if (!try_run_publish_cross_compile_answers(ctx, req, origin, compile_output, &answers)) return false;
        *out_handled = true;
        return true;
    }

    if (!try_run_publish_cross_compile_placeholders(ctx, req, origin)) return false;
    *out_handled = true;
    return true;
}

static bool try_run_publish_process_outputs(EvalExecContext *ctx,
                                            const Try_Run_Request *req,
                                            Cmake_Event_Origin origin,
                                            Try_Run_Result *run_res,
                                            const Eval_Process_Run_Result *proc_res) {
    if (!ctx || !req || !run_res || !proc_res) return false;

    run_res->run_invoked = true;
    run_res->run_exit_code = proc_res->exit_code;
    run_res->run_stdout = proc_res->stdout_text;
    run_res->run_stderr = proc_res->stderr_text;

    if (!try_run_publish_run_result(ctx, req, origin, proc_res->result_text)) return false;
    if (req->run_stdout_var.count > 0 && !eval_var_set_current(ctx, req->run_stdout_var, run_res->run_stdout)) return false;
    if (req->run_stderr_var.count > 0 && !eval_var_set_current(ctx, req->run_stderr_var, run_res->run_stderr)) return false;

    String_View combined_run_output = try_run_merge_stdout_stderr_temp(ctx, run_res->run_stdout, run_res->run_stderr);
    if (req->run_output_var.count > 0 && !eval_var_set_current(ctx, req->run_output_var, combined_run_output)) {
        return false;
    }
    return try_run_set_legacy_output(ctx, req, run_res->compile_output, combined_run_output);
}

static bool try_run_execute_request(EvalExecContext *ctx,
                                    const Node *node,
                                    const Try_Run_Request *req) {
    if (!ctx || !node || !req) return false;

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Try_Compile_Execution_Result exec_res = {0};
    Try_Run_Result run_res = {0};
    if (!try_run_execute_compile_phase(ctx, node, req, &exec_res, &run_res)) return false;
    if (!try_run_publish_compile_phase(ctx, origin, req, &run_res)) return false;

    if (!run_res.compile_ok) {
        return try_run_finish_without_run(ctx, req, origin, run_res.compile_output, nob_sv_from_cstr(""));
    }

    if (exec_res.artifact_path.count == 0) {
        return try_run_finish_without_run(ctx,
                                          req,
                                          origin,
                                          run_res.compile_output,
                                          nob_sv_from_cstr("FAILED_TO_RUN"));
    }

    String_View exec_path = try_run_resolve_exec_path(ctx, exec_res.artifact_path);
    if (eval_should_stop(ctx)) return false;

    String_View cross_compiling = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CROSSCOMPILING"));
    bool is_cross_compiling = cross_compiling.count > 0 && !try_compile_is_false(cross_compiling);
    SV_List argv = NULL;
    if (is_cross_compiling) {
        String_View emulator = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CROSSCOMPILING_EMULATOR"));
        if (emulator.count > 0 && !try_compile_is_false(emulator)) {
            SV_List emulator_argv = NULL;
            if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), emulator, &emulator_argv)) {
                return false;
            }
            for (size_t i = 0; i < arena_arr_len(emulator_argv); i++) {
                if (!eval_sv_arr_push_temp(ctx, &argv, emulator_argv[i])) return false;
            }
        } else {
            bool handled = false;
            if (!try_run_handle_cross_compile_without_emulator(ctx,
                                                               req,
                                                               origin,
                                                               run_res.compile_output,
                                                               &handled)) {
                return false;
            }
            if (handled) return true;
        }
    }
    if (!try_run_append_exec_argv(ctx, req, exec_path, &argv)) return false;

    String_View working_dir = req->working_directory.count > 0
        ? eval_path_resolve_for_cmake_arg(ctx, req->working_directory, req->compile_req.current_bin_dir, false)
        : req->compile_req.binary_dir;
    if (eval_should_stop(ctx)) return false;

    Eval_Process_Run_Request proc_req = {
        .argv = argv,
        .argc = arena_arr_len(argv),
        .working_directory = working_dir,
        .stdin_data = nob_sv_from_cstr(""),
    };
    Eval_Process_Run_Result proc_res = {0};
    if (!eval_process_run_capture(ctx, &proc_req, &proc_res)) return false;

    if (!proc_res.started) {
        return try_run_finish_without_run(ctx,
                                          req,
                                          origin,
                                          run_res.compile_output,
                                          nob_sv_from_cstr("FAILED_TO_RUN"));
    }

    return try_run_publish_process_outputs(ctx, req, origin, &run_res, &proc_res);
}

Eval_Result eval_handle_try_run(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Try_Run_Request req = {0};
    if (!try_run_parse_request(ctx, node, &args, &req)) return eval_result_from_ctx(ctx);
    if (!try_run_execute_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
