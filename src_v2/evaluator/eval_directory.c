#include "eval_directory.h"

#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <string.h>

static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";
static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_link_opts_var = "NOBIFY_GLOBAL_LINK_OPTIONS";

typedef struct {
    SV_List definitions;
    SV_List options;
} Remove_Definitions_Request;

typedef struct {
    SV_List items;
} Directory_Items_Request;

typedef struct {
    SV_List definitions;
    SV_List options;
} Add_Definitions_Request;

typedef struct {
    String_View regex_match;
    String_View regex_complain;
    bool has_regex_complain;
} Include_Regular_Expression_Request;

typedef struct {
    SV_List items;
    bool is_before;
    bool is_system;
} Directory_Path_Mutation_Request;

typedef struct {
    SV_List items;
} Link_Libraries_Request;

typedef enum {
    GFC_MODE_DIRECTORY = 0,
    GFC_MODE_PATH,
    GFC_MODE_NAME,
    GFC_MODE_EXT,
    GFC_MODE_LAST_EXT,
    GFC_MODE_NAME_WE,
    GFC_MODE_NAME_WLE,
    GFC_MODE_ABSOLUTE,
    GFC_MODE_REALPATH,
    GFC_MODE_PROGRAM,
} Get_Filename_Component_Mode;

typedef struct {
    String_View out_var;
    String_View input;
    Get_Filename_Component_Mode mode;
    bool cache_result;
    String_View cache_type;
    String_View base_dir;
    String_View args_var;
} Get_Filename_Component_Request;

static bool sync_directory_property_mutation(EvalExecContext *ctx,
                                             Event_Origin origin,
                                             const char *property_name,
                                             Event_Property_Mutate_Op op,
                                             uint32_t modifier_flags,
                                             const SV_List *items);

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

static bool sv_list_push_unique_temp(EvalExecContext *ctx, SV_List *list, String_View item) {
    if (!ctx || !list) return false;
    if (item.count == 0) return true;
    if (sv_list_contains_exact(list, item)) return true;
    return svu_list_push_temp(ctx, list, item);
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

static bool append_list_var_unique(EvalExecContext *ctx, String_View var, String_View item, bool *out_added) {
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

static bool remove_list_var_exact(EvalExecContext *ctx, String_View var, String_View item, bool *out_removed) {
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

static bool collect_new_list_var_items(EvalExecContext *ctx,
                                       String_View var,
                                       const SV_List *requested_items,
                                       SV_List *out_added_items) {
    if (!ctx || !requested_items || !out_added_items) return false;
    *out_added_items = NULL;

    for (size_t i = 0; i < arena_arr_len(*requested_items); i++) {
        bool added = false;
        if (!append_list_var_unique(ctx, var, (*requested_items)[i], &added)) return false;
        if (!added) continue;
        if (!svu_list_push_temp(ctx, out_added_items, (*requested_items)[i])) return false;
    }
    return true;
}

static bool current_list_var_items_temp(EvalExecContext *ctx, String_View var, SV_List *out_items) {
    if (!ctx || !out_items) return false;
    *out_items = NULL;

    String_View current = eval_var_get_visible(ctx, var);
    if (current.count == 0) return true;
    return eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), current, out_items);
}

static bool split_definition_flag_body(String_View item, String_View *out_body) {
    if (!out_body) return false;
    *out_body = nob_sv_from_cstr("");
    if (item.count < 2 || !item.data) return false;
    bool is_dash_d = item.data[0] == '-' && (item.data[1] == 'D' || item.data[1] == 'd');
    bool is_slash_d = item.data[0] == '/' && (item.data[1] == 'D' || item.data[1] == 'd');
    if (!is_dash_d && !is_slash_d) return false;
    *out_body = nob_sv_from_parts(item.data + 2, item.count - 2);
    return true;
}

static bool split_valid_definition_flag(String_View item, String_View *out_definition) {
    if (!out_definition) return false;
    *out_definition = nob_sv_from_cstr("");

    String_View body = nob_sv_from_cstr("");
    if (!split_definition_flag_body(item, &body)) return false;
    if (body.count == 0 || !body.data) return false;

    char first = body.data[0];
    if (!(isalpha((unsigned char)first) || first == '_')) return false;

    size_t i = 1;
    while (i < body.count && body.data[i] != '=') {
        char c = body.data[i];
        if (!(isalnum((unsigned char)c) || c == '_')) return false;
        i++;
    }

    *out_definition = body;
    return true;
}

static String_View directory_legacy_definition_canonical_temp(EvalExecContext *ctx, String_View value) {
    if (!ctx || value.count == 0) return value;
    if (!eval_policy_is_new(ctx, EVAL_POLICY_CMP0005)) return value;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), value.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < value.count; i++) {
        char c = value.data[i];
        if (c == '\\' && (i + 1) < value.count) {
            char next = value.data[i + 1];
            if (next == '\\' || next == '"' || next == ';') {
                buf[off++] = next;
                i++;
                continue;
            }
        }
        buf[off++] = c;
    }
    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static bool directory_definition_equals_for_policy(EvalExecContext *ctx, String_View lhs, String_View rhs) {
    if (!ctx) return false;
    if (!eval_policy_is_new(ctx, EVAL_POLICY_CMP0005)) return sv_eq_exact(lhs, rhs);
    String_View lhs_canon = directory_legacy_definition_canonical_temp(ctx, lhs);
    if (eval_should_stop(ctx)) return false;
    String_View rhs_canon = directory_legacy_definition_canonical_temp(ctx, rhs);
    if (eval_should_stop(ctx)) return false;
    return sv_eq_exact(lhs_canon, rhs_canon);
}

