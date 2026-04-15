#include "eval_project.h"

#include "eval_include.h"
#include "evaluator_internal.h"
#include "sv_utils.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";

static bool project_execute_top_level_include(EvalExecContext *ctx,
                                              const Node *node,
                                              Cmake_Event_Origin origin,
                                              String_View raw_path) {
    if (!ctx || !node || raw_path.count == 0) return false;

    String_View resolved_path = nob_sv_from_cstr("");
    if (!eval_include_resolve_target(ctx, raw_path, &resolved_path) || resolved_path.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_IO_FAILURE,
                                       "dispatcher",
                                       nob_sv_from_cstr("project() could not find CMAKE_PROJECT_TOP_LEVEL_INCLUDES entry"),
                                       raw_path);
        return false;
    }

    String_View scope_source = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (scope_source.count == 0) scope_source = ctx->source_dir;
    String_View scope_binary = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (scope_binary.count == 0) scope_binary = ctx->binary_dir;

    if (!eval_emit_include_begin(ctx, origin, resolved_path, false)) return false;
    if (!eval_emit_dir_push(ctx, origin, scope_source, scope_binary)) return false;

    bool pushed_policy = false;
    if (!eval_policy_push(ctx)) {
        (void)eval_emit_dir_pop(ctx, origin, scope_source, scope_binary);
        return false;
    }
    pushed_policy = true;

    String_View saved_context_file = ctx->dependency_provider_context_file;
    ctx->dependency_provider_context_file = sv_copy_to_event_arena(ctx, resolved_path);
    if (eval_should_stop(ctx)) {
        ctx->dependency_provider_context_file = saved_context_file;
        if (pushed_policy) (void)eval_policy_pop(ctx);
        (void)eval_emit_dir_pop(ctx, origin, scope_source, scope_binary);
        return false;
    }

    Eval_Result exec_res = eval_execute_file(ctx, resolved_path, false, nob_sv_from_cstr(""));
    ctx->dependency_provider_context_file = saved_context_file;

    bool success = !eval_result_is_fatal(exec_res);
    if (pushed_policy && !eval_policy_pop(ctx) && !eval_should_stop(ctx)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_POLICY_CONFLICT,
                                       "dispatcher",
                                       nob_sv_from_cstr("project() failed to restore policy stack after CMAKE_PROJECT_TOP_LEVEL_INCLUDES"),
                                       resolved_path);
    }
    if (!eval_emit_dir_pop(ctx, origin, scope_source, scope_binary)) return false;
    if (!eval_emit_include_end(ctx, origin, resolved_path, success)) return false;
    if (!success && !eval_should_stop(ctx)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_IO_FAILURE,
                                       "dispatcher",
                                       nob_sv_from_cstr("project() failed to read or evaluate CMAKE_PROJECT_TOP_LEVEL_INCLUDES entry"),
                                       resolved_path);
    }

    return success;
}

static bool project_execute_top_level_includes(EvalExecContext *ctx,
                                               const Node *node,
                                               Cmake_Event_Origin origin) {
    if (!ctx || !node) return false;

    String_View raw_includes = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_PROJECT_TOP_LEVEL_INCLUDES"));
    if (raw_includes.count == 0) return true;

    SV_List include_items = NULL;
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), raw_includes, &include_items)) return false;
    for (size_t i = 0; i < arena_arr_len(include_items); i++) {
        if (include_items[i].count == 0) continue;
        if (!project_execute_top_level_include(ctx, node, origin, include_items[i])) return false;
    }

    if (eval_should_stop(ctx)) return false;

    return true;
}

static bool apply_subdir_system_default_to_target(EvalExecContext *ctx,
                                                  Cmake_Event_Origin o,
                                                  String_View target_name) {
    String_View raw = eval_var_get_visible(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"));
    if (raw.count == 0) return true;
    if (eval_sv_eq_ci_lit(raw, "0") || eval_sv_eq_ci_lit(raw, "FALSE") || eval_sv_eq_ci_lit(raw, "OFF")) {
        return true;
    }
    return eval_emit_target_prop_set(ctx,
                                     o,
                                     target_name,
                                     nob_sv_from_cstr("SYSTEM"),
                                     nob_sv_from_cstr("1"),
                                     EV_PROP_SET);
}

static bool emit_items_from_list(EvalExecContext *ctx,
                                 Cmake_Event_Origin o,
                                 String_View target_name,
                                 String_View list_text,
                                 int kind) {
    if (!ctx || target_name.count == 0 || list_text.count == 0) return true;

    SV_List items = NULL;
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), list_text, &items)) return false;
    if (eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(items); i++) {
        String_View item = items[i];
        if (kind == 0) {
            item = eval_normalize_compile_definition_item(item);
            if (item.count == 0) continue;
            if (!eval_emit_target_compile_definitions(ctx,
                                                      o,
                                                      target_name,
                                                      EV_VISIBILITY_PRIVATE,
                                                      item)) {
                return false;
            }
            continue;
        }

        if (item.count == 0) continue;
        if (!eval_emit_target_compile_options(ctx,
                                              o,
                                              target_name,
                                              EV_VISIBILITY_PRIVATE,
                                              item,
                                              false)) {
            return false;
        }
    }
    return true;
}

static bool apply_global_compile_state_to_target(EvalExecContext *ctx,
                                                 Cmake_Event_Origin o,
                                                 String_View target_name) {
    String_View defs = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_defs_var));
    String_View opts = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_opts_var));
    if (!emit_items_from_list(ctx, o, target_name, defs, 0)) return false;
    if (!emit_items_from_list(ctx, o, target_name, opts, 1)) return false;
    return true;
}

