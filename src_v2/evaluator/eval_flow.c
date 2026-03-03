#include "eval_flow.h"

#include "evaluator_internal.h"
#include "eval_expr.h"
#include "arena_dyn.h"
#include "subprocess.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

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

static bool flow_token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, token);
}

static bool flow_parse_inline_script(Evaluator_Context *ctx, String_View script, Ast_Root *out_ast) {
    if (!ctx || !out_ast) return false;
    *out_ast = NULL;

    Lexer lx = lexer_init(script);
    Token_List toks = NULL;
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!flow_token_list_append(ctx->arena, &toks, t)) return ctx_oom(ctx);
    }

    *out_ast = parse_tokens(ctx->arena, toks);
    return true;
}

static bool flow_is_valid_command_name(String_View name) {
    if (name.count == 0 || !name.data) return false;
    char c0 = name.data[0];
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_')) return false;
    for (size_t i = 1; i < name.count; i++) {
        char c = name.data[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') continue;
        return false;
    }
    return true;
}

static bool flow_is_call_disallowed(String_View name) {
    return eval_sv_eq_ci_lit(name, "if") ||
           eval_sv_eq_ci_lit(name, "elseif") ||
           eval_sv_eq_ci_lit(name, "else") ||
           eval_sv_eq_ci_lit(name, "endif") ||
           eval_sv_eq_ci_lit(name, "foreach") ||
           eval_sv_eq_ci_lit(name, "endforeach") ||
           eval_sv_eq_ci_lit(name, "while") ||
           eval_sv_eq_ci_lit(name, "endwhile") ||
           eval_sv_eq_ci_lit(name, "function") ||
           eval_sv_eq_ci_lit(name, "endfunction") ||
           eval_sv_eq_ci_lit(name, "macro") ||
           eval_sv_eq_ci_lit(name, "endmacro") ||
           eval_sv_eq_ci_lit(name, "block") ||
           eval_sv_eq_ci_lit(name, "endblock");
}

static size_t flow_bracket_eq_count(String_View sv) {
    for (size_t eqs = 0; eqs < 8; eqs++) {
        bool conflict = false;
        for (size_t i = 0; i + eqs + 1 < sv.count; i++) {
            if (sv.data[i] != ']') continue;
            size_t j = 0;
            while (j < eqs && (i + 1 + j) < sv.count && sv.data[i + 1 + j] == '=') j++;
            if (j != eqs) continue;
            if ((i + 1 + eqs) < sv.count && sv.data[i + 1 + eqs] == ']') {
                conflict = true;
                break;
            }
        }
        if (!conflict) return eqs;
    }
    return 8;
}

static bool flow_append_sv(Nob_String_Builder *sb, String_View sv) {
    if (!sb) return false;
    for (size_t i = 0; i < sv.count; i++) nob_sb_append(sb, sv.data[i]);
    return true;
}

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

static bool flow_build_call_script(Evaluator_Context *ctx,
                                   String_View command_name,
                                   const SV_List *args,
                                   String_View *out_script) {
    if (!ctx || !args || !out_script) return false;
    *out_script = nob_sv_from_cstr("");

    Nob_String_Builder sb = {0};
    if (!flow_append_sv(&sb, command_name)) {
        nob_sb_free(sb);
        return ctx_oom(ctx);
    }
    nob_sb_append(&sb, '(');
    for (size_t i = 0; i < args->count; i++) {
        nob_sb_append(&sb, ' ');
        size_t eqs = flow_bracket_eq_count(args->items[i]);
        nob_sb_append(&sb, '[');
        for (size_t j = 0; j < eqs; j++) nob_sb_append(&sb, '=');
        nob_sb_append(&sb, '[');
        if (!flow_append_sv(&sb, args->items[i])) {
            nob_sb_free(sb);
            return ctx_oom(ctx);
        }
        nob_sb_append(&sb, ']');
        for (size_t j = 0; j < eqs; j++) nob_sb_append(&sb, '=');
        nob_sb_append(&sb, ']');
    }
    nob_sb_append_cstr(&sb, ")\n");

    char *copy = arena_strndup(ctx->arena, sb.items, sb.count);
    nob_sb_free(sb);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, false);
    *out_script = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

static bool flow_sv_eq_exact(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

static bool flow_arg_exact_ci(String_View value, const char *lit) {
    return eval_sv_eq_ci_lit(value, lit);
}

static String_View flow_current_binary_dir(Evaluator_Context *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (dir.count == 0) dir = ctx->binary_dir;
    return dir;
}

static String_View flow_resolve_binary_relative_path(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return nob_sv_from_cstr("");
    return eval_path_resolve_for_cmake_arg(ctx, path, flow_current_binary_dir(ctx), false);
}

static double flow_now_seconds(void) {
    struct timespec ts = {0};
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static String_View flow_sb_to_temp_sv(Evaluator_Context *ctx, Nob_String_Builder *sb) {
    if (!ctx || !sb) return nob_sv_from_cstr("");
    if (sb->count == 0) return nob_sv_from_cstr("");
    char *copy = arena_strndup(ctx->arena, sb->items, sb->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, nob_sv_from_cstr(""));
    return nob_sv_from_parts(copy, sb->count);
}

static String_View flow_trim_trailing_ascii_ws(String_View sv) {
    while (sv.count > 0) {
        unsigned char c = (unsigned char)sv.data[sv.count - 1];
        if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')) break;
        sv.count--;
    }
    return sv;
}

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
    for (size_t i = 0; i < cmd->args.count; i++) {
        if (fprintf(out, " %.*s", (int)cmd->args.items[i].count, cmd->args.items[i].data ? cmd->args.items[i].data : "") < 0) {
            return false;
        }
    }
    if (fputc('\n', out) == EOF) return false;
    return fflush(out) == 0;
}

static Flow_Exec_Command_Echo flow_exec_default_command_echo(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_EXECUTE_PROCESS_COMMAND_ECHO"));
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

static bool flow_exec_append_command_arg(Evaluator_Context *ctx, Flow_Exec_Command *cmd, String_View arg) {
    if (!ctx || !cmd) return false;
    if (!arena_da_try_append(ctx->arena, &cmd->args, arg)) return ctx_oom(ctx);
    return true;
}

static bool flow_exec_append_command(Evaluator_Context *ctx,
                                     Flow_Exec_Command_List *commands,
                                     Flow_Exec_Command cmd) {
    if (!ctx || !commands) return false;
    if (!arena_da_try_append(ctx->arena, commands, cmd)) return ctx_oom(ctx);
    return true;
}

static bool flow_exec_parse_timeout(Evaluator_Context *ctx,
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
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("execute_process(TIMEOUT) requires a non-negative numeric value"),
                             value);
        return !eval_should_stop(ctx);
    }

    *out_seconds = timeout;
    return true;
}

static bool flow_exec_parse_options(Evaluator_Context *ctx,
                                    const Node *node,
                                    SV_List args,
                                    Flow_Exec_Options *out_opt) {
    if (!ctx || !node || !out_opt) return false;
    memset(out_opt, 0, sizeof(*out_opt));
    out_opt->command_echo = flow_exec_default_command_echo(ctx);

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    for (size_t i = 0; i < args.count; i++) {
        String_View token = args.items[i];

        if (flow_arg_exact_ci(token, "COMMAND")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("flow"),
                                     node->as.cmd.name,
                                     origin,
                                     nob_sv_from_cstr("execute_process(COMMAND) requires at least one argument"),
                                     nob_sv_from_cstr("Usage: execute_process(COMMAND <cmd> [<arg>...] ...)"));
                return !eval_should_stop(ctx);
            }

            Flow_Exec_Command cmd = {0};
            i++;
            for (; i < args.count; i++) {
                if (flow_exec_is_keyword(args.items[i])) {
                    i--;
                    break;
                }
                if (!flow_exec_append_command_arg(ctx, &cmd, args.items[i])) return false;
            }

            if (cmd.args.count == 0) {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("flow"),
                                     node->as.cmd.name,
                                     origin,
                                     nob_sv_from_cstr("execute_process(COMMAND) requires at least one argument"),
                                     nob_sv_from_cstr("Usage: execute_process(COMMAND <cmd> [<arg>...] ...)"));
                return !eval_should_stop(ctx);
            }
            if (!flow_exec_append_command(ctx, &out_opt->commands, cmd)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "WORKING_DIRECTORY")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(WORKING_DIRECTORY) requires a path"),
                                     nob_sv_from_cstr("Usage: execute_process(... WORKING_DIRECTORY <dir>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_working_directory = true;
            out_opt->working_directory = flow_resolve_binary_relative_path(ctx, args.items[++i]);
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "TIMEOUT")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(TIMEOUT) requires a value"),
                                     nob_sv_from_cstr("Usage: execute_process(... TIMEOUT <seconds>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_timeout = true;
            if (!flow_exec_parse_timeout(ctx, node, args.items[++i], &out_opt->timeout_seconds)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "RESULT_VARIABLE")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(RESULT_VARIABLE) requires an output variable"),
                                     nob_sv_from_cstr("Usage: execute_process(... RESULT_VARIABLE <var>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_result_variable = true;
            out_opt->result_variable = args.items[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "RESULTS_VARIABLE")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(RESULTS_VARIABLE) requires an output variable"),
                                     nob_sv_from_cstr("Usage: execute_process(... RESULTS_VARIABLE <var>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_results_variable = true;
            out_opt->results_variable = args.items[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "OUTPUT_VARIABLE")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(OUTPUT_VARIABLE) requires an output variable"),
                                     nob_sv_from_cstr("Usage: execute_process(... OUTPUT_VARIABLE <var>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_output_variable = true;
            out_opt->output_variable = args.items[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "ERROR_VARIABLE")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(ERROR_VARIABLE) requires an output variable"),
                                     nob_sv_from_cstr("Usage: execute_process(... ERROR_VARIABLE <var>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_error_variable = true;
            out_opt->error_variable = args.items[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "INPUT_FILE")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(INPUT_FILE) requires a path"),
                                     nob_sv_from_cstr("Usage: execute_process(... INPUT_FILE <path>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_input_file = true;
            out_opt->input_file = flow_resolve_binary_relative_path(ctx, args.items[++i]);
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "OUTPUT_FILE")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(OUTPUT_FILE) requires a path"),
                                     nob_sv_from_cstr("Usage: execute_process(... OUTPUT_FILE <path>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_output_file = true;
            out_opt->output_file = flow_resolve_binary_relative_path(ctx, args.items[++i]);
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        if (flow_arg_exact_ci(token, "ERROR_FILE")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(ERROR_FILE) requires a path"),
                                     nob_sv_from_cstr("Usage: execute_process(... ERROR_FILE <path>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_error_file = true;
            out_opt->error_file = flow_resolve_binary_relative_path(ctx, args.items[++i]);
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
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(COMMAND_ECHO) requires a value"),
                                     nob_sv_from_cstr("Usage: execute_process(... COMMAND_ECHO <STDOUT|STDERR|NONE>)"));
                return !eval_should_stop(ctx);
            }
            String_View value = args.items[++i];
            if (eval_sv_eq_ci_lit(value, "STDOUT")) {
                out_opt->command_echo = FLOW_EXEC_ECHO_STDOUT;
            } else if (eval_sv_eq_ci_lit(value, "STDERR")) {
                out_opt->command_echo = FLOW_EXEC_ECHO_STDERR;
            } else if (eval_sv_eq_ci_lit(value, "NONE")) {
                out_opt->command_echo = FLOW_EXEC_ECHO_NONE;
            } else {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(COMMAND_ECHO) received an invalid value"),
                                     value);
                return !eval_should_stop(ctx);
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
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(COMMAND_ERROR_IS_FATAL) requires a value"),
                                     nob_sv_from_cstr("Usage: execute_process(... COMMAND_ERROR_IS_FATAL <ANY|LAST>)"));
                return !eval_should_stop(ctx);
            }
            String_View value = args.items[++i];
            out_opt->has_command_error_is_fatal = true;
            if (eval_sv_eq_ci_lit(value, "ANY")) {
                out_opt->command_error_is_fatal = FLOW_EXEC_FATAL_ANY;
            } else if (eval_sv_eq_ci_lit(value, "LAST")) {
                out_opt->command_error_is_fatal = FLOW_EXEC_FATAL_LAST;
            } else if (eval_sv_eq_ci_lit(value, "NONE")) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(COMMAND_ERROR_IS_FATAL NONE) is not part of the CMake 3.28 baseline"),
                                     nob_sv_from_cstr("Use ANY or LAST for the 3.28 command surface"));
                return !eval_should_stop(ctx);
            } else {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(COMMAND_ERROR_IS_FATAL) received an invalid value"),
                                     value);
                return !eval_should_stop(ctx);
            }
            continue;
        }

        if (flow_arg_exact_ci(token, "ENCODING")) {
            if (i + 1 >= args.count) {
                (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), node->as.cmd.name, origin,
                                     nob_sv_from_cstr("execute_process(ENCODING) requires a value"),
                                     nob_sv_from_cstr("Usage: execute_process(... ENCODING <name>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_encoding = true;
            out_opt->encoding = args.items[++i];
            continue;
        }

        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("execute_process() received an unsupported argument"),
                             token);
        return !eval_should_stop(ctx);
    }

    if (out_opt->commands.count == 0) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("execute_process() requires at least one COMMAND clause"),
                             nob_sv_from_cstr("Usage: execute_process(COMMAND <cmd> [<arg>...] ...)"));
        return !eval_should_stop(ctx);
    }

    return true;
}

