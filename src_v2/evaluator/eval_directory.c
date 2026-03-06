#include "eval_directory.h"

#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <string.h>

static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";
static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_link_opts_var = "NOBIFY_GLOBAL_LINK_OPTIONS";

static bool sv_eq_exact(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

static bool sv_list_contains_exact(const SV_List *list, String_View item) {
    if (!list) return false;
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        if (sv_eq_exact((*list)[i], item)) return true;
    }
    return false;
}

static bool semicolon_list_contains_exact(String_View list, String_View item) {
    if (list.count == 0) return false;
    const char *p = list.data;
    const char *end = list.data + list.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View cur = nob_sv_from_parts(p, (size_t)(q - p));
        if (sv_eq_exact(cur, item)) return true;
        if (q >= end) break;
        p = q + 1;
    }
    return false;
}

static bool append_list_var_unique(Evaluator_Context *ctx, String_View var, String_View item, bool *out_added) {
    if (out_added) *out_added = false;
    String_View current = eval_var_get_visible(ctx, var);
    if (current.count == 0) {
        if (!eval_var_set_current(ctx, var, item)) return false;
        if (out_added) *out_added = true;
        return true;
    }
    if (semicolon_list_contains_exact(current, item)) {
        return true;
    }

    size_t total = current.count + 1 + item.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    memcpy(buf, current.data, current.count);
    buf[current.count] = ';';
    memcpy(buf + current.count + 1, item.data, item.count);
    buf[total] = '\0';
    if (!eval_var_set_current(ctx, var, nob_sv_from_cstr(buf))) return false;
    if (out_added) *out_added = true;
    return true;
}

static bool remove_list_var_exact(Evaluator_Context *ctx, String_View var, String_View item, bool *out_removed) {
    if (out_removed) *out_removed = false;
    if (!ctx || item.count == 0) return false;

    String_View current = eval_var_get_visible(ctx, var);
    if (current.count == 0) return true;

    size_t kept_count = 0;
    size_t kept_total = 0;
    size_t removed_count = 0;
    const char *p = current.data;
    const char *end = current.data + current.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View cur = nob_sv_from_parts(p, (size_t)(q - p));
        if (sv_eq_exact(cur, item)) {
            removed_count++;
        } else {
            kept_total += cur.count;
            kept_count++;
        }
        if (q >= end) break;
        p = q + 1;
    }

    if (removed_count == 0) return true;

    if (out_removed) *out_removed = true;
    if (kept_count == 0) return eval_var_set_current(ctx, var, nob_sv_from_cstr(""));

    size_t total = kept_total + (kept_count - 1);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t off = 0;
    size_t written = 0;
    p = current.data;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View cur = nob_sv_from_parts(p, (size_t)(q - p));
        if (!sv_eq_exact(cur, item)) {
            if (written > 0) buf[off++] = ';';
            if (cur.count > 0) {
                memcpy(buf + off, cur.data, cur.count);
                off += cur.count;
            }
            written++;
        }
        if (q >= end) break;
        p = q + 1;
    }

    buf[off] = '\0';
    return eval_var_set_current(ctx, var, nob_sv_from_parts(buf, off));
}

static bool split_definition_flag(String_View item, String_View *out_definition) {
    if (!out_definition) return false;
    *out_definition = nob_sv_from_cstr("");
    if (item.count < 2 || !item.data) return false;
    bool is_dash_d = item.data[0] == '-' && (item.data[1] == 'D' || item.data[1] == 'd');
    bool is_slash_d = item.data[0] == '/' && (item.data[1] == 'D' || item.data[1] == 'd');
    if (!is_dash_d && !is_slash_d) return false;
    *out_definition = nob_sv_from_parts(item.data + 2, item.count - 2);
    return true;
}

