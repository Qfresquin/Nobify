#include "evaluator.h"
#include "evaluator_internal.h"
#include "eval_dispatcher.h"
#include "eval_exec_core.h"
#include "eval_expr.h"
#include "eval_file_internal.h"
#include "eval_compat.h"
#include "eval_diag_classify.h"
#include "eval_report.h"
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

Event_Origin eval_origin_from_node(const Evaluator_Context *ctx, const Node *node) {
    Event_Origin o = {0};
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
    return ctx ? ctx->runtime_state.continue_on_error_snapshot : false;
}

void eval_request_stop(Evaluator_Context *ctx) {
    if (ctx) ctx->stop_requested = true;
}

void eval_request_stop_on_error(Evaluator_Context *ctx) {
    if (!ctx) return;
    if (eval_continue_on_error(ctx)) return;
    ctx->stop_requested = true;
}

void eval_clear_stop_if_not_oom(Evaluator_Context *ctx) {
    if (!ctx || ctx->oom) return;
    ctx->stop_requested = false;
}

bool eval_should_stop(Evaluator_Context *ctx) {
    if (!ctx) return true;
    return ctx->oom || ctx->stop_requested;
}

// -----------------------------------------------------------------------------
// Diagnostics (error as data)
// -----------------------------------------------------------------------------

Eval_Result eval_emit_diag(Evaluator_Context *ctx,
                           Eval_Diag_Code code,
                           String_View component,
                           String_View command,
                           Event_Origin origin,
                           String_View cause,
                           String_View hint) {
    return eval_emit_diag_with_severity(ctx,
                                        eval_diag_default_severity(code),
                                        code,
                                        component,
                                        command,
                                        origin,
                                        cause,
                                        hint);
}

static Diag_Severity eval_diag_to_global_severity(Event_Diag_Severity sev) {
    return sev == EV_DIAG_ERROR ? DIAG_SEV_ERROR : DIAG_SEV_WARNING;
}

static Event_Diag_Severity eval_diag_from_global_severity(Diag_Severity sev) {
    return sev == DIAG_SEV_ERROR ? EV_DIAG_ERROR : EV_DIAG_WARNING;
}

Eval_Result eval_emit_diag_with_severity(Evaluator_Context *ctx,
                                         Event_Diag_Severity sev,
                                         Eval_Diag_Code code,
                                         String_View component,
                                         String_View command,
                                         Event_Origin origin,
                                         String_View cause,
                                         String_View hint) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();

    Eval_Error_Class cls = eval_diag_error_class(code);
    Event_Diag_Severity compat_sev = eval_compat_effective_severity(ctx, sev);
    Diag_Severity logger_input_sev = eval_diag_to_global_severity(compat_sev);
    Event_Diag_Severity effective_sev = eval_diag_from_global_severity(diag_effective_severity(logger_input_sev));

    diag_log(logger_input_sev,
             "evaluator",
             ctx->current_file ? ctx->current_file : "<input>",
             origin.line,
             origin.col,
             nob_temp_sprintf("%.*s", (int)command.count, command.data ? command.data : ""),
             cause.data ? cause.data : "",
             hint.data ? hint.data : "");

    Event ev = {0};
    ev.h.kind = EVENT_DIAG;
    ev.h.origin = origin;
    ev.h.scope_depth = (uint32_t) eval_scope_visible_depth(ctx);
    ev.h.policy_depth = (uint32_t) eval_policy_visible_depth(ctx);
    ev.as.diag.severity = effective_sev;
    ev.as.diag.component = sv_copy_to_event_arena(ctx, component);
    ev.as.diag.command = sv_copy_to_event_arena(ctx, command);
    ev.as.diag.code = sv_copy_to_event_arena(ctx, eval_diag_code_to_sv(code));
    ev.as.diag.error_class = sv_copy_to_event_arena(ctx, eval_error_class_to_sv(cls));
    ev.as.diag.cause = sv_copy_to_event_arena(ctx, cause);
    ev.as.diag.hint = sv_copy_to_event_arena(ctx, hint);
    if (!event_stream_push(ctx->stream, &ev)) {
        (void)ctx_oom(ctx);
        return eval_result_fatal();
    }
    eval_report_record_diag(ctx, compat_sev, effective_sev, code);
    (void)eval_compat_decide_on_diag(ctx, effective_sev);
    if (eval_should_stop(ctx)) return eval_result_fatal();
    if (effective_sev == EV_DIAG_ERROR) return eval_result_soft_error();
    return eval_result_ok();
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

