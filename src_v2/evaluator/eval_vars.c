#include "eval_vars.h"

#include "evaluator_internal.h"
#include "eval_expr.h"
#include "eval_opt_parser.h"
#include "arena_dyn.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "stb_ds.h"

static bool load_cache_emit_diag(EvalExecContext *ctx,
                                 const Node *node,
                                 Cmake_Diag_Severity severity,
                                 String_View cause,
                                 String_View hint) {
    return EVAL_DIAG_BOOL_SEV(ctx, severity, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("load_cache"), node->as.cmd.name, eval_origin_from_node(ctx, node), cause, hint);
}

static bool load_cache_parse_line(String_View line,
                                  String_View *out_key,
                                  String_View *out_type,
                                  String_View *out_value);

typedef enum {
    LOAD_CACHE_MODE_LEGACY = 0,
    LOAD_CACHE_MODE_READ_WITH_PREFIX,
} Load_Cache_Mode;

typedef struct {
    Load_Cache_Mode mode;
    String_View prefix;
    SV_List requested;
    SV_List excludes;
    SV_List include_internals;
    SV_List cache_inputs;
} Load_Cache_Request;

static bool load_cache_name_in_list(String_View name, const SV_List *list) {
    if (!list) return false;
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        if (nob_sv_eq(name, (*list)[i])) return true;
    }
    return false;
}

static bool load_cache_is_keyword(String_View token) {
    return eval_sv_eq_ci_lit(token, "EXCLUDE") ||
           eval_sv_eq_ci_lit(token, "INCLUDE_INTERNALS");
}

static bool load_cache_push_temp(EvalExecContext *ctx, SV_List *list, String_View value) {
    if (!ctx || !list) return false;
    if (value.count == 0) return true;
    return eval_sv_arr_push_temp(ctx, list, value);
}

static bool load_cache_collect_until_keyword(EvalExecContext *ctx,
                                             SV_List args,
                                             size_t *io_index,
                                             SV_List *out_list) {
    if (!ctx || !io_index || !out_list) return false;
    while (*io_index < arena_arr_len(args) && !load_cache_is_keyword(args[*io_index])) {
        if (!load_cache_push_temp(ctx, out_list, args[*io_index])) return false;
        (*io_index)++;
    }
    return true;
}

static String_View load_cache_prefixed_name_temp(EvalExecContext *ctx,
                                                 String_View prefix,
                                                 String_View key) {
    if (!ctx) return nob_sv_from_cstr("");
    size_t total = prefix.count + key.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (prefix.count > 0) memcpy(buf, prefix.data, prefix.count);
    if (key.count > 0) memcpy(buf + prefix.count, key.data, key.count);
    buf[total] = '\0';
    return nob_sv_from_parts(buf, total);
}

static bool load_cache_apply_prefixed_entry(EvalExecContext *ctx,
                                            String_View prefix,
                                            String_View key,
                                            String_View value) {
    if (!ctx) return false;
    String_View prefixed = load_cache_prefixed_name_temp(ctx, prefix, key);
    if (eval_should_stop(ctx)) return false;
    if (value.count == 0) return eval_var_unset_current(ctx, prefixed);
    return eval_var_set_current(ctx, prefixed, value);
}

static bool load_cache_process_contents(EvalExecContext *ctx,
                                        const Node *node,
                                        bool read_with_prefix,
                                        String_View prefix,
                                        const SV_List *requested,
                                        const SV_List *excludes,
                                        const SV_List *include_internals,
                                        const Nob_String_Builder *sb) {
    if (!ctx || !node || !sb) return false;

    const char *data = sb->items ? sb->items : "";
    size_t len = sb->count;
    size_t start = 0;
    while (start <= len) {
        size_t end = start;
        while (end < len && data[end] != '\n') end++;
        size_t line_len = end - start;
        if (line_len > 0 && data[start + line_len - 1] == '\r') line_len--;
        String_View line = nob_sv_from_parts(data + start, line_len);

        String_View key = {0};
        String_View type = {0};
        String_View value = {0};
        if (line.count > 0 && line.data[0] != '#' && line.data[0] != '/') {
            if (!load_cache_parse_line(line, &key, &type, &value)) {
                (void)load_cache_emit_diag(ctx,
                                           node,
                                           EV_DIAG_WARNING,
                                           nob_sv_from_cstr("load_cache() skipped a malformed cache entry"),
                                           line);
            } else {
                bool is_internal = eval_sv_eq_ci_lit(type, "INTERNAL");
                if (read_with_prefix) {
                    if (load_cache_name_in_list(key, requested) &&
                        !load_cache_apply_prefixed_entry(ctx, prefix, key, value)) {
                        return false;
                    }
                } else if ((!is_internal || load_cache_name_in_list(key, include_internals)) &&
                           !load_cache_name_in_list(key, excludes)) {
                    if (!eval_cache_set(ctx,
                                        key,
                                        value,
                                        nob_sv_from_cstr("INTERNAL"),
                                        nob_sv_from_cstr("loaded by load_cache"))) {
                        return false;
                    }
                }
            }
        }

        if (end == len) break;
        start = end + 1;
    }

    return true;
}