static bool remove_definition_list_var(EvalExecContext *ctx,
                                       String_View var,
                                       String_View item,
                                       bool *out_removed) {
    if (out_removed) *out_removed = false;
    if (!ctx || item.count == 0) return false;

    SV_List current_items = NULL;
    if (!current_list_var_items_temp(ctx, var, &current_items)) return false;
    if (arena_arr_len(current_items) == 0) return true;

    SV_List kept = NULL;
    bool removed = false;
    for (size_t i = 0; i < arena_arr_len(current_items); i++) {
        if (directory_definition_equals_for_policy(ctx, current_items[i], item)) {
            removed = true;
            continue;
        }
        if (!svu_list_push_temp(ctx, &kept, current_items[i])) return false;
    }
    if (!removed) return true;

    if (out_removed) *out_removed = true;
    return eval_var_set_current(ctx,
                                var,
                                arena_arr_len(kept) > 0
                                    ? eval_sv_join_semi_temp(ctx, kept, arena_arr_len(kept))
                                    : nob_sv_from_cstr(""));
}

static bool directory_canonicalize_definition_list_temp(EvalExecContext *ctx,
                                                        const SV_List *raw_items,
                                                        SV_List *out_items) {
    if (!ctx || !raw_items || !out_items) return false;
    *out_items = NULL;

    for (size_t i = 0; i < arena_arr_len(*raw_items); i++) {
        String_View item = directory_legacy_definition_canonical_temp(ctx, (*raw_items)[i]);
        if (eval_should_stop(ctx)) return false;
        if (!svu_list_push_temp(ctx, out_items, item)) return false;
    }
    return true;
}

static bool remove_definitions_parse_request(EvalExecContext *ctx,
                                             SV_List args,
                                             Remove_Definitions_Request *out_req) {
    if (!ctx || !out_req) return false;

    *out_req = (Remove_Definitions_Request){0};
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (args[i].count == 0) continue;

        String_View definition = nob_sv_from_cstr("");
        if (split_valid_definition_flag(args[i], &definition)) {
            if (!eval_sv_arr_push_temp(ctx, &out_req->definitions, definition)) return false;
            continue;
        }
        if (!eval_sv_arr_push_temp(ctx, &out_req->options, args[i])) return false;
    }

    return true;
}

static bool remove_definitions_execute_request(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               const Remove_Definitions_Request *req) {
    if (!ctx || !req) return false;

    bool removed_any_definition = false;
    for (size_t i = 0; i < arena_arr_len(req->definitions); i++) {
        bool removed = false;
        if (!remove_definition_list_var(ctx, nob_sv_from_cstr(k_global_defs_var), req->definitions[i], &removed)) {
            return false;
        }
        removed_any_definition = removed_any_definition || removed;
    }

    bool removed_any_option = false;
    for (size_t i = 0; i < arena_arr_len(req->options); i++) {
        bool removed = false;
        if (!remove_list_var_exact(ctx, nob_sv_from_cstr(k_global_opts_var), req->options[i], &removed)) {
            return false;
        }
        removed_any_option = removed_any_option || removed;
    }

    if (removed_any_definition) {
        SV_List remaining_definitions = NULL;
        SV_List remaining_canonical = NULL;
        if (!current_list_var_items_temp(ctx, nob_sv_from_cstr(k_global_defs_var), &remaining_definitions)) {
            return false;
        }
        if (!directory_canonicalize_definition_list_temp(ctx, &remaining_definitions, &remaining_canonical)) {
            return false;
        }
        if (!sync_directory_property_mutation(ctx,
                                              origin,
                                              "COMPILE_DEFINITIONS",
                                              EVENT_PROPERTY_MUTATE_SET,
                                              EVENT_PROPERTY_MODIFIER_NONE,
                                              &remaining_canonical)) {
            return false;
        }
    }

    if (removed_any_option) {
        SV_List remaining_options = NULL;
        if (!current_list_var_items_temp(ctx, nob_sv_from_cstr(k_global_opts_var), &remaining_options)) {
            return false;
        }
        if (!sync_directory_property_mutation(ctx,
                                              origin,
                                              "COMPILE_OPTIONS",
                                              EVENT_PROPERTY_MUTATE_SET,
                                              EVENT_PROPERTY_MODIFIER_NONE,
                                              &remaining_options)) {
            return false;
        }
    }

    return true;
}

