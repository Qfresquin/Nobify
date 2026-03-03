#include "eval_target.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "arena_dyn.h"
#include "stb_ds.h"

#include <string.h>

static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";

static bool emit_target_prop_set(Evaluator_Context *ctx,
                                 Cmake_Event_Origin o,
                                 String_View target_name,
                                 String_View key,
                                 String_View value,
                                 Cmake_Target_Property_Op op) {
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_PROP_SET;
    ev.origin = o;
    ev.as.target_prop_set.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_prop_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.target_prop_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.target_prop_set.op = op;
    return emit_event(ctx, ev);
}

static bool emit_var_set(Evaluator_Context *ctx, Cmake_Event_Origin o, String_View key, String_View value) {
    Cmake_Event ev = {0};
    ev.kind = EV_VAR_SET;
    ev.origin = o;
    ev.as.var_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_set.value = sv_copy_to_event_arena(ctx, value);
    return emit_event(ctx, ev);
}

static bool emit_target_dependency(Evaluator_Context *ctx,
                                   Cmake_Event_Origin o,
                                   String_View target_name,
                                   String_View dependency_name) {
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_ADD_DEPENDENCY;
    ev.origin = o;
    ev.as.target_add_dependency.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_add_dependency.dependency_name = sv_copy_to_event_arena(ctx, dependency_name);
    return emit_event(ctx, ev);
}

static bool emit_target_add_source(Evaluator_Context *ctx,
                                   Cmake_Event_Origin o,
                                   String_View target_name,
                                   String_View path) {
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_ADD_SOURCE;
    ev.origin = o;
    ev.as.target_add_source.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}

static bool target_usage_validate_target(Evaluator_Context *ctx,
                                         const Node *node,
                                         String_View target_name) {
    if (!ctx || !node) return false;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    char *cmd_c = eval_sv_to_cstr_temp(ctx, node->as.cmd.name);
    EVAL_OOM_RETURN_IF_NULL(ctx, cmd_c, false);

    if (!eval_target_known(ctx, target_name)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr(nob_temp_sprintf("%s() target was not declared", cmd_c)),
                       target_name);
        return false;
    }
    if (eval_target_alias_known(ctx, target_name)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr(nob_temp_sprintf("%s() cannot be used on ALIAS targets", cmd_c)),
                       target_name);
        return false;
    }
    return true;
}

static bool target_usage_parse_visibility(String_View tok, Cmake_Visibility *io_vis) {
    if (!io_vis) return false;
    if (eval_sv_eq_ci_lit(tok, "PRIVATE")) {
        *io_vis = EV_VISIBILITY_PRIVATE;
        return true;
    }
    if (eval_sv_eq_ci_lit(tok, "PUBLIC")) {
        *io_vis = EV_VISIBILITY_PUBLIC;
        return true;
    }
    if (eval_sv_eq_ci_lit(tok, "INTERFACE")) {
        *io_vis = EV_VISIBILITY_INTERFACE;
        return true;
    }
    return false;
}

static bool target_usage_require_visibility(Evaluator_Context *ctx,
                                            const Node *node) {
    if (!ctx || !node) return false;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    eval_emit_diag(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("target command requires PUBLIC, PRIVATE or INTERFACE before items"),
                   nob_sv_from_cstr("Start each item group with PUBLIC, PRIVATE or INTERFACE"));
    return false;
}

static bool source_group_emit_assignment(Evaluator_Context *ctx,
                                         Cmake_Event_Origin o,
                                         String_View file_path,
                                         String_View group_name) {
    if (!ctx) return false;
    String_View key_parts[3] = {
        nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::"),
        file_path,
        nob_sv_from_cstr("")
    };
    String_View key = svu_join_no_sep_temp(ctx, key_parts, 3);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set(ctx, key, group_name)) return false;
    return emit_var_set(ctx, o, key, group_name);
}

static String_View source_group_dirname(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    for (size_t i = path.count; i > 0; i--) {
        if (!svu_is_path_sep(path.data[i - 1])) continue;
        if (i == 1) return nob_sv_from_cstr("");
        return nob_sv_from_parts(path.data, i - 1);
    }
    return nob_sv_from_cstr("");
}

static bool source_group_path_relative_to_root(Evaluator_Context *ctx,
                                               String_View root,
                                               String_View file_path,
                                               String_View *out_relative) {
    if (!out_relative) return false;
    *out_relative = nob_sv_from_cstr("");
    if (!ctx) return false;

    String_View root_norm = eval_sv_path_normalize_temp(ctx, root);
    if (eval_should_stop(ctx)) return false;
    String_View file_norm = eval_sv_path_normalize_temp(ctx, file_path);
    if (eval_should_stop(ctx)) return false;

    if (file_norm.count < root_norm.count) return false;
    if (root_norm.count > 0 && !svu_eq_ci_sv(nob_sv_from_parts(file_norm.data, root_norm.count), root_norm)) {
        return false;
    }
    if (file_norm.count == root_norm.count) {
        *out_relative = nob_sv_from_cstr("");
        return true;
    }
    if (!svu_is_path_sep(file_norm.data[root_norm.count])) return false;
    *out_relative = nob_sv_from_parts(file_norm.data + root_norm.count + 1,
                                      file_norm.count - root_norm.count - 1);
    return true;
}

static String_View source_group_join_tree_name_temp(Evaluator_Context *ctx,
                                                    String_View prefix,
                                                    String_View relative_dir) {
    if (!ctx) return nob_sv_from_cstr("");
    if (prefix.count == 0 && relative_dir.count == 0) return nob_sv_from_cstr("");
    if (relative_dir.count == 0) return prefix;
    if (prefix.count == 0) return relative_dir;

    size_t total = prefix.count + 1 + relative_dir.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix.data, prefix.count);
    off += prefix.count;
    buf[off++] = '\\';
    memcpy(buf + off, relative_dir.data, relative_dir.count);
    off += relative_dir.count;
    buf[off] = '\0';

    for (size_t i = 0; i < off; i++) {
        if (svu_is_path_sep(buf[i])) buf[i] = '\\';
    }

    return nob_sv_from_parts(buf, off);
}

static String_View target_pch_item_normalize_temp(Evaluator_Context *ctx, String_View item) {
    if (!ctx) return item;
    if (item.count == 0) return item;
    if (item.count >= 2 && item.data[0] == '$' && item.data[1] == '<') return item;
    if (item.data[0] == '<' && item.data[item.count - 1] == '>') return item;
    return eval_path_resolve_for_cmake_arg(ctx, item, eval_current_source_dir_for_paths(ctx), true);
}

static bool source_group_emit_regex_rule(Evaluator_Context *ctx,
                                         Cmake_Event_Origin o,
                                         String_View group_name,
                                         String_View regex_value) {
    if (!ctx) return false;
    String_View line_sv = nob_sv_from_cstr(nob_temp_sprintf("%zu", o.line));
    String_View col_sv = nob_sv_from_cstr(nob_temp_sprintf("%zu", o.col));
    String_View key_parts[5] = {
        nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::"),
        line_sv,
        nob_sv_from_cstr(":"),
        col_sv,
        nob_sv_from_cstr("")
    };
    String_View key = svu_join_no_sep_temp(ctx, key_parts, 5);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set(ctx, key, regex_value)) return false;
    if (!emit_var_set(ctx, o, key, regex_value)) return false;

    String_View name_key_parts[3] = {
        key,
        nob_sv_from_cstr("::NAME"),
        nob_sv_from_cstr("")
    };
    String_View name_key = svu_join_no_sep_temp(ctx, name_key_parts, 3);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set(ctx, name_key, group_name)) return false;
    return emit_var_set(ctx, o, name_key, group_name);
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

static String_View merge_property_value_temp(Evaluator_Context *ctx,
                                             String_View current,
                                             String_View incoming,
                                             Cmake_Target_Property_Op op) {
    if (op == EV_PROP_SET) return incoming;
    if (incoming.count == 0) return current;
    if (current.count == 0) return incoming;

    bool with_semicolon = (op == EV_PROP_APPEND_LIST);
    size_t total = current.count + incoming.count + (with_semicolon ? 1 : 0);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, current.data, current.count);
    off += current.count;
    if (with_semicolon) buf[off++] = ';';
    memcpy(buf + off, incoming.data, incoming.count);
    off += incoming.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool is_current_directory_object(Evaluator_Context *ctx, String_View object_id) {
    if (!ctx) return false;
    if (object_id.count == 0) return true;
    if (eval_sv_eq_ci_lit(object_id, ".")) return true;

    String_View cur_src = eval_current_source_dir(ctx);
    String_View cur_bin = eval_current_binary_dir(ctx);
    if (svu_eq_ci_sv(object_id, cur_src)) return true;
    if (svu_eq_ci_sv(object_id, cur_bin)) return true;
    return false;
}

static bool define_property_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "PROPERTY") ||
           eval_sv_eq_ci_lit(tok, "INHERITED") ||
           eval_sv_eq_ci_lit(tok, "BRIEF_DOCS") ||
           eval_sv_eq_ci_lit(tok, "FULL_DOCS") ||
           eval_sv_eq_ci_lit(tok, "INITIALIZE_FROM_VARIABLE");
}