static bool load_cache_resolve_path(EvalExecContext *ctx, String_View raw_path, String_View *out_path) {
    if (!ctx || !out_path) return false;
    String_View path = raw_path;
    if (!eval_sv_is_abs_path(path)) {
        String_View base = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
        if (base.count == 0) base = ctx->binary_dir;
        path = eval_sv_path_join(eval_temp_arena(ctx), base, path);
        if (eval_should_stop(ctx)) return false;
    }
    if (!nob_sv_end_with(path, "CMakeCache.txt")) {
        path = eval_sv_path_join(eval_temp_arena(ctx), path, nob_sv_from_cstr("CMakeCache.txt"));
        if (eval_should_stop(ctx)) return false;
    }
    *out_path = path;
    return true;
}

static bool load_cache_parse_read_with_prefix_request(EvalExecContext *ctx,
                                                      const Node *node,
                                                      SV_List args,
                                                      Load_Cache_Request *out_req) {
    if (!ctx || !node || !out_req) return false;

    if (arena_arr_len(args) < 4) {
        (void)load_cache_emit_diag(
            ctx,
            node,
            EV_DIAG_ERROR,
            nob_sv_from_cstr("load_cache(READ_WITH_PREFIX ...) requires a prefix and at least one entry"),
            nob_sv_from_cstr("Usage: load_cache(<path> READ_WITH_PREFIX <prefix> <entry>...)"));
        return false;
    }

    out_req->mode = LOAD_CACHE_MODE_READ_WITH_PREFIX;
    out_req->prefix = args[2];
    if (!load_cache_push_temp(ctx, &out_req->cache_inputs, args[0])) return false;

    for (size_t i = 3; i < arena_arr_len(args); i++) {
        if (!load_cache_push_temp(ctx, &out_req->requested, args[i])) return false;
    }

    return true;
}

static bool load_cache_parse_legacy_request(EvalExecContext *ctx,
                                            const Node *node,
                                            SV_List args,
                                            Load_Cache_Request *out_req) {
    if (!ctx || !node || !out_req) return false;

    out_req->mode = LOAD_CACHE_MODE_LEGACY;
    if (!load_cache_push_temp(ctx, &out_req->cache_inputs, args[0])) return false;

    size_t i = 1;
    if (!load_cache_collect_until_keyword(ctx, args, &i, &out_req->cache_inputs)) return false;

    while (i < arena_arr_len(args)) {
        if (eval_sv_eq_ci_lit(args[i], "EXCLUDE")) {
            i++;
            if (!load_cache_collect_until_keyword(ctx, args, &i, &out_req->excludes)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "INCLUDE_INTERNALS")) {
            i++;
            if (!load_cache_collect_until_keyword(ctx, args, &i, &out_req->include_internals)) return false;
            continue;
        }

        (void)load_cache_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("load_cache() received an unsupported argument"),
                                   args[i]);
        return false;
    }

    return true;
}

static bool load_cache_parse_request(EvalExecContext *ctx,
                                     const Node *node,
                                     SV_List args,
                                     Load_Cache_Request *out_req) {
    if (!ctx || !node || !out_req) return false;

    if (arena_arr_len(args) < 2) {
        (void)load_cache_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("load_cache() requires a path and a supported mode"),
                                   nob_sv_from_cstr("Usage: load_cache(<path> READ_WITH_PREFIX <prefix> <entry>...)"));
        return false;
    }

    *out_req = (Load_Cache_Request){0};
    if (eval_sv_eq_ci_lit(args[1], "READ_WITH_PREFIX")) {
        return load_cache_parse_read_with_prefix_request(ctx, node, args, out_req);
    }
    return load_cache_parse_legacy_request(ctx, node, args, out_req);
}

