#include "eval_flow_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SV_List args;
} Flow_Exec_Command;

typedef struct {
    Flow_Exec_Command *items;
    size_t count;
    size_t capacity;
} Flow_Exec_Command_List;

typedef enum {
    FLOW_EXEC_ECHO_NONE = 0,
    FLOW_EXEC_ECHO_STDOUT,
    FLOW_EXEC_ECHO_STDERR,
} Flow_Exec_Command_Echo;

typedef enum {
    FLOW_EXEC_FATAL_NONE = 0,
    FLOW_EXEC_FATAL_ANY,
    FLOW_EXEC_FATAL_LAST,
} Flow_Exec_Command_Error_Mode;

typedef struct {
    Flow_Exec_Command_List commands;
    bool has_working_directory;
    String_View working_directory;
    bool has_timeout;
    double timeout_seconds;
    bool has_result_variable;
    String_View result_variable;
    bool has_results_variable;
    String_View results_variable;
    bool has_output_variable;
    String_View output_variable;
    bool has_error_variable;
    String_View error_variable;
    bool has_input_file;
    String_View input_file;
    bool has_output_file;
    String_View output_file;
    bool has_error_file;
    String_View error_file;
    bool output_quiet;
    bool error_quiet;
    bool output_strip_trailing_whitespace;
    bool error_strip_trailing_whitespace;
    bool echo_output_variable;
    bool echo_error_variable;
    Flow_Exec_Command_Echo command_echo;
    bool has_command_error_is_fatal;
    Flow_Exec_Command_Error_Mode command_error_is_fatal;
    bool has_encoding;
    String_View encoding;
} Flow_Exec_Options;

typedef struct {
    bool timed_out;
    int exit_code;
    String_View stdout_text;
    String_View stderr_text;
    String_View result_text;
} Flow_Exec_Result;

typedef struct {
    String_View executable;
    bool has_working_directory;
    String_View working_directory;
    bool has_args;
    String_View arg_string;
    bool has_output_variable;
    String_View output_variable;
    bool has_return_value;
    String_View return_value;
} Flow_Exec_Program_Compat;

typedef struct {
    Flow_Exec_Options options;
    String_View request_command;
} Flow_Execute_Process_Request;

typedef struct {
    Flow_Exec_Options options;
} Flow_Exec_Program_Request;

static bool flow_exec_result_is_success(String_View result) {
    return flow_sv_eq_exact(result, nob_sv_from_cstr("0"));
}

static bool flow_exec_append_bytes(Nob_String_Builder *sb, const char *buf, size_t count) {
    if (!sb || (!buf && count > 0)) return false;
    if (count == 0) return true;
    nob_sb_append_buf(sb, buf, count);
    return true;
}

static bool flow_exec_emit_command_echo(const Flow_Exec_Command *cmd, Flow_Exec_Command_Echo where) {
    if (!cmd || where == FLOW_EXEC_ECHO_NONE) return true;

    FILE *out = (where == FLOW_EXEC_ECHO_STDERR) ? stderr : stdout;
    if (fprintf(out, "execute_process:") < 0) return false;
    for (size_t i = 0; i < arena_arr_len(cmd->args); i++) {
        if (fprintf(out, " %.*s", (int)cmd->args[i].count, cmd->args[i].data ? cmd->args[i].data : "") < 0) {
            return false;
        }
    }
    if (fputc('\n', out) == EOF) return false;
    return fflush(out) == 0;
}

static Flow_Exec_Command_Echo flow_exec_default_command_echo(EvalExecContext *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_EXECUTE_PROCESS_COMMAND_ECHO"));
    if (eval_sv_eq_ci_lit(v, "STDOUT")) return FLOW_EXEC_ECHO_STDOUT;
    if (eval_sv_eq_ci_lit(v, "STDERR")) return FLOW_EXEC_ECHO_STDERR;
    return FLOW_EXEC_ECHO_NONE;
}