static bool set_non_target_property(Evaluator_Context *ctx,
                                    Cmake_Event_Origin o,
                                    String_View scope_upper,
                                    String_View object_id,
                                    String_View prop_key,
                                    String_View value,
                                    Cmake_Target_Property_Op op) {
    String_View prop_upper = eval_property_upper_name_temp(ctx, prop_key);
    if (eval_should_stop(ctx)) return false;

    String_View store_key = eval_property_store_key_temp(ctx, scope_upper, object_id, prop_upper);
    if (eval_should_stop(ctx)) return false;

    String_View current = eval_var_get(ctx, store_key);
    String_View merged = merge_property_value_temp(ctx, current, value, op);
    if (eval_should_stop(ctx)) return false;

    if (!eval_var_set(ctx, store_key, merged)) return false;
    if (!emit_var_set(ctx, o, store_key, merged)) return false;

    // Bridge common DIRECTORY properties to existing evaluator behavior.
    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY") && is_current_directory_object(ctx, object_id)) {
        if (eval_sv_eq_ci_lit(prop_upper, "COMPILE_OPTIONS")) {
            String_View cur = eval_var_get(ctx, nob_sv_from_cstr(k_global_opts_var));
            String_View next = merge_property_value_temp(ctx, cur, value, op);
            if (eval_should_stop(ctx)) return false;
            if (!eval_var_set(ctx, nob_sv_from_cstr(k_global_opts_var), next)) return false;
        } else if (eval_sv_eq_ci_lit(prop_upper, "COMPILE_DEFINITIONS")) {
            String_View cur = eval_var_get(ctx, nob_sv_from_cstr(k_global_defs_var));
            String_View next = merge_property_value_temp(ctx, cur, value, op);
            if (eval_should_stop(ctx)) return false;
            if (!eval_var_set(ctx, nob_sv_from_cstr(k_global_defs_var), next)) return false;
        }
    }

    // Pragmatic CACHE behavior: PROPERTY VALUE mutates cache entry variable.
    if (eval_sv_eq_ci_lit(scope_upper, "CACHE") && eval_sv_eq_ci_lit(prop_upper, "VALUE")) {
        if (!eval_var_set(ctx, object_id, merged)) return false;

        Cmake_Event ce = {0};
        ce.kind = EV_SET_CACHE_ENTRY;
        ce.origin = o;
        ce.as.cache_entry.key = sv_copy_to_event_arena(ctx, object_id);
        ce.as.cache_entry.value = sv_copy_to_event_arena(ctx, merged);
        if (!emit_event(ctx, ce)) return false;
    }

    return true;
}

typedef enum {
    PROP_QUERY_VALUE = 0,
    PROP_QUERY_SET,
    PROP_QUERY_DEFINED,
    PROP_QUERY_BRIEF_DOCS,
    PROP_QUERY_FULL_DOCS,
} Eval_Property_Query_Mode;

static bool set_output_var_value(Evaluator_Context *ctx, String_View out_var, String_View value) {
    return eval_var_set(ctx, out_var, value);
}

static bool set_output_var_notfound(Evaluator_Context *ctx, String_View out_var) {
    String_View suffix = nob_sv_from_cstr("-NOTFOUND");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), out_var.count + suffix.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memcpy(buf, out_var.data, out_var.count);
    memcpy(buf + out_var.count, suffix.data, suffix.count);
    buf[out_var.count + suffix.count] = '\0';
    return eval_var_set(ctx, out_var, nob_sv_from_cstr(buf));
}

static bool target_get_declared_dir_temp(Evaluator_Context *ctx, String_View target_name, String_View *out_dir) {
    if (!ctx || !out_dir) return false;
    *out_dir = nob_sv_from_cstr("");
    if (!ctx->stream) return true;

    for (size_t i = 0; i < arena_arr_len(ctx->stream->items); i++) {
        const Cmake_Event *ev = &ctx->stream->items[i];
        if (ev->kind != EV_TARGET_DECLARE) continue;
        if (!nob_sv_eq(ev->as.target_declare.name, target_name)) continue;
        String_View file_path = ev->origin.file_path;
        if (file_path.count == 0) return true;

        size_t end = file_path.count;
        while (end > 0) {
            char c = file_path.data[end - 1];
            if (c != '/' && c != '\\') break;
            end--;
        }
        size_t slash = SIZE_MAX;
        for (size_t si = 0; si < end; si++) {
            char c = file_path.data[si];
            if (c == '/' || c == '\\') slash = si;
        }
        if (slash == SIZE_MAX) *out_dir = nob_sv_from_cstr(".");
        else if (slash == 0) *out_dir = nob_sv_from_cstr("/");
        else if (file_path.data[slash - 1] == ':') *out_dir = nob_sv_from_parts(file_path.data, slash + 1);
        else *out_dir = nob_sv_from_parts(file_path.data, slash);
        return true;
    }

    return true;
}

static bool property_append_unique_temp(Evaluator_Context *ctx, SV_List *list, String_View value) {
    if (!ctx || !list) return false;
    if (value.count == 0) return true;
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        if (eval_sv_key_eq(*list[i], value)) return true;
    }
    return svu_list_push_temp(ctx, list, value);
}

static String_View target_property_from_events_temp(Evaluator_Context *ctx,
                                                    String_View target_name,
                                                    String_View prop_upper,
                                                    bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx || !ctx->stream) return nob_sv_from_cstr("");

    String_View current = nob_sv_from_cstr("");
    bool have = false;

    for (size_t i = 0; i < arena_arr_len(ctx->stream->items); i++) {
        const Cmake_Event *ev = &ctx->stream->items[i];
        String_View incoming = nob_sv_from_cstr("");
        Cmake_Target_Property_Op op = EV_PROP_APPEND_LIST;
        bool matches = false;

        switch (ev->kind) {
            case EV_TARGET_PROP_SET:
                if (!nob_sv_eq(ev->as.target_prop_set.target_name, target_name)) break;
                if (!svu_eq_ci_sv(ev->as.target_prop_set.key, prop_upper)) break;
                incoming = ev->as.target_prop_set.value;
                op = ev->as.target_prop_set.op;
                matches = true;
                break;
            case EV_TARGET_ADD_SOURCE:
                if (!eval_sv_eq_ci_lit(prop_upper, "SOURCES")) break;
                if (!nob_sv_eq(ev->as.target_add_source.target_name, target_name)) break;
                incoming = ev->as.target_add_source.path;
                matches = true;
                break;
            case EV_TARGET_INCLUDE_DIRECTORIES:
                if (!eval_sv_eq_ci_lit(prop_upper, "INCLUDE_DIRECTORIES")) break;
                if (!nob_sv_eq(ev->as.target_include_directories.target_name, target_name)) break;
                incoming = ev->as.target_include_directories.path;
                matches = true;
                break;
            case EV_TARGET_COMPILE_DEFINITIONS:
                if (!eval_sv_eq_ci_lit(prop_upper, "COMPILE_DEFINITIONS")) break;
                if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, target_name)) break;
                incoming = ev->as.target_compile_definitions.item;
                matches = true;
                break;
            case EV_TARGET_COMPILE_OPTIONS:
                if (!eval_sv_eq_ci_lit(prop_upper, "COMPILE_OPTIONS")) break;
                if (!nob_sv_eq(ev->as.target_compile_options.target_name, target_name)) break;
                incoming = ev->as.target_compile_options.item;
                matches = true;
                break;
            case EV_TARGET_LINK_LIBRARIES:
                if (!eval_sv_eq_ci_lit(prop_upper, "LINK_LIBRARIES")) break;
                if (!nob_sv_eq(ev->as.target_link_libraries.target_name, target_name)) break;
                incoming = ev->as.target_link_libraries.item;
                matches = true;
                break;
            case EV_TARGET_LINK_OPTIONS:
                if (!eval_sv_eq_ci_lit(prop_upper, "LINK_OPTIONS")) break;
                if (!nob_sv_eq(ev->as.target_link_options.target_name, target_name)) break;
                incoming = ev->as.target_link_options.item;
                matches = true;
                break;
            case EV_TARGET_LINK_DIRECTORIES:
                if (!eval_sv_eq_ci_lit(prop_upper, "LINK_DIRECTORIES")) break;
                if (!nob_sv_eq(ev->as.target_link_directories.target_name, target_name)) break;
                incoming = ev->as.target_link_directories.path;
                matches = true;
                break;
            default:
                break;
        }

        if (!matches) continue;
        if (!have) {
            current = incoming;
            have = true;
            continue;
        }

        current = merge_property_value_temp(ctx, current, incoming, op);
        if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    }

    if (out_set) *out_set = have;
    return current;
}

static bool property_parent_directory_temp(Evaluator_Context *ctx, String_View dir, String_View *out_parent) {
    if (!ctx || !out_parent) return false;
    *out_parent = nob_sv_from_cstr("");

    String_View norm = eval_sv_path_normalize_temp(ctx, dir);
    if (eval_should_stop(ctx)) return false;
    if (norm.count == 0) return true;
    if (nob_sv_eq(norm, nob_sv_from_cstr("/"))) return true;
    if (norm.count == 3 && norm.data[1] == ':' &&
        (norm.data[2] == '/' || norm.data[2] == '\\')) return true;

    size_t end = norm.count;
    while (end > 0) {
        char c = norm.data[end - 1];
        if (c != '/' && c != '\\') break;
        end--;
    }
    if (end == 0) return true;

    size_t slash = SIZE_MAX;
    for (size_t i = 0; i < end; i++) {
        char c = norm.data[i];
        if (c == '/' || c == '\\') slash = i;
    }
    if (slash == SIZE_MAX) return true;
    if (slash == 0) {
        *out_parent = nob_sv_from_cstr("/");
        return true;
    }
    if (norm.data[slash - 1] == ':') {
        *out_parent = nob_sv_from_parts(norm.data, slash + 1);
        return true;
    }
    *out_parent = nob_sv_from_parts(norm.data, slash);
    return true;
}