static bool emit_bool_target_prop_true(EvalExecContext *ctx,
                                       Cmake_Event_Origin o,
                                       String_View target_name,
                                       const char *key) {
    String_View stored_true = nob_sv_from_cstr("TRUE");
    if (!eval_emit_target_prop_set(ctx,
                                   o,
                                   target_name,
                                   nob_sv_from_cstr(key),
                                   nob_sv_from_cstr("1"),
                                   EV_PROP_SET)) {
        return false;
    }
    return eval_property_write(ctx,
                               o,
                               nob_sv_from_cstr("TARGET"),
                               target_name,
                               nob_sv_from_cstr(key),
                               stored_true,
                               EV_PROP_SET,
                               false);
}

static bool add_target_name_must_be_new(EvalExecContext *ctx,
                                        String_View cmd_name,
                                        Cmake_Event_Origin o,
                                        String_View target_name) {
    if (!eval_target_known(ctx, target_name)) return true;
    EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("dispatcher"), cmd_name, o, nob_sv_from_cstr("Target name already exists"), target_name);
    return false;
}

static bool add_alias_target_validate(EvalExecContext *ctx,
                                      String_View cmd_name,
                                      Cmake_Event_Origin o,
                                      String_View alias_name,
                                      String_View real_target) {
    if (!eval_target_known(ctx, real_target)) {
        EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, nob_sv_from_cstr("dispatcher"), cmd_name, o, nob_sv_from_cstr("ALIAS target does not exist"), real_target);
        return false;
    }
    if (eval_target_alias_known(ctx, real_target)) {
        EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("dispatcher"), cmd_name, o, nob_sv_from_cstr("ALIAS target cannot reference another ALIAS target"), real_target);
        return false;
    }
    if (!eval_target_register(ctx, alias_name)) return false;
    if (!eval_target_alias_register(ctx, alias_name)) return false;
    if (!eval_target_set_alias(ctx, alias_name, real_target)) return false;
    return true;
}

static bool add_library_default_shared(EvalExecContext *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr("BUILD_SHARED_LIBS"));
    if (v.count == 0) return false;
    if (eval_sv_eq_ci_lit(v, "0") ||
        eval_sv_eq_ci_lit(v, "OFF") ||
        eval_sv_eq_ci_lit(v, "NO") ||
        eval_sv_eq_ci_lit(v, "FALSE") ||
        eval_sv_eq_ci_lit(v, "N") ||
        eval_sv_eq_ci_lit(v, "IGNORE") ||
        eval_sv_eq_ci_lit(v, "NOTFOUND")) {
        return false;
    }
    return true;
}

static const Eval_Semver k_policy_floor_24 = {2, 4, 0, 0};
static const Eval_Semver k_running_cmake_328 = {3, 28, 0, 0};

static bool policy_parse_version_range_strict(String_View token,
                                              String_View *out_min_token,
                                              Eval_Semver *out_min_version,
                                              bool *out_has_max,
                                              String_View *out_max_token,
                                              Eval_Semver *out_max_version) {
    if (!out_min_token || !out_min_version || !out_has_max || !out_max_token || !out_max_version) return false;

    size_t range_pos = token.count;
    bool found = false;
    for (size_t i = 0; i + 2 < token.count; i++) {
        if (token.data[i] == '.' && token.data[i + 1] == '.' && token.data[i + 2] == '.') {
            if (found) return false;
            found = true;
            range_pos = i;
            i += 2;
        }
    }

    *out_has_max = found;
    if (found) {
        *out_min_token = nob_sv_from_parts(token.data, range_pos);
        *out_max_token = nob_sv_from_parts(token.data + range_pos + 3, token.count - (range_pos + 3));
    } else {
        *out_min_token = token;
        *out_max_token = token;
    }

    if (out_min_token->count == 0 || out_max_token->count == 0) return false;
    if (!eval_semver_parse_strict(*out_min_token, out_min_version)) return false;
    if (!eval_semver_parse_strict(*out_max_token, out_max_version)) return false;
    return true;
}

static bool policy_apply_version_defaults(EvalExecContext *ctx, Eval_Semver policy_version) {
    if (!ctx || eval_should_stop(ctx)) return false;
    int first = eval_policy_known_min_id();
    int last = eval_policy_known_max_id();
    for (int policy_no = first; policy_no <= last; policy_no++) {
        char policy_id_buf[8];
        if (snprintf(policy_id_buf, sizeof(policy_id_buf), "CMP%04d", policy_no) != 7) return ctx_oom(ctx);
        String_View policy_id = nob_sv_from_parts(policy_id_buf, 7);
        int intro_major = 0;
        int intro_minor = 0;
        int intro_patch = 0;
        if (!eval_policy_get_intro_version(policy_id, &intro_major, &intro_minor, &intro_patch)) continue;

        Eval_Semver intro = {intro_major, intro_minor, intro_patch, 0};
        Eval_Policy_Status status =
            eval_semver_compare(&intro, &policy_version) <= 0 ? POLICY_STATUS_NEW : POLICY_STATUS_UNSET;
        if (!eval_policy_set_status(ctx, policy_id, status)) return false;
    }
    return true;
}

