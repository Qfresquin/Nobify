#include "eval_include.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <string.h>

static bool emit_dir_push_event(EvalExecContext *ctx,
                                Cmake_Event_Origin origin,
                                String_View source_dir,
                                String_View binary_dir) {
    return eval_emit_dir_push(ctx, origin, source_dir, binary_dir);
}

static bool emit_dir_pop_event(EvalExecContext *ctx,
                               Cmake_Event_Origin origin,
                               String_View source_dir,
                               String_View binary_dir) {
    return eval_emit_dir_pop(ctx, origin, source_dir, binary_dir);
}

static bool include_enables_cpack_component_commands(String_View arg) {
    return eval_sv_eq_ci_lit(arg, "CPackComponent") ||
           eval_sv_eq_ci_lit(arg, "CPackComponent.cmake") ||
           eval_sv_eq_ci_lit(arg, "CPack") ||
           eval_sv_eq_ci_lit(arg, "CPack.cmake");
}

static bool include_is_cpack_module(String_View arg) {
    return eval_sv_eq_ci_lit(arg, "CPack") ||
           eval_sv_eq_ci_lit(arg, "CPack.cmake");
}

static bool include_enables_fetchcontent_commands(String_View arg) {
    return eval_sv_eq_ci_lit(arg, "FetchContent") ||
           eval_sv_eq_ci_lit(arg, "FetchContent.cmake");
}

static bool include_file_exists_sv(EvalExecContext *ctx, String_View path) {
    bool exists = false;
    return eval_service_file_exists(ctx, path, &exists) && exists;
}

