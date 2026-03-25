#include "eval_flow_internal.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    FLOW_CMAKE_LANGUAGE_REQUEST_NONE = 0,
    FLOW_CMAKE_LANGUAGE_REQUEST_CALL,
    FLOW_CMAKE_LANGUAGE_REQUEST_EVAL_CODE,
    FLOW_CMAKE_LANGUAGE_REQUEST_GET_MESSAGE_LOG_LEVEL,
    FLOW_CMAKE_LANGUAGE_REQUEST_DEFER,
    FLOW_CMAKE_LANGUAGE_REQUEST_SET_DEPENDENCY_PROVIDER,
} Flow_Cmake_Language_Request_Kind;

typedef enum {
    FLOW_DEFERRED_ID_VALID = 0,
    FLOW_DEFERRED_ID_INVALID_EMPTY,
    FLOW_DEFERRED_ID_INVALID_CAPITAL,
    FLOW_DEFERRED_ID_INVALID_UNDERSCORE,
} Flow_Deferred_Id_Validation;

typedef struct {
    Flow_Cmake_Language_Request_Kind kind;
    union {
        struct {
            SV_List args;
            String_View command_name;
        } call;
        struct {
            SV_List args;
            SV_List code_parts;
            size_t code_part_count;
        } eval_code;
        struct {
            String_View out_var;
        } get_message_log_level;
        struct {
            String_View queued_command_name;
        } defer;
        struct {
            SV_List args;
        } set_dependency_provider;
    } as;
} Flow_Cmake_Language_Request;

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

static Eval_Deferred_Dir_Frame *flow_current_defer_dir(EvalExecContext *ctx) {
    if (!ctx || arena_arr_len(ctx->file_state.deferred_dirs) == 0) return NULL;
    return &ctx->file_state.deferred_dirs[arena_arr_len(ctx->file_state.deferred_dirs) - 1];
}