String_View eval_var_get_visible(Evaluator_Context *ctx, String_View key) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0) return nob_sv_from_cstr("");
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    for (size_t d = eval_scope_visible_depth(ctx); d-- > 0;) {
        Var_Scope *s = &scope->scopes[d];
        Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
        if (b) return b->value;
    }
    Eval_Cache_Entry *ce = eval_cache_var_find(scope->cache_entries, key);
    if (ce) return ce->value.data;
    return nob_sv_from_cstr("");
}

static bool eval_variable_watch_notify(Evaluator_Context *ctx,
                                       String_View key,
                                       String_View action,
                                       String_View value) {
    if (!ctx || ctx->runtime_state.in_variable_watch_notification || key.count == 0) return true;
    if (nob_sv_starts_with(key, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_"))) return true;

    Eval_Command_State *commands = eval_command_slice(ctx);
    bool watched = false;
    String_View command = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(commands->watched_variables); i++) {
        if (!eval_sv_key_eq(commands->watched_variables[i], key)) continue;
        watched = true;
        if (i < arena_arr_len(commands->watched_variable_commands)) {
            command = commands->watched_variable_commands[i];
        }
        break;
    }
    if (!watched) return true;

    ctx->runtime_state.in_variable_watch_notification = true;
    bool ok = true;
    ok = ok && eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VAR"), key);
    ok = ok && eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_ACTION"), action);
    ok = ok && eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VALUE"), value);
    if (ok && command.count > 0) {
        ok = eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_COMMAND"), command);
    }
    if (ok) {
        Cmake_Event_Origin origin = {0};
        ok = EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("eval_legacy"), nob_sv_from_cstr("variable_watch"), origin, nob_sv_from_cstr("variable_watch() observed a watched variable mutation"), key);
    }
    ctx->runtime_state.in_variable_watch_notification = false;
    return ok;
}

bool eval_var_set_current(Evaluator_Context *ctx, String_View key, String_View value) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0 || eval_should_stop(ctx)) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
    Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
    if (b) {
        b->value = sv_copy_to_event_arena(ctx, value);
        if (eval_should_stop(ctx)) return false;
        return eval_variable_watch_notify(ctx, key, nob_sv_from_cstr("SET"), value);
    }

    char *stable_key = eval_copy_key_cstr_event(ctx, key);
    if (!stable_key) return false;
    String_View stable_value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;

    Eval_Var_Entry *vars = s->vars;
    stbds_shput(vars, stable_key, stable_value);
    s->vars = vars;
    return eval_variable_watch_notify(ctx, key, nob_sv_from_cstr("SET"), value);
}

bool eval_var_unset_current(Evaluator_Context *ctx, String_View key) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0 || eval_should_stop(ctx)) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
    String_View old_value = eval_var_get_visible(ctx, key);
    if (s->vars) {
        (void)stbds_shdel(s->vars, nob_temp_sv_to_cstr(key));
    }
    return eval_variable_watch_notify(ctx, key, nob_sv_from_cstr("UNSET"), old_value);
}

bool eval_var_defined_visible(Evaluator_Context *ctx, String_View key) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    for (size_t d = eval_scope_visible_depth(ctx); d-- > 0;) {
        Var_Scope *s = &scope->scopes[d];
        Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
        if (b) return true;
    }
    if (eval_cache_var_find(scope->cache_entries, key)) return true;
    return false;
}

bool eval_var_defined_current(Evaluator_Context *ctx, String_View key) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
    Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
    return b != NULL;
}

