#include "eval_dispatcher.h"
#include "evaluator_internal.h"
#include "eval_file.h"
#include "eval_stdlib.h"
#include "eval_vars.h"
#include "eval_diag.h"
#include "eval_flow.h"

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

static char *sv_to_cstr_temp(Evaluator_Context *ctx, String_View sv) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sv.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (sv.count) memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    return buf;
}

static String_View sv_dirname(String_View path) {
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c == '/' || c == '\\') {
            if (i == 0) return nob_sv_from_parts(path.data, 1);
            return nob_sv_from_parts(path.data, i);
        }
    }
    return nob_sv_from_cstr(".");
}

static bool file_exists_sv(Evaluator_Context *ctx, String_View path) {
    char *path_c = sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_file_exists(path_c) != 0;
}

static String_View sv_concat_suffix_temp(Evaluator_Context *ctx, String_View base, const char *suffix) {
    if (!ctx || !suffix) return nob_sv_from_cstr("");
    size_t suffix_len = strlen(suffix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), base.count + suffix_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (base.count) memcpy(buf, base.data, base.count);
    if (suffix_len) memcpy(buf + base.count, suffix, suffix_len);
    buf[base.count + suffix_len] = '\0';
    return nob_sv_from_cstr(buf);
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

static bool emit_items_from_list(Evaluator_Context *ctx,
                                 Cmake_Event_Origin o,
                                 String_View target_name,
                                 String_View list_text,
                                 Cmake_Event_Kind kind) {
    const char *p = list_text.data;
    const char *end = list_text.data + list_text.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View item = nob_sv_from_parts(p, (size_t)(q - p));
        if (item.count > 0) {
            Cmake_Event ev = {0};
            ev.kind = kind;
            ev.origin = o;
            if (kind == EV_TARGET_COMPILE_DEFINITIONS) {
                ev.as.target_compile_definitions.target_name = sv_copy_to_event_arena(ctx, target_name);
                ev.as.target_compile_definitions.visibility = EV_VISIBILITY_UNSPECIFIED;
                ev.as.target_compile_definitions.item = sv_copy_to_event_arena(ctx, item);
            } else if (kind == EV_TARGET_COMPILE_OPTIONS) {
                ev.as.target_compile_options.target_name = sv_copy_to_event_arena(ctx, target_name);
                ev.as.target_compile_options.visibility = EV_VISIBILITY_UNSPECIFIED;
                ev.as.target_compile_options.item = sv_copy_to_event_arena(ctx, item);
            } else {
                return false;
            }
            if (!emit_event(ctx, ev)) return false;
        }
        if (q >= end) break;
        p = q + 1;
    }
    return true;
}

static bool apply_global_compile_state_to_target(Evaluator_Context *ctx,
                                                 Cmake_Event_Origin o,
                                                 String_View target_name) {
    String_View defs = eval_var_get(ctx, nob_sv_from_cstr(k_global_defs_var));
    String_View opts = eval_var_get(ctx, nob_sv_from_cstr(k_global_opts_var));
    if (!emit_items_from_list(ctx, o, target_name, defs, EV_TARGET_COMPILE_DEFINITIONS)) return false;
    if (!emit_items_from_list(ctx, o, target_name, opts, EV_TARGET_COMPILE_OPTIONS)) return false;
    return true;
}

static bool h_project(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("project() missing name"),
                       nob_sv_from_cstr("Usage: project(<name> [VERSION v] ...)"));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    String_View version = nob_sv_from_cstr("");
    String_View desc = nob_sv_from_cstr("");

    String_View langs_items[32];
    size_t langs_n = 0;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "VERSION") && i + 1 < a.count) {
            version = a.items[++i];
        } else if (eval_sv_eq_ci_lit(a.items[i], "DESCRIPTION") && i + 1 < a.count) {
            desc = a.items[++i];
        } else if (eval_sv_eq_ci_lit(a.items[i], "LANGUAGES")) {
            for (size_t j = i + 1; j < a.count && langs_n < 32; j++) {
                langs_items[langs_n++] = a.items[j];
            }
            break;
        }
    }

    String_View langs = eval_sv_join_semi_temp(ctx, langs_items, langs_n);

    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_NAME"), name);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_VERSION"), version);

    Cmake_Event ev = {0};
    ev.kind = EV_PROJECT_DECLARE;
    ev.origin = o;
    ev.as.project_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.project_declare.version = sv_copy_to_event_arena(ctx, version);
    ev.as.project_declare.description = sv_copy_to_event_arena(ctx, desc);
    ev.as.project_declare.languages = sv_copy_to_event_arena(ctx, langs);
    (void)emit_event(ctx, ev);
    return !eval_should_stop(ctx);
}