static String_View join_directory_property_items_temp(Evaluator_Context *ctx,
                                                      const SV_List *items,
                                                      Event_Property_Mutate_Op op) {
    if (!ctx || !items) return nob_sv_from_cstr("");
    size_t count = arena_arr_len(*items);
    if (count == 0) return nob_sv_from_cstr("");
    if (op == EVENT_PROPERTY_MUTATE_APPEND_STRING) return svu_join_no_sep_temp(ctx, *items, count);
    return eval_sv_join_semi_temp(ctx, *items, count);
}

static String_View directory_property_store_key_temp(Evaluator_Context *ctx, String_View property_name) {
    static const char prefix[] = "NOBIFY_PROPERTY_DIRECTORY::";
    if (!ctx) return nob_sv_from_cstr("");

    String_View current_dir = eval_current_source_dir_for_paths(ctx);
    size_t total = (sizeof(prefix) - 1) + current_dir.count + 2 + property_name.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, sizeof(prefix) - 1);
    off += sizeof(prefix) - 1;
    memcpy(buf + off, current_dir.data, current_dir.count);
    off += current_dir.count;
    buf[off++] = ':';
    buf[off++] = ':';
    memcpy(buf + off, property_name.data, property_name.count);
    off += property_name.count;
    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static String_View merge_directory_property_value_temp(Evaluator_Context *ctx,
                                                       String_View current,
                                                       const SV_List *items,
                                                       Event_Property_Mutate_Op op) {
    String_View incoming = join_directory_property_items_temp(ctx, items, op);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (op == EVENT_PROPERTY_MUTATE_SET) return incoming;
    if (incoming.count == 0) return current;
    if (current.count == 0) return incoming;

    bool incoming_first = (op == EVENT_PROPERTY_MUTATE_PREPEND_LIST);
    bool with_semicolon =
        (op == EVENT_PROPERTY_MUTATE_APPEND_LIST || op == EVENT_PROPERTY_MUTATE_PREPEND_LIST);
    size_t total = current.count + incoming.count + (with_semicolon ? 1 : 0);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    if (incoming_first) {
        memcpy(buf + off, incoming.data, incoming.count);
        off += incoming.count;
        if (with_semicolon) buf[off++] = ';';
        memcpy(buf + off, current.data, current.count);
        off += current.count;
    } else {
        memcpy(buf + off, current.data, current.count);
        off += current.count;
        if (with_semicolon) buf[off++] = ';';
        memcpy(buf + off, incoming.data, incoming.count);
        off += incoming.count;
    }
    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static bool sync_directory_property_mutation(Evaluator_Context *ctx,
                                             Event_Origin origin,
                                             const char *property_name,
                                             Event_Property_Mutate_Op op,
                                             uint32_t modifier_flags,
                                             const SV_List *items) {
    if (!ctx) return false;

    size_t item_count = items ? arena_arr_len(*items) : 0;
    if (op != EVENT_PROPERTY_MUTATE_SET && item_count == 0) return true;

    String_View property_sv = nob_sv_from_cstr(property_name);
    String_View store_key = directory_property_store_key_temp(ctx, property_sv);
    if (eval_should_stop(ctx)) return false;

    bool current_defined = eval_var_defined_visible(ctx, store_key);
    String_View current = current_defined ? eval_var_get_visible(ctx, store_key) : nob_sv_from_cstr("");
    String_View merged = merge_directory_property_value_temp(ctx, current, items, op);
    if (eval_should_stop(ctx)) return false;

    if (!current_defined && op == EVENT_PROPERTY_MUTATE_SET && merged.count == 0) return true;
    if (current_defined && nob_sv_eq(current, merged)) return true;

    if (!eval_var_set_current(ctx, store_key, merged)) return false;
    return eval_emit_directory_property_mutate(ctx,
                                               origin,
                                               property_sv,
                                               op,
                                               modifier_flags,
                                               items ? *items : NULL,
                                               item_count);
}

static size_t gfc_last_separator_index(String_View path) {
    for (size_t i = path.count; i > 0; i--) {
        if (svu_is_path_sep(path.data[i - 1])) return i - 1;
    }
    return SIZE_MAX;
}

static String_View gfc_filename_sv(String_View path) {
    size_t sep = gfc_last_separator_index(path);
    if (sep == SIZE_MAX) return path;
    return nob_sv_from_parts(path.data + sep + 1, path.count - sep - 1);
}

static String_View gfc_directory_sv(String_View path) {
    size_t sep = gfc_last_separator_index(path);
    if (sep == SIZE_MAX) return nob_sv_from_cstr("");
    if (sep == 0) return nob_sv_from_cstr("/");
    if (sep == 2 && path.count >= 3 && path.data[1] == ':') {
        return nob_sv_from_parts(path.data, 3);
    }
    return nob_sv_from_parts(path.data, sep);
}

static ssize_t gfc_dot_index(String_View name, bool last_only) {
    if (name.count == 0) return -1;
    ssize_t dot = -1;

    if (last_only) {
        for (size_t i = name.count; i > 0; i--) {
            if (name.data[i - 1] == '.') {
                dot = (ssize_t)(i - 1);
                break;
            }
        }
    } else {
        for (size_t i = 0; i < name.count; i++) {
            if (name.data[i] == '.') {
                dot = (ssize_t)i;
                break;
            }
        }
    }

    if (dot <= 0) return -1;
    return dot;
}

static String_View gfc_extension_from_name(String_View name, bool last_only) {
    ssize_t dot = gfc_dot_index(name, last_only);
    if (dot < 0) return nob_sv_from_cstr("");
    return nob_sv_from_parts(name.data + dot, name.count - (size_t)dot);
}

static String_View gfc_stem_from_name(String_View name, bool last_only) {
    ssize_t dot = gfc_dot_index(name, last_only);
    if (dot < 0) return name;
    return nob_sv_from_parts(name.data, (size_t)dot);
}

static bool gfc_contains_path_sep(String_View value) {
    if (value.count == 0) return false;
    for (size_t i = 0; i < value.count; i++) {
        if (svu_is_path_sep(value.data[i])) return true;
    }
    return false;
}

static bool gfc_candidate_is_file(Evaluator_Context *ctx, String_View candidate) {
    if (!ctx || candidate.count == 0) return false;
    char *path_c = eval_sv_to_cstr_temp(ctx, candidate);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    if (!nob_file_exists(path_c)) return false;
    Nob_File_Type kind = nob_get_file_type(path_c);
    return kind == NOB_FILE_REGULAR || kind == NOB_FILE_SYMLINK;
}

static bool gfc_resolve_program_full_path(Evaluator_Context *ctx,
                                          String_View token,
                                          String_View *out_program) {
    if (!ctx || !out_program) return false;
    *out_program = token;
    if (token.count == 0) return true;

    String_View current_src = eval_current_source_dir(ctx);

    if (eval_sv_is_abs_path(token) || gfc_contains_path_sep(token)) {
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, token, current_src, false);
        if (eval_should_stop(ctx)) return false;
        *out_program = resolved;
        return true;
    }

    const char *path_env = eval_getenv_temp(ctx, "PATH");
    if (!path_env || path_env[0] == '\0') return true;

    String_View raw_path = nob_sv_from_cstr(path_env);
    const char path_sep =
#if defined(_WIN32)
        ';';
#else
        ':';
#endif

#if defined(_WIN32)
    static const char *const k_exts[] = {"", ".exe", ".cmd", ".bat", ".com"};
#else
    static const char *const k_exts[] = {""};
#endif
    const char *const *exts = k_exts;
    const size_t ext_count =
#if defined(_WIN32)
        NOB_ARRAY_LEN(k_exts);
#else
        NOB_ARRAY_LEN(k_exts);
#endif

    const char *p = raw_path.data;
    const char *end = raw_path.data + raw_path.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != path_sep) q++;
        String_View dir = nob_sv_from_parts(p, (size_t)(q - p));
        if (dir.count > 0) {
            for (size_t ei = 0; ei < ext_count; ei++) {
                String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dir, token);
                if (eval_should_stop(ctx)) return false;
                if (exts[ei][0] != '\0') {
                    candidate = svu_concat_suffix_temp(ctx, candidate, exts[ei]);
                    if (eval_should_stop(ctx)) return false;
                }
                if (!gfc_candidate_is_file(ctx, candidate)) continue;
                *out_program = eval_sv_path_normalize_temp(ctx, candidate);
                return !eval_result_is_fatal(eval_result_from_ctx(ctx));
            }
        }
        if (q >= end) break;
        p = q + 1;
    }

    return true;
}

