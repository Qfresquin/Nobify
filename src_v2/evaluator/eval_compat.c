#include "eval_compat.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static Eval_Compat_Profile eval_profile_from_sv(String_View v) {
    if (eval_sv_eq_ci_lit(v, "STRICT")) return EVAL_PROFILE_STRICT;
    if (eval_sv_eq_ci_lit(v, "CI_STRICT")) return EVAL_PROFILE_CI_STRICT;
    return EVAL_PROFILE_PERMISSIVE;
}

static Eval_Unsupported_Policy eval_unsupported_policy_from_sv(String_View v) {
    if (eval_sv_eq_ci_lit(v, "ERROR")) return EVAL_UNSUPPORTED_ERROR;
    if (eval_sv_eq_ci_lit(v, "NOOP_WARN")) return EVAL_UNSUPPORTED_NOOP_WARN;
    return EVAL_UNSUPPORTED_WARN;
}

static bool eval_sv_ends_with_ci_lit(String_View v, const char *suffix) {
    String_View s = nob_sv_from_cstr(suffix);
    if (v.count < s.count) return false;
    return eval_sv_eq_ci_lit(nob_sv_from_parts(v.data + (v.count - s.count), s.count), suffix);
}

static bool eval_compat_truthy_sv(String_View v) {
    if (v.count == 0 || !v.data) return false;

    if (eval_sv_eq_ci_lit(v, "1") ||
        eval_sv_eq_ci_lit(v, "ON") ||
        eval_sv_eq_ci_lit(v, "YES") ||
        eval_sv_eq_ci_lit(v, "TRUE") ||
        eval_sv_eq_ci_lit(v, "Y")) {
        return true;
    }

    if (eval_sv_eq_ci_lit(v, "0") ||
        eval_sv_eq_ci_lit(v, "OFF") ||
        eval_sv_eq_ci_lit(v, "NO") ||
        eval_sv_eq_ci_lit(v, "FALSE") ||
        eval_sv_eq_ci_lit(v, "N") ||
        eval_sv_eq_ci_lit(v, "IGNORE") ||
        eval_sv_eq_ci_lit(v, "NOTFOUND") ||
        eval_sv_ends_with_ci_lit(v, "-NOTFOUND")) {
        return false;
    }

    char buf[64];
    if (v.count < sizeof(buf)) {
        memcpy(buf, v.data, v.count);
        buf[v.count] = '\0';
        char *end = NULL;
        double numeric = strtod(buf, &end);
        if (end && *end == '\0') return numeric != 0.0;
    }

    return true;
}

static bool eval_parse_size_t_sv(String_View v, size_t *out) {
    if (!out || v.count == 0) return false;
    char buf[64];
    if (v.count >= sizeof(buf)) return false;
    memcpy(buf, v.data, v.count);
    buf[v.count] = '\0';
    char *end = NULL;
    unsigned long long raw = strtoull(buf, &end, 10);
    if (!end || *end != '\0') return false;
    if (raw > (unsigned long long)SIZE_MAX) return false;
    *out = (size_t)raw;
    return true;
}

String_View eval_compat_profile_to_sv(Eval_Compat_Profile profile) {
    switch (profile) {
        case EVAL_PROFILE_STRICT: return nob_sv_from_cstr("STRICT");
        case EVAL_PROFILE_CI_STRICT: return nob_sv_from_cstr("CI_STRICT");
        default: return nob_sv_from_cstr("PERMISSIVE");
    }
}

bool eval_compat_set_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile) {
    if (!ctx) return false;
    if (profile != EVAL_PROFILE_STRICT &&
        profile != EVAL_PROFILE_CI_STRICT &&
        profile != EVAL_PROFILE_PERMISSIVE) {
        return false;
    }
    Eval_Runtime_State *runtime = eval_runtime_slice(ctx);
    runtime->compat_profile = profile;
    runtime->continue_on_error_snapshot = (profile == EVAL_PROFILE_PERMISSIVE);

    if (eval_scope_visible_depth(ctx) == 0) return true;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_COMPAT_PROFILE), eval_compat_profile_to_sv(profile))) {
        return false;
    }
    return eval_var_set_current(ctx,
                        nob_sv_from_cstr(EVAL_VAR_NOBIFY_CONTINUE_ON_ERROR),
                        profile == EVAL_PROFILE_PERMISSIVE ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
}

void eval_refresh_runtime_compat(Evaluator_Context *ctx) {
    if (!ctx) return;
    if (eval_scope_visible_depth(ctx) == 0) return;
    Eval_Runtime_State *runtime = eval_runtime_slice(ctx);

    // This refresh snapshots evaluator-local compatibility knobs for the current
    // command cycle. Callers are expected to invoke it once at command entry and
    // then keep the resulting snapshot stable for the rest of that cycle.
    String_View profile = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_COMPAT_PROFILE));
    if (profile.count > 0) runtime->compat_profile = eval_profile_from_sv(profile);

    String_View policy = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_UNSUPPORTED_POLICY));
    if (policy.count > 0) runtime->unsupported_policy = eval_unsupported_policy_from_sv(policy);

    String_View budget_sv = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_ERROR_BUDGET));
    size_t parsed = 0;
    if (eval_parse_size_t_sv(budget_sv, &parsed)) runtime->error_budget = parsed;

    String_View continue_on_error = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_CONTINUE_ON_ERROR));
    if (continue_on_error.count > 0) {
        runtime->continue_on_error_snapshot = eval_compat_truthy_sv(continue_on_error);
    }
}

Cmake_Diag_Severity eval_compat_effective_severity(const Evaluator_Context *ctx, Cmake_Diag_Severity sev) {
    if (!ctx) return sev;
    if (sev == EV_DIAG_WARNING &&
        (ctx->runtime_state.compat_profile == EVAL_PROFILE_STRICT ||
         ctx->runtime_state.compat_profile == EVAL_PROFILE_CI_STRICT)) {
        return EV_DIAG_ERROR;
    }
    return sev;
}

bool eval_compat_decide_on_diag(Evaluator_Context *ctx, Cmake_Diag_Severity effective_sev) {
    if (!ctx) return false;
    if (effective_sev != EV_DIAG_ERROR) return true;

    Eval_Runtime_State *runtime = eval_runtime_slice(ctx);
    if (runtime->compat_profile == EVAL_PROFILE_PERMISSIVE) {
        if (runtime->error_budget > 0 && runtime->run_report.error_count >= runtime->error_budget) {
            eval_request_stop(ctx);
        }
    } else {
        eval_request_stop_on_error(ctx);
    }
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}