static bool flow_exec_read_file(Evaluator_Context *ctx, String_View path, String_View *out_text) {
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
    return !eval_should_stop(ctx);
}

static bool flow_exec_write_file(Evaluator_Context *ctx, String_View path, String_View content) {
    if (!ctx) return false;
    if (path.count == 0) return true;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    return nob_write_entire_file(path_c, content.data ? content.data : "", content.count);
}

static bool flow_exec_run_command(Evaluator_Context *ctx,
                                  const Flow_Exec_Command *cmd,
                                  String_View working_directory,
                                  String_View stdin_data,
                                  double deadline_seconds,
                                  Flow_Exec_Result *out_result) {
    if (!ctx || !cmd || !out_result || cmd->args.count == 0) return false;
    *out_result = (Flow_Exec_Result){0};

    Eval_Process_Run_Request req = {
        .argv = cmd->args,
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

static bool flow_exec_collect_results(Evaluator_Context *ctx,
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
    return !eval_should_stop(ctx);
}

static bool flow_exec_apply_outputs(Evaluator_Context *ctx,
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

    if (opt->has_result_variable && !eval_var_set(ctx, opt->result_variable, last_result)) return false;
    if (opt->has_results_variable && !eval_var_set(ctx, opt->results_variable, results_joined)) return false;

    if (opt->has_output_variable) {
        String_View to_set = opt->output_quiet ? nob_sv_from_cstr("")
                                               : (share_var ? merged_value : output_value);
        if (!eval_var_set(ctx, opt->output_variable, to_set)) return false;
    }
    if (opt->has_error_variable) {
        String_View to_set = opt->error_quiet ? nob_sv_from_cstr("")
                                              : (share_var ? merged_value : error_value);
        if (!eval_var_set(ctx, opt->error_variable, to_set)) return false;
    }

    if (opt->has_output_file && !opt->output_quiet) {
        String_View file_value = share_file ? merged_value : output_value;
        if (!flow_exec_write_file(ctx, opt->output_file, file_value)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("execute_process() failed to write OUTPUT_FILE"),
                                 opt->output_file);
            return !eval_should_stop(ctx);
        }
    }
    if (opt->has_error_file && !opt->error_quiet && !share_file) {
        if (!flow_exec_write_file(ctx, opt->error_file, error_value)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("execute_process() failed to write ERROR_FILE"),
                                 opt->error_file);
            return !eval_should_stop(ctx);
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
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("execute_process() child process failed"),
                                 last_result);
            eval_request_stop(ctx);
        }
    }

    return !eval_should_stop(ctx);
}

