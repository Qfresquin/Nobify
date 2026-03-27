#include "evaluator_internal.h"

#include "eval_exec_core.h"
#include "eval_file_internal.h"
#include "lexer.h"

#include <string.h>

static String_View nested_exec_dir_from_path(String_View path) {
    for (size_t i = path.count; i-- > 0;) {
        if (path.data[i] == '/' || path.data[i] == '\\') {
            return nob_sv_from_parts(path.data, i);
        }
    }
    return nob_sv_from_cstr(".");
}

static bool nested_exec_token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, token);
}

typedef struct {
    bool is_add_subdirectory;
    bool scope_pushed;
    bool defer_pushed;
} External_Eval_State;

static bool nested_exec_publish_current_vars(EvalExecContext *ctx) {
    return eval_exec_publish_current_vars(ctx);
}

static bool eval_read_external_source(EvalExecContext *ctx,
                                      String_View file_path,
                                      char **out_path_c,
                                      String_View *out_source_code) {
    if (!ctx || !out_path_c || !out_source_code) return false;
    *out_path_c = NULL;
    *out_source_code = nob_sv_from_cstr("");

    char *path_c = (char*)arena_alloc(ctx->arena, file_path.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    memcpy(path_c, file_path.data, file_path.count);
    path_c[file_path.count] = '\0';

    String_View source_code = nob_sv_from_cstr("");
    bool found = false;
    if (!eval_service_read_file(ctx, file_path, &source_code, &found) || !found) {
        return false;
    }

    *out_path_c = path_c;
    *out_source_code = source_code;
    return true;
}

static bool eval_lex_external_tokens(EvalExecContext *ctx,
                                     const char *path_c,
                                     String_View source_code,
                                     Token_List *out_tokens) {
    if (!ctx || !path_c || !out_tokens) return false;
    memset(out_tokens, 0, sizeof(*out_tokens));

    Lexer lexer = lexer_init(source_code);
    for (;;) {
        Token token = lexer_next(&lexer);
        if (token.kind == TOKEN_END) break;
        if (token.kind == TOKEN_INVALID) {
            Cmake_Event_Origin o = {0};
            o.file_path = nob_sv_from_cstr(path_c);
            o.line = token.line;
            o.col = token.col;
            EVAL_DIAG_EMIT_SEV(ctx,
                               EV_DIAG_ERROR,
                               EVAL_DIAG_PARSE_ERROR,
                               nob_sv_from_cstr("lexer"),
                               nob_sv_from_cstr("parse"),
                               o,
                               nob_sv_from_cstr("Invalid token while evaluating external file"),
                               nob_sv_from_cstr("Check escaping, quoting and variable syntax"));
            return false;
        }
        if (!nested_exec_token_list_append(ctx->arena, out_tokens, token)) {
            ctx_oom(ctx);
            return false;
        }
    }

    if (eval_should_stop(ctx)) return false;

    return true;
}

static bool eval_parse_external_ast(EvalExecContext *ctx,
                                    const Token_List *tokens,
                                    Ast_Root *out_ast) {
    if (!ctx || !tokens || !out_ast) return false;
    *out_ast = parse_tokens(ctx->arena, *tokens);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool eval_push_external_context(EvalExecContext *ctx,
                                       String_View file_path,
                                       const char *path_c,
                                       bool is_add_subdirectory,
                                       String_View explicit_bin_dir,
                                       External_Eval_State *state) {
    if (!ctx || !path_c || !state) return false;
    memset(state, 0, sizeof(*state));

    state->is_add_subdirectory = is_add_subdirectory;

    String_View new_list_dir = nested_exec_dir_from_path(file_path);
    String_View current_source_dir = eval_current_source_dir(ctx);
    String_View current_binary_dir = eval_current_binary_dir(ctx);

    if (is_add_subdirectory) {
        if (!eval_directory_capture_current_scope(ctx)) return false;
        if (!eval_scope_push(ctx)) return false;
        state->scope_pushed = true;
    }

    Eval_Exec_Context exec = {0};
    exec.kind = is_add_subdirectory ? EVAL_EXEC_CTX_SUBDIRECTORY : EVAL_EXEC_CTX_INCLUDE;
    exec.return_context = EVAL_RETURN_CTX_INCLUDE;
    exec.source_dir = sv_copy_to_event_arena(ctx, is_add_subdirectory ? new_list_dir : current_source_dir);
    if ((is_add_subdirectory ? new_list_dir.count : current_source_dir.count) > 0 && exec.source_dir.count == 0) return false;
    exec.binary_dir = sv_copy_to_event_arena(ctx,
                                             is_add_subdirectory
                                                 ? (explicit_bin_dir.count > 0 ? explicit_bin_dir : new_list_dir)
                                                 : current_binary_dir);
    if ((is_add_subdirectory
             ? (explicit_bin_dir.count > 0 ? explicit_bin_dir.count : new_list_dir.count)
             : current_binary_dir.count) > 0 &&
        exec.binary_dir.count == 0) {
        return false;
    }
    exec.list_dir = sv_copy_to_event_arena(ctx, new_list_dir);
    if (new_list_dir.count > 0 && exec.list_dir.count == 0) return false;
    exec.current_file = arena_strndup(ctx->event_arena, path_c, strlen(path_c));
    EVAL_OOM_RETURN_IF_NULL(ctx, exec.current_file, false);

    if (!eval_exec_push(ctx, exec)) return false;
    if (!nested_exec_publish_current_vars(ctx)) {
        eval_exec_pop(ctx);
        return false;
    }
    return true;
}

static bool eval_pop_external_context(EvalExecContext *ctx, External_Eval_State *state) {
    if (!ctx || !state) return false;
    if (state->scope_pushed) {
        eval_scope_pop(ctx);
        state->scope_pushed = false;
    }
    eval_exec_pop(ctx);
    return nested_exec_publish_current_vars(ctx);
}

Eval_Result eval_execute_file(EvalExecContext *ctx,
                              String_View file_path,
                              bool is_add_subdirectory,
                              String_View explicit_bin_dir) {
    if (eval_should_stop(ctx)) return eval_result_fatal();
    Arena_Mark temp_mark = arena_mark(ctx->arena);
    Eval_Result result = eval_result_fatal();
    size_t entered_file_depth = 0;
    char *path_c = NULL;
    String_View source_code = {0};
    Token_List tokens = NULL;
    Ast_Root new_ast = NULL;
    External_Eval_State state = {0};

    if (!eval_read_external_source(ctx, file_path, &path_c, &source_code)) goto cleanup;
    if (!eval_lex_external_tokens(ctx, path_c, source_code, &tokens)) goto cleanup;
    if (!eval_parse_external_ast(ctx, &tokens, &new_ast)) goto cleanup;
    if (!eval_push_external_context(ctx, file_path, path_c, is_add_subdirectory, explicit_bin_dir, &state)) goto cleanup;
    entered_file_depth = ctx->file_eval_depth;
    if (is_add_subdirectory) {
        String_View current_src_dir = eval_current_source_dir(ctx);
        String_View current_bin_dir = eval_current_binary_dir(ctx);
        if (!eval_directory_register_known(ctx, current_src_dir)) goto cleanup;
        if (!eval_defer_push_directory(ctx, current_src_dir, current_bin_dir)) goto cleanup;
        state.defer_pushed = true;
    }

    result = eval_execute_node_list(ctx, &new_ast);
    if (!eval_result_is_fatal(result) && !eval_should_stop(ctx)) {
        if (!eval_defer_flush_current_directory(ctx)) result = eval_result_fatal();
    }
    eval_clear_return_state(ctx);
    if (is_add_subdirectory) {
        if (!eval_directory_capture_current_scope(ctx)) result = eval_result_fatal();
        if (!eval_defer_pop_directory(ctx)) result = eval_result_fatal();
        state.defer_pushed = false;
    }
    if (!eval_pop_external_context(ctx, &state)) result = eval_result_fatal();

cleanup:
    if (entered_file_depth > 0) {
        eval_file_lock_release_file_scope(ctx, entered_file_depth);
    }
    if (eval_exec_current(ctx) &&
        (eval_exec_current(ctx)->kind == EVAL_EXEC_CTX_INCLUDE ||
         eval_exec_current(ctx)->kind == EVAL_EXEC_CTX_SUBDIRECTORY)) {
        if (state.defer_pushed) {
            (void)eval_defer_pop_directory(ctx);
        }
        (void)eval_pop_external_context(ctx, &state);
    } else if (state.scope_pushed) {
        eval_scope_pop(ctx);
    }
    arena_rewind(ctx->arena, temp_mark);
    return eval_result_merge(result, eval_result_ok_if_running(ctx));
}