static bool load_cache_execute_request(EvalExecContext *ctx,
                                       const Node *node,
                                       const Load_Cache_Request *req) {
    if (!ctx || !node || !req) return false;

    bool read_with_prefix = req->mode == LOAD_CACHE_MODE_READ_WITH_PREFIX;
    for (size_t path_i = 0; path_i < arena_arr_len(req->cache_inputs); path_i++) {
        String_View cache_path = nob_sv_from_cstr("");
        if (!load_cache_resolve_path(ctx, req->cache_inputs[path_i], &cache_path)) return false;
        char *cache_path_c = eval_sv_to_cstr_temp(ctx, cache_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, cache_path_c, false);

        Nob_String_Builder sb = {0};
        if (!nob_read_entire_file(cache_path_c, &sb)) {
            (void)load_cache_emit_diag(ctx,
                                       node,
                                       EV_DIAG_ERROR,
                                       nob_sv_from_cstr("load_cache() failed to read CMakeCache.txt"),
                                       cache_path);
            return false;
        }

        bool ok = load_cache_process_contents(ctx,
                                              node,
                                              read_with_prefix,
                                              req->prefix,
                                              &req->requested,
                                              &req->excludes,
                                              &req->include_internals,
                                              &sb);
        nob_sb_free(sb);
        if (!ok) return false;
    }

    return true;
}