static bool flow_strip_bracket_arg(String_View in, String_View *out) {
    if (!out) return false;
    *out = in;
    if (in.count < 4 || !in.data || in.data[0] != '[') return false;

    size_t eq_count = 0;
    size_t i = 1;
    while (i < in.count && in.data[i] == '=') {
        eq_count++;
        i++;
    }
    if (i >= in.count || in.data[i] != '[') return false;
    size_t open_len = i + 1;
    if (in.count < open_len + 2 + eq_count) return false;

    size_t close_pos = in.count - (eq_count + 2);
    if (in.data[close_pos] != ']') return false;
    for (size_t k = 0; k < eq_count; k++) {
        if (in.data[close_pos + 1 + k] != '=') return false;
    }
    if (in.data[in.count - 1] != ']') return false;
    if (close_pos < open_len) return false;

    *out = nob_sv_from_parts(in.data + open_len, close_pos - open_len);
    return true;
}

static String_View flow_arg_flat(Evaluator_Context *ctx, const Arg *arg) {
    if (!ctx || !arg || arena_arr_len(arg->items) == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) total += arg->items[i].text.count;

    char *buf = (char*)arena_alloc(ctx->arena, total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) {
        String_View text = arg->items[i].text;
        if (text.count > 0) {
            memcpy(buf + off, text.data, text.count);
            off += text.count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static String_View flow_eval_arg_single(Evaluator_Context *ctx, const Arg *arg, bool expand_vars) {
    if (!ctx || !arg) return nob_sv_from_cstr("");

    String_View flat = flow_arg_flat(ctx, arg);
    String_View value = expand_vars ? eval_expand_vars(ctx, flat) : flat;
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    if (arg->kind == ARG_QUOTED) {
        if (value.count >= 2 && value.data[0] == '"' && value.data[value.count - 1] == '"') {
            return nob_sv_from_parts(value.data + 1, value.count - 2);
        }
        return value;
    }
    if (arg->kind == ARG_BRACKET) {
        String_View stripped = value;
        (void)flow_strip_bracket_arg(value, &stripped);
        return stripped;
    }
    return value;
}

static bool flow_clone_args_to_event_range(Evaluator_Context *ctx,
                                           const Args *src,
                                           size_t begin,
                                           Args *dst) {
    if (!ctx || !src || !dst) return false;
    *dst = NULL;
    if (begin >= arena_arr_len(*src)) return true;

    for (size_t i = begin; i < arena_arr_len(*src); i++) {
        const Arg *in = &(*src)[i];
        Arg out = {0};
        out.kind = in->kind;
        for (size_t k = 0; k < arena_arr_len(in->items); k++) {
            Token t = in->items[k];
            t.text = sv_copy_to_event_arena(ctx, t.text);
            if (eval_should_stop(ctx)) return false;
            if (!arena_arr_push(ctx->event_arena, out.items, t)) return ctx_oom(ctx);
        }
        if (!arena_arr_push(ctx->event_arena, *dst, out)) return ctx_oom(ctx);
    }
    return true;
}

static Eval_Deferred_Dir_Frame *flow_current_defer_dir(Evaluator_Context *ctx) {
    if (!ctx || ctx->deferred_dirs.count == 0) return NULL;
    return &ctx->deferred_dirs.items[ctx->deferred_dirs.count - 1];
}

static Eval_Deferred_Dir_Frame *flow_find_defer_dir(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return NULL;
    for (size_t i = ctx->deferred_dirs.count; i-- > 0;) {
        Eval_Deferred_Dir_Frame *frame = &ctx->deferred_dirs.items[i];
        if (flow_sv_eq_exact(frame->source_dir, path) || flow_sv_eq_exact(frame->binary_dir, path)) {
            return frame;
        }
    }
    return NULL;
}

static Eval_Deferred_Call *flow_find_deferred_call(Eval_Deferred_Dir_Frame *frame, String_View id, size_t *out_index) {
    if (!frame || id.count == 0) return NULL;
    for (size_t i = 0; i < frame->calls.count; i++) {
        if (!flow_sv_eq_exact(frame->calls.items[i].id, id)) continue;
        if (out_index) *out_index = i;
        return &frame->calls.items[i];
    }
    return NULL;
}

static bool flow_deferred_id_is_valid(String_View id) {
    if (id.count == 0 || !id.data) return false;
    char c0 = id.data[0];
    if (c0 >= 'A' && c0 <= 'Z') return false;
    return true;
}

static String_View flow_make_deferred_id(Evaluator_Context *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    char buf[64];
    ctx->next_deferred_call_id++;
    int n = snprintf(buf, sizeof(buf), "_defer_call_%zu", ctx->next_deferred_call_id);
    if (n <= 0 || (size_t)n >= sizeof(buf)) {
        ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    return sv_copy_to_event_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
}

static String_View flow_current_source_dir(Evaluator_Context *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (dir.count == 0) dir = ctx->source_dir;
    return dir;
}

static Eval_Deferred_Dir_Frame *flow_resolve_defer_directory(Evaluator_Context *ctx,
                                                             const Node *node,
                                                             bool has_directory,
                                                             String_View raw_directory) {
    if (!ctx) return NULL;
    if (!has_directory) return flow_current_defer_dir(ctx);

    String_View directory = raw_directory;
    if (!eval_sv_is_abs_path(directory)) {
        directory = eval_path_resolve_for_cmake_arg(ctx, directory, flow_current_source_dir(ctx), false);
        if (eval_should_stop(ctx)) return NULL;
    } else {
        directory = eval_sv_path_normalize_temp(ctx, directory);
    }

    Eval_Deferred_Dir_Frame *frame = flow_find_defer_dir(ctx, directory);
    if (frame) return frame;

    (void)eval_emit_diag(ctx,
                         EV_DIAG_ERROR,
                         nob_sv_from_cstr("flow"),
                         node->as.cmd.name,
                         eval_origin_from_node(ctx, node),
                         nob_sv_from_cstr("cmake_language(DEFER DIRECTORY ...) must name the current or an unfinished parent directory"),
                         raw_directory);
    return NULL;
}

static bool flow_append_defer_queue(Evaluator_Context *ctx,
                                    Eval_Deferred_Dir_Frame *frame,
                                    Eval_Deferred_Call call) {
    if (!ctx || !frame) return false;
    if (!arena_da_try_append(ctx->event_arena, &frame->calls, call)) return ctx_oom(ctx);
    return true;
}

bool eval_defer_push_directory(Evaluator_Context *ctx, String_View source_dir, String_View binary_dir) {
    if (!ctx) return false;
    Eval_Deferred_Dir_Frame frame = {0};
    frame.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    frame.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    if (eval_should_stop(ctx)) return false;
    if (!arena_da_try_append(ctx->event_arena, &ctx->deferred_dirs, frame)) return ctx_oom(ctx);
    return true;
}

bool eval_defer_pop_directory(Evaluator_Context *ctx) {
    if (!ctx) return false;
    if (ctx->deferred_dirs.count == 0) return true;
    ctx->deferred_dirs.count--;
    return true;
}

bool eval_defer_flush_current_directory(Evaluator_Context *ctx) {
    Eval_Deferred_Dir_Frame *frame = flow_current_defer_dir(ctx);
    if (!ctx || !frame) return true;

    while (frame->calls.count > 0) {
        Eval_Deferred_Call call = frame->calls.items[0];
        if (frame->calls.count > 1) {
            memmove(frame->calls.items, frame->calls.items + 1, (frame->calls.count - 1) * sizeof(frame->calls.items[0]));
        }
        frame->calls.count--;

        Node deferred = {0};
        deferred.kind = NODE_COMMAND;
        deferred.line = call.origin.line;
        deferred.col = call.origin.col;
        deferred.as.cmd.name = call.command_name;
        deferred.as.cmd.args = call.args;

        Ast_Root ast = NULL;
        if (!arena_arr_push(ctx->arena, ast, deferred)) return ctx_oom(ctx);
        if (!eval_run_ast_inline(ctx, ast)) return false;

        if (ctx->return_requested) ctx->return_requested = false;
        ctx->return_propagate_vars = NULL;
        ctx->return_propagate_count = 0;
        if (eval_should_stop(ctx)) return false;
        frame = flow_current_defer_dir(ctx);
        if (!frame) return true;
    }

    return true;
}

static bool flow_run_call(Evaluator_Context *ctx, const Node *node, const SV_List *args) {
    if (!ctx || !node || !args || args->count < 2) {
        return false;
    }

    String_View command_name = args->items[1];
    if (!flow_is_valid_command_name(command_name)) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("cmake_language(CALL) requires a valid command name"),
                             command_name);
        return !eval_should_stop(ctx);
    }
    if (flow_is_call_disallowed(command_name)) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("cmake_language(CALL) does not allow structural commands"),
                             command_name);
        return !eval_should_stop(ctx);
    }

    SV_List call_args = {0};
    call_args.items = (String_View*)(args->items + 2);
    call_args.count = args->count - 2;
    call_args.capacity = call_args.count;

    String_View script = nob_sv_from_cstr("");
    if (!flow_build_call_script(ctx, command_name, &call_args, &script)) return !eval_should_stop(ctx);

    Ast_Root ast = NULL;
    if (!flow_parse_inline_script(ctx, script, &ast)) return !eval_should_stop(ctx);
    if (!eval_run_ast_inline(ctx, ast)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

static bool flow_run_eval_code(Evaluator_Context *ctx, const Node *node, const SV_List *args) {
    if (!ctx || !node || !args || args->count < 3) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("cmake_language(EVAL CODE ...) requires code text"),
                             nob_sv_from_cstr("Usage: cmake_language(EVAL CODE <code>...)"));
        return !eval_should_stop(ctx);
    }

    size_t total = 0;
    for (size_t i = 2; i < args->count; i++) total += args->items[i].count;
    char *buf = (char*)arena_alloc(ctx->arena, total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    size_t off = 0;
    for (size_t i = 2; i < args->count; i++) {
        if (args->items[i].count > 0) {
            memcpy(buf + off, args->items[i].data, args->items[i].count);
            off += args->items[i].count;
        }
    }
    buf[off] = '\0';
    String_View code = nob_sv_from_parts(buf, off);

    Ast_Root ast = NULL;
    if (!flow_parse_inline_script(ctx, code, &ast)) return !eval_should_stop(ctx);
    if (!eval_run_ast_inline(ctx, ast)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

static bool flow_set_var_to_deferred_ids(Evaluator_Context *ctx,
                                         Eval_Deferred_Dir_Frame *frame,
                                         String_View out_var) {
    if (!ctx || !frame) return false;

    String_View *ids = NULL;
    if (frame->calls.count > 0) {
        ids = arena_alloc_array(ctx->arena, String_View, frame->calls.count);
        EVAL_OOM_RETURN_IF_NULL(ctx, ids, false);
        for (size_t i = 0; i < frame->calls.count; i++) ids[i] = frame->calls.items[i].id;
    }
    return eval_var_set(ctx, out_var, eval_sv_join_semi_temp(ctx, ids, frame->calls.count));
}

static bool flow_set_var_to_deferred_call(Evaluator_Context *ctx,
                                          Eval_Deferred_Call *call,
                                          String_View out_var) {
    if (!ctx || !call) return false;

    Nob_String_Builder sb = {0};
    if (!flow_append_sv(&sb, call->command_name)) {
        nob_sb_free(sb);
        return ctx_oom(ctx);
    }
    for (size_t i = 0; i < arena_arr_len(call->args); i++) {
        nob_sb_append(&sb, ';');
        String_View item = flow_eval_arg_single(ctx, &call->args[i], false);
        if (eval_should_stop(ctx) || !flow_append_sv(&sb, item)) {
            nob_sb_free(sb);
            return ctx_oom(ctx);
        }
    }

    char *copy = arena_strndup(ctx->arena, sb.items, sb.count);
    nob_sb_free(sb);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, false);
    return eval_var_set(ctx, out_var, nob_sv_from_parts(copy, strlen(copy)));
}

static bool flow_handle_defer(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node) return false;

    const Args *raw = &node->as.cmd.args;
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (arena_arr_len(*raw) < 2) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("cmake_language(DEFER) requires a subcommand"),
                             nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] <subcommand> ...)"));
        return !eval_should_stop(ctx);
    }

    size_t i = 1;
    bool has_directory = false;
    String_View directory = nob_sv_from_cstr("");
    String_View tok = flow_eval_arg_single(ctx, &(*raw)[i], true);
    if (eval_should_stop(ctx)) return false;
    if (eval_sv_eq_ci_lit(tok, "DIRECTORY")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER DIRECTORY) requires a directory argument"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER DIRECTORY <dir> ...)"));
            return !eval_should_stop(ctx);
        }
        has_directory = true;
        directory = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        i += 2;
        if (i >= arena_arr_len(*raw)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER) requires a subcommand"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] <subcommand> ...)"));
            return !eval_should_stop(ctx);
        }
    }

    String_View subcmd = flow_eval_arg_single(ctx, &(*raw)[i], true);
    if (eval_should_stop(ctx)) return false;
    Eval_Deferred_Dir_Frame *frame = flow_resolve_defer_directory(ctx, node, has_directory, directory);
    if (!frame) return !eval_should_stop(ctx);

    if (eval_sv_eq_ci_lit(subcmd, "GET_CALL_IDS")) {
        if (i + 2 != arena_arr_len(*raw)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER GET_CALL_IDS) expects one output variable"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] GET_CALL_IDS <out-var>)"));
            return !eval_should_stop(ctx);
        }
        String_View out_var = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        (void)flow_set_var_to_deferred_ids(ctx, frame, out_var);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(subcmd, "GET_CALL")) {
        if (i + 3 != arena_arr_len(*raw)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER GET_CALL) expects an id and one output variable"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] GET_CALL <id> <out-var>)"));
            return !eval_should_stop(ctx);
        }
        String_View id = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        String_View out_var = flow_eval_arg_single(ctx, &(*raw)[i + 2], true);
        if (eval_should_stop(ctx)) return false;
        Eval_Deferred_Call *call = flow_find_deferred_call(frame, id, NULL);
        if (!call) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER GET_CALL) requires a known deferred call id"),
                                 id);
            return !eval_should_stop(ctx);
        }
        (void)flow_set_var_to_deferred_call(ctx, call, out_var);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(subcmd, "CANCEL_CALL")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER CANCEL_CALL) requires at least one id"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] CANCEL_CALL <id>...)"));
            return !eval_should_stop(ctx);
        }
        for (size_t k = i + 1; k < arena_arr_len(*raw); k++) {
            String_View id = flow_eval_arg_single(ctx, &(*raw)[k], true);
            if (eval_should_stop(ctx)) return false;
            size_t idx = 0;
            if (!flow_find_deferred_call(frame, id, &idx)) continue;
            if (idx + 1 < frame->calls.count) {
                memmove(frame->calls.items + idx, frame->calls.items + idx + 1, (frame->calls.count - idx - 1) * sizeof(frame->calls.items[0]));
            }
            frame->calls.count--;
        }
        return !eval_should_stop(ctx);
    }

    String_View explicit_id = nob_sv_from_cstr("");
    String_View id_var = nob_sv_from_cstr("");
    while (eval_sv_eq_ci_lit(subcmd, "ID") || eval_sv_eq_ci_lit(subcmd, "ID_VAR")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER) option requires a value"),
                                 subcmd);
            return !eval_should_stop(ctx);
        }
        String_View value = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        if (eval_sv_eq_ci_lit(subcmd, "ID")) {
            explicit_id = value;
        } else {
            id_var = value;
        }
        i += 2;
        if (i >= arena_arr_len(*raw)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER) missing CALL subcommand"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] [ID <id>] [ID_VAR <var>] CALL <command> [<arg>...])"));
            return !eval_should_stop(ctx);
        }
        subcmd = flow_eval_arg_single(ctx, &(*raw)[i], true);
        if (eval_should_stop(ctx)) return false;
    }

    if (!eval_sv_eq_ci_lit(subcmd, "CALL")) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("Unsupported cmake_language(DEFER) subcommand"),
                             subcmd);
        return !eval_should_stop(ctx);
    }
    if (i + 1 >= arena_arr_len(*raw)) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("cmake_language(DEFER CALL) requires a command name"),
                             nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] [ID <id>] [ID_VAR <var>] CALL <command> [<arg>...])"));
        return !eval_should_stop(ctx);
    }

    String_View command_name = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
    if (eval_should_stop(ctx)) return false;
    if (!flow_is_valid_command_name(command_name)) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("cmake_language(DEFER CALL) requires a valid command name"),
                             command_name);
        return !eval_should_stop(ctx);
    }
    if (flow_is_call_disallowed(command_name)) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("cmake_language(DEFER CALL) does not allow structural commands"),
                             command_name);
        return !eval_should_stop(ctx);
    }

    String_View id = explicit_id.count > 0 ? explicit_id : flow_make_deferred_id(ctx);
    if (eval_should_stop(ctx)) return false;
    if (!flow_deferred_id_is_valid(id)) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("cmake_language(DEFER ID) requires an id that does not start with A-Z"),
                             id);
        return !eval_should_stop(ctx);
    }
    if (flow_find_deferred_call(frame, id, NULL)) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("cmake_language(DEFER ID) requires a unique id in the target directory"),
                             id);
        return !eval_should_stop(ctx);
    }

    Eval_Deferred_Call call = {0};
    call.origin = origin;
    call.id = sv_copy_to_event_arena(ctx, id);
    call.command_name = sv_copy_to_event_arena(ctx, command_name);
    if (eval_should_stop(ctx)) return false;
    if (!flow_clone_args_to_event_range(ctx, raw, i + 2, &call.args)) return false;
    if (!flow_append_defer_queue(ctx, frame, call)) return false;
    if (id_var.count > 0 && !eval_var_set(ctx, id_var, call.id)) return false;
    return !eval_should_stop(ctx);
}

