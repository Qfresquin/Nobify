#include "eval_vars.h"

#include "evaluator_internal.h"

#include <string.h>
#include <stdlib.h>

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool parse_env_var_name(String_View token, String_View *out_name) {
    if (!out_name) return false;
    *out_name = (String_View){0};
    if (token.count < 6) return false; // ENV{a}
    if (memcmp(token.data, "ENV{", 4) != 0) return false;
    if (token.data[token.count - 1] != '}') return false;
    *out_name = nob_sv_from_parts(token.data + 4, token.count - 5);
    return true;
}

static bool set_process_env(Evaluator_Context *ctx, String_View name, String_View value) {
    if (!ctx) return false;
    char *name_c = eval_sv_to_cstr_temp(ctx, name);
    EVAL_OOM_RETURN_IF_NULL(ctx, name_c, false);
    char *value_c = eval_sv_to_cstr_temp(ctx, value);
    EVAL_OOM_RETURN_IF_NULL(ctx, value_c, false);

#if defined(_WIN32)
    return _putenv_s(name_c, value_c) == 0;
#else
    return setenv(name_c, value_c, 1) == 0;
#endif
}

static bool unset_process_env(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return false;
    char *name_c = eval_sv_to_cstr_temp(ctx, name);
    EVAL_OOM_RETURN_IF_NULL(ctx, name_c, false);

#if defined(_WIN32)
    return _putenv_s(name_c, "") == 0;
#else
    return unsetenv(name_c) == 0;
#endif
}

bool eval_handle_set(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count == 0) return !eval_should_stop(ctx);

    String_View var = a.items[0];
    String_View env_name = {0};
    if (parse_env_var_name(var, &env_name)) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);

        // CMake: set(ENV{var} [value]) uses only first value arg and warns on extras.
        String_View env_value = (a.count >= 2) ? a.items[1] : nob_sv_from_cstr("");
        if (a.count > 2) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_WARNING,
                                 nob_sv_from_cstr("set"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("set(ENV{...}) ignores extra arguments after value"),
                                 nob_sv_from_cstr("Only the first value argument is used"));
        }

        if (!set_process_env(ctx, env_name, env_value)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("set"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("Failed to set environment variable"),
                                 env_name);
        }
        return !eval_should_stop(ctx);
    }

    if (a.count == 1) {
        (void)eval_var_unset(ctx, var);
        return !eval_should_stop(ctx);
    }

    bool parent_scope = false;
    size_t cache_idx = 0;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PARENT_SCOPE")) {
            parent_scope = true;
            break;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "CACHE")) {
            cache_idx = i;
            break;
        }
    }

    size_t val_end = a.count;
    if (parent_scope) val_end--;
    else if (cache_idx > 0) val_end = cache_idx;

    String_View value = nob_sv_from_cstr("");
    if (val_end > 1) value = eval_sv_join_semi_temp(ctx, &a.items[1], val_end - 1);

    if (parent_scope) {
        if (ctx->scope_depth > 1) {
            size_t saved_depth = ctx->scope_depth;
            ctx->scope_depth = saved_depth - 1;
            bool ok = eval_var_set(ctx, var, value);
            ctx->scope_depth = saved_depth;
            if (!ok) return false;
        }
        else {
            Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
            if (!eval_emit_diag(ctx,
                                EV_DIAG_ERROR,
                                nob_sv_from_cstr("set"),
                                node->as.cmd.name,
                                o,
                                nob_sv_from_cstr("PARENT_SCOPE used without a parent scope"),
                                nob_sv_from_cstr("Use PARENT_SCOPE only inside function/subscope"))) {
                return false;
            }
        }
    } else {
        (void)eval_var_set(ctx, var, value);
    }

    if (cache_idx > 0) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        Cmake_Event ce = {0};
        ce.kind = EV_SET_CACHE_ENTRY;
        ce.origin = o;
        ce.as.cache_entry.key = sv_copy_to_event_arena(ctx, var);
        ce.as.cache_entry.value = sv_copy_to_event_arena(ctx, value);
        if (!emit_event(ctx, ce)) return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_unset(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (a.count == 0) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("unset"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("unset() requires variable name"),
                             nob_sv_from_cstr("Usage: unset(<var> [CACHE|PARENT_SCOPE])"));
        return !eval_should_stop(ctx);
    }
    if (a.count > 2) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("unset"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("unset() accepts at most one option"),
                             nob_sv_from_cstr("Supported options: CACHE, PARENT_SCOPE"));
        return !eval_should_stop(ctx);
    }

    String_View var = a.items[0];
    String_View env_name = {0};
    if (parse_env_var_name(var, &env_name)) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        if (a.count > 1) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("unset"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("unset(ENV{...}) does not accept options"),
                                 nob_sv_from_cstr("Usage: unset(ENV{<var>})"));
            return !eval_should_stop(ctx);
        }
        if (!unset_process_env(ctx, env_name)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("unset"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("Failed to unset environment variable"),
                                 env_name);
        }
        return !eval_should_stop(ctx);
    }

    bool cache_mode = false;
    bool parent_scope = false;
    if (a.count == 2) {
        if (eval_sv_eq_ci_lit(a.items[1], "CACHE")) {
            cache_mode = true;
        } else if (eval_sv_eq_ci_lit(a.items[1], "PARENT_SCOPE")) {
            parent_scope = true;
        } else {
            Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("unset"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("unset() received unsupported option"),
                                 a.items[1]);
            return !eval_should_stop(ctx);
        }
    }

    if (cache_mode) {
        // Pragmatic cache behavior: emit a cache-entry mutation to empty value.
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        Cmake_Event ce = {0};
        ce.kind = EV_SET_CACHE_ENTRY;
        ce.origin = o;
        ce.as.cache_entry.key = sv_copy_to_event_arena(ctx, var);
        ce.as.cache_entry.value = sv_copy_to_event_arena(ctx, nob_sv_from_cstr(""));
        if (!emit_event(ctx, ce)) return false;
        return !eval_should_stop(ctx);
    }

    if (parent_scope) {
        if (ctx->scope_depth <= 1) {
            Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("unset"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("PARENT_SCOPE used without a parent scope"),
                                 nob_sv_from_cstr("Use PARENT_SCOPE only inside function/subscope"));
            return !eval_should_stop(ctx);
        }

        size_t saved_depth = ctx->scope_depth;
        ctx->scope_depth = saved_depth - 1;
        bool ok = eval_var_unset(ctx, var);
        ctx->scope_depth = saved_depth;
        if (!ok) return false;
        return !eval_should_stop(ctx);
    }

    (void)eval_var_unset(ctx, var);
    return !eval_should_stop(ctx);
}
