#include "evaluator.h"
#include "evaluator_internal.h"
#include "eval_expr.h"
#include "eval_dispatcher.h"
#include "lexer.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "stb_ds.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void destroy_sub_arena_cb(void *userdata) {
    Arena *arena = (Arena*)userdata;
    arena_destroy(arena);
}

// -----------------------------------------------------------------------------
// Arena helpers and origin tracking
// -----------------------------------------------------------------------------

Arena *eval_temp_arena(Evaluator_Context *ctx) { return ctx ? ctx->arena : NULL; }
Arena *eval_event_arena(Evaluator_Context *ctx) { return ctx ? ctx->event_arena : NULL; }

String_View sv_copy_to_temp_arena(Evaluator_Context *ctx, String_View sv) {
    String_View out = sv_copy_to_arena(ctx ? ctx->arena : NULL, sv);
    if (ctx && sv.count > 0 && out.count == 0) ctx_oom(ctx);
    return out;
}

String_View sv_copy_to_event_arena(Evaluator_Context *ctx, String_View sv) {
    String_View out = sv_copy_to_arena(ctx ? ctx->event_arena : NULL, sv);
    if (ctx && sv.count > 0 && out.count == 0) ctx_oom(ctx);
    return out;
}

Cmake_Event_Origin eval_origin_from_node(const Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = {0};
    o.file_path = (ctx && ctx->current_file) ? nob_sv_from_cstr(ctx->current_file) : nob_sv_from_cstr("<input>");
    if (node) {
        o.line = node->line;
        o.col = node->col;
    }
    return o;
}

bool ctx_oom(Evaluator_Context *ctx) {
    if (!ctx) return true;
    ctx->oom = true;
    ctx->stop_requested = true;
    return false;
}

bool eval_continue_on_error(Evaluator_Context *ctx) {
    if (!ctx) return false;
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_CONTINUE_ON_ERROR"));
    if (v.count == 0) return false;
    return eval_truthy(ctx, v);
}

void eval_request_stop(Evaluator_Context *ctx) {
    if (ctx) ctx->stop_requested = true;
}

void eval_request_stop_on_error(Evaluator_Context *ctx) {
    if (!ctx) return;
    if (eval_continue_on_error(ctx)) return;
    ctx->stop_requested = true;
}

bool eval_should_stop(Evaluator_Context *ctx) {
    if (!ctx) return true;
    return ctx->oom || ctx->stop_requested;
}

// -----------------------------------------------------------------------------
// Diagnostics (error as data)
// -----------------------------------------------------------------------------

