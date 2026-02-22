#include "eval_dispatcher.h"
#include "evaluator_internal.h"
#include "eval_file.h"
#include "eval_stdlib.h"
#include "eval_vars.h"
#include "eval_diag.h"
#include "eval_flow.h"
#include "eval_expr.h"
#include "arena_dyn.h"

#include <string.h>
#include <stdlib.h>
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
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
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

static bool sv_list_push_temp(Evaluator_Context *ctx, SV_List *list, String_View sv) {
    if (!ctx || !list) return false;
    if (!arena_da_reserve(eval_temp_arena(ctx), (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) {
        return ctx_oom(ctx);
    }
    list->items[list->count++] = sv;
    return true;
}

static String_View sv_join_no_sep_temp(Evaluator_Context *ctx, const String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += items[i].count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].count == 0) continue;
        memcpy(buf + off, items[i].data, items[i].count);
        off += items[i].count;
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View sv_join_space_temp(Evaluator_Context *ctx, const String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        total += items[i].count;
        if (i + 1 < count) total += 1;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].count > 0) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
        if (i + 1 < count) {
            buf[off++] = ' ';
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View sv_to_lower_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) {
        buf[i] = (char)tolower((unsigned char)in.data[i]);
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
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

static bool h_cmake_minimum_required(Evaluator_Context *ctx, const Node *node) {
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

static bool h_cmake_policy(Evaluator_Context *ctx, const Node *node) {
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

static bool h_target_link_options(Evaluator_Context *ctx, const Node *node) {
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

static bool h_target_link_directories(Evaluator_Context *ctx, const Node *node) {
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

static bool h_set_target_properties(Evaluator_Context *ctx, const Node *node) {
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
        for (size_t i = kv_start; i + 1 < a.count; i += 2) {
            if (!emit_target_prop_set(ctx, o, tgt, a.items[i], a.items[i + 1], EV_PROP_SET)) {
                return !eval_should_stop(ctx);
            }
        }
    }

    return !eval_should_stop(ctx);
}

static bool h_set_property(Evaluator_Context *ctx, const Node *node) {
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
                       nob_sv_from_cstr("Usage: set_property(TARGET <t...> [APPEND|APPEND_STRING] PROPERTY <k> [v...])"));
        return !eval_should_stop(ctx);
    }

    if (!eval_sv_eq_ci_lit(a.items[0], "TARGET")) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() V1 supports only TARGET scope"),
                       nob_sv_from_cstr("GLOBAL/DIRECTORY/SOURCE/INSTALL/TEST/CACHE scopes are ignored in v2"));
        return !eval_should_stop(ctx);
    }

    bool append = false;
    bool append_string = false;
    SV_List targets = {0};

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
        if (!sv_list_push_temp(ctx, &targets, a.items[i])) return !eval_should_stop(ctx);
    }

    if (targets.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(TARGET ...) requires at least one target"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    if (i >= a.count || !eval_sv_eq_ci_lit(a.items[i], "PROPERTY")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(TARGET ...) missing PROPERTY keyword"),
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
                       nob_sv_from_cstr("set_property(TARGET ...) missing property key"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View key = a.items[i++];
    String_View value = nob_sv_from_cstr("");
    if (i < a.count) {
        if (append_string) value = sv_join_no_sep_temp(ctx, &a.items[i], a.count - i);
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

    for (size_t ti = 0; ti < targets.count; ti++) {
        if (!emit_target_prop_set(ctx, o, targets.items[ti], key, value, op)) {
            return !eval_should_stop(ctx);
        }
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

static bool h_add_link_options(Evaluator_Context *ctx, const Node *node) {
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

static bool h_link_libraries(Evaluator_Context *ctx, const Node *node) {
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

static bool h_include_directories(Evaluator_Context *ctx, const Node *node) {
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

static bool h_link_directories(Evaluator_Context *ctx, const Node *node) {
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

static bool h_enable_testing(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count > 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("enable_testing() does not expect arguments"),
                       nob_sv_from_cstr("Extra arguments are ignored"));
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TESTING_ENABLE;
    ev.origin = o;
    ev.as.testing_enable.enabled = true;
    (void)eval_var_set(ctx, nob_sv_from_cstr("BUILD_TESTING"), nob_sv_from_cstr("1"));
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

static bool h_add_test(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_test() requires at least test name and command"),
                       nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>) or add_test(<name> <cmd...>)"));
        return !eval_should_stop(ctx);
    }

    String_View name = nob_sv_from_cstr("");
    String_View command = nob_sv_from_cstr("");
    String_View working_dir = nob_sv_from_cstr("");
    bool command_expand_lists = false;

    if (eval_sv_eq_ci_lit(a.items[0], "NAME")) {
        if (a.count < 4) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) requires COMMAND clause"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>)"));
            return !eval_should_stop(ctx);
        }
        name = a.items[1];

        size_t cmd_i = 2;
        if (!eval_sv_eq_ci_lit(a.items[cmd_i], "COMMAND")) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) missing COMMAND"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>)"));
            return !eval_should_stop(ctx);
        }
        cmd_i++;
        size_t cmd_start = cmd_i;
        size_t cmd_end = a.count;
        for (size_t i = cmd_i; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "WORKING_DIRECTORY") ||
                eval_sv_eq_ci_lit(a.items[i], "COMMAND_EXPAND_LISTS")) {
                cmd_end = i;
                break;
            }
        }
        if (cmd_end <= cmd_start) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) has empty COMMAND"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        command = sv_join_space_temp(ctx, &a.items[cmd_start], cmd_end - cmd_start);

        size_t i = cmd_end;
        while (i < a.count) {
            if (eval_sv_eq_ci_lit(a.items[i], "WORKING_DIRECTORY")) {
                if (i + 1 >= a.count) {
                    eval_emit_diag(ctx,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("dispatcher"),
                                   node->as.cmd.name,
                                   o,
                                   nob_sv_from_cstr("add_test() missing value after WORKING_DIRECTORY"),
                                   nob_sv_from_cstr(""));
                    return !eval_should_stop(ctx);
                }
                working_dir = a.items[i + 1];
                i += 2;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "COMMAND_EXPAND_LISTS")) {
                command_expand_lists = true;
                i++;
                continue;
            }
            eval_emit_diag(ctx,
                           EV_DIAG_WARNING,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test() has unsupported/extra argument"),
                           a.items[i]);
            i++;
        }
    } else {
        name = a.items[0];
        command = sv_join_space_temp(ctx, &a.items[1], a.count - 1);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TEST_ADD;
    ev.origin = o;
    ev.as.test_add.name = sv_copy_to_event_arena(ctx, name);
    ev.as.test_add.command = sv_copy_to_event_arena(ctx, command);
    ev.as.test_add.working_dir = sv_copy_to_event_arena(ctx, working_dir);
    ev.as.test_add.command_expand_lists = command_expand_lists;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

static bool h_install(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() requires rule type, items and DESTINATION"),
                       nob_sv_from_cstr("Usage: install(TARGETS|FILES|PROGRAMS|DIRECTORY <items...> DESTINATION <dir>)"));
        return !eval_should_stop(ctx);
    }

    Cmake_Install_Rule_Type rule_type = EV_INSTALL_RULE_TARGET;
    if (eval_sv_eq_ci_lit(a.items[0], "TARGETS")) rule_type = EV_INSTALL_RULE_TARGET;
    else if (eval_sv_eq_ci_lit(a.items[0], "FILES")) rule_type = EV_INSTALL_RULE_FILE;
    else if (eval_sv_eq_ci_lit(a.items[0], "PROGRAMS")) rule_type = EV_INSTALL_RULE_PROGRAM;
    else if (eval_sv_eq_ci_lit(a.items[0], "DIRECTORY")) rule_type = EV_INSTALL_RULE_DIRECTORY;
    else {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() unsupported rule type in v2"),
                       a.items[0]);
        return !eval_should_stop(ctx);
    }

    size_t dest_i = a.count;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "DESTINATION")) {
            dest_i = i;
            break;
        }
    }
    if (dest_i == a.count || dest_i + 1 >= a.count) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() missing DESTINATION"),
                       nob_sv_from_cstr("Usage: install(TARGETS|FILES|PROGRAMS|DIRECTORY <items...> DESTINATION <dir>)"));
        return !eval_should_stop(ctx);
    }

    if (dest_i <= 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() has no items before DESTINATION"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View destination = a.items[dest_i + 1];
    for (size_t i = 1; i < dest_i; i++) {
        Cmake_Event ev = {0};
        ev.kind = EV_INSTALL_ADD_RULE;
        ev.origin = o;
        ev.as.install_add_rule.rule_type = rule_type;
        ev.as.install_add_rule.item = sv_copy_to_event_arena(ctx, a.items[i]);
        ev.as.install_add_rule.destination = sv_copy_to_event_arena(ctx, destination);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

static bool h_cpack_add_install_type(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_install_type() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    String_View display_name = nob_sv_from_cstr("");
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "DISPLAY_NAME") && i + 1 < a.count) {
            display_name = a.items[++i];
            continue;
        }
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_install_type() unsupported/extra argument"),
                       a.items[i]);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_CPACK_ADD_INSTALL_TYPE;
    ev.origin = o;
    ev.as.cpack_add_install_type.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_install_type.display_name = sv_copy_to_event_arena(ctx, display_name);
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

