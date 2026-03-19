#ifndef EVAL_STRING_INTERNAL_H_
#define EVAL_STRING_INTERNAL_H_

#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline bool eval_string_parse_i64(String_View sv, long long *out) {
    if (!out || sv.count == 0) return false;
    char buf[64];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    long long v = strtoll(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out = v;
    return true;
}

static inline String_View eval_string_join_no_sep_temp(EvalExecContext *ctx, String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    return svu_join_no_sep_temp(ctx, items, count);
}

static inline bool eval_string_parse_u64_sv(String_View sv, unsigned long long *out) {
    if (!out || sv.count == 0) return false;
    char buf[128];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(buf, &end, 10);
    if (errno == ERANGE || !end || *end != '\0') return false;
    *out = v;
    return true;
}

static inline int eval_string_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline bool eval_string_is_hash_command(String_View cmd) {
    return eval_sv_eq_ci_lit(cmd, "MD5") ||
           eval_sv_eq_ci_lit(cmd, "SHA1") ||
           eval_sv_eq_ci_lit(cmd, "SHA224") ||
           eval_sv_eq_ci_lit(cmd, "SHA256") ||
           eval_sv_eq_ci_lit(cmd, "SHA384") ||
           eval_sv_eq_ci_lit(cmd, "SHA512") ||
           eval_sv_eq_ci_lit(cmd, "SHA3_224") ||
           eval_sv_eq_ci_lit(cmd, "SHA3_256") ||
           eval_sv_eq_ci_lit(cmd, "SHA3_384") ||
           eval_sv_eq_ci_lit(cmd, "SHA3_512");
}

Eval_Result eval_string_handle_text(EvalExecContext *ctx, const Node *node, Cmake_Event_Origin o, SV_List a);
Eval_Result eval_string_handle_regex(EvalExecContext *ctx, const Node *node, Cmake_Event_Origin o, SV_List a);
Eval_Result eval_string_handle_json(EvalExecContext *ctx, const Node *node, Cmake_Event_Origin o, SV_List a);
Eval_Result eval_string_handle_misc(EvalExecContext *ctx, const Node *node, Cmake_Event_Origin o, SV_List a);

#endif // EVAL_STRING_INTERNAL_H_