bool eval_var_collect_visible_names(Evaluator_Context *ctx, SV_List *out_names) {
    if (!ctx || !out_names) return false;
    *out_names = NULL;

    Eval_Scope_State *scope_state = eval_scope_slice(ctx);
    for (size_t depth = 0; depth < eval_scope_visible_depth(ctx); depth++) {
        Var_Scope *scope = &scope_state->scopes[depth];
        ptrdiff_t n = stbds_shlen(scope->vars);
        for (ptrdiff_t i = 0; i < n; i++) {
            if (!scope->vars[i].key) continue;
            if (!eval_sv_arr_push_temp(ctx, out_names, nob_sv_from_cstr(scope->vars[i].key))) return false;
        }
    }

    return true;
}

bool eval_cache_defined(Evaluator_Context *ctx, String_View key) {
    if (!ctx || key.count == 0) return false;
    return eval_cache_var_find(ctx->scope_state.cache_entries, key) != NULL;
}

bool eval_cache_set(Evaluator_Context *ctx,
                    String_View key,
                    String_View value,
                    String_View type,
                    String_View doc) {
    if (!ctx || key.count == 0 || eval_should_stop(ctx)) return false;

    Eval_Scope_State *scope = eval_scope_slice(ctx);
    Eval_Cache_Entry *entry = eval_cache_var_find(scope->cache_entries, key);
    if (entry) {
        entry->value.data = sv_copy_to_event_arena(ctx, value);
        if (eval_should_stop(ctx)) return false;
        entry->value.type = sv_copy_to_event_arena(ctx, type);
        if (eval_should_stop(ctx)) return false;
        entry->value.doc = sv_copy_to_event_arena(ctx, doc);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    char *stable_key = eval_copy_key_cstr_event(ctx, key);
    if (!stable_key) return false;

    Eval_Cache_Entry *entries = scope->cache_entries;
    Eval_Cache_Value cache_value = {
        .data = sv_copy_to_event_arena(ctx, value),
        .type = sv_copy_to_event_arena(ctx, type),
        .doc = sv_copy_to_event_arena(ctx, doc),
    };
    if (eval_should_stop(ctx)) return false;
    stbds_shput(entries, stable_key, cache_value);
    scope->cache_entries = entries;
    return true;
}

bool eval_macro_frame_push(Evaluator_Context *ctx) {
    if (!ctx) return false;
    Macro_Frame frame = {0};
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->scope_state.macro_frames, frame);
}

void eval_macro_frame_pop(Evaluator_Context *ctx) {
    if (!ctx || arena_arr_len(ctx->scope_state.macro_frames) == 0) return;
    arena_arr_set_len(ctx->scope_state.macro_frames, arena_arr_len(ctx->scope_state.macro_frames) - 1);
}

bool eval_macro_bind_set(Evaluator_Context *ctx, String_View key, String_View value) {
    if (!ctx || arena_arr_len(ctx->scope_state.macro_frames) == 0) return false;

    Macro_Frame *top = &ctx->scope_state.macro_frames[arena_arr_len(ctx->scope_state.macro_frames) - 1];
    key = sv_copy_to_event_arena(ctx, key);
    value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;

    for (size_t i = arena_arr_len(top->bindings); i-- > 0;) {
        if (eval_sv_key_eq(top->bindings[i].key, key)) {
            top->bindings[i].value = value;
            return true;
        }
    }

    Var_Binding b = {0};
    b.key = key;
    b.value = value;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, top->bindings, b);
}

bool eval_macro_bind_get(Evaluator_Context *ctx, String_View key, String_View *out_value) {
    if (!ctx || !out_value) return false;
    for (size_t fi = arena_arr_len(ctx->scope_state.macro_frames); fi-- > 0;) {
        const Macro_Frame *f = &ctx->scope_state.macro_frames[fi];
        for (size_t i = arena_arr_len(f->bindings); i-- > 0;) {
            if (eval_sv_key_eq(f->bindings[i].key, key)) {
                *out_value = f->bindings[i].value;
                return true;
            }
        }
    }
    return false;
}