static bool h_cpack_add_component_group(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_component_group() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    String_View display_name = nob_sv_from_cstr("");
    String_View description = nob_sv_from_cstr("");
    String_View parent_group = nob_sv_from_cstr("");
    bool expanded = false;
    bool bold_title = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "DISPLAY_NAME") && i + 1 < a.count) {
            display_name = a.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "DESCRIPTION") && i + 1 < a.count) {
            description = a.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PARENT_GROUP") && i + 1 < a.count) {
            parent_group = a.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "EXPANDED")) {
            expanded = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "BOLD_TITLE")) {
            bold_title = true;
            continue;
        }
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_component_group() unsupported/extra argument"),
                       a.items[i]);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_CPACK_ADD_COMPONENT_GROUP;
    ev.origin = o;
    ev.as.cpack_add_component_group.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_component_group.display_name = sv_copy_to_event_arena(ctx, display_name);
    ev.as.cpack_add_component_group.description = sv_copy_to_event_arena(ctx, description);
    ev.as.cpack_add_component_group.parent_group = sv_copy_to_event_arena(ctx, parent_group);
    ev.as.cpack_add_component_group.expanded = expanded;
    ev.as.cpack_add_component_group.bold_title = bold_title;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

static bool h_cpack_add_component(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_component() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    String_View display_name = nob_sv_from_cstr("");
    String_View description = nob_sv_from_cstr("");
    String_View group = nob_sv_from_cstr("");
    String_View depends = nob_sv_from_cstr("");
    String_View install_types = nob_sv_from_cstr("");
    bool required = false;
    bool hidden = false;
    bool disabled = false;
    bool downloaded = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "DISPLAY_NAME") && i + 1 < a.count) {
            display_name = a.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "DESCRIPTION") && i + 1 < a.count) {
            description = a.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "GROUP") && i + 1 < a.count) {
            group = a.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "DEPENDS")) {
            size_t start = i + 1;
            size_t end = start;
            while (end < a.count) {
                if (eval_sv_eq_ci_lit(a.items[end], "INSTALL_TYPES") ||
                    eval_sv_eq_ci_lit(a.items[end], "DISPLAY_NAME") ||
                    eval_sv_eq_ci_lit(a.items[end], "DESCRIPTION") ||
                    eval_sv_eq_ci_lit(a.items[end], "GROUP") ||
                    eval_sv_eq_ci_lit(a.items[end], "REQUIRED") ||
                    eval_sv_eq_ci_lit(a.items[end], "HIDDEN") ||
                    eval_sv_eq_ci_lit(a.items[end], "DISABLED") ||
                    eval_sv_eq_ci_lit(a.items[end], "DOWNLOADED")) {
                    break;
                }
                end++;
            }
            depends = (end > start) ? eval_sv_join_semi_temp(ctx, &a.items[start], end - start) : nob_sv_from_cstr("");
            i = end - 1;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INSTALL_TYPES")) {
            size_t start = i + 1;
            size_t end = start;
            while (end < a.count) {
                if (eval_sv_eq_ci_lit(a.items[end], "DEPENDS") ||
                    eval_sv_eq_ci_lit(a.items[end], "DISPLAY_NAME") ||
                    eval_sv_eq_ci_lit(a.items[end], "DESCRIPTION") ||
                    eval_sv_eq_ci_lit(a.items[end], "GROUP") ||
                    eval_sv_eq_ci_lit(a.items[end], "REQUIRED") ||
                    eval_sv_eq_ci_lit(a.items[end], "HIDDEN") ||
                    eval_sv_eq_ci_lit(a.items[end], "DISABLED") ||
                    eval_sv_eq_ci_lit(a.items[end], "DOWNLOADED")) {
                    break;
                }
                end++;
            }
            install_types = (end > start) ? eval_sv_join_semi_temp(ctx, &a.items[start], end - start) : nob_sv_from_cstr("");
            i = end - 1;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "REQUIRED")) {
            required = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "HIDDEN")) {
            hidden = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "DISABLED")) {
            disabled = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "DOWNLOADED")) {
            downloaded = true;
            continue;
        }
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_component() unsupported/extra argument"),
                       a.items[i]);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_CPACK_ADD_COMPONENT;
    ev.origin = o;
    ev.as.cpack_add_component.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_component.display_name = sv_copy_to_event_arena(ctx, display_name);
    ev.as.cpack_add_component.description = sv_copy_to_event_arena(ctx, description);
    ev.as.cpack_add_component.group = sv_copy_to_event_arena(ctx, group);
    ev.as.cpack_add_component.depends = sv_copy_to_event_arena(ctx, depends);
    ev.as.cpack_add_component.install_types = sv_copy_to_event_arena(ctx, install_types);
    ev.as.cpack_add_component.required = required;
    ev.as.cpack_add_component.hidden = hidden;
    ev.as.cpack_add_component.disabled = disabled;
    ev.as.cpack_add_component.downloaded = downloaded;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
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

    String_View scope_source = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (scope_source.count == 0) scope_source = ctx->source_dir;
    String_View scope_binary = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (scope_binary.count == 0) scope_binary = ctx->source_dir;

    if (!emit_dir_push_event(ctx, o, scope_source, scope_binary)) return !eval_should_stop(ctx);
    bool success = eval_execute_file(ctx, file_path, false, nob_sv_from_cstr(""));
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

    String_View scope_binary = binary_dir.count > 0 ? binary_dir : source_dir;
    if (!emit_dir_push_event(ctx, o, source_dir, scope_binary)) return !eval_should_stop(ctx);
    bool success = eval_execute_file(ctx, full_path, true, binary_dir);
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