bool eval_handle_cmake_language(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);

    if (args.count == 0) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("cmake_language() requires a subcommand"),
                             nob_sv_from_cstr("Supported here: CALL, EVAL CODE, DEFER, GET_MESSAGE_LOG_LEVEL"));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(args.items[0], "CALL")) {
        if (args.count < 2) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("cmake_language(CALL) requires a command name"),
                                 nob_sv_from_cstr("Usage: cmake_language(CALL <command> [<arg>...])"));
            return !eval_should_stop(ctx);
        }
        return flow_run_call(ctx, node, &args);
    }

    if (eval_sv_eq_ci_lit(args.items[0], "EVAL")) {
        if (args.count < 2 || !eval_sv_eq_ci_lit(args.items[1], "CODE")) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("cmake_language(EVAL) requires CODE"),
                                 nob_sv_from_cstr("Usage: cmake_language(EVAL CODE <code>...)"));
            return !eval_should_stop(ctx);
        }
        return flow_run_eval_code(ctx, node, &args);
    }

    if (eval_sv_eq_ci_lit(args.items[0], "GET_MESSAGE_LOG_LEVEL")) {
        if (args.count != 2) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("cmake_language(GET_MESSAGE_LOG_LEVEL) expects one output variable"),
                                 nob_sv_from_cstr("Usage: cmake_language(GET_MESSAGE_LOG_LEVEL <out-var>)"));
            return !eval_should_stop(ctx);
        }
        String_View value = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_MESSAGE_LOG_LEVEL"));
        (void)eval_var_set(ctx, args.items[1], value);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(args.items[0], "DEFER")) {
        return flow_handle_defer(ctx, node);
    }

    if (eval_sv_eq_ci_lit(args.items[0], "SET_DEPENDENCY_PROVIDER")) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("cmake_language() subcommand not implemented yet"),
                             args.items[0]);
        return !eval_should_stop(ctx);
    }

    (void)eval_emit_diag(ctx,
                         EV_DIAG_ERROR,
                         nob_sv_from_cstr("flow"),
                         node->as.cmd.name,
                         o,
                         nob_sv_from_cstr("Unsupported cmake_language() subcommand"),
                         args.items[0]);
    return !eval_should_stop(ctx);
}

