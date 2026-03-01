#include "eval_target.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <ctype.h>
#include <string.h>

static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

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

static String_View sv_to_upper_temp(Evaluator_Context *ctx, String_View in) {
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) {
        buf[i] = (char)toupper((unsigned char)in.data[i]);
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
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

static String_View make_property_store_key_temp(Evaluator_Context *ctx,
                                                String_View scope_upper,
                                                String_View object_id,
                                                String_View prop_upper) {
    static const char prefix[] = "NOBIFY_PROPERTY_";
    bool has_obj = object_id.count > 0;
    size_t total = (sizeof(prefix) - 1) + scope_upper.count + 2 + prop_upper.count;
    if (has_obj) total += 2 + object_id.count;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, sizeof(prefix) - 1);
    off += sizeof(prefix) - 1;
    memcpy(buf + off, scope_upper.data, scope_upper.count);
    off += scope_upper.count;
    buf[off++] = ':';
    buf[off++] = ':';
    if (has_obj) {
        memcpy(buf + off, object_id.data, object_id.count);
        off += object_id.count;
        buf[off++] = ':';
        buf[off++] = ':';
    }
    memcpy(buf + off, prop_upper.data, prop_upper.count);
    off += prop_upper.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool is_current_directory_object(Evaluator_Context *ctx, String_View object_id) {
    if (!ctx) return false;
    if (object_id.count == 0) return true;
    if (eval_sv_eq_ci_lit(object_id, ".")) return true;

    String_View cur_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    String_View cur_bin = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (svu_eq_ci_sv(object_id, cur_src)) return true;
    if (svu_eq_ci_sv(object_id, cur_bin)) return true;
    return false;
}

static bool set_non_target_property(Evaluator_Context *ctx,
                                    Cmake_Event_Origin o,
                                    String_View scope_upper,
                                    String_View object_id,
                                    String_View prop_key,
                                    String_View value,
                                    Cmake_Target_Property_Op op) {
    String_View prop_upper = sv_to_upper_temp(ctx, prop_key);
    if (eval_should_stop(ctx)) return false;

    String_View store_key = make_property_store_key_temp(ctx, scope_upper, object_id, prop_upper);
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

bool eval_handle_target_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 2) return !eval_should_stop(ctx);

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_LIBRARIES;
        ev.origin = o;
        ev.as.target_link_libraries.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_libraries.visibility = vis;
        ev.as.target_link_libraries.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_target_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_link_options() requires target and items"),
                       nob_sv_from_cstr("Usage: target_link_options(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_OPTIONS;
        ev.origin = o;
        ev.as.target_link_options.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_options.visibility = vis;
        ev.as.target_link_options.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_target_link_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_link_directories() requires target and items"),
                       nob_sv_from_cstr("Usage: target_link_directories(<tgt> <PUBLIC|PRIVATE|INTERFACE> <dirs...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_DIRECTORIES;
        ev.origin = o;
        ev.as.target_link_directories.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_directories.visibility = vis;
        ev.as.target_link_directories.path = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_target_include_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_include_directories() requires target and items"),
                       nob_sv_from_cstr("Usage: target_include_directories(<tgt> [SYSTEM] [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_system = false;
    bool is_before = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "SYSTEM")) {
            is_system = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "AFTER")) {
            is_before = false;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_INCLUDE_DIRECTORIES;
        ev.origin = o;
        ev.as.target_include_directories.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_include_directories.visibility = vis;
        ev.as.target_include_directories.path = sv_copy_to_event_arena(ctx, a.items[i]);
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

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_compile_definitions() requires target and items"),
                       nob_sv_from_cstr("Usage: target_compile_definitions(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_COMPILE_DEFINITIONS;
        ev.origin = o;
        ev.as.target_compile_definitions.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_compile_definitions.visibility = vis;
        ev.as.target_compile_definitions.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_target_compile_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_compile_options() requires target and items"),
                       nob_sv_from_cstr("Usage: target_compile_options(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.target_compile_options.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_compile_options.visibility = vis;
        ev.as.target_compile_options.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_set_target_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() requires targets and PROPERTIES key/value pairs"),
                       nob_sv_from_cstr("Usage: set_target_properties(<t1> [<t2> ...] PROPERTIES <k1> <v1> ...)"));
        return !eval_should_stop(ctx);
    }

    size_t props_i = a.count;
    for (size_t i = 0; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PROPERTIES")) {
            props_i = i;
            break;
        }
    }

    if (props_i == 0 || props_i >= a.count - 1) {
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
    size_t kv_count = a.count - kv_start;
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
        String_View tgt = a.items[ti];
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
        for (size_t i = kv_start; i + 1 < a.count; i += 2) {
            if (!emit_target_prop_set(ctx, o, tgt, a.items[i], a.items[i + 1], EV_PROP_SET)) {
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

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() missing scope"),
                       nob_sv_from_cstr("Usage: set_property(<GLOBAL|DIRECTORY|TARGET|SOURCE|INSTALL|TEST|CACHE> ... PROPERTY <k> [v...])"));
        return !eval_should_stop(ctx);
    }

    String_View scope = a.items[0];
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
    SV_List objects = {0};

    size_t i = 1;
    for (; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PROPERTY")) break;
        if (eval_sv_eq_ci_lit(a.items[i], "APPEND")) {
            append = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "APPEND_STRING")) {
            append_string = true;
            continue;
        }
        if (!svu_list_push_temp(ctx, &objects, a.items[i])) return !eval_should_stop(ctx);
    }

    if (is_target_scope && objects.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(TARGET ...) requires at least one target"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    if (is_global_scope && objects.count > 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(GLOBAL ...) does not take object names"),
                       nob_sv_from_cstr("Use: set_property(GLOBAL PROPERTY <key> [value...])"));
        return !eval_should_stop(ctx);
    }

    if (!is_target_scope && !is_global_scope && !is_dir_scope && objects.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() scope requires at least one object"),
                       nob_sv_from_cstr("SOURCE/INSTALL/TEST/CACHE require object names"));
        return !eval_should_stop(ctx);
    }

    if (i >= a.count || !eval_sv_eq_ci_lit(a.items[i], "PROPERTY")) {
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
    if (i >= a.count) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() missing property key"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View key = a.items[i++];
    String_View value = nob_sv_from_cstr("");
    if (i < a.count) {
        if (append_string) value = svu_join_no_sep_temp(ctx, &a.items[i], a.count - i);
        else value = eval_sv_join_semi_temp(ctx, &a.items[i], a.count - i);
    }

    Cmake_Target_Property_Op op = EV_PROP_SET;
    if (append_string) op = EV_PROP_APPEND_STRING;
    else if (append) op = EV_PROP_APPEND_LIST;

    if (append && append_string) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() received both APPEND and APPEND_STRING"),
                       nob_sv_from_cstr("Using APPEND_STRING behavior"));
    }

    if (is_target_scope) {
        for (size_t ti = 0; ti < objects.count; ti++) {
            if (!emit_target_prop_set(ctx, o, objects.items[ti], key, value, op)) {
                return !eval_should_stop(ctx);
            }
        }
        return !eval_should_stop(ctx);
    }

    String_View scope_upper = sv_to_upper_temp(ctx, scope);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (is_global_scope) {
        if (!set_non_target_property(ctx, o, scope_upper, nob_sv_from_cstr(""), key, value, op)) {
            return !eval_should_stop(ctx);
        }
        return !eval_should_stop(ctx);
    }

    if (is_dir_scope && objects.count == 0) {
        String_View current_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
        if (!set_non_target_property(ctx, o, scope_upper, current_dir, key, value, op)) {
            return !eval_should_stop(ctx);
        }
        return !eval_should_stop(ctx);
    }

    for (size_t oi = 0; oi < objects.count; oi++) {
        if (!set_non_target_property(ctx, o, scope_upper, objects.items[oi], key, value, op)) {
            return !eval_should_stop(ctx);
        }
    }

    return !eval_should_stop(ctx);
}