static bool gfc_set_output_value(Evaluator_Context *ctx,
                                 Cmake_Event_Origin origin,
                                 String_View out_var,
                                 String_View value,
                                 bool cache_result) {
    if (!ctx) return false;
    if (!cache_result) return eval_var_set_current(ctx, out_var, value);
    if (!eval_cache_set(ctx, out_var, value, nob_sv_from_cstr(""), nob_sv_from_cstr(""))) return false;
    return eval_emit_var_set_cache(ctx, origin, out_var, value);
}

static bool emit_compile_definition_to_current_file_targets(Evaluator_Context *ctx,
                                                            Cmake_Event_Origin origin,
                                                            String_View item) {
    if (!ctx || item.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(ctx->command_state.known_targets); i++) {
        String_View target_name = ctx->command_state.known_targets[i];
        if (target_name.count == 0) continue;
        if (eval_target_alias_known(ctx, target_name)) continue;
        if (!eval_emit_target_compile_definitions(ctx,
                                                  origin,
                                                  target_name,
                                                  EV_VISIBILITY_PRIVATE,
                                                  item)) {
            return false;
        }
    }
    return true;
}

static bool emit_compile_option_to_current_file_targets(Evaluator_Context *ctx,
                                                        Cmake_Event_Origin origin,
                                                        String_View item) {
    if (!ctx || item.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(ctx->command_state.known_targets); i++) {
        String_View target_name = ctx->command_state.known_targets[i];
        if (target_name.count == 0) continue;
        if (eval_target_alias_known(ctx, target_name)) continue;
        if (!eval_emit_target_compile_options(ctx,
                                              origin,
                                              target_name,
                                              EV_VISIBILITY_PRIVATE,
                                              item,
                                              false)) {
            return false;
        }
    }
    return true;
}

