#include "eval_policy_engine.h"
#include "evaluator_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int major;
    int minor;
    int patch;
    int tweak;
} Policy_Semver;

typedef struct {
    int first_id;
    int last_id;
    int major;
    int minor;
    int patch;
} Policy_Intro_Range;

typedef enum {
    POLICY_SLOT_INHERIT = 0,
    POLICY_SLOT_OLD = 1,
    POLICY_SLOT_NEW = 2,
    POLICY_SLOT_UNSET_SHADOW = 3,
} Policy_Slot_State;

enum {
    POLICY_KNOWN_MIN_ID = 0,
    POLICY_KNOWN_MAX_ID = 155,
    POLICY_KNOWN_COUNT = 156,
};

static const Policy_Intro_Range POLICY_INTRO_RANGES[] = {
    {0,   11, 2,  6, 0},
    {12,  23, 2,  8, 0},
    {24,  50, 3,  0, 0},
    {51,  54, 3,  1, 0},
    {55,  56, 3,  2, 0},
    {57,  63, 3,  3, 0},
    {64,  65, 3,  4, 0},
    {66,  66, 3,  7, 0},
    {67,  67, 3,  8, 0},
    {68,  69, 3,  9, 0},
    {70,  71, 3, 10, 0},
    {72,  72, 3, 11, 0},
    {73,  75, 3, 12, 0},
    {76,  81, 3, 13, 0},
    {82,  88, 3, 14, 0},
    {89,  94, 3, 15, 0},
    {95,  97, 3, 16, 0},
    {98, 102, 3, 17, 0},
    {103,108, 3, 18, 0},
    {109,114, 3, 19, 0},
    {115,120, 3, 20, 0},
    {121,126, 3, 21, 0},
    {127,128, 3, 22, 0},
    {129,129, 3, 23, 0},
    {130,139, 3, 24, 0},
    {140,142, 3, 25, 0},
    {143,143, 3, 26, 0},
    {144,151, 3, 27, 0},
    {152,155, 3, 28, 0},
};
static const size_t POLICY_INTRO_RANGES_COUNT = sizeof(POLICY_INTRO_RANGES) / sizeof(POLICY_INTRO_RANGES[0]);

String_View eval_policy_status_to_sv(Eval_Policy_Status status) {
    switch (status) {
        case POLICY_STATUS_OLD: return nob_sv_from_cstr("OLD");
        case POLICY_STATUS_NEW: return nob_sv_from_cstr("NEW");
        default: return nob_sv_from_cstr("");
    }
}

static Eval_Policy_Status policy_status_from_sv(String_View v) {
    if (eval_sv_eq_ci_lit(v, "NEW")) return POLICY_STATUS_NEW;
    if (eval_sv_eq_ci_lit(v, "OLD")) return POLICY_STATUS_OLD;
    return POLICY_STATUS_UNSET;
}

static Policy_Slot_State policy_slot_from_status(Eval_Policy_Status status) {
    switch (status) {
        case POLICY_STATUS_OLD: return POLICY_SLOT_OLD;
        case POLICY_STATUS_NEW: return POLICY_SLOT_NEW;
        case POLICY_STATUS_UNSET: return POLICY_SLOT_UNSET_SHADOW;
        default: return POLICY_SLOT_INHERIT;
    }
}

bool eval_policy_is_id(String_View policy_id) {
    if (policy_id.count != 7) return false;
    if (!(policy_id.data[0] == 'C' || policy_id.data[0] == 'c')) return false;
    if (!(policy_id.data[1] == 'M' || policy_id.data[1] == 'm')) return false;
    if (!(policy_id.data[2] == 'P' || policy_id.data[2] == 'p')) return false;
    for (size_t i = 3; i < 7; i++) {
        if (!isdigit((unsigned char)policy_id.data[i])) return false;
    }
    return true;
}

static bool policy_parse_id_number(String_View policy_id, int *out_number) {
    if (!out_number || !eval_policy_is_id(policy_id)) return false;
    int acc = 0;
    for (size_t i = 3; i < 7; i++) {
        acc = (acc * 10) + (policy_id.data[i] - '0');
    }
    *out_number = acc;
    return true;
}

static bool policy_get_intro_for_number(int policy_number, int *major, int *minor, int *patch) {
    for (size_t i = 0; i < POLICY_INTRO_RANGES_COUNT; i++) {
        const Policy_Intro_Range *r = &POLICY_INTRO_RANGES[i];
        if (policy_number < r->first_id || policy_number > r->last_id) continue;
        if (major) *major = r->major;
        if (minor) *minor = r->minor;
        if (patch) *patch = r->patch;
        return true;
    }
    return false;
}