static bool h_add_executable(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    String_View name = a.items[0];
    (void)eval_target_register(ctx, name);

    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_DECLARE;
    ev.origin = o;
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.type = EV_TARGET_EXECUTABLE;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "WIN32") ||
            eval_sv_eq_ci_lit(a.items[i], "MACOSX_BUNDLE") ||
            eval_sv_eq_ci_lit(a.items[i], "EXCLUDE_FROM_ALL")) {
            continue;
        }

        Cmake_Event src_ev = {0};
        src_ev.kind = EV_TARGET_ADD_SOURCE;
        src_ev.origin = o;
        src_ev.as.target_add_source.target_name = ev.as.target_declare.name;
        src_ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, src_ev)) return !eval_should_stop(ctx);
    }

    (void)apply_global_compile_state_to_target(ctx, o, name);
    return !eval_should_stop(ctx);
}

static bool h_add_library(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    String_View name = a.items[0];
    (void)eval_target_register(ctx, name);

    Cmake_Target_Type ty = EV_TARGET_LIBRARY_UNKNOWN;
    size_t i = 1;
    if (i < a.count) {
        if (eval_sv_eq_ci_lit(a.items[i], "STATIC")) {
            ty = EV_TARGET_LIBRARY_STATIC;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "SHARED")) {
            ty = EV_TARGET_LIBRARY_SHARED;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "MODULE")) {
            ty = EV_TARGET_LIBRARY_MODULE;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            ty = EV_TARGET_LIBRARY_INTERFACE;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "OBJECT")) {
            ty = EV_TARGET_LIBRARY_OBJECT;
            i++;
        }
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_DECLARE;
    ev.origin = o;
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.type = ty;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);

    for (; i < a.count; i++) {
        Cmake_Event src_ev = {0};
        src_ev.kind = EV_TARGET_ADD_SOURCE;
        src_ev.origin = o;
        src_ev.as.target_add_source.target_name = ev.as.target_declare.name;
        src_ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, src_ev)) return !eval_should_stop(ctx);
    }

    (void)apply_global_compile_state_to_target(ctx, o, name);
    return !eval_should_stop(ctx);
}

static bool h_target_link_libraries(Evaluator_Context *ctx, const Node *node) {
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

static bool h_target_include_directories(Evaluator_Context *ctx, const Node *node) {
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

static bool h_target_compile_definitions(Evaluator_Context *ctx, const Node *node) {
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

static bool h_target_compile_options(Evaluator_Context *ctx, const Node *node) {
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

static bool h_add_compile_options(Evaluator_Context *ctx, const Node *node) {
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

static bool h_add_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        String_View item = a.items[i];
        bool is_def = false;

        if (item.count > 2 &&
            ((item.data[0] == '-' && item.data[1] == 'D') ||
             (item.data[0] == '/' && item.data[1] == 'D'))) {
            item = nob_sv_from_parts(item.data + 2, item.count - 2);
            is_def = true;
        }

        if (item.count == 0) continue;

        if (is_def) {
            if (!append_list_var(ctx, nob_sv_from_cstr(k_global_defs_var), item)) return false;
            Cmake_Event ev = {0};
            ev.kind = EV_GLOBAL_COMPILE_DEFINITIONS;
            ev.origin = o;
            ev.as.global_compile_definitions.item = sv_copy_to_event_arena(ctx, item);
            if (!emit_event(ctx, ev)) return false;
        } else {
            if (!append_list_var(ctx, nob_sv_from_cstr(k_global_opts_var), item)) return false;
            Cmake_Event ev = {0};
            ev.kind = EV_GLOBAL_COMPILE_OPTIONS;
            ev.origin = o;
            ev.as.global_compile_options.item = sv_copy_to_event_arena(ctx, item);
            if (!emit_event(ctx, ev)) return false;
        }
    }
    return !eval_should_stop(ctx);
}

static bool h_include(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    String_View file_path = a.items[0];
    bool optional = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "OPTIONAL")) {
            optional = true;
            break;
        }
    }

    if (!eval_sv_is_abs_path(file_path)) {
        String_View current_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"));
        if (current_dir.count == 0) current_dir = ctx->source_dir;
        file_path = eval_sv_path_join(eval_temp_arena(ctx), current_dir, file_path);
    }

    bool success = eval_execute_file(ctx, file_path, false, nob_sv_from_cstr(""));
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

static bool h_add_subdirectory(Evaluator_Context *ctx, const Node *node) {
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

    if (a.count >= 2 && !eval_sv_eq_ci_lit(a.items[1], "EXCLUDE_FROM_ALL")) {
        binary_dir = a.items[1];
    }

    if (!eval_sv_is_abs_path(source_dir)) {
        String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
        source_dir = eval_sv_path_join(eval_temp_arena(ctx), current_src, source_dir);
    }

    String_View full_path = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr("CMakeLists.txt"));

    if (binary_dir.count > 0 && !eval_sv_is_abs_path(binary_dir)) {
        String_View current_bin = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
        binary_dir = eval_sv_path_join(eval_temp_arena(ctx), current_bin, binary_dir);
    }

    bool success = eval_execute_file(ctx, full_path, true, binary_dir);
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

static bool find_package_try_module(Evaluator_Context *ctx, String_View pkg, String_View *out_path) {
    String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;
    String_View module_paths = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_MODULE_PATH"));

    String_View module_name = nob_sv_from_cstr("");
    {
        String_View tmp = sv_concat_suffix_temp(ctx, pkg, ".cmake");
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), tmp.count + 5);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
        memcpy(buf, "Find", 4);
        memcpy(buf + 4, tmp.data, tmp.count);
        buf[tmp.count + 4] = '\0';
        module_name = nob_sv_from_cstr(buf);
    }

    String_View fallback = eval_sv_path_join(eval_temp_arena(ctx), current_src, nob_sv_from_cstr("CMake"));
    String_View search = module_paths;
    if (fallback.count > 0) {
        if (search.count > 0) {
            search = eval_sv_join_semi_temp(ctx, (String_View[]){module_paths, fallback}, 2);
        } else {
            search = fallback;
        }
    }

    const char *p = search.data;
    const char *end = search.data + search.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View dir = nob_sv_from_parts(p, (size_t)(q - p));
        if (dir.count > 0) {
            if (!eval_sv_is_abs_path(dir)) {
                dir = eval_sv_path_join(eval_temp_arena(ctx), current_src, dir);
            }
            String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dir, module_name);
            if (file_exists_sv(ctx, candidate)) {
                *out_path = candidate;
                return true;
            }
        }
        if (q >= end) break;
        p = q + 1;
    }
    return false;
}