static bool include_split_semicolon_temp(EvalExecContext *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;
    *out = NULL;
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

static bool include_find_module_candidate_in_dir(EvalExecContext *ctx,
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

static bool include_try_module_search(EvalExecContext *ctx,
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

    SV_List module_dirs = NULL;
    if (!include_split_semicolon_temp(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_MODULE_PATH")), &module_dirs)) {
        return false;
    }

    // Resolve the host CMake installation's builtin Modules tree as a fallback,
    // but keep user-provided CMAKE_MODULE_PATH ahead of it for now.
    String_View modules_dir = eval_cmake_builtin_modules_dir_temp(ctx);
    bool cmp0017_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0017);
    bool search_builtin_first = false;
    (void)cmp0017_new;
    (void)current_list_dir;

    if (search_builtin_first && modules_dir.count > 0) {
        if (include_find_module_candidate_in_dir(ctx, modules_dir, candidates, candidate_count, out_path)) {
            return true;
        }
    }

    for (size_t di = 0; di < arena_arr_len(module_dirs); di++) {
        String_View dir = module_dirs[di];
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

bool eval_include_resolve_target(EvalExecContext *ctx,
                                 String_View file_or_module,
                                 String_View *out_resolved) {
    if (!ctx || !out_resolved) return false;
    *out_resolved = nob_sv_from_cstr("");
    if (file_or_module.count == 0) return false;

    String_View current_list_dir = eval_current_list_dir(ctx);
    String_View current_src_dir = eval_current_source_dir(ctx);

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

typedef enum {
    INCLUDE_GUARD_VARIABLE = 0,
    INCLUDE_GUARD_DIRECTORY,
    INCLUDE_GUARD_GLOBAL,
} Include_Guard_Mode;

static bool include_guard_var_defined_global(EvalExecContext *ctx, String_View key) {
    size_t saved_depth = 0;
    if (!eval_scope_use_global_view(ctx, &saved_depth)) return false;
    bool defined = eval_var_defined_visible(ctx, key);
    eval_scope_restore_view(ctx, saved_depth);
    return defined;
}

static String_View include_guard_var_get_global(EvalExecContext *ctx, String_View key) {
    size_t saved_depth = 0;
    if (!eval_scope_use_global_view(ctx, &saved_depth)) return nob_sv_from_cstr("");
    String_View value = eval_var_get_visible(ctx, key);
    eval_scope_restore_view(ctx, saved_depth);
    return value;
}

static bool include_guard_var_set_global(EvalExecContext *ctx, String_View key, String_View value) {
    size_t saved_depth = 0;
    if (!eval_scope_use_global_view(ctx, &saved_depth)) return false;
    bool ok = eval_var_set_current(ctx, key, value);
    eval_scope_restore_view(ctx, saved_depth);
    return ok;
}

static String_View include_guard_key_for_mode(EvalExecContext *ctx, Include_Guard_Mode mode, String_View current_file) {
    if (!ctx) return nob_sv_from_cstr("");
    switch (mode) {
        case INCLUDE_GUARD_GLOBAL: {
            String_View parts[2] = {nob_sv_from_cstr("NOBIFY_INCLUDE_GUARD_GLOBAL::"), current_file};
            return svu_join_no_sep_temp(ctx, parts, 2);
        }
        case INCLUDE_GUARD_DIRECTORY: {
            String_View parts[2] = {nob_sv_from_cstr("NOBIFY_INCLUDE_GUARD_DIRECTORY::"), current_file};
            return svu_join_no_sep_temp(ctx, parts, 2);
        }
        case INCLUDE_GUARD_VARIABLE:
        default: {
            String_View parts[2] = {nob_sv_from_cstr("NOBIFY_INCLUDE_GUARD_VARIABLE::"), current_file};
            return svu_join_no_sep_temp(ctx, parts, 2);
        }
    }
}

static bool include_guard_directory_hit(EvalExecContext *ctx, String_View key, String_View current_source_dir) {
    if (!ctx) return false;
    String_View current_norm = eval_sv_path_normalize_temp(ctx, current_source_dir);
    String_View existing = include_guard_var_get_global(ctx, key);
    if (existing.count == 0) return false;

    SV_List dirs = NULL;
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), existing, &dirs)) return false;
    if (eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(dirs); i++) {
        if (dirs[i].count == 0) continue;
        String_View guard_dir = eval_sv_path_normalize_temp(ctx, dirs[i]);
        if (include_path_is_same_or_child(current_norm, guard_dir)) return true;
    }
    return false;
}

static bool include_guard_directory_add(EvalExecContext *ctx, String_View key, String_View current_source_dir) {
    if (!ctx) return false;
    String_View current_norm = eval_sv_path_normalize_temp(ctx, current_source_dir);
    String_View existing = include_guard_var_get_global(ctx, key);
    if (existing.count == 0) return include_guard_var_set_global(ctx, key, current_norm);

    SV_List dirs = NULL;
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), existing, &dirs)) return false;
    if (eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(dirs); i++) {
        if (nob_sv_eq(dirs[i], current_norm)) return true;
    }

    String_View *joined = arena_alloc_array(eval_temp_arena(ctx), String_View, arena_arr_len(dirs) + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, joined, false);
    for (size_t i = 0; i < arena_arr_len(dirs); i++) joined[i] = dirs[i];
    joined[arena_arr_len(dirs)] = current_norm;
    String_View updated = eval_sv_join_semi_temp(ctx, joined, arena_arr_len(dirs) + 1);
    return include_guard_var_set_global(ctx, key, updated);
}