bool eval_target_known(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return false;
    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = 0; i < arena_arr_len(commands->known_targets); i++) {
        if (eval_sv_key_eq(commands->known_targets[i].name, name)) return true;
    }
    return false;
}

bool eval_target_register(Evaluator_Context *ctx, String_View name) {
    if (!ctx || eval_target_known(ctx, name)) return true;
    Eval_Command_State *commands = eval_command_slice(ctx);
    Eval_Target_Record record = {0};
    record.name = sv_copy_to_arena(commands->known_targets_arena, name);
    if (name.count > 0 && record.name.count == 0) return ctx_oom(ctx);

    String_View declared_dir = eval_current_source_dir_for_paths(ctx);
    record.declared_dir = sv_copy_to_arena(commands->known_targets_arena, declared_dir);
    if (declared_dir.count > 0 && record.declared_dir.count == 0) return ctx_oom(ctx);

    return EVAL_ARR_PUSH(ctx, commands->known_targets_arena, commands->known_targets, record);
}

bool eval_target_set_imported(Evaluator_Context *ctx, String_View name, bool imported) {
    if (!ctx) return false;
    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = 0; i < arena_arr_len(commands->known_targets); i++) {
        if (!eval_sv_key_eq(commands->known_targets[i].name, name)) continue;
        commands->known_targets[i].imported = imported;
        return true;
    }
    return false;
}

bool eval_target_declared_dir(Evaluator_Context *ctx, String_View name, String_View *out_dir) {
    if (!out_dir) return false;
    *out_dir = nob_sv_from_cstr("");
    if (!ctx) return false;

    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = 0; i < arena_arr_len(commands->known_targets); i++) {
        if (!eval_sv_key_eq(commands->known_targets[i].name, name)) continue;
        *out_dir = commands->known_targets[i].declared_dir;
        return true;
    }
    return true;
}

bool eval_target_is_imported(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return false;
    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = 0; i < arena_arr_len(commands->known_targets); i++) {
        if (!eval_sv_key_eq(commands->known_targets[i].name, name)) continue;
        return commands->known_targets[i].imported;
    }
    return false;
}

bool eval_target_alias_known(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return false;
    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = 0; i < arena_arr_len(commands->alias_targets); i++) {
        if (eval_sv_key_eq(commands->alias_targets[i], name)) return true;
    }
    return false;
}

bool eval_target_alias_register(Evaluator_Context *ctx, String_View name) {
    if (!ctx || eval_target_alias_known(ctx, name)) return true;
    Eval_Command_State *commands = eval_command_slice(ctx);
    name = sv_copy_to_event_arena(ctx, name);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, commands->known_targets_arena, commands->alias_targets, name);
}

// -----------------------------------------------------------------------------
// Argument resolution (token flattening + variable expansion + list splitting)
// -----------------------------------------------------------------------------

static char *sv_ascii_upper_copy(Arena *arena, String_View sv) {
    if (!arena || !sv.data || sv.count == 0) return NULL;
    char *buf = (char*)arena_alloc(arena, sv.count + 1);
    if (!buf) return NULL;
    for (size_t i = 0; i < sv.count; i++) {
        buf[i] = (char)toupper((unsigned char)sv.data[i]);
    }
    buf[sv.count] = '\0';
    return buf;
}

static char *sv_ascii_upper_heap(String_View sv) {
    if (!sv.data || sv.count == 0) return NULL;
    char *buf = (char*)malloc(sv.count + 1);
    if (!buf) return NULL;
    for (size_t i = 0; i < sv.count; i++) {
        buf[i] = (char)toupper((unsigned char)sv.data[i]);
    }
    buf[sv.count] = '\0';
    return buf;
}