Eval_Result eval_handle_cmake_minimum_required(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2 || !eval_sv_eq_ci_lit(a[0], "VERSION")) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_minimum_required() expects VERSION"), nob_sv_from_cstr("Usage: cmake_minimum_required(VERSION <min>[...<max>] [FATAL_ERROR])"));
        return eval_result_from_ctx(ctx);
    }
    if (arena_arr_len(a) > 3 || (arena_arr_len(a) == 3 && !eval_sv_eq_ci_lit(a[2], "FATAL_ERROR"))) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "dispatcher", nob_sv_from_cstr("cmake_minimum_required() received invalid arguments"), nob_sv_from_cstr("Usage: cmake_minimum_required(VERSION <min>[...<max>] [FATAL_ERROR])"));
        return eval_result_from_ctx(ctx);
    }

    String_View min_token = nob_sv_from_cstr("");
    String_View max_token = nob_sv_from_cstr("");
    Eval_Semver min_version = {0};
    Eval_Semver max_version = {0};
    bool has_max = false;
    if (!policy_parse_version_range_strict(a[1],
                                           &min_token,
                                           &min_version,
                                           &has_max,
                                           &max_token,
                                           &max_version)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "dispatcher", nob_sv_from_cstr("cmake_minimum_required() received invalid VERSION token"), a[1]);
        return eval_result_from_ctx(ctx);
    }
    if (eval_semver_compare(&min_version, &k_running_cmake_328) > 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_minimum_required() requires a newer CMake than evaluator baseline"), min_token);
        return eval_result_from_ctx(ctx);
    }
    if (has_max && eval_semver_compare(&max_version, &min_version) < 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_minimum_required() requires max version >= min version"), a[1]);
        return eval_result_from_ctx(ctx);
    }

    Eval_Semver policy_version = has_max ? max_version : min_version;
    String_View policy_version_token = has_max ? max_token : min_token;
    if (eval_semver_compare(&policy_version, &k_policy_floor_24) < 0) {
        policy_version = k_policy_floor_24;
        policy_version_token = nob_sv_from_cstr("2.4");
    }

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_MINIMUM_REQUIRED_VERSION"), min_token)) {
        return eval_result_from_ctx(ctx);
    }
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), policy_version_token)) {
        return eval_result_from_ctx(ctx);
    }
    if (!policy_apply_version_defaults(ctx, policy_version)) return eval_result_from_ctx(ctx);
    if (!eval_emit_project_minimum_required(ctx, o, min_token, arena_arr_len(a) == 3)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_cmake_policy(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_policy() missing subcommand"), nob_sv_from_cstr("Expected one of: VERSION, SET, GET, PUSH, POP"));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "VERSION")) {
        if (arena_arr_len(a) != 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_policy(VERSION ...) expects exactly one version argument"), nob_sv_from_cstr("Usage: cmake_policy(VERSION <min>[...<max>])"));
            return eval_result_from_ctx(ctx);
        }

        String_View min_token = nob_sv_from_cstr("");
        String_View max_token = nob_sv_from_cstr("");
        Eval_Semver min_version = {0};
        Eval_Semver max_version = {0};
        bool has_max = false;
        if (!policy_parse_version_range_strict(a[1],
                                               &min_token,
                                               &min_version,
                                               &has_max,
                                               &max_token,
                                               &max_version)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "dispatcher", nob_sv_from_cstr("cmake_policy(VERSION ...) received invalid version token"), a[1]);
            return eval_result_from_ctx(ctx);
        }
        if (eval_semver_compare(&min_version, &k_policy_floor_24) < 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_policy(VERSION ...) requires minimum version >= 2.4"), min_token);
            return eval_result_from_ctx(ctx);
        }
        if (eval_semver_compare(&min_version, &k_running_cmake_328) > 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("cmake_policy(VERSION ...) min version exceeds evaluator baseline"), min_token);
            return eval_result_from_ctx(ctx);
        }
        if (has_max && eval_semver_compare(&max_version, &min_version) < 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_policy(VERSION ...) requires max version >= min version"), a[1]);
            return eval_result_from_ctx(ctx);
        }

        Eval_Semver policy_version = has_max ? max_version : min_version;
        String_View policy_token = has_max ? max_token : min_token;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), policy_token)) return eval_result_from_ctx(ctx);
        if (!policy_apply_version_defaults(ctx, policy_version)) return eval_result_from_ctx(ctx);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "SET")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_policy(SET ...) expects exactly policy id and value"), nob_sv_from_cstr("Usage: cmake_policy(SET CMP0077 NEW)"));
            return eval_result_from_ctx(ctx);
        }
        if (!eval_policy_is_known(a[1])) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_POLICY_CONFLICT, "dispatcher", nob_sv_from_cstr("cmake_policy(SET ...) requires a known CMP policy id"), a[1]);
            return eval_result_from_ctx(ctx);
        }
        if (!(eval_sv_eq_ci_lit(a[2], "OLD") || eval_sv_eq_ci_lit(a[2], "NEW"))) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "dispatcher", nob_sv_from_cstr("cmake_policy(SET ...) requires OLD or NEW"), a[2]);
            return eval_result_from_ctx(ctx);
        }
        if (!eval_policy_set(ctx, a[1], a[2])) return eval_result_from_ctx(ctx);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "GET")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cmake_policy(GET ...) expects exactly policy id and output variable"), nob_sv_from_cstr("Usage: cmake_policy(GET CMP0077 out_var)"));
            return eval_result_from_ctx(ctx);
        }
        if (!eval_policy_is_known(a[1])) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_POLICY_CONFLICT, "dispatcher", nob_sv_from_cstr("cmake_policy(GET ...) requires a known CMP policy id"), a[1]);
            return eval_result_from_ctx(ctx);
        }
        String_View val = eval_policy_get_effective(ctx, a[1]);
        if (!eval_var_set_current(ctx, a[2], val)) return eval_result_from_ctx(ctx);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "PUSH")) {
        if (arena_arr_len(a) != 1) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("cmake_policy(PUSH) does not accept extra arguments"), nob_sv_from_cstr("Usage: cmake_policy(PUSH)"));
            return eval_result_from_ctx(ctx);
        }
        if (!eval_policy_push(ctx)) return eval_result_from_ctx(ctx);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "POP")) {
        if (arena_arr_len(a) != 1) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("cmake_policy(POP) does not accept extra arguments"), nob_sv_from_cstr("Usage: cmake_policy(POP)"));
            return eval_result_from_ctx(ctx);
        }
        if (!eval_policy_pop(ctx)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_POLICY_CONFLICT, "dispatcher", nob_sv_from_cstr("cmake_policy(POP) called without matching PUSH"), nob_sv_from_cstr("Add cmake_policy(PUSH) before POP"));
            eval_request_stop_on_error(ctx);
            return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("Unknown cmake_policy() subcommand"), a[0]);
    eval_request_stop_on_error(ctx);
    return eval_result_from_ctx(ctx);
}

