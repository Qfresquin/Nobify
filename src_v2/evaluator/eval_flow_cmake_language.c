#include "eval_flow_internal.h"

#include <stdio.h>
#include <string.h>

static bool flow_deferred_call_list_pop_front(Eval_Deferred_Call_List *calls, Eval_Deferred_Call *out_call) {
    if (!calls || !out_call || arena_arr_len(*calls) == 0) return false;
    *out_call = (*calls)[0];
    if (arena_arr_len(*calls) > 1) {
        memmove(*calls, *calls + 1, (arena_arr_len(*calls) - 1) * sizeof((*calls)[0]));
    }
    arena_arr_set_len(*calls, arena_arr_len(*calls) - 1);
    return true;
}

static bool flow_deferred_call_list_remove_at(Eval_Deferred_Call_List *calls, size_t idx) {
    if (!calls || idx >= arena_arr_len(*calls)) return false;
    if (idx + 1 < arena_arr_len(*calls)) {
        memmove(*calls + idx, *calls + idx + 1, (arena_arr_len(*calls) - idx - 1) * sizeof((*calls)[0]));
    }
    arena_arr_set_len(*calls, arena_arr_len(*calls) - 1);
    return true;
}

static Eval_Deferred_Dir_Frame *flow_current_defer_dir(Evaluator_Context *ctx) {
    if (!ctx || arena_arr_len(ctx->deferred_dirs) == 0) return NULL;
    return &ctx->deferred_dirs[arena_arr_len(ctx->deferred_dirs) - 1];
}

static Eval_Deferred_Dir_Frame *flow_find_defer_dir(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return NULL;
    for (size_t i = arena_arr_len(ctx->deferred_dirs); i-- > 0;) {
        Eval_Deferred_Dir_Frame *frame = &ctx->deferred_dirs[i];
        if (flow_sv_eq_exact(frame->source_dir, path) || flow_sv_eq_exact(frame->binary_dir, path)) {
            return frame;
        }
    }
    return NULL;
}

static Eval_Deferred_Call *flow_find_deferred_call(Eval_Deferred_Dir_Frame *frame, String_View id, size_t *out_index) {
    if (!frame || id.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(frame->calls); i++) {
        if (!flow_sv_eq_exact(frame->calls[i].id, id)) continue;
        if (out_index) *out_index = i;
        return &frame->calls[i];
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

    (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("cmake_language(DEFER DIRECTORY ...) must name the current or an unfinished parent directory"), raw_directory);
    return NULL;
}

static bool flow_append_defer_queue(Evaluator_Context *ctx,
                                    Eval_Deferred_Dir_Frame *frame,
                                    Eval_Deferred_Call call) {
    if (!ctx || !frame) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, frame->calls, call);
}

bool eval_defer_push_directory(Evaluator_Context *ctx, String_View source_dir, String_View binary_dir) {
    if (!ctx) return false;
    Eval_Deferred_Dir_Frame frame = {0};
    frame.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    frame.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->deferred_dirs, frame);
}

bool eval_defer_pop_directory(Evaluator_Context *ctx) {
    if (!ctx) return false;
    if (arena_arr_len(ctx->deferred_dirs) == 0) return true;
    arena_arr_set_len(ctx->deferred_dirs, arena_arr_len(ctx->deferred_dirs) - 1);
    return true;
}