static bool find_package_try_module(Evaluator_Context *ctx,
                                    String_View pkg,
                                    String_View extra_paths,
                                    bool no_default_path,
                                    String_View *out_path) {
    String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;
    String_View module_paths = extra_paths;
    if (!no_default_path) {
        String_View var_module_paths = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_MODULE_PATH"));
        if (module_paths.count > 0 && var_module_paths.count > 0) {
            module_paths = eval_sv_join_semi_temp(ctx, (String_View[]){module_paths, var_module_paths}, 2);
        } else if (module_paths.count == 0) {
            module_paths = var_module_paths;
        }
    }

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

    String_View fallback = no_default_path
        ? nob_sv_from_cstr("")
        : eval_sv_path_join(eval_temp_arena(ctx), current_src, nob_sv_from_cstr("CMake"));
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

static void find_package_push_prefix(String_View *items, size_t *io_count, size_t cap, String_View v) {
    if (!items || !io_count || *io_count >= cap) return;
    if (v.count == 0) return;
    items[(*io_count)++] = v;
}

static void find_package_push_prefix_variants(Evaluator_Context *ctx,
                                              String_View *items,
                                              size_t *io_count,
                                              size_t cap,
                                              String_View root) {
    if (!ctx || root.count == 0) return;
    find_package_push_prefix(items, io_count, cap, root);
    find_package_push_prefix(items, io_count, cap, eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("lib/cmake")));
    find_package_push_prefix(items, io_count, cap, eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("lib64/cmake")));
    find_package_push_prefix(items, io_count, cap, eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("share/cmake")));
}