static bool sv_list_push(Arena *arena, SV_List *list, String_View sv) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, sv);
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
    if (!ctx || !arg || arena_arr_len(arg->items) == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) total += arg->items[i].text.count;

    // Flatten into temp arena; caller rewinds at statement boundary.
    char *buf = (char*)arena_alloc(ctx->arena, total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) {
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

    for (size_t i = 0; i < arena_arr_len(*raw_args); i++) {
        const Arg *arg = &(*raw_args)[i];

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
// Native command registration/lookup
// -----------------------------------------------------------------------------

static bool eval_native_cmd_index_rebuild(Evaluator_Context *ctx) {
    if (!ctx) return false;
    Eval_Command_State *commands = eval_command_slice(ctx);

    if (commands->native_command_index) {
        stbds_shfree(commands->native_command_index);
        commands->native_command_index = NULL;
    }

    size_t count = arena_arr_len(commands->native_commands);
    for (size_t i = 0; i < count; i++) {
        Eval_Native_Command *cmd = &commands->native_commands[i];
        if (!cmd->normalized_name) {
            Arena *registry_arena = commands->native_commands_arena ? commands->native_commands_arena : ctx->event_arena;
            cmd->normalized_name = sv_ascii_upper_copy(registry_arena, cmd->name);
            EVAL_OOM_RETURN_IF_NULL(ctx, cmd->normalized_name, false);
        }
        stbds_shput(commands->native_command_index, cmd->normalized_name, i);
    }

    return true;
}

const Eval_Native_Command *eval_native_cmd_find_const(const Evaluator_Context *ctx, String_View name) {
    if (!ctx || !name.data || name.count == 0) return NULL;

    Evaluator_Context *mutable_ctx = (Evaluator_Context*)ctx;
    Eval_Command_State *commands = eval_command_slice(mutable_ctx);
    char *lookup_key = sv_ascii_upper_heap(name);
    if (!lookup_key) {
        (void)ctx_oom(mutable_ctx);
        return NULL;
    }

    Eval_Native_Command_Index_Entry *entry = stbds_shgetp_null(commands->native_command_index, lookup_key);
    if (!entry) {
        free(lookup_key);
        return NULL;
    }
    if (entry->value >= arena_arr_len(commands->native_commands)) {
        free(lookup_key);
        return NULL;
    }
    const Eval_Native_Command *out = &commands->native_commands[entry->value];
    free(lookup_key);
    return out;
}

Eval_Native_Command *eval_native_cmd_find(Evaluator_Context *ctx, String_View name) {
    return (Eval_Native_Command*)eval_native_cmd_find_const(ctx, name);
}

bool eval_native_cmd_register_internal(Evaluator_Context *ctx,
                                       const Evaluator_Native_Command_Def *def,
                                       bool is_builtin,
                                       bool allow_during_run) {
    if (!ctx || !def || !def->handler || def->name.count == 0 || !def->name.data) return false;
    if (!allow_during_run && ctx->file_eval_depth > 0) return false;
    if (eval_native_cmd_find(ctx, def->name)) return false;
    if (eval_user_cmd_find(ctx, def->name)) return false;

    Eval_Command_State *commands = eval_command_slice(ctx);
    Arena *registry_arena = commands->native_commands_arena ? commands->native_commands_arena : ctx->event_arena;

    Eval_Native_Command cmd = {0};
    cmd.name = sv_copy_to_event_arena(ctx, def->name);
    if (eval_should_stop(ctx)) return false;
    cmd.normalized_name = sv_ascii_upper_copy(registry_arena, def->name);
    EVAL_OOM_RETURN_IF_NULL(ctx, cmd.normalized_name, false);
    cmd.handler = def->handler;
    cmd.implemented_level = def->implemented_level;
    cmd.fallback_behavior = def->fallback_behavior;
    cmd.is_builtin = is_builtin;

    if (!EVAL_ARR_PUSH(ctx, registry_arena, commands->native_commands, cmd)) return false;
    return eval_native_cmd_index_rebuild(ctx);
}

bool eval_native_cmd_unregister_internal(Evaluator_Context *ctx,
                                         String_View name,
                                         bool allow_builtin_remove,
                                         bool allow_during_run) {
    if (!ctx || name.count == 0 || !name.data) return false;
    if (!allow_during_run && ctx->file_eval_depth > 0) return false;

    Eval_Command_State *commands = eval_command_slice(ctx);
    char *lookup_key = sv_ascii_upper_heap(name);
    EVAL_OOM_RETURN_IF_NULL(ctx, lookup_key, false);

    Eval_Native_Command_Index_Entry *entry = stbds_shgetp_null(commands->native_command_index, lookup_key);
    if (!entry) {
        free(lookup_key);
        return false;
    }

    size_t idx = entry->value;
    size_t count = arena_arr_len(commands->native_commands);
    if (idx >= count) {
        free(lookup_key);
        return false;
    }
    if (commands->native_commands[idx].is_builtin && !allow_builtin_remove) {
        free(lookup_key);
        return false;
    }

    for (size_t j = idx + 1; j < count; j++) {
        commands->native_commands[j - 1] = commands->native_commands[j];
    }
    arena_arr_set_len(commands->native_commands, count - 1);
    free(lookup_key);
    return eval_native_cmd_index_rebuild(ctx);
}

// -----------------------------------------------------------------------------
// Scope stack management
// -----------------------------------------------------------------------------

bool eval_scope_push(Evaluator_Context *ctx) {
    if (!ctx) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    size_t depth = eval_scope_visible_depth(ctx);
    if (depth < arena_arr_len(scope->scopes)) {
        Var_Scope *s = &scope->scopes[depth];
        if (s->vars) {
            stbds_shfree(s->vars);
            s->vars = NULL;
        }
    } else {
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, scope->scopes, ((Var_Scope){0}))) return false;
    }
    scope->visible_scope_depth = depth + 1;
    Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
    s->vars = NULL;
    return true;
}