Eval_Result eval_handle_include_guard(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) > 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_include", nob_sv_from_cstr("include_guard() accepts at most one scope argument"), nob_sv_from_cstr("Usage: include_guard([DIRECTORY|GLOBAL])"));
        return eval_result_from_ctx(ctx);
    }

    Include_Guard_Mode mode = INCLUDE_GUARD_VARIABLE;
    if (arena_arr_len(a) == 1) {
        if (eval_sv_eq_ci_lit(a[0], "DIRECTORY")) {
            mode = INCLUDE_GUARD_DIRECTORY;
        } else if (eval_sv_eq_ci_lit(a[0], "GLOBAL")) {
            mode = INCLUDE_GUARD_GLOBAL;
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_include", nob_sv_from_cstr("include_guard() received invalid scope"), nob_sv_from_cstr("Expected one of: DIRECTORY, GLOBAL"));
            return eval_result_from_ctx(ctx);
        }
    }

    String_View current_file = eval_current_list_file(ctx);
    String_View current_source_dir = eval_current_source_dir(ctx);
    if (current_source_dir.count == 0) {
        current_source_dir = eval_current_list_dir(ctx);
    }
    if (current_source_dir.count == 0) current_source_dir = ctx->source_dir;

    String_View key = include_guard_key_for_mode(ctx, mode, current_file);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    bool already_guarded = false;
    switch (mode) {
        case INCLUDE_GUARD_VARIABLE:
            already_guarded = eval_var_defined_visible(ctx, key);
            if (!already_guarded && !eval_var_set_current(ctx, key, nob_sv_from_cstr("1"))) {
                return eval_result_from_ctx(ctx);
            }
            break;
        case INCLUDE_GUARD_GLOBAL:
            already_guarded = include_guard_var_defined_global(ctx, key);
            if (!already_guarded && !include_guard_var_set_global(ctx, key, nob_sv_from_cstr("1"))) {
                return eval_result_from_ctx(ctx);
            }
            break;
        case INCLUDE_GUARD_DIRECTORY:
            already_guarded = include_guard_directory_hit(ctx, key, current_source_dir);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            if (!already_guarded && !include_guard_directory_add(ctx, key, current_source_dir)) {
                return eval_result_from_ctx(ctx);
            }
            break;
        default:
            return eval_result_from_ctx(ctx);
    }

    if (already_guarded) {
        (void)eval_exec_request_return(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_include(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_include", nob_sv_from_cstr("include() missing file or module argument"), nob_sv_from_cstr("Usage: include(<file|module> [OPTIONAL] [RESULT_VARIABLE <var>] [NO_POLICY_SCOPE])"));
        return eval_result_from_ctx(ctx);
    }

    String_View file_or_module = a[0];
    bool optional = false;
    bool no_policy_scope = false;
    String_View result_variable = nob_sv_from_cstr("");

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "OPTIONAL")) {
            optional = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "NO_POLICY_SCOPE")) {
            no_policy_scope = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "RESULT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_include", nob_sv_from_cstr("include(RESULT_VARIABLE) requires an output variable name"), nob_sv_from_cstr("Usage: include(<file|module> ... RESULT_VARIABLE <var>)"));
                return eval_result_from_ctx(ctx);
            }
            result_variable = a[++i];
            continue;
        }
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_include", nob_sv_from_cstr("include() received unexpected argument"), a[i]);
        return eval_result_from_ctx(ctx);
    }

    // CPack component commands are provided by CPackComponent (and by CPack, which includes it).
    if (include_enables_cpack_component_commands(file_or_module)) {
        if (include_is_cpack_module(file_or_module)) {
            ctx->cpack_module_loaded = true;
        }
        ctx->cpack_component_module_loaded = true;
        if (result_variable.count > 0) {
            (void)eval_var_set_current(ctx, result_variable, file_or_module);
        }
        return eval_result_from_ctx(ctx);
    }
    if (include_enables_fetchcontent_commands(file_or_module)) {
        ctx->fetchcontent_module_loaded = true;
        if (result_variable.count > 0) {
            (void)eval_var_set_current(ctx, result_variable, file_or_module);
        }
        return eval_result_from_ctx(ctx);
    }

    String_View file_path = nob_sv_from_cstr("");
    bool found = eval_include_resolve_target(ctx, file_or_module, &file_path);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (!found) {
        if (result_variable.count > 0) {
            (void)eval_var_set_current(ctx, result_variable, nob_sv_from_cstr("NOTFOUND"));
        }
        if (!optional) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_include", nob_sv_from_cstr("include() could not find requested file or module"), file_or_module);
        }
        return eval_result_from_ctx(ctx);
    }

    if (result_variable.count > 0) {
        (void)eval_var_set_current(ctx, result_variable, file_path);
    }

    if (!eval_emit_include_begin(ctx, o, file_path, no_policy_scope)) return eval_result_from_ctx(ctx);

    String_View scope_source = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (scope_source.count == 0) scope_source = ctx->source_dir;
    String_View scope_binary = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (scope_binary.count == 0) scope_binary = ctx->source_dir;

    if (!emit_dir_push_event(ctx, o, scope_source, scope_binary)) return eval_result_from_ctx(ctx);
    bool pushed_policy = false;
    if (!no_policy_scope) {
        if (!eval_policy_push(ctx)) {
            (void)emit_dir_pop_event(ctx, o, scope_source, scope_binary);
            return eval_result_from_ctx(ctx);
        }
        pushed_policy = true;
    }
    Eval_Result exec_res = eval_execute_file(ctx, file_path, false, nob_sv_from_cstr(""));
    bool success = !eval_result_is_fatal(exec_res);
    if (pushed_policy && !eval_policy_pop(ctx) && !eval_should_stop(ctx)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_POLICY_CONFLICT, "dispatcher", nob_sv_from_cstr("include() failed to restore policy stack"), nob_sv_from_cstr("cmake_policy(POP) underflow while leaving include()"));
    }
    if (!emit_dir_pop_event(ctx, o, scope_source, scope_binary)) return eval_result_from_ctx(ctx);
    if (!eval_emit_include_end(ctx, o, file_path, success)) return eval_result_from_ctx(ctx);
    if (!success && !optional && !eval_should_stop(ctx)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_include", nob_sv_from_cstr("include() failed to read or evaluate file"), file_path);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_subdirectory(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_subdirectory() missing source_dir"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    String_View source_dir = a[0];
    String_View binary_dir = nob_sv_from_cstr("");
    bool exclude_from_all = false;
    bool system = false;

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "EXCLUDE_FROM_ALL")) {
            exclude_from_all = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "SYSTEM")) {
            system = true;
            continue;
        }
        if (binary_dir.count == 0) {
            binary_dir = a[i];
            continue;
        }
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_LEGACY_WARNING, "dispatcher", nob_sv_from_cstr("add_subdirectory() ignoring extra argument"), a[i]);
    }

    String_View current_src = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;
    String_View current_bin = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (current_bin.count == 0) current_bin = ctx->binary_dir;

    source_dir = eval_path_resolve_for_cmake_arg(ctx, source_dir, current_src, false);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (binary_dir.count > 0) {
        binary_dir = eval_path_resolve_for_cmake_arg(ctx, binary_dir, current_bin, false);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    }

    String_View full_path = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr("CMakeLists.txt"));
    String_View scope_binary = binary_dir.count > 0
        ? binary_dir
        : eval_path_resolve_for_cmake_arg(ctx, a[0], current_bin, false);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (!eval_emit_add_subdirectory_begin(ctx, o, source_dir, scope_binary, exclude_from_all, system)) {
        return eval_result_from_ctx(ctx);
    }
    if (!emit_dir_push_event(ctx, o, source_dir, scope_binary)) return eval_result_from_ctx(ctx);

    bool had_system_default = eval_var_defined_visible(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"));
    String_View system_default_prev = had_system_default
        ? eval_var_get_visible(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"))
        : nob_sv_from_cstr("");
    if (system) {
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"), nob_sv_from_cstr("1"))) {
            (void)emit_dir_pop_event(ctx, o, source_dir, scope_binary);
            return eval_result_from_ctx(ctx);
        }
    }

    Eval_Result exec_res = eval_execute_file(ctx, full_path, true, scope_binary);
    bool success = !eval_result_is_fatal(exec_res);

    if (system) {
        if (had_system_default) {
            (void)eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"), system_default_prev);
        } else {
            (void)eval_var_unset_current(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"));
        }
    }

    if (!emit_dir_pop_event(ctx, o, source_dir, scope_binary)) return eval_result_from_ctx(ctx);
    if (!eval_emit_add_subdirectory_end(ctx, o, source_dir, scope_binary, success)) return eval_result_from_ctx(ctx);
    if (!success && !eval_should_stop(ctx)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "dispatcher", nob_sv_from_cstr("add_subdirectory() failed to read or evaluate CMakeLists.txt"), full_path);
    }
    return eval_result_from_ctx(ctx);
}