typedef struct {
    bool has_version;
    String_View raw;
    String_View major;
    String_View minor;
    String_View patch;
    String_View tweak;
} Project_Version_Info;

static bool project_parse_version_token(String_View token, Project_Version_Info *out_info) {
    if (!out_info || token.count == 0) return false;

    String_View parts[4] = {0};
    size_t part_count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= token.count; i++) {
        bool at_end = (i == token.count);
        if (!at_end && token.data[i] != '.') continue;

        if (part_count >= 4) return false;
        if (i == start) return false;
        String_View part = nob_sv_from_parts(token.data + start, i - start);
        for (size_t j = 0; j < part.count; j++) {
            char c = part.data[j];
            if (c < '0' || c > '9') return false;
        }
        parts[part_count++] = part;
        start = i + 1;
    }

    if (part_count < 1 || part_count > 4) return false;
    out_info->has_version = true;
    out_info->raw = token;
    out_info->major = parts[0];
    out_info->minor = (part_count >= 2) ? parts[1] : nob_sv_from_cstr("0");
    out_info->patch = (part_count >= 3) ? parts[2] : nob_sv_from_cstr("0");
    out_info->tweak = (part_count >= 4) ? parts[3] : nob_sv_from_cstr("0");
    return true;
}

static bool project_set_prefixed_var(EvalExecContext *ctx,
                                     String_View prefix,
                                     const char *suffix,
                                     String_View value) {
    String_View key = svu_concat_suffix_temp(ctx, prefix, suffix);
    if (eval_should_stop(ctx)) return false;
    return eval_var_set_current(ctx, key, value);
}

static bool language_token_is_known(String_View lang) {
    return eval_sv_eq_ci_lit(lang, "C") ||
           eval_sv_eq_ci_lit(lang, "CXX") ||
           eval_sv_eq_ci_lit(lang, "OBJC") ||
           eval_sv_eq_ci_lit(lang, "OBJCXX") ||
           eval_sv_eq_ci_lit(lang, "CUDA") ||
           eval_sv_eq_ci_lit(lang, "HIP") ||
           eval_sv_eq_ci_lit(lang, "ISPC") ||
           eval_sv_eq_ci_lit(lang, "Fortran") ||
           eval_sv_eq_ci_lit(lang, "Swift") ||
           eval_sv_eq_ci_lit(lang, "CSharp") ||
           eval_sv_eq_ci_lit(lang, "ASM") ||
           eval_sv_eq_ci_lit(lang, "ASM_NASM") ||
           eval_sv_eq_ci_lit(lang, "ASM_MASM") ||
           eval_sv_eq_ci_lit(lang, "ASM_MARMASM") ||
           eval_sv_eq_ci_lit(lang, "ASM-ATT");
}

static bool enabled_languages_contains(SV_List langs, String_View candidate) {
    for (size_t i = 0; i < arena_arr_len(langs); i++) {
        if (eval_sv_key_eq(langs[i], candidate)) return true;
    }
    return false;
}

static String_View language_compiler_loaded_var_temp(EvalExecContext *ctx, String_View lang) {
    static const char prefix[] = "CMAKE_";
    static const char suffix[] = "_COMPILER_LOADED";
    size_t total = (sizeof(prefix) - 1) + lang.count + (sizeof(suffix) - 1);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, sizeof(prefix) - 1);
    off += sizeof(prefix) - 1;
    memcpy(buf + off, lang.data, lang.count);
    off += lang.count;
    memcpy(buf + off, suffix, sizeof(suffix) - 1);
    off += sizeof(suffix) - 1;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool apply_enabled_languages(EvalExecContext *ctx,
                                    Cmake_Event_Origin o,
                                    String_View cmd_name,
                                    const SV_List *requested) {
    if (!ctx || !requested) return false;

    String_View existing_text = eval_var_get_visible(ctx, nob_sv_from_cstr("NOBIFY_ENABLED_LANGUAGES"));
    SV_List enabled = NULL;
    if (existing_text.count > 0) {
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), existing_text, &enabled)) return false;
        if (eval_should_stop(ctx)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(*requested); i++) {
        String_View lang = (*requested)[i];
        if (!language_token_is_known(lang)) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("dispatcher"), cmd_name, o, nob_sv_from_cstr("Unknown language in language-enabling command"), lang);
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        if (!enabled_languages_contains(enabled, lang)) {
            if (!svu_list_push_temp(ctx, &enabled, lang)) return false;
        }

        String_View loaded_var = language_compiler_loaded_var_temp(ctx, lang);
        if (eval_should_stop(ctx)) return false;
        if (!eval_var_set_current(ctx, loaded_var, nob_sv_from_cstr("1"))) return false;
    }

    String_View merged = eval_sv_join_semi_temp(ctx, enabled, arena_arr_len(enabled));
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_ENABLED_LANGUAGES"), merged)) return false;
    if (!eval_property_write(ctx,
                             o,
                             nob_sv_from_cstr("GLOBAL"),
                             nob_sv_from_cstr(""),
                             nob_sv_from_cstr("ENABLED_LANGUAGES"),
                             merged,
                             EV_PROP_SET,
                             false)) {
        return false;
    }
    return true;
}