static void find_package_push_env_prefix_variants(Evaluator_Context *ctx,
                                                  String_View *items,
                                                  size_t *io_count,
                                                  size_t cap,
                                                  const char *env_name) {
    if (!ctx || !env_name) return;
    const char *raw = getenv(env_name);
    if (!raw || raw[0] == '\0') return;
    String_View root = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(raw));
    if (eval_should_stop(ctx)) return;
    find_package_push_prefix_variants(ctx, items, io_count, cap, root);
}

static bool find_package_try_config_in_prefixes(Evaluator_Context *ctx,
                                                String_View current_src,
                                                String_View pkg,
                                                String_View *config_names,
                                                size_t config_name_count,
                                                String_View prefixes,
                                                String_View *out_path) {
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
            for (size_t ni = 0; ni < config_name_count; ni++) {
                String_View config_name = config_names[ni];
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
        }
        if (q >= end) break;
        p = q + 1;
    }
    return false;
}

static bool find_package_try_config(Evaluator_Context *ctx,
                                    String_View pkg,
                                    String_View extra_prefixes,
                                    bool no_default_path,
                                    String_View *out_path) {
    String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;

    String_View config_names[3] = {0};
    size_t config_name_count = 0;
    config_names[config_name_count++] = sv_concat_suffix_temp(ctx, pkg, "Config.cmake");
    config_names[config_name_count++] = sv_concat_suffix_temp(ctx, pkg, "-config.cmake");
    String_View lower_pkg = sv_to_lower_temp(ctx, pkg);
    if (lower_pkg.count > 0) {
        config_names[config_name_count++] = sv_concat_suffix_temp(ctx, lower_pkg, "-config.cmake");
    }

    String_View dir_var = sv_concat_suffix_temp(ctx, pkg, "_DIR");
    String_View pkg_dir = eval_var_get(ctx, dir_var);
    if (pkg_dir.count > 0) {
        String_View dir = pkg_dir;
        if (!eval_sv_is_abs_path(dir)) {
            dir = eval_sv_path_join(eval_temp_arena(ctx), current_src, dir);
        }
        for (size_t ni = 0; ni < config_name_count; ni++) {
            String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dir, config_names[ni]);
            if (file_exists_sv(ctx, candidate)) {
                *out_path = candidate;
                return true;
            }
        }
    }

    String_View merged_prefixes[64] = {0};
    size_t merged_count = 0;
    find_package_push_prefix(merged_prefixes, &merged_count, 64, extra_prefixes);
    if (!no_default_path) {
        String_View prefixes = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_PREFIX_PATH"));
        find_package_push_prefix(merged_prefixes, &merged_count, 64, prefixes);
    }

    if (!no_default_path) {
#if defined(_WIN32)
        find_package_push_env_prefix_variants(ctx, merged_prefixes, &merged_count, 64, "ProgramFiles");
        find_package_push_env_prefix_variants(ctx, merged_prefixes, &merged_count, 64, "ProgramFiles(x86)");
        find_package_push_env_prefix_variants(ctx, merged_prefixes, &merged_count, 64, "ProgramW6432");
        find_package_push_env_prefix_variants(ctx, merged_prefixes, &merged_count, 64, "VCPKG_ROOT");
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, 64, nob_sv_from_cstr("C:/Program Files"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, 64, nob_sv_from_cstr("C:/Program Files (x86)"));
#else
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, 64, nob_sv_from_cstr("/usr/local"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, 64, nob_sv_from_cstr("/usr"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, 64, nob_sv_from_cstr("/opt/local"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, 64, nob_sv_from_cstr("/opt/homebrew"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, 64, nob_sv_from_cstr("/opt"));
#endif
    }

    String_View all_prefixes = eval_sv_join_semi_temp(ctx, merged_prefixes, merged_count);
    if (eval_should_stop(ctx)) return false;
    if (find_package_try_config_in_prefixes(ctx, current_src, pkg, config_names, config_name_count, all_prefixes, out_path)) {
        return true;
    }

    return false;
}

typedef struct {
    String_View pkg;
    String_View mode; // MODULE / CONFIG / AUTO
    bool required;
    bool quiet;
    String_View requested_version;
    bool exact_version;
    String_View components;
    String_View optional_components;
    String_View extra_prefixes;
    bool no_default_path;
    bool saw_registry_view;
} Find_Package_Options;

static bool find_package_is_option_keyword(String_View t) {
    return eval_sv_eq_ci_lit(t, "REQUIRED") ||
           eval_sv_eq_ci_lit(t, "QUIET") ||
           eval_sv_eq_ci_lit(t, "MODULE") ||
           eval_sv_eq_ci_lit(t, "CONFIG") ||
           eval_sv_eq_ci_lit(t, "EXACT") ||
           eval_sv_eq_ci_lit(t, "COMPONENTS") ||
           eval_sv_eq_ci_lit(t, "OPTIONAL_COMPONENTS") ||
           eval_sv_eq_ci_lit(t, "HINTS") ||
           eval_sv_eq_ci_lit(t, "PATHS") ||
           eval_sv_eq_ci_lit(t, "NO_DEFAULT_PATH") ||
           eval_sv_eq_ci_lit(t, "REGISTRY_VIEW");
}

static bool find_package_looks_like_version(String_View t) {
    if (t.count == 0) return false;
    bool saw_digit = false;
    for (size_t i = 0; i < t.count; i++) {
        char c = t.data[i];
        if (isdigit((unsigned char)c)) {
            saw_digit = true;
            continue;
        }
        if (c == '.' || c == '_' || c == '-' || isalpha((unsigned char)c)) continue;
        return false;
    }
    return saw_digit;
}

static int find_package_version_part_cmp(String_View a, String_View b) {
    bool da = true;
    bool db = true;
    for (size_t i = 0; i < a.count; i++) if (!isdigit((unsigned char)a.data[i])) da = false;
    for (size_t i = 0; i < b.count; i++) if (!isdigit((unsigned char)b.data[i])) db = false;

    if (da && db) {
        size_t ia = 0, ib = 0;
        while (ia < a.count && a.data[ia] == '0') ia++;
        while (ib < b.count && b.data[ib] == '0') ib++;
        size_t na = a.count - ia;
        size_t nb = b.count - ib;
        if (na < nb) return -1;
        if (na > nb) return 1;
        if (na == 0) return 0;
        int c = memcmp(a.data + ia, b.data + ib, na);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }

    size_t n = a.count < b.count ? a.count : b.count;
    int c = memcmp(a.data, b.data, n);
    if (c != 0) return c < 0 ? -1 : 1;
    if (a.count < b.count) return -1;
    if (a.count > b.count) return 1;
    return 0;
}

static int find_package_version_cmp(String_View a, String_View b) {
    size_t pa = 0, pb = 0;
    for (;;) {
        String_View sa = nob_sv_from_cstr("");
        String_View sb = nob_sv_from_cstr("");
        if (pa < a.count) {
            size_t s = pa;
            while (pa < a.count && a.data[pa] != '.') pa++;
            sa = nob_sv_from_parts(a.data + s, pa - s);
            if (pa < a.count) pa++;
        }
        if (pb < b.count) {
            size_t s = pb;
            while (pb < b.count && b.data[pb] != '.') pb++;
            sb = nob_sv_from_parts(b.data + s, pb - s);
            if (pb < b.count) pb++;
        }
        if (sa.count == 0 && sb.count == 0) return 0;
        int c = find_package_version_part_cmp(sa, sb);
        if (c != 0) return c;
    }
}

static Find_Package_Options find_package_parse_options(Evaluator_Context *ctx, SV_List args) {
    Find_Package_Options out = {0};
    out.pkg = args.count > 0 ? args.items[0] : nob_sv_from_cstr("");
    out.mode = nob_sv_from_cstr("AUTO");

    bool module_mode = false;
    bool config_mode = false;
    String_View comps[64] = {0};
    size_t comps_n = 0;
    String_View opt_comps[64] = {0};
    size_t opt_comps_n = 0;
    String_View prefixes[64] = {0};
    size_t prefixes_n = 0;

    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "REQUIRED")) out.required = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "QUIET")) out.quiet = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "MODULE")) module_mode = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "CONFIG")) config_mode = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "EXACT")) out.exact_version = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "NO_DEFAULT_PATH")) out.no_default_path = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "REGISTRY_VIEW")) {
            out.saw_registry_view = true;
            if (i + 1 < args.count && !find_package_is_option_keyword(args.items[i + 1])) i++;
        } else if (eval_sv_eq_ci_lit(args.items[i], "COMPONENTS")) {
            for (size_t j = i + 1; j < args.count; j++) {
                if (find_package_is_option_keyword(args.items[j])) break;
                if (comps_n < 64) comps[comps_n++] = args.items[j];
                i = j;
            }
        } else if (eval_sv_eq_ci_lit(args.items[i], "OPTIONAL_COMPONENTS")) {
            for (size_t j = i + 1; j < args.count; j++) {
                if (find_package_is_option_keyword(args.items[j])) break;
                if (opt_comps_n < 64) opt_comps[opt_comps_n++] = args.items[j];
                i = j;
            }
        } else if (eval_sv_eq_ci_lit(args.items[i], "HINTS") || eval_sv_eq_ci_lit(args.items[i], "PATHS")) {
            for (size_t j = i + 1; j < args.count; j++) {
                if (find_package_is_option_keyword(args.items[j])) break;
                if (prefixes_n < 64) prefixes[prefixes_n++] = args.items[j];
                i = j;
            }
        } else if (out.requested_version.count == 0 && find_package_looks_like_version(args.items[i])) {
            out.requested_version = args.items[i];
        }
    }

    if (module_mode && !config_mode) out.mode = nob_sv_from_cstr("MODULE");
    if (config_mode && !module_mode) out.mode = nob_sv_from_cstr("CONFIG");
    if (comps_n > 0) out.components = eval_sv_join_semi_temp(ctx, comps, comps_n);
    if (opt_comps_n > 0) out.optional_components = eval_sv_join_semi_temp(ctx, opt_comps, opt_comps_n);
    if (prefixes_n > 0) out.extra_prefixes = eval_sv_join_semi_temp(ctx, prefixes, prefixes_n);
    return out;
}

