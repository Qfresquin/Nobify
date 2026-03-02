#include "eval_flow.h"

#include "evaluator_internal.h"
#include "eval_expr.h"
#include "arena_dyn.h"

#include <stdio.h>
#include <string.h>

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
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) {
        return false;
    }
    list->items[list->count++] = token;
    return true;
}

static bool flow_parse_inline_script(Evaluator_Context *ctx, String_View script, Ast_Root *out_ast) {
    if (!ctx || !out_ast) return false;
    *out_ast = (Ast_Root){0};

    Lexer lx = lexer_init(script);
    Token_List toks = {0};
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
    if (!ctx || !arg || arg->count == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < arg->count; i++) total += arg->items[i].text.count;

    char *buf = (char*)arena_alloc(ctx->arena, total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arg->count; i++) {
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
    memset(dst, 0, sizeof(*dst));
    if (begin >= src->count) return true;

    size_t count = src->count - begin;
    dst->items = arena_alloc_array(ctx->event_arena, Arg, count);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst->items, false);
    dst->count = count;
    dst->capacity = count;

    for (size_t i = 0; i < count; i++) {
        const Arg *in = &src->items[begin + i];
        Arg *out = &dst->items[i];
        out->kind = in->kind;
        out->count = in->count;
        out->capacity = in->count;
        if (in->count == 0) continue;

        out->items = arena_alloc_array(ctx->event_arena, Token, in->count);
        EVAL_OOM_RETURN_IF_NULL(ctx, out->items, false);
        for (size_t k = 0; k < in->count; k++) {
            out->items[k] = in->items[k];
            out->items[k].text = sv_copy_to_event_arena(ctx, in->items[k].text);
            if (eval_should_stop(ctx)) return false;
        }
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

        Ast_Root ast = {0};
        ast.items = &deferred;
        ast.count = 1;
        ast.capacity = 1;
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

    Ast_Root ast = {0};
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

    Ast_Root ast = {0};
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
    for (size_t i = 0; i < call->args.count; i++) {
        nob_sb_append(&sb, ';');
        String_View item = flow_eval_arg_single(ctx, &call->args.items[i], false);
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
    if (raw->count < 2) {
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
    String_View tok = flow_eval_arg_single(ctx, &raw->items[i], true);
    if (eval_should_stop(ctx)) return false;
    if (eval_sv_eq_ci_lit(tok, "DIRECTORY")) {
        if (i + 1 >= raw->count) {
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
        directory = flow_eval_arg_single(ctx, &raw->items[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        i += 2;
        if (i >= raw->count) {
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

    String_View subcmd = flow_eval_arg_single(ctx, &raw->items[i], true);
    if (eval_should_stop(ctx)) return false;
    Eval_Deferred_Dir_Frame *frame = flow_resolve_defer_directory(ctx, node, has_directory, directory);
    if (!frame) return !eval_should_stop(ctx);

    if (eval_sv_eq_ci_lit(subcmd, "GET_CALL_IDS")) {
        if (i + 2 != raw->count) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER GET_CALL_IDS) expects one output variable"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] GET_CALL_IDS <out-var>)"));
            return !eval_should_stop(ctx);
        }
        String_View out_var = flow_eval_arg_single(ctx, &raw->items[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        (void)flow_set_var_to_deferred_ids(ctx, frame, out_var);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(subcmd, "GET_CALL")) {
        if (i + 3 != raw->count) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER GET_CALL) expects an id and one output variable"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] GET_CALL <id> <out-var>)"));
            return !eval_should_stop(ctx);
        }
        String_View id = flow_eval_arg_single(ctx, &raw->items[i + 1], true);
        String_View out_var = flow_eval_arg_single(ctx, &raw->items[i + 2], true);
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
        if (i + 1 >= raw->count) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER CANCEL_CALL) requires at least one id"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] CANCEL_CALL <id>...)"));
            return !eval_should_stop(ctx);
        }
        for (size_t k = i + 1; k < raw->count; k++) {
            String_View id = flow_eval_arg_single(ctx, &raw->items[k], true);
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
        if (i + 1 >= raw->count) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER) option requires a value"),
                                 subcmd);
            return !eval_should_stop(ctx);
        }
        String_View value = flow_eval_arg_single(ctx, &raw->items[i + 1], true);
        if (eval_should_stop(ctx)) return false;
        if (eval_sv_eq_ci_lit(subcmd, "ID")) {
            explicit_id = value;
        } else {
            id_var = value;
        }
        i += 2;
        if (i >= raw->count) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 node->as.cmd.name,
                                 origin,
                                 nob_sv_from_cstr("cmake_language(DEFER) missing CALL subcommand"),
                                 nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] [ID <id>] [ID_VAR <var>] CALL <command> [<arg>...])"));
            return !eval_should_stop(ctx);
        }
        subcmd = flow_eval_arg_single(ctx, &raw->items[i], true);
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
    if (i + 1 >= raw->count) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("flow"),
                             node->as.cmd.name,
                             origin,
                             nob_sv_from_cstr("cmake_language(DEFER CALL) requires a command name"),
                             nob_sv_from_cstr("Usage: cmake_language(DEFER [DIRECTORY <dir>] [ID <id>] [ID_VAR <var>] CALL <command> [<arg>...])"));
        return !eval_should_stop(ctx);
    }

    String_View command_name = flow_eval_arg_single(ctx, &raw->items[i + 1], true);
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