bool eval_emit_diag(Evaluator_Context *ctx,
                    Cmake_Diag_Severity sev,
                    String_View component,
                    String_View command,
                    Cmake_Event_Origin origin,
                    String_View cause,
                    String_View hint) {
    if (!ctx || eval_should_stop(ctx)) return false;

    diag_log(sev == EV_DIAG_ERROR ? DIAG_SEV_ERROR : DIAG_SEV_WARNING,
             "evaluator",
             ctx->current_file ? ctx->current_file : "<input>",
             origin.line,
             origin.col,
             nob_temp_sprintf("%.*s", (int)command.count, command.data ? command.data : ""),
             cause.data ? cause.data : "",
             hint.data ? hint.data : "");

    Cmake_Event ev = {0};
    ev.kind = EV_DIAGNOSTIC;
    ev.origin = origin;
    ev.as.diag.severity = sev;
    ev.as.diag.component = sv_copy_to_event_arena(ctx, component);
    ev.as.diag.command = sv_copy_to_event_arena(ctx, command);
    ev.as.diag.cause = sv_copy_to_event_arena(ctx, cause);
    ev.as.diag.hint = sv_copy_to_event_arena(ctx, hint);
    if (!event_stream_push(ctx->event_arena, ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    if (sev == EV_DIAG_ERROR) {
        eval_request_stop_on_error(ctx);
    }
    return true;
}

// -----------------------------------------------------------------------------
// Variable scopes
// -----------------------------------------------------------------------------
static Eval_Var_Entry *eval_scope_var_find(Eval_Var_Entry *vars, String_View key) {
    if (!vars || !key.data) return NULL;
    return stbds_shgetp_null(vars, nob_temp_sv_to_cstr(key));
}

static char *eval_copy_key_cstr_event(Evaluator_Context *ctx, String_View key) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(ctx->event_arena, key.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (key.count > 0 && key.data) memcpy(buf, key.data, key.count);
    buf[key.count] = '\0';
    return buf;
}

String_View eval_var_get(Evaluator_Context *ctx, String_View key) {
    if (!ctx || ctx->scope_depth == 0) return nob_sv_from_cstr("");
    for (size_t d = ctx->scope_depth; d-- > 0;) {
        Var_Scope *s = &ctx->scopes[d];
        Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
        if (b) return b->value;
    }
    return nob_sv_from_cstr("");
}

bool eval_var_set(Evaluator_Context *ctx, String_View key, String_View value) {
    if (!ctx || ctx->scope_depth == 0 || eval_should_stop(ctx)) return false;
    Var_Scope *s = &ctx->scopes[ctx->scope_depth - 1];
    Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
    if (b) {
        b->value = sv_copy_to_event_arena(ctx, value);
        return !eval_should_stop(ctx);
    }

    char *stable_key = eval_copy_key_cstr_event(ctx, key);
    if (!stable_key) return false;
    String_View stable_value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;

    Eval_Var_Entry *vars = s->vars;
    stbds_shput(vars, stable_key, stable_value);
    s->vars = vars;
    return true;
}

bool eval_var_unset(Evaluator_Context *ctx, String_View key) {
    if (!ctx || ctx->scope_depth == 0 || eval_should_stop(ctx)) return false;
    Var_Scope *s = &ctx->scopes[ctx->scope_depth - 1];
    if (s->vars) {
        (void)stbds_shdel(s->vars, nob_temp_sv_to_cstr(key));
    }
    return true;
}

bool eval_var_defined(Evaluator_Context *ctx, String_View key) {
    // True only if binding exists in any visible scope.
    if (!ctx || ctx->scope_depth == 0) return false;
    for (size_t d = ctx->scope_depth; d-- > 0;) {
        Var_Scope *s = &ctx->scopes[d];
        Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
        if (b) return true;
    }
    return false;
}

bool eval_var_defined_in_current_scope(Evaluator_Context *ctx, String_View key) {
    // Used by commands that need local-scope semantics only.
    if (!ctx || ctx->scope_depth == 0) return false;
    Var_Scope *s = &ctx->scopes[ctx->scope_depth - 1];
    Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
    return b != NULL;
}

bool eval_macro_frame_push(Evaluator_Context *ctx) {
    if (!ctx) return false;
    Macro_Frame frame = {0};
    if (!arena_da_try_append(ctx->event_arena, &ctx->macro_frames, frame)) return ctx_oom(ctx);
    return true;
}

void eval_macro_frame_pop(Evaluator_Context *ctx) {
    if (!ctx || ctx->macro_frames.count == 0) return;
    ctx->macro_frames.count--;
}

bool eval_macro_bind_set(Evaluator_Context *ctx, String_View key, String_View value) {
    if (!ctx || ctx->macro_frames.count == 0) return false;

    Macro_Frame *top = &ctx->macro_frames.items[ctx->macro_frames.count - 1];
    key = sv_copy_to_event_arena(ctx, key);
    value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;

    for (size_t i = top->bindings.count; i-- > 0;) {
        if (eval_sv_key_eq(top->bindings.items[i].key, key)) {
            top->bindings.items[i].value = value;
            return true;
        }
    }

    Var_Binding b = {0};
    b.key = key;
    b.value = value;
    if (!arena_da_try_append(ctx->event_arena, &top->bindings, b)) return ctx_oom(ctx);
    return true;
}

bool eval_macro_bind_get(Evaluator_Context *ctx, String_View key, String_View *out_value) {
    if (!ctx || !out_value) return false;
    for (size_t fi = ctx->macro_frames.count; fi-- > 0;) {
        const Macro_Frame *f = &ctx->macro_frames.items[fi];
        for (size_t i = f->bindings.count; i-- > 0;) {
            if (eval_sv_key_eq(f->bindings.items[i].key, key)) {
                *out_value = f->bindings.items[i].value;
                return true;
            }
        }
    }
    return false;
}

bool eval_target_known(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return false;
    for (size_t i = 0; i < ctx->known_targets.count; i++) {
        if (eval_sv_key_eq(ctx->known_targets.items[i], name)) return true;
    }
    return false;
}

bool eval_target_register(Evaluator_Context *ctx, String_View name) {
    if (!ctx || eval_target_known(ctx, name)) return true;
    name = sv_copy_to_event_arena(ctx, name);
    if (eval_should_stop(ctx)) return false;
    if (!arena_da_try_append(ctx->known_targets_arena, &ctx->known_targets, name)) return ctx_oom(ctx);
    return true;
}

// -----------------------------------------------------------------------------
// Argument resolution (token flattening + variable expansion + list splitting)
// -----------------------------------------------------------------------------

static bool sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

static bool sv_list_push(Arena *arena, SV_List *list, String_View sv) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = sv;
    return true;
}

static bool sv_strip_cmake_bracket_arg(String_View in, String_View *out) {
    if (!out) return false;
    *out = in;
    if (in.count < 4 || !in.data) return false;
    if (in.data[0] != '[') return false;

    size_t eq_count = 0;
    size_t i = 1;
    while (i < in.count && in.data[i] == '=') {
        eq_count++;
        i++;
    }
    if (i >= in.count || in.data[i] != '[') return false;
    size_t open_len = i + 1; // '[' + '='* + '['

    if (in.count < open_len + 2 + eq_count) return false;
    size_t close_pos = in.count - (eq_count + 2); // start of closing ']'
    if (in.data[close_pos] != ']') return false;
    for (size_t k = 0; k < eq_count; k++) {
        if (in.data[close_pos + 1 + k] != '=') return false;
    }
    if (in.data[in.count - 1] != ']') return false;

    if (close_pos < open_len) return false;
    *out = nob_sv_from_parts(in.data + open_len, close_pos - open_len);
    return true;
}

static String_View arg_to_sv_flat(Evaluator_Context *ctx, const Arg *arg) {
    if (!ctx || !arg || arg->count == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < arg->count; i++) total += arg->items[i].text.count;

    // Flatten into temp arena; caller rewinds at statement boundary.
    char *buf = (char*)arena_alloc(ctx->arena, total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arg->count; i++) {
        String_View t = arg->items[i].text;
        if (t.count) {
            memcpy(buf + off, t.data, t.count);
            off += t.count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static SV_List eval_resolve_args_impl(Evaluator_Context *ctx,
                                      const Args *raw_args,
                                      bool expand_vars,
                                      bool split_unquoted_lists) {
    SV_List out = {0};
    if (!ctx || !raw_args || eval_should_stop(ctx)) return out;

    for (size_t i = 0; i < raw_args->count; i++) {
        const Arg *arg = &raw_args->items[i];

        String_View flat = arg_to_sv_flat(ctx, arg);
        String_View expanded = expand_vars ? eval_expand_vars(ctx, flat) : flat;
        if (eval_should_stop(ctx)) return (SV_List){0};

        if (arg->kind == ARG_QUOTED) {
            // Quoted args preserve semicolons as plain text.
            if (expanded.count >= 2 && expanded.data[0] == '"' && expanded.data[expanded.count - 1] == '"') {
                expanded.data += 1;
                expanded.count -= 2;
            }
            if (!sv_list_push(ctx->arena, &out, expanded)) {
                ctx_oom(ctx);
                return (SV_List){0};
            }
        } else if (arg->kind == ARG_BRACKET) {
            // Bracket args also preserve semicolons and skip normal splitting.
            String_View stripped = expanded;
            (void)sv_strip_cmake_bracket_arg(expanded, &stripped);
            expanded = stripped;
            if (!sv_list_push(ctx->arena, &out, expanded)) {
                ctx_oom(ctx);
                return (SV_List){0};
            }
        } else {
            // Macro call-site semantics keep unquoted args literal (no list split).
            if (!split_unquoted_lists) {
                if (!sv_list_push(ctx->arena, &out, expanded)) {
                    ctx_oom(ctx);
                    return (SV_List){0};
                }
                continue;
            }

            // Default command/function semantics: unquoted args are list-split by ';'.
            if (expanded.count == 0) continue;
            if (!eval_sv_split_semicolon_genex_aware(ctx->arena, expanded, &out)) {
                ctx_oom(ctx);
                return (SV_List){0};
            }
        }
    }

    return out;
}

SV_List eval_resolve_args(Evaluator_Context *ctx, const Args *raw_args) {
    return eval_resolve_args_impl(ctx, raw_args, true, true);
}

SV_List eval_resolve_args_literal(Evaluator_Context *ctx, const Args *raw_args) {
    return eval_resolve_args_impl(ctx, raw_args, false, false);
}

// -----------------------------------------------------------------------------
// Node evaluation and control-flow flattening
// -----------------------------------------------------------------------------

static bool eval_node_list(Evaluator_Context *ctx, const Node_List *list);
static bool clone_node_to_event(Evaluator_Context *ctx, const Node *src, Node *dst);
static bool clone_node_list_to_event(Evaluator_Context *ctx, const Node_List *src, Node_List *dst);
static bool clone_elseif_list_to_event(Evaluator_Context *ctx, const ElseIf_Clause_List *src, ElseIf_Clause_List *dst);

static bool eval_if(Evaluator_Context *ctx, const Node *node) {
    bool cond = eval_condition(ctx, &node->as.if_stmt.condition);
    if (ctx->oom) return false;
    if (cond) return eval_node_list(ctx, &node->as.if_stmt.then_block);

    for (size_t i = 0; i < node->as.if_stmt.elseif_clauses.count; i++) {
        const ElseIf_Clause *cl = &node->as.if_stmt.elseif_clauses.items[i];
        bool elseif_cond = eval_condition(ctx, &cl->condition);
        if (ctx->oom) return false;
        if (elseif_cond) return eval_node_list(ctx, &cl->block);
    }

    return eval_node_list(ctx, &node->as.if_stmt.else_block);
}

static bool eval_foreach(Evaluator_Context *ctx, const Node *node) {
    SV_List a = eval_resolve_args(ctx, &node->as.foreach_stmt.args);
    if (ctx->oom) return false;
    if (a.count == 0) return true;

    String_View var = a.items[0];
    size_t idx = 1;

    if (idx < a.count && eval_sv_eq_ci_lit(a.items[idx], "IN")) {
        idx++;
        if (idx < a.count && eval_sv_eq_ci_lit(a.items[idx], "ITEMS")) idx++;
    }

    ctx->loop_depth++;
    for (; idx < a.count; idx++) {
        if (!eval_var_set(ctx, var, a.items[idx])) {
            ctx->loop_depth--;
            return false;
        }
        if (!eval_node_list(ctx, &node->as.foreach_stmt.body)) {
            ctx->loop_depth--;
            return false;
        }
        if (ctx->return_requested) {
            ctx->loop_depth--;
            return true;
        }
        if (ctx->continue_requested) {
            ctx->continue_requested = false;
            continue;
        }
        if (ctx->break_requested) {
            ctx->break_requested = false;
            break;
        }
    }
    ctx->loop_depth--;
    return true;
}

static bool eval_while(Evaluator_Context *ctx, const Node *node) {
    // Hard guard against infinite loops during transpilation.
    const size_t kMaxIter = 10000;
    ctx->loop_depth++;
    for (size_t iter = 0; iter < kMaxIter; iter++) {
        bool cond = eval_condition(ctx, &node->as.while_stmt.condition);
        if (ctx->oom) {
            ctx->loop_depth--;
            return false;
        }
        if (!cond) {
            ctx->loop_depth--;
            return true;
        }
        if (!eval_node_list(ctx, &node->as.while_stmt.body)) {
            ctx->loop_depth--;
            return false;
        }
        if (ctx->return_requested) {
            ctx->loop_depth--;
            return true;
        }
        if (ctx->continue_requested) {
            ctx->continue_requested = false;
            continue;
        }
        if (ctx->break_requested) {
            ctx->break_requested = false;
            ctx->loop_depth--;
            return true;
        }
    }
    ctx->loop_depth--;

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    eval_emit_diag(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("while"),
                   nob_sv_from_cstr("while"),
                   origin,
                   nob_sv_from_cstr("Iteration limit exceeded"),
                   nob_sv_from_cstr("Infinite loop detected"));
    return false;
}

static bool eval_node(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return false;

    {
        char line_buf[32];
        int n = snprintf(line_buf, sizeof(line_buf), "%zu", node->line);
        if (n < 0 || (size_t)n >= sizeof(line_buf)) {
            return ctx_oom(ctx);
        }
        if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_LINE"), nob_sv_from_cstr(line_buf))) {
            return false;
        }
    }

    // Statement-local temp allocations are rewound after each node.
    Arena_Mark mark = arena_mark(ctx->arena);

    bool ok = true;
    switch (node->kind) {
        case NODE_COMMAND:
            if (node->as.cmd.name.count > 0) {
                eval_dispatch_command(ctx, node);
                ok = !eval_should_stop(ctx);
            }
            break;
        case NODE_IF:
            ok = eval_if(ctx, node);
            break;
        case NODE_FOREACH:
            ok = eval_foreach(ctx, node);
            break;
        case NODE_WHILE:
            ok = eval_while(ctx, node);
            break;
        case NODE_FUNCTION:
        case NODE_MACRO:
            ok = eval_user_cmd_register(ctx, node);
            break;
        default:
            break;
    }

    arena_rewind(ctx->arena, mark);
    return ok;
}

static bool eval_node_list(Evaluator_Context *ctx, const Node_List *list) {
    if (!ctx || !list) return false;
    for (size_t i = 0; i < list->count; i++) {
        if (!eval_node(ctx, &list->items[i])) return false;
        if (ctx->break_requested || ctx->continue_requested || ctx->return_requested) return true;
    }
    return true;
}

static bool clone_args_to_event(Evaluator_Context *ctx, const Args *src, Args *dst) {
    if (!ctx || !src || !dst) return false;
    memset(dst, 0, sizeof(*dst));
    if (src->count == 0) return true;

    dst->items = arena_alloc_array(ctx->event_arena, Arg, src->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst->items, false);
    dst->count = src->count;
    dst->capacity = src->count;

    for (size_t i = 0; i < src->count; i++) {
        dst->items[i].kind = src->items[i].kind;
        dst->items[i].count = src->items[i].count;
        dst->items[i].capacity = src->items[i].count;
        if (src->items[i].count == 0) continue;

        dst->items[i].items = arena_alloc_array(ctx->event_arena, Token, src->items[i].count);
        EVAL_OOM_RETURN_IF_NULL(ctx, dst->items[i].items, false);

        for (size_t k = 0; k < src->items[i].count; k++) {
            Token t = src->items[i].items[k];
            t.text = sv_copy_to_event_arena(ctx, t.text);
            if (eval_should_stop(ctx)) return false;
            dst->items[i].items[k] = t;
        }
    }

    return true;
}

static bool clone_node_to_event(Evaluator_Context *ctx, const Node *src, Node *dst) {
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

static bool clone_elseif_list_to_event(Evaluator_Context *ctx, const ElseIf_Clause_List *src, ElseIf_Clause_List *dst) {
    if (!ctx || !src || !dst) return false;
    memset(dst, 0, sizeof(*dst));
    if (src->count == 0) return true;

    dst->items = arena_alloc_array(ctx->event_arena, ElseIf_Clause, src->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst->items, false);
    dst->count = src->count;
    dst->capacity = src->count;

    for (size_t i = 0; i < src->count; i++) {
        if (!clone_args_to_event(ctx, &src->items[i].condition, &dst->items[i].condition)) return false;
        if (!clone_node_list_to_event(ctx, &src->items[i].block, &dst->items[i].block)) return false;
    }
    return true;
}

static bool clone_node_list_to_event(Evaluator_Context *ctx, const Node_List *src, Node_List *dst) {
    if (!ctx || !src || !dst) return false;
    memset(dst, 0, sizeof(*dst));
    if (src->count == 0) return true;

    dst->items = arena_alloc_array(ctx->event_arena, Node, src->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst->items, false);
    dst->count = src->count;
    dst->capacity = src->count;

    for (size_t i = 0; i < src->count; i++) {
        if (!clone_node_to_event(ctx, &src->items[i], &dst->items[i])) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// User command registration/invocation (function/macro)
// -----------------------------------------------------------------------------

bool eval_user_cmd_register(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node) return false;
    if (node->kind != NODE_FUNCTION && node->kind != NODE_MACRO) return false;

    String_View name = sv_copy_to_event_arena(ctx, node->as.func_def.name);
    if (eval_should_stop(ctx)) return false;

    size_t param_count = node->as.func_def.params.count;
    String_View *params = NULL;
    if (param_count > 0) {
        params = (String_View*)arena_alloc(ctx->event_arena, param_count * sizeof(String_View));
        EVAL_OOM_RETURN_IF_NULL(ctx, params, false);
        for (size_t i = 0; i < param_count; i++) {
            const Arg *param = &node->as.func_def.params.items[i];
            if (param->count == 0) {
                params[i] = nob_sv_from_cstr("");
                continue;
            }
            params[i] = sv_copy_to_event_arena(ctx, param->items[0].text);
            if (eval_should_stop(ctx)) return false;
        }
    }

    Node_List body = {0};
    if (!clone_node_list_to_event(ctx, &node->as.func_def.body, &body)) return false;

    // Store function/macro metadata in persistent arena.
    User_Command cmd = {0};
    cmd.name = name;
    cmd.kind = (node->kind == NODE_MACRO) ? USER_CMD_MACRO : USER_CMD_FUNCTION;
    cmd.params = params;
    cmd.param_count = param_count;
    cmd.body = body;

    if (!arena_da_try_append(ctx->user_commands_arena, &ctx->user_commands, cmd)) return ctx_oom(ctx);
    return true;
}

User_Command *eval_user_cmd_find(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return NULL;
    for (size_t i = ctx->user_commands.count; i-- > 0;) {
        if (sv_eq_ci(ctx->user_commands.items[i].name, name)) {
            return &ctx->user_commands.items[i];
        }
    }
    return NULL;
}

bool eval_user_cmd_invoke(Evaluator_Context *ctx, String_View name, const SV_List *args, Cmake_Event_Origin origin) {
    (void)origin;
    if (eval_should_stop(ctx)) return false;

    User_Command *cmd = eval_user_cmd_find(ctx, name);
    if (!cmd) return false;

    // function() creates scope; macro() executes in caller scope.
    bool is_function = (cmd->kind == USER_CMD_FUNCTION);
    bool is_macro = (cmd->kind == USER_CMD_MACRO);
    bool scope_pushed = false;
    bool macro_pushed = false;
    if (is_function) {
        if (!eval_scope_push(ctx)) return false;
        scope_pushed = true;
    } else if (is_macro) {
        if (!eval_macro_frame_push(ctx)) return false;
        macro_pushed = true;
    }

    bool ok = false;
    for (size_t i = 0; i < cmd->param_count; i++) {
        String_View val = nob_sv_from_cstr("");
        if (i < args->count) val = args->items[i];
        if (is_function) {
            if (!eval_var_set(ctx, cmd->params[i], val)) goto cleanup;
        } else {
            if (!eval_macro_bind_set(ctx, cmd->params[i], val)) goto cleanup;
        }
    }

    // CMake implicit args.
    char buf_argc[64];
    int argc_n = snprintf(buf_argc, sizeof(buf_argc), "%zu", args->count);
    if (argc_n < 0 || (size_t)argc_n >= sizeof(buf_argc)) {
        return ctx_oom(ctx);
    }
    if (is_function) {
        if (!eval_var_set(ctx, nob_sv_from_cstr("ARGC"), nob_sv_from_cstr(buf_argc))) goto cleanup;
    } else {
        if (!eval_macro_bind_set(ctx, nob_sv_from_cstr("ARGC"), nob_sv_from_cstr(buf_argc))) goto cleanup;
    }

    String_View argv_val = eval_sv_join_semi_temp(ctx, args->items, args->count);
    if (is_function) {
        if (!eval_var_set(ctx, nob_sv_from_cstr("ARGV"), argv_val)) goto cleanup;
    } else {
        if (!eval_macro_bind_set(ctx, nob_sv_from_cstr("ARGV"), argv_val)) goto cleanup;
    }

    if (args->count > cmd->param_count) {
        String_View argn_val = eval_sv_join_semi_temp(ctx,
                                                      &args->items[cmd->param_count],
                                                      args->count - cmd->param_count);
        if (is_function) {
            if (!eval_var_set(ctx, nob_sv_from_cstr("ARGN"), argn_val)) goto cleanup;
        } else {
            if (!eval_macro_bind_set(ctx, nob_sv_from_cstr("ARGN"), argn_val)) goto cleanup;
        }
    } else {
        if (is_function) {
            if (!eval_var_set(ctx, nob_sv_from_cstr("ARGN"), nob_sv_from_cstr(""))) goto cleanup;
        } else {
            if (!eval_macro_bind_set(ctx, nob_sv_from_cstr("ARGN"), nob_sv_from_cstr(""))) goto cleanup;
        }
    }

    for (size_t i = 0; i < args->count; i++) {
        char key[64];
        int key_n = snprintf(key, sizeof(key), "ARGV%zu", i);
        if (key_n < 0 || (size_t)key_n >= sizeof(key)) goto cleanup;
        if (is_function) {
            if (!eval_var_set(ctx, nob_sv_from_cstr(key), args->items[i])) goto cleanup;
        } else {
            if (!eval_macro_bind_set(ctx, nob_sv_from_cstr(key), args->items[i])) goto cleanup;
        }
    }

    ok = eval_node_list(ctx, &cmd->body);
cleanup:
    if (ctx->return_requested) {
        ctx->return_requested = false;
    }
    if (scope_pushed) {
        eval_scope_pop(ctx);
    } else if (macro_pushed) {
        eval_macro_frame_pop(ctx);
    }

    return ok;
}

// -----------------------------------------------------------------------------
// Scope stack management
// -----------------------------------------------------------------------------

static bool ensure_scope_capacity(Evaluator_Context *ctx, size_t min_cap) {
    if (!ctx || !ctx->event_arena) return false;
    if (min_cap <= ctx->scope_capacity) return true;

    size_t new_cap = ctx->scope_capacity == 0 ? 4 : ctx->scope_capacity * 2;
    while (new_cap < min_cap) new_cap *= 2;

    // Do not rely on realloc-last semantics here: the persistent arena receives
    // many interleaved allocations (vars, macro frames, user commands).
    // Growing by allocate+copy is stable and preserves existing scope payloads.
    Var_Scope *new_scopes = arena_alloc_array_zero(ctx->event_arena, Var_Scope, new_cap);
    EVAL_OOM_RETURN_IF_NULL(ctx, new_scopes, false);
    if (ctx->scopes && ctx->scope_capacity > 0) {
        memcpy(new_scopes, ctx->scopes, ctx->scope_capacity * sizeof(Var_Scope));
    }
    ctx->scopes = new_scopes;
    ctx->scope_capacity = new_cap;
    return true;
}

bool eval_scope_push(Evaluator_Context *ctx) {
    if (!ctx) return false;
    if (!ensure_scope_capacity(ctx, ctx->scope_depth + 1)) return ctx_oom(ctx);
    ctx->scope_depth++;
    Var_Scope *s = &ctx->scopes[ctx->scope_depth - 1];
    s->vars = NULL;
    return true;
}

void eval_scope_pop(Evaluator_Context *ctx) {
    if (ctx && ctx->scope_depth > 1) {
        Var_Scope *s = &ctx->scopes[ctx->scope_depth - 1];
        if (s->vars) {
            stbds_shfree(s->vars);
            s->vars = NULL;
        }
        ctx->scope_depth--;
    }
}

// -----------------------------------------------------------------------------
// CMake policy stack/state
// -----------------------------------------------------------------------------

static bool policy_parse_depth(String_View sv, size_t *out_depth) {
    if (!out_depth || sv.count == 0) return false;
    size_t acc = 0;
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c < '0' || c > '9') return false;
        size_t digit = (size_t)(c - '0');
        if (acc > (SIZE_MAX / 10)) return false;
        acc = (acc * 10) + digit;
    }
    if (acc == 0) return false;
    *out_depth = acc;
    return true;
}

static size_t policy_current_depth(Evaluator_Context *ctx) {
    if (!ctx) return 1;
    String_View depth_sv = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_POLICY_STACK_DEPTH"));
    size_t depth = 1;
    if (!policy_parse_depth(depth_sv, &depth)) depth = 1;
    return depth;
}

static bool policy_set_depth(Evaluator_Context *ctx, size_t depth) {
    if (!ctx || depth == 0) return false;
    char depth_buf[32];
    int n = snprintf(depth_buf, sizeof(depth_buf), "%zu", depth);
    if (n < 0 || (size_t)n >= sizeof(depth_buf)) return ctx_oom(ctx);
    return eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_POLICY_STACK_DEPTH"), nob_sv_from_cstr(depth_buf));
}

static String_View policy_canonical_id_temp(Evaluator_Context *ctx, String_View policy_id) {
    if (!ctx || !eval_policy_is_id(policy_id)) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), 8);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < 7; i++) {
        buf[i] = (char)toupper((unsigned char)policy_id.data[i]);
    }
    buf[7] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View policy_slot_key_temp(Evaluator_Context *ctx, size_t depth, String_View canonical_id) {
    if (!ctx || canonical_id.count == 0) return nob_sv_from_cstr("");
    static const char *prefix = "NOBIFY_POLICY_D";

    char depth_buf[32];
    int n = snprintf(depth_buf, sizeof(depth_buf), "%zu", depth);
    if (n < 0 || (size_t)n >= sizeof(depth_buf)) return nob_sv_from_cstr("");
    size_t depth_len = (size_t)n;

    size_t prefix_len = strlen(prefix);
    size_t total = prefix_len + depth_len + 1 + canonical_id.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, prefix_len);
    off += prefix_len;
    memcpy(buf + off, depth_buf, depth_len);
    off += depth_len;
    buf[off++] = '_';
    memcpy(buf + off, canonical_id.data, canonical_id.count);
    off += canonical_id.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View policy_default_key_temp(Evaluator_Context *ctx, String_View canonical_id) {
    if (!ctx || canonical_id.count == 0) return nob_sv_from_cstr("");
    static const char *prefix = "CMAKE_POLICY_DEFAULT_";
    size_t prefix_len = strlen(prefix);
    size_t total = prefix_len + canonical_id.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, prefix, prefix_len);
    memcpy(buf + prefix_len, canonical_id.data, canonical_id.count);
    buf[total] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View policy_legacy_key_temp(Evaluator_Context *ctx, String_View canonical_id) {
    if (!ctx || canonical_id.count == 0) return nob_sv_from_cstr("");
    static const char *prefix = "CMAKE_POLICY_";
    size_t prefix_len = strlen(prefix);
    size_t total = prefix_len + canonical_id.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, prefix, prefix_len);
    memcpy(buf + prefix_len, canonical_id.data, canonical_id.count);
    buf[total] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View policy_normalize_status(String_View v) {
    if (eval_sv_eq_ci_lit(v, "NEW")) return nob_sv_from_cstr("NEW");
    if (eval_sv_eq_ci_lit(v, "OLD")) return nob_sv_from_cstr("OLD");
    return nob_sv_from_cstr("");
}

bool eval_policy_is_id(String_View policy_id) {
    if (policy_id.count != 7) return false;
    if (!(policy_id.data[0] == 'C' || policy_id.data[0] == 'c')) return false;
    if (!(policy_id.data[1] == 'M' || policy_id.data[1] == 'm')) return false;
    if (!(policy_id.data[2] == 'P' || policy_id.data[2] == 'p')) return false;
    for (size_t i = 3; i < 7; i++) {
        if (!isdigit((unsigned char)policy_id.data[i])) return false;
    }
    return true;
}

bool eval_policy_push(Evaluator_Context *ctx) {
    if (!ctx) return false;
    size_t depth = policy_current_depth(ctx);
    return policy_set_depth(ctx, depth + 1);
}

bool eval_policy_pop(Evaluator_Context *ctx) {
    if (!ctx) return false;
    size_t depth = policy_current_depth(ctx);
    if (depth <= 1) return false;
    return policy_set_depth(ctx, depth - 1);
}

bool eval_policy_set(Evaluator_Context *ctx, String_View policy_id, String_View value) {
    if (!ctx || eval_should_stop(ctx)) return false;
    String_View canonical_id = policy_canonical_id_temp(ctx, policy_id);
    if (eval_should_stop(ctx) || canonical_id.count == 0) return false;

    String_View normalized = policy_normalize_status(value);
    if (normalized.count == 0) return false;

    size_t depth = policy_current_depth(ctx);
    if (depth == 0) depth = 1;

    String_View slot_key = policy_slot_key_temp(ctx, depth, canonical_id);
    if (eval_should_stop(ctx) || slot_key.count == 0) return false;
    if (!eval_var_set(ctx, slot_key, normalized)) return false;

    // Keep compatibility mirror for scripts reading CMAKE_POLICY_CMP<NNNN>.
    String_View legacy_key = policy_legacy_key_temp(ctx, canonical_id);
    if (eval_should_stop(ctx) || legacy_key.count == 0) return false;
    return eval_var_set(ctx, legacy_key, normalized);
}

String_View eval_policy_get_effective(Evaluator_Context *ctx, String_View policy_id) {
    if (!ctx || eval_should_stop(ctx)) return nob_sv_from_cstr("");
    String_View canonical_id = policy_canonical_id_temp(ctx, policy_id);
    if (eval_should_stop(ctx) || canonical_id.count == 0) return nob_sv_from_cstr("");

    size_t depth = policy_current_depth(ctx);
    for (size_t d = depth; d > 0; d--) {
        String_View slot_key = policy_slot_key_temp(ctx, d, canonical_id);
        if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        if (!eval_var_defined(ctx, slot_key)) continue;
        String_View v = policy_normalize_status(eval_var_get(ctx, slot_key));
        if (v.count > 0) return v;
    }

    // Backward compatibility with legacy variable path.
    String_View legacy_key = policy_legacy_key_temp(ctx, canonical_id);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (eval_var_defined(ctx, legacy_key)) {
        String_View v = policy_normalize_status(eval_var_get(ctx, legacy_key));
        if (v.count > 0) return v;
    }

    // Honor documented default override variable.
    String_View default_key = policy_default_key_temp(ctx, canonical_id);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (eval_var_defined(ctx, default_key)) {
        String_View v = policy_normalize_status(eval_var_get(ctx, default_key));
        if (v.count > 0) return v;
    }

    // Heuristic fallback: if policy-version is set, prefer NEW defaults.
    // This mirrors cmake_policy(VERSION) intent in evaluator contexts.
    String_View policy_version = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"));
    if (policy_version.count > 0) return nob_sv_from_cstr("NEW");

    return nob_sv_from_cstr("");
}

// -----------------------------------------------------------------------------
// External execution: include() / add_subdirectory()
// -----------------------------------------------------------------------------

static String_View get_dir_from_path(String_View path) {
    for (size_t i = path.count; i-- > 0;) {
        if (path.data[i] == '/' || path.data[i] == '\\') {
            return nob_sv_from_parts(path.data, i);
        }
    }
    return nob_sv_from_cstr(".");
}

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) {
        return false;
    }
    list->items[list->count++] = token;
    return true;
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
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("lexer"),
                           nob_sv_from_cstr("parse"),
                           o,
                           nob_sv_from_cstr("Invalid token while evaluating external file"),
                           nob_sv_from_cstr("Check escaping, quoting and variable syntax"));
            return false;
        }
        if (!token_list_append(ctx->arena, out_tokens, token)) {
            ctx_oom(ctx);
            return false;
        }
    }

    return !eval_should_stop(ctx);
}