static bool flow_exec_is_keyword(String_View token) {
    return flow_arg_exact_ci(token, "COMMAND") ||
           flow_arg_exact_ci(token, "WORKING_DIRECTORY") ||
           flow_arg_exact_ci(token, "TIMEOUT") ||
           flow_arg_exact_ci(token, "RESULT_VARIABLE") ||
           flow_arg_exact_ci(token, "RESULTS_VARIABLE") ||
           flow_arg_exact_ci(token, "OUTPUT_VARIABLE") ||
           flow_arg_exact_ci(token, "ERROR_VARIABLE") ||
           flow_arg_exact_ci(token, "INPUT_FILE") ||
           flow_arg_exact_ci(token, "OUTPUT_FILE") ||
           flow_arg_exact_ci(token, "ERROR_FILE") ||
           flow_arg_exact_ci(token, "OUTPUT_QUIET") ||
           flow_arg_exact_ci(token, "ERROR_QUIET") ||
           flow_arg_exact_ci(token, "OUTPUT_STRIP_TRAILING_WHITESPACE") ||
           flow_arg_exact_ci(token, "ERROR_STRIP_TRAILING_WHITESPACE") ||
           flow_arg_exact_ci(token, "COMMAND_ECHO") ||
           flow_arg_exact_ci(token, "ECHO_OUTPUT_VARIABLE") ||
           flow_arg_exact_ci(token, "ECHO_ERROR_VARIABLE") ||
           flow_arg_exact_ci(token, "COMMAND_ERROR_IS_FATAL") ||
           flow_arg_exact_ci(token, "ENCODING");
}

static bool flow_exec_append_command_arg(EvalExecContext *ctx, Flow_Exec_Command *cmd, String_View arg) {
    if (!ctx || !cmd) return false;
    return EVAL_ARR_PUSH(ctx, ctx->arena, cmd->args, arg);
}

static bool flow_exec_append_command(EvalExecContext *ctx,
                                     Flow_Exec_Command_List *commands,
                                     Flow_Exec_Command cmd) {
    if (!ctx || !commands) return false;
    if (!EVAL_ARR_PUSH(ctx, ctx->arena, commands->items, cmd)) return false;
    commands->count = arena_arr_len(commands->items);
    commands->capacity = arena_arr_cap(commands->items);
    return true;
}

static bool flow_exec_parse_timeout(EvalExecContext *ctx,
                                    const Node *node,
                                    String_View value,
                                    double *out_seconds) {
    if (!ctx || !node || !out_seconds) return false;

    char *text = eval_sv_to_cstr_temp(ctx, value);
    EVAL_OOM_RETURN_IF_NULL(ctx, text, false);

    errno = 0;
    char *end = NULL;
    double timeout = strtod(text, &end);
    if (errno != 0 || !end || *end != '\0' || timeout < 0.0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("execute_process(TIMEOUT) requires a non-negative numeric value"), value);
        return false;
    }

    *out_seconds = timeout;
    return true;
}

