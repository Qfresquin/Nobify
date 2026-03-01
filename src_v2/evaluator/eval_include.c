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

static bool include_enables_cpack_component_commands(String_View arg) {
    return eval_sv_eq_ci_lit(arg, "CPackComponent") ||
           eval_sv_eq_ci_lit(arg, "CPackComponent.cmake") ||
           eval_sv_eq_ci_lit(arg, "CPack") ||
           eval_sv_eq_ci_lit(arg, "CPack.cmake");
}

static bool include_file_exists_sv(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return false;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_file_exists(path_c) != 0;
}

static bool include_split_semicolon_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;
    *out = (SV_List){0};
    if (input.count == 0) return true;

    const char *p = input.data;
    const char *end = input.data + input.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        if (!svu_list_push_temp(ctx, out, nob_sv_from_parts(p, (size_t)(q - p)))) return false;
        if (q >= end) break;
        p = q + 1;
    }
    return true;
}

static bool include_has_path_separator(String_View value) {
    for (size_t i = 0; i < value.count; i++) {
        if (value.data[i] == '/' || value.data[i] == '\\') return true;
    }
    return false;
}

static bool include_has_cmake_extension(String_View value) {
    static const char *k_suffix = ".cmake";
    size_t n = strlen(k_suffix);
    if (value.count < n) return false;
    String_View tail = nob_sv_from_parts(value.data + value.count - n, n);
    return eval_sv_eq_ci_lit(tail, k_suffix);
}

static bool include_path_is_same_or_child(String_View path, String_View base) {
    if (path.count < base.count) return false;
    if (!nob_sv_eq(nob_sv_from_parts(path.data, base.count), base)) return false;
    if (path.count == base.count) return true;
    return path.data[base.count] == '/' || path.data[base.count] == '\\';
}

static bool include_find_module_candidate_in_dir(Evaluator_Context *ctx,
                                                 String_View dir,
                                                 const String_View *candidates,
                                                 size_t candidate_count,
                                                 String_View *out_path) {
    if (!ctx || !out_path || !candidates) return false;
    for (size_t ci = 0; ci < candidate_count; ci++) {
        String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dir, candidates[ci]);
        candidate = eval_sv_path_normalize_temp(ctx, candidate);
        if (include_file_exists_sv(ctx, candidate)) {
            *out_path = candidate;
            return true;
        }
    }
    return false;
}