static String_View join_directory_property_items_temp(EvalExecContext *ctx,
                                                      const SV_List *items,
                                                      Event_Property_Mutate_Op op) {
    if (!ctx || !items) return nob_sv_from_cstr("");
    size_t count = arena_arr_len(*items);
    if (count == 0) return nob_sv_from_cstr("");
    if (op == EVENT_PROPERTY_MUTATE_APPEND_STRING) return svu_join_no_sep_temp(ctx, *items, count);
    return eval_sv_join_semi_temp(ctx, *items, count);
}

static String_View directory_property_store_key_temp(EvalExecContext *ctx, String_View property_name) {
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

static String_View merge_directory_property_value_temp(EvalExecContext *ctx,
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

static bool sync_directory_property_mutation(EvalExecContext *ctx,
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

    String_View current_dir = eval_current_source_dir_for_paths(ctx);
    bool current_defined = false;
    String_View current = nob_sv_from_cstr("");
    if (!eval_property_engine_get(ctx,
                                  nob_sv_from_cstr("DIRECTORY"),
                                  current_dir,
                                  property_sv,
                                  &current,
                                  &current_defined)) {
        return false;
    }
    String_View merged = merge_directory_property_value_temp(ctx, current, items, op);
    if (eval_should_stop(ctx)) return false;

    if (!current_defined && op == EVENT_PROPERTY_MUTATE_SET && merged.count == 0) return true;
    if (current_defined && nob_sv_eq(current, merged)) return true;

    if (!eval_property_engine_set(ctx, nob_sv_from_cstr("DIRECTORY"), current_dir, property_sv, merged)) {
        return false;
    }
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

static bool gfc_sv_ends_with_ci_lit(String_View value, const char *suffix) {
    String_View suffix_sv = nob_sv_from_cstr(suffix);
    if (value.count < suffix_sv.count) return false;
    return eval_sv_eq_ci_lit(nob_sv_from_parts(value.data + value.count - suffix_sv.count,
                                               suffix_sv.count),
                             suffix);
}

static bool gfc_is_notfound_value(String_View value) {
    return eval_sv_eq_ci_lit(value, "NOTFOUND") || gfc_sv_ends_with_ci_lit(value, "-NOTFOUND");
}

static bool gfc_cache_request_reuses_existing_result(EvalExecContext *ctx, String_View out_var) {
    if (!ctx || out_var.count == 0) return false;
    if (!eval_var_defined_visible(ctx, out_var)) return false;
    return !gfc_is_notfound_value(eval_var_get_visible(ctx, out_var));
}

static bool gfc_resolve_program_full_path(EvalExecContext *ctx,
                                          String_View token,
                                          String_View *out_program,
                                          bool *out_found) {
    if (!ctx || !out_program) return false;
    return eval_find_program_full_path_temp(ctx, token, out_program, out_found);
}

static bool gfc_set_output_value(EvalExecContext *ctx,
                                 Cmake_Event_Origin origin,
                                 String_View out_var,
                                 String_View value,
                                 bool cache_result,
                                 String_View cache_type) {
    if (!ctx) return false;
    if (!cache_result) return eval_var_set_current(ctx, out_var, value);
    if (!eval_cache_set(ctx, out_var, value, cache_type, nob_sv_from_cstr(""))) return false;
    return eval_emit_var_set_cache(ctx, origin, out_var, value);
}

static bool gfc_parse_cache_only_trailing_args(EvalExecContext *ctx,
                                               const Node *node,
                                               Cmake_Event_Origin origin,
                                               SV_List args,
                                               size_t start_index,
                                               String_View unexpected_cause,
                                               Get_Filename_Component_Request *req) {
    if (!ctx || !node || !req) return false;
    for (size_t i = start_index; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "CACHE")) {
            req->cache_result = true;
            continue;
        }
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                             node,
                                             origin,
                                             EV_DIAG_ERROR,
                                             EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                             "dispatcher",
                                             unexpected_cause,
                                             args[i]);
        return false;
    }
    return true;
}

