#include "eval_directory.h"

#include "evaluator_internal.h"

#include <string.h>

static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool append_list_var(Evaluator_Context *ctx, String_View var, String_View item) {
    String_View current = eval_var_get(ctx, var);
    if (current.count == 0) {
        return eval_var_set(ctx, var, item);
    }

    size_t total = current.count + 1 + item.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    memcpy(buf, current.data, current.count);
    buf[current.count] = ';';
    memcpy(buf + current.count + 1, item.data, item.count);
    buf[total] = '\0';
    return eval_var_set(ctx, var, nob_sv_from_cstr(buf));
}
bool eval_handle_add_compile_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        if (!append_list_var(ctx, nob_sv_from_cstr(k_global_opts_var), a.items[i])) return false;

        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.global_compile_options.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_add_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        String_View item = a.items[i];
        if (item.count == 0) continue;

        // Keep add_definitions() semantics close to CMake: treat arguments as raw flags.
        if (!append_list_var(ctx, nob_sv_from_cstr(k_global_opts_var), item)) return false;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.global_compile_options.item = sv_copy_to_event_arena(ctx, item);
        if (!emit_event(ctx, ev)) return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_add_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        if (a.items[i].count == 0) continue;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_LINK_OPTIONS;
        ev.origin = o;
        ev.as.global_link_options.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        if (a.items[i].count == 0) continue;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_LINK_LIBRARIES;
        ev.origin = o;
        ev.as.global_link_libraries.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_include_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    bool is_system = false;
    bool is_before = false;
    for (size_t i = 0; i < a.count; i++) {
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

        Cmake_Event ev = {0};
        ev.kind = EV_DIRECTORY_INCLUDE_DIRECTORIES;
        ev.origin = o;
        ev.as.directory_include_directories.path = sv_copy_to_event_arena(ctx, a.items[i]);
        ev.as.directory_include_directories.is_system = is_system;
        ev.as.directory_include_directories.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_link_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    bool is_before = false;
    for (size_t i = 0; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "AFTER")) {
            is_before = false;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_DIRECTORY_LINK_DIRECTORIES;
        ev.origin = o;
        ev.as.directory_link_directories.path = sv_copy_to_event_arena(ctx, a.items[i]);
        ev.as.directory_link_directories.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