static bool load_cache_parse_line(String_View line,
                                  String_View *out_key,
                                  String_View *out_type,
                                  String_View *out_value) {
    if (!out_key || !out_type || !out_value) return false;
    *out_key = nob_sv_from_cstr("");
    *out_type = nob_sv_from_cstr("");
    *out_value = nob_sv_from_cstr("");
    if (line.count == 0) return false;
    if (line.data[0] == '#' || line.data[0] == '/') return false;

    size_t colon = (size_t)-1;
    size_t eq = (size_t)-1;
    for (size_t i = 0; i < line.count; i++) {
        if (line.data[i] == ':' && colon == (size_t)-1) {
            colon = i;
            continue;
        }
        if (line.data[i] == '=' && colon != (size_t)-1) {
            eq = i;
            break;
        }
    }
    if (colon == (size_t)-1 || eq == (size_t)-1 || colon == 0 || eq <= colon + 1) return false;
    *out_key = nob_sv_from_parts(line.data, colon);
    *out_type = nob_sv_from_parts(line.data + colon + 1, eq - colon - 1);
    *out_value = nob_sv_from_parts(line.data + eq + 1, line.count - eq - 1);
    return out_key->count > 0 && out_type->count > 0;
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

static bool cache_promote_untyped_path_value_if_needed(EvalExecContext *ctx,
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
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static Eval_Cache_Entry *cache_find(EvalExecContext *ctx, String_View key) {
    if (!ctx || !ctx->scope_state.cache_entries) return NULL;
    return stbds_shgetp_null(ctx->scope_state.cache_entries, nob_temp_sv_to_cstr(key));
}

static bool cache_remove(EvalExecContext *ctx, String_View key) {
    if (!ctx || !ctx->scope_state.cache_entries) return true;
    (void)stbds_shdel(ctx->scope_state.cache_entries, nob_temp_sv_to_cstr(key));
    return true;
}

static char *cache_copy_key_cstr(EvalExecContext *ctx, String_View key) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_event_arena(ctx), key.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (key.count > 0) memcpy(buf, key.data, key.count);
    buf[key.count] = '\0';
    return buf;
}

static bool cache_upsert(EvalExecContext *ctx,
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
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    char *stable_key = cache_copy_key_cstr(ctx, key);
    EVAL_OOM_RETURN_IF_NULL(ctx, stable_key, false);
    Eval_Cache_Value cv = {0};
    cv.data = sv_copy_to_event_arena(ctx, value);
    cv.type = sv_copy_to_event_arena(ctx, type);
    cv.doc = sv_copy_to_event_arena(ctx, doc);
    if (eval_should_stop(ctx)) return false;

    Eval_Cache_Entry *entries = ctx->scope_state.cache_entries;
    stbds_shput(entries, stable_key, cv);
    ctx->scope_state.cache_entries = entries;
    return true;
}

static bool visible_scope_has_normal_binding(EvalExecContext *ctx, String_View key) {
    if (eval_scope_visible_depth(ctx) == 0 || key.count == 0) return false;
    for (size_t depth = eval_scope_visible_depth(ctx); depth-- > 0;) {
        Var_Scope *scope = &ctx->scope_state.scopes[depth];
        if (!scope->vars) continue;
        if (stbds_shgetp_null(scope->vars, nob_temp_sv_to_cstr(key)) != NULL) return true;
    }
    return false;
}

static bool unset_visible_normal_binding(EvalExecContext *ctx, String_View key) {
    if (eval_scope_visible_depth(ctx) == 0 || key.count == 0) return false;
    for (size_t depth = eval_scope_visible_depth(ctx); depth-- > 0;) {
        Var_Scope *scope = &ctx->scope_state.scopes[depth];
        if (!scope->vars) continue;
        if (stbds_shgetp_null(scope->vars, nob_temp_sv_to_cstr(key)) == NULL) continue;
        (void)stbds_shdel(scope->vars, nob_temp_sv_to_cstr(key));
        return true;
    }
    return true;
}

static bool emit_cache_entry_write(EvalExecContext *ctx,
                                   Cmake_Event_Origin origin,
                                   String_View key,
                                   String_View value) {
    return eval_emit_var_set_cache(ctx, origin, key, value);
}

static bool option_cache_write(EvalExecContext *ctx,
                               Cmake_Event_Origin origin,
                               String_View key,
                               String_View value,
                               String_View doc) {
    if (!cache_upsert(ctx, key, value, nob_sv_from_cstr("BOOL"), doc)) return false;
    return emit_cache_entry_write(ctx, origin, key, value);
}

static bool mark_as_advanced_apply(EvalExecContext *ctx,
                                   String_View var_name,
                                   bool clear_mode) {
    if (!ctx || var_name.count == 0) return false;
    return eval_property_write(ctx,
                               (Event_Origin){0},
                               nob_sv_from_cstr("CACHE"),
                               var_name,
                               nob_sv_from_cstr("ADVANCED"),
                               clear_mode ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"),
                               EV_PROP_SET,
                               false);
}

typedef enum {
    SET_REQUEST_NOOP = 0,
    SET_REQUEST_ENV,
    SET_REQUEST_CACHE,
    SET_REQUEST_NORMAL,
} Set_Request_Kind;

typedef struct {
    Set_Request_Kind kind;
    String_View var;
    String_View env_name;
    String_View env_value;
    bool warn_env_extra_args;
    String_View cache_type_raw;
    String_View cache_type;
    String_View cache_doc;
    String_View cache_value;
    String_View value;
    bool cache_force;
    bool cache_internal;
    bool parent_scope;
    bool unset_current;
} Set_Request;

typedef enum {
    UNSET_REQUEST_ENV = 0,
    UNSET_REQUEST_CACHE,
    UNSET_REQUEST_PARENT_SCOPE,
    UNSET_REQUEST_CURRENT,
} Unset_Request_Kind;

typedef struct {
    Unset_Request_Kind kind;
    String_View var;
    String_View env_name;
} Unset_Request;

typedef struct {
    String_View var;
    String_View doc;
    String_View value;
} Option_Request;

typedef struct {
    bool clear_mode;
    SV_List vars;
} Mark_As_Advanced_Request;

static bool set_parse_request(EvalExecContext *ctx,
                              const Node *node,
                              SV_List args,
                              Set_Request *out_req) {
    if (!ctx || !node || !out_req) return false;

    *out_req = (Set_Request){0};
    if (arena_arr_len(args) == 0) {
        out_req->kind = SET_REQUEST_NOOP;
        return true;
    }

    out_req->var = args[0];
    if (parse_env_var_name(out_req->var, &out_req->env_name)) {
        out_req->kind = SET_REQUEST_ENV;
        out_req->env_value = (arena_arr_len(args) >= 2) ? args[1] : nob_sv_from_cstr("");
        out_req->warn_env_extra_args = arena_arr_len(args) > 2;
        return true;
    }

    size_t cache_idx = arena_arr_len(args);
    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "CACHE")) {
            cache_idx = i;
            break;
        }
    }

    if (cache_idx < arena_arr_len(args)) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        out_req->kind = SET_REQUEST_CACHE;
        if (cache_idx + 2 >= arena_arr_len(args)) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "set", nob_sv_from_cstr("set(... CACHE ...) requires <type> and <docstring>"), nob_sv_from_cstr("Usage: set(<var> <value>... CACHE <type> <doc> [FORCE])"));
            return false;
        }

        out_req->cache_type_raw = args[cache_idx + 1];
        if (!parse_cache_type(out_req->cache_type_raw, &out_req->cache_type, &out_req->cache_internal)) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "set", nob_sv_from_cstr("set(... CACHE ...) received invalid cache type"), out_req->cache_type_raw);
            return false;
        }

        out_req->cache_doc = args[cache_idx + 2];
        if (cache_idx + 3 < arena_arr_len(args)) {
            if (cache_idx + 3 == arena_arr_len(args) - 1 &&
                eval_sv_eq_ci_lit(args[cache_idx + 3], "FORCE")) {
                out_req->cache_force = true;
            } else {
                (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "set", nob_sv_from_cstr("set(... CACHE ...) received unsupported trailing arguments"), nob_sv_from_cstr("Only optional FORCE is accepted after <docstring>"));
                return false;
            }
        }
        if (out_req->cache_internal) out_req->cache_force = true;

        if (cache_idx > 1) {
            out_req->cache_value = eval_sv_join_semi_temp(ctx, &args[1], cache_idx - 1);
            if (eval_should_stop(ctx)) return false;
        }
        return true;
    }

    out_req->kind = SET_REQUEST_NORMAL;
    if (arena_arr_len(args) == 1) {
        out_req->unset_current = true;
        return true;
    }

    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "PARENT_SCOPE")) {
            out_req->parent_scope = true;
            break;
        }
    }

    size_t value_end = arena_arr_len(args);
    if (out_req->parent_scope) value_end--;
    if (value_end > 1) {
        out_req->value = eval_sv_join_semi_temp(ctx, &args[1], value_end - 1);
        if (eval_should_stop(ctx)) return false;
    }

    return true;
}