void eval_scope_pop(Evaluator_Context *ctx) {
    if (ctx && eval_scope_visible_depth(ctx) > 1) {
        Eval_Scope_State *scope = eval_scope_slice(ctx);
        Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
        if (s->vars) {
            stbds_shfree(s->vars);
            s->vars = NULL;
        }
        scope->visible_scope_depth--;
    }
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
    eval_clear_return_state(ctx);
    ctx->runtime_state.compat_profile = EVAL_PROFILE_PERMISSIVE;
    if (init->compat_profile == EVAL_PROFILE_STRICT || init->compat_profile == EVAL_PROFILE_CI_STRICT) {
        ctx->runtime_state.compat_profile = init->compat_profile;
    }
    ctx->runtime_state.unsupported_policy = EVAL_UNSUPPORTED_WARN;
    ctx->runtime_state.error_budget = 0; // 0 == unlimited in permissive profile
    ctx->runtime_state.continue_on_error_snapshot = (ctx->runtime_state.compat_profile == EVAL_PROFILE_PERMISSIVE);
    eval_report_reset(ctx);

    ctx->command_state.native_commands_arena = arena_create(4096);
    if (!ctx->command_state.native_commands_arena) return NULL;
    if (!arena_on_destroy(init->event_arena, destroy_sub_arena_cb, ctx->command_state.native_commands_arena)) {
        arena_destroy(ctx->command_state.native_commands_arena);
        ctx->command_state.native_commands_arena = NULL;
        return NULL;
    }

    ctx->command_state.known_targets_arena = arena_create(4096);
    if (!ctx->command_state.known_targets_arena) return NULL;
    if (!arena_on_destroy(init->event_arena, destroy_sub_arena_cb, ctx->command_state.known_targets_arena)) {
        arena_destroy(ctx->command_state.known_targets_arena);
        ctx->command_state.known_targets_arena = NULL;
        return NULL;
    }

    ctx->command_state.user_commands_arena = arena_create(4096);
    if (!ctx->command_state.user_commands_arena) return NULL;
    if (!arena_on_destroy(init->event_arena, destroy_sub_arena_cb, ctx->command_state.user_commands_arena)) {
        arena_destroy(ctx->command_state.user_commands_arena);
        ctx->command_state.user_commands_arena = NULL;
        return NULL;
    }

    if (init->current_file) {
        ctx->current_file = arena_strndup(init->event_arena, init->current_file, strlen(init->current_file));
        if (!ctx->current_file) return NULL;
    } else {
        ctx->current_file = NULL;
    }

    // Global scope always exists at visible depth 1.
    if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->scope_state.scopes, ((Var_Scope){0}))) return NULL;
    ctx->scope_state.visible_scope_depth = 1;

    // Bootstrap canonical CMAKE_* variables.
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SOURCE_DIR"), ctx->source_dir)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"), ctx->binary_dir)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR), ctx->source_dir)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR), ctx->binary_dir)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR), ctx->source_dir)) return NULL;
    if (ctx->current_file) {
        if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE), nob_sv_from_cstr(ctx->current_file))) return NULL;
    } else {
        if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE), nob_sv_from_cstr(""))) return NULL;
    }
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_LINE"), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_POLICY_STACK_DEPTH), nob_sv_from_cstr("1"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_defer_push_directory(ctx, ctx->source_dir, ctx->binary_dir)) return NULL;

    // Inject host/platform built-ins commonly used by scripts in if() conditions.
