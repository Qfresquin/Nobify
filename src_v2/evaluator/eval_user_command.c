#include "evaluator_internal.h"

#include "eval_exec_core.h"
#include "eval_file_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool user_cmd_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

static bool clone_args_to_event(EvalExecContext *ctx, const Args *src, Args *dst) {
    if (!ctx || !src || !dst) return false;
    *dst = NULL;
    if (arena_arr_len(*src) == 0) return true;

    for (size_t i = 0; i < arena_arr_len(*src); i++) {
        Arg copy = {0};
        copy.kind = (*src)[i].kind;

        for (size_t k = 0; k < arena_arr_len((*src)[i].items); k++) {
            Token t = (*src)[i].items[k];
            t.text = sv_copy_to_event_arena(ctx, t.text);
            if (eval_should_stop(ctx)) return false;
            if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, copy.items, t)) return false;
        }

        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, *dst, copy)) return false;
    }

    return true;
}

static bool clone_node_to_event(EvalExecContext *ctx, const Node *src, Node *dst);
static bool clone_node_list_to_event(EvalExecContext *ctx, const Node_List *src, Node_List *dst);
static bool clone_elseif_list_to_event(EvalExecContext *ctx, const ElseIf_Clause_List *src, ElseIf_Clause_List *dst);

static bool clone_node_to_event(EvalExecContext *ctx, const Node *src, Node *dst) {
    if (!ctx || !src || !dst) return false;
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    dst->line = src->line;
    dst->col = src->col;

    switch (src->kind) {
        case NODE_COMMAND:
            dst->as.cmd.name = sv_copy_to_event_arena(ctx, src->as.cmd.name);
            if (eval_should_stop(ctx)) return false;
            return clone_args_to_event(ctx, &src->as.cmd.args, &dst->as.cmd.args);
        case NODE_IF:
            if (!clone_args_to_event(ctx, &src->as.if_stmt.condition, &dst->as.if_stmt.condition)) return false;
            if (!clone_node_list_to_event(ctx, &src->as.if_stmt.then_block, &dst->as.if_stmt.then_block)) return false;
            if (!clone_elseif_list_to_event(ctx, &src->as.if_stmt.elseif_clauses, &dst->as.if_stmt.elseif_clauses)) return false;
            if (!clone_node_list_to_event(ctx, &src->as.if_stmt.else_block, &dst->as.if_stmt.else_block)) return false;
            return true;
        case NODE_FOREACH:
            if (!clone_args_to_event(ctx, &src->as.foreach_stmt.args, &dst->as.foreach_stmt.args)) return false;
            if (!clone_node_list_to_event(ctx, &src->as.foreach_stmt.body, &dst->as.foreach_stmt.body)) return false;
            return true;
        case NODE_WHILE:
            if (!clone_args_to_event(ctx, &src->as.while_stmt.condition, &dst->as.while_stmt.condition)) return false;
            if (!clone_node_list_to_event(ctx, &src->as.while_stmt.body, &dst->as.while_stmt.body)) return false;
            return true;
        case NODE_FUNCTION:
        case NODE_MACRO:
            dst->as.func_def.name = sv_copy_to_event_arena(ctx, src->as.func_def.name);
            if (eval_should_stop(ctx)) return false;
            if (!clone_args_to_event(ctx, &src->as.func_def.params, &dst->as.func_def.params)) return false;
            if (!clone_node_list_to_event(ctx, &src->as.func_def.body, &dst->as.func_def.body)) return false;
            return true;
        default:
            return true;
    }
}

static bool clone_elseif_list_to_event(EvalExecContext *ctx, const ElseIf_Clause_List *src, ElseIf_Clause_List *dst) {
    if (!ctx || !src || !dst) return false;
    *dst = NULL;
    for (size_t i = 0; i < arena_arr_len(*src); i++) {
        ElseIf_Clause copy = {0};
        if (!clone_args_to_event(ctx, &(*src)[i].condition, &copy.condition)) return false;
        if (!clone_node_list_to_event(ctx, &(*src)[i].block, &copy.block)) return false;
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, *dst, copy)) return false;
    }
    return true;
}