static bool set_execute_env_request(EvalExecContext *ctx,
                                    const Node *node,
                                    const Set_Request *req) {
    if (!ctx || !node || !req) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (req->warn_env_extra_args) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_LEGACY_WARNING, "set", nob_sv_from_cstr("set(ENV{...}) ignores extra arguments after value"), nob_sv_from_cstr("Only the first value argument is used"));
    }

    if (!eval_process_env_set(ctx, req->env_name, req->env_value)) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "set", nob_sv_from_cstr("Failed to set environment variable"), req->env_name);
        return false;
    }

    return true;
}

static bool set_execute_cache_request(EvalExecContext *ctx,
                                      const Node *node,
                                      const Set_Request *req) {
    if (!ctx || !node || !req) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    Eval_Cache_Entry *existing = cache_find(ctx, req->var);
    bool should_write_cache = (existing == NULL) || req->cache_force;
    bool cmp0126_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0126);
    bool remove_local_binding_old = (existing == NULL) ||
                                    (existing && existing->value.type.count == 0) ||
                                    req->cache_force ||
                                    req->cache_internal;

    if (!cmp0126_new && remove_local_binding_old) (void)eval_var_unset_current(ctx, req->var);

    if (should_write_cache) {
        if (!cache_upsert(ctx, req->var, req->cache_value, req->cache_type, req->cache_doc)) return false;
        if (!eval_emit_var_set_cache(ctx, o, req->var, req->cache_value)) return false;
    } else if (existing->value.type.count == 0) {
        if (!cache_promote_untyped_path_value_if_needed(ctx, existing, req->cache_type)) return false;
        existing->value.type = sv_copy_to_event_arena(ctx, req->cache_type);
        existing->value.doc = sv_copy_to_event_arena(ctx, req->cache_doc);
    }

    return true;
}

