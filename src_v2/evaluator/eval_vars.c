#include "eval_vars.h"

#include "evaluator_internal.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "stb_ds.h"

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

static bool parse_cache_type(String_View type, String_View *out_upper_type, bool *out_is_internal) {
    if (!out_upper_type || !out_is_internal) return false;
    *out_upper_type = nob_sv_from_cstr("");
    *out_is_internal = false;
    if (type.count == 0) return false;

    char tmp[32];
    if (type.count >= sizeof(tmp)) return false;
    for (size_t i = 0; i < type.count; i++) tmp[i] = (char)toupper((unsigned char)type.data[i]);
    tmp[type.count] = '\0';

    if (strcmp(tmp, "BOOL") == 0) {
        *out_upper_type = nob_sv_from_cstr("BOOL");
        return true;
    }
    if (strcmp(tmp, "FILEPATH") == 0) {
        *out_upper_type = nob_sv_from_cstr("FILEPATH");
        return true;
    }
    if (strcmp(tmp, "PATH") == 0) {
        *out_upper_type = nob_sv_from_cstr("PATH");
        return true;
    }
    if (strcmp(tmp, "STRING") == 0) {
        *out_upper_type = nob_sv_from_cstr("STRING");
        return true;
    }
    if (strcmp(tmp, "INTERNAL") == 0) {
        *out_upper_type = nob_sv_from_cstr("INTERNAL");
        *out_is_internal = true;
        return true;
    }
    return false;
}

static bool cache_type_is_path_like(String_View cache_type) {
    return eval_sv_eq_ci_lit(cache_type, "PATH") || eval_sv_eq_ci_lit(cache_type, "FILEPATH");
}

static bool cache_promote_untyped_path_value_if_needed(Evaluator_Context *ctx,
                                                        Eval_Cache_Entry *entry,
                                                        String_View cache_type) {
    if (!ctx || !entry) return false;
    if (entry->value.type.count != 0) return true;
    if (!cache_type_is_path_like(cache_type)) return true;
    if (entry->value.data.count == 0) return true;
    if (eval_sv_is_abs_path(entry->value.data)) return true;

    const char *cwd_c = nob_get_current_dir_temp();
    if (!cwd_c || cwd_c[0] == '\0') return true;

    String_View cwd = nob_sv_from_cstr(cwd_c);
    String_View abs = eval_sv_path_join(eval_event_arena(ctx), cwd, entry->value.data);
    if (entry->value.data.count > 0 && abs.count == 0) return ctx_oom(ctx);
    entry->value.data = abs;
    return !eval_should_stop(ctx);
}

static Eval_Cache_Entry *cache_find(Evaluator_Context *ctx, String_View key) {
    if (!ctx || !ctx->cache_entries) return NULL;
    return stbds_shgetp_null(ctx->cache_entries, nob_temp_sv_to_cstr(key));
}

static bool cache_remove(Evaluator_Context *ctx, String_View key) {
    if (!ctx || !ctx->cache_entries) return true;
    (void)stbds_shdel(ctx->cache_entries, nob_temp_sv_to_cstr(key));
    return true;
}

static char *cache_copy_key_cstr(Evaluator_Context *ctx, String_View key) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_event_arena(ctx), key.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (key.count > 0) memcpy(buf, key.data, key.count);
    buf[key.count] = '\0';
    return buf;
}