bool eval_policy_is_known(String_View policy_id) {
    int number = -1;
    if (!policy_parse_id_number(policy_id, &number)) return false;
    if (number < POLICY_KNOWN_MIN_ID || number > POLICY_KNOWN_MAX_ID) return false;
    return policy_get_intro_for_number(number, NULL, NULL, NULL);
}

bool eval_policy_get_intro_version(String_View policy_id, int *major, int *minor, int *patch) {
    int number = -1;
    if (!policy_parse_id_number(policy_id, &number)) return false;
    return policy_get_intro_for_number(number, major, minor, patch);
}

int eval_policy_known_min_id(void) { return POLICY_KNOWN_MIN_ID; }
int eval_policy_known_max_id(void) { return POLICY_KNOWN_MAX_ID; }
size_t eval_policy_known_count(void) { return POLICY_KNOWN_COUNT; }

static bool policy_parse_int_component(String_View token, int *out_value) {
    if (!out_value || token.count == 0) return false;
    long long acc = 0;
    for (size_t i = 0; i < token.count; i++) {
        char c = token.data[i];
        if (c < '0' || c > '9') return false;
        acc = (acc * 10) + (long long)(c - '0');
        if (acc > INT_MAX) return false;
    }
    *out_value = (int)acc;
    return true;
}

static bool policy_parse_semver(String_View sv, Policy_Semver *out_version) {
    if (!out_version || sv.count == 0) return false;

    int values[4] = {0, 0, 0, 0};
    size_t value_count = 0;
    size_t pos = 0;
    while (pos < sv.count) {
        size_t start = pos;
        while (pos < sv.count && sv.data[pos] != '.') pos++;
        if (value_count >= 4) return false;
        String_View comp = nob_sv_from_parts(sv.data + start, pos - start);
        if (!policy_parse_int_component(comp, &values[value_count])) return false;
        value_count++;
        if (pos == sv.count) break;
        pos++;
        if (pos == sv.count) return false;
    }

    if (value_count < 2 || value_count > 4) return false;
    out_version->major = values[0];
    out_version->minor = values[1];
    out_version->patch = values[2];
    out_version->tweak = values[3];
    return true;
}

static int policy_compare_semver(const Policy_Semver *lhs, const Policy_Semver *rhs) {
    if (!lhs || !rhs) return 0;
    if (lhs->major != rhs->major) return (lhs->major < rhs->major) ? -1 : 1;
    if (lhs->minor != rhs->minor) return (lhs->minor < rhs->minor) ? -1 : 1;
    if (lhs->patch != rhs->patch) return (lhs->patch < rhs->patch) ? -1 : 1;
    if (lhs->tweak != rhs->tweak) return (lhs->tweak < rhs->tweak) ? -1 : 1;
    return 0;
}

static bool policy_set_depth_var(Evaluator_Context *ctx) {
    if (!ctx || ctx->policy_depth == 0) return false;
    char depth_buf[32];
    int n = snprintf(depth_buf, sizeof(depth_buf), "%zu", ctx->policy_depth);
    if (n < 0 || (size_t)n >= sizeof(depth_buf)) return ctx_oom(ctx);
    return eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_POLICY_STACK_DEPTH"), nob_sv_from_cstr(depth_buf));
}

static bool policy_ensure_capacity(Evaluator_Context *ctx, size_t min_capacity) {
    if (!ctx) return false;
    if (ctx->policy_capacity >= min_capacity && ctx->policy_levels) return true;

    size_t old_capacity = ctx->policy_capacity;
    size_t new_capacity = old_capacity > 0 ? old_capacity : 4;
    while (new_capacity < min_capacity) {
        if (new_capacity > (SIZE_MAX / 2)) return ctx_oom(ctx);
        new_capacity *= 2;
    }

    Eval_Policy_Level *grown =
        (Eval_Policy_Level*)realloc(ctx->policy_levels, new_capacity * sizeof(Eval_Policy_Level));
    EVAL_OOM_RETURN_IF_NULL(ctx, grown, false);

    ctx->policy_levels = grown;
    if (new_capacity > old_capacity) {
        memset(ctx->policy_levels + old_capacity, 0, (new_capacity - old_capacity) * sizeof(Eval_Policy_Level));
    }
    ctx->policy_capacity = new_capacity;
    return true;
}

static bool policy_stack_bootstrap(Evaluator_Context *ctx) {
    if (!ctx || eval_should_stop(ctx)) return false;
    if (ctx->policy_depth == 0) {
        ctx->policy_depth = 1;
        if (!policy_ensure_capacity(ctx, 1)) return false;
        if (!policy_set_depth_var(ctx)) return false;
        return true;
    }
    if (!policy_ensure_capacity(ctx, ctx->policy_depth)) return false;
    return true;
}