static bool set_execute_normal_request(EvalExecContext *ctx,
                                       const Node *node,
                                       const Set_Request *req) {
    if (!ctx || !node || !req) return false;

    if (req->unset_current) return eval_var_unset_current(ctx, req->var);

    if (req->parent_scope) {
        if (eval_scope_visible_depth(ctx) > 1) {
            size_t saved_depth = 0;
            if (!eval_scope_use_parent_view(ctx, &saved_depth)) return false;
            bool ok = eval_var_set_current(ctx, req->var, req->value);
            eval_scope_restore_view(ctx, saved_depth);
            return ok;
        }

        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        (void)EVAL_NODE_ORIGIN_DIAG_BOOL_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "set", nob_sv_from_cstr("PARENT_SCOPE used without a parent scope"), nob_sv_from_cstr("Use PARENT_SCOPE only inside function/subscope"));
        return false;
    }

    (void)eval_var_set_current(ctx, req->var, req->value);
    return true;
}

static bool set_execute_request(EvalExecContext *ctx,
                                const Node *node,
                                const Set_Request *req) {
    if (!ctx || !node || !req) return false;

    switch (req->kind) {
        case SET_REQUEST_NOOP:
            return true;
        case SET_REQUEST_ENV:
            return set_execute_env_request(ctx, node, req);
        case SET_REQUEST_CACHE:
            return set_execute_cache_request(ctx, node, req);
        case SET_REQUEST_NORMAL:
            return set_execute_normal_request(ctx, node, req);
        default:
            break;
    }

    return false;
}

static bool unset_parse_request(EvalExecContext *ctx,
                                const Node *node,
                                SV_List args,
                                Unset_Request *out_req) {
    if (!ctx || !node || !out_req) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    *out_req = (Unset_Request){0};

    if (arena_arr_len(args) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "unset", nob_sv_from_cstr("unset() requires variable name"), nob_sv_from_cstr("Usage: unset(<var> [CACHE|PARENT_SCOPE])"));
        return false;
    }
    if (arena_arr_len(args) > 2) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "unset", nob_sv_from_cstr("unset() accepts at most one option"), nob_sv_from_cstr("Supported options: CACHE, PARENT_SCOPE"));
        return false;
    }

    out_req->var = args[0];
    if (parse_env_var_name(out_req->var, &out_req->env_name)) {
        out_req->kind = UNSET_REQUEST_ENV;
        if (arena_arr_len(args) > 1) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "unset", nob_sv_from_cstr("unset(ENV{...}) does not accept options"), nob_sv_from_cstr("Usage: unset(ENV{<var>})"));
            return false;
        }
        return true;
    }

    out_req->kind = UNSET_REQUEST_CURRENT;
    if (arena_arr_len(args) == 1) return true;

    if (eval_sv_eq_ci_lit(args[1], "CACHE")) {
        out_req->kind = UNSET_REQUEST_CACHE;
        return true;
    }
    if (eval_sv_eq_ci_lit(args[1], "PARENT_SCOPE")) {
        out_req->kind = UNSET_REQUEST_PARENT_SCOPE;
        return true;
    }

    (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "unset", nob_sv_from_cstr("unset() received unsupported option"), args[1]);
    return false;
}

static bool unset_execute_request(EvalExecContext *ctx,
                                  const Node *node,
                                  const Unset_Request *req) {
    if (!ctx || !node || !req) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    switch (req->kind) {
        case UNSET_REQUEST_ENV:
            if (!eval_process_env_unset(ctx, req->env_name)) {
                (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "unset", nob_sv_from_cstr("Failed to unset environment variable"), req->env_name);
                return false;
            }
            return true;
        case UNSET_REQUEST_CACHE:
            (void)cache_remove(ctx, req->var);
            return eval_emit_var_unset_cache(ctx, o, req->var);
        case UNSET_REQUEST_PARENT_SCOPE: {
            if (eval_scope_visible_depth(ctx) <= 1) {
                (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "unset", nob_sv_from_cstr("PARENT_SCOPE used without a parent scope"), nob_sv_from_cstr("Use PARENT_SCOPE only inside function/subscope"));
                return false;
            }

            size_t saved_depth = 0;
            if (!eval_scope_use_parent_view(ctx, &saved_depth)) return false;
            bool ok = eval_var_unset_current(ctx, req->var);
            eval_scope_restore_view(ctx, saved_depth);
            return ok;
        }
        case UNSET_REQUEST_CURRENT:
            return eval_var_unset_current(ctx, req->var);
        default:
            break;
    }

    return false;
}