static bool flow_exec_parse_options(EvalExecContext *ctx,
                                    const Node *node,
                                    SV_List args,
                                    Flow_Exec_Options *out_opt) {
    if (!ctx || !node || !out_opt) return false;
    memset(out_opt, 0, sizeof(*out_opt));
    out_opt->command_echo = flow_exec_default_command_echo(ctx);

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        String_View token = args[i];

        if (flow_arg_exact_ci(token, "COMMAND")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(COMMAND) requires at least one argument"), nob_sv_from_cstr("Usage: execute_process(COMMAND <cmd> [<arg>...] ...)"));
                return false;
            }

            Flow_Exec_Command cmd = {0};
            i++;
            for (; i < arena_arr_len(args); i++) {
                if (flow_exec_is_keyword(args[i])) {
                    i--;
                    break;
                }
                if (!flow_exec_append_command_arg(ctx, &cmd, args[i])) return false;
            }

            if (arena_arr_len(cmd.args) == 0) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(COMMAND) requires at least one argument"), nob_sv_from_cstr("Usage: execute_process(COMMAND <cmd> [<arg>...] ...)"));
                return false;
            }
            if (!flow_exec_append_command(ctx, &out_opt->commands, cmd)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "WORKING_DIRECTORY")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(WORKING_DIRECTORY) requires a path"), nob_sv_from_cstr("Usage: execute_process(... WORKING_DIRECTORY <dir>)"));
                return false;
            }
            out_opt->has_working_directory = true;
            out_opt->working_directory = flow_resolve_binary_relative_path(ctx, args[++i]);
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "TIMEOUT")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(TIMEOUT) requires a value"), nob_sv_from_cstr("Usage: execute_process(... TIMEOUT <seconds>)"));
                return false;
            }
            out_opt->has_timeout = true;
            if (!flow_exec_parse_timeout(ctx, node, args[++i], &out_opt->timeout_seconds)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "RESULT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(RESULT_VARIABLE) requires an output variable"), nob_sv_from_cstr("Usage: execute_process(... RESULT_VARIABLE <var>)"));
                return false;
            }
            out_opt->has_result_variable = true;
            out_opt->result_variable = args[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "RESULTS_VARIABLE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(RESULTS_VARIABLE) requires an output variable"), nob_sv_from_cstr("Usage: execute_process(... RESULTS_VARIABLE <var>)"));
                return false;
            }
            out_opt->has_results_variable = true;
            out_opt->results_variable = args[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "OUTPUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(OUTPUT_VARIABLE) requires an output variable"), nob_sv_from_cstr("Usage: execute_process(... OUTPUT_VARIABLE <var>)"));
                return false;
            }
            out_opt->has_output_variable = true;
            out_opt->output_variable = args[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "ERROR_VARIABLE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(ERROR_VARIABLE) requires an output variable"), nob_sv_from_cstr("Usage: execute_process(... ERROR_VARIABLE <var>)"));
                return false;
            }
            out_opt->has_error_variable = true;
            out_opt->error_variable = args[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "INPUT_FILE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(INPUT_FILE) requires a path"), nob_sv_from_cstr("Usage: execute_process(... INPUT_FILE <path>)"));
                return false;
            }
            out_opt->has_input_file = true;
            out_opt->input_file = flow_resolve_binary_relative_path(ctx, args[++i]);
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "OUTPUT_FILE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(OUTPUT_FILE) requires a path"), nob_sv_from_cstr("Usage: execute_process(... OUTPUT_FILE <path>)"));
                return false;
            }
            out_opt->has_output_file = true;
            out_opt->output_file = flow_resolve_binary_relative_path(ctx, args[++i]);
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "ERROR_FILE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(ERROR_FILE) requires a path"), nob_sv_from_cstr("Usage: execute_process(... ERROR_FILE <path>)"));
                return false;
            }
            out_opt->has_error_file = true;
            out_opt->error_file = flow_resolve_binary_relative_path(ctx, args[++i]);
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "OUTPUT_QUIET")) {
            out_opt->output_quiet = true;
            continue;
        }

        if (flow_arg_exact_ci(token, "ERROR_QUIET")) {
            out_opt->error_quiet = true;
            continue;
        }

        if (flow_arg_exact_ci(token, "OUTPUT_STRIP_TRAILING_WHITESPACE")) {
            out_opt->output_strip_trailing_whitespace = true;
            continue;
        }

        if (flow_arg_exact_ci(token, "ERROR_STRIP_TRAILING_WHITESPACE")) {
            out_opt->error_strip_trailing_whitespace = true;
            continue;
        }

        if (flow_arg_exact_ci(token, "COMMAND_ECHO")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(COMMAND_ECHO) requires a value"), nob_sv_from_cstr("Usage: execute_process(... COMMAND_ECHO <STDOUT|STDERR|NONE>)"));
                return false;
            }
            String_View value = args[++i];
            if (eval_sv_eq_ci_lit(value, "STDOUT")) {
                out_opt->command_echo = FLOW_EXEC_ECHO_STDOUT;
            } else if (eval_sv_eq_ci_lit(value, "STDERR")) {
                out_opt->command_echo = FLOW_EXEC_ECHO_STDERR;
            } else if (eval_sv_eq_ci_lit(value, "NONE")) {
                out_opt->command_echo = FLOW_EXEC_ECHO_NONE;
            } else {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(COMMAND_ECHO) received an invalid value"), value);
                return false;
            }
            continue;
        }

        if (flow_arg_exact_ci(token, "ECHO_OUTPUT_VARIABLE")) {
            out_opt->echo_output_variable = true;
            continue;
        }

        if (flow_arg_exact_ci(token, "ECHO_ERROR_VARIABLE")) {
            out_opt->echo_error_variable = true;
            continue;
        }

        if (flow_arg_exact_ci(token, "COMMAND_ERROR_IS_FATAL")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(COMMAND_ERROR_IS_FATAL) requires a value"), nob_sv_from_cstr("Usage: execute_process(... COMMAND_ERROR_IS_FATAL <ANY|LAST>)"));
                return false;
            }
            String_View value = args[++i];
            out_opt->has_command_error_is_fatal = true;
            if (eval_sv_eq_ci_lit(value, "ANY")) {
                out_opt->command_error_is_fatal = FLOW_EXEC_FATAL_ANY;
            } else if (eval_sv_eq_ci_lit(value, "LAST")) {
                out_opt->command_error_is_fatal = FLOW_EXEC_FATAL_LAST;
            } else if (eval_sv_eq_ci_lit(value, "NONE")) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(COMMAND_ERROR_IS_FATAL NONE) is not part of the CMake 3.28 baseline"), nob_sv_from_cstr("Use ANY or LAST for the 3.28 command surface"));
                return false;
            } else {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(COMMAND_ERROR_IS_FATAL) received an invalid value"), value);
                return false;
            }
            continue;
        }

        if (flow_arg_exact_ci(token, "ENCODING")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process(ENCODING) requires a value"), nob_sv_from_cstr("Usage: execute_process(... ENCODING <name>)"));
                return false;
            }
            out_opt->has_encoding = true;
            out_opt->encoding = args[++i];
            continue;
        }

        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process() received an unsupported argument"), token);
        return false;
    }

    if (out_opt->commands.count == 0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("execute_process() requires at least one COMMAND clause"), nob_sv_from_cstr("Usage: execute_process(COMMAND <cmd> [<arg>...] ...)"));
        return false;
    }

    return true;
}