bool eval_defer_flush_current_directory(Evaluator_Context *ctx) {
    Eval_Deferred_Dir_Frame *frame = flow_current_defer_dir(ctx);
    if (!ctx || !frame) return true;
    if (arena_arr_len(frame->calls) > 0) {
        Event_Origin origin = {0};
        origin.file_path = nob_sv_from_cstr(ctx->current_file ? ctx->current_file : "");
        if (!eval_emit_flow_defer_flush(ctx, origin, (uint32_t)arena_arr_len(frame->calls))) return false;
    }

    while (arena_arr_len(frame->calls) > 0) {
        Eval_Deferred_Call call = {0};
        if (!flow_deferred_call_list_pop_front(&frame->calls, &call)) return false;

        Node deferred = {0};
        deferred.kind = NODE_COMMAND;
        deferred.line = call.origin.line;
        deferred.col = call.origin.col;
        deferred.as.cmd.name = call.command_name;
        deferred.as.cmd.args = call.args;

        Ast_Root ast = NULL;
        if (!EVAL_ARR_PUSH(ctx, ctx->arena, ast, deferred)) return false;
        Eval_Result inline_result = eval_run_ast_inline(ctx, ast);
        if (eval_result_is_fatal(inline_result)) return false;

        eval_clear_return_state(ctx);
        if (eval_should_stop(ctx)) return false;
        frame = flow_current_defer_dir(ctx);
        if (!frame) return true;
    }

    return true;
}