static bool eval_parse_external_ast(Evaluator_Context *ctx,
                                    const Token_List *tokens,
                                    Ast_Root *out_ast) {
    if (!ctx || !tokens || !out_ast) return false;
    *out_ast = parse_tokens(ctx->arena, *tokens);
    return !eval_should_stop(ctx);
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
    state->old_list_file = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_FILE"));
    state->old_list_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"));
    state->old_src_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    state->old_bin_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    state->is_add_subdirectory = is_add_subdirectory;
    state->restore_needed = true;

    String_View new_list_dir = get_dir_from_path(file_path);
    const char *new_current_file = arena_strndup(ctx->event_arena, path_c, strlen(path_c));
    EVAL_OOM_RETURN_IF_NULL(ctx, new_current_file, false);
    ctx->current_file = new_current_file;

    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_FILE"), file_path)) return false;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"), new_list_dir)) return false;

    if (!is_add_subdirectory) return true;

    if (!eval_scope_push(ctx)) return false;
    state->scope_pushed = true;

    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"), new_list_dir)) return false;
    if (explicit_bin_dir.count > 0) {
        if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"), explicit_bin_dir)) return false;
    } else {
        String_View bin_path = sv_copy_to_event_arena(ctx, new_list_dir);
        if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"), bin_path)) return false;
    }
    return true;
}

