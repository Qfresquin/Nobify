#include "eval_policy_engine.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool policy_parse_depth(String_View sv, size_t *out_depth) {
    if (!out_depth || sv.count == 0) return false;
    size_t acc = 0;
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c < '0' || c > '9') return false;
        size_t digit = (size_t)(c - '0');
        if (acc > (SIZE_MAX / 10)) return false;
        acc = (acc * 10) + digit;
    }
    if (acc == 0) return false;
    *out_depth = acc;
    return true;
}

static size_t policy_current_depth(Evaluator_Context *ctx) {
    if (!ctx) return 1;
    String_View depth_sv = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_POLICY_STACK_DEPTH"));
    size_t depth = 1;
    if (!policy_parse_depth(depth_sv, &depth)) depth = 1;
    return depth;
}

static bool policy_set_depth(Evaluator_Context *ctx, size_t depth) {
    if (!ctx || depth == 0) return false;
    char depth_buf[32];
    int n = snprintf(depth_buf, sizeof(depth_buf), "%zu", depth);
    if (n < 0 || (size_t)n >= sizeof(depth_buf)) return ctx_oom(ctx);
    return eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_POLICY_STACK_DEPTH"), nob_sv_from_cstr(depth_buf));
}

static String_View policy_canonical_id_temp(Evaluator_Context *ctx, String_View policy_id) {
    if (!ctx || !eval_policy_is_id(policy_id)) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), 8);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < 7; i++) {
        buf[i] = (char)toupper((unsigned char)policy_id.data[i]);
    }
    buf[7] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View policy_slot_key_temp(Evaluator_Context *ctx, size_t depth, String_View canonical_id) {
    if (!ctx || canonical_id.count == 0) return nob_sv_from_cstr("");
    static const char *prefix = "NOBIFY_POLICY_D";

    char depth_buf[32];
    int n = snprintf(depth_buf, sizeof(depth_buf), "%zu", depth);
    if (n < 0 || (size_t)n >= sizeof(depth_buf)) return nob_sv_from_cstr("");
    size_t depth_len = (size_t)n;

    size_t prefix_len = strlen(prefix);
    size_t total = prefix_len + depth_len + 1 + canonical_id.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, prefix_len);
    off += prefix_len;
    memcpy(buf + off, depth_buf, depth_len);
    off += depth_len;
    buf[off++] = '_';
    memcpy(buf + off, canonical_id.data, canonical_id.count);
    off += canonical_id.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View policy_default_key_temp(Evaluator_Context *ctx, String_View canonical_id) {
    if (!ctx || canonical_id.count == 0) return nob_sv_from_cstr("");
    static const char *prefix = "CMAKE_POLICY_DEFAULT_";
    size_t prefix_len = strlen(prefix);
    size_t total = prefix_len + canonical_id.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, prefix, prefix_len);
    memcpy(buf + prefix_len, canonical_id.data, canonical_id.count);
    buf[total] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View policy_legacy_key_temp(Evaluator_Context *ctx, String_View canonical_id) {
    if (!ctx || canonical_id.count == 0) return nob_sv_from_cstr("");
    static const char *prefix = "CMAKE_POLICY_";
    size_t prefix_len = strlen(prefix);
    size_t total = prefix_len + canonical_id.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, prefix, prefix_len);
    memcpy(buf + prefix_len, canonical_id.data, canonical_id.count);
    buf[total] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View policy_normalize_status(String_View v) {
    if (eval_sv_eq_ci_lit(v, "NEW")) return nob_sv_from_cstr("NEW");
    if (eval_sv_eq_ci_lit(v, "OLD")) return nob_sv_from_cstr("OLD");
    return nob_sv_from_cstr("");
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

bool eval_policy_push(Evaluator_Context *ctx) {
    if (!ctx) return false;
    size_t depth = policy_current_depth(ctx);
    return policy_set_depth(ctx, depth + 1);
}

bool eval_policy_pop(Evaluator_Context *ctx) {
    if (!ctx) return false;
    size_t depth = policy_current_depth(ctx);
    if (depth <= 1) return false;
    return policy_set_depth(ctx, depth - 1);
}

bool eval_policy_set(Evaluator_Context *ctx, String_View policy_id, String_View value) {
    if (!ctx || eval_should_stop(ctx)) return false;
    String_View canonical_id = policy_canonical_id_temp(ctx, policy_id);
    if (eval_should_stop(ctx) || canonical_id.count == 0) return false;

    String_View normalized = policy_normalize_status(value);
    if (normalized.count == 0) return false;

    size_t depth = policy_current_depth(ctx);
    if (depth == 0) depth = 1;

    String_View slot_key = policy_slot_key_temp(ctx, depth, canonical_id);
    if (eval_should_stop(ctx) || slot_key.count == 0) return false;
    if (!eval_var_set(ctx, slot_key, normalized)) return false;

    String_View legacy_key = policy_legacy_key_temp(ctx, canonical_id);
    if (eval_should_stop(ctx) || legacy_key.count == 0) return false;
    return eval_var_set(ctx, legacy_key, normalized);
}

String_View eval_policy_get_effective(Evaluator_Context *ctx, String_View policy_id) {
    if (!ctx || eval_should_stop(ctx)) return nob_sv_from_cstr("");
    String_View canonical_id = policy_canonical_id_temp(ctx, policy_id);
    if (eval_should_stop(ctx) || canonical_id.count == 0) return nob_sv_from_cstr("");

    size_t depth = policy_current_depth(ctx);
    for (size_t d = depth; d > 0; d--) {
        String_View slot_key = policy_slot_key_temp(ctx, d, canonical_id);
        if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        if (!eval_var_defined(ctx, slot_key)) continue;
        String_View v = policy_normalize_status(eval_var_get(ctx, slot_key));
        if (v.count > 0) return v;
    }

    String_View legacy_key = policy_legacy_key_temp(ctx, canonical_id);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (eval_var_defined(ctx, legacy_key)) {
        String_View v = policy_normalize_status(eval_var_get(ctx, legacy_key));
        if (v.count > 0) return v;
    }

    String_View default_key = policy_default_key_temp(ctx, canonical_id);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (eval_var_defined(ctx, default_key)) {
        String_View v = policy_normalize_status(eval_var_get(ctx, default_key));
        if (v.count > 0) return v;
    }

    String_View policy_version = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"));
    if (policy_version.count > 0) return nob_sv_from_cstr("NEW");

    return nob_sv_from_cstr("");
}