static bool gfc_parse_absolute_or_realpath_trailing_args(EvalExecContext *ctx,
                                                         const Node *node,
                                                         Cmake_Event_Origin origin,
                                                         SV_List args,
                                                         String_View current_src,
                                                         Get_Filename_Component_Request *req) {
    if (!ctx || !node || !req) return false;
    req->base_dir = current_src;

    for (size_t i = 3; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "BASE_DIR")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_filename_component(BASE_DIR) requires a value"), nob_sv_from_cstr("Usage: get_filename_component(<var> <file> ABSOLUTE|REALPATH [BASE_DIR <dir>] [CACHE])"));
                return false;
            }
            req->base_dir = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "CACHE")) {
            req->cache_result = true;
            continue;
        }
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_filename_component(ABSOLUTE/REALPATH) received unexpected argument"), args[i]);
        return false;
    }

    return true;
}

static bool gfc_parse_program_trailing_args(EvalExecContext *ctx,
                                            const Node *node,
                                            Cmake_Event_Origin origin,
                                            SV_List args,
                                            Get_Filename_Component_Request *req) {
    if (!ctx || !node || !req) return false;
    for (size_t i = 3; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "PROGRAM_ARGS")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_filename_component(PROGRAM_ARGS) requires an output variable"), nob_sv_from_cstr("Usage: get_filename_component(<var> <file> PROGRAM [PROGRAM_ARGS <arg-var>] [CACHE])"));
                return false;
            }
            req->args_var = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "CACHE")) {
            req->cache_result = true;
            continue;
        }
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_filename_component(PROGRAM) received unexpected argument"), args[i]);
        return false;
    }
    return true;
}