static String_View flow_exec_primary_command_name(const Flow_Exec_Options *opt) {
    if (!opt || opt->commands.count == 0) return nob_sv_from_cstr("");
    if (arena_arr_len(opt->commands.items[0].args) == 0) return nob_sv_from_cstr("");
    return opt->commands.items[0].args[0];
}

static bool flow_exec_stabilize_sv(EvalExecContext *ctx, String_View *value) {
    if (!ctx || !value) return false;
    if (value->count == 0) return true;
    *value = sv_copy_to_arena(ctx->arena, *value);
    EVAL_OOM_RETURN_IF_NULL(ctx, value->data, false);
    return true;
}

static bool flow_exec_stabilize_options(EvalExecContext *ctx, Flow_Exec_Options *opt) {
    if (!ctx || !opt) return false;
    if (opt->has_working_directory && !flow_exec_stabilize_sv(ctx, &opt->working_directory)) return false;
    if (opt->has_input_file && !flow_exec_stabilize_sv(ctx, &opt->input_file)) return false;
    if (opt->has_output_file && !flow_exec_stabilize_sv(ctx, &opt->output_file)) return false;
    if (opt->has_error_file && !flow_exec_stabilize_sv(ctx, &opt->error_file)) return false;
    return true;
}

static bool flow_parse_execute_process_request(EvalExecContext *ctx,
                                               const Node *node,
                                               Flow_Execute_Process_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Flow_Execute_Process_Request req = {0};

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (!flow_exec_parse_options(ctx, node, args, &req.options)) return false;
    if (!flow_exec_stabilize_options(ctx, &req.options)) return false;

    req.request_command = flow_exec_primary_command_name(&req.options);
    *out_req = req;
    return true;
}