static void eval_pop_external_context(Evaluator_Context *ctx, const External_Eval_State *state) {
    if (!ctx || !state || !state->restore_needed) return;

    if (state->scope_pushed) {
        eval_scope_pop(ctx);
    }

    ctx->current_file = state->old_file;
    (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_FILE"), state->old_list_file);
    (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"), state->old_list_dir);
    if (state->is_add_subdirectory) {
        (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"), state->old_src_dir);
        (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"), state->old_bin_dir);
    }
}

bool eval_execute_file(Evaluator_Context *ctx,
                       String_View file_path,
                       bool is_add_subdirectory,
                       String_View explicit_bin_dir) {
    if (eval_should_stop(ctx)) return false;
    Arena_Mark temp_mark = arena_mark(ctx->arena);
    bool ok = false;
    char *path_c = NULL;
    String_View source_code = {0};
    Token_List tokens = {0};
    Ast_Root new_ast = {0};
    External_Eval_State state = {0};

    if (!eval_read_external_source(ctx, file_path, &path_c, &source_code)) goto cleanup;
    if (!eval_lex_external_tokens(ctx, path_c, source_code, &tokens)) goto cleanup;
    if (!eval_parse_external_ast(ctx, &tokens, &new_ast)) goto cleanup;
    if (!eval_push_external_context(ctx, file_path, path_c, is_add_subdirectory, explicit_bin_dir, &state)) {
        eval_pop_external_context(ctx, &state);
        goto cleanup;
    }

    ok = eval_node_list(ctx, &new_ast);
    if (ctx->return_requested) ctx->return_requested = false;
    eval_pop_external_context(ctx, &state);

cleanup:
    arena_rewind(ctx->arena, temp_mark);
    return ok;
}

static String_View detect_host_system_name(void) {
#if defined(_WIN32)
    return nob_sv_from_cstr("Windows");
#elif defined(__APPLE__)
    return nob_sv_from_cstr("Darwin");
#elif defined(__linux__)
    return nob_sv_from_cstr("Linux");
#elif defined(__unix__)
    return nob_sv_from_cstr("Unix");
#else
    return nob_sv_from_cstr("Unknown");
#endif
}

static String_View detect_compiler_id(void) {
#if defined(__clang__)
    return nob_sv_from_cstr("Clang");
#elif defined(_MSC_VER)
    return nob_sv_from_cstr("MSVC");
#elif defined(__GNUC__)
    return nob_sv_from_cstr("GNU");
#else
    return nob_sv_from_cstr("Unknown");
#endif
}

static String_View detect_host_processor(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return nob_sv_from_cstr("x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    return nob_sv_from_cstr("aarch64");
#elif defined(__i386__) || defined(_M_IX86)
    return nob_sv_from_cstr("x86");
#elif defined(__arm__) || defined(_M_ARM)
    return nob_sv_from_cstr("arm");
#else
    return nob_sv_from_cstr("unknown");
#endif
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

Evaluator_Context *evaluator_create(const Evaluator_Init *init) {
    if (!init || !init->arena || !init->event_arena || !init->stream) return NULL;

    Evaluator_Context *ctx = arena_alloc_zero(init->event_arena, sizeof(Evaluator_Context));
    if (!ctx) return NULL;

    ctx->arena = init->arena;
    ctx->event_arena = init->event_arena;
    ctx->stream = init->stream;
    ctx->source_dir = init->source_dir;
    ctx->binary_dir = init->binary_dir;

    ctx->known_targets_arena = arena_create(4096);
    if (!ctx->known_targets_arena) return NULL;
    if (!arena_on_destroy(init->event_arena, destroy_sub_arena_cb, ctx->known_targets_arena)) {
        arena_destroy(ctx->known_targets_arena);
        ctx->known_targets_arena = NULL;
        return NULL;
    }

    ctx->user_commands_arena = arena_create(4096);
    if (!ctx->user_commands_arena) return NULL;
    if (!arena_on_destroy(init->event_arena, destroy_sub_arena_cb, ctx->user_commands_arena)) {
        arena_destroy(ctx->user_commands_arena);
        ctx->user_commands_arena = NULL;
        return NULL;
    }

    if (init->current_file) {
        ctx->current_file = arena_strndup(init->event_arena, init->current_file, strlen(init->current_file));
        if (!ctx->current_file) return NULL;
    } else {
        ctx->current_file = NULL;
    }

    // Global scope always exists at depth 1.
    if (!ensure_scope_capacity(ctx, 1)) return NULL;
    ctx->scope_depth = 1;

    // Bootstrap canonical CMAKE_* variables.
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_SOURCE_DIR"), ctx->source_dir)) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"), ctx->binary_dir)) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"), ctx->source_dir)) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"), ctx->binary_dir)) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"), ctx->source_dir)) return NULL;
    if (ctx->current_file) {
        if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_FILE"), nob_sv_from_cstr(ctx->current_file))) return NULL;
    } else {
        if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_FILE"), nob_sv_from_cstr(""))) return NULL;
    }
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_LINE"), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_POLICY_STACK_DEPTH"), nob_sv_from_cstr("1"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), nob_sv_from_cstr(""))) return NULL;

    // Inject host/platform built-ins commonly used by scripts in if() conditions.