Eval_Result eval_handle_enable_language(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("enable_language() requires at least one language"), nob_sv_from_cstr("Usage: enable_language(<lang>... [OPTIONAL])"));
        return eval_result_from_ctx(ctx);
    }

    if (ctx->function_eval_depth > 0 || eval_exec_has_active_kind(ctx, EVAL_EXEC_CTX_BLOCK)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_CONTEXT, "dispatcher", nob_sv_from_cstr("enable_language() must be called at file scope"), nob_sv_from_cstr("Do not call enable_language() from inside function() or block() scopes"));
        return eval_result_from_ctx(ctx);
    }

    SV_List requested = NULL;
    bool saw_optional = false;
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "OPTIONAL")) {
            saw_optional = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "NONE")) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("enable_language() does not accept NONE"), nob_sv_from_cstr("Use project(... LANGUAGES NONE) to leave all languages disabled"));
            return eval_result_from_ctx(ctx);
        }
        if (!svu_list_push_temp(ctx, &requested, a[i])) return eval_result_from_ctx(ctx);
    }

    if (arena_arr_len(requested) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("enable_language() requires at least one real language"), nob_sv_from_cstr("Usage: enable_language(<lang>... [OPTIONAL])"));
        return eval_result_from_ctx(ctx);
    }

    if (saw_optional) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "dispatcher", nob_sv_from_cstr("enable_language(OPTIONAL) is not supported"), nob_sv_from_cstr("CMake documents OPTIONAL as a placeholder; use CheckLanguage instead"));
        return eval_result_from_ctx(ctx);
    }

    (void)apply_enabled_languages(ctx, o, node->as.cmd.name, &requested);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_project(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("project() missing name"), nob_sv_from_cstr("Usage: project(<name> [VERSION v] ...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View name = a[0];
    String_View version_token = nob_sv_from_cstr("");
    String_View desc = nob_sv_from_cstr("");
    String_View homepage_url = nob_sv_from_cstr("");
    bool has_version_arg = false;
    bool seen_version = false;
    bool seen_description = false;
    bool seen_homepage = false;
    bool seen_languages_keyword = false;
    bool seen_named_options = false;
    SV_List lang_items = NULL;

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        String_View token = a[i];
        if (eval_sv_eq_ci_lit(token, "VERSION")) {
            seen_named_options = true;
            if (seen_version) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_DUPLICATE_ARGUMENT, "dispatcher", nob_sv_from_cstr("project() received duplicate VERSION keyword"), nob_sv_from_cstr("Use VERSION only once"));
                return eval_result_from_ctx(ctx);
            }
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("project(VERSION ...) requires a version value"), nob_sv_from_cstr("Usage: project(<name> VERSION <major>[.<minor>[.<patch>[.<tweak>]])"));
                return eval_result_from_ctx(ctx);
            }
            seen_version = true;
            has_version_arg = true;
            version_token = a[++i];
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "DESCRIPTION")) {
            seen_named_options = true;
            if (seen_description) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_DUPLICATE_ARGUMENT, "dispatcher", nob_sv_from_cstr("project() received duplicate DESCRIPTION keyword"), nob_sv_from_cstr("Use DESCRIPTION only once"));
                return eval_result_from_ctx(ctx);
            }
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("project(DESCRIPTION ...) requires a value"), nob_sv_from_cstr("Usage: project(<name> DESCRIPTION <text>)"));
                return eval_result_from_ctx(ctx);
            }
            seen_description = true;
            desc = a[++i];
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "HOMEPAGE_URL")) {
            seen_named_options = true;
            if (seen_homepage) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_DUPLICATE_ARGUMENT, "dispatcher", nob_sv_from_cstr("project() received duplicate HOMEPAGE_URL keyword"), nob_sv_from_cstr("Use HOMEPAGE_URL only once"));
                return eval_result_from_ctx(ctx);
            }
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("project(HOMEPAGE_URL ...) requires a value"), nob_sv_from_cstr("Usage: project(<name> HOMEPAGE_URL <url>)"));
                return eval_result_from_ctx(ctx);
            }
            seen_homepage = true;
            homepage_url = a[++i];
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "LANGUAGES")) {
            if (seen_languages_keyword) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_DUPLICATE_ARGUMENT, "dispatcher", nob_sv_from_cstr("project() received duplicate LANGUAGES keyword"), nob_sv_from_cstr("Use LANGUAGES only once"));
                return eval_result_from_ctx(ctx);
            }
            seen_languages_keyword = true;
            for (size_t j = i + 1; j < arena_arr_len(a); j++) {
                if (!svu_list_push_temp(ctx, &lang_items, a[j])) return eval_result_from_ctx(ctx);
            }
            break;
        }

        if (seen_named_options) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("project() received unexpected argument in keyword signature"), token);
            return eval_result_from_ctx(ctx);
        }

        for (size_t j = i; j < arena_arr_len(a); j++) {
            if (!svu_list_push_temp(ctx, &lang_items, a[j])) return eval_result_from_ctx(ctx);
        }
        break;
    }

    Project_Version_Info version_info = {0};
    if (has_version_arg && !project_parse_version_token(version_token, &version_info)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("project(VERSION ...) expects numeric components"), version_token);
        return eval_result_from_ctx(ctx);
    }

    if (arena_arr_len(lang_items) == 0 && !seen_languages_keyword) {
        if (!svu_list_push_temp(ctx, &lang_items, nob_sv_from_cstr("C"))) return eval_result_from_ctx(ctx);
        if (!svu_list_push_temp(ctx, &lang_items, nob_sv_from_cstr("CXX"))) return eval_result_from_ctx(ctx);
    }

    size_t none_count = 0;
    for (size_t i = 0; i < arena_arr_len(lang_items); i++) {
        if (eval_sv_eq_ci_lit(lang_items[i], "NONE")) none_count++;
    }
    if (none_count > 0) {
        if (arena_arr_len(lang_items) != 1 || none_count != 1) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("project() LANGUAGES NONE cannot be combined with other languages"), nob_sv_from_cstr("Use only NONE to skip enabling languages"));
            return eval_result_from_ctx(ctx);
        }
        arena_arr_set_len(lang_items, 0);
    }

    String_View version = has_version_arg ? version_info.raw : nob_sv_from_cstr("");
    String_View version_major = has_version_arg ? version_info.major : nob_sv_from_cstr("");
    String_View version_minor = has_version_arg ? version_info.minor : nob_sv_from_cstr("");
    String_View version_patch = has_version_arg ? version_info.patch : nob_sv_from_cstr("");
    String_View version_tweak = has_version_arg ? version_info.tweak : nob_sv_from_cstr("");
    String_View langs = eval_sv_join_semi_temp(ctx, lang_items, arena_arr_len(lang_items));

    String_View project_src_dir = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (project_src_dir.count == 0) project_src_dir = ctx->source_dir;
    String_View project_bin_dir = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (project_bin_dir.count == 0) project_bin_dir = ctx->binary_dir;
    bool is_top_level = nob_sv_eq(project_src_dir, ctx->source_dir) && nob_sv_eq(project_bin_dir, ctx->binary_dir);
    String_View is_top_level_sv = is_top_level ? nob_sv_from_cstr("TRUE") : nob_sv_from_cstr("FALSE");
    bool cmp0048_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0048);
    bool should_apply_version_vars = has_version_arg || cmp0048_new;
    bool is_first_project_call = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_PROJECT_NAME")).count == 0;

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_NAME"), name)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_SOURCE_DIR"), project_src_dir)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_BINARY_DIR"), project_bin_dir)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_DESCRIPTION"), desc)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_HOMEPAGE_URL"), homepage_url)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_IS_TOP_LEVEL"), is_top_level_sv)) return eval_result_from_ctx(ctx);
    if (should_apply_version_vars) {
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_VERSION"), version)) return eval_result_from_ctx(ctx);
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_VERSION_MAJOR"), version_major)) return eval_result_from_ctx(ctx);
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_VERSION_MINOR"), version_minor)) return eval_result_from_ctx(ctx);
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_VERSION_PATCH"), version_patch)) return eval_result_from_ctx(ctx);
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_VERSION_TWEAK"), version_tweak)) return eval_result_from_ctx(ctx);
    }

    String_View cmake_project_name = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_PROJECT_NAME"));
    if (is_top_level || cmake_project_name.count == 0) {
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PROJECT_NAME"), name)) return eval_result_from_ctx(ctx);
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PROJECT_DESCRIPTION"), desc)) return eval_result_from_ctx(ctx);
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PROJECT_HOMEPAGE_URL"), homepage_url)) return eval_result_from_ctx(ctx);
        if (should_apply_version_vars) {
            if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PROJECT_VERSION"), version)) return eval_result_from_ctx(ctx);
            if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PROJECT_VERSION_MAJOR"), version_major)) return eval_result_from_ctx(ctx);
            if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PROJECT_VERSION_MINOR"), version_minor)) return eval_result_from_ctx(ctx);
            if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PROJECT_VERSION_PATCH"), version_patch)) return eval_result_from_ctx(ctx);
            if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PROJECT_VERSION_TWEAK"), version_tweak)) return eval_result_from_ctx(ctx);
        }
    }

    if (!project_set_prefixed_var(ctx, name, "_SOURCE_DIR", project_src_dir)) return eval_result_from_ctx(ctx);
    if (!project_set_prefixed_var(ctx, name, "_BINARY_DIR", project_bin_dir)) return eval_result_from_ctx(ctx);
    if (!project_set_prefixed_var(ctx, name, "_DESCRIPTION", desc)) return eval_result_from_ctx(ctx);
    if (!project_set_prefixed_var(ctx, name, "_HOMEPAGE_URL", homepage_url)) return eval_result_from_ctx(ctx);
    if (!project_set_prefixed_var(ctx, name, "_IS_TOP_LEVEL", is_top_level_sv)) return eval_result_from_ctx(ctx);
    if (should_apply_version_vars) {
        if (!project_set_prefixed_var(ctx, name, "_VERSION", version)) return eval_result_from_ctx(ctx);
        if (!project_set_prefixed_var(ctx, name, "_VERSION_MAJOR", version_major)) return eval_result_from_ctx(ctx);
        if (!project_set_prefixed_var(ctx, name, "_VERSION_MINOR", version_minor)) return eval_result_from_ctx(ctx);
        if (!project_set_prefixed_var(ctx, name, "_VERSION_PATCH", version_patch)) return eval_result_from_ctx(ctx);
        if (!project_set_prefixed_var(ctx, name, "_VERSION_TWEAK", version_tweak)) return eval_result_from_ctx(ctx);
    }

    if (is_first_project_call && !project_execute_top_level_includes(ctx, node, o)) {
        return eval_result_from_ctx(ctx);
    }
    if (!apply_enabled_languages(ctx, o, node->as.cmd.name, &lang_items)) return eval_result_from_ctx(ctx);

    (void)name;
    (void)version;
    (void)desc;
    (void)langs;
    if (!eval_emit_project_declare(ctx, o, name, version, desc, homepage_url, langs)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_executable(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_executable() missing target name"), nob_sv_from_cstr("Usage: add_executable(<name> [WIN32] [MACOSX_BUNDLE] [EXCLUDE_FROM_ALL] <sources...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View name = a[0];
    if (!add_target_name_must_be_new(ctx, node->as.cmd.name, o, name)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) >= 2 && eval_sv_eq_ci_lit(a[1], "ALIAS")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_executable(ALIAS ...) expects exactly alias name and real target"), nob_sv_from_cstr("Usage: add_executable(<name> ALIAS <target>)"));
            return eval_result_from_ctx(ctx);
        }
        if (!add_alias_target_validate(ctx, node->as.cmd.name, o, name, a[2])) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_declare(ctx,
                                      o,
                                      name,
                                      EV_TARGET_EXECUTABLE,
                                      false,
                                      true,
                                      a[2])) {
            return eval_result_fatal();
        }
        return eval_result_from_ctx(ctx);
    }

    bool is_imported = false;
    bool is_global = false;
    bool is_win32 = false;
    bool is_macos_bundle = false;
    bool is_exclude_from_all = false;
    size_t source_start = 1;

    if (arena_arr_len(a) >= 2 && eval_sv_eq_ci_lit(a[1], "IMPORTED")) {
        is_imported = true;
        source_start = 2;
        if (source_start < arena_arr_len(a) && eval_sv_eq_ci_lit(a[source_start], "GLOBAL")) {
            is_global = true;
            source_start++;
        }
        if (source_start < arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("add_executable(IMPORTED ...) does not accept source files"), nob_sv_from_cstr("Usage: add_executable(<name> IMPORTED [GLOBAL])"));
            return eval_result_from_ctx(ctx);
        }
    }

    if (!is_imported) {
        for (size_t i = 1; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "WIN32")) {
                is_win32 = true;
                source_start = i + 1;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "MACOSX_BUNDLE")) {
                is_macos_bundle = true;
                source_start = i + 1;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "EXCLUDE_FROM_ALL")) {
                is_exclude_from_all = true;
                source_start = i + 1;
                continue;
            }
            source_start = i;
            break;
        }
    }

    (void)eval_target_register(ctx, name);
    if (!eval_target_set_type(ctx, name, EV_TARGET_EXECUTABLE)) return eval_result_from_ctx(ctx);

    if (!eval_target_apply_defined_initializers(ctx, o, name)) return eval_result_from_ctx(ctx);
    if (is_imported) {
        if (!eval_target_set_imported(ctx, name, true)) return eval_result_from_ctx(ctx);
        if (!emit_bool_target_prop_true(ctx, o, name, "IMPORTED")) return eval_result_from_ctx(ctx);
        if (is_global) {
            if (!eval_target_set_imported_global(ctx, name, true)) return eval_result_from_ctx(ctx);
            if (!emit_bool_target_prop_true(ctx, o, name, "IMPORTED_GLOBAL")) return eval_result_from_ctx(ctx);
        }
    } else {
        if (!apply_subdir_system_default_to_target(ctx, o, name)) return eval_result_from_ctx(ctx);
        if (is_win32) {
            if (!emit_bool_target_prop_true(ctx, o, name, "WIN32_EXECUTABLE")) return eval_result_from_ctx(ctx);
        }
        if (is_macos_bundle) {
            if (!emit_bool_target_prop_true(ctx, o, name, "MACOSX_BUNDLE")) return eval_result_from_ctx(ctx);
        }
        if (is_exclude_from_all) {
            if (!emit_bool_target_prop_true(ctx, o, name, "EXCLUDE_FROM_ALL")) return eval_result_from_ctx(ctx);
        }
    }

    if (!is_imported) {
        (void)apply_global_compile_state_to_target(ctx, o, name);
    }
    if (!eval_emit_target_declare(ctx,
                                  o,
                                  name,
                                  EV_TARGET_EXECUTABLE,
                                  is_imported,
                                  false,
                                  nob_sv_from_cstr(""))) {
        return eval_result_fatal();
    }
    if (!is_imported) {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        for (size_t i = source_start; i < arena_arr_len(a); ++i) {
            if (a[i].count == 0) continue;
            String_View path = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            if (!eval_emit_target_add_source(ctx, o, name, path)) return eval_result_from_ctx(ctx);
        }
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_library(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_library() missing target name"), nob_sv_from_cstr("Usage: add_library(<name> [STATIC|SHARED|MODULE|OBJECT|INTERFACE|UNKNOWN] [EXCLUDE_FROM_ALL] <sources...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View name = a[0];
    if (!add_target_name_must_be_new(ctx, node->as.cmd.name, o, name)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) >= 2 && eval_sv_eq_ci_lit(a[1], "ALIAS")) {
        Cmake_Target_Type alias_type = EV_TARGET_LIBRARY_UNKNOWN;
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_library(ALIAS ...) expects exactly alias name and real target"), nob_sv_from_cstr("Usage: add_library(<name> ALIAS <target>)"));
            return eval_result_from_ctx(ctx);
        }
        if (!add_alias_target_validate(ctx, node->as.cmd.name, o, name, a[2])) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_target_get_type(ctx, a[2], &alias_type)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_declare(ctx,
                                      o,
                                      name,
                                      alias_type,
                                      false,
                                      true,
                                      a[2])) {
            return eval_result_fatal();
        }
        return eval_result_from_ctx(ctx);
    }

    (void)eval_target_register(ctx, name);

    Cmake_Target_Type ty = EV_TARGET_LIBRARY_UNKNOWN;
    size_t i = 1;
    bool has_explicit_type = false;
    bool is_imported = false;
    bool is_global = false;
    bool is_exclude_from_all = false;
    if (i < arena_arr_len(a)) {
        if (eval_sv_eq_ci_lit(a[i], "STATIC")) {
            ty = EV_TARGET_LIBRARY_STATIC;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a[i], "SHARED")) {
            ty = EV_TARGET_LIBRARY_SHARED;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a[i], "MODULE")) {
            ty = EV_TARGET_LIBRARY_MODULE;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            ty = EV_TARGET_LIBRARY_INTERFACE;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a[i], "OBJECT")) {
            ty = EV_TARGET_LIBRARY_OBJECT;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a[i], "UNKNOWN")) {
            ty = EV_TARGET_LIBRARY_UNKNOWN;
            has_explicit_type = true;
            i++;
        }
    }

    if (i < arena_arr_len(a) && eval_sv_eq_ci_lit(a[i], "IMPORTED")) {
        is_imported = true;
        i++;
        if (!has_explicit_type) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_library(IMPORTED ...) requires an explicit library type"), nob_sv_from_cstr("Usage: add_library(<name> <STATIC|SHARED|MODULE|OBJECT|INTERFACE|UNKNOWN> IMPORTED [GLOBAL])"));
            return eval_result_from_ctx(ctx);
        }
        if (i < arena_arr_len(a) && eval_sv_eq_ci_lit(a[i], "GLOBAL")) {
            is_global = true;
            i++;
        }
        if (i < arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("add_library(IMPORTED ...) does not accept source files"), nob_sv_from_cstr("Usage: add_library(<name> <type> IMPORTED [GLOBAL])"));
            return eval_result_from_ctx(ctx);
        }
    }

    if (!is_imported) {
        if (!has_explicit_type) {
            ty = add_library_default_shared(ctx) ? EV_TARGET_LIBRARY_SHARED : EV_TARGET_LIBRARY_STATIC;
        }
        if (i < arena_arr_len(a) && eval_sv_eq_ci_lit(a[i], "EXCLUDE_FROM_ALL")) {
            is_exclude_from_all = true;
            i++;
        }
    }

    if (!eval_target_set_type(ctx, name, ty)) return eval_result_from_ctx(ctx);
    if (!eval_target_apply_defined_initializers(ctx, o, name)) return eval_result_from_ctx(ctx);
    if (is_imported) {
        if (!eval_target_set_imported(ctx, name, true)) return eval_result_from_ctx(ctx);
        if (!emit_bool_target_prop_true(ctx, o, name, "IMPORTED")) return eval_result_from_ctx(ctx);
        if (is_global) {
            if (!eval_target_set_imported_global(ctx, name, true)) return eval_result_from_ctx(ctx);
            if (!emit_bool_target_prop_true(ctx, o, name, "IMPORTED_GLOBAL")) return eval_result_from_ctx(ctx);
        }
    } else {
        if (!apply_subdir_system_default_to_target(ctx, o, name)) return eval_result_from_ctx(ctx);
        if (is_exclude_from_all) {
            if (!emit_bool_target_prop_true(ctx, o, name, "EXCLUDE_FROM_ALL")) return eval_result_from_ctx(ctx);
        }
    }

    if (!is_imported) {
        (void)apply_global_compile_state_to_target(ctx, o, name);
    }
    if (!eval_emit_target_declare(ctx,
                                  o,
                                  name,
                                  ty,
                                  is_imported,
                                  false,
                                  nob_sv_from_cstr(""))) {
        return eval_result_fatal();
    }
    if (!is_imported && ty != EV_TARGET_LIBRARY_INTERFACE) {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        for (size_t si = i; si < arena_arr_len(a); ++si) {
            if (a[si].count == 0) continue;
            String_View path = eval_path_resolve_for_cmake_arg(ctx, a[si], cur_src, true);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            if (!eval_emit_target_add_source(ctx, o, name, path)) return eval_result_from_ctx(ctx);
        }
    }
    return eval_result_from_ctx(ctx);
}