static bool flow_exec_read_file(EvalExecContext *ctx, String_View path, String_View *out_text) {
    if (!ctx || !out_text) return false;
    *out_text = nob_sv_from_cstr("");
    if (path.count == 0) return true;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) {
        nob_sb_free(sb);
        return false;
    }
    *out_text = flow_sb_to_temp_sv(ctx, &sb);
    nob_sb_free(sb);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool flow_exec_write_file(EvalExecContext *ctx, String_View path, String_View content) {
    if (!ctx) return false;
    if (path.count == 0) return true;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    return nob_write_entire_file(path_c, content.data ? content.data : "", content.count);
}

static bool flow_exec_run_command(EvalExecContext *ctx,
                                  const Flow_Exec_Command *cmd,
                                  String_View working_directory,
                                  String_View stdin_data,
                                  double deadline_seconds,
                                  Flow_Exec_Result *out_result) {
    if (!ctx || !cmd || !out_result || arena_arr_len(cmd->args) == 0) return false;
    *out_result = (Flow_Exec_Result){0};

    Eval_Process_Run_Request req = {
        .argv = cmd->args,
        .argc = arena_arr_len(cmd->args),
        .working_directory = working_directory,
        .stdin_data = stdin_data,
        .has_timeout = deadline_seconds > 0.0,
        .timeout_seconds = 0.0,
    };
    if (req.has_timeout) {
        double remaining = deadline_seconds - flow_now_seconds();
        if (remaining < 0.0) remaining = 0.0;
        req.timeout_seconds = remaining;
    }

    Eval_Process_Run_Result proc = {0};
    if (!eval_process_run_capture(ctx, &req, &proc)) return false;

    out_result->timed_out = proc.timed_out;
    out_result->exit_code = proc.exit_code;
    out_result->stdout_text = proc.stdout_text;
    out_result->stderr_text = proc.stderr_text;
    out_result->result_text = proc.result_text;
    return true;
}