static Eval_Deferred_Dir_Frame *flow_find_defer_dir(EvalExecContext *ctx, String_View path) {
    if (!ctx || path.count == 0) return NULL;
    for (size_t i = arena_arr_len(ctx->file_state.deferred_dirs); i-- > 0;) {
        Eval_Deferred_Dir_Frame *frame = &ctx->file_state.deferred_dirs[i];
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

static bool flow_remove_all_deferred_calls_with_id(Eval_Deferred_Dir_Frame *frame, String_View id) {
    if (!frame || id.count == 0) return false;
    for (size_t i = arena_arr_len(frame->calls); i-- > 0;) {
        if (!flow_sv_eq_exact(frame->calls[i].id, id)) continue;
        if (!flow_deferred_call_list_remove_at(&frame->calls, i)) return false;
    }
    return true;
}

static bool flow_generated_deferred_id_exists(EvalExecContext *ctx, String_View id) {
    if (!ctx || id.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(ctx->file_state.generated_deferred_ids); i++) {
        if (flow_sv_eq_exact(ctx->file_state.generated_deferred_ids[i], id)) return true;
    }
    return false;
}

static bool flow_record_generated_deferred_id(EvalExecContext *ctx, String_View id) {
    if (!ctx || id.count == 0) return false;
    if (flow_generated_deferred_id_exists(ctx, id)) return true;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->file_state.generated_deferred_ids, id);
}

static Flow_Deferred_Id_Validation flow_validate_deferred_id(EvalExecContext *ctx,
                                                             String_View id,
                                                             bool allow_generated_prefix) {
    if (id.count == 0 || !id.data) return FLOW_DEFERRED_ID_INVALID_EMPTY;

    char c0 = id.data[0];
    if (c0 >= 'A' && c0 <= 'Z') return FLOW_DEFERRED_ID_INVALID_CAPITAL;
    if (c0 == '_' && !allow_generated_prefix && !flow_generated_deferred_id_exists(ctx, id)) {
        return FLOW_DEFERRED_ID_INVALID_UNDERSCORE;
    }

    return FLOW_DEFERRED_ID_VALID;
}

static String_View flow_deferred_id_validation_cause(Flow_Deferred_Id_Validation validation) {
    switch (validation) {
    case FLOW_DEFERRED_ID_INVALID_EMPTY:
        return nob_sv_from_cstr("cmake_language(DEFER ID) requires a non-empty id");
    case FLOW_DEFERRED_ID_INVALID_CAPITAL:
        return nob_sv_from_cstr("cmake_language(DEFER ID) requires an id that does not start with A-Z");
    case FLOW_DEFERRED_ID_INVALID_UNDERSCORE:
        return nob_sv_from_cstr("cmake_language(DEFER ID) only allows a leading '_' for ids generated earlier by ID_VAR");
    case FLOW_DEFERRED_ID_VALID:
    default:
        return nob_sv_from_cstr("");
    }
}

static String_View flow_make_deferred_id(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    char buf[64];
    ctx->file_state.next_deferred_call_id++;
    int n = snprintf(buf, sizeof(buf), "_defer_call_%zu", ctx->file_state.next_deferred_call_id);
    if (n <= 0 || (size_t)n >= sizeof(buf)) {
        ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    String_View id = sv_copy_to_event_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (!flow_record_generated_deferred_id(ctx, id)) return nob_sv_from_cstr("");
    return id;
}

static Eval_Deferred_Dir_Frame *flow_resolve_defer_directory(EvalExecContext *ctx,
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

static bool flow_append_defer_queue(EvalExecContext *ctx,
                                    Eval_Deferred_Dir_Frame *frame,
                                    Eval_Deferred_Call call) {
    if (!ctx || !frame) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, frame->calls, call);
}

bool eval_defer_push_directory(EvalExecContext *ctx, String_View source_dir, String_View binary_dir) {
    if (!ctx) return false;
    Eval_Deferred_Dir_Frame frame = {0};
    frame.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    frame.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->file_state.deferred_dirs, frame);
}

bool eval_defer_pop_directory(EvalExecContext *ctx) {
    if (!ctx) return false;
    if (arena_arr_len(ctx->file_state.deferred_dirs) == 0) return true;
    arena_arr_set_len(ctx->file_state.deferred_dirs, arena_arr_len(ctx->file_state.deferred_dirs) - 1);
    return true;
}

bool eval_defer_flush_current_directory(EvalExecContext *ctx) {
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
        Eval_Exec_Context exec = {0};
        exec.kind = EVAL_EXEC_CTX_DEFERRED;
        exec.return_context = ctx->return_context;
        exec.source_dir = eval_current_source_dir(ctx);
        exec.binary_dir = eval_current_binary_dir(ctx);
        exec.list_dir = eval_current_list_dir(ctx);
        exec.current_file = ctx->current_file;
        if (!eval_exec_push(ctx, exec)) return false;
        if (!eval_exec_publish_current_vars(ctx)) {
            eval_exec_pop(ctx);
            return false;
        }
        Eval_Result inline_result = eval_run_ast_inline(ctx, ast);
        eval_exec_pop(ctx);
        if (!eval_exec_publish_current_vars(ctx)) return false;
        if (eval_result_is_fatal(inline_result)) return false;

        eval_clear_return_state(ctx);
        if (eval_should_stop(ctx)) return false;
        frame = flow_current_defer_dir(ctx);
        if (!frame) return true;
    }

    return true;
}