static bool find_package_resolve(Evaluator_Context *ctx,
                                 const Find_Package_Options *opt,
                                 String_View *out_found_path) {
    if (!ctx || !opt || !out_found_path) return false;
    *out_found_path = nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(opt->mode, "MODULE")) {
        return find_package_try_module(ctx, opt->pkg, opt->extra_prefixes, opt->no_default_path, out_found_path);
    }
    if (eval_sv_eq_ci_lit(opt->mode, "CONFIG")) {
        return find_package_try_config(ctx, opt->pkg, opt->extra_prefixes, opt->no_default_path, out_found_path);
    }

    bool found = find_package_try_module(ctx, opt->pkg, opt->extra_prefixes, opt->no_default_path, out_found_path);
    if (!found) found = find_package_try_config(ctx, opt->pkg, opt->extra_prefixes, opt->no_default_path, out_found_path);
    return found;
}

static String_View find_package_guess_version_path(Evaluator_Context *ctx,
                                                   String_View config_path) {
    if (!ctx || config_path.count == 0) return nob_sv_from_cstr("");
    String_View dir = sv_dirname(config_path);
    size_t name_off = 0;
    for (size_t i = config_path.count; i-- > 0;) {
        char c = config_path.data[i];
        if (c == '/' || c == '\\') {
            name_off = i + 1;
            break;
        }
    }
    size_t name_len = config_path.count - name_off;

    String_View base = nob_sv_from_parts(config_path.data + name_off, name_len);
    if (base.count >= 12 && eval_sv_eq_ci_lit(nob_sv_from_parts(base.data + base.count - 12, 12), "Config.cmake")) {
        String_View stem = nob_sv_from_parts(base.data, base.count - 12);
        String_View version_name = sv_concat_suffix_temp(ctx, stem, "ConfigVersion.cmake");
        return eval_sv_path_join(eval_temp_arena(ctx), dir, version_name);
    }
    if (base.count >= 13 && eval_sv_eq_ci_lit(nob_sv_from_parts(base.data + base.count - 13, 13), "-config.cmake")) {
        String_View stem = nob_sv_from_parts(base.data, base.count - 13);
        String_View version_name = sv_concat_suffix_temp(ctx, stem, "-config-version.cmake");
        return eval_sv_path_join(eval_temp_arena(ctx), dir, version_name);
    }
    return nob_sv_from_cstr("");
}

