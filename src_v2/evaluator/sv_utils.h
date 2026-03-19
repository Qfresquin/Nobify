#ifndef SV_UTILS_H_
#define SV_UTILS_H_

#include "evaluator_internal.h"
#include "arena_dyn.h"

#include <ctype.h>
#include <string.h>

static inline bool svu_is_path_sep(char c) {
    return c == '/' || c == '\\';
}

static inline String_View svu_trim_ascii_ws(String_View sv) {
    return nob_sv_trim(sv);
}

static inline String_View svu_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        return nob_sv_from_parts(sv.data, sv.count - 1);
    }
    return sv;
}

static inline bool svu_starts_with_lit(String_View sv, const char *lit) {
    return lit && nob_sv_starts_with(sv, nob_sv_from_cstr(lit));
}

static inline bool svu_strip_prefix_lit(String_View *sv, const char *lit) {
    return sv && lit && nob_sv_chop_prefix(sv, nob_sv_from_cstr(lit));
}

static inline bool svu_has_prefix_ci_lit(String_View sv, const char *lit) {
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

static inline bool svu_eq_ci_sv(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        char ca = (char)tolower((unsigned char)a.data[i]);
        char cb = (char)tolower((unsigned char)b.data[i]);
        if (ca != cb) return false;
    }
    return true;
}

static inline String_View svu_dirname(String_View path) {
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (svu_is_path_sep(c)) {
            if (i == 0) return nob_sv_from_parts(path.data, 1);
            return nob_sv_from_parts(path.data, i);
        }
    }
    return nob_sv_from_cstr(".");
}

static inline String_View svu_concat_suffix_temp(EvalExecContext *ctx, String_View base, const char *suffix) {
    if (!ctx || !suffix) return nob_sv_from_cstr("");
    size_t suffix_len = strlen(suffix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), base.count + suffix_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (base.count) memcpy(buf, base.data, base.count);
    if (suffix_len) memcpy(buf + base.count, suffix, suffix_len);
    buf[base.count + suffix_len] = '\0';
    return nob_sv_from_cstr(buf);
}

static inline bool svu_list_push_temp(EvalExecContext *ctx, SV_List *list, String_View sv) {
    return eval_sv_arr_push_temp(ctx, list, sv);
}

static inline String_View svu_join_space_temp(EvalExecContext *ctx, const String_View *items, size_t count) {
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

static inline String_View svu_join_no_sep_temp(EvalExecContext *ctx, const String_View *items, size_t count) {
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

#endif // SV_UTILS_H_