bool eval_handle_execute_process(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    Flow_Exec_Options opt = {0};
    if (!flow_exec_parse_options(ctx, node, args, &opt)) return !eval_should_stop(ctx);

    String_View stdout_text = nob_sv_from_cstr("");
    String_View stderr_text = nob_sv_from_cstr("");
    String_View last_result = nob_sv_from_cstr("0");
    String_View results_joined = nob_sv_from_cstr("");
    bool had_error = false;
    if (!flow_exec_collect_results(ctx,
                                   &opt,
                                   &stdout_text,
                                   &stderr_text,
                                   &last_result,
                                   &results_joined,
                                   &had_error)) {
        return !eval_should_stop(ctx);
    }

    return flow_exec_apply_outputs(ctx, node, &opt, stdout_text, stderr_text, last_result, results_joined, had_error);
}

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

static bool flow_exec_program_parse(Evaluator_Context *ctx,
                                    const Node *node,
                                    const SV_List *args,
                                    Flow_Exec_Program_Compat *out) {
    if (!ctx || !node || !args || !out) return false;
    *out = (Flow_Exec_Program_Compat){0};

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (args->count == 0) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("exec_program() requires an executable"),
                             nob_sv_from_cstr("Usage: exec_program(<executable> [<working-dir>] [ARGS <arg-string>] [OUTPUT_VARIABLE <var>] [RETURN_VALUE <var>])"));
        return !eval_should_stop(ctx);
    }

    out->executable = args->items[0];
    size_t i = 1;
    if (i < args->count &&
        !flow_arg_exact_ci(args->items[i], "ARGS") &&
        !flow_arg_exact_ci(args->items[i], "OUTPUT_VARIABLE") &&
        !flow_arg_exact_ci(args->items[i], "RETURN_VALUE")) {
        out->has_working_directory = true;
        out->working_directory = args->items[i++];
    }

    for (; i < args->count; i++) {
        String_View token = args->items[i];
        if (flow_arg_exact_ci(token, "ARGS")) {
            if (out->has_args || i + 1 >= args->count) {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("flow"),
                                     node->as.cmd.name,
                                     origin,
                                     nob_sv_from_cstr("exec_program(ARGS) requires exactly one argument string"),
                                     nob_sv_from_cstr("Usage: exec_program(... ARGS \"<arg-string>\")"));
                return !eval_should_stop(ctx);
            }
            out->has_args = true;
            out->arg_string = args->items[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "OUTPUT_VARIABLE")) {
            if (out->has_output_variable || i + 1 >= args->count) {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("flow"),
                                     node->as.cmd.name,
                                     origin,
                                     nob_sv_from_cstr("exec_program(OUTPUT_VARIABLE) requires exactly one output variable"),
                                     nob_sv_from_cstr("Usage: exec_program(... OUTPUT_VARIABLE <var>)"));
                return !eval_should_stop(ctx);
            }
            out->has_output_variable = true;
            out->output_variable = args->items[++i];
            continue;
        }

        if (flow_arg_exact_ci(token, "RETURN_VALUE")) {
            if (out->has_return_value || i + 1 >= args->count) {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("flow"),
                                     node->as.cmd.name,
                                     origin,
                                     nob_sv_from_cstr("exec_program(RETURN_VALUE) requires exactly one output variable"),
                                     nob_sv_from_cstr("Usage: exec_program(... RETURN_VALUE <var>)"));
                return !eval_should_stop(ctx);
            }
            out->has_return_value = true;
            out->return_value = args->items[++i];
            continue;
        }

        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("exec_program() received an unsupported argument"),
                             token);
        return !eval_should_stop(ctx);
    }

    return true;
}