static bool cache_upsert(Evaluator_Context *ctx,
                         String_View key,
                         String_View value,
                         String_View type,
                         String_View doc) {
    if (!ctx) return false;

    Eval_Cache_Entry *entry = cache_find(ctx, key);
    if (entry) {
        entry->value.data = sv_copy_to_event_arena(ctx, value);
        entry->value.type = sv_copy_to_event_arena(ctx, type);
        entry->value.doc = sv_copy_to_event_arena(ctx, doc);
        return !eval_should_stop(ctx);
    }

    char *stable_key = cache_copy_key_cstr(ctx, key);
    EVAL_OOM_RETURN_IF_NULL(ctx, stable_key, false);
    Eval_Cache_Value cv = {0};
    cv.data = sv_copy_to_event_arena(ctx, value);
    cv.type = sv_copy_to_event_arena(ctx, type);
    cv.doc = sv_copy_to_event_arena(ctx, doc);
    if (eval_should_stop(ctx)) return false;

    Eval_Cache_Entry *entries = ctx->cache_entries;
    stbds_shput(entries, stable_key, cv);
    ctx->cache_entries = entries;
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

    size_t cache_idx = a.count;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "CACHE")) {
            cache_idx = i;
            break;
        }
    }

    if (cache_idx < a.count) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        if (cache_idx + 2 >= a.count) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("set"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("set(... CACHE ...) requires <type> and <docstring>"),
                                 nob_sv_from_cstr("Usage: set(<var> <value>... CACHE <type> <doc> [FORCE])"));
            return !eval_should_stop(ctx);
        }

        String_View cache_type_raw = a.items[cache_idx + 1];
        String_View cache_type = {0};
        bool is_internal = false;
        if (!parse_cache_type(cache_type_raw, &cache_type, &is_internal)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("set"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("set(... CACHE ...) received invalid cache type"),
                                 cache_type_raw);
            return !eval_should_stop(ctx);
        }

        String_View cache_doc = a.items[cache_idx + 2];
        bool force = false;
        if (cache_idx + 3 < a.count) {
            if (cache_idx + 3 == a.count - 1 && eval_sv_eq_ci_lit(a.items[cache_idx + 3], "FORCE")) {
                force = true;
            } else {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("set"),
                                     node->as.cmd.name,
                                     o,
                                     nob_sv_from_cstr("set(... CACHE ...) received unsupported trailing arguments"),
                                     nob_sv_from_cstr("Only optional FORCE is accepted after <docstring>"));
                return !eval_should_stop(ctx);
            }
        }
        if (is_internal) force = true;

        String_View value = nob_sv_from_cstr("");
        if (cache_idx > 1) value = eval_sv_join_semi_temp(ctx, &a.items[1], cache_idx - 1);

        Eval_Cache_Entry *existing = cache_find(ctx, var);
        bool should_write_cache = (existing == NULL) || force;
        String_View cmp0126 = eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0126"));
        bool cmp0126_new = eval_sv_eq_ci_lit(cmp0126, "NEW");
        bool remove_local_binding_old = (existing == NULL) || (existing && existing->value.type.count == 0) || force || is_internal;

        if (!cmp0126_new && remove_local_binding_old) (void)eval_var_unset(ctx, var);

        if (should_write_cache) {
            if (!cache_upsert(ctx, var, value, cache_type, cache_doc)) return false;

            Cmake_Event ce = {0};
            ce.kind = EV_SET_CACHE_ENTRY;
            ce.origin = o;
            ce.as.cache_entry.key = sv_copy_to_event_arena(ctx, var);
            ce.as.cache_entry.value = sv_copy_to_event_arena(ctx, value);
            if (!emit_event(ctx, ce)) return false;
        } else if (existing->value.type.count == 0) {
            if (!cache_promote_untyped_path_value_if_needed(ctx, existing, cache_type)) return false;
            existing->value.type = sv_copy_to_event_arena(ctx, cache_type);
            existing->value.doc = sv_copy_to_event_arena(ctx, cache_doc);
        }

        return !eval_should_stop(ctx);
    }

    if (a.count == 1) {
        (void)eval_var_unset(ctx, var);
        return !eval_should_stop(ctx);
    }

    bool parent_scope = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PARENT_SCOPE")) {
            parent_scope = true;
            break;
        }
    }

    size_t val_end = a.count;
    if (parent_scope) val_end--;

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
        (void)cache_remove(ctx, var);
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