static bool clone_node_list_to_event(EvalExecContext *ctx, const Node_List *src, Node_List *dst) {
    if (!ctx || !src || !dst) return false;
    *dst = NULL;
    for (size_t i = 0; i < arena_arr_len(*src); i++) {
        Node copy = {0};
        if (!clone_node_to_event(ctx, &(*src)[i], &copy)) return false;
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, *dst, copy)) return false;
    }
    return true;
}

bool eval_user_cmd_register(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node) return false;
    if (node->kind != NODE_FUNCTION && node->kind != NODE_MACRO) return false;

    String_View name = sv_copy_to_event_arena(ctx, node->as.func_def.name);
    if (eval_should_stop(ctx)) return false;

    String_View *params = NULL;
    for (size_t i = 0; i < arena_arr_len(node->as.func_def.params); i++) {
        const Arg *param = &node->as.func_def.params[i];
        String_View value = nob_sv_from_cstr("");
        if (arena_arr_len(param->items) > 0) {
            value = sv_copy_to_event_arena(ctx, param->items[0].text);
            if (eval_should_stop(ctx)) return false;
        }
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, params, value)) return false;
    }

    Node_List body = NULL;
    if (!clone_node_list_to_event(ctx, &node->as.func_def.body, &body)) return false;

    // Store function/macro bodies in persistent arena storage so later
    // invocations survive statement-local temp-arena rewinds.
    User_Command cmd = {0};
    cmd.name = name;
    cmd.kind = (node->kind == NODE_MACRO) ? USER_CMD_MACRO : USER_CMD_FUNCTION;
    cmd.params = params;
    cmd.body = body;

    Eval_Command_State *commands = eval_command_slice(ctx);
    return EVAL_ARR_PUSH(ctx, commands->user_commands_arena, commands->user_commands, cmd);
}

User_Command *eval_user_cmd_find(EvalExecContext *ctx, String_View name) {
    if (!ctx) return NULL;
    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = arena_arr_len(commands->user_commands); i-- > 0;) {
        if (user_cmd_sv_eq_ci(commands->user_commands[i].name, name)) {
            return &commands->user_commands[i];
        }
    }
    return NULL;
}

