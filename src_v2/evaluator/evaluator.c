#include "evaluator.h"
#include "evaluator_internal.h"
#include "eval_expr.h"
#include "eval_dispatcher.h"
#include "eval_flow.h"
#include "eval_file_internal.h"
#include "eval_compat.h"
#include "eval_diag_classify.h"
#include "eval_report.h"
#include "lexer.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "stb_ds.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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
    eval_refresh_runtime_compat(ctx);

    Eval_Diag_Code code = EVAL_ERR_NONE;
    Eval_Error_Class cls = EVAL_ERR_CLASS_NONE;
    eval_diag_classify(component, cause, sev, &code, &cls);

    Cmake_Diag_Severity effective_sev = eval_compat_effective_severity(ctx, sev);

    diag_log(effective_sev == EV_DIAG_ERROR ? DIAG_SEV_ERROR : DIAG_SEV_WARNING,
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
    ev.as.diag.severity = effective_sev;
    ev.as.diag.component = sv_copy_to_event_arena(ctx, component);
    ev.as.diag.command = sv_copy_to_event_arena(ctx, command);
    ev.as.diag.code = sv_copy_to_event_arena(ctx, eval_diag_code_to_sv(code));
    ev.as.diag.error_class = sv_copy_to_event_arena(ctx, eval_error_class_to_sv(cls));
    ev.as.diag.cause = sv_copy_to_event_arena(ctx, cause);
    ev.as.diag.hint = sv_copy_to_event_arena(ctx, hint);
    if (!event_stream_push(ctx->event_arena, ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    eval_report_record_diag(ctx, effective_sev, code, cls);
    (void)eval_compat_decide_on_diag(ctx, effective_sev);
    return true;
}

// -----------------------------------------------------------------------------
// Variable scopes
// -----------------------------------------------------------------------------
static Eval_Var_Entry *eval_scope_var_find(Eval_Var_Entry *vars, String_View key) {
    if (!vars || !key.data) return NULL;
    return stbds_shgetp_null(vars, nob_temp_sv_to_cstr(key));
}

static Eval_Cache_Entry *eval_cache_var_find(Eval_Cache_Entry *entries, String_View key) {
    if (!entries || !key.data) return NULL;
    return stbds_shgetp_null(entries, nob_temp_sv_to_cstr(key));
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
    Eval_Cache_Entry *ce = eval_cache_var_find(ctx->cache_entries, key);
    if (ce) return ce->value.data;
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
    if (eval_cache_var_find(ctx->cache_entries, key)) return true;
    return false;
}

bool eval_var_defined_in_current_scope(Evaluator_Context *ctx, String_View key) {
    // Used by commands that need local-scope semantics only.
    if (!ctx || ctx->scope_depth == 0) return false;
    Var_Scope *s = &ctx->scopes[ctx->scope_depth - 1];
    Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
    return b != NULL;
}

bool eval_cache_defined(Evaluator_Context *ctx, String_View key) {
    if (!ctx || key.count == 0) return false;
    return eval_cache_var_find(ctx->cache_entries, key) != NULL;
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

bool eval_target_alias_known(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return false;
    for (size_t i = 0; i < ctx->alias_targets.count; i++) {
        if (eval_sv_key_eq(ctx->alias_targets.items[i], name)) return true;
    }
    return false;
}

bool eval_target_alias_register(Evaluator_Context *ctx, String_View name) {
    if (!ctx || eval_target_alias_known(ctx, name)) return true;
    name = sv_copy_to_event_arena(ctx, name);
    if (eval_should_stop(ctx)) return false;
    if (!arena_da_try_append(ctx->known_targets_arena, &ctx->alias_targets, name)) return ctx_oom(ctx);
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

static bool sv_parse_long(String_View sv, long *out) {
    if (!out || sv.count == 0) return false;
    char buf[128];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out = v;
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
    SV_List items = {0};
    bool cmp0124_new = eval_sv_eq_ci_lit(eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0124")), "NEW");

    if (idx < a.count && eval_sv_eq_ci_lit(a.items[idx], "RANGE")) {
        idx++;
        if (a.count - idx < 1 || a.count - idx > 3) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("flow"),
                                 nob_sv_from_cstr("foreach"),
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("foreach(RANGE ...) expects 1..3 numeric arguments"),
                                 nob_sv_from_cstr("Usage: foreach(v RANGE stop) or RANGE start stop [step]"));
            return !eval_should_stop(ctx);
        }
        long start = 0, stop = 0, step = 1;
        if (a.count - idx == 1) {
            if (!sv_parse_long(a.items[idx], &stop)) return false;
        } else {
            if (!sv_parse_long(a.items[idx], &start)) return false;
            if (!sv_parse_long(a.items[idx + 1], &stop)) return false;
            if (a.count - idx == 3 && !sv_parse_long(a.items[idx + 2], &step)) return false;
        }
        if (step == 0) {
            (void)eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("flow"), nob_sv_from_cstr("foreach"),
                                 eval_origin_from_node(ctx, node),
                                 nob_sv_from_cstr("foreach(RANGE ...) step must be non-zero"),
                                 nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        if (a.count - idx == 1) start = 0;
        if ((step > 0 && start > stop) || (step < 0 && start < stop)) return true;
        for (long v = start;; v += step) {
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "%ld", v);
            if (n < 0 || (size_t)n >= sizeof(buf)) return ctx_oom(ctx);
            if (!sv_list_push(ctx->arena, &items, nob_sv_from_cstr(buf))) return ctx_oom(ctx);
            if ((step > 0 && v + step > stop) || (step < 0 && v + step < stop)) break;
        }
    } else if (idx < a.count && eval_sv_eq_ci_lit(a.items[idx], "IN")) {
        idx++;
        if (idx < a.count && eval_sv_eq_ci_lit(a.items[idx], "ITEMS")) {
            idx++;
            items.items = &a.items[idx];
            items.count = a.count - idx;
        } else if (idx < a.count && eval_sv_eq_ci_lit(a.items[idx], "LISTS")) {
            idx++;
            for (; idx < a.count; idx++) {
                String_View list_txt = eval_var_get(ctx, a.items[idx]);
                if (list_txt.count == 0) continue;
                if (!eval_sv_split_semicolon_genex_aware(ctx->arena, list_txt, &items)) return ctx_oom(ctx);
            }
        } else if (idx < a.count && eval_sv_eq_ci_lit(a.items[idx], "ZIP_LISTS")) {
            idx++;
            size_t list_count = a.count - idx;
            String_View **zip_items = arena_alloc_array(eval_temp_arena(ctx), String_View*, list_count);
            size_t *zip_counts = arena_alloc_array(eval_temp_arena(ctx), size_t, list_count);
            EVAL_OOM_RETURN_IF_NULL(ctx, zip_items, false);
            EVAL_OOM_RETURN_IF_NULL(ctx, zip_counts, false);
            size_t max_len = 0;
            for (size_t li = 0; li < list_count; li++, idx++) {
                String_View list_txt = eval_var_get(ctx, a.items[idx]);
                SV_List one = {0};
                if (list_txt.count > 0 && !eval_sv_split_semicolon_genex_aware(ctx->arena, list_txt, &one)) return ctx_oom(ctx);
                if (one.count > max_len) max_len = one.count;
                zip_items[li] = one.items;
                zip_counts[li] = one.count;
            }
            for (size_t row = 0; row < max_len; row++) {
                Nob_String_Builder sb = {0};
                for (size_t li = 0; li < list_count; li++) {
                    String_View cur = (row < zip_counts[li]) ? zip_items[li][row] : nob_sv_from_cstr("");
                    if (li > 0) nob_sb_append(&sb, ';');
                    if (cur.count > 0) nob_sb_append_buf(&sb, cur.data, cur.count);
                }
                String_View zipped = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
                nob_sb_free(sb);
                if (!sv_list_push(ctx->arena, &items, zipped)) return ctx_oom(ctx);
            }
        } else {
            items.items = &a.items[idx];
            items.count = a.count - idx;
        }
    } else {
        items.items = &a.items[idx];
        items.count = a.count - idx;
    }

    String_View loop_old = eval_var_get(ctx, var);
    bool loop_old_defined = eval_var_defined(ctx, var);

    ctx->loop_depth++;
    for (size_t ii = 0; ii < items.count; ii++) {
        if (!eval_var_set(ctx, var, items.items[ii])) {
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
    if (cmp0124_new) {
        if (loop_old_defined) (void)eval_var_set(ctx, var, loop_old);
        else (void)eval_var_unset(ctx, var);
    }
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
        if (ctx->return_requested) {
            if (!eval_unwind_blocks_for_return(ctx)) return false;
            return true;
        }
        if (ctx->break_requested || ctx->continue_requested) return true;
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
    Eval_Return_Context saved_return_ctx = ctx->return_context;
    size_t entered_function_depth = 0;
    bool scope_pushed = false;
    bool macro_pushed = false;
    if (is_function) {
        if (!eval_scope_push(ctx)) return false;
        entered_function_depth = ++ctx->function_eval_depth;
        ctx->return_context = EVAL_RETURN_CTX_FUNCTION;
        scope_pushed = true;
    } else if (is_macro) {
        if (!eval_macro_frame_push(ctx)) return false;
        ctx->return_context = EVAL_RETURN_CTX_MACRO;
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
    if (ctx->return_requested && ctx->return_propagate_count > 0 && is_function && ctx->scope_depth > 1) {
        for (size_t i = 0; i < ctx->return_propagate_count; i++) {
            String_View key = ctx->return_propagate_vars[i];
            if (!eval_var_defined_in_current_scope(ctx, key)) continue;
            String_View value = eval_var_get(ctx, key);
            size_t saved_depth = ctx->scope_depth;
            ctx->scope_depth = saved_depth - 1;
            bool ok_set = eval_var_set(ctx, key, value);
            ctx->scope_depth = saved_depth;
            if (!ok_set) ok = false;
        }
    }
    if (ctx->return_requested) {
        ctx->return_requested = false;
    }
    ctx->return_propagate_vars = NULL;
    ctx->return_propagate_count = 0;
    ctx->return_context = saved_return_ctx;
    if (entered_function_depth > 0) {
        eval_file_lock_release_function_scope(ctx, entered_function_depth);
        if (ctx->function_eval_depth > 0) ctx->function_eval_depth--;
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
    size_t entered_file_depth = ++ctx->file_eval_depth;
    Eval_Return_Context saved_return_ctx = ctx->return_context;
    ctx->return_context = EVAL_RETURN_CTX_INCLUDE;
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
    ctx->return_propagate_vars = NULL;
    ctx->return_propagate_count = 0;
    eval_pop_external_context(ctx, &state);

cleanup:
    eval_file_lock_release_file_scope(ctx, entered_file_depth);
    if (ctx->file_eval_depth > 0) ctx->file_eval_depth--;
    ctx->return_context = saved_return_ctx;
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
    ctx->return_context = EVAL_RETURN_CTX_TOPLEVEL;
    ctx->return_propagate_vars = NULL;
    ctx->return_propagate_count = 0;
    ctx->compat_profile = EVAL_PROFILE_PERMISSIVE;
    if (init->compat_profile == EVAL_PROFILE_STRICT || init->compat_profile == EVAL_PROFILE_CI_STRICT) {
        ctx->compat_profile = init->compat_profile;
    }
    ctx->unsupported_policy = EVAL_UNSUPPORTED_WARN;
    ctx->error_budget = 0; // 0 == unlimited in permissive profile
    eval_report_reset(ctx);

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
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_CONTINUE_ON_ERROR"),
                      ctx->compat_profile == EVAL_PROFILE_PERMISSIVE ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_COMPAT_PROFILE"), eval_compat_profile_to_sv(ctx->compat_profile))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_ERROR_BUDGET"), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_UNSUPPORTED_POLICY"), nob_sv_from_cstr("WARN"))) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_FILE_GLOB_STRICT"), nob_sv_from_cstr("0"))) return NULL;

    {
        const char *cc = getenv("CC");
        if (!cc || cc[0] == '\0') cc = "cc";
        const char *cxx = getenv("CXX");
        if (!cxx || cxx[0] == '\0') cxx = "c++";
        if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER"), nob_sv_from_cstr(cc))) return NULL;
        if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"), nob_sv_from_cstr(cxx))) return NULL;
    }

    // Compiler ID can be fixed for compatibility scripts.
    String_View compiler_id = detect_compiler_id();
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER_ID"), compiler_id)) return NULL;
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID"), compiler_id)) return NULL;

    return ctx;
}

void evaluator_destroy(Evaluator_Context *ctx) {
    if (!ctx) return;
    eval_file_lock_cleanup(ctx);
    if (ctx->policy_levels) {
        free(ctx->policy_levels);
        ctx->policy_levels = NULL;
    }
    if (ctx->cache_entries) {
        stbds_shfree(ctx->cache_entries);
        ctx->cache_entries = NULL;
    }
    for (size_t i = 0; i < ctx->scope_depth; i++) {
        if (ctx->scopes[i].vars) {
            stbds_shfree(ctx->scopes[i].vars);
            ctx->scopes[i].vars = NULL;
        }
    }
}

bool evaluator_run(Evaluator_Context *ctx, Ast_Root ast) {
    if (!ctx || eval_should_stop(ctx)) return false;
    size_t entered_file_depth = ++ctx->file_eval_depth;
    eval_report_reset(ctx);
    bool ok = eval_node_list(ctx, &ast);
    if (ok && !eval_should_stop(ctx)) {
        ok = eval_file_generate_flush(ctx);
    }
    if (ctx->return_requested) {
        ctx->return_requested = false;
    }
    ctx->return_propagate_vars = NULL;
    ctx->return_propagate_count = 0;
    eval_file_lock_release_file_scope(ctx, entered_file_depth);
    if (ctx->file_eval_depth > 0) ctx->file_eval_depth--;
    eval_report_finalize(ctx);
    return ok && !eval_should_stop(ctx);
}

const Eval_Run_Report *evaluator_get_run_report(const Evaluator_Context *ctx) {
    if (!ctx) return NULL;
    return &ctx->run_report;
}

const Eval_Run_Report *evaluator_get_run_report_snapshot(const Evaluator_Context *ctx) {
    return evaluator_get_run_report(ctx);
}

bool evaluator_set_compat_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile) {
    return eval_compat_set_profile(ctx, profile);
}

bool evaluator_get_command_capability(String_View command_name, Command_Capability *out_capability) {
    return eval_dispatcher_get_command_capability(command_name, out_capability);
}