static String_View wrap_link_item_with_config_genex_temp(Evaluator_Context *ctx,
                                                         String_View item,
                                                         String_View cond_prefix) {
    if (!ctx || item.count == 0 || cond_prefix.count == 0) return item;
    String_View parts[3] = {
        cond_prefix,
        item,
        nob_sv_from_cstr(">")
    };
    return svu_join_no_sep_temp(ctx, parts, 3);
}

static bool split_comma_list_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;
    if (input.count == 0) return true;

    size_t start = 0;
    for (size_t i = 0; i <= input.count; i++) {
        if (i < input.count && input.data[i] != ',') continue;
        String_View part = nob_sv_from_parts(input.data + start, i - start);
        while (part.count > 0 && isspace((unsigned char)part.data[0])) {
            part = nob_sv_from_parts(part.data + 1, part.count - 1);
        }
        while (part.count > 0 && isspace((unsigned char)part.data[part.count - 1])) {
            part = nob_sv_from_parts(part.data, part.count - 1);
        }
        if (part.count > 0) {
            if (!svu_list_push_temp(ctx, out, part)) return false;
        }
        start = i + 1;
    }
    return true;
}

static bool expand_compile_option_token(Evaluator_Context *ctx, String_View tok, SV_List *out) {
    if (!ctx || !out) return false;
    if (tok.count == 0) return true;

    if (svu_has_prefix_ci_lit(tok, "SHELL:")) {
        String_View payload = (tok.count > 6) ? nob_sv_from_parts(tok.data + 6, tok.count - 6) : nob_sv_from_cstr("");
        return eval_split_shell_like_temp(ctx, payload, out);
    }
    return svu_list_push_temp(ctx, out, tok);
}