static bool find_package_requested_version_matches(const Find_Package_Options *opt,
                                                   String_View actual_version) {
    if (!opt || opt->requested_version.count == 0) return true;
    if (actual_version.count == 0) return false;
    int c = find_package_version_cmp(actual_version, opt->requested_version);
    if (opt->exact_version) return c == 0;
    return c >= 0;
}

static void find_package_seed_find_context_vars(Evaluator_Context *ctx, const Find_Package_Options *opt) {
    if (!ctx || !opt) return;
    String_View key_required = sv_concat_suffix_temp(ctx, opt->pkg, "_FIND_REQUIRED");
    String_View key_quiet = sv_concat_suffix_temp(ctx, opt->pkg, "_FIND_QUIETLY");
    String_View key_ver = sv_concat_suffix_temp(ctx, opt->pkg, "_FIND_VERSION");
    String_View key_exact = sv_concat_suffix_temp(ctx, opt->pkg, "_FIND_VERSION_EXACT");
    String_View key_comps = sv_concat_suffix_temp(ctx, opt->pkg, "_FIND_COMPONENTS");
    String_View key_req_comps = sv_concat_suffix_temp(ctx, opt->pkg, "_FIND_REQUIRED_COMPONENTS");
    String_View key_opt_comps = sv_concat_suffix_temp(ctx, opt->pkg, "_FIND_OPTIONAL_COMPONENTS");

    (void)eval_var_set(ctx, key_required, opt->required ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    (void)eval_var_set(ctx, key_quiet, opt->quiet ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    if (opt->requested_version.count > 0) {
        (void)eval_var_set(ctx, key_ver, opt->requested_version);
        (void)eval_var_set(ctx, key_exact, opt->exact_version ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    }
    if (opt->components.count > 0) {
        (void)eval_var_set(ctx, key_comps, opt->components);
        (void)eval_var_set(ctx, key_req_comps, opt->components);
    }
    if (opt->optional_components.count > 0) (void)eval_var_set(ctx, key_opt_comps, opt->optional_components);
}

static void find_package_publish_vars(Evaluator_Context *ctx,
                                      const Find_Package_Options *opt,
                                      bool *io_found,
                                      String_View found_path) {
    String_View found_key = sv_concat_suffix_temp(ctx, opt->pkg, "_FOUND");
    String_View dir_key = sv_concat_suffix_temp(ctx, opt->pkg, "_DIR");
    String_View cfg_key = sv_concat_suffix_temp(ctx, opt->pkg, "_CONFIG");

    if (*io_found) {
        String_View dir = sv_dirname(found_path);
        (void)eval_var_set(ctx, dir_key, dir);
        (void)eval_var_set(ctx, cfg_key, found_path);

        find_package_seed_find_context_vars(ctx, opt);

        bool version_ok = true;
        if (opt->requested_version.count > 0) {
            String_View version_path = find_package_guess_version_path(ctx, found_path);
            if (version_path.count > 0 && file_exists_sv(ctx, version_path)) {
                (void)eval_var_set(ctx, nob_sv_from_cstr("PACKAGE_VERSION"), nob_sv_from_cstr(""));
                (void)eval_var_set(ctx, nob_sv_from_cstr("PACKAGE_VERSION_EXACT"), nob_sv_from_cstr(""));
                (void)eval_var_set(ctx, nob_sv_from_cstr("PACKAGE_VERSION_COMPATIBLE"), nob_sv_from_cstr(""));
                if (!eval_execute_file(ctx, version_path, false, nob_sv_from_cstr(""))) {
                    version_ok = false;
                } else {
                    String_View exact = eval_var_get(ctx, nob_sv_from_cstr("PACKAGE_VERSION_EXACT"));
                    String_View compat = eval_var_get(ctx, nob_sv_from_cstr("PACKAGE_VERSION_COMPATIBLE"));
                    if (opt->exact_version && exact.count > 0) {
                        version_ok = eval_truthy(ctx, exact);
                    } else if (!opt->exact_version && compat.count > 0) {
                        version_ok = eval_truthy(ctx, compat);
                    } else {
                        version_ok = find_package_requested_version_matches(opt, eval_var_get(ctx, nob_sv_from_cstr("PACKAGE_VERSION")));
                    }
                }
            }
        }

        if (version_ok) {
            if (!eval_execute_file(ctx, found_path, false, nob_sv_from_cstr(""))) {
                *io_found = false;
            }
        } else {
            *io_found = false;
        }

        if (*io_found && opt->requested_version.count > 0) {
            String_View pkg_ver_key = sv_concat_suffix_temp(ctx, opt->pkg, "_VERSION");
            String_View actual = eval_var_get(ctx, pkg_ver_key);
            if (actual.count == 0) actual = eval_var_get(ctx, nob_sv_from_cstr("PACKAGE_VERSION"));
            if (!find_package_requested_version_matches(opt, actual)) {
                *io_found = false;
            }
        }
    }

    if (*io_found) {
        if (eval_var_defined(ctx, found_key)) {
            *io_found = eval_truthy(ctx, eval_var_get(ctx, found_key));
        }
    }
    (void)eval_var_set(ctx, found_key, *io_found ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
}

static void find_package_emit_result(Evaluator_Context *ctx,
                                     const Node *node,
                                     Cmake_Event_Origin o,
                                     const Find_Package_Options *opt,
                                     bool found,
                                     String_View found_path) {
    if (!found && opt->required) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Required package not found"),
                       opt->pkg);
        eval_request_stop_on_error(ctx);
    } else if (!found && !opt->quiet) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Package not found"),
                       opt->pkg);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_FIND_PACKAGE;
    ev.origin = o;
    ev.as.find_package.package_name = sv_copy_to_event_arena(ctx, opt->pkg);
    ev.as.find_package.mode = sv_copy_to_event_arena(ctx, opt->mode);
    ev.as.find_package.required = opt->required;
    ev.as.find_package.found = found;
    ev.as.find_package.location = sv_copy_to_event_arena(ctx, found_path);
    (void)emit_event(ctx, ev);
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

    Find_Package_Options opt = find_package_parse_options(ctx, a);
    if (opt.exact_version && opt.requested_version.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("find_package() EXACT specified without version"),
                       nob_sv_from_cstr("EXACT is ignored when no version is requested"));
    }
    if (opt.saw_registry_view) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("find_package() REGISTRY_VIEW is not implemented in v2"),
                       nob_sv_from_cstr("Windows registry-based package discovery is currently unsupported"));
    }
    String_View found_path = nob_sv_from_cstr("");
    bool found = find_package_resolve(ctx, &opt, &found_path);
    find_package_publish_vars(ctx, &opt, &found, found_path);
    find_package_emit_result(ctx, node, o, &opt, found, found_path);
    return !eval_should_stop(ctx);
}