bool eval_handle_exec_program(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    String_View cmp0153 = eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0153"));
    if (eval_sv_eq_ci_lit(cmp0153, "NEW")) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("exec_program() is disallowed by CMP0153"),
                             nob_sv_from_cstr("Set CMP0153 to OLD only for legacy compatibility"));
        return !eval_should_stop(ctx);
    }

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    Flow_Exec_Program_Compat compat = {0};
    if (!flow_exec_program_parse(ctx, node, &args, &compat)) return !eval_should_stop(ctx);

    Flow_Exec_Command cmd = {0};
    if (!flow_exec_append_command_arg(ctx, &cmd, compat.executable)) return false;

    if (compat.has_args) {
        SV_List split_args = {0};
        if (!eval_split_command_line_temp(ctx, EVAL_CMDLINE_NATIVE, compat.arg_string, &split_args)) return false;
        for (size_t i = 0; i < split_args.count; i++) {
            if (!flow_exec_append_command_arg(ctx, &cmd, split_args.items[i])) return false;
        }
    }

    Flow_Exec_Options opt = {0};
    if (!flow_exec_append_command(ctx, &opt.commands, cmd)) return false;
    if (compat.has_working_directory) {
        opt.has_working_directory = true;
        opt.working_directory = flow_resolve_binary_relative_path(ctx, compat.working_directory);
        if (eval_should_stop(ctx)) return false;
    }
    if (compat.has_output_variable) {
        opt.has_output_variable = true;
        opt.output_variable = compat.output_variable;
    }
    if (compat.has_return_value) {
        opt.has_result_variable = true;
        opt.result_variable = compat.return_value;
    }

    String_View stdout_text = nob_sv_from_cstr("");
    String_View stderr_text = nob_sv_from_cstr("");
    String_View last_result = nob_sv_from_cstr("0");
    String_View results_joined = nob_sv_from_cstr("");
    bool had_error = false;
    if (!flow_exec_collect_results(ctx,
                                   &opt,
                                   &stdout_text,
                                   &stderr_text,
                                   &last_result,
                                   &results_joined,
                                   &had_error)) {
        return !eval_should_stop(ctx);
    }

    return flow_exec_apply_outputs(ctx, node, &opt, stdout_text, stderr_text, last_result, results_joined, had_error);
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