static bool expand_link_option_token(Evaluator_Context *ctx, String_View tok, SV_List *out) {
    if (!ctx || !out) return false;
    if (tok.count == 0) return true;

    if (svu_has_prefix_ci_lit(tok, "SHELL:")) {
        String_View payload = (tok.count > 6) ? nob_sv_from_parts(tok.data + 6, tok.count - 6) : nob_sv_from_cstr("");
        return eval_split_shell_like_temp(ctx, payload, out);
    }

    if (svu_has_prefix_ci_lit(tok, "LINKER:")) {
        String_View payload = (tok.count > 7) ? nob_sv_from_parts(tok.data + 7, tok.count - 7) : nob_sv_from_cstr("");
        SV_List linker_parts = {0};
        if (svu_has_prefix_ci_lit(payload, "SHELL:")) {
            String_View shell_payload = (payload.count > 6)
                ? nob_sv_from_parts(payload.data + 6, payload.count - 6)
                : nob_sv_from_cstr("");
            if (!eval_split_shell_like_temp(ctx, shell_payload, &linker_parts)) return false;
        } else {
            if (!split_comma_list_temp(ctx, payload, &linker_parts)) return false;
        }

        for (size_t i = 0; i < arena_arr_len(linker_parts); i++) {
            String_View piece = linker_parts[i];
            size_t total = 7 + piece.count;
            char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
            memcpy(buf, "LINKER:", 7);
            if (piece.count > 0) memcpy(buf + 7, piece.data, piece.count);
            buf[total] = '\0';
            if (!svu_list_push_temp(ctx, out, nob_sv_from_cstr(buf))) return false;
        }
        return true;
    }

    return svu_list_push_temp(ctx, out, tok);
}

