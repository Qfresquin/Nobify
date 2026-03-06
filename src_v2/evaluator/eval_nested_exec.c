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
    const char *old_file;
    String_View old_list_file;
    String_View old_list_dir;
    String_View old_src_dir;
    String_View old_bin_dir;
    bool is_add_subdirectory;
    bool scope_pushed;
    bool restore_needed;
} External_Eval_State;

static bool eval_read_external_source(Evaluator_Context *ctx,
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

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) {
        return false;
    }

    String_View source_code = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items, sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;

    *out_path_c = path_c;
    *out_source_code = source_code;
    return true;
}

static bool eval_lex_external_tokens(Evaluator_Context *ctx,
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

    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool eval_parse_external_ast(Evaluator_Context *ctx,
                                    const Token_List *tokens,
                                    Ast_Root *out_ast) {
    if (!ctx || !tokens || !out_ast) return false;
    *out_ast = parse_tokens(ctx->arena, *tokens);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool eval_push_external_context(Evaluator_Context *ctx,
                                       String_View file_path,
                                       const char *path_c,
                                       bool is_add_subdirectory,
                                       String_View explicit_bin_dir,
                                       External_Eval_State *state) {
    if (!ctx || !path_c || !state) return false;
    memset(state, 0, sizeof(*state));

    state->old_file = ctx->current_file;
    state->old_list_file = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE));
    state->old_list_dir = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR));
    state->old_src_dir = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR));
    state->old_bin_dir = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR));
    state->is_add_subdirectory = is_add_subdirectory;
    state->restore_needed = true;

    String_View new_list_dir = nested_exec_dir_from_path(file_path);
    const char *new_current_file = arena_strndup(ctx->event_arena, path_c, strlen(path_c));
    EVAL_OOM_RETURN_IF_NULL(ctx, new_current_file, false);
    ctx->current_file = new_current_file;

    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE), file_path)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR), new_list_dir)) return false;

    if (!is_add_subdirectory) return true;

    if (!eval_scope_push(ctx)) return false;
    state->scope_pushed = true;

    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR), new_list_dir)) return false;
    if (explicit_bin_dir.count > 0) {
        if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR), explicit_bin_dir)) return false;
    } else {
        String_View bin_path = sv_copy_to_event_arena(ctx, new_list_dir);
        if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR), bin_path)) return false;
    }
    return true;
}

static void eval_pop_external_context(Evaluator_Context *ctx, const External_Eval_State *state) {
    if (!ctx || !state || !state->restore_needed) return;

    if (state->scope_pushed) {
        eval_scope_pop(ctx);
    }

    ctx->current_file = state->old_file;
    (void)eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE), state->old_list_file);
    (void)eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR), state->old_list_dir);
    if (state->is_add_subdirectory) {
        (void)eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR), state->old_src_dir);
        (void)eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR), state->old_bin_dir);
    }
}

Eval_Result eval_execute_file(Evaluator_Context *ctx,
                              String_View file_path,
                              bool is_add_subdirectory,
                              String_View explicit_bin_dir) {
    if (eval_should_stop(ctx)) return eval_result_fatal();
    size_t entered_file_depth = ++ctx->file_eval_depth;
    Eval_Return_Context saved_return_ctx = ctx->return_context;
    ctx->return_context = EVAL_RETURN_CTX_INCLUDE;
    Arena_Mark temp_mark = arena_mark(ctx->arena);
    Eval_Result result = eval_result_fatal();
    char *path_c = NULL;
    String_View source_code = {0};
    Token_List tokens = NULL;
    Ast_Root new_ast = NULL;
    External_Eval_State state = {0};

    if (!eval_read_external_source(ctx, file_path, &path_c, &source_code)) goto cleanup;
    if (!eval_lex_external_tokens(ctx, path_c, source_code, &tokens)) goto cleanup;
    if (!eval_parse_external_ast(ctx, &tokens, &new_ast)) goto cleanup;
    if (!eval_push_external_context(ctx, file_path, path_c, is_add_subdirectory, explicit_bin_dir, &state)) {
        eval_pop_external_context(ctx, &state);
        goto cleanup;
    }
    if (is_add_subdirectory) {
        String_View current_src_dir = eval_current_source_dir(ctx);
        String_View current_bin_dir = eval_current_binary_dir(ctx);
        if (!eval_defer_push_directory(ctx, current_src_dir, current_bin_dir)) {
            eval_pop_external_context(ctx, &state);
            goto cleanup;
        }
    }

    result = eval_execute_node_list(ctx, &new_ast);
    if (!eval_result_is_fatal(result) && !eval_should_stop(ctx)) {
        if (!eval_defer_flush_current_directory(ctx)) result = eval_result_fatal();
    }
    eval_clear_return_state(ctx);
    if (is_add_subdirectory) {
        if (!eval_defer_pop_directory(ctx)) result = eval_result_fatal();
    }
    eval_pop_external_context(ctx, &state);

cleanup:
    eval_file_lock_release_file_scope(ctx, entered_file_depth);
    if (ctx->file_eval_depth > 0) ctx->file_eval_depth--;
    ctx->return_context = saved_return_ctx;
    arena_rewind(ctx->arena, temp_mark);
    return eval_result_merge(result, eval_result_ok_if_running(ctx));
}