static bool flow_exec_collect_results(EvalExecContext *ctx,
                                      const Flow_Exec_Options *opt,
                                      String_View *out_stdout,
                                      String_View *out_stderr,
                                      String_View *out_last_result,
                                      String_View *out_results_joined,
                                      bool *out_had_error) {
    if (!ctx || !opt || !out_stdout || !out_stderr || !out_last_result || !out_results_joined || !out_had_error) {
        return false;
    }

    *out_stdout = nob_sv_from_cstr("");
    *out_stderr = nob_sv_from_cstr("");
    *out_last_result = nob_sv_from_cstr("0");
    *out_results_joined = nob_sv_from_cstr("");
    *out_had_error = false;

    String_View stdin_payload = nob_sv_from_cstr("");
    if (opt->has_input_file && !flow_exec_read_file(ctx, opt->input_file, &stdin_payload)) {
        *out_last_result = nob_sv_from_cstr("failed to read INPUT_FILE");
        *out_results_joined = *out_last_result;
        *out_had_error = true;
        return true;
    }

    String_View *results = arena_alloc_array(ctx->arena, String_View, opt->commands.count);
    EVAL_OOM_RETURN_IF_NULL(ctx, results, false);

    Nob_String_Builder stderr_sb = {0};
    String_View final_stdout = nob_sv_from_cstr("");
    double deadline = 0.0;
    if (opt->has_timeout && opt->timeout_seconds > 0.0) {
        deadline = flow_now_seconds() + opt->timeout_seconds;
    }

    size_t executed = 0;
    for (size_t i = 0; i < opt->commands.count; i++) {
        const Flow_Exec_Command *cmd = &opt->commands.items[i];
        if (!flow_exec_emit_command_echo(cmd, opt->command_echo)) {
            nob_sb_free(stderr_sb);
            return ctx_oom(ctx);
        }

        Flow_Exec_Result step = {0};
        if (!flow_exec_run_command(ctx, cmd, opt->working_directory, stdin_payload, deadline, &step)) {
            nob_sb_free(stderr_sb);
            return false;
        }

        if (step.stderr_text.count > 0 && !flow_exec_append_bytes(&stderr_sb, step.stderr_text.data, step.stderr_text.count)) {
            nob_sb_free(stderr_sb);
            return ctx_oom(ctx);
        }

        results[executed++] = step.result_text;
        *out_last_result = step.result_text;
        final_stdout = step.stdout_text;
        stdin_payload = step.stdout_text;

        if (!flow_exec_result_is_success(step.result_text)) {
            *out_had_error = true;
            if (step.timed_out) break;
        }
    }

    *out_stdout = final_stdout;
    *out_stderr = flow_sb_to_temp_sv(ctx, &stderr_sb);
    nob_sb_free(stderr_sb);
    if (eval_should_stop(ctx)) return false;

    *out_results_joined = eval_sv_join_semi_temp(ctx, results, executed);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool flow_exec_apply_outputs(EvalExecContext *ctx,
                                    const Node *node,
                                    const Flow_Exec_Options *opt,
                                    String_View stdout_text,
                                    String_View stderr_text,
                                    String_View last_result,
                                    String_View results_joined,
                                    bool had_error) {
    if (!ctx || !node || !opt) return false;

    String_View output_value = opt->output_strip_trailing_whitespace ? flow_trim_trailing_ascii_ws(stdout_text)
                                                                     : stdout_text;
    String_View error_value = opt->error_strip_trailing_whitespace ? flow_trim_trailing_ascii_ws(stderr_text)
                                                                   : stderr_text;

    bool share_var = opt->has_output_variable &&
                     opt->has_error_variable &&
                     flow_sv_eq_exact(opt->output_variable, opt->error_variable);
    bool share_file = opt->has_output_file &&
                      opt->has_error_file &&
                      flow_sv_eq_exact(opt->output_file, opt->error_file);

    String_View merged_value = nob_sv_from_cstr("");
    if (share_var || share_file) {
        Nob_String_Builder merged_sb = {0};
        if (!opt->error_quiet && error_value.count > 0 && !flow_exec_append_bytes(&merged_sb, error_value.data, error_value.count)) {
            nob_sb_free(merged_sb);
            return ctx_oom(ctx);
        }
        if (!opt->output_quiet && output_value.count > 0 && !flow_exec_append_bytes(&merged_sb, output_value.data, output_value.count)) {
            nob_sb_free(merged_sb);
            return ctx_oom(ctx);
        }
        merged_value = flow_sb_to_temp_sv(ctx, &merged_sb);
        nob_sb_free(merged_sb);
        if (eval_should_stop(ctx)) return false;
    }

    if (opt->has_result_variable && !eval_var_set_current(ctx, opt->result_variable, last_result)) return false;
    if (opt->has_results_variable && !eval_var_set_current(ctx, opt->results_variable, results_joined)) return false;

    if (opt->has_output_variable) {
        String_View to_set = opt->output_quiet ? nob_sv_from_cstr("")
                                               : (share_var ? merged_value : output_value);
        if (!eval_var_set_current(ctx, opt->output_variable, to_set)) return false;
    }
    if (opt->has_error_variable) {
        String_View to_set = opt->error_quiet ? nob_sv_from_cstr("")
                                              : (share_var ? merged_value : error_value);
        if (!eval_var_set_current(ctx, opt->error_variable, to_set)) return false;
    }

    if (opt->has_output_file && !opt->output_quiet) {
        String_View file_value = share_file ? merged_value : output_value;
        if (!flow_exec_write_file(ctx, opt->output_file, file_value)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("execute_process() failed to write OUTPUT_FILE"), opt->output_file);
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
    }
    if (opt->has_error_file && !opt->error_quiet && !share_file) {
        if (!flow_exec_write_file(ctx, opt->error_file, error_value)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("execute_process() failed to write ERROR_FILE"), opt->error_file);
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
    }

    if (opt->echo_output_variable && !opt->output_quiet && output_value.count > 0) {
        (void)fwrite(output_value.data, 1, output_value.count, stdout);
        (void)fflush(stdout);
    }
    if (opt->echo_error_variable && !opt->error_quiet && error_value.count > 0) {
        (void)fwrite(error_value.data, 1, error_value.count, stderr);
        (void)fflush(stderr);
    }

    if (opt->has_command_error_is_fatal) {
        bool fatal_hit = false;
        if (opt->command_error_is_fatal == FLOW_EXEC_FATAL_ANY) {
            fatal_hit = had_error;
        } else if (opt->command_error_is_fatal == FLOW_EXEC_FATAL_LAST) {
            fatal_hit = !flow_exec_result_is_success(last_result);
        }

        if (fatal_hit) {
            eval_command_tx_preserve_scope_vars_on_failure(ctx);
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("execute_process() child process failed"), last_result);
            eval_request_stop(ctx);
        }
    }

    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool flow_execute_execute_process_request(EvalExecContext *ctx,
                                                 const Node *node,
                                                 Cmake_Event_Origin origin,
                                                 const Flow_Execute_Process_Request *req) {
    if (!ctx || !node || !req) return false;

    if (!eval_emit_proc_exec_request(ctx,
                                     origin,
                                     req->request_command,
                                     req->options.has_working_directory
                                         ? req->options.working_directory
                                         : nob_sv_from_cstr(""))) {
        return false;
    }

    String_View stdout_text = nob_sv_from_cstr("");
    String_View stderr_text = nob_sv_from_cstr("");
    String_View last_result = nob_sv_from_cstr("0");
    String_View results_joined = nob_sv_from_cstr("");
    bool had_error = false;
    if (!flow_exec_collect_results(ctx,
                                   &req->options,
                                   &stdout_text,
                                   &stderr_text,
                                   &last_result,
                                   &results_joined,
                                   &had_error)) {
        return false;
    }

    if (!eval_emit_proc_exec_result(ctx,
                                    origin,
                                    req->request_command,
                                    last_result,
                                    stdout_text,
                                    stderr_text,
                                    had_error)) {
        return false;
    }

    return flow_exec_apply_outputs(ctx,
                                   node,
                                   &req->options,
                                   stdout_text,
                                   stderr_text,
                                   last_result,
                                   results_joined,
                                   had_error);
}

Eval_Result eval_handle_execute_process(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Flow_Execute_Process_Request req = {0};
    if (!flow_parse_execute_process_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!flow_execute_execute_process_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

static bool flow_exec_program_parse(EvalExecContext *ctx,
                                    const Node *node,
                                    const SV_List *args,
                                    Flow_Exec_Program_Compat *out) {
    if (!ctx || !node || !args || !out) return false;
    *out = (Flow_Exec_Program_Compat){0};

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (arena_arr_len(*args) == 0) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("exec_program() requires an executable"), nob_sv_from_cstr("Usage: exec_program(<executable> [<working-dir>] [ARGS <arg-string>] [OUTPUT_VARIABLE <var>] [RETURN_VALUE <var>])"));
        return false;
    }

    out->executable = (*args)[0];
    size_t i = 1;
    if (i < arena_arr_len(*args) &&
        !flow_arg_exact_ci((*args)[i], "ARGS") &&
        !flow_arg_exact_ci((*args)[i], "OUTPUT_VARIABLE") &&
        !flow_arg_exact_ci((*args)[i], "RETURN_VALUE")) {
        out->has_working_directory = true;
        out->working_directory = (*args)[i++];
    }

    for (; i < arena_arr_len(*args); i++) {
        String_View token = (*args)[i];
        if (flow_arg_exact_ci(token, "ARGS")) {
            if (out->has_args || i + 1 >= arena_arr_len(*args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("exec_program(ARGS) requires exactly one argument string"), nob_sv_from_cstr("Usage: exec_program(... ARGS <arg-string>)"));
                return false;
            }
            out->has_args = true;
            out->arg_string = (*args)[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "OUTPUT_VARIABLE")) {
            if (out->has_output_variable || i + 1 >= arena_arr_len(*args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("exec_program(OUTPUT_VARIABLE) requires exactly one output variable"), nob_sv_from_cstr("Usage: exec_program(... OUTPUT_VARIABLE <var>)"));
                return false;
            }
            out->has_output_variable = true;
            out->output_variable = (*args)[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "RETURN_VALUE")) {
            if (out->has_return_value || i + 1 >= arena_arr_len(*args)) {
                (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("exec_program(RETURN_VALUE) requires exactly one output variable"), nob_sv_from_cstr("Usage: exec_program(... RETURN_VALUE <var>)"));
                return false;
            }
            out->has_return_value = true;
            out->return_value = (*args)[++i];
            continue;
        }

        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("exec_program() received an unsupported argument"), token);
        return false;
    }

    return true;
}