#if defined(_WIN32)
    if (!eval_var_set(ctx, nob_sv_from_cstr("WIN32"), nob_sv_from_cstr("1"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("UNIX"), nob_sv_from_cstr("0"))) return NULL;
#else
    if (!eval_var_set(ctx, nob_sv_from_cstr("WIN32"), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("UNIX"), nob_sv_from_cstr("1"))) return NULL;
#endif

#if defined(__APPLE__)
    if (!eval_var_set(ctx, nob_sv_from_cstr("APPLE"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set(ctx, nob_sv_from_cstr("APPLE"), nob_sv_from_cstr("0"))) return NULL;
#endif

#if defined(_MSC_VER) && !defined(__clang__)
    if (!eval_var_set(ctx, nob_sv_from_cstr("MSVC"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set(ctx, nob_sv_from_cstr("MSVC"), nob_sv_from_cstr("0"))) return NULL;
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
    if (!eval_var_set(ctx, nob_sv_from_cstr("MINGW"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set(ctx, nob_sv_from_cstr("MINGW"), nob_sv_from_cstr("0"))) return NULL;
#endif

    // Project and toolchain built-ins.
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_VERSION"), nob_sv_from_cstr("3.28.0"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_MAJOR_VERSION"), nob_sv_from_cstr("3"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_MINOR_VERSION"), nob_sv_from_cstr("28"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_PATCH_VERSION"), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_SYSTEM_NAME"), detect_host_system_name())) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"), detect_host_system_name())) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_SYSTEM_PROCESSOR"), detect_host_processor())) return NULL;

    // Project built-ins default to empty and are updated by project().
    if (!eval_var_set(ctx, nob_sv_from_cstr("PROJECT_NAME"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("PROJECT_VERSION"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_CONTINUE_ON_ERROR"), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_FILE_GLOB_STRICT"), nob_sv_from_cstr("0"))) return NULL;

    // Compiler ID can be fixed for compatibility scripts.
    String_View compiler_id = detect_compiler_id();
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER_ID"), compiler_id)) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID"), compiler_id)) return NULL;

    return ctx;
}

void evaluator_destroy(Evaluator_Context *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->scope_depth; i++) {
        if (ctx->scopes[i].vars) {
            stbds_shfree(ctx->scopes[i].vars);
            ctx->scopes[i].vars = NULL;
        }
    }
}

bool evaluator_run(Evaluator_Context *ctx, Ast_Root ast) {
    if (!ctx || eval_should_stop(ctx)) return false;
    bool ok = eval_node_list(ctx, &ast);
    if (ctx->return_requested) {
        ctx->return_requested = false;
    }
    return ok && !eval_should_stop(ctx);
}