static bool flow_run_call(Evaluator_Context *ctx, const Node *node, const SV_List *args) {
    if (!ctx || !node || !args || arena_arr_len(*args) < 2) {
        return false;
    }

    String_View command_name = (*args)[1];
    if (!flow_is_valid_command_name(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("cmake_language(CALL) requires a valid command name"), command_name);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (flow_is_call_disallowed(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("cmake_language(CALL) does not allow structural commands"), command_name);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    const String_View *call_args = &(*args)[2];
    size_t call_arg_count = arena_arr_len(*args) - 2;

    String_View script = nob_sv_from_cstr("");
    {
        SV_List tmp = NULL;
        for (size_t i = 0; i < call_arg_count; i++) {
            if (!EVAL_ARR_PUSH(ctx, ctx->arena, tmp, call_args[i])) return false;
        }
        if (!flow_build_call_script(ctx, command_name, &tmp, &script)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    Ast_Root ast = NULL;
    if (!flow_parse_inline_script(ctx, script, &ast)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    Eval_Result inline_result = eval_run_ast_inline(ctx, ast);
    if (eval_result_is_fatal(inline_result)) return false;
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool flow_run_eval_code(Evaluator_Context *ctx, const Node *node, const SV_List *args) {
    if (!ctx || !node || !args || arena_arr_len(*args) < 3) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("cmake_language(EVAL CODE ...) requires code text"), nob_sv_from_cstr("Usage: cmake_language(EVAL CODE <code>...)"));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    size_t total = 0;
    for (size_t i = 2; i < arena_arr_len(*args); i++) total += (*args)[i].count;
    char *buf = (char*)arena_alloc(ctx->arena, total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    size_t off = 0;
    for (size_t i = 2; i < arena_arr_len(*args); i++) {
        if ((*args)[i].count > 0) {
            memcpy(buf + off, (*args)[i].data, (*args)[i].count);
            off += (*args)[i].count;
        }
    }
    buf[off] = '\0';
    String_View code = nob_sv_from_parts(buf, off);

    Ast_Root ast = NULL;
    if (!flow_parse_inline_script(ctx, code, &ast)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    Eval_Result inline_result = eval_run_ast_inline(ctx, ast);
    if (eval_result_is_fatal(inline_result)) return false;
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool flow_set_var_to_deferred_ids(Evaluator_Context *ctx,
                                         Eval_Deferred_Dir_Frame *frame,
                                         String_View out_var) {
    if (!ctx || !frame) return false;

    String_View *ids = NULL;
    if (arena_arr_len(frame->calls) > 0) {
        ids = arena_alloc_array(ctx->arena, String_View, arena_arr_len(frame->calls));
        EVAL_OOM_RETURN_IF_NULL(ctx, ids, false);
        for (size_t i = 0; i < arena_arr_len(frame->calls); i++) ids[i] = frame->calls[i].id;
    }
    return eval_var_set_current(ctx, out_var, eval_sv_join_semi_temp(ctx, ids, arena_arr_len(frame->calls)));
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
    return eval_var_set_current(ctx, out_var, nob_sv_from_parts(copy, strlen(copy)));
}

static bool flow_handle_defer(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node) return false;

    const Args *raw = &node->as.cmd.args;
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (arena_arr_len(*raw) < 2) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER) requires a subcommand"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] <subcommand> ...)"));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    size_t i = 1;
    bool has_directory = false;
    String_View directory = nob_sv_from_cstr("");
    String_View tok = flow_eval_arg_single(ctx, &(*raw)[i], true);
    if (eval_should_stop(ctx)) return false;
    if (eval_sv_eq_ci_lit(tok, "DIRECTORY")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER DIRECTORY) requires a directory argument"), nob_sv_from_cstr("Usage: cmake_language(DEFER DIRECTORY <dir> ...)"));
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
        has_directory = true;
        directory = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        i += 2;
        if (i >= arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER) requires a subcommand"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] <subcommand> ...)"));
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
    }

    String_View subcmd = flow_eval_arg_single(ctx, &(*raw)[i], true);
    if (eval_should_stop(ctx)) return false;
    Eval_Deferred_Dir_Frame *frame = flow_resolve_defer_directory(ctx, node, has_directory, directory);
    if (!frame) return !eval_result_is_fatal(eval_result_from_ctx(ctx));

    if (eval_sv_eq_ci_lit(subcmd, "GET_CALL_IDS")) {
        if (i + 2 != arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER GET_CALL_IDS) expects one output variable"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] GET_CALL_IDS <out-var>)"));
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
        String_View out_var = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        (void)flow_set_var_to_deferred_ids(ctx, frame, out_var);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    if (eval_sv_eq_ci_lit(subcmd, "GET_CALL")) {
        if (i + 3 != arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER GET_CALL) expects an id and one output variable"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] GET_CALL <id> <out-var>)"));
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
        String_View id = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        String_View out_var = flow_eval_arg_single(ctx, &(*raw)[i + 2], true);
        if (eval_should_stop(ctx)) return false;
        Eval_Deferred_Call *call = flow_find_deferred_call(frame, id, NULL);
        if (!call) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER GET_CALL) requires a known deferred call id"), id);
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
        (void)flow_set_var_to_deferred_call(ctx, call, out_var);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    if (eval_sv_eq_ci_lit(subcmd, "CANCEL_CALL")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER CANCEL_CALL) requires at least one id"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] CANCEL_CALL <id>...)"));
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
        for (size_t k = i + 1; k < arena_arr_len(*raw); k++) {
            String_View id = flow_eval_arg_single(ctx, &(*raw)[k], true);
            if (eval_should_stop(ctx)) return false;
            size_t idx = 0;
            if (!flow_find_deferred_call(frame, id, &idx)) continue;
            (void)flow_deferred_call_list_remove_at(&frame->calls, idx);
        }
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    String_View explicit_id = nob_sv_from_cstr("");
    String_View id_var = nob_sv_from_cstr("");
    while (eval_sv_eq_ci_lit(subcmd, "ID") || eval_sv_eq_ci_lit(subcmd, "ID_VAR")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER) option requires a value"), subcmd);
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER) missing CALL subcommand"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] [ID <id>] [ID_VAR <var>] CALL <command> [<arg>...])"));
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
        subcmd = flow_eval_arg_single(ctx, &(*raw)[i], true);
        if (eval_should_stop(ctx)) return false;
    }

    if (!eval_sv_eq_ci_lit(subcmd, "CALL")) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("Unsupported cmake_language(DEFER) subcommand"), subcmd);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (i + 1 >= arena_arr_len(*raw)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER CALL) requires a command name"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] [ID <id>] [ID_VAR <var>] CALL <command> [<arg>...])"));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    String_View command_name = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
    if (eval_should_stop(ctx)) return false;
    if (!flow_is_valid_command_name(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER CALL) requires a valid command name"), command_name);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (flow_is_call_disallowed(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER CALL) does not allow structural commands"), command_name);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    String_View id = explicit_id.count > 0 ? explicit_id : flow_make_deferred_id(ctx);
    if (eval_should_stop(ctx)) return false;
    if (!flow_deferred_id_is_valid(id)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER ID) requires an id that does not start with A-Z"), id);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (flow_find_deferred_call(frame, id, NULL)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER ID) requires a unique id in the target directory"), id);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    Eval_Deferred_Call call = {0};
    call.origin = origin;
    call.id = sv_copy_to_event_arena(ctx, id);
    call.command_name = sv_copy_to_event_arena(ctx, command_name);
    if (eval_should_stop(ctx)) return false;
    if (!flow_clone_args_to_event_range(ctx, raw, i + 2, &call.args)) return false;
    if (!flow_append_defer_queue(ctx, frame, call)) return false;
    if (!eval_emit_flow_defer_queue(ctx, origin, call.id, call.command_name)) return false;
    if (id_var.count > 0 && !eval_var_set_current(ctx, id_var, call.id)) return false;
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

