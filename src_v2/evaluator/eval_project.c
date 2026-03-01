#include "eval_project.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>

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

static bool apply_subdir_system_default_to_target(Evaluator_Context *ctx,
                                                  Cmake_Event_Origin o,
                                                  String_View target_name) {
    String_View raw = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"));
    if (raw.count == 0) return true;
    if (eval_sv_eq_ci_lit(raw, "0") || eval_sv_eq_ci_lit(raw, "FALSE") || eval_sv_eq_ci_lit(raw, "OFF")) {
        return true;
    }
    return emit_target_prop_set(ctx,
                                o,
                                target_name,
                                nob_sv_from_cstr("SYSTEM"),
                                nob_sv_from_cstr("1"),
                                EV_PROP_SET);
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

static bool emit_bool_target_prop_true(Evaluator_Context *ctx,
                                       Cmake_Event_Origin o,
                                       String_View target_name,
                                       const char *key) {
    return emit_target_prop_set(ctx,
                                o,
                                target_name,
                                nob_sv_from_cstr(key),
                                nob_sv_from_cstr("1"),
                                EV_PROP_SET);
}

static bool add_target_name_must_be_new(Evaluator_Context *ctx,
                                        String_View cmd_name,
                                        Cmake_Event_Origin o,
                                        String_View target_name) {
    if (!eval_target_known(ctx, target_name)) return true;
    eval_emit_diag(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("dispatcher"),
                   cmd_name,
                   o,
                   nob_sv_from_cstr("Target name already exists"),
                   target_name);
    return false;
}

static bool add_alias_target_validate(Evaluator_Context *ctx,
                                      String_View cmd_name,
                                      Cmake_Event_Origin o,
                                      String_View alias_name,
                                      String_View real_target) {
    if (!eval_target_known(ctx, real_target)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       cmd_name,
                       o,
                       nob_sv_from_cstr("ALIAS target does not exist"),
                       real_target);
        return false;
    }
    if (eval_target_alias_known(ctx, real_target)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       cmd_name,
                       o,
                       nob_sv_from_cstr("ALIAS target cannot reference another ALIAS target"),
                       real_target);
        return false;
    }
    if (!eval_target_register(ctx, alias_name)) return false;
    if (!eval_target_alias_register(ctx, alias_name)) return false;
    return true;
}

static bool add_library_default_shared(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("BUILD_SHARED_LIBS"));
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

typedef struct {
    int major;
    int minor;
    int patch;
    int tweak;
} Eval_Semver;

static const Eval_Semver k_policy_floor_24 = {2, 4, 0, 0};
static const Eval_Semver k_running_cmake_328 = {3, 28, 0, 0};

static bool semver_parse_component(String_View sv, int *out_value) {
    if (!out_value || sv.count == 0) return false;
    long long acc = 0;
    for (size_t i = 0; i < sv.count; i++) {
        if (sv.data[i] < '0' || sv.data[i] > '9') return false;
        acc = (acc * 10) + (long long)(sv.data[i] - '0');
        if (acc > INT_MAX) return false;
    }
    *out_value = (int)acc;
    return true;
}

static bool semver_parse_strict(String_View version_token, Eval_Semver *out_version) {
    if (!out_version || version_token.count == 0) return false;
    int values[4] = {0, 0, 0, 0};
    size_t value_count = 0;
    size_t pos = 0;

    while (pos < version_token.count) {
        size_t start = pos;
        while (pos < version_token.count && version_token.data[pos] != '.') pos++;
        if (value_count >= 4) return false;
        String_View part = nob_sv_from_parts(version_token.data + start, pos - start);
        if (!semver_parse_component(part, &values[value_count])) return false;
        value_count++;
        if (pos == version_token.count) break;
        pos++;
        if (pos == version_token.count) return false;
    }

    if (value_count < 2 || value_count > 4) return false;
    out_version->major = values[0];
    out_version->minor = values[1];
    out_version->patch = values[2];
    out_version->tweak = values[3];
    return true;
}

static int semver_compare(const Eval_Semver *lhs, const Eval_Semver *rhs) {
    if (!lhs || !rhs) return 0;
    if (lhs->major != rhs->major) return (lhs->major < rhs->major) ? -1 : 1;
    if (lhs->minor != rhs->minor) return (lhs->minor < rhs->minor) ? -1 : 1;
    if (lhs->patch != rhs->patch) return (lhs->patch < rhs->patch) ? -1 : 1;
    if (lhs->tweak != rhs->tweak) return (lhs->tweak < rhs->tweak) ? -1 : 1;
    return 0;
}

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
    if (!semver_parse_strict(*out_min_token, out_min_version)) return false;
    if (!semver_parse_strict(*out_max_token, out_max_version)) return false;
    return true;
}