static bool include_try_module_search(Evaluator_Context *ctx,
                                      String_View module_name,
                                      String_View current_list_dir,
                                      String_View current_source_dir,
                                      String_View *out_path) {
    if (!ctx || !out_path || module_name.count == 0) return false;
    *out_path = nob_sv_from_cstr("");

    String_View candidates[2] = {0};
    size_t candidate_count = 0;
    if (include_has_cmake_extension(module_name)) {
        candidates[candidate_count++] = module_name;
    } else {
        candidates[candidate_count++] = svu_concat_suffix_temp(ctx, module_name, ".cmake");
    }
    if (eval_should_stop(ctx)) return false;

    SV_List module_dirs = {0};
    if (!include_split_semicolon_temp(ctx, eval_var_get(ctx, nob_sv_from_cstr("CMAKE_MODULE_PATH")), &module_dirs)) {
        return false;
    }

    String_View modules_dir = nob_sv_from_cstr("");
    String_View cmake_root = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_ROOT"));
    if (cmake_root.count > 0) {
        modules_dir = eval_sv_path_join(eval_temp_arena(ctx), cmake_root, nob_sv_from_cstr("Modules"));
        modules_dir = eval_sv_path_normalize_temp(ctx, modules_dir);
    }

    bool cmp0017_new = eval_sv_eq_ci_lit(eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0017")), "NEW");
    bool search_builtin_first = false;
    if (cmp0017_new && modules_dir.count > 0 && current_list_dir.count > 0) {
        String_View normalized_list_dir = eval_sv_path_normalize_temp(ctx, current_list_dir);
        search_builtin_first = include_path_is_same_or_child(normalized_list_dir, modules_dir);
    }

    if (search_builtin_first && modules_dir.count > 0) {
        if (include_find_module_candidate_in_dir(ctx, modules_dir, candidates, candidate_count, out_path)) {
            return true;
        }
    }

    for (size_t di = 0; di < module_dirs.count; di++) {
        String_View dir = module_dirs.items[di];
        if (dir.count == 0) continue;
        if (!eval_sv_is_abs_path(dir)) {
            dir = eval_path_resolve_for_cmake_arg(ctx, dir, current_source_dir, false);
            if (eval_should_stop(ctx)) return false;
        } else {
            dir = eval_sv_path_normalize_temp(ctx, dir);
        }
        if (include_find_module_candidate_in_dir(ctx, dir, candidates, candidate_count, out_path)) {
            return true;
        }
    }

    if (!search_builtin_first && modules_dir.count > 0) {
        if (include_find_module_candidate_in_dir(ctx, modules_dir, candidates, candidate_count, out_path)) {
            return true;
        }
    }

    return false;
}

static bool include_resolve_target(Evaluator_Context *ctx,
                                   String_View file_or_module,
                                   String_View *out_resolved) {
    if (!ctx || !out_resolved) return false;
    *out_resolved = nob_sv_from_cstr("");
    if (file_or_module.count == 0) return false;

    String_View current_list_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"));
    if (current_list_dir.count == 0) current_list_dir = ctx->source_dir;
    String_View current_src_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src_dir.count == 0) current_src_dir = ctx->source_dir;

    if (eval_sv_is_abs_path(file_or_module)) {
        String_View path = eval_sv_path_normalize_temp(ctx, file_or_module);
        if (include_file_exists_sv(ctx, path)) {
            *out_resolved = path;
            return true;
        }
        return false;
    }

    bool has_sep = include_has_path_separator(file_or_module);
    String_View rel = eval_path_resolve_for_cmake_arg(ctx, file_or_module, current_list_dir, false);
    if (eval_should_stop(ctx)) return false;
    if (include_file_exists_sv(ctx, rel)) {
        *out_resolved = rel;
        return true;
    }

    if (has_sep) return false;
    return include_try_module_search(ctx, file_or_module, current_list_dir, current_src_dir, out_resolved);
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
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_include"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("include() missing file or module argument"),
                       nob_sv_from_cstr("Usage: include(<file|module> [OPTIONAL] [RESULT_VARIABLE <var>] [NO_POLICY_SCOPE])"));
        return !eval_should_stop(ctx);
    }

    String_View file_or_module = a.items[0];
    bool optional = false;
    bool no_policy_scope = false;
    String_View result_variable = nob_sv_from_cstr("");

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "OPTIONAL")) {
            optional = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "NO_POLICY_SCOPE")) {
            no_policy_scope = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "RESULT_VARIABLE")) {
            if (i + 1 >= a.count) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("eval_include"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("include(RESULT_VARIABLE) requires an output variable name"),
                               nob_sv_from_cstr("Usage: include(<file|module> ... RESULT_VARIABLE <var>)"));
                return !eval_should_stop(ctx);
            }
            result_variable = a.items[++i];
            continue;
        }
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_include"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("include() received unexpected argument"),
                       a.items[i]);
        return !eval_should_stop(ctx);
    }

    // CPack component commands are provided by CPackComponent (and by CPack, which includes it).
    if (include_enables_cpack_component_commands(file_or_module)) {
        ctx->cpack_component_module_loaded = true;
        if (result_variable.count > 0) {
            (void)eval_var_set(ctx, result_variable, file_or_module);
        }
        return !eval_should_stop(ctx);
    }

    String_View file_path = nob_sv_from_cstr("");
    bool found = include_resolve_target(ctx, file_or_module, &file_path);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (!found) {
        if (result_variable.count > 0) {
            (void)eval_var_set(ctx, result_variable, nob_sv_from_cstr("NOTFOUND"));
        }
        if (!optional) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("eval_include"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("include() could not find requested file or module"),
                           file_or_module);
        }
        return !eval_should_stop(ctx);
    }

    if (result_variable.count > 0) {
        (void)eval_var_set(ctx, result_variable, file_path);
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
                       nob_sv_from_cstr("eval_include"),
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