Eval_Result eval_handle_cmake_language(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);

    if (arena_arr_len(args) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "flow", nob_sv_from_cstr("cmake_language() requires a subcommand"), nob_sv_from_cstr("Supported here: CALL, EVAL CODE, DEFER, GET_MESSAGE_LOG_LEVEL"));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(args[0], "CALL")) {
        if (arena_arr_len(args) < 2) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "flow", nob_sv_from_cstr("cmake_language(CALL) requires a command name"), nob_sv_from_cstr("Usage: cmake_language(CALL <command> [<arg>...])"));
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_cmake_language_call(ctx, o, args[1])) return eval_result_fatal();
        return eval_result_from_bool(flow_run_call(ctx, node, &args));
    }

    if (eval_sv_eq_ci_lit(args[0], "EVAL")) {
        if (arena_arr_len(args) < 2 || !eval_sv_eq_ci_lit(args[1], "CODE")) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "flow", nob_sv_from_cstr("cmake_language(EVAL) requires CODE"), nob_sv_from_cstr("Usage: cmake_language(EVAL CODE <code>...)"));
            return eval_result_from_ctx(ctx);
        }
        String_View code = arena_arr_len(args) > 2
            ? eval_sv_join_semi_temp(ctx, args + 2, arena_arr_len(args) - 2)
            : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return eval_result_fatal();
        if (!eval_emit_cmake_language_eval(ctx, o, code)) return eval_result_fatal();
        return eval_result_from_bool(flow_run_eval_code(ctx, node, &args));
    }

    if (eval_sv_eq_ci_lit(args[0], "GET_MESSAGE_LOG_LEVEL")) {
        if (arena_arr_len(args) != 2) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "flow", nob_sv_from_cstr("cmake_language(GET_MESSAGE_LOG_LEVEL) expects one output variable"), nob_sv_from_cstr("Usage: cmake_language(GET_MESSAGE_LOG_LEVEL <out-var>)"));
            return eval_result_from_ctx(ctx);
        }
        String_View value = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_MESSAGE_LOG_LEVEL"));
        (void)eval_var_set_current(ctx, args[1], value);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(args[0], "DEFER")) {
        bool ok = flow_handle_defer(ctx, node);
        if (ok && !eval_should_stop(ctx)) {
            String_View command_name = nob_sv_from_cstr("");
            for (size_t i = 1; i + 1 < arena_arr_len(args); ++i) {
                if (eval_sv_eq_ci_lit(args[i], "CALL")) {
                    command_name = args[i + 1];
                    break;
                }
            }
            if (!eval_emit_cmake_language_defer_queue(ctx, o, nob_sv_from_cstr(""), command_name)) return eval_result_fatal();
        }
        return eval_result_from_bool(ok);
    }

    if (eval_sv_eq_ci_lit(args[0], "SET_DEPENDENCY_PROVIDER")) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_IMPLEMENTED, "flow", nob_sv_from_cstr("cmake_language() subcommand not implemented yet"), args[0]);
        return eval_result_from_ctx(ctx);
    }

    (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "flow", nob_sv_from_cstr("Unsupported cmake_language() subcommand"), args[0]);
    return eval_result_from_ctx(ctx);
}