bool eval_policy_push(Evaluator_Context *ctx) {
    if (!ctx || eval_should_stop(ctx)) return false;
    if (!policy_stack_bootstrap(ctx)) return false;
    if (!policy_ensure_capacity(ctx, ctx->policy_depth + 1)) return false;
    memset(&ctx->policy_levels[ctx->policy_depth], 0, sizeof(Eval_Policy_Level));
    ctx->policy_depth += 1;
    return policy_set_depth_var(ctx);
}

bool eval_policy_pop(Evaluator_Context *ctx) {
    if (!ctx || eval_should_stop(ctx)) return false;
    if (!policy_stack_bootstrap(ctx)) return false;
    if (ctx->policy_depth <= 1) return false;
    memset(&ctx->policy_levels[ctx->policy_depth - 1], 0, sizeof(Eval_Policy_Level));
    ctx->policy_depth -= 1;
    return policy_set_depth_var(ctx);
}

bool eval_policy_set_status(Evaluator_Context *ctx, String_View policy_id, Eval_Policy_Status status) {
    if (!ctx || eval_should_stop(ctx)) return false;
    if (!eval_policy_is_known(policy_id)) return false;
    if (!(status == POLICY_STATUS_OLD || status == POLICY_STATUS_NEW || status == POLICY_STATUS_UNSET)) return false;
    if (!policy_stack_bootstrap(ctx)) return false;

    int policy_number = -1;
    if (!policy_parse_id_number(policy_id, &policy_number)) return false;
    if (policy_number < POLICY_KNOWN_MIN_ID || policy_number > POLICY_KNOWN_MAX_ID) return false;

    Policy_Slot_State slot = policy_slot_from_status(status);
    ctx->policy_levels[ctx->policy_depth - 1].states[policy_number] = (unsigned char)slot;

    char id_buf[8];
    if (snprintf(id_buf, sizeof(id_buf), "CMP%04d", policy_number) != 7) return ctx_oom(ctx);
    char legacy_key[32];
    if (snprintf(legacy_key, sizeof(legacy_key), "CMAKE_POLICY_%s", id_buf) < 0) return ctx_oom(ctx);
    return eval_var_set(ctx, nob_sv_from_cstr(legacy_key), eval_policy_status_to_sv(status));
}

bool eval_policy_set(Evaluator_Context *ctx, String_View policy_id, String_View value) {
    Eval_Policy_Status normalized = policy_status_from_sv(value);
    if (normalized == POLICY_STATUS_UNSET) return false;
    return eval_policy_set_status(ctx, policy_id, normalized);
}

String_View eval_policy_get_effective(Evaluator_Context *ctx, String_View policy_id) {
    if (!ctx || eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (!eval_policy_is_known(policy_id)) return nob_sv_from_cstr("");
    if (!policy_stack_bootstrap(ctx)) return nob_sv_from_cstr("");

    int policy_number = -1;
    if (!policy_parse_id_number(policy_id, &policy_number)) return nob_sv_from_cstr("");
    if (policy_number < POLICY_KNOWN_MIN_ID || policy_number > POLICY_KNOWN_MAX_ID) return nob_sv_from_cstr("");

    for (size_t depth = ctx->policy_depth; depth > 0; depth--) {
        unsigned char raw = ctx->policy_levels[depth - 1].states[policy_number];
        Policy_Slot_State slot = (Policy_Slot_State)raw;
        if (slot == POLICY_SLOT_INHERIT) continue;
        if (slot == POLICY_SLOT_OLD) return nob_sv_from_cstr("OLD");
        if (slot == POLICY_SLOT_NEW) return nob_sv_from_cstr("NEW");
        return nob_sv_from_cstr("");
    }

    char default_key[40];
    if (snprintf(default_key, sizeof(default_key), "CMAKE_POLICY_DEFAULT_CMP%04d", policy_number) < 0) {
        (void)ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    if (eval_var_defined(ctx, nob_sv_from_cstr(default_key))) {
        Eval_Policy_Status s = policy_status_from_sv(eval_var_get(ctx, nob_sv_from_cstr(default_key)));
        if (s == POLICY_STATUS_OLD) return nob_sv_from_cstr("OLD");
        if (s == POLICY_STATUS_NEW) return nob_sv_from_cstr("NEW");
        return nob_sv_from_cstr("");
    }

    String_View policy_version = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"));
    Policy_Semver current = {0};
    if (!policy_parse_semver(policy_version, &current)) return nob_sv_from_cstr("");

    int intro_major = 0;
    int intro_minor = 0;
    int intro_patch = 0;
    if (!policy_get_intro_for_number(policy_number, &intro_major, &intro_minor, &intro_patch)) {
        return nob_sv_from_cstr("");
    }
    Policy_Semver intro = {intro_major, intro_minor, intro_patch, 0};
    if (policy_compare_semver(&current, &intro) >= 0) return nob_sv_from_cstr("NEW");
    return nob_sv_from_cstr("");
}