static bool policy_apply_version_defaults(Evaluator_Context *ctx, Eval_Semver policy_version) {
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
            semver_compare(&intro, &policy_version) <= 0 ? POLICY_STATUS_NEW : POLICY_STATUS_UNSET;
        if (!eval_policy_set_status(ctx, policy_id, status)) return false;
    }
    return true;
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
    if (a.count > 3 || (a.count == 3 && !eval_sv_eq_ci_lit(a.items[2], "FATAL_ERROR"))) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_minimum_required() received invalid arguments"),
                       nob_sv_from_cstr("Usage: cmake_minimum_required(VERSION <min>[...<max>] [FATAL_ERROR])"));
        return !eval_should_stop(ctx);
    }

    String_View min_token = nob_sv_from_cstr("");
    String_View max_token = nob_sv_from_cstr("");
    Eval_Semver min_version = {0};
    Eval_Semver max_version = {0};
    bool has_max = false;
    if (!policy_parse_version_range_strict(a.items[1],
                                           &min_token,
                                           &min_version,
                                           &has_max,
                                           &max_token,
                                           &max_version)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_minimum_required() received invalid VERSION token"),
                       a.items[1]);
        return !eval_should_stop(ctx);
    }
    if (semver_compare(&min_version, &k_running_cmake_328) > 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_minimum_required() requires a newer CMake than evaluator baseline"),
                       min_token);
        return !eval_should_stop(ctx);
    }
    if (has_max && semver_compare(&max_version, &min_version) < 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_minimum_required() requires max version >= min version"),
                       a.items[1]);
        return !eval_should_stop(ctx);
    }

    Eval_Semver policy_version = has_max ? max_version : min_version;
    String_View policy_version_token = has_max ? max_token : min_token;
    if (semver_compare(&policy_version, &k_policy_floor_24) < 0) {
        policy_version = k_policy_floor_24;
        policy_version_token = nob_sv_from_cstr("2.4");
    }

    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_MINIMUM_REQUIRED_VERSION"), min_token)) {
        return !eval_should_stop(ctx);
    }
    if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), policy_version_token)) {
        return !eval_should_stop(ctx);
    }
    if (!policy_apply_version_defaults(ctx, policy_version)) return !eval_should_stop(ctx);
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
        if (a.count != 2) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(VERSION ...) expects exactly one version argument"),
                           nob_sv_from_cstr("Usage: cmake_policy(VERSION <min>[...<max>])"));
            return !eval_should_stop(ctx);
        }

        String_View min_token = nob_sv_from_cstr("");
        String_View max_token = nob_sv_from_cstr("");
        Eval_Semver min_version = {0};
        Eval_Semver max_version = {0};
        bool has_max = false;
        if (!policy_parse_version_range_strict(a.items[1],
                                               &min_token,
                                               &min_version,
                                               &has_max,
                                               &max_token,
                                               &max_version)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(VERSION ...) received invalid version token"),
                           a.items[1]);
            return !eval_should_stop(ctx);
        }
        if (semver_compare(&min_version, &k_policy_floor_24) < 0) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(VERSION ...) requires minimum version >= 2.4"),
                           min_token);
            return !eval_should_stop(ctx);
        }
        if (semver_compare(&min_version, &k_running_cmake_328) > 0) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(VERSION ...) min version exceeds evaluator baseline"),
                           min_token);
            return !eval_should_stop(ctx);
        }
        if (has_max && semver_compare(&max_version, &min_version) < 0) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(VERSION ...) requires max version >= min version"),
                           a.items[1]);
            return !eval_should_stop(ctx);
        }

        Eval_Semver policy_version = has_max ? max_version : min_version;
        String_View policy_token = has_max ? max_token : min_token;
        if (!eval_var_set(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), policy_token)) return !eval_should_stop(ctx);
        if (!policy_apply_version_defaults(ctx, policy_version)) return !eval_should_stop(ctx);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "SET")) {
        if (a.count != 3) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(SET ...) expects exactly policy id and value"),
                           nob_sv_from_cstr("Usage: cmake_policy(SET CMP0077 NEW)"));
            return !eval_should_stop(ctx);
        }
        if (!eval_policy_is_known(a.items[1])) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(SET ...) requires a known CMP policy id"),
                           a.items[1]);
            return !eval_should_stop(ctx);
        }
        if (!(eval_sv_eq_ci_lit(a.items[2], "OLD") || eval_sv_eq_ci_lit(a.items[2], "NEW"))) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(SET ...) requires OLD or NEW"),
                           a.items[2]);
            return !eval_should_stop(ctx);
        }
        if (!eval_policy_set(ctx, a.items[1], a.items[2])) return !eval_should_stop(ctx);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "GET")) {
        if (a.count != 3) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(GET ...) expects exactly policy id and output variable"),
                           nob_sv_from_cstr("Usage: cmake_policy(GET CMP0077 out_var)"));
            return !eval_should_stop(ctx);
        }
        if (!eval_policy_is_known(a.items[1])) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(GET ...) requires a known CMP policy id"),
                           a.items[1]);
            return !eval_should_stop(ctx);
        }
        String_View val = eval_policy_get_effective(ctx, a.items[1]);
        if (!eval_var_set(ctx, a.items[2], val)) return !eval_should_stop(ctx);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "PUSH")) {
        if (a.count != 1) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(PUSH) does not accept extra arguments"),
                           nob_sv_from_cstr("Usage: cmake_policy(PUSH)"));
            return !eval_should_stop(ctx);
        }
        if (!eval_policy_push(ctx)) return !eval_should_stop(ctx);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "POP")) {
        if (a.count != 1) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(POP) does not accept extra arguments"),
                           nob_sv_from_cstr("Usage: cmake_policy(POP)"));
            return !eval_should_stop(ctx);
        }
        if (!eval_policy_pop(ctx)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(POP) called without matching PUSH"),
                           nob_sv_from_cstr("Add cmake_policy(PUSH) before POP"));
            eval_request_stop_on_error(ctx);
            return !eval_should_stop(ctx);
        }
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("Unknown cmake_policy() subcommand"),
                   a.items[0]);
    eval_request_stop_on_error(ctx);
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

    String_View key_src = svu_concat_suffix_temp(ctx, name, "_SOURCE_DIR");
    String_View key_bin = svu_concat_suffix_temp(ctx, name, "_BINARY_DIR");
    String_View key_ver = svu_concat_suffix_temp(ctx, name, "_VERSION");
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
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_executable() missing target name"),
                       nob_sv_from_cstr("Usage: add_executable(<name> [WIN32] [MACOSX_BUNDLE] [EXCLUDE_FROM_ALL] <sources...>)"));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    if (!add_target_name_must_be_new(ctx, node->as.cmd.name, o, name)) return !eval_should_stop(ctx);

    if (a.count >= 2 && eval_sv_eq_ci_lit(a.items[1], "ALIAS")) {
        if (a.count != 3) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_executable(ALIAS ...) expects exactly alias name and real target"),
                           nob_sv_from_cstr("Usage: add_executable(<name> ALIAS <target>)"));
            return !eval_should_stop(ctx);
        }
        (void)add_alias_target_validate(ctx, node->as.cmd.name, o, name, a.items[2]);
        return !eval_should_stop(ctx);
    }

    bool is_imported = false;
    bool is_global = false;
    bool is_win32 = false;
    bool is_macos_bundle = false;
    bool is_exclude_from_all = false;
    size_t source_start = 1;

    if (a.count >= 2 && eval_sv_eq_ci_lit(a.items[1], "IMPORTED")) {
        is_imported = true;
        source_start = 2;
        if (source_start < a.count && eval_sv_eq_ci_lit(a.items[source_start], "GLOBAL")) {
            is_global = true;
            source_start++;
        }
        if (source_start < a.count) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_executable(IMPORTED ...) does not accept source files"),
                           nob_sv_from_cstr("Usage: add_executable(<name> IMPORTED [GLOBAL])"));
            return !eval_should_stop(ctx);
        }
    }

    if (!is_imported) {
        for (size_t i = 1; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "WIN32")) {
                is_win32 = true;
                source_start = i + 1;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "MACOSX_BUNDLE")) {
                is_macos_bundle = true;
                source_start = i + 1;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "EXCLUDE_FROM_ALL")) {
                is_exclude_from_all = true;
                source_start = i + 1;
                continue;
            }
            source_start = i;
            break;
        }
    }

    (void)eval_target_register(ctx, name);

    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_DECLARE;
    ev.origin = o;
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.type = EV_TARGET_EXECUTABLE;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    if (is_imported) {
        if (!emit_bool_target_prop_true(ctx, o, name, "IMPORTED")) return !eval_should_stop(ctx);
        if (is_global) {
            if (!emit_bool_target_prop_true(ctx, o, name, "IMPORTED_GLOBAL")) return !eval_should_stop(ctx);
        }
    } else {
        if (!apply_subdir_system_default_to_target(ctx, o, name)) return !eval_should_stop(ctx);
        if (is_win32) {
            if (!emit_bool_target_prop_true(ctx, o, name, "WIN32_EXECUTABLE")) return !eval_should_stop(ctx);
        }
        if (is_macos_bundle) {
            if (!emit_bool_target_prop_true(ctx, o, name, "MACOSX_BUNDLE")) return !eval_should_stop(ctx);
        }
        if (is_exclude_from_all) {
            if (!emit_bool_target_prop_true(ctx, o, name, "EXCLUDE_FROM_ALL")) return !eval_should_stop(ctx);
        }
    }

    for (size_t i = source_start; !is_imported && i < a.count; i++) {
        Cmake_Event src_ev = {0};
        src_ev.kind = EV_TARGET_ADD_SOURCE;
        src_ev.origin = o;
        src_ev.as.target_add_source.target_name = ev.as.target_declare.name;
        src_ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, src_ev)) return !eval_should_stop(ctx);
    }

    if (!is_imported) {
        (void)apply_global_compile_state_to_target(ctx, o, name);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_add_library(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_library() missing target name"),
                       nob_sv_from_cstr("Usage: add_library(<name> [STATIC|SHARED|MODULE|OBJECT|INTERFACE|UNKNOWN] [EXCLUDE_FROM_ALL] <sources...>)"));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    if (!add_target_name_must_be_new(ctx, node->as.cmd.name, o, name)) return !eval_should_stop(ctx);

    if (a.count >= 2 && eval_sv_eq_ci_lit(a.items[1], "ALIAS")) {
        if (a.count != 3) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_library(ALIAS ...) expects exactly alias name and real target"),
                           nob_sv_from_cstr("Usage: add_library(<name> ALIAS <target>)"));
            return !eval_should_stop(ctx);
        }
        (void)add_alias_target_validate(ctx, node->as.cmd.name, o, name, a.items[2]);
        return !eval_should_stop(ctx);
    }

    (void)eval_target_register(ctx, name);

    Cmake_Target_Type ty = EV_TARGET_LIBRARY_UNKNOWN;
    size_t i = 1;
    bool has_explicit_type = false;
    bool is_imported = false;
    bool is_global = false;
    bool is_exclude_from_all = false;
    if (i < a.count) {
        if (eval_sv_eq_ci_lit(a.items[i], "STATIC")) {
            ty = EV_TARGET_LIBRARY_STATIC;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "SHARED")) {
            ty = EV_TARGET_LIBRARY_SHARED;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "MODULE")) {
            ty = EV_TARGET_LIBRARY_MODULE;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            ty = EV_TARGET_LIBRARY_INTERFACE;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "OBJECT")) {
            ty = EV_TARGET_LIBRARY_OBJECT;
            has_explicit_type = true;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "UNKNOWN")) {
            ty = EV_TARGET_LIBRARY_UNKNOWN;
            has_explicit_type = true;
            i++;
        }
    }

    if (i < a.count && eval_sv_eq_ci_lit(a.items[i], "IMPORTED")) {
        is_imported = true;
        i++;
        if (!has_explicit_type) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_library(IMPORTED ...) requires an explicit library type"),
                           nob_sv_from_cstr("Usage: add_library(<name> <STATIC|SHARED|MODULE|OBJECT|INTERFACE|UNKNOWN> IMPORTED [GLOBAL])"));
            return !eval_should_stop(ctx);
        }
        if (i < a.count && eval_sv_eq_ci_lit(a.items[i], "GLOBAL")) {
            is_global = true;
            i++;
        }
        if (i < a.count) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_library(IMPORTED ...) does not accept source files"),
                           nob_sv_from_cstr("Usage: add_library(<name> <type> IMPORTED [GLOBAL])"));
            return !eval_should_stop(ctx);
        }
    }

    if (!is_imported) {
        if (!has_explicit_type) {
            ty = add_library_default_shared(ctx) ? EV_TARGET_LIBRARY_SHARED : EV_TARGET_LIBRARY_STATIC;
        }
        if (i < a.count && eval_sv_eq_ci_lit(a.items[i], "EXCLUDE_FROM_ALL")) {
            is_exclude_from_all = true;
            i++;
        }
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_DECLARE;
    ev.origin = o;
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.type = ty;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    if (is_imported) {
        if (!emit_bool_target_prop_true(ctx, o, name, "IMPORTED")) return !eval_should_stop(ctx);
        if (is_global) {
            if (!emit_bool_target_prop_true(ctx, o, name, "IMPORTED_GLOBAL")) return !eval_should_stop(ctx);
        }
    } else {
        if (!apply_subdir_system_default_to_target(ctx, o, name)) return !eval_should_stop(ctx);
        if (is_exclude_from_all) {
            if (!emit_bool_target_prop_true(ctx, o, name, "EXCLUDE_FROM_ALL")) return !eval_should_stop(ctx);
        }
    }

    for (; !is_imported && i < a.count; i++) {
        Cmake_Event src_ev = {0};
        src_ev.kind = EV_TARGET_ADD_SOURCE;
        src_ev.origin = o;
        src_ev.as.target_add_source.target_name = ev.as.target_declare.name;
        src_ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, src_ev)) return !eval_should_stop(ctx);
    }

    if (!is_imported) {
        (void)apply_global_compile_state_to_target(ctx, o, name);
    }
    return !eval_should_stop(ctx);
}