#if defined(_WIN32)
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("WIN32"), nob_sv_from_cstr("1"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("UNIX"), nob_sv_from_cstr("0"))) return NULL;
#else
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("WIN32"), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("UNIX"), nob_sv_from_cstr("1"))) return NULL;
#endif

#if defined(__APPLE__)
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("APPLE"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("APPLE"), nob_sv_from_cstr("0"))) return NULL;
#endif

#if defined(_MSC_VER) && !defined(__clang__)
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("MSVC"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("MSVC"), nob_sv_from_cstr("0"))) return NULL;
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("MINGW"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("MINGW"), nob_sv_from_cstr("0"))) return NULL;
#endif

    // Project and toolchain built-ins.
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_VERSION"), nob_sv_from_cstr("3.28.0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_MAJOR_VERSION"), nob_sv_from_cstr("3"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_MINOR_VERSION"), nob_sv_from_cstr("28"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PATCH_VERSION"), nob_sv_from_cstr("0"))) return NULL;
    String_View host_system_name = eval_detect_host_system_name();
    String_View host_processor = eval_detect_host_processor();
    String_View host_system_version = eval_host_os_version_temp(ctx);
    if (eval_should_stop(ctx)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SYSTEM_NAME"), host_system_name)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"), host_system_name)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SYSTEM_PROCESSOR"), host_processor)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_PROCESSOR"), host_processor)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SYSTEM"), host_system_name)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM"), host_system_name)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_VERSION"), host_system_version)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_COMMAND"), nob_sv_from_cstr("cmake"))) return NULL;

    // Project built-ins default to empty and are updated by project().
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_NAME"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_VERSION"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_CONTINUE_ON_ERROR),
                      ctx->runtime_state.compat_profile == EVAL_PROFILE_PERMISSIVE ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_COMPAT_PROFILE), eval_compat_profile_to_sv(ctx->runtime_state.compat_profile))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_ERROR_BUDGET), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_UNSUPPORTED_POLICY), nob_sv_from_cstr("WARN"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_FILE_GLOB_STRICT), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_WHILE_MAX_ITERATIONS), nob_sv_from_cstr("10000"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_ENABLED_LANGUAGES"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_property_write(ctx,
                             (Event_Origin){0},
                             nob_sv_from_cstr("GLOBAL"),
                             nob_sv_from_cstr(""),
                             nob_sv_from_cstr("ENABLED_LANGUAGES"),
                             nob_sv_from_cstr(""),
                             EV_PROP_SET,
                             false)) {
        return NULL;
    }

    {
        const char *cc = eval_getenv_temp(ctx, "CC");
        if (!cc || cc[0] == '\0') cc = "cc";
        const char *cxx = eval_getenv_temp(ctx, "CXX");
        if (!cxx || cxx[0] == '\0') cxx = "c++";
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER"), nob_sv_from_cstr(cc))) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"), nob_sv_from_cstr(cxx))) return NULL;
    }

    // Compiler ID can be fixed for compatibility scripts.
    String_View compiler_id = detect_compiler_id();
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER_ID"), compiler_id)) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID"), compiler_id)) return NULL;
    if (!eval_dispatcher_seed_builtin_commands(ctx)) return NULL;

    return ctx;
}