static bool gfc_parse_request(EvalExecContext *ctx,
                              const Node *node,
                              Cmake_Event_Origin origin,
                              Get_Filename_Component_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Get_Filename_Component_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    if (arena_arr_len(args) < 3) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_filename_component() requires <var> <file> <component>"), nob_sv_from_cstr("Usage: get_filename_component(<var> <file> <DIRECTORY|NAME|EXT|NAME_WE|LAST_EXT|NAME_WLE|PATH|ABSOLUTE|REALPATH|PROGRAM> ...)"));
        return false;
    }

    req.out_var = args[0];
    req.input = args[1];
    req.cache_type = nob_sv_from_cstr("STRING");

    String_View current_src = eval_current_source_dir(ctx);
    String_View mode = args[2];
    if (eval_sv_eq_ci_lit(mode, "DIRECTORY")) {
        req.mode = GFC_MODE_DIRECTORY;
        if (!gfc_parse_cache_only_trailing_args(ctx,
                                                node,
                                                origin,
                                                args,
                                                3,
                                                nob_sv_from_cstr("get_filename_component(DIRECTORY) received unexpected argument"),
                                                &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "PATH")) {
        req.mode = GFC_MODE_PATH;
        req.cache_type = nob_sv_from_cstr("FILEPATH");
        if (!gfc_parse_cache_only_trailing_args(ctx,
                                                node,
                                                origin,
                                                args,
                                                3,
                                                nob_sv_from_cstr("get_filename_component(DIRECTORY) received unexpected argument"),
                                                &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "NAME")) {
        req.mode = GFC_MODE_NAME;
        if (!gfc_parse_cache_only_trailing_args(ctx,
                                                node,
                                                origin,
                                                args,
                                                3,
                                                nob_sv_from_cstr("get_filename_component(NAME) received unexpected argument"),
                                                &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "EXT")) {
        req.mode = GFC_MODE_EXT;
        if (!gfc_parse_cache_only_trailing_args(ctx,
                                                node,
                                                origin,
                                                args,
                                                3,
                                                nob_sv_from_cstr("get_filename_component() received unexpected argument for name/extension mode"),
                                                &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "LAST_EXT")) {
        req.mode = GFC_MODE_LAST_EXT;
        if (!gfc_parse_cache_only_trailing_args(ctx,
                                                node,
                                                origin,
                                                args,
                                                3,
                                                nob_sv_from_cstr("get_filename_component() received unexpected argument for name/extension mode"),
                                                &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "NAME_WE")) {
        req.mode = GFC_MODE_NAME_WE;
        if (!gfc_parse_cache_only_trailing_args(ctx,
                                                node,
                                                origin,
                                                args,
                                                3,
                                                nob_sv_from_cstr("get_filename_component() received unexpected argument for name/extension mode"),
                                                &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "NAME_WLE")) {
        req.mode = GFC_MODE_NAME_WLE;
        if (!gfc_parse_cache_only_trailing_args(ctx,
                                                node,
                                                origin,
                                                args,
                                                3,
                                                nob_sv_from_cstr("get_filename_component() received unexpected argument for name/extension mode"),
                                                &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "ABSOLUTE")) {
        req.mode = GFC_MODE_ABSOLUTE;
        if (!gfc_parse_absolute_or_realpath_trailing_args(ctx, node, origin, args, current_src, &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "REALPATH")) {
        req.mode = GFC_MODE_REALPATH;
        if (!gfc_parse_absolute_or_realpath_trailing_args(ctx, node, origin, args, current_src, &req)) {
            return false;
        }
    } else if (eval_sv_eq_ci_lit(mode, "PROGRAM")) {
        req.mode = GFC_MODE_PROGRAM;
        if (!gfc_parse_program_trailing_args(ctx, node, origin, args, &req)) return false;
    } else {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "dispatcher", nob_sv_from_cstr("get_filename_component() unsupported component"), mode);
        return false;
    }

    *out_req = req;
    return true;
}

static bool gfc_execute_program_request(EvalExecContext *ctx,
                                        Cmake_Event_Origin origin,
                                        const Get_Filename_Component_Request *req,
                                        String_View *out_result) {
    if (!ctx || !req || !out_result) return false;
    *out_result = nob_sv_from_cstr("");

    String_View program_path = nob_sv_from_cstr("");
    bool found = false;
    if (req->input.count > 0 &&
        !gfc_resolve_program_full_path(ctx, req->input, &program_path, &found)) {
        return false;
    }

    String_View program_args = nob_sv_from_cstr("");
    if (!found) {
        String_View program_token = nob_sv_from_cstr("");
        if (!eval_split_program_from_command_line_temp(ctx,
                                                       EVAL_CMDLINE_NATIVE,
                                                       req->input,
                                                       &program_token,
                                                       &program_args)) {
            return false;
        }
        if (program_token.count > 0 &&
            !gfc_resolve_program_full_path(ctx, program_token, &program_path, &found)) {
            return false;
        }
        if (!found) program_args = nob_sv_from_cstr("");
    }

    if (found) *out_result = program_path;
    if (req->args_var.count > 0 && program_args.count > 0) {
        if (!gfc_set_output_value(ctx,
                                  origin,
                                  req->args_var,
                                  program_args,
                                  req->cache_result,
                                  nob_sv_from_cstr("STRING"))) {
            return false;
        }
    }
    return true;
}

static bool gfc_execute_request(EvalExecContext *ctx,
                                Cmake_Event_Origin origin,
                                const Get_Filename_Component_Request *req) {
    if (!ctx || !req) return false;
    if (req->cache_result && gfc_cache_request_reuses_existing_result(ctx, req->out_var)) return true;

    String_View result = nob_sv_from_cstr("");
    switch (req->mode) {
        case GFC_MODE_DIRECTORY:
        case GFC_MODE_PATH: {
            String_View normalized = eval_sv_path_normalize_temp(ctx, req->input);
            if (eval_should_stop(ctx)) return false;
            result = gfc_directory_sv(normalized);
        } break;
        case GFC_MODE_NAME: {
            String_View normalized = eval_sv_path_normalize_temp(ctx, req->input);
            if (eval_should_stop(ctx)) return false;
            result = gfc_filename_sv(normalized);
        } break;
        case GFC_MODE_EXT:
        case GFC_MODE_LAST_EXT:
        case GFC_MODE_NAME_WE:
        case GFC_MODE_NAME_WLE: {
            String_View normalized = eval_sv_path_normalize_temp(ctx, req->input);
            if (eval_should_stop(ctx)) return false;
            String_View name = gfc_filename_sv(normalized);
            bool last_only = (req->mode == GFC_MODE_LAST_EXT) || (req->mode == GFC_MODE_NAME_WLE);
            if (req->mode == GFC_MODE_EXT || req->mode == GFC_MODE_LAST_EXT) {
                result = gfc_extension_from_name(name, last_only);
            } else {
                result = gfc_stem_from_name(name, last_only);
            }
        } break;
        case GFC_MODE_ABSOLUTE:
        case GFC_MODE_REALPATH: {
            result = eval_path_resolve_for_cmake_arg(ctx, req->input, req->base_dir, false);
            if (eval_should_stop(ctx)) return false;
            if (req->mode == GFC_MODE_REALPATH) {
                String_View resolved = nob_sv_from_cstr("");
                if (!eval_real_path_resolve_temp(ctx, result, true, &resolved)) return false;
                result = resolved;
            }
        } break;
        case GFC_MODE_PROGRAM:
            if (!gfc_execute_program_request(ctx, origin, req, &result)) return false;
            break;
    }

    return gfc_set_output_value(ctx,
                                origin,
                                req->out_var,
                                result,
                                req->cache_result,
                                req->cache_type);
}

static bool emit_compile_definition_to_current_file_targets(EvalExecContext *ctx,
                                                            Cmake_Event_Origin origin,
                                                            String_View item) {
    if (!ctx || item.count == 0) return false;
    Eval_Target_Model *targets = &ctx->semantic_state.targets;
    for (size_t i = 0; i < arena_arr_len(targets->records); i++) {
        String_View target_name = targets->records[i].name;
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

static bool emit_compile_option_to_current_file_targets(EvalExecContext *ctx,
                                                        Cmake_Event_Origin origin,
                                                        String_View item) {
    if (!ctx || item.count == 0) return false;
    Eval_Target_Model *targets = &ctx->semantic_state.targets;
    for (size_t i = 0; i < arena_arr_len(targets->records); i++) {
        String_View target_name = targets->records[i].name;
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

static String_View wrap_link_item_with_config_genex_temp(EvalExecContext *ctx,
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

static bool split_comma_list_temp(EvalExecContext *ctx, String_View input, SV_List *out) {
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

static bool expand_compile_option_token(EvalExecContext *ctx, String_View tok, SV_List *out) {
    if (!ctx || !out) return false;
    if (tok.count == 0) return true;

    if (svu_has_prefix_ci_lit(tok, "SHELL:")) {
        String_View payload = (tok.count > 6) ? nob_sv_from_parts(tok.data + 6, tok.count - 6) : nob_sv_from_cstr("");
        return eval_split_shell_like_temp(ctx, payload, out);
    }
    return svu_list_push_temp(ctx, out, tok);
}

static bool expand_link_option_token(EvalExecContext *ctx, String_View tok, SV_List *out) {
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

static bool directory_parse_add_compile_options_request(EvalExecContext *ctx,
                                                        const Node *node,
                                                        Directory_Items_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Directory_Items_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    SV_List expanded = {0};
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (!expand_compile_option_token(ctx, args[i], &expanded)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(expanded); i++) {
        if (!sv_list_push_unique_temp(ctx, &req.items, expanded[i])) return false;
    }

    *out_req = req;
    return true;
}

static bool directory_execute_add_compile_options_request(EvalExecContext *ctx,
                                                          Event_Origin origin,
                                                          const Directory_Items_Request *req) {
    if (!ctx || !req) return false;
    SV_List added_items = {0};
    if (!collect_new_list_var_items(ctx, nob_sv_from_cstr(k_global_opts_var), &req->items, &added_items)) {
        return false;
    }
    if (!sync_directory_property_mutation(ctx,
                                          origin,
                                          "COMPILE_OPTIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &added_items)) {
        return false;
    }
    return true;
}

static bool directory_parse_add_compile_definitions_request(EvalExecContext *ctx,
                                                            const Node *node,
                                                            Directory_Items_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Directory_Items_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(args); i++) {
        String_View item = eval_normalize_compile_definition_item(args[i]);
        if (!sv_list_push_unique_temp(ctx, &req.items, item)) return false;
    }

    *out_req = req;
    return true;
}

static bool directory_execute_add_compile_definitions_request(EvalExecContext *ctx,
                                                              Cmake_Event_Origin origin,
                                                              const Directory_Items_Request *req) {
    if (!ctx || !req) return false;
    SV_List added_items = {0};
    if (!collect_new_list_var_items(ctx, nob_sv_from_cstr(k_global_defs_var), &req->items, &added_items)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(added_items); i++) {
        if (!emit_compile_definition_to_current_file_targets(ctx, origin, added_items[i])) return false;
    }
    if (!sync_directory_property_mutation(ctx,
                                          origin,
                                          "COMPILE_DEFINITIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &added_items)) {
        return false;
    }
    return true;
}

static bool directory_parse_add_definitions_request(EvalExecContext *ctx,
                                                    const Node *node,
                                                    Add_Definitions_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Add_Definitions_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(args); i++) {
        String_View item = args[i];
        if (item.count == 0) continue;

        String_View definition = nob_sv_from_cstr("");
        if (split_valid_definition_flag(item, &definition)) {
            if (!svu_list_push_temp(ctx, &req.definitions, definition)) return false;
            continue;
        }
        if (!svu_list_push_temp(ctx, &req.options, item)) return false;
    }

    *out_req = req;
    return true;
}

static bool directory_execute_add_definitions_request(EvalExecContext *ctx,
                                                      Cmake_Event_Origin origin,
                                                      const Add_Definitions_Request *req) {
    if (!ctx || !req) return false;

    SV_List emitted_defs = {0};
    SV_List emitted_defs_canonical = {0};
    if (!collect_new_list_var_items(ctx, nob_sv_from_cstr(k_global_defs_var), &req->definitions, &emitted_defs)) {
        return false;
    }
    if (!directory_canonicalize_definition_list_temp(ctx, &emitted_defs, &emitted_defs_canonical)) return false;
    for (size_t i = 0; i < arena_arr_len(emitted_defs); i++) {
        if (!emit_compile_definition_to_current_file_targets(ctx, origin, emitted_defs_canonical[i])) return false;
    }

    SV_List emitted_opts = {0};
    if (!collect_new_list_var_items(ctx, nob_sv_from_cstr(k_global_opts_var), &req->options, &emitted_opts)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(emitted_opts); i++) {
        if (!emit_compile_option_to_current_file_targets(ctx, origin, emitted_opts[i])) return false;
    }

    if (!sync_directory_property_mutation(ctx,
                                          origin,
                                          "COMPILE_DEFINITIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &emitted_defs_canonical)) {
        return false;
    }
    if (!sync_directory_property_mutation(ctx,
                                          origin,
                                          "COMPILE_OPTIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &emitted_opts)) {
        return false;
    }
    return true;
}

static bool directory_parse_include_regular_expression_request(EvalExecContext *ctx,
                                                               const Node *node,
                                                               Cmake_Event_Origin origin,
                                                               Include_Regular_Expression_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Include_Regular_Expression_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    if (arena_arr_len(args) < 1 || arena_arr_len(args) > 2) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "include_regular_expression", nob_sv_from_cstr("include_regular_expression() requires one or two regex arguments"), nob_sv_from_cstr("Usage: include_regular_expression(<regex_match> [<regex_complain>])"));
        return false;
    }

    req.regex_match = args[0];
    if (arena_arr_len(args) == 2) {
        req.regex_complain = args[1];
        req.has_regex_complain = true;
    }
    *out_req = req;
    return true;
}

static bool directory_execute_include_regular_expression_request(EvalExecContext *ctx,
                                                                 const Include_Regular_Expression_Request *req) {
    if (!ctx || !req) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION"), req->regex_match)) {
        return false;
    }
    if (req->has_regex_complain &&
        !eval_var_set_current(ctx,
                              nob_sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN"),
                              req->regex_complain)) {
        return false;
    }
    return true;
}

static bool directory_parse_add_link_options_request(EvalExecContext *ctx,
                                                     const Node *node,
                                                     Directory_Items_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Directory_Items_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    SV_List expanded = {0};
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (!expand_link_option_token(ctx, args[i], &expanded)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(expanded); i++) {
        if (!sv_list_push_unique_temp(ctx, &req.items, expanded[i])) return false;
    }

    *out_req = req;
    return true;
}

static bool directory_execute_add_link_options_request(EvalExecContext *ctx,
                                                       Cmake_Event_Origin origin,
                                                       const Directory_Items_Request *req) {
    if (!ctx || !req) return false;
    SV_List added_items = {0};
    if (!collect_new_list_var_items(ctx, nob_sv_from_cstr(k_global_link_opts_var), &req->items, &added_items)) {
        return false;
    }
    if (!sync_directory_property_mutation(ctx,
                                          origin,
                                          "LINK_OPTIONS",
                                          EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                          EVENT_PROPERTY_MODIFIER_NONE,
                                          &added_items)) {
        return false;
    }
    return true;
}

static bool directory_parse_include_directories_request(EvalExecContext *ctx,
                                                        const Node *node,
                                                        Directory_Path_Mutation_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Directory_Path_Mutation_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    String_View current_src = eval_current_source_dir(ctx);
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "SYSTEM")) {
            req.is_system = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "BEFORE")) {
            req.is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "AFTER")) {
            req.is_before = false;
            continue;
        }

        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, args[i], current_src, true);
        if (eval_should_stop(ctx)) return false;
        if (!svu_list_push_temp(ctx, &req.items, resolved)) return false;
    }

    *out_req = req;
    return true;
}

static bool directory_execute_include_directories_request(EvalExecContext *ctx,
                                                          Event_Origin origin,
                                                          const Directory_Path_Mutation_Request *req) {
    if (!ctx || !req) return false;
    return sync_directory_property_mutation(ctx,
                                            origin,
                                            "INCLUDE_DIRECTORIES",
                                            req->is_before ? EVENT_PROPERTY_MUTATE_PREPEND_LIST
                                                           : EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                            (req->is_before ? EVENT_PROPERTY_MODIFIER_BEFORE : EVENT_PROPERTY_MODIFIER_NONE) |
                                                (req->is_system ? EVENT_PROPERTY_MODIFIER_SYSTEM : EVENT_PROPERTY_MODIFIER_NONE),
                                            &req->items);
}

static bool directory_parse_link_directories_request(EvalExecContext *ctx,
                                                     const Node *node,
                                                     Directory_Path_Mutation_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Directory_Path_Mutation_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    String_View current_src = eval_current_source_dir(ctx);
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "BEFORE")) {
            req.is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "AFTER")) {
            req.is_before = false;
            continue;
        }

        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, args[i], current_src, true);
        if (eval_should_stop(ctx)) return false;
        if (!svu_list_push_temp(ctx, &req.items, resolved)) return false;
    }

    *out_req = req;
    return true;
}

static bool directory_execute_link_directories_request(EvalExecContext *ctx,
                                                       Event_Origin origin,
                                                       const Directory_Path_Mutation_Request *req) {
    if (!ctx || !req) return false;
    return sync_directory_property_mutation(ctx,
                                            origin,
                                            "LINK_DIRECTORIES",
                                            req->is_before ? EVENT_PROPERTY_MUTATE_PREPEND_LIST
                                                           : EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                            req->is_before ? EVENT_PROPERTY_MODIFIER_BEFORE : EVENT_PROPERTY_MODIFIER_NONE,
                                            &req->items);
}

static bool directory_parse_link_libraries_request(EvalExecContext *ctx,
                                                   const Node *node,
                                                   Cmake_Event_Origin origin,
                                                   Link_Libraries_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    Link_Libraries_Request req = {0};
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    String_View qualifier = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (args[i].count == 0) continue;
        if (eval_sv_eq_ci_lit(args[i], "DEBUG") ||
            eval_sv_eq_ci_lit(args[i], "OPTIMIZED") ||
            eval_sv_eq_ci_lit(args[i], "GENERAL")) {
            if (qualifier.count > 0) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("link_libraries() qualifier without following item"), qualifier);
            }
            qualifier = args[i];
            continue;
        }

        String_View item = args[i];
        if (eval_sv_eq_ci_lit(qualifier, "DEBUG")) {
            item = wrap_link_item_with_config_genex_temp(ctx,
                                                         item,
                                                         nob_sv_from_cstr("$<$<CONFIG:Debug>:"));
        } else if (eval_sv_eq_ci_lit(qualifier, "OPTIMIZED")) {
            item = wrap_link_item_with_config_genex_temp(ctx,
                                                         item,
                                                         nob_sv_from_cstr("$<$<NOT:$<CONFIG:Debug>>:"));
        }

        if (!svu_list_push_temp(ctx, &req.items, item)) return false;
        qualifier = nob_sv_from_cstr("");
    }

    if (qualifier.count > 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("link_libraries() qualifier without following item"), qualifier);
    }

    *out_req = req;
    return true;
}