static bool find_package_try_config(Evaluator_Context *ctx, String_View pkg, String_View *out_path) {
    String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;

    String_View config_name = sv_concat_suffix_temp(ctx, pkg, "Config.cmake");

    String_View dir_var = sv_concat_suffix_temp(ctx, pkg, "_DIR");
    String_View pkg_dir = eval_var_get(ctx, dir_var);
    if (pkg_dir.count > 0) {
        String_View dir = pkg_dir;
        if (!eval_sv_is_abs_path(dir)) {
            dir = eval_sv_path_join(eval_temp_arena(ctx), current_src, dir);
        }
        String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dir, config_name);
        if (file_exists_sv(ctx, candidate)) {
            *out_path = candidate;
            return true;
        }
    }

    String_View prefixes = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_PREFIX_PATH"));
    const char *p = prefixes.data;
    const char *end = prefixes.data + prefixes.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View prefix = nob_sv_from_parts(p, (size_t)(q - p));
        if (prefix.count > 0) {
            if (!eval_sv_is_abs_path(prefix)) {
                prefix = eval_sv_path_join(eval_temp_arena(ctx), current_src, prefix);
            }
            String_View c1 = eval_sv_path_join(eval_temp_arena(ctx), prefix, config_name);
            if (file_exists_sv(ctx, c1)) {
                *out_path = c1;
                return true;
            }
            String_View pkg_subdir = eval_sv_path_join(eval_temp_arena(ctx), prefix, pkg);
            String_View c2 = eval_sv_path_join(eval_temp_arena(ctx), pkg_subdir, config_name);
            if (file_exists_sv(ctx, c2)) {
                *out_path = c2;
                return true;
            }
        }
        if (q >= end) break;
        p = q + 1;
    }

    return false;
}