static String_View property_value_from_store_temp(Evaluator_Context *ctx,
                                                  String_View scope_upper,
                                                  String_View object_id,
                                                  String_View prop_upper,
                                                  bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
        return target_property_from_events_temp(ctx, object_id, prop_upper, out_set);
    }

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE") &&
        eval_sv_eq_ci_lit(prop_upper, "VALUE") &&
        eval_cache_defined(ctx, object_id)) {
        if (out_set) *out_set = true;
        return eval_var_get(ctx, object_id);
    }

    String_View store_key = eval_property_store_key_temp(ctx, scope_upper, object_id, prop_upper);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (!eval_var_defined(ctx, store_key)) return nob_sv_from_cstr("");
    if (out_set) *out_set = true;
    return eval_var_get(ctx, store_key);
}

static String_View property_value_from_directory_chain_temp(Evaluator_Context *ctx,
                                                            String_View start_dir,
                                                            String_View prop_upper,
                                                            bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View cur = start_dir;
    while (cur.count > 0) {
        bool have = false;
        String_View value = property_value_from_store_temp(ctx,
                                                           nob_sv_from_cstr("DIRECTORY"),
                                                           cur,
                                                           prop_upper,
                                                           &have);
        if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        if (have) {
            if (out_set) *out_set = true;
            return value;
        }

        String_View parent = nob_sv_from_cstr("");
        if (!property_parent_directory_temp(ctx, cur, &parent)) return nob_sv_from_cstr("");
        if (parent.count == 0 || eval_sv_key_eq(parent, cur)) break;
        cur = parent;
    }

    bool have_global = false;
    String_View value = property_value_from_store_temp(ctx,
                                                       nob_sv_from_cstr("GLOBAL"),
                                                       nob_sv_from_cstr(""),
                                                       prop_upper,
                                                       &have_global);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (have_global && out_set) *out_set = true;
    return have_global ? value : nob_sv_from_cstr("");
}

static String_View resolve_property_value_temp(Evaluator_Context *ctx,
                                               String_View scope_upper,
                                               String_View object_id,
                                               String_View prop_name,
                                               bool allow_inherit,
                                               String_View inherit_directory,
                                               bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View prop_upper = eval_property_upper_name_temp(ctx, prop_name);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    bool have = false;
    String_View value = property_value_from_store_temp(ctx, scope_upper, object_id, prop_upper, &have);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (have) {
        if (out_set) *out_set = true;
        return value;
    }
    if (!allow_inherit) return nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) {
        return property_value_from_directory_chain_temp(ctx, object_id, prop_upper, out_set);
    }

    if (eval_sv_eq_ci_lit(scope_upper, "TARGET") ||
        eval_sv_eq_ci_lit(scope_upper, "SOURCE") ||
        eval_sv_eq_ci_lit(scope_upper, "TEST")) {
        String_View base_dir = inherit_directory;
        if (base_dir.count == 0 && eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
            if (!target_get_declared_dir_temp(ctx, object_id, &base_dir)) return nob_sv_from_cstr("");
            if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        }
        if (base_dir.count > 0) {
            return property_value_from_directory_chain_temp(ctx, base_dir, prop_upper, out_set);
        }
    }

    return nob_sv_from_cstr("");
}

static bool property_known_for_scope(Evaluator_Context *ctx,
                                     String_View scope_upper,
                                     String_View object_id,
                                     String_View prop_name,
                                     String_View inherit_directory) {
    if (!ctx) return false;
    if (eval_property_definition_find(ctx, scope_upper, prop_name)) {
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_should_stop(ctx)) return false;

    bool have = false;
    (void)resolve_property_value_temp(ctx,
                                      scope_upper,
                                      object_id,
                                      prop_name,
                                      false,
                                      inherit_directory,
                                      &have);
    if (eval_should_stop(ctx)) return false;
    return have;
}

static bool parse_property_query_mode(Evaluator_Context *ctx,
                                      const Node *node,
                                      Cmake_Event_Origin o,
                                      String_View token,
                                      Eval_Property_Query_Mode *out_mode) {
    if (!ctx || !node || !out_mode) return false;
    if (eval_sv_eq_ci_lit(token, "SET")) {
        *out_mode = PROP_QUERY_SET;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "DEFINED")) {
        *out_mode = PROP_QUERY_DEFINED;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "BRIEF_DOCS")) {
        *out_mode = PROP_QUERY_BRIEF_DOCS;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "FULL_DOCS")) {
        *out_mode = PROP_QUERY_FULL_DOCS;
        return true;
    }

    eval_emit_diag(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("get_property() received unsupported query mode"),
                   token);
    return false;
}

static bool get_property_impl(Evaluator_Context *ctx,
                              const Node *node,
                              Cmake_Event_Origin o,
                              String_View out_var,
                              String_View scope_upper,
                              String_View object_id,
                              String_View prop_name,
                              Eval_Property_Query_Mode mode,
                              String_View inherit_directory,
                              bool validate_object) {
    if (!ctx || !node) return false;

    const Eval_Property_Definition *def = eval_property_definition_find(ctx, scope_upper, prop_name);
    if (eval_should_stop(ctx)) return false;
    bool allow_inherit = def && def->inherited;

    if (mode == PROP_QUERY_DEFINED) {
        return set_output_var_value(ctx,
                                    out_var,
                                    property_known_for_scope(ctx,
                                                             scope_upper,
                                                             object_id,
                                                             prop_name,
                                                             inherit_directory)
                                        ? nob_sv_from_cstr("1")
                                        : nob_sv_from_cstr("0"));
    }

    if (mode == PROP_QUERY_BRIEF_DOCS || mode == PROP_QUERY_FULL_DOCS) {
        if (!def) return set_output_var_notfound(ctx, out_var);
        if (mode == PROP_QUERY_BRIEF_DOCS) {
            return set_output_var_value(ctx,
                                        out_var,
                                        def->has_brief_docs ? def->brief_docs : nob_sv_from_cstr(""));
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    def->has_full_docs ? def->full_docs : nob_sv_from_cstr(""));
    }

    if (validate_object) {
        if (eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
            if (!eval_target_known(ctx, object_id)) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("get_property(TARGET ...) target was not declared"),
                               object_id);
                return !eval_should_stop(ctx);
            }
        } else if (eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
            if (!eval_cache_defined(ctx, object_id)) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("get_property(CACHE ...) cache entry does not exist"),
                               object_id);
                return !eval_should_stop(ctx);
            }
        } else if (eval_sv_eq_ci_lit(scope_upper, "TEST")) {
            String_View test_dir = inherit_directory.count > 0 ? inherit_directory : eval_current_source_dir_for_paths(ctx);
            if (!eval_test_exists_in_directory_scope(ctx, object_id, test_dir)) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("get_property(TEST ...) test was not declared in selected directory scope"),
                               object_id);
                return !eval_should_stop(ctx);
            }
        }
    }

    bool have = false;
    String_View value = resolve_property_value_temp(ctx,
                                                    scope_upper,
                                                    object_id,
                                                    prop_name,
                                                    allow_inherit,
                                                    inherit_directory,
                                                    &have);
    if (eval_should_stop(ctx)) return false;

    if (mode == PROP_QUERY_SET) {
        return set_output_var_value(ctx, out_var, have ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    }

    if (!have) return eval_var_unset(ctx, out_var);
    return set_output_var_value(ctx, out_var, value);
}