Eval_Result eval_handle_add_compile_options(Evaluator_Context *ctx, const Node *node) {
    Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    SV_List expanded = {0};
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (!expand_compile_option_token(ctx, a[i], &expanded)) return eval_result_from_ctx(ctx);
    }

    SV_List unique = {0};
    for (size_t i = 0; i < arena_arr_len(expanded); i++) {
        if (expanded[i].count == 0) continue;
        if (sv_list_contains_exact(&unique, expanded[i])) continue;
        if (!svu_list_push_temp(ctx, &unique, expanded[i])) return eval_result_from_ctx(ctx);
    }

    SV_List added_items = {0};
    for (size_t i = 0; i < arena_arr_len(unique); i++) {
        bool added = false;
        if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_opts_var), unique[i], &added)) return eval_result_fatal();
        if (!added) continue;
        if (!svu_list_push_temp(ctx, &added_items, unique[i])) return eval_result_from_ctx(ctx);
    }
    if (!sync_directory_property_mutation(ctx,
                                          o,
                                          "COMPILE_OPTIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &added_items)) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_compile_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    SV_List unique = {0};
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        String_View item = eval_normalize_compile_definition_item(a[i]);
        if (item.count == 0) continue;
        if (sv_list_contains_exact(&unique, item)) continue;
        if (!svu_list_push_temp(ctx, &unique, item)) return eval_result_from_ctx(ctx);
    }

    SV_List added_items = {0};
    for (size_t i = 0; i < arena_arr_len(unique); i++) {
        bool added = false;
        if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_defs_var), unique[i], &added)) return eval_result_fatal();
        if (!added) continue;
        if (!svu_list_push_temp(ctx, &added_items, unique[i])) return eval_result_from_ctx(ctx);
        if (!emit_compile_definition_to_current_file_targets(ctx, o, unique[i])) return eval_result_fatal();
    }
    if (!sync_directory_property_mutation(ctx,
                                          o,
                                          "COMPILE_DEFINITIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &added_items)) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    SV_List emitted_defs = {0};
    SV_List emitted_opts = {0};

    for (size_t i = 0; i < arena_arr_len(a); i++) {
        String_View item = a[i];
        if (item.count == 0) continue;

        String_View definition = nob_sv_from_cstr("");
        bool looks_like_definition = split_definition_flag(item, &definition);
        if (looks_like_definition && definition.count > 0) {
            bool added = false;
            if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_defs_var), definition, &added)) return eval_result_fatal();
            if (!added) continue;
            if (!svu_list_push_temp(ctx, &emitted_defs, definition)) return eval_result_from_ctx(ctx);
            if (!emit_compile_definition_to_current_file_targets(ctx, o, definition)) return eval_result_fatal();
            continue;
        }

        bool added = false;
        if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_opts_var), item, &added)) return eval_result_fatal();
        if (!added) continue;
        if (!svu_list_push_temp(ctx, &emitted_opts, item)) return eval_result_from_ctx(ctx);
        if (!emit_compile_option_to_current_file_targets(ctx, o, item)) return eval_result_fatal();
    }

    if (!sync_directory_property_mutation(ctx,
                                          o,
                                          "COMPILE_DEFINITIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &emitted_defs)) {
        return eval_result_fatal();
    }
    if (!sync_directory_property_mutation(ctx,
                                          o,
                                          "COMPILE_OPTIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &emitted_opts)) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_remove_definitions(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    bool removed_any = false;
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        String_View definition = nob_sv_from_cstr("");
        if (!split_definition_flag(a[i], &definition)) continue;
        if (definition.count == 0) continue;
        bool removed = false;
        if (!remove_list_var_exact(ctx, nob_sv_from_cstr(k_global_defs_var), definition, &removed)) {
            return eval_result_from_ctx(ctx);
        }
        removed_any = removed_any || removed;
    }

    if (removed_any) {
        String_View current_defs = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_defs_var));
        SV_List remaining = {0};
        if (current_defs.count > 0 &&
            !eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), current_defs, &remaining)) {
            return eval_result_fatal();
        }
        if (!sync_directory_property_mutation(ctx,
                                              o,
                                              "COMPILE_DEFINITIONS",
                                              EVENT_PROPERTY_MUTATE_SET,
                                              EVENT_PROPERTY_MODIFIER_NONE,
                                              &remaining)) {
            return eval_result_fatal();
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_include_regular_expression(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 1 || arena_arr_len(a) > 2) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "include_regular_expression", nob_sv_from_cstr("include_regular_expression() requires one or two regex arguments"), nob_sv_from_cstr("Usage: include_regular_expression(<regex_match> [<regex_complain>])"));
        return eval_result_from_ctx(ctx);
    }

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION"), a[0])) {
        return eval_result_from_ctx(ctx);
    }
    if (arena_arr_len(a) == 2) {
        if (!eval_var_set_current(ctx,
                          nob_sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN"),
                          a[1])) {
            return eval_result_from_ctx(ctx);
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    SV_List expanded = {0};
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (!expand_link_option_token(ctx, a[i], &expanded)) return eval_result_from_ctx(ctx);
    }

    SV_List unique = {0};
    for (size_t i = 0; i < arena_arr_len(expanded); i++) {
        if (expanded[i].count == 0) continue;
        if (sv_list_contains_exact(&unique, expanded[i])) continue;
        if (!svu_list_push_temp(ctx, &unique, expanded[i])) return eval_result_from_ctx(ctx);
    }

    SV_List added_items = {0};
    for (size_t i = 0; i < arena_arr_len(unique); i++) {
        bool added = false;
        if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_link_opts_var), unique[i], &added)) return eval_result_fatal();
        if (!added) continue;
        if (!svu_list_push_temp(ctx, &added_items, unique[i])) return eval_result_from_ctx(ctx);
    }
    if (!sync_directory_property_mutation(ctx,
                                          o,
                                          "LINK_OPTIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &added_items)) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    String_View qualifier = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (a[i].count == 0) continue;
        if (eval_sv_eq_ci_lit(a[i], "DEBUG") ||
            eval_sv_eq_ci_lit(a[i], "OPTIMIZED") ||
            eval_sv_eq_ci_lit(a[i], "GENERAL")) {
            if (qualifier.count > 0) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("link_libraries() qualifier without following item"), qualifier);
            }
            qualifier = a[i];
            continue;
        }

        String_View item = a[i];
        if (eval_sv_eq_ci_lit(qualifier, "DEBUG")) {
            item = wrap_link_item_with_config_genex_temp(ctx,
                                                         item,
                                                         nob_sv_from_cstr("$<$<CONFIG:Debug>:"));
        } else if (eval_sv_eq_ci_lit(qualifier, "OPTIMIZED")) {
            item = wrap_link_item_with_config_genex_temp(ctx,
                                                         item,
                                                         nob_sv_from_cstr("$<$<NOT:$<CONFIG:Debug>>:"));
        }

        qualifier = nob_sv_from_cstr("");
    }

    if (qualifier.count > 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("link_libraries() qualifier without following item"), qualifier);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_include_directories(Evaluator_Context *ctx, const Node *node) {
    Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    String_View cur_src = eval_current_source_dir(ctx);
    SV_List resolved_items = {0};

    bool is_system = false;
    bool is_before = false;
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "SYSTEM")) {
            is_system = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "AFTER")) {
            is_before = false;
            continue;
        }

        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (!svu_list_push_temp(ctx, &resolved_items, resolved)) return eval_result_from_ctx(ctx);
    }
    if (!sync_directory_property_mutation(ctx,
                                          o,
                                          "INCLUDE_DIRECTORIES",
                                          is_before ? EVENT_PROPERTY_MUTATE_PREPEND_LIST
                                                    : EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          (is_before ? EVENT_PROPERTY_MODIFIER_BEFORE : EVENT_PROPERTY_MODIFIER_NONE) |
                                              (is_system ? EVENT_PROPERTY_MODIFIER_SYSTEM : EVENT_PROPERTY_MODIFIER_NONE),
                                          &resolved_items)) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_link_directories(Evaluator_Context *ctx, const Node *node) {
    Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    String_View cur_src = eval_current_source_dir(ctx);
    SV_List resolved_items = {0};

    bool is_before = false;
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "AFTER")) {
            is_before = false;
            continue;
        }

        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (!svu_list_push_temp(ctx, &resolved_items, resolved)) return eval_result_from_ctx(ctx);
    }
    if (!sync_directory_property_mutation(ctx,
                                          o,
                                          "LINK_DIRECTORIES",
                                          is_before ? EVENT_PROPERTY_MUTATE_PREPEND_LIST
                                                    : EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          is_before ? EVENT_PROPERTY_MODIFIER_BEFORE : EVENT_PROPERTY_MODIFIER_NONE,
                                          &resolved_items)) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_filename_component(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_filename_component() requires <var> <file> <component>"), nob_sv_from_cstr("Usage: get_filename_component(<var> <file> <DIRECTORY|NAME|EXT|NAME_WE|LAST_EXT|NAME_WLE|PATH|ABSOLUTE|REALPATH|PROGRAM> ...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View out_var = a[0];
    String_View input = a[1];
    String_View mode = a[2];
    bool cache_result = false;

    String_View current_src = eval_current_source_dir(ctx);

    String_View result = nob_sv_from_cstr("");
    if (eval_sv_eq_ci_lit(mode, "DIRECTORY") || eval_sv_eq_ci_lit(mode, "PATH")) {
        for (size_t i = 3; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "CACHE")) {
                cache_result = true;
                continue;
            }
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_filename_component(DIRECTORY) received unexpected argument"), a[i]);
            return eval_result_from_ctx(ctx);
        }
        String_View normalized = eval_sv_path_normalize_temp(ctx, input);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        result = gfc_directory_sv(normalized);
    } else if (eval_sv_eq_ci_lit(mode, "NAME")) {
        for (size_t i = 3; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "CACHE")) {
                cache_result = true;
                continue;
            }
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_filename_component(NAME) received unexpected argument"), a[i]);
            return eval_result_from_ctx(ctx);
        }
        String_View normalized = eval_sv_path_normalize_temp(ctx, input);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        result = gfc_filename_sv(normalized);
    } else if (eval_sv_eq_ci_lit(mode, "EXT") || eval_sv_eq_ci_lit(mode, "LAST_EXT") ||
               eval_sv_eq_ci_lit(mode, "NAME_WE") || eval_sv_eq_ci_lit(mode, "NAME_WLE")) {
        for (size_t i = 3; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "CACHE")) {
                cache_result = true;
                continue;
            }
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_filename_component() received unexpected argument for name/extension mode"), a[i]);
            return eval_result_from_ctx(ctx);
        }
        String_View normalized = eval_sv_path_normalize_temp(ctx, input);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        String_View name = gfc_filename_sv(normalized);
        bool last_only = eval_sv_eq_ci_lit(mode, "LAST_EXT") || eval_sv_eq_ci_lit(mode, "NAME_WLE");
        if (eval_sv_eq_ci_lit(mode, "EXT") || eval_sv_eq_ci_lit(mode, "LAST_EXT")) {
            result = gfc_extension_from_name(name, last_only);
        } else {
            result = gfc_stem_from_name(name, last_only);
        }
    } else if (eval_sv_eq_ci_lit(mode, "ABSOLUTE") || eval_sv_eq_ci_lit(mode, "REALPATH")) {
        String_View base_dir = current_src;
        for (size_t i = 3; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "BASE_DIR")) {
                if (i + 1 >= arena_arr_len(a)) {
                    (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_filename_component(BASE_DIR) requires a value"), nob_sv_from_cstr("Usage: get_filename_component(<var> <file> ABSOLUTE|REALPATH [BASE_DIR <dir>] [CACHE])"));
                    return eval_result_from_ctx(ctx);
                }
                base_dir = eval_path_resolve_for_cmake_arg(ctx, a[++i], current_src, false);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "CACHE")) {
                cache_result = true;
                continue;
            }
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_filename_component(ABSOLUTE/REALPATH) received unexpected argument"), a[i]);
            return eval_result_from_ctx(ctx);
        }

        result = eval_path_resolve_for_cmake_arg(ctx, input, base_dir, false);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (eval_sv_eq_ci_lit(mode, "REALPATH")) {
            String_View resolved = nob_sv_from_cstr("");
            if (!eval_real_path_resolve_temp(ctx, result, true, &resolved)) return eval_result_from_ctx(ctx);
            result = resolved;
        }
    } else if (eval_sv_eq_ci_lit(mode, "PROGRAM")) {
        String_View args_var = nob_sv_from_cstr("");
        for (size_t i = 3; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "PROGRAM_ARGS")) {
                if (i + 1 >= arena_arr_len(a)) {
                    (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_filename_component(PROGRAM_ARGS) requires an output variable"), nob_sv_from_cstr("Usage: get_filename_component(<var> <file> PROGRAM [PROGRAM_ARGS <arg-var>] [CACHE])"));
                    return eval_result_from_ctx(ctx);
                }
                args_var = a[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "CACHE")) {
                cache_result = true;
                continue;
            }
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_filename_component(PROGRAM) received unexpected argument"), a[i]);
            return eval_result_from_ctx(ctx);
        }

        SV_List tokens = {0};
        if (!eval_split_shell_like_temp(ctx, input, &tokens)) return eval_result_from_ctx(ctx);
        if (arena_arr_len(tokens) > 0) {
            result = tokens[0];
            if (!gfc_resolve_program_full_path(ctx, result, &result)) return eval_result_from_ctx(ctx);
        }
        if (args_var.count > 0) {
            String_View arg_value = (arena_arr_len(tokens) > 1)
                ? eval_sv_join_semi_temp(ctx, &tokens[1], arena_arr_len(tokens) - 1)
                : nob_sv_from_cstr("");
            if (!eval_var_set_current(ctx, args_var, arg_value)) return eval_result_from_ctx(ctx);
        }
    } else {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "dispatcher", nob_sv_from_cstr("get_filename_component() unsupported component"), mode);
        return eval_result_from_ctx(ctx);
    }

    if (!gfc_set_output_value(ctx, o, out_var, result, cache_result)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