typedef bool (*Cmd_Handler)(Evaluator_Context *ctx, const Node *node);

typedef struct {
    const char *name;
    Cmd_Handler fn;
} Command_Entry;

static const Command_Entry DISPATCH[] = {
    {"add_link_options", h_add_link_options},
    {"add_compile_options", h_add_compile_options},
    {"add_definitions", h_add_definitions},
    {"add_test", h_add_test},
    {"add_executable", h_add_executable},
    {"add_library", h_add_library},
    {"add_subdirectory", h_add_subdirectory},
    {"break", h_break},
    {"cmake_minimum_required", h_cmake_minimum_required},
    {"cmake_policy", h_cmake_policy},
    {"cpack_add_component", h_cpack_add_component},
    {"cpack_add_component_group", h_cpack_add_component_group},
    {"cpack_add_install_type", h_cpack_add_install_type},
    {"continue", h_continue},
    {"enable_testing", h_enable_testing},
    {"file", h_file},
    {"find_package", h_find_package},
    {"include", h_include},
    {"include_directories", h_include_directories},
    {"install", h_install},
    {"list", h_list},
    {"link_directories", h_link_directories},
    {"link_libraries", h_link_libraries},
    {"math", h_math},
    {"message", h_message},
    {"project", h_project},
    {"return", h_return},
    {"set", h_set},
    {"set_property", h_set_property},
    {"set_target_properties", h_set_target_properties},
    {"string", h_string},
    {"target_compile_definitions", h_target_compile_definitions},
    {"target_compile_options", h_target_compile_options},
    {"target_include_directories", h_target_include_directories},
    {"target_link_directories", h_target_link_directories},
    {"target_link_libraries", h_target_link_libraries},
    {"target_link_options", h_target_link_options},
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