bool eval_handle_get_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_property() requires output variable, scope, PROPERTY and property name"),
                       nob_sv_from_cstr("Usage: get_property(<var> <GLOBAL|DIRECTORY|TARGET|SOURCE|INSTALL|TEST|CACHE|VARIABLE> ... PROPERTY <name> [SET|DEFINED|BRIEF_DOCS|FULL_DOCS])"));
        return !eval_should_stop(ctx);
    }

    String_View out_var = a[0];
    String_View scope_upper = nob_sv_from_cstr("");
    if (!eval_property_scope_upper_temp(ctx, a[1], &scope_upper)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_property() unknown scope"),
                       a[1]);
        return !eval_should_stop(ctx);
    }

    size_t i = 2;
    String_View object_token = nob_sv_from_cstr("");
    String_View object_id = nob_sv_from_cstr("");
    String_View inherit_dir = nob_sv_from_cstr("");
    bool saw_source_dir_clause = false;
    bool saw_source_target_dir_clause = false;
    bool saw_test_dir_clause = false;

    if (!(eval_sv_eq_ci_lit(scope_upper, "GLOBAL") || eval_sv_eq_ci_lit(scope_upper, "VARIABLE"))) {
        if (i >= arena_arr_len(a)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("get_property() missing scope object"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) {
            if (!eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
                object_token = a[i++];
            }
        } else {
            object_token = a[i++];
        }
    }

    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        if (object_token.count == 0) object_id = cur_src;
        else object_id = eval_path_resolve_for_cmake_arg(ctx, object_token, cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        inherit_dir = object_id;
    } else if (eval_sv_eq_ci_lit(scope_upper, "SOURCE")) {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
            if (eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
                if (saw_source_dir_clause || saw_source_target_dir_clause || i + 1 >= arena_arr_len(a)) {
                    eval_emit_diag(ctx,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("dispatcher"),
                                   node->as.cmd.name,
                                   o,
                                   nob_sv_from_cstr("get_property(SOURCE DIRECTORY ...) requires exactly one directory"),
                                   nob_sv_from_cstr(""));
                    return !eval_should_stop(ctx);
                }
                inherit_dir = eval_path_resolve_for_cmake_arg(ctx, a[++i], cur_src, true);
                if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
                saw_source_dir_clause = true;
                i++;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "TARGET_DIRECTORY")) {
                if (saw_source_dir_clause || saw_source_target_dir_clause || i + 1 >= arena_arr_len(a)) {
                    eval_emit_diag(ctx,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("dispatcher"),
                                   node->as.cmd.name,
                                   o,
                                   nob_sv_from_cstr("get_property(SOURCE TARGET_DIRECTORY ...) requires exactly one target"),
                                   nob_sv_from_cstr(""));
                    return !eval_should_stop(ctx);
                }
                String_View target_name = a[++i];
                if (!eval_target_known(ctx, target_name)) {
                    eval_emit_diag(ctx,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("dispatcher"),
                                   node->as.cmd.name,
                                   o,
                                   nob_sv_from_cstr("get_property(SOURCE TARGET_DIRECTORY ...) target was not declared"),
                                   target_name);
                    return !eval_should_stop(ctx);
                }
                if (!target_get_declared_dir_temp(ctx, target_name, &inherit_dir)) return !eval_should_stop(ctx);
                if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
                object_id = eval_property_scoped_object_id_temp(ctx, "TARGET_DIRECTORY", target_name, object_token);
                if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
                saw_source_target_dir_clause = true;
                i++;
                continue;
            }
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("get_property(SOURCE ...) received unexpected argument before PROPERTY"),
                           a[i]);
            return !eval_should_stop(ctx);
        }
        if (!saw_source_target_dir_clause) {
            if (saw_source_dir_clause) object_id = eval_property_scoped_object_id_temp(ctx, "DIRECTORY", inherit_dir, object_token);
            else {
                object_id = object_token;
                inherit_dir = cur_src;
            }
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        }
    } else if (eval_sv_eq_ci_lit(scope_upper, "TEST")) {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
            if (!eval_sv_eq_ci_lit(a[i], "DIRECTORY") || saw_test_dir_clause || i + 1 >= arena_arr_len(a)) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("get_property(TEST ...) received invalid DIRECTORY clause"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            inherit_dir = eval_path_resolve_for_cmake_arg(ctx, a[++i], cur_src, true);
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
            saw_test_dir_clause = true;
            i++;
        }
        if (saw_test_dir_clause) object_id = eval_property_scoped_object_id_temp(ctx, "DIRECTORY", inherit_dir, object_token);
        else {
            object_id = object_token;
            inherit_dir = cur_src;
        }
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    } else if (eval_sv_eq_ci_lit(scope_upper, "TARGET") ||
               eval_sv_eq_ci_lit(scope_upper, "INSTALL") ||
               eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
        object_id = object_token;
    }

    if (i >= arena_arr_len(a) || !eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_property() missing PROPERTY keyword"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    i++;
    if (i >= arena_arr_len(a)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_property() missing property name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    String_View prop_name = a[i++];

    Eval_Property_Query_Mode mode = PROP_QUERY_VALUE;
    if (i < arena_arr_len(a)) {
        if (!parse_property_query_mode(ctx, node, o, a[i++], &mode)) return !eval_should_stop(ctx);
    }
    if (i != arena_arr_len(a)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_property() received unexpected trailing arguments"),
                       a[i]);
        return !eval_should_stop(ctx);
    }

    if ((mode == PROP_QUERY_VALUE || mode == PROP_QUERY_SET) &&
        eval_sv_eq_ci_lit(scope_upper, "TARGET") &&
        !eval_target_known(ctx, object_token)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_property(TARGET ...) target was not declared"),
                       object_token);
        return !eval_should_stop(ctx);
    }
    if ((mode == PROP_QUERY_VALUE || mode == PROP_QUERY_SET) &&
        eval_sv_eq_ci_lit(scope_upper, "CACHE") &&
        !eval_cache_defined(ctx, object_token)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_property(CACHE ...) cache entry does not exist"),
                       object_token);
        return !eval_should_stop(ctx);
    }
    if ((mode == PROP_QUERY_VALUE || mode == PROP_QUERY_SET) &&
        eval_sv_eq_ci_lit(scope_upper, "TEST")) {
        String_View test_dir = inherit_dir.count > 0 ? inherit_dir : eval_current_source_dir_for_paths(ctx);
        if (!eval_test_exists_in_directory_scope(ctx, object_token, test_dir)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("get_property(TEST ...) test was not declared in selected directory scope"),
                           object_token);
            return !eval_should_stop(ctx);
        }
    }

    if (!get_property_impl(ctx,
                           node,
                           o,
                           out_var,
                           scope_upper,
                           object_id,
                           prop_name,
                           mode,
                           inherit_dir,
                           false)) {
        return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_get_cmake_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) != 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_cmake_property() expects output variable and property name"),
                       nob_sv_from_cstr("Usage: get_cmake_property(<var> <property>)"));
        return !eval_should_stop(ctx);
    }

    String_View out_var = a[0];
    String_View prop = a[1];
    SV_List values = NULL;

    if (eval_sv_eq_ci_lit(prop, "VARIABLES")) {
        for (size_t depth = 0; depth < eval_scope_visible_depth(ctx); depth++) {
            Var_Scope *scope = &ctx->scopes[depth];
            ptrdiff_t n = stbds_shlen(scope->vars);
            for (ptrdiff_t i = 0; i < n; i++) {
                if (!scope->vars[i].key) continue;
                if (!property_append_unique_temp(ctx, &values, nob_sv_from_cstr(scope->vars[i].key))) {
                    return !eval_should_stop(ctx);
                }
            }
        }
        return set_output_var_value(ctx, out_var, eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(prop, "CACHE_VARIABLES")) {
        ptrdiff_t n = stbds_shlen(ctx->cache_entries);
        for (ptrdiff_t i = 0; i < n; i++) {
            if (!ctx->cache_entries[i].key) continue;
            if (!property_append_unique_temp(ctx, &values, nob_sv_from_cstr(ctx->cache_entries[i].key))) {
                return !eval_should_stop(ctx);
            }
        }
        return set_output_var_value(ctx, out_var, eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(prop, "MACROS")) {
        for (size_t i = 0; i < arena_arr_len(ctx->user_commands); i++) {
            const User_Command *cmd = &ctx->user_commands[i];
            if (cmd->kind != USER_CMD_MACRO) continue;
            if (!property_append_unique_temp(ctx, &values, cmd->name)) return !eval_should_stop(ctx);
        }
        return set_output_var_value(ctx, out_var, eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(prop, "COMPONENTS")) {
        if (ctx->stream) {
            for (size_t i = 0; i < arena_arr_len(ctx->stream->items); i++) {
                const Cmake_Event *ev = &ctx->stream->items[i];
                if (ev->kind != EV_CPACK_ADD_COMPONENT) continue;
                if (!property_append_unique_temp(ctx, &values, ev->as.cpack_add_component.name)) {
                    return !eval_should_stop(ctx);
                }
            }
        }
        return set_output_var_value(ctx, out_var, eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (!get_property_impl(ctx,
                           node,
                           o,
                           out_var,
                           nob_sv_from_cstr("GLOBAL"),
                           nob_sv_from_cstr(""),
                           prop,
                           PROP_QUERY_VALUE,
                           nob_sv_from_cstr(""),
                           false)) {
        return !eval_should_stop(ctx);
    }
    if (!eval_var_defined(ctx, out_var)) return set_output_var_notfound(ctx, out_var);
    return !eval_should_stop(ctx);
}

bool eval_handle_get_directory_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_directory_property() requires output variable and query"),
                       nob_sv_from_cstr("Usage: get_directory_property(<var> [DIRECTORY <dir>] <prop-name>|DEFINITION <var-name>)"));
        return !eval_should_stop(ctx);
    }

    String_View out_var = a[0];
    size_t i = 1;
    String_View dir = eval_current_source_dir_for_paths(ctx);
    if (eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
        if (i + 1 >= arena_arr_len(a)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("get_directory_property(DIRECTORY ...) requires a directory"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        dir = eval_path_resolve_for_cmake_arg(ctx, a[i + 1], dir, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        i += 2;
    }
    if (i >= arena_arr_len(a)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_directory_property() missing property query"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a[i], "DEFINITION")) {
        if (i + 1 >= arena_arr_len(a)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("get_directory_property(DEFINITION ...) requires a variable name"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        if (i + 2 != arena_arr_len(a)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("get_directory_property(DEFINITION ...) expects exactly one variable name"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        String_View value = eval_var_get(ctx, a[i + 1]);
        return set_output_var_value(ctx, out_var, value);
    }

    if (i + 1 != arena_arr_len(a)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_directory_property() received unexpected trailing arguments"),
                       a[i + 1]);
        return !eval_should_stop(ctx);
    }

    if (!get_property_impl(ctx,
                           node,
                           o,
                           out_var,
                           nob_sv_from_cstr("DIRECTORY"),
                           dir,
                           a[i],
                           PROP_QUERY_VALUE,
                           dir,
                           false)) {
        return !eval_should_stop(ctx);
    }
    if (!eval_var_defined(ctx, out_var)) return set_output_var_value(ctx, out_var, nob_sv_from_cstr(""));
    return !eval_should_stop(ctx);
}

bool eval_handle_get_source_file_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) != 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_source_file_property() expects output variable, source and property"),
                       nob_sv_from_cstr("Usage: get_source_file_property(<var> <source> <property>)"));
        return !eval_should_stop(ctx);
    }

    String_View inherit_dir = eval_current_source_dir_for_paths(ctx);
    if (!get_property_impl(ctx,
                           node,
                           o,
                           a[0],
                           nob_sv_from_cstr("SOURCE"),
                           a[1],
                           a[2],
                           PROP_QUERY_VALUE,
                           inherit_dir,
                           false)) {
        return !eval_should_stop(ctx);
    }
    if (!eval_var_defined(ctx, a[0])) return set_output_var_notfound(ctx, a[0]);
    return !eval_should_stop(ctx);
}

bool eval_handle_get_target_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) != 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_target_property() expects output variable, target and property"),
                       nob_sv_from_cstr("Usage: get_target_property(<var> <target> <property>)"));
        return !eval_should_stop(ctx);
    }
    if (!eval_target_known(ctx, a[1])) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_target_property() target was not declared"),
                       a[1]);
        return !eval_should_stop(ctx);
    }

    if (!get_property_impl(ctx,
                           node,
                           o,
                           a[0],
                           nob_sv_from_cstr("TARGET"),
                           a[1],
                           a[2],
                           PROP_QUERY_VALUE,
                           nob_sv_from_cstr(""),
                           false)) {
        return !eval_should_stop(ctx);
    }
    if (!eval_var_defined(ctx, a[0])) return set_output_var_notfound(ctx, a[0]);
    return !eval_should_stop(ctx);
}