static bool flow_run_call(EvalExecContext *ctx, const Node *node, const SV_List *args) {
    if (!ctx || !node || !args || arena_arr_len(*args) < 2) {
        return false;
    }

    String_View command_name = (*args)[1];
    if (!flow_is_valid_command_name(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("cmake_language(CALL) requires a valid command name"), command_name);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (flow_is_call_disallowed(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("cmake_language(CALL) does not allow structural commands"), command_name);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    const String_View *call_args = &(*args)[2];
    size_t call_arg_count = arena_arr_len(*args) - 2;

    String_View script = nob_sv_from_cstr("");
    {
        SV_List tmp = NULL;
        for (size_t i = 0; i < call_arg_count; i++) {
            if (!EVAL_ARR_PUSH(ctx, ctx->arena, tmp, call_args[i])) return false;
        }
        if (!flow_build_call_script(ctx, command_name, &tmp, &script)) { if (eval_should_stop(ctx)) return false; return true; }
    }

    Ast_Root ast = NULL;
    if (!flow_parse_inline_script(ctx, script, &ast)) { if (eval_should_stop(ctx)) return false; return true; }
    eval_command_tx_preserve_scope_vars_on_failure(ctx);
    Eval_Result inline_result = eval_run_ast_inline(ctx, ast);
    if (eval_result_is_fatal(inline_result)) return false;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool flow_run_eval_code(EvalExecContext *ctx, const Node *node, const SV_List *args) {
    if (!ctx || !node || !args || arena_arr_len(*args) < 3) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("cmake_language(EVAL CODE ...) requires code text"), nob_sv_from_cstr("Usage: cmake_language(EVAL CODE <code>...)"));
        if (eval_should_stop(ctx)) return false;
        return true;
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
    if (!flow_parse_inline_script(ctx, code, &ast)) { if (eval_should_stop(ctx)) return false; return true; }
    eval_command_tx_preserve_scope_vars_on_failure(ctx);
    Eval_Result inline_result = eval_run_ast_inline(ctx, ast);
    if (eval_result_is_fatal(inline_result)) return false;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static String_View flow_cmake_language_find_defer_call_command_name(SV_List args) {
    for (size_t i = 1; i + 1 < arena_arr_len(args); ++i) {
        if (eval_sv_eq_ci_lit(args[i], "CALL")) return args[i + 1];
    }
    return nob_sv_from_cstr("");
}

static bool flow_cmake_language_is_file_scope(EvalExecContext *ctx) {
    return eval_exec_is_file_scope(ctx);
}

static bool flow_cmake_language_provider_command_exists(EvalExecContext *ctx, String_View command_name) {
    if (!ctx || command_name.count == 0) return false;
    return eval_user_cmd_find(ctx, command_name) != NULL;
}

static bool flow_cmake_language_provider_context_is_active(EvalExecContext *ctx) {
    if (!ctx || !ctx->current_file || ctx->dependency_provider_context_file.count == 0) return false;
    return flow_sv_eq_exact(nob_sv_from_cstr(ctx->current_file), ctx->dependency_provider_context_file);
}

static bool flow_cmake_language_set_dependency_provider(EvalExecContext *ctx,
                                                        const Node *node,
                                                        const SV_List *args) {
    if (!ctx || !node || !args) return false;

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!flow_cmake_language_is_file_scope(ctx)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_INVALID_CONTEXT,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) must be called at file scope"),
                                 nob_sv_from_cstr("Do not call it from inside function(), macro(), or block() scopes"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (arena_arr_len(*args) < 2) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_MISSING_REQUIRED,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) requires a command or an empty string"),
                                 nob_sv_from_cstr("Usage: cmake_language(SET_DEPENDENCY_PROVIDER <command> SUPPORTED_METHODS FIND_PACKAGE) or cmake_language(SET_DEPENDENCY_PROVIDER \"\")"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (!flow_cmake_language_provider_context_is_active(ctx)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_INVALID_CONTEXT,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) may only be called from a file listed in CMAKE_PROJECT_TOP_LEVEL_INCLUDES during the first project()"),
                                 nob_sv_from_cstr("Move this call into a CMAKE_PROJECT_TOP_LEVEL_INCLUDES file processed by the first project() call"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    String_View command_name = (*args)[1];
    if (command_name.count == 0) {
        if (arena_arr_len(*args) != 2) {
            (void)EVAL_DIAG_EMIT_SEV(ctx,
                                     EV_DIAG_ERROR,
                                     EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                     nob_sv_from_cstr("flow"),
                                     node->as.cmd.name,
                                     origin,
                                     nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER \"\") does not accept SUPPORTED_METHODS"),
                                     nob_sv_from_cstr("Usage: cmake_language(SET_DEPENDENCY_PROVIDER \"\")"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        ctx->semantic_state.package.dependency_provider.command_name = nob_sv_from_cstr("");
        ctx->semantic_state.package.dependency_provider.supports_find_package = false;
        ctx->semantic_state.package.dependency_provider.supports_fetchcontent_makeavailable_serial = false;
        ctx->semantic_state.package.dependency_provider.active_find_package_depth = 0;
        ctx->semantic_state.package.dependency_provider.active_fetchcontent_makeavailable_depth = 0;
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (!flow_is_valid_command_name(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_INVALID_VALUE,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) requires a valid command name"),
                                 command_name);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (!flow_cmake_language_provider_command_exists(ctx, command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_NOT_FOUND,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) requires an existing function() or macro()"),
                                 command_name);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (arena_arr_len(*args) < 4 || !eval_sv_eq_ci_lit((*args)[2], "SUPPORTED_METHODS")) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_MISSING_REQUIRED,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) requires SUPPORTED_METHODS"),
                                 nob_sv_from_cstr("Usage: cmake_language(SET_DEPENDENCY_PROVIDER <command> SUPPORTED_METHODS FIND_PACKAGE)"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    bool supports_find_package = false;
    bool supports_fetchcontent = false;
    for (size_t i = 3; i < arena_arr_len(*args); i++) {
        String_View method = (*args)[i];
        if (eval_sv_eq_ci_lit(method, "FIND_PACKAGE")) {
            supports_find_package = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(method, "FETCHCONTENT_MAKEAVAILABLE_SERIAL")) {
            supports_fetchcontent = true;
            continue;
        }
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_INVALID_VALUE,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) received an unknown method"),
                                 method);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (!supports_find_package && !supports_fetchcontent) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_MISSING_REQUIRED,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) requires at least one supported method"),
                                 nob_sv_from_cstr("Supported here: FIND_PACKAGE, FETCHCONTENT_MAKEAVAILABLE_SERIAL"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    ctx->semantic_state.package.dependency_provider.command_name = sv_copy_to_event_arena(ctx, command_name);
    if (eval_should_stop(ctx)) return false;
    ctx->semantic_state.package.dependency_provider.supports_find_package = supports_find_package;
    ctx->semantic_state.package.dependency_provider.supports_fetchcontent_makeavailable_serial = supports_fetchcontent;
    ctx->semantic_state.package.dependency_provider.active_find_package_depth = 0;
    ctx->semantic_state.package.dependency_provider.active_fetchcontent_makeavailable_depth = 0;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool flow_set_var_to_deferred_ids(EvalExecContext *ctx,
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

static bool flow_set_var_to_deferred_call(EvalExecContext *ctx,
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

static bool flow_handle_defer(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node) return false;

    const Args *raw = &node->as.cmd.args;
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (arena_arr_len(*raw) < 2) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER) requires a subcommand"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] <subcommand> ...)"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    size_t i = 1;
    bool has_directory = false;
    String_View directory = nob_sv_from_cstr("");
    String_View tok = flow_eval_arg_single(ctx, &(*raw)[i], true);
    if (eval_should_stop(ctx)) return false;
    if (eval_sv_eq_ci_lit(tok, "DIRECTORY")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER DIRECTORY) requires a directory argument"), nob_sv_from_cstr("Usage: cmake_language(DEFER DIRECTORY <dir> ...)"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        has_directory = true;
        directory = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        i += 2;
        if (i >= arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER) requires a subcommand"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] <subcommand> ...)"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
    }

    String_View subcmd = flow_eval_arg_single(ctx, &(*raw)[i], true);
    if (eval_should_stop(ctx)) return false;
    Eval_Deferred_Dir_Frame *frame = flow_resolve_defer_directory(ctx, node, has_directory, directory);
    if (!frame) { if (eval_should_stop(ctx)) return false; return true; }

    if (eval_sv_eq_ci_lit(subcmd, "GET_CALL_IDS")) {
        if (i + 2 != arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER GET_CALL_IDS) expects one output variable"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] GET_CALL_IDS <out-var>)"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        String_View out_var = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        (void)flow_set_var_to_deferred_ids(ctx, frame, out_var);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (eval_sv_eq_ci_lit(subcmd, "GET_CALL")) {
        if (i + 3 != arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER GET_CALL) expects an id and one output variable"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] GET_CALL <id> <out-var>)"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        String_View id = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
        String_View out_var = flow_eval_arg_single(ctx, &(*raw)[i + 2], true);
        if (eval_should_stop(ctx)) return false;
        Eval_Deferred_Call *call = flow_find_deferred_call(frame, id, NULL);
        if (!call) {
            return eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""));
        }
        (void)flow_set_var_to_deferred_call(ctx, call, out_var);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (eval_sv_eq_ci_lit(subcmd, "CANCEL_CALL")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER CANCEL_CALL) requires at least one id"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] CANCEL_CALL <id>...)"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        for (size_t k = i + 1; k < arena_arr_len(*raw); k++) {
            String_View id = flow_eval_arg_single(ctx, &(*raw)[k], true);
            if (eval_should_stop(ctx)) return false;
            if (!flow_remove_all_deferred_calls_with_id(frame, id)) return false;
        }
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    String_View explicit_id = nob_sv_from_cstr("");
    String_View id_var = nob_sv_from_cstr("");
    while (eval_sv_eq_ci_lit(subcmd, "ID") || eval_sv_eq_ci_lit(subcmd, "ID_VAR")) {
        if (i + 1 >= arena_arr_len(*raw)) {
            (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER) option requires a value"), subcmd);
            if (eval_should_stop(ctx)) return false;
            return true;
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
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        subcmd = flow_eval_arg_single(ctx, &(*raw)[i], true);
        if (eval_should_stop(ctx)) return false;
    }

    if (!eval_sv_eq_ci_lit(subcmd, "CALL")) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("Unsupported cmake_language(DEFER) subcommand"), subcmd);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (i + 1 >= arena_arr_len(*raw)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER CALL) requires a command name"), nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] [ID <id>] [ID_VAR <var>] CALL <command> [<arg>...])"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    String_View command_name = flow_eval_arg_single(ctx, &(*raw)[i + 1], true);
    if (eval_should_stop(ctx)) return false;
    if (!flow_is_valid_command_name(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER CALL) requires a valid command name"), command_name);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (flow_is_call_disallowed(command_name)) {
        (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("flow"), node->as.cmd.name, origin, nob_sv_from_cstr("cmake_language(DEFER CALL) does not allow structural commands"), command_name);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    bool generated_id = explicit_id.count == 0;
    String_View id = generated_id ? flow_make_deferred_id(ctx) : explicit_id;
    if (eval_should_stop(ctx)) return false;
    Flow_Deferred_Id_Validation id_validation = flow_validate_deferred_id(ctx, id, generated_id);
    if (id_validation != FLOW_DEFERRED_ID_VALID) {
        (void)EVAL_DIAG_EMIT_SEV(ctx,
                                 EV_DIAG_ERROR,
                                 EVAL_DIAG_MISSING_REQUIRED,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 flow_deferred_id_validation_cause(id_validation),
                                 id);
        if (eval_should_stop(ctx)) return false;
        return true;
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
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool flow_parse_cmake_language_request(EvalExecContext *ctx,
                                              const Node *node,
                                              Cmake_Event_Origin origin,
                                              Flow_Cmake_Language_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Flow_Cmake_Language_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    if (arena_arr_len(args) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "flow", nob_sv_from_cstr("cmake_language() requires a subcommand"), nob_sv_from_cstr("Supported here: CALL, EVAL CODE, DEFER, GET_MESSAGE_LOG_LEVEL, SET_DEPENDENCY_PROVIDER"));
        return false;
    }

    if (eval_sv_eq_ci_lit(args[0], "CALL")) {
        if (arena_arr_len(args) < 2) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "flow", nob_sv_from_cstr("cmake_language(CALL) requires a command name"), nob_sv_from_cstr("Usage: cmake_language(CALL <command> [<arg>...])"));
            return false;
        }
        req.kind = FLOW_CMAKE_LANGUAGE_REQUEST_CALL;
        req.as.call.args = args;
        req.as.call.command_name = args[1];
        *out_req = req;
        return true;
    }

    if (eval_sv_eq_ci_lit(args[0], "EVAL")) {
        if (arena_arr_len(args) < 2 || !eval_sv_eq_ci_lit(args[1], "CODE")) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "flow", nob_sv_from_cstr("cmake_language(EVAL) requires CODE"), nob_sv_from_cstr("Usage: cmake_language(EVAL CODE <code>...)"));
            return false;
        }
        req.kind = FLOW_CMAKE_LANGUAGE_REQUEST_EVAL_CODE;
        req.as.eval_code.args = args;
        req.as.eval_code.code_parts = arena_arr_len(args) > 2 ? args + 2 : NULL;
        req.as.eval_code.code_part_count = arena_arr_len(args) > 2 ? arena_arr_len(args) - 2 : 0;
        *out_req = req;
        return true;
    }

    if (eval_sv_eq_ci_lit(args[0], "GET_MESSAGE_LOG_LEVEL")) {
        if (arena_arr_len(args) != 2) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "flow", nob_sv_from_cstr("cmake_language(GET_MESSAGE_LOG_LEVEL) expects one output variable"), nob_sv_from_cstr("Usage: cmake_language(GET_MESSAGE_LOG_LEVEL <out-var>)"));
            return false;
        }
        req.kind = FLOW_CMAKE_LANGUAGE_REQUEST_GET_MESSAGE_LOG_LEVEL;
        req.as.get_message_log_level.out_var = args[1];
        *out_req = req;
        return true;
    }

    if (eval_sv_eq_ci_lit(args[0], "DEFER")) {
        req.kind = FLOW_CMAKE_LANGUAGE_REQUEST_DEFER;
        req.as.defer.queued_command_name = flow_cmake_language_find_defer_call_command_name(args);
        *out_req = req;
        return true;
    }

    if (eval_sv_eq_ci_lit(args[0], "SET_DEPENDENCY_PROVIDER")) {
        req.kind = FLOW_CMAKE_LANGUAGE_REQUEST_SET_DEPENDENCY_PROVIDER;
        req.as.set_dependency_provider.args = args;
        *out_req = req;
        return true;
    }

    (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "flow", nob_sv_from_cstr("Unsupported cmake_language() subcommand"), args[0]);
    return false;
}

static bool flow_execute_cmake_language_request(EvalExecContext *ctx,
                                                const Node *node,
                                                Cmake_Event_Origin origin,
                                                const Flow_Cmake_Language_Request *req) {
    if (!ctx || !node || !req) return false;

    switch (req->kind) {
    case FLOW_CMAKE_LANGUAGE_REQUEST_CALL:
        if (!eval_emit_cmake_language_call(ctx, origin, req->as.call.command_name)) return false;
        return flow_run_call(ctx, node, &req->as.call.args);
    case FLOW_CMAKE_LANGUAGE_REQUEST_EVAL_CODE: {
        String_View code = req->as.eval_code.code_part_count > 0
            ? eval_sv_join_semi_temp(ctx, req->as.eval_code.code_parts, req->as.eval_code.code_part_count)
            : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return false;
        if (!eval_emit_cmake_language_eval(ctx, origin, code)) return false;
        return flow_run_eval_code(ctx, node, &req->as.eval_code.args);
    }
    case FLOW_CMAKE_LANGUAGE_REQUEST_GET_MESSAGE_LOG_LEVEL: {
        String_View value = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_MESSAGE_LOG_LEVEL"));
        return eval_var_set_current(ctx, req->as.get_message_log_level.out_var, value);
    }
    case FLOW_CMAKE_LANGUAGE_REQUEST_DEFER: {
        bool ok = flow_handle_defer(ctx, node);
        if (ok && !eval_should_stop(ctx)) {
            if (!eval_emit_cmake_language_defer_queue(ctx,
                                                      origin,
                                                      nob_sv_from_cstr(""),
                                                      req->as.defer.queued_command_name)) {
                return false;
            }
        }
        return ok;
    }
    case FLOW_CMAKE_LANGUAGE_REQUEST_SET_DEPENDENCY_PROVIDER:
        return flow_cmake_language_set_dependency_provider(ctx, node, &req->as.set_dependency_provider.args);
    case FLOW_CMAKE_LANGUAGE_REQUEST_NONE:
    default:
        return false;
    }
}

Eval_Result eval_handle_cmake_language(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Flow_Cmake_Language_Request req = {0};
    if (!flow_parse_cmake_language_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    if (!flow_execute_cmake_language_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