static bool flow_exec_program_lower_request(EvalExecContext *ctx,
                                            const Flow_Exec_Program_Compat *compat,
                                            Flow_Exec_Program_Request *out_req) {
    if (!ctx || !compat || !out_req) return false;
    Flow_Exec_Program_Request req = {0};

    Flow_Exec_Command cmd = {0};
    if (!flow_exec_append_command_arg(ctx, &cmd, compat->executable)) return false;

    if (compat->has_args) {
        SV_List split_args = NULL;
        if (!eval_split_command_line_temp(ctx, EVAL_CMDLINE_NATIVE, compat->arg_string, &split_args)) return false;
        for (size_t i = 0; i < arena_arr_len(split_args); i++) {
            if (!flow_exec_append_command_arg(ctx, &cmd, split_args[i])) return false;
        }
    }

    if (!flow_exec_append_command(ctx, &req.options.commands, cmd)) return false;
    if (compat->has_working_directory) {
        req.options.has_working_directory = true;
        req.options.working_directory = flow_resolve_binary_relative_path(ctx, compat->working_directory);
        if (eval_should_stop(ctx)) return false;
    }
    if (compat->has_output_variable) {
        req.options.has_output_variable = true;
        req.options.output_variable = compat->output_variable;
    }
    if (compat->has_return_value) {
        req.options.has_result_variable = true;
        req.options.result_variable = compat->return_value;
    }
    if (!flow_exec_stabilize_options(ctx, &req.options)) return false;

    *out_req = req;
    return true;
}