bool eval_handle_get_test_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) != 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_test_property() expects output variable, test and property"),
                       nob_sv_from_cstr("Usage: get_test_property(<var> <test> <property>)"));
        return !eval_should_stop(ctx);
    }

    String_View test_dir = eval_current_source_dir_for_paths(ctx);
    if (!eval_test_exists_in_directory_scope(ctx, a[1], test_dir)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("get_test_property() test was not declared in current directory scope"),
                       a[1]);
        return !eval_should_stop(ctx);
    }

    if (!get_property_impl(ctx,
                           node,
                           o,
                           a[0],
                           nob_sv_from_cstr("TEST"),
                           a[1],
                           a[2],
                           PROP_QUERY_VALUE,
                           test_dir,
                           false)) {
        return !eval_should_stop(ctx);
    }
    if (!eval_var_defined(ctx, a[0])) return set_output_var_notfound(ctx, a[0]);
    return !eval_should_stop(ctx);
}

bool eval_handle_set_directory_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 3 || !eval_sv_eq_ci_lit(a[0], "PROPERTIES")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_directory_properties() requires PROPERTIES key/value pairs"),
                       nob_sv_from_cstr("Usage: set_directory_properties(PROPERTIES <k> <v> [<k> <v>...])"));
        return !eval_should_stop(ctx);
    }
    if (((arena_arr_len(a) - 1) % 2) != 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_directory_properties() has dangling property key without value"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View current_dir = eval_current_source_dir_for_paths(ctx);
    String_View scope_upper = nob_sv_from_cstr("DIRECTORY");
    for (size_t i = 1; i + 1 < arena_arr_len(a); i += 2) {
        if (!set_non_target_property(ctx, o, scope_upper, current_dir, a[i], a[i + 1], EV_PROP_SET)) {
            return !eval_should_stop(ctx);
        }
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_set_source_files_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_source_files_properties() requires files and PROPERTIES pairs"),
                       nob_sv_from_cstr("Usage: set_source_files_properties(<file>... [DIRECTORY <dir>...] [TARGET_DIRECTORY <target>...] PROPERTIES <k> <v> [<k> <v>...])"));
        return !eval_should_stop(ctx);
    }

    SV_List files = NULL;
    SV_List dirs = NULL;
    SV_List target_dirs = NULL;
    enum {
        SF_PARSE_FILES = 0,
        SF_PARSE_DIRS,
        SF_PARSE_TARGET_DIRS,
    } mode = SF_PARSE_FILES;

    size_t i = 0;
    for (; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PROPERTIES")) break;
        if (eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
            mode = SF_PARSE_DIRS;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "TARGET_DIRECTORY")) {
            mode = SF_PARSE_TARGET_DIRS;
            continue;
        }

        if (mode == SF_PARSE_FILES) {
            if (!svu_list_push_temp(ctx, &files, a[i])) return !eval_should_stop(ctx);
        } else if (mode == SF_PARSE_DIRS) {
            if (!svu_list_push_temp(ctx, &dirs, a[i])) return !eval_should_stop(ctx);
        } else {
            if (!svu_list_push_temp(ctx, &target_dirs, a[i])) return !eval_should_stop(ctx);
        }
    }

    if (arena_arr_len(files) == 0 || i >= arena_arr_len(a)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_source_files_properties() requires at least one source file and PROPERTIES"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    i++;
    if (i >= arena_arr_len(a) || ((arena_arr_len(a) - i) % 2) != 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_source_files_properties() has invalid PROPERTIES key/value pairs"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    for (size_t di = 0; di < arena_arr_len(dirs); di++) {
        dirs[di] = eval_path_resolve_for_cmake_arg(ctx, dirs[di], cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    }
    for (size_t ti = 0; ti < arena_arr_len(target_dirs); ti++) {
        if (!eval_target_known(ctx, target_dirs[ti])) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("set_source_files_properties(TARGET_DIRECTORY ...) target was not declared"),
                           target_dirs[ti]);
            return !eval_should_stop(ctx);
        }
    }

    String_View scope_upper = nob_sv_from_cstr("SOURCE");
    for (size_t fi = 0; fi < arena_arr_len(files); fi++) {
        if (arena_arr_len(dirs) == 0 && arena_arr_len(target_dirs) == 0) {
            for (size_t pi = i; pi + 1 < arena_arr_len(a); pi += 2) {
                if (!set_non_target_property(ctx, o, scope_upper, files[fi], a[pi], a[pi + 1], EV_PROP_SET)) {
                    return !eval_should_stop(ctx);
                }
            }
            continue;
        }

        for (size_t di = 0; di < arena_arr_len(dirs); di++) {
            String_View object_id = eval_property_scoped_object_id_temp(ctx, "DIRECTORY", dirs[di], files[fi]);
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
            for (size_t pi = i; pi + 1 < arena_arr_len(a); pi += 2) {
                if (!set_non_target_property(ctx, o, scope_upper, object_id, a[pi], a[pi + 1], EV_PROP_SET)) {
                    return !eval_should_stop(ctx);
                }
            }
        }

        for (size_t ti = 0; ti < arena_arr_len(target_dirs); ti++) {
            String_View object_id = eval_property_scoped_object_id_temp(ctx,
                                                                        "TARGET_DIRECTORY",
                                                                        target_dirs[ti],
                                                                        files[fi]);
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
            for (size_t pi = i; pi + 1 < arena_arr_len(a); pi += 2) {
                if (!set_non_target_property(ctx, o, scope_upper, object_id, a[pi], a[pi + 1], EV_PROP_SET)) {
                    return !eval_should_stop(ctx);
                }
            }
        }
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_set_tests_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_tests_properties() requires tests and PROPERTIES pairs"),
                       nob_sv_from_cstr("Usage: set_tests_properties(<test>... [DIRECTORY <dir>] PROPERTIES <k> <v> [<k> <v>...])"));
        return !eval_should_stop(ctx);
    }

    SV_List tests = NULL;
    size_t i = 0;
    while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "DIRECTORY") && !eval_sv_eq_ci_lit(a[i], "PROPERTIES")) {
        if (!svu_list_push_temp(ctx, &tests, a[i])) return !eval_should_stop(ctx);
        i++;
    }

    if (arena_arr_len(tests) == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_tests_properties() requires at least one test name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View test_dir = eval_current_source_dir_for_paths(ctx);
    bool saw_directory_clause = false;
    if (i < arena_arr_len(a) && eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
        if (i + 1 >= arena_arr_len(a)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("set_tests_properties(DIRECTORY ...) requires a directory"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        test_dir = eval_path_resolve_for_cmake_arg(ctx, a[i + 1], test_dir, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        saw_directory_clause = true;
        i += 2;
    }

    if (i >= arena_arr_len(a) || !eval_sv_eq_ci_lit(a[i], "PROPERTIES")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_tests_properties() missing PROPERTIES keyword"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    i++;
    if (i >= arena_arr_len(a) || ((arena_arr_len(a) - i) % 2) != 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_tests_properties() has invalid PROPERTIES key/value pairs"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    for (size_t ti = 0; ti < arena_arr_len(tests); ti++) {
        if (eval_test_exists_in_directory_scope(ctx, tests[ti], test_dir)) continue;
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_tests_properties() test was not declared in selected directory scope"),
                       tests[ti]);
        return !eval_should_stop(ctx);
    }

    String_View scope_upper = nob_sv_from_cstr("TEST");
    for (size_t ti = 0; ti < arena_arr_len(tests); ti++) {
        String_View object_id = tests[ti];
        if (saw_directory_clause) {
            object_id = eval_property_scoped_object_id_temp(ctx, "DIRECTORY", test_dir, tests[ti]);
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        }

        for (size_t pi = i; pi + 1 < arena_arr_len(a); pi += 2) {
            if (!set_non_target_property(ctx, o, scope_upper, object_id, a[pi], a[pi + 1], EV_PROP_SET)) {
                return !eval_should_stop(ctx);
            }
        }
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_add_dependencies(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_dependencies() requires target and at least one dependency"),
                       nob_sv_from_cstr("Usage: add_dependencies(<target> <target-dependency>...)"));
        return !eval_should_stop(ctx);
    }

    String_View target_name = a[0];
    if (!eval_target_known(ctx, target_name)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_dependencies() target was not declared"),
                       target_name);
        return !eval_should_stop(ctx);
    }
    if (eval_target_alias_known(ctx, target_name)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_dependencies() cannot be used on ALIAS targets"),
                       target_name);
        return !eval_should_stop(ctx);
    }

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        String_View dep = a[i];
        if (dep.count == 0) continue;

        if (!eval_target_known(ctx, dep)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_dependencies() dependency target was not declared"),
                           dep);
            return !eval_should_stop(ctx);
        }
        if (eval_target_alias_known(ctx, dep)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_dependencies() cannot depend on ALIAS targets"),
                           dep);
            return !eval_should_stop(ctx);
        }
        if (!emit_target_dependency(ctx, o, target_name, dep)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_target_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || arena_arr_len(a) < 2) return !eval_should_stop(ctx);

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View qualifier = nob_sv_from_cstr("");

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "DEBUG") ||
            eval_sv_eq_ci_lit(a[i], "OPTIMIZED") ||
            eval_sv_eq_ci_lit(a[i], "GENERAL")) {
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

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_LIBRARIES;
        ev.origin = o;
        ev.as.target_link_libraries.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_libraries.visibility = vis;
        ev.as.target_link_libraries.item = sv_copy_to_event_arena(ctx, item);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
        qualifier = nob_sv_from_cstr("");
    }

    if (qualifier.count > 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_link_libraries() qualifier without following item"),
                       qualifier);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_target_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_link_options() requires target and items"),
                       nob_sv_from_cstr("Usage: target_link_options(<tgt> [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_before = false;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_OPTIONS;
        ev.origin = o;
        ev.as.target_link_options.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_options.visibility = vis;
        ev.as.target_link_options.item = sv_copy_to_event_arena(ctx, a[i]);
        ev.as.target_link_options.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_target_link_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_link_directories() requires target and items"),
                       nob_sv_from_cstr("Usage: target_link_directories(<tgt> <PUBLIC|PRIVATE|INTERFACE> <dirs...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_DIRECTORIES;
        ev.origin = o;
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        ev.as.target_link_directories.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_directories.visibility = vis;
        ev.as.target_link_directories.path = sv_copy_to_event_arena(ctx, resolved);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_target_include_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_include_directories() requires target and items"),
                       nob_sv_from_cstr("Usage: target_include_directories(<tgt> [SYSTEM] [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_system = false;
    bool is_before = false;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);

    for (size_t i = 1; i < arena_arr_len(a); i++) {
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
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_INCLUDE_DIRECTORIES;
        ev.origin = o;
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        ev.as.target_include_directories.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_include_directories.visibility = vis;
        ev.as.target_include_directories.path = sv_copy_to_event_arena(ctx, resolved);
        ev.as.target_include_directories.is_system = is_system;
        ev.as.target_include_directories.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_target_compile_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_compile_definitions() requires target and items"),
                       nob_sv_from_cstr("Usage: target_compile_definitions(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        String_View item = eval_normalize_compile_definition_item(a[i]);
        if (item.count == 0) continue;

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_COMPILE_DEFINITIONS;
        ev.origin = o;
        ev.as.target_compile_definitions.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_compile_definitions.visibility = vis;
        ev.as.target_compile_definitions.item = sv_copy_to_event_arena(ctx, item);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_target_compile_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_compile_options() requires target and items"),
                       nob_sv_from_cstr("Usage: target_compile_options(<tgt> [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_before = false;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.target_compile_options.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_compile_options.visibility = vis;
        ev.as.target_compile_options.item = sv_copy_to_event_arena(ctx, a[i]);
        ev.as.target_compile_options.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_target_sources(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_sources() requires target, visibility and items"),
                       nob_sv_from_cstr("Usage: target_sources(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (eval_sv_eq_ci_lit(a[i], "FILE_SET")) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("target_sources(FILE_SET ...) is not implemented yet"),
                           nob_sv_from_cstr("Supported in this batch: source item groups with PRIVATE, PUBLIC and INTERFACE"));
            return !eval_should_stop(ctx);
        }
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return !eval_should_stop(ctx);
        }

        String_View item = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!emit_target_add_source(ctx, o, tgt, item)) return !eval_should_stop(ctx);
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!emit_target_prop_set(ctx,
                                      o,
                                      tgt,
                                      nob_sv_from_cstr("INTERFACE_SOURCES"),
                                      item,
                                      EV_PROP_APPEND_LIST)) {
                return !eval_should_stop(ctx);
            }
        }
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_target_compile_features(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_compile_features() requires target, visibility and features"),
                       nob_sv_from_cstr("Usage: target_compile_features(<tgt> <PUBLIC|PRIVATE|INTERFACE> <features...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return !eval_should_stop(ctx);
        }
        if (a[i].count == 0) continue;

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!emit_target_prop_set(ctx,
                                      o,
                                      tgt,
                                      nob_sv_from_cstr("COMPILE_FEATURES"),
                                      a[i],
                                      EV_PROP_APPEND_LIST)) {
                return !eval_should_stop(ctx);
            }
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!emit_target_prop_set(ctx,
                                      o,
                                      tgt,
                                      nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES"),
                                      a[i],
                                      EV_PROP_APPEND_LIST)) {
                return !eval_should_stop(ctx);
            }
        }
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_target_precompile_headers(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_precompile_headers() requires target and arguments"),
                       nob_sv_from_cstr("Usage: target_precompile_headers(<tgt> <PUBLIC|PRIVATE|INTERFACE> <headers...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return !eval_should_stop(ctx);

    if (eval_sv_eq_ci_lit(a[1], "REUSE_FROM")) {
        if (arena_arr_len(a) != 3) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("target_precompile_headers(REUSE_FROM) expects exactly one donor target"),
                           nob_sv_from_cstr("Usage: target_precompile_headers(<tgt> REUSE_FROM <other-target>)"));
            return !eval_should_stop(ctx);
        }
        if (!target_usage_validate_target(ctx, node, a[2])) return !eval_should_stop(ctx);
        if (!emit_target_prop_set(ctx,
                                  o,
                                  tgt,
                                  nob_sv_from_cstr("PRECOMPILE_HEADERS_REUSE_FROM"),
                                  a[2],
                                  EV_PROP_SET)) {
            return !eval_should_stop(ctx);
        }
        if (!eval_sv_key_eq(tgt, a[2])) {
            if (!emit_target_dependency(ctx, o, tgt, a[2])) return !eval_should_stop(ctx);
        }
        return !eval_should_stop(ctx);
    }

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return !eval_should_stop(ctx);
        }

        String_View item = target_pch_item_normalize_temp(ctx, a[i]);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        if (item.count == 0) continue;

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!emit_target_prop_set(ctx,
                                      o,
                                      tgt,
                                      nob_sv_from_cstr("PRECOMPILE_HEADERS"),
                                      item,
                                      EV_PROP_APPEND_LIST)) {
                return !eval_should_stop(ctx);
            }
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!emit_target_prop_set(ctx,
                                      o,
                                      tgt,
                                      nob_sv_from_cstr("INTERFACE_PRECOMPILE_HEADERS"),
                                      item,
                                      EV_PROP_APPEND_LIST)) {
                return !eval_should_stop(ctx);
            }
        }
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_source_group(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("source_group() requires a group descriptor and mapping input"),
                       nob_sv_from_cstr("Usage: source_group(<name> [FILES <src>...]) or source_group(TREE <root> [PREFIX <prefix>] FILES <src>...)"));
        return !eval_should_stop(ctx);
    }

    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    if (eval_sv_eq_ci_lit(a[0], "TREE")) {
        if (arena_arr_len(a) < 4) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("source_group(TREE ...) requires root and FILES list"),
                           nob_sv_from_cstr("Usage: source_group(TREE <root> [PREFIX <prefix>] FILES <src>...)"));
            return !eval_should_stop(ctx);
        }

        String_View root = eval_path_resolve_for_cmake_arg(ctx, a[1], cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        String_View prefix = nob_sv_from_cstr("");
        size_t files_index = 2;
        if (files_index < arena_arr_len(a) && eval_sv_eq_ci_lit(a[files_index], "PREFIX")) {
            if (files_index + 1 >= arena_arr_len(a)) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("source_group(TREE ... PREFIX) requires a value"),
                               nob_sv_from_cstr("Usage: source_group(TREE <root> PREFIX <prefix> FILES <src>...)"));
                return !eval_should_stop(ctx);
            }
            prefix = a[files_index + 1];
            files_index += 2;
        }
        if (files_index >= arena_arr_len(a) || !eval_sv_eq_ci_lit(a[files_index], "FILES") || files_index + 1 >= arena_arr_len(a)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("source_group(TREE ...) requires FILES followed by at least one source"),
                           nob_sv_from_cstr("Usage: source_group(TREE <root> [PREFIX <prefix>] FILES <src>...)"));
            return !eval_should_stop(ctx);
        }

        for (size_t i = files_index + 1; i < arena_arr_len(a); i++) {
            String_View file_path = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
            String_View relative = nob_sv_from_cstr("");
            if (!source_group_path_relative_to_root(ctx, root, file_path, &relative)) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("source_group(TREE ...) file is outside the declared tree root"),
                               file_path);
                return !eval_should_stop(ctx);
            }
            String_View group_name = source_group_join_tree_name_temp(ctx, prefix, source_group_dirname(relative));
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
            if (!source_group_emit_assignment(ctx, o, file_path, group_name)) return !eval_should_stop(ctx);
        }
        return !eval_should_stop(ctx);
    }

    String_View group_name = a[0];
    bool have_files = false;
    bool have_regex = false;
    String_View regex_value = nob_sv_from_cstr("");
    SV_List files = NULL;

    if (arena_arr_len(a) == 2 &&
        !eval_sv_eq_ci_lit(a[1], "FILES") &&
        !eval_sv_eq_ci_lit(a[1], "REGULAR_EXPRESSION")) {
        have_regex = true;
        regex_value = a[1];
    } else {
        size_t i = 1;
        while (i < arena_arr_len(a)) {
            if (eval_sv_eq_ci_lit(a[i], "FILES")) {
                i++;
                if (i >= arena_arr_len(a) || eval_sv_eq_ci_lit(a[i], "REGULAR_EXPRESSION")) {
                    eval_emit_diag(ctx,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("dispatcher"),
                                   node->as.cmd.name,
                                   o,
                                   nob_sv_from_cstr("source_group(FILES) requires at least one source"),
                                   nob_sv_from_cstr("Usage: source_group(<name> [FILES <src>...] [REGULAR_EXPRESSION <regex>])"));
                    return !eval_should_stop(ctx);
                }
                while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "REGULAR_EXPRESSION")) {
                    if (!svu_list_push_temp(ctx, &files, a[i])) return !eval_should_stop(ctx);
                    i++;
                }
                have_files = arena_arr_len(files) > 0;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "REGULAR_EXPRESSION")) {
                if (have_regex || i + 1 >= arena_arr_len(a)) {
                    eval_emit_diag(ctx,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("dispatcher"),
                                   node->as.cmd.name,
                                   o,
                                   nob_sv_from_cstr("source_group(REGULAR_EXPRESSION) requires exactly one regex"),
                                   nob_sv_from_cstr("Usage: source_group(<name> [FILES <src>...] [REGULAR_EXPRESSION <regex>])"));
                    return !eval_should_stop(ctx);
                }
                have_regex = true;
                regex_value = a[i + 1];
                i += 2;
                continue;
            }

            if (have_files) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("source_group() received unexpected argument"),
                               a[i]);
                return !eval_should_stop(ctx);
            }

            if (!svu_list_push_temp(ctx, &files, a[i])) return !eval_should_stop(ctx);
            have_files = arena_arr_len(files) > 0;
            i++;
        }
    }

    if (!have_files && !have_regex) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("source_group() requires source files and/or a regular expression"),
                       nob_sv_from_cstr("Usage: source_group(<name> [FILES <src>...] [REGULAR_EXPRESSION <regex>])"));
        return !eval_should_stop(ctx);
    }

    if (have_regex) {
        if (!source_group_emit_regex_rule(ctx, o, group_name, regex_value)) return !eval_should_stop(ctx);
    }

    for (size_t i = 0; i < arena_arr_len(files); i++) {
        String_View file_path = eval_path_resolve_for_cmake_arg(ctx, files[i], cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        if (!source_group_emit_assignment(ctx, o, file_path, group_name)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_define_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("define_property() requires scope and PROPERTY name"),
                       nob_sv_from_cstr("Usage: define_property(<GLOBAL|DIRECTORY|TARGET|SOURCE|TEST|VARIABLE|CACHED_VARIABLE> PROPERTY <name> [INHERITED] [BRIEF_DOCS <docs>...] [FULL_DOCS <docs>...] [INITIALIZE_FROM_VARIABLE <var>])"));
        return !eval_should_stop(ctx);
    }

    String_View scope_upper = nob_sv_from_cstr("");
    if (!eval_property_scope_upper_temp(ctx, a[0], &scope_upper)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("define_property() unknown scope"),
                       a[0]);
        return !eval_should_stop(ctx);
    }

    if (!eval_sv_eq_ci_lit(a[1], "PROPERTY")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("define_property() missing PROPERTY keyword"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    Eval_Property_Definition def = {0};
    def.scope_upper = scope_upper;
    def.property_upper = eval_property_upper_name_temp(ctx, a[2]);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (def.property_upper.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("define_property() missing property name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    size_t i = 3;
    while (i < arena_arr_len(a)) {
        if (eval_sv_eq_ci_lit(a[i], "INHERITED")) {
            def.inherited = true;
            i++;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "BRIEF_DOCS")) {
            size_t start = ++i;
            while (i < arena_arr_len(a) && !define_property_keyword(a[i])) i++;
            def.has_brief_docs = true;
            if (i > start) {
                def.brief_docs = eval_sv_join_semi_temp(ctx, &a[start], i - start);
                if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "FULL_DOCS")) {
            size_t start = ++i;
            while (i < arena_arr_len(a) && !define_property_keyword(a[i])) i++;
            def.has_full_docs = true;
            if (i > start) {
                def.full_docs = eval_sv_join_semi_temp(ctx, &a[start], i - start);
                if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INITIALIZE_FROM_VARIABLE")) {
            if (!eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("define_property(INITIALIZE_FROM_VARIABLE) is only valid for TARGET scope"),
                               scope_upper);
                return !eval_should_stop(ctx);
            }
            if (def.has_initialize_from_variable) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("define_property() received duplicate INITIALIZE_FROM_VARIABLE"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            if (i + 1 >= arena_arr_len(a) || define_property_keyword(a[i + 1])) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("define_property(INITIALIZE_FROM_VARIABLE) requires a variable name"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            def.has_initialize_from_variable = true;
            def.initialize_from_variable = a[i + 1];
            i += 2;
            continue;
        }

        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("define_property() received unexpected argument"),
                       a[i]);
        return !eval_should_stop(ctx);
    }

    if (!eval_property_define(ctx, &def)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

bool eval_handle_set_target_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() requires targets and PROPERTIES key/value pairs"),
                       nob_sv_from_cstr("Usage: set_target_properties(<t1> [<t2> ...] PROPERTIES <k1> <v1> ...)"));
        return !eval_should_stop(ctx);
    }

    size_t props_i = arena_arr_len(a);
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PROPERTIES")) {
            props_i = i;
            break;
        }
    }

    if (props_i == 0 || props_i >= arena_arr_len(a) - 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() missing PROPERTIES section"),
                       nob_sv_from_cstr("Expected: set_target_properties(<targets...> PROPERTIES <key> <value> ...)"));
        return !eval_should_stop(ctx);
    }

    size_t kv_start = props_i + 1;
    size_t kv_count = arena_arr_len(a) - kv_start;
    if (kv_count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() missing property key/value"),
                       nob_sv_from_cstr("Provide at least one <key> <value> pair"));
        return !eval_should_stop(ctx);
    }

    if ((kv_count % 2) != 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() has dangling property key without value"),
                       nob_sv_from_cstr("Ignoring the last unmatched key"));
    }

    for (size_t ti = 0; ti < props_i; ti++) {
        String_View tgt = a[ti];
        if (!eval_target_known(ctx, tgt)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("set_target_properties() target was not declared"),
                           tgt);
            continue;
        }
        if (eval_target_alias_known(ctx, tgt)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("set_target_properties() cannot be used on ALIAS targets"),
                           tgt);
            continue;
        }
        for (size_t i = kv_start; i + 1 < arena_arr_len(a); i += 2) {
            if (!emit_target_prop_set(ctx, o, tgt, a[i], a[i + 1], EV_PROP_SET)) {
                return !eval_should_stop(ctx);
            }
        }
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_set_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() missing scope"),
                       nob_sv_from_cstr("Usage: set_property(<GLOBAL|DIRECTORY|TARGET|SOURCE|INSTALL|TEST|CACHE> ... PROPERTY <k> [v...])"));
        return !eval_should_stop(ctx);
    }

    String_View scope = a[0];
    bool is_target_scope = eval_sv_eq_ci_lit(scope, "TARGET");
    bool is_global_scope = eval_sv_eq_ci_lit(scope, "GLOBAL");
    bool is_dir_scope = eval_sv_eq_ci_lit(scope, "DIRECTORY");
    bool is_source_scope = eval_sv_eq_ci_lit(scope, "SOURCE");
    bool is_install_scope = eval_sv_eq_ci_lit(scope, "INSTALL");
    bool is_test_scope = eval_sv_eq_ci_lit(scope, "TEST");
    bool is_cache_scope = eval_sv_eq_ci_lit(scope, "CACHE");
    if (!(is_target_scope || is_global_scope || is_dir_scope || is_source_scope || is_install_scope || is_test_scope || is_cache_scope)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() unknown scope"),
                       scope);
        return !eval_should_stop(ctx);
    }

    bool append = false;
    bool append_string = false;
    SV_List objects = NULL;
    SV_List source_dirs = NULL;
    SV_List source_target_dirs = NULL;
    SV_List test_dirs = NULL;
    bool saw_source_directory_clause = false;
    bool saw_source_target_directory_clause = false;
    bool saw_test_directory_clause = false;

    enum {
        SP_PARSE_OBJECTS = 0,
        SP_PARSE_SOURCE_DIRS,
        SP_PARSE_SOURCE_TARGET_DIRS,
        SP_PARSE_TEST_DIRS,
    } parse_mode = SP_PARSE_OBJECTS;

    size_t i = 1;
    for (; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PROPERTY")) break;
        if (eval_sv_eq_ci_lit(a[i], "APPEND")) {
            append = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "APPEND_STRING")) {
            append_string = true;
            continue;
        }
        if (is_source_scope && eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
            saw_source_directory_clause = true;
            parse_mode = SP_PARSE_SOURCE_DIRS;
            continue;
        }
        if (is_source_scope && eval_sv_eq_ci_lit(a[i], "TARGET_DIRECTORY")) {
            saw_source_target_directory_clause = true;
            parse_mode = SP_PARSE_SOURCE_TARGET_DIRS;
            continue;
        }
        if (is_test_scope && eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
            saw_test_directory_clause = true;
            parse_mode = SP_PARSE_TEST_DIRS;
            continue;
        }

        if (parse_mode == SP_PARSE_SOURCE_DIRS) {
            if (!svu_list_push_temp(ctx, &source_dirs, a[i])) return !eval_should_stop(ctx);
            continue;
        }
        if (parse_mode == SP_PARSE_SOURCE_TARGET_DIRS) {
            if (!svu_list_push_temp(ctx, &source_target_dirs, a[i])) return !eval_should_stop(ctx);
            continue;
        }
        if (parse_mode == SP_PARSE_TEST_DIRS) {
            if (!svu_list_push_temp(ctx, &test_dirs, a[i])) return !eval_should_stop(ctx);
            continue;
        }
        if (!svu_list_push_temp(ctx, &objects, a[i])) return !eval_should_stop(ctx);
    }

    if (saw_source_directory_clause && arena_arr_len(source_dirs) == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(SOURCE DIRECTORY ...) requires at least one directory"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    if (saw_source_target_directory_clause && arena_arr_len(source_target_dirs) == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(SOURCE TARGET_DIRECTORY ...) requires at least one target"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    if (saw_test_directory_clause && arena_arr_len(test_dirs) == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(TEST DIRECTORY ...) requires at least one directory"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    if (saw_test_directory_clause && arena_arr_len(test_dirs) > 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(TEST DIRECTORY ...) expects exactly one directory"),
                       nob_sv_from_cstr("Use: set_property(TEST [<test>...] [DIRECTORY <dir>] PROPERTY <key> [value...])"));
        return !eval_should_stop(ctx);
    }

    if (is_global_scope && arena_arr_len(objects) > 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(GLOBAL ...) does not take object names"),
                       nob_sv_from_cstr("Use: set_property(GLOBAL PROPERTY <key> [value...])"));
        return !eval_should_stop(ctx);
    }

    {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        for (size_t di = 0; di < arena_arr_len(source_dirs); di++) {
            source_dirs[di] = eval_path_resolve_for_cmake_arg(ctx, source_dirs[di], cur_src, true);
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        }
        for (size_t di = 0; di < arena_arr_len(test_dirs); di++) {
            test_dirs[di] = eval_path_resolve_for_cmake_arg(ctx, test_dirs[di], cur_src, true);
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        }
    }

    if (i >= arena_arr_len(a) || !eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() missing PROPERTY keyword"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    i++;
    if (i >= arena_arr_len(a)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() missing property key"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View key = a[i++];
    String_View value = nob_sv_from_cstr("");
    if (i < arena_arr_len(a)) {
        if (append_string) value = svu_join_no_sep_temp(ctx, &a[i], arena_arr_len(a) - i);
        else value = eval_sv_join_semi_temp(ctx, &a[i], arena_arr_len(a) - i);
    }

    Cmake_Target_Property_Op op = EV_PROP_SET;
    if (append_string) op = EV_PROP_APPEND_STRING;
    else if (append) op = EV_PROP_APPEND_LIST;

    if (append && append_string) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() received both APPEND and APPEND_STRING"),
                       nob_sv_from_cstr("Use only one of APPEND or APPEND_STRING"));
        return !eval_should_stop(ctx);
    }

    if (is_target_scope) {
        for (size_t ti = 0; ti < arena_arr_len(objects); ti++) {
            if (!eval_target_known(ctx, objects[ti])) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("set_property(TARGET ...) target was not declared"),
                               objects[ti]);
                continue;
            }
            if (eval_target_alias_known(ctx, objects[ti])) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("dispatcher"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("set_property(TARGET ...) cannot be used on ALIAS targets"),
                               objects[ti]);
                continue;
            }
            if (!emit_target_prop_set(ctx, o, objects[ti], key, value, op)) {
                return !eval_should_stop(ctx);
            }
        }
        return !eval_should_stop(ctx);
    }

    String_View scope_upper = eval_property_upper_name_temp(ctx, scope);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (is_global_scope) {
        if (!set_non_target_property(ctx, o, scope_upper, nob_sv_from_cstr(""), key, value, op)) {
            return !eval_should_stop(ctx);
        }
        return !eval_should_stop(ctx);
    }

    if (is_dir_scope && arena_arr_len(objects) == 0) {
        String_View current_dir = eval_current_source_dir(ctx);
        if (!set_non_target_property(ctx, o, scope_upper, current_dir, key, value, op)) {
            return !eval_should_stop(ctx);
        }
        return !eval_should_stop(ctx);
    }

    if (is_source_scope) {
        for (size_t ti = 0; ti < arena_arr_len(source_target_dirs); ti++) {
            if (eval_target_known(ctx, source_target_dirs[ti])) continue;
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("set_property(SOURCE TARGET_DIRECTORY ...) target was not declared"),
                           source_target_dirs[ti]);
            return !eval_should_stop(ctx);
        }

        if (saw_source_directory_clause || saw_source_target_directory_clause) {
            for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
                if (!saw_source_directory_clause && !saw_source_target_directory_clause) {
                    if (!set_non_target_property(ctx, o, scope_upper, objects[oi], key, value, op)) {
                        return !eval_should_stop(ctx);
                    }
                    continue;
                }

                for (size_t di = 0; di < arena_arr_len(source_dirs); di++) {
                    String_View object_id = eval_property_scoped_object_id_temp(ctx,
                                                                                "DIRECTORY",
                                                                                source_dirs[di],
                                                                                objects[oi]);
                    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
                    if (!set_non_target_property(ctx, o, scope_upper, object_id, key, value, op)) {
                        return !eval_should_stop(ctx);
                    }
                }
                for (size_t ti = 0; ti < arena_arr_len(source_target_dirs); ti++) {
                    String_View object_id = eval_property_scoped_object_id_temp(ctx,
                                                                                "TARGET_DIRECTORY",
                                                                                source_target_dirs[ti],
                                                                                objects[oi]);
                    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
                    if (!set_non_target_property(ctx, o, scope_upper, object_id, key, value, op)) {
                        return !eval_should_stop(ctx);
                    }
                }
            }
            return !eval_should_stop(ctx);
        }
    }

    if (is_test_scope) {
        String_View test_scope_dir = eval_current_source_dir_for_paths(ctx);
        if (saw_test_directory_clause) test_scope_dir = test_dirs[0];

        for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
            if (eval_test_exists_in_directory_scope(ctx, objects[oi], test_scope_dir)) continue;
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("set_property(TEST ...) test was not declared in selected directory scope"),
                           objects[oi]);
            return !eval_should_stop(ctx);
        }

        if (saw_test_directory_clause) {
            for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
                String_View object_id = eval_property_scoped_object_id_temp(ctx,
                                                                            "DIRECTORY",
                                                                            test_scope_dir,
                                                                            objects[oi]);
                if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
                if (!set_non_target_property(ctx, o, scope_upper, object_id, key, value, op)) {
                    return !eval_should_stop(ctx);
                }
            }
            return !eval_should_stop(ctx);
        }
    }

    if (is_cache_scope) {
        for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
            if (eval_cache_defined(ctx, objects[oi])) continue;
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("set_property(CACHE ...) cache entry does not exist"),
                           objects[oi]);
            return !eval_should_stop(ctx);
        }
    }

    for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
        if (!set_non_target_property(ctx, o, scope_upper, objects[oi], key, value, op)) {
            return !eval_should_stop(ctx);
        }
    }

    return !eval_should_stop(ctx);
}