void evaluator_destroy(Evaluator_Context *ctx) {
    if (!ctx) return;
    eval_file_lock_cleanup(ctx);
    if (ctx->command_state.native_command_index) {
        stbds_shfree(ctx->command_state.native_command_index);
        ctx->command_state.native_command_index = NULL;
    }
    if (ctx->command_state.property_store) {
        stbds_shfree(ctx->command_state.property_store);
        ctx->command_state.property_store = NULL;
    }
    if (ctx->scope_state.cache_entries) {
        stbds_shfree(ctx->scope_state.cache_entries);
        ctx->scope_state.cache_entries = NULL;
    }
    if (ctx->process_state.env_overrides) {
        stbds_shfree(ctx->process_state.env_overrides);
        ctx->process_state.env_overrides = NULL;
    }
    for (size_t i = 0; i < arena_arr_len(ctx->scope_state.scopes); i++) {
        if (ctx->scope_state.scopes[i].vars) {
            stbds_shfree(ctx->scope_state.scopes[i].vars);
            ctx->scope_state.scopes[i].vars = NULL;
        }
    }
}

Eval_Result evaluator_run(Evaluator_Context *ctx, Ast_Root ast) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    size_t entered_file_depth = ++ctx->file_eval_depth;
    eval_report_reset(ctx);
    Eval_Result result = eval_execute_node_list(ctx, &ast);
    if (!eval_result_is_fatal(result) && !eval_should_stop(ctx)) {
        if (!eval_defer_flush_current_directory(ctx)) result = eval_result_fatal();
    }
    if (!eval_result_is_fatal(result) && !eval_should_stop(ctx)) {
        if (!eval_file_generate_flush(ctx)) result = eval_result_fatal();
    }
    eval_clear_return_state(ctx);
    eval_file_lock_release_file_scope(ctx, entered_file_depth);
    if (ctx->file_eval_depth > 0) ctx->file_eval_depth--;
    eval_report_finalize(ctx);
    result = eval_result_merge(result, eval_result_ok_if_running(ctx));
    if (!eval_result_is_fatal(result) && ctx->runtime_state.run_report.error_count > 0) {
        result = eval_result_merge(result, eval_result_soft_error());
    }
    return result;
}

Eval_Result eval_run_ast_inline(Evaluator_Context *ctx, Ast_Root ast) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    return eval_execute_node_list(ctx, &ast);
}

const Eval_Run_Report *evaluator_get_run_report(const Evaluator_Context *ctx) {
    if (!ctx) return NULL;
    return &ctx->runtime_state.run_report;
}

const Eval_Run_Report *evaluator_get_run_report_snapshot(const Evaluator_Context *ctx) {
    return evaluator_get_run_report(ctx);
}

bool evaluator_set_compat_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile) {
    return eval_compat_set_profile(ctx, profile);
}

bool evaluator_register_native_command(Evaluator_Context *ctx, const Evaluator_Native_Command_Def *def) {
    return eval_native_cmd_register_internal(ctx, def, false, false);
}

bool evaluator_unregister_native_command(Evaluator_Context *ctx, String_View command_name) {
    return eval_native_cmd_unregister_internal(ctx, command_name, false, false);
}

bool evaluator_get_command_capability(Evaluator_Context *ctx,
                                      String_View command_name,
                                      Command_Capability *out_capability) {
    return eval_dispatcher_get_command_capability(ctx, command_name, out_capability);
}
