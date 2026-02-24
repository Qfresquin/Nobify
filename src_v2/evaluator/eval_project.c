#include "eval_project.h"

#include "evaluator_internal.h"

#include <string.h>
#include <ctype.h>

static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
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

static bool sv_has_prefix_ci_lit(String_View sv, const char *lit) {
    if (!lit || sv.count == 0) return false;
    size_t n = strlen(lit);
    if (sv.count < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = (char)tolower((unsigned char)sv.data[i]);
        char b = (char)tolower((unsigned char)lit[i]);
        if (a != b) return false;
    }
    return true;
}

static bool is_cmp_policy_id(String_View sv) {
    if (sv.count != 7) return false;
    if (!sv_has_prefix_ci_lit(sv, "CMP")) return false;
    for (size_t i = 3; i < 7; i++) {
        if (!isdigit((unsigned char)sv.data[i])) return false;
    }
    return true;
}

static String_View policy_key_from_id(Evaluator_Context *ctx, String_View policy_id) {
    return sv_concat_suffix_temp(ctx, nob_sv_from_cstr("CMAKE_POLICY_"), eval_sv_to_cstr_temp(ctx, policy_id));
}

static void emit_policy_partial_warning_once(Evaluator_Context *ctx,
                                             Cmake_Event_Origin o,
                                             String_View command_name) {
    String_View warned_key = nob_sv_from_cstr("NOBIFY_POLICY_PARTIAL_WARNED");
    if (eval_var_defined(ctx, warned_key)) return;
    (void)eval_var_set(ctx, warned_key, nob_sv_from_cstr("1"));
    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   command_name,
                   o,
                   nob_sv_from_cstr("Policy semantics are only partially modeled in evaluator v2"),
                   nob_sv_from_cstr("Commands parse and store basic policy state, but policy-driven behavior changes are not fully applied"));
}
bool eval_handle_cmake_minimum_required(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2 || !eval_sv_eq_ci_lit(a.items[0], "VERSION")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_minimum_required() expects VERSION"),
                       nob_sv_from_cstr("Usage: cmake_minimum_required(VERSION <min>[...<max>] [FATAL_ERROR])"));
        return !eval_should_stop(ctx);
    }

    String_View version = a.items[1];
    String_View min_version = version;
    String_View policy_version = version;
    for (size_t i = 0; i + 2 < version.count; i++) {
        if (version.data[i] == '.' && version.data[i + 1] == '.' && version.data[i + 2] == '.') {
            min_version = nob_sv_from_parts(version.data, i);
            policy_version = nob_sv_from_parts(version.data + i + 3, version.count - (i + 3));
            break;
        }
    }
    if (min_version.count == 0) min_version = version;
    if (policy_version.count == 0) policy_version = min_version;

    (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_MINIMUM_REQUIRED_VERSION"), min_version);
    (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), policy_version);

    emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
    return !eval_should_stop(ctx);
}

bool eval_handle_cmake_policy(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_policy() missing subcommand"),
                       nob_sv_from_cstr("Expected one of: VERSION, SET, GET, PUSH, POP"));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "VERSION")) {
        if (a.count < 2) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(VERSION ...) missing version"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), a.items[1]);
        emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "SET")) {
        if (a.count < 3 || !is_cmp_policy_id(a.items[1])) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(SET ...) expects CMP<NNNN> and value"),
                           nob_sv_from_cstr("Usage: cmake_policy(SET CMP0077 NEW)"));
            return !eval_should_stop(ctx);
        }
        String_View value = a.items[2];
        if (!(eval_sv_eq_ci_lit(value, "OLD") || eval_sv_eq_ci_lit(value, "NEW"))) {
            eval_emit_diag(ctx,
                           EV_DIAG_WARNING,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(SET ...) supports only OLD/NEW in v2"),
                           value);
        }
        String_View key = policy_key_from_id(ctx, a.items[1]);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        (void)eval_var_set(ctx, key, value);
        emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "GET")) {
        if (a.count < 3 || !is_cmp_policy_id(a.items[1])) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(GET ...) expects CMP<NNNN> and output variable"),
                           nob_sv_from_cstr("Usage: cmake_policy(GET CMP0077 out_var)"));
            return !eval_should_stop(ctx);
        }
        String_View key = policy_key_from_id(ctx, a.items[1]);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        String_View val = eval_var_get(ctx, key);
        (void)eval_var_set(ctx, a.items[2], val);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "PUSH") || eval_sv_eq_ci_lit(a.items[0], "POP")) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_policy(PUSH/POP) is not implemented in evaluator v2"),
                       nob_sv_from_cstr("Policy stack semantics are currently ignored"));
        emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("Unknown cmake_policy() subcommand"),
                   a.items[0]);
    emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
    return !eval_should_stop(ctx);
}

bool eval_handle_project(Evaluator_Context *ctx, const Node *node) {
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

    String_View project_src_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (project_src_dir.count == 0) project_src_dir = ctx->source_dir;
    String_View project_bin_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (project_bin_dir.count == 0) project_bin_dir = ctx->binary_dir;

    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_NAME"), name);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_VERSION"), version);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_SOURCE_DIR"), project_src_dir);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_BINARY_DIR"), project_bin_dir);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_DESCRIPTION"), desc);

    String_View cmake_project_name = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_PROJECT_NAME"));
    if (cmake_project_name.count == 0) {
        (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_PROJECT_NAME"), name);
    }

    String_View key_src = sv_concat_suffix_temp(ctx, name, "_SOURCE_DIR");
    String_View key_bin = sv_concat_suffix_temp(ctx, name, "_BINARY_DIR");
    String_View key_ver = sv_concat_suffix_temp(ctx, name, "_VERSION");
    (void)eval_var_set(ctx, key_src, project_src_dir);
    (void)eval_var_set(ctx, key_bin, project_bin_dir);
    (void)eval_var_set(ctx, key_ver, version);

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

bool eval_handle_add_executable(Evaluator_Context *ctx, const Node *node) {
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

bool eval_handle_add_library(Evaluator_Context *ctx, const Node *node) {
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