static bool directory_execute_link_libraries_request(EvalExecContext *ctx,
                                                     Event_Origin origin,
                                                     const Link_Libraries_Request *req) {
    return sync_directory_property_mutation(ctx,
                                            origin,
                                            "LINK_LIBRARIES",
                                            EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                            EVENT_PROPERTY_MODIFIER_NONE,
                                            &req->items);
}

Eval_Result eval_handle_add_compile_options(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Event_Origin origin = eval_origin_from_node(ctx, node);
    Directory_Items_Request req = {0};
    if (!directory_parse_add_compile_options_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!directory_execute_add_compile_options_request(ctx, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_compile_definitions(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Directory_Items_Request req = {0};
    if (!directory_parse_add_compile_definitions_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!directory_execute_add_compile_definitions_request(ctx, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_definitions(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Add_Definitions_Request req = {0};
    if (!directory_parse_add_definitions_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!directory_execute_add_definitions_request(ctx, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_remove_definitions(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Remove_Definitions_Request req = {0};
    if (!remove_definitions_parse_request(ctx, a, &req)) return eval_result_from_ctx(ctx);
    if (!remove_definitions_execute_request(ctx, o, &req)) return eval_result_from_ctx(ctx);

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_include_regular_expression(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Include_Regular_Expression_Request req = {0};
    if (!directory_parse_include_regular_expression_request(ctx, node, origin, &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!directory_execute_include_regular_expression_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_link_options(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Directory_Items_Request req = {0};
    if (!directory_parse_add_link_options_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!directory_execute_add_link_options_request(ctx, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_link_libraries(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Link_Libraries_Request req = {0};
    if (!directory_parse_link_libraries_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    if (!directory_execute_link_libraries_request(ctx, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_include_directories(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Event_Origin origin = eval_origin_from_node(ctx, node);
    Directory_Path_Mutation_Request req = {0};
    if (!directory_parse_include_directories_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!directory_execute_include_directories_request(ctx, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_link_directories(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Event_Origin origin = eval_origin_from_node(ctx, node);
    Directory_Path_Mutation_Request req = {0};
    if (!directory_parse_link_directories_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!directory_execute_link_directories_request(ctx, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_filename_component(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Get_Filename_Component_Request req = {0};
    if (!gfc_parse_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    if (!gfc_execute_request(ctx, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