bool eval_user_cmd_invoke(EvalExecContext *ctx, String_View name, const SV_List *args, Cmake_Event_Origin origin) {
    if (eval_should_stop(ctx)) return false;

    User_Command *cmd = eval_user_cmd_find(ctx, name);
    if (!cmd) return false;

    bool is_function = (cmd->kind == USER_CMD_FUNCTION);
    bool is_macro = (cmd->kind == USER_CMD_MACRO);
    size_t arg_count = args ? arena_arr_len(*args) : 0;
    size_t entered_function_depth = 0;
    bool scope_pushed = false;
    bool macro_pushed = false;
    bool exec_pushed = false;
    Eval_Exec_Context exec = {0};
    exec.kind = is_function ? EVAL_EXEC_CTX_FUNCTION : EVAL_EXEC_CTX_MACRO;
    exec.return_context = is_function ? EVAL_RETURN_CTX_FUNCTION : EVAL_RETURN_CTX_MACRO;
    exec.source_dir = eval_current_source_dir(ctx);
    exec.binary_dir = eval_current_binary_dir(ctx);
    exec.list_dir = eval_current_list_dir(ctx);
    exec.current_file = ctx->current_file;
    if (is_function) {
        if (!eval_scope_push(ctx)) return false;
        scope_pushed = true;
        if (!eval_exec_push(ctx, exec)) {
            eval_scope_pop(ctx);
            return false;
        }
        exec_pushed = true;
        entered_function_depth = ctx->function_eval_depth;
        if (!eval_emit_flow_function_begin(ctx, origin, name, (uint32_t)arg_count)) {
            eval_exec_pop(ctx);
            eval_scope_pop(ctx);
            return false;
        }
    } else if (is_macro) {
        if (!eval_macro_frame_push(ctx)) return false;
        macro_pushed = true;
        if (!eval_exec_push(ctx, exec)) {
            eval_macro_frame_pop(ctx);
            return false;
        }
        exec_pushed = true;
        if (!eval_emit_flow_macro_begin(ctx, origin, name, (uint32_t)arg_count)) {
            eval_exec_pop(ctx);
            eval_macro_frame_pop(ctx);
            return false;
        }
    }

    bool ok = false;
    bool did_return = false;
    for (size_t i = 0; i < arena_arr_len(cmd->params); i++) {
        String_View val = nob_sv_from_cstr("");
        if (i < arg_count) val = (*args)[i];
        if (is_function) {
            if (!eval_var_set_current(ctx, cmd->params[i], val)) goto cleanup;
        } else {
            if (!eval_macro_bind_set(ctx, cmd->params[i], val)) goto cleanup;
        }
    }

    char buf_argc[64];
    int argc_n = snprintf(buf_argc, sizeof(buf_argc), "%zu", arg_count);
    if (argc_n < 0 || (size_t)argc_n >= sizeof(buf_argc)) {
        return ctx_oom(ctx);
    }
    if (is_function) {
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("ARGC"), nob_sv_from_cstr(buf_argc))) goto cleanup;
    } else {
        if (!eval_macro_bind_set(ctx, nob_sv_from_cstr("ARGC"), nob_sv_from_cstr(buf_argc))) goto cleanup;
    }

    String_View argv_val = eval_sv_join_semi_temp(ctx, args ? *args : NULL, arg_count);
    if (is_function) {
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("ARGV"), argv_val)) goto cleanup;
    } else {
        if (!eval_macro_bind_set(ctx, nob_sv_from_cstr("ARGV"), argv_val)) goto cleanup;
    }

    if (arg_count > arena_arr_len(cmd->params)) {
        String_View argn_val = eval_sv_join_semi_temp(ctx,
                                                      &(*args)[arena_arr_len(cmd->params)],
                                                      arg_count - arena_arr_len(cmd->params));
        if (is_function) {
            if (!eval_var_set_current(ctx, nob_sv_from_cstr("ARGN"), argn_val)) goto cleanup;
        } else {
            if (!eval_macro_bind_set(ctx, nob_sv_from_cstr("ARGN"), argn_val)) goto cleanup;
        }
    } else {
        if (is_function) {
            if (!eval_var_set_current(ctx, nob_sv_from_cstr("ARGN"), nob_sv_from_cstr(""))) goto cleanup;
        } else {
            if (!eval_macro_bind_set(ctx, nob_sv_from_cstr("ARGN"), nob_sv_from_cstr(""))) goto cleanup;
        }
    }

    for (size_t i = 0; i < arg_count; i++) {
        char key[64];
        int key_n = snprintf(key, sizeof(key), "ARGV%zu", i);
        if (key_n < 0 || (size_t)key_n >= sizeof(key)) goto cleanup;
        if (is_function) {
            if (!eval_var_set_current(ctx, nob_sv_from_cstr(key), (*args)[i])) goto cleanup;
        } else {
            if (!eval_macro_bind_set(ctx, nob_sv_from_cstr(key), (*args)[i])) goto cleanup;
        }
    }

    ok = !eval_result_is_fatal(eval_execute_node_list(ctx, &cmd->body));
cleanup:
    did_return = exec_pushed &&
                 eval_exec_current(ctx) &&
                 eval_exec_current(ctx)->pending_flow == EVAL_FLOW_RETURN;
    if (did_return && arena_arr_len(ctx->scope_state.return_propagate_vars) > 0 &&
        is_function && eval_scope_visible_depth(ctx) > 1) {
        for (size_t i = 0; i < arena_arr_len(ctx->scope_state.return_propagate_vars); i++) {
            String_View key = ctx->scope_state.return_propagate_vars[i];
            if (!eval_var_defined_current(ctx, key)) continue;
            String_View value = eval_var_get_visible(ctx, key);
            size_t saved_depth = 0;
            if (!eval_scope_use_parent_view(ctx, &saved_depth)) {
                ok = false;
                continue;
            }
            bool ok_set = eval_var_set_current(ctx, key, value);
            eval_scope_restore_view(ctx, saved_depth);
            if (!ok_set) ok = false;
        }
    }
    eval_clear_return_state(ctx);
    if (entered_function_depth > 0) {
        eval_file_lock_release_function_scope(ctx, entered_function_depth);
    }
    if (exec_pushed) eval_exec_pop(ctx);
    if (scope_pushed) {
        eval_scope_pop(ctx);
    } else if (macro_pushed) {
        eval_macro_frame_pop(ctx);
    }
    if (is_function) {
        if (!eval_emit_flow_function_end(ctx, origin, name, did_return)) return false;
    } else if (is_macro) {
        if (!eval_emit_flow_macro_end(ctx, origin, name, did_return)) return false;
    }

    return ok;
}