static bool option_parse_request(EvalExecContext *ctx,
                                 const Node *node,
                                 SV_List args,
                                 Option_Request *out_req) {
    if (!ctx || !node || !out_req) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 2 || arena_arr_len(args) > 3) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "option", nob_sv_from_cstr("option() requires <variable> <help_text> [value]"), nob_sv_from_cstr("Usage: option(<variable> <help_text> [value])"));
        return false;
    }

    *out_req = (Option_Request){
        .var = args[0],
        .doc = args[1],
        .value = (arena_arr_len(args) >= 3) ? args[2] : nob_sv_from_cstr("OFF"),
    };

    if (out_req->var.count == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "option", nob_sv_from_cstr("option() requires a non-empty variable name"), nob_sv_from_cstr("Provide a cache variable identifier"));
        return false;
    }

    return true;
}

static bool option_execute_request(EvalExecContext *ctx,
                                   const Node *node,
                                   const Option_Request *req) {
    if (!ctx || !node || !req) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    bool cmp0077_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0077);
    bool has_normal_binding = visible_scope_has_normal_binding(ctx, req->var);
    Eval_Cache_Entry *existing = cache_find(ctx, req->var);
    bool has_typed_cache = existing && existing->value.type.count > 0;

    if (cmp0077_new && has_normal_binding) return true;

    if (!cmp0077_new && has_normal_binding) {
        if (!unset_visible_normal_binding(ctx, req->var)) return false;
    }

    if (has_typed_cache) return true;
    return option_cache_write(ctx, o, req->var, req->value, req->doc);
}

static bool mark_as_advanced_parse_request(EvalExecContext *ctx,
                                           const Node *node,
                                           SV_List args,
                                           Mark_As_Advanced_Request *out_req) {
    if (!ctx || !node || !out_req) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    *out_req = (Mark_As_Advanced_Request){0};

    size_t start = 0;
    if (arena_arr_len(args) > 0 && eval_sv_eq_ci_lit(args[0], "CLEAR")) {
        out_req->clear_mode = true;
        start = 1;
    } else if (arena_arr_len(args) > 0 && eval_sv_eq_ci_lit(args[0], "FORCE")) {
        start = 1;
    }

    if (start >= arena_arr_len(args)) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "mark_as_advanced", nob_sv_from_cstr("mark_as_advanced() requires at least one variable name"), nob_sv_from_cstr("Usage: mark_as_advanced([CLEAR|FORCE] <var>...)"));
        return false;
    }

    for (size_t i = start; i < arena_arr_len(args); i++) {
        if (!eval_sv_arr_push_temp(ctx, &out_req->vars, args[i])) return false;
    }

    return true;
}

static bool mark_as_advanced_execute_request(EvalExecContext *ctx,
                                             const Node *node,
                                             const Mark_As_Advanced_Request *req) {
    if (!ctx || !node || !req) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    bool cmp0102_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0102);
    for (size_t i = 0; i < arena_arr_len(req->vars); i++) {
        String_View var_name = req->vars[i];
        if (var_name.count == 0) continue;

        if (!eval_cache_defined(ctx, var_name)) {
            if (cmp0102_new) continue;
            if (!cache_upsert(ctx,
                              var_name,
                              nob_sv_from_cstr(""),
                              nob_sv_from_cstr("UNINITIALIZED"),
                              nob_sv_from_cstr(""))) {
                return false;
            }
            if (!emit_cache_entry_write(ctx, o, var_name, nob_sv_from_cstr(""))) return false;
        }

        if (!mark_as_advanced_apply(ctx, var_name, req->clear_mode)) return false;
    }

    return true;
}

Eval_Result eval_handle_set(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Set_Request req = {0};
    if (!set_parse_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!set_execute_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_unset(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Unset_Request req = {0};
    if (!unset_parse_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!unset_execute_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_option(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Option_Request req = {0};
    if (!option_parse_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!option_execute_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_mark_as_advanced(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Mark_As_Advanced_Request req = {0};
    if (!mark_as_advanced_parse_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!mark_as_advanced_execute_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_load_cache(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    Load_Cache_Request req = {0};
    if (!load_cache_parse_request(ctx, node, a, &req)) return eval_result_from_ctx(ctx);
    if (!load_cache_execute_request(ctx, node, &req)) return eval_result_from_ctx(ctx);

    return eval_result_from_ctx(ctx);
}