static bool h_find_package(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("find_package() missing package name"),
                       nob_sv_from_cstr("Usage: find_package(<Pkg> [REQUIRED] [MODULE|CONFIG])"));
        return !eval_should_stop(ctx);
    }

    String_View pkg = a.items[0];
    bool required = false;
    bool quiet = false;
    bool module_mode = false;
    bool config_mode = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "REQUIRED")) required = true;
        else if (eval_sv_eq_ci_lit(a.items[i], "QUIET")) quiet = true;
        else if (eval_sv_eq_ci_lit(a.items[i], "MODULE")) module_mode = true;
        else if (eval_sv_eq_ci_lit(a.items[i], "CONFIG")) config_mode = true;
    }

    String_View mode = nob_sv_from_cstr("AUTO");
    if (module_mode && !config_mode) mode = nob_sv_from_cstr("MODULE");
    if (config_mode && !module_mode) mode = nob_sv_from_cstr("CONFIG");

    bool found = false;
    String_View found_path = nob_sv_from_cstr("");
    if (eval_sv_eq_ci_lit(mode, "MODULE")) {
        found = find_package_try_module(ctx, pkg, &found_path);
    } else if (eval_sv_eq_ci_lit(mode, "CONFIG")) {
        found = find_package_try_config(ctx, pkg, &found_path);
    } else {
        found = find_package_try_module(ctx, pkg, &found_path);
        if (!found) found = find_package_try_config(ctx, pkg, &found_path);
    }

    String_View found_key = sv_concat_suffix_temp(ctx, pkg, "_FOUND");
    String_View dir_key = sv_concat_suffix_temp(ctx, pkg, "_DIR");
    String_View cfg_key = sv_concat_suffix_temp(ctx, pkg, "_CONFIG");

    if (found) {
        String_View dir = sv_dirname(found_path);
        (void)eval_var_set(ctx, found_key, nob_sv_from_cstr("1"));
        (void)eval_var_set(ctx, dir_key, dir);
        (void)eval_var_set(ctx, cfg_key, found_path);
        if (!eval_execute_file(ctx, found_path, false, nob_sv_from_cstr(""))) {
            found = false;
            (void)eval_var_set(ctx, found_key, nob_sv_from_cstr("0"));
        }
    } else {
        (void)eval_var_set(ctx, found_key, nob_sv_from_cstr("0"));
    }

    if (!found && required) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Required package not found"),
                       pkg);
        eval_request_stop(ctx);
    } else if (!found && !quiet) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Package not found"),
                       pkg);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_FIND_PACKAGE;
    ev.origin = o;
    ev.as.find_package.package_name = sv_copy_to_event_arena(ctx, pkg);
    ev.as.find_package.mode = sv_copy_to_event_arena(ctx, mode);
    ev.as.find_package.required = required;
    ev.as.find_package.found = found;
    ev.as.find_package.location = sv_copy_to_event_arena(ctx, found_path);
    (void)emit_event(ctx, ev);
    return !eval_should_stop(ctx);
}

typedef bool (*Cmd_Handler)(Evaluator_Context *ctx, const Node *node);

typedef struct {
    const char *name;
    Cmd_Handler fn;
} Command_Entry;

static const Command_Entry DISPATCH[] = {
    {"add_compile_options", h_add_compile_options},
    {"add_definitions", h_add_definitions},
    {"add_executable", h_add_executable},
    {"add_library", h_add_library},
    {"add_subdirectory", h_add_subdirectory},
    {"break", h_break},
    {"continue", h_continue},
    {"file", h_file},
    {"find_package", h_find_package},
    {"include", h_include},
    {"list", h_list},
    {"math", h_math},
    {"message", h_message},
    {"project", h_project},
    {"return", h_return},
    {"set", h_set},
    {"string", h_string},
    {"target_compile_definitions", h_target_compile_definitions},
    {"target_compile_options", h_target_compile_options},
    {"target_include_directories", h_target_include_directories},
    {"target_link_libraries", h_target_link_libraries},
};
static const size_t DISPATCH_COUNT = sizeof(DISPATCH) / sizeof(DISPATCH[0]);

bool eval_dispatcher_is_known_command(String_View name) {
    for (size_t i = 0; i < DISPATCH_COUNT; i++) {
        if (eval_sv_eq_ci_lit(name, DISPATCH[i].name)) return true;
    }
    return false;
}

bool eval_dispatch_command(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node || node->kind != NODE_COMMAND) return false;

    for (size_t i = 0; i < DISPATCH_COUNT; i++) {
        if (eval_sv_eq_ci_lit(node->as.cmd.name, DISPATCH[i].name)) {
            if (!DISPATCH[i].fn(ctx, node)) return false;
            return !eval_should_stop(ctx);
        }
    }

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    User_Command *user = eval_user_cmd_find(ctx, node->as.cmd.name);
    if (user) {
        SV_List args = (user->kind == USER_CMD_MACRO)
            ? eval_resolve_args_literal(ctx, &node->as.cmd.args)
            : eval_resolve_args(ctx, &node->as.cmd.args);
        if (eval_should_stop(ctx)) return false;
        if (eval_user_cmd_invoke(ctx, node->as.cmd.name, &args, o)) {
            return true;
        }
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("Unknown command"),
                   nob_sv_from_cstr("Ignored during evaluation"));
    return true;
}