static bool flow_parse_exec_program_request(EvalExecContext *ctx,
                                            const Node *node,
                                            Cmake_Event_Origin origin,
                                            Flow_Exec_Program_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (eval_policy_is_new(ctx, EVAL_POLICY_CMP0153)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_POLICY_CONFLICT,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("exec_program() is disallowed by CMP0153"),
                                 nob_sv_from_cstr("Set CMP0153 to OLD only for legacy compatibility"));
        return false;
    }

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    Flow_Exec_Program_Compat compat = {0};
    if (!flow_exec_program_parse(ctx, node, &args, &compat)) return false;
    return flow_exec_program_lower_request(ctx, &compat, out_req);
}

static bool flow_execute_exec_program_request(EvalExecContext *ctx,
                                              const Node *node,
                                              const Flow_Exec_Program_Request *req) {
    if (!ctx || !node || !req) return false;

    String_View stdout_text = nob_sv_from_cstr("");
    String_View stderr_text = nob_sv_from_cstr("");
    String_View last_result = nob_sv_from_cstr("0");
    String_View results_joined = nob_sv_from_cstr("");
    bool had_error = false;
    if (!flow_exec_collect_results(ctx,
                                   &req->options,
                                   &stdout_text,
                                   &stderr_text,
                                   &last_result,
                                   &results_joined,
                                   &had_error)) {
        return false;
    }

    return flow_exec_apply_outputs(ctx,
                                   node,
                                   &req->options,
                                   stdout_text,
                                   stderr_text,
                                   last_result,
                                   results_joined,
                                   had_error);
}

Eval_Result eval_handle_exec_program(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Flow_Exec_Program_Request req = {0};
    if (!flow_parse_exec_program_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    if (!flow_execute_exec_program_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
