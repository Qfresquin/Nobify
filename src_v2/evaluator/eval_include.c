#include "eval_include.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <string.h>

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool emit_dir_push_event(Evaluator_Context *ctx,
                                Cmake_Event_Origin origin,
                                String_View source_dir,
                                String_View binary_dir) {
    Cmake_Event ev = {0};
    ev.kind = EV_DIR_PUSH;
    ev.origin = origin;
    ev.as.dir_push.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.dir_push.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    return emit_event(ctx, ev);
}

static bool emit_dir_pop_event(Evaluator_Context *ctx, Cmake_Event_Origin origin) {
    Cmake_Event ev = {0};
    ev.kind = EV_DIR_POP;
    ev.origin = origin;
    return emit_event(ctx, ev);
}

bool eval_handle_include_guard(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    String_View mode = nob_sv_from_cstr("DIRECTORY");
    if (a.count > 0) {
        mode = a.items[0];
        if (!(eval_sv_eq_ci_lit(mode, "DIRECTORY") || eval_sv_eq_ci_lit(mode, "GLOBAL"))) {
            eval_emit_diag(ctx,
                           EV_DIAG_WARNING,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("include_guard() unsupported mode"),
                           mode);
            mode = nob_sv_from_cstr("DIRECTORY");
        }
    }

    String_View current_file = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_FILE"));
    if (current_file.count == 0 && ctx->current_file) {
        current_file = nob_sv_from_cstr(ctx->current_file);
    }
    String_View current_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"));

    String_View key = nob_sv_from_cstr("");
    if (eval_sv_eq_ci_lit(mode, "GLOBAL")) {
        String_View parts[2] = {nob_sv_from_cstr("NOBIFY_INCLUDE_GUARD_GLOBAL::"), current_file};
        key = svu_join_no_sep_temp(ctx, parts, 2);
    } else {
        String_View parts[4] = {
            nob_sv_from_cstr("NOBIFY_INCLUDE_GUARD_DIR::"),
            current_dir,
            nob_sv_from_cstr("::"),
            current_file
        };
        key = svu_join_no_sep_temp(ctx, parts, 4);
    }
    if (ctx->oom) return !eval_should_stop(ctx);

    if (eval_var_defined(ctx, key)) {
        ctx->return_requested = true;
        return !eval_should_stop(ctx);
    }
    (void)eval_var_set(ctx, key, nob_sv_from_cstr("1"));
    return !eval_should_stop(ctx);
}

bool eval_handle_include(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    String_View file_path = a.items[0];
    bool optional = false;
    bool no_policy_scope = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "OPTIONAL")) {
            optional = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "NO_POLICY_SCOPE")) {
            no_policy_scope = true;
        }
    }

    if (!eval_sv_is_abs_path(file_path)) {
        String_View current_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"));
        if (current_dir.count == 0) current_dir = ctx->source_dir;
        file_path = eval_sv_path_join(eval_temp_arena(ctx), current_dir, file_path);
    }

    String_View scope_source = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (scope_source.count == 0) scope_source = ctx->source_dir;
    String_View scope_binary = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (scope_binary.count == 0) scope_binary = ctx->source_dir;

    if (!emit_dir_push_event(ctx, o, scope_source, scope_binary)) return !eval_should_stop(ctx);
    bool pushed_policy = false;
    if (!no_policy_scope) {
        if (!eval_policy_push(ctx)) {
            (void)emit_dir_pop_event(ctx, o);
            return !eval_should_stop(ctx);
        }
        pushed_policy = true;
    }
    bool success = eval_execute_file(ctx, file_path, false, nob_sv_from_cstr(""));
    if (pushed_policy && !eval_policy_pop(ctx) && !eval_should_stop(ctx)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("include() failed to restore policy stack"),
                       nob_sv_from_cstr("cmake_policy(POP) underflow while leaving include()"));
    }
    if (!emit_dir_pop_event(ctx, o)) return !eval_should_stop(ctx);
    if (!success && !optional && !eval_should_stop(ctx)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("include() failed to read or evaluate file"),
                       file_path);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_add_subdirectory(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_subdirectory() missing source_dir"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View source_dir = a.items[0];
    String_View binary_dir = nob_sv_from_cstr("");
    bool system = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "EXCLUDE_FROM_ALL")) continue;
        if (eval_sv_eq_ci_lit(a.items[i], "SYSTEM")) {
            system = true;
            continue;
        }
        if (binary_dir.count == 0) {
            binary_dir = a.items[i];
            continue;
        }
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_subdirectory() ignoring extra argument"),
                       a.items[i]);
    }

    String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;
    String_View current_bin = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (current_bin.count == 0) current_bin = ctx->binary_dir;

    source_dir = eval_path_resolve_for_cmake_arg(ctx, source_dir, current_src, false);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (binary_dir.count > 0) {
        binary_dir = eval_path_resolve_for_cmake_arg(ctx, binary_dir, current_bin, false);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    }

    String_View full_path = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr("CMakeLists.txt"));

    String_View scope_binary = binary_dir.count > 0 ? binary_dir : source_dir;
    if (!emit_dir_push_event(ctx, o, source_dir, scope_binary)) return !eval_should_stop(ctx);

    bool had_system_default = eval_var_defined(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"));
    String_View system_default_prev = had_system_default
        ? eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"))
        : nob_sv_from_cstr("");
    if (system) {
        if (!eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"), nob_sv_from_cstr("1"))) {
            (void)emit_dir_pop_event(ctx, o);
            return !eval_should_stop(ctx);
        }
    }

    bool success = eval_execute_file(ctx, full_path, true, binary_dir);

    if (system) {
        if (had_system_default) {
            (void)eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"), system_default_prev);
        } else {
            (void)eval_var_unset(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"));
        }
    }

    if (!emit_dir_pop_event(ctx, o)) return !eval_should_stop(ctx);
    if (!success && !eval_should_stop(ctx)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_subdirectory() failed to read or evaluate CMakeLists.txt"),
                       full_path);
    }
    return !eval_should_stop(ctx);
}

