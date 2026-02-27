#include "eval_expr.h"
#include "evaluator_internal.h"
#include "eval_dispatcher.h"
#include "sv_utils.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "pcre/pcre2posix.h"
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#define EVAL_EXPAND_MAX_RECURSION 100
#define EVAL_EXPAND_MAX_RECURSION_HARD_CAP 10000

static bool path_exists_cstr(const char *path) {
    if (!path || path[0] == '\0') return false;
    struct stat st;
    return stat(path, &st) == 0;
}

static bool path_is_dir_cstr(const char *path) {
    if (!path || path[0] == '\0') return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
#if defined(S_IFDIR)
    return (st.st_mode & S_IFMT) == S_IFDIR;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static bool path_is_symlink_cstr(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#elif defined(S_ISLNK)
    struct stat st;
    if (lstat(path, &st) != 0) return false;
    return S_ISLNK(st.st_mode);
#else
    return false;
#endif
}

static bool sv_ends_with_ci_lit(String_View sv, const char *suffix) {
    String_View s = nob_sv_from_cstr(suffix);
    if (s.count > sv.count) return false;
    String_View tail = nob_sv_from_parts(sv.data + (sv.count - s.count), s.count);
    for (size_t i = 0; i < s.count; i++) {
        if (tolower((unsigned char)tail.data[i]) != tolower((unsigned char)s.data[i])) return false;
    }
    return true;
}

static bool sv_is_number(String_View sv, long *out) {
    if (sv.count == 0) return false;
    char buf[128];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = 0;

    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (!end || *end != 0) return false;
    if (out) *out = v;
    return true;
}

static int sv_lex_cmp(String_View a, String_View b) {
    size_t n = a.count < b.count ? a.count : b.count;
    int c = (n > 0) ? memcmp(a.data, b.data, n) : 0;
    if (c != 0) return c;
    if (a.count < b.count) return -1;
    if (a.count > b.count) return 1;
    return 0;
}

static bool sv_is_drive_root(String_View sv) {
    return sv.count == 3 &&
           isalpha((unsigned char)sv.data[0]) &&
           sv.data[1] == ':' &&
           sv.data[2] == '/';
}

static String_View sv_path_normalize_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx || in.count == 0) return in;
    char *seg_buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, seg_buf, nob_sv_from_cstr(""));
    size_t *seg_pos = (size_t*)arena_alloc(eval_temp_arena(ctx), sizeof(size_t) * (in.count + 1));
    EVAL_OOM_RETURN_IF_NULL(ctx, seg_pos, nob_sv_from_cstr(""));
    size_t *seg_len = (size_t*)arena_alloc(eval_temp_arena(ctx), sizeof(size_t) * (in.count + 1));
    EVAL_OOM_RETURN_IF_NULL(ctx, seg_len, nob_sv_from_cstr(""));
    char *out_buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 2);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_buf, nob_sv_from_cstr(""));

    // root_kind: 0=relative, 1="/", 2="X:/", 3="X:" (drive-relative)
    int root_kind = 0;
    char drive_letter = '\0';
    size_t i = 0;
    if (svu_is_path_sep(in.data[0])) {
        root_kind = 1;
        while (i < in.count && svu_is_path_sep(in.data[i])) i++;
    } else if (in.count >= 2 && isalpha((unsigned char)in.data[0]) && in.data[1] == ':') {
        drive_letter = in.data[0];
        i = 2;
        if (i < in.count && svu_is_path_sep(in.data[i])) {
            root_kind = 2;
            while (i < in.count && svu_is_path_sep(in.data[i])) i++;
        } else {
            root_kind = 3;
        }
    }

    size_t seg_count = 0;
    size_t seg_off = 0;
    while (i < in.count) {
        size_t start = i;
        while (i < in.count && !svu_is_path_sep(in.data[i])) i++;
        size_t len = i - start;
        while (i < in.count && svu_is_path_sep(in.data[i])) i++;
        if (len == 0) continue;

        bool is_dot = (len == 1 && in.data[start] == '.');
        bool is_dotdot = (len == 2 && in.data[start] == '.' && in.data[start + 1] == '.');
        if (is_dot) continue;

        if (is_dotdot) {
            if (seg_count > 0) {
                size_t prev = seg_count - 1;
                bool prev_is_dotdot = (seg_len[prev] == 2 &&
                                       seg_buf[seg_pos[prev]] == '.' &&
                                       seg_buf[seg_pos[prev] + 1] == '.');
                if (!prev_is_dotdot) {
                    seg_count--;
                    continue;
                }
            }
            if (root_kind == 1 || root_kind == 2) {
                // Absolute paths do not walk above root.
                continue;
            }
        }

        seg_pos[seg_count] = seg_off;
        seg_len[seg_count] = len;
        for (size_t k = 0; k < len; k++) seg_buf[seg_off++] = in.data[start + k];
        seg_count++;
    }

    size_t out_off = 0;
    if (root_kind == 1) {
        out_buf[out_off++] = '/';
    } else if (root_kind == 2) {
        out_buf[out_off++] = drive_letter;
        out_buf[out_off++] = ':';
        out_buf[out_off++] = '/';
    } else if (root_kind == 3) {
        out_buf[out_off++] = drive_letter;
        out_buf[out_off++] = ':';
    }

    for (size_t s = 0; s < seg_count; s++) {
        bool need_sep = false;
        if (s > 0) need_sep = true;
        if ((root_kind == 1 || root_kind == 2) && s == 0) need_sep = false;
        if (root_kind == 3 && s == 0) need_sep = false;
        if (need_sep) out_buf[out_off++] = '/';
        memcpy(out_buf + out_off, seg_buf + seg_pos[s], seg_len[s]);
        out_off += seg_len[s];
    }

    out_buf[out_off] = '\0';
    if (out_off == 0) return nob_sv_from_cstr("");
    String_View out = nob_sv_from_parts(out_buf, out_off);
    if (out.count > 1 && out.data[out.count - 1] == '/' && !sv_is_drive_root(out)) out.count--;
    return out;
}

static String_View sv_lookup_if_var(struct Evaluator_Context *ctx, String_View tok) {
    if (!ctx || tok.count == 0) return tok;
    String_View macro_val = {0};
    if (eval_macro_bind_get(ctx, tok, &macro_val)) return macro_val;
    if (eval_var_defined(ctx, tok)) return eval_var_get(ctx, tok);
    return tok;
}

static bool sv_list_contains_item(String_View list_text, String_View needle) {
    const char *p = list_text.data;
    const char *end = list_text.data + list_text.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View item = nob_sv_from_parts(p, (size_t)(q - p));
        if (sv_eq(item, needle)) return true;
        if (q >= end) break;
        p = q + 1;
    }
    return false;
}

static bool sv_is_digits(String_View sv) {
    if (sv.count == 0) return false;
    for (size_t i = 0; i < sv.count; i++) {
        if (!isdigit((unsigned char)sv.data[i])) return false;
    }
    return true;
}

static int sv_numeric_cmp_digits(String_View a, String_View b) {
    size_t ia = 0, ib = 0;
    while (ia < a.count && a.data[ia] == '0') ia++;
    while (ib < b.count && b.data[ib] == '0') ib++;

    size_t na = a.count - ia;
    size_t nb = b.count - ib;
    if (na < nb) return -1;
    if (na > nb) return 1;
    if (na == 0) return 0;
    int c = memcmp(a.data + ia, b.data + ib, na);
    if (c < 0) return -1;
    if (c > 0) return 1;
    return 0;
}

static bool sv_next_version_part(String_View sv, size_t *io_pos, String_View *out) {
    if (!io_pos || !out) return false;
    size_t pos = *io_pos;
    if (pos > sv.count) pos = sv.count;
    if (pos == sv.count) {
        *out = nob_sv_from_cstr("");
        return false;
    }
    size_t start = pos;
    while (pos < sv.count && sv.data[pos] != '.') pos++;
    *out = nob_sv_from_parts(sv.data + start, pos - start);
    *io_pos = (pos < sv.count) ? pos + 1 : pos;
    return true;
}

static int sv_version_cmp(String_View a, String_View b) {
    size_t pa = 0, pb = 0;
    for (;;) {
        String_View ca = {0}, cb = {0};
        bool ha = sv_next_version_part(a, &pa, &ca);
        bool hb = sv_next_version_part(b, &pb, &cb);
        if (!ha && !hb) return 0;

        int cmp = 0;
        bool da = sv_is_digits(ca);
        bool db = sv_is_digits(cb);
        if (da && db) cmp = sv_numeric_cmp_digits(ca, cb);
        else cmp = sv_lex_cmp(ca, cb);
        if (cmp != 0) return cmp;
    }
}

static bool sv_parse_positive_int(String_View sv, int *out) {
    if (!out || sv.count == 0) return false;
    char buf[64];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';

    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (!end || *end != '\0') return false;
    if (v <= 0 || v > EVAL_EXPAND_MAX_RECURSION_HARD_CAP) return false;
    *out = (int)v;
    return true;
}

static int eval_expand_limit(struct Evaluator_Context *ctx) {
    int limit = EVAL_EXPAND_MAX_RECURSION;

    // Priority 1: context variable (script/runtime configurable).
    if (ctx) {
        String_View cfg = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_EXPAND_MAX_RECURSION"));
        int parsed = 0;
        if (sv_parse_positive_int(cfg, &parsed)) {
            limit = parsed;
        }
    }

    // Priority 2: process environment override.
    {
        const char *env = eval_getenv_temp(ctx, "NOBIFY_EVAL_EXPAND_MAX_RECURSION");
        if (env && env[0] != '\0') {
            int parsed = 0;
            if (sv_parse_positive_int(nob_sv_from_cstr(env), &parsed)) {
                limit = parsed;
            }
        }
    }

    return limit;
}

static bool eval_truthy_constant_only(String_View v, bool *known) {
    if (known) *known = true;
    if (v.count == 0) return false;

    if (eval_sv_eq_ci_lit(v, "1") || eval_sv_eq_ci_lit(v, "ON") || eval_sv_eq_ci_lit(v, "YES") ||
        eval_sv_eq_ci_lit(v, "TRUE") || eval_sv_eq_ci_lit(v, "Y")) return true;

    if (eval_sv_eq_ci_lit(v, "0") || eval_sv_eq_ci_lit(v, "OFF") || eval_sv_eq_ci_lit(v, "NO") ||
        eval_sv_eq_ci_lit(v, "FALSE") || eval_sv_eq_ci_lit(v, "N") || eval_sv_eq_ci_lit(v, "IGNORE") ||
        eval_sv_eq_ci_lit(v, "NOTFOUND")) return false;

    if (sv_ends_with_ci_lit(v, "-NOTFOUND")) return false;

    long num = 0;
    if (sv_is_number(v, &num)) return num != 0;

    if (known) *known = false;
    return false;
}

bool eval_truthy(struct Evaluator_Context *ctx, String_View v) {
    bool known = false;
    bool direct = eval_truthy_constant_only(v, &known);
    if (known) return direct;

    if (ctx) {
        String_View macro_val = {0};
        if (eval_macro_bind_get(ctx, v, &macro_val)) {
            return eval_truthy_constant_only(macro_val, NULL);
        }

        if (eval_var_defined(ctx, v)) {
            String_View var_val = eval_var_get(ctx, v);
            return eval_truthy_constant_only(var_val, NULL);
        }
    }

    // Non-constant bare tokens are treated as true in if() conditions.
    return true;
}

static String_View sb_build_sv_temp(Evaluator_Context *ctx, Nob_String_Builder *sb) {
    if (!ctx || !sb || sb->count == 0) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sb->count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    memcpy(buf, sb->items, sb->count);
    buf[sb->count] = '\0';
    return nob_sv_from_cstr(buf);
}

static void sb_append_sv(Nob_String_Builder *sb, String_View sv) {
    if (!sb || !sv.data || sv.count == 0) return;
    nob_sb_append_buf(sb, sv.data, sv.count);
}

static String_View expand_once(struct Evaluator_Context *ctx, String_View in) {
    Arena *temp_arena = eval_temp_arena(ctx);
    if (!temp_arena) return nob_sv_from_cstr("");

    bool has_dollar = false;
    for (size_t i = 0; i < in.count; i++) {
        if (in.data[i] == '$' || (in.data[i] == '\\' && i + 1 < in.count && in.data[i + 1] == '$')) {
            has_dollar = true;
            break;
        }
    }
    if (!has_dollar) return in;

    Nob_String_Builder sb = {0};

    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];

        if (c == '\\' && i + 1 < in.count && in.data[i + 1] == '$') {
            nob_sb_append(&sb, '$');
            i++;
            continue;
        }

        if (c == '$' && i + 4 < in.count && memcmp(in.data + i, "$ENV{", 5) == 0) {
            size_t start = i + 5;
            size_t j = start;
            while (j < in.count && in.data[j] != '}') j++;
            if (j < in.count) {
                String_View name = nob_sv_from_parts(in.data + start, j - start);
                char *env_name = eval_sv_to_cstr_temp(ctx, name);
                if (eval_should_stop(ctx)) {
                    nob_sb_free(sb);
                    return nob_sv_from_cstr("");
                }
                const char *v = eval_getenv_temp(ctx, env_name);
                if (v) nob_sb_append_cstr(&sb, v);
                i = j;
                continue;
            }
        }

        if (c == '$' && i + 1 < in.count && in.data[i + 1] == '{') {
            size_t start = i + 2;
            size_t j = start;
            int depth = 1;
            while (j < in.count && depth > 0) {
                if (in.data[j] == '{') depth++;
                else if (in.data[j] == '}') depth--;
                j++;
            }
            if (depth == 0) {
                String_View inner = nob_sv_from_parts(in.data + start, (j - 1) - start);
                inner = eval_expand_vars(ctx, inner);
                String_View val = nob_sv_from_cstr("");
                if (!eval_macro_bind_get(ctx, inner, &val)) {
                    val = eval_var_get(ctx, inner);
                }
                sb_append_sv(&sb, val);
                i = j - 1;
                continue;
            }
        }

        nob_sb_append(&sb, c);
    }

    String_View out = sb_build_sv_temp(ctx, &sb);
    nob_sb_free(sb);
    return out;
}

String_View eval_expand_vars(struct Evaluator_Context *ctx, String_View input) {
    if (!ctx || eval_should_stop(ctx)) return nob_sv_from_cstr("");

    String_View cur = sv_copy_to_temp_arena(ctx, input);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    const int max_iter = eval_expand_limit(ctx);
    String_View *seen = (String_View*)arena_alloc(eval_temp_arena(ctx), sizeof(String_View) * (size_t)(max_iter + 1));
    EVAL_OOM_RETURN_IF_NULL(ctx, seen, nob_sv_from_cstr(""));
    size_t seen_count = 0;
    seen[seen_count++] = cur;

    for (int i = 0; i < max_iter; i++) {
        String_View next = expand_once(ctx, cur);
        if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        if (sv_eq(next, cur)) return next;

        // Proper cycle detection: any repeated expansion state means cycle.
        for (size_t j = 0; j < seen_count; j++) {
            if (sv_eq(next, seen[j])) {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_WARNING,
                                     nob_sv_from_cstr("eval_expr"),
                                     nob_sv_from_cstr("expand_vars"),
                                     (Cmake_Event_Origin){0},
                                     nob_sv_from_cstr("Cyclic variable expansion detected"),
                                     nob_sv_from_cstr("Check mutually recursive set() definitions"));
                return next;
            }
        }

        seen[seen_count++] = next;
        cur = next;
    }

    (void)eval_emit_diag(ctx,
                         EV_DIAG_WARNING,
                         nob_sv_from_cstr("eval_expr"),
                         nob_sv_from_cstr("expand_vars"),
                         (Cmake_Event_Origin){0},
                         nob_sv_from_cstr("Recursion limit exceeded"),
                         nob_sv_from_cstr("Tune CMAKE_NOBIFY_EXPAND_MAX_RECURSION or NOBIFY_EVAL_EXPAND_MAX_RECURSION"));
    return cur;
}

typedef struct {
    struct Evaluator_Context *ctx;
    String_View *toks;
    size_t count;
    size_t pos;
    Cmake_Event_Origin origin;
} Expr;

static bool expr_has(Expr *e) { return e->pos < e->count; }
static String_View expr_peek(Expr *e) { return e->toks[e->pos]; }
static String_View expr_next(Expr *e) { return e->toks[e->pos++]; }
static bool parse_expr(Expr *e);

static bool parse_primary(Expr *e) {
    if (!expr_has(e)) return false;
    String_View tok = expr_next(e);

    if (eval_sv_eq_ci_lit(tok, "(")) {
        bool v = parse_expr(e);
        if (expr_has(e) && eval_sv_eq_ci_lit(expr_peek(e), ")")) {
            expr_next(e);
        } else {
            eval_emit_diag(e->ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("eval_expr"),
                           nob_sv_from_cstr("if"),
                           e->origin,
                           nob_sv_from_cstr("Missing ')' in expression"),
                           nob_sv_from_cstr("Close parentheses"));
        }
        return v;
    }

    return eval_truthy(e->ctx, tok);
}

static bool parse_unary(Expr *e) {
    if (!expr_has(e)) return false;
    String_View tok = expr_peek(e);

    if (eval_sv_eq_ci_lit(tok, "NOT")) {
        expr_next(e);
        return !parse_unary(e);
    }

    if (eval_sv_eq_ci_lit(tok, "DEFINED")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        String_View var_name = expr_next(e);

        if (var_name.count > 4 && memcmp(var_name.data, "ENV{", 4) == 0 && var_name.data[var_name.count - 1] == '}') {
            size_t n = var_name.count - 5;
            String_View env_name_sv = nob_sv_from_parts(var_name.data + 4, n);
            char *env_name = eval_sv_to_cstr_temp(e->ctx, env_name_sv);
            EVAL_OOM_RETURN_IF_NULL(e->ctx, env_name, false);
            return eval_has_env(e->ctx, env_name);
        }

        return eval_var_defined(e->ctx, var_name);
    }

    if (eval_sv_eq_ci_lit(tok, "TARGET")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        return eval_target_known(e->ctx, expr_next(e));
    }

    if (eval_sv_eq_ci_lit(tok, "COMMAND")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        String_View name = expr_next(e);
        if (eval_dispatcher_is_known_command(name)) return true;
        return eval_user_cmd_find(e->ctx, name) != NULL;
    }

    if (eval_sv_eq_ci_lit(tok, "POLICY")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        return eval_policy_is_id(expr_next(e));
    }

    if (eval_sv_eq_ci_lit(tok, "EXISTS")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        char *path = eval_sv_to_cstr_temp(e->ctx, expr_next(e));
        EVAL_OOM_RETURN_IF_NULL(e->ctx, path, false);
        return path_exists_cstr(path);
    }

    if (eval_sv_eq_ci_lit(tok, "IS_DIRECTORY")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        char *path = eval_sv_to_cstr_temp(e->ctx, expr_next(e));
        EVAL_OOM_RETURN_IF_NULL(e->ctx, path, false);
        return path_is_dir_cstr(path);
    }

    if (eval_sv_eq_ci_lit(tok, "IS_SYMLINK")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        char *path = eval_sv_to_cstr_temp(e->ctx, expr_next(e));
        EVAL_OOM_RETURN_IF_NULL(e->ctx, path, false);
        return path_is_symlink_cstr(path);
    }

    if (eval_sv_eq_ci_lit(tok, "IS_ABSOLUTE")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        return eval_sv_is_abs_path(expr_next(e));
    }

    return parse_primary(e);
}

static bool parse_cmp(Expr *e) {
    if (!expr_has(e)) return false;

    String_View first = expr_peek(e);
    if (eval_sv_eq_ci_lit(first, "(") ||
        eval_sv_eq_ci_lit(first, "NOT") ||
        eval_sv_eq_ci_lit(first, "DEFINED") ||
        eval_sv_eq_ci_lit(first, "TARGET") ||
        eval_sv_eq_ci_lit(first, "COMMAND") ||
        eval_sv_eq_ci_lit(first, "POLICY") ||
        eval_sv_eq_ci_lit(first, "EXISTS") ||
        eval_sv_eq_ci_lit(first, "IS_DIRECTORY") ||
        eval_sv_eq_ci_lit(first, "IS_SYMLINK") ||
        eval_sv_eq_ci_lit(first, "IS_ABSOLUTE")) {
        return parse_unary(e);
    }

    size_t save = e->pos;
    String_View lhs = expr_next(e);

    if (eval_sv_eq_ci_lit(lhs, "(")) {
        e->pos = save;
        return parse_unary(e);
    }

    if (!expr_has(e)) return eval_truthy(e->ctx, lhs);

    String_View op = expr_peek(e);

    if (eval_sv_eq_ci_lit(op, "STREQUAL")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        return sv_eq(lhs, expr_next(e));
    }

    if (eval_sv_eq_ci_lit(op, "EQUAL")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        long a = 0, b = 0;
        if (!sv_is_number(lhs, &a) || !sv_is_number(expr_next(e), &b)) return false;
        return a == b;
    }

    if (eval_sv_eq_ci_lit(op, "LESS") || eval_sv_eq_ci_lit(op, "GREATER") ||
        eval_sv_eq_ci_lit(op, "LESS_EQUAL") || eval_sv_eq_ci_lit(op, "GREATER_EQUAL")) {
        bool is_less = eval_sv_eq_ci_lit(op, "LESS");
        bool is_greater = eval_sv_eq_ci_lit(op, "GREATER");
        bool is_le = eval_sv_eq_ci_lit(op, "LESS_EQUAL");
        bool is_ge = eval_sv_eq_ci_lit(op, "GREATER_EQUAL");
        expr_next(e);
        if (!expr_has(e)) return false;
        long a = 0, b = 0;
        if (!sv_is_number(lhs, &a) || !sv_is_number(expr_next(e), &b)) return false;
        if (is_less) return a < b;
        if (is_greater) return a > b;
        if (is_le) return a <= b;
        if (is_ge) return a >= b;
        return false;
    }

    if (eval_sv_eq_ci_lit(op, "STRLESS") || eval_sv_eq_ci_lit(op, "STRGREATER") ||
        eval_sv_eq_ci_lit(op, "STRLESS_EQUAL") || eval_sv_eq_ci_lit(op, "STRGREATER_EQUAL")) {
        bool is_less = eval_sv_eq_ci_lit(op, "STRLESS");
        bool is_greater = eval_sv_eq_ci_lit(op, "STRGREATER");
        bool is_le = eval_sv_eq_ci_lit(op, "STRLESS_EQUAL");
        bool is_ge = eval_sv_eq_ci_lit(op, "STRGREATER_EQUAL");
        expr_next(e);
        if (!expr_has(e)) return false;
        int cmp = sv_lex_cmp(lhs, expr_next(e));
        if (is_less) return cmp < 0;
        if (is_greater) return cmp > 0;
        if (is_le) return cmp <= 0;
        if (is_ge) return cmp >= 0;
        return false;
    }

    if (eval_sv_eq_ci_lit(op, "VERSION_LESS") || eval_sv_eq_ci_lit(op, "VERSION_GREATER") ||
        eval_sv_eq_ci_lit(op, "VERSION_EQUAL") || eval_sv_eq_ci_lit(op, "VERSION_LESS_EQUAL") ||
        eval_sv_eq_ci_lit(op, "VERSION_GREATER_EQUAL")) {
        bool is_less = eval_sv_eq_ci_lit(op, "VERSION_LESS");
        bool is_greater = eval_sv_eq_ci_lit(op, "VERSION_GREATER");
        bool is_eq = eval_sv_eq_ci_lit(op, "VERSION_EQUAL");
        bool is_le = eval_sv_eq_ci_lit(op, "VERSION_LESS_EQUAL");
        bool is_ge = eval_sv_eq_ci_lit(op, "VERSION_GREATER_EQUAL");
        expr_next(e);
        if (!expr_has(e)) return false;
        int cmp = sv_version_cmp(lhs, expr_next(e));
        if (is_less) return cmp < 0;
        if (is_greater) return cmp > 0;
        if (is_eq) return cmp == 0;
        if (is_le) return cmp <= 0;
        if (is_ge) return cmp >= 0;
        return false;
    }

    if (eval_sv_eq_ci_lit(op, "MATCHES")) {
        expr_next(e);
        if (!expr_has(e)) return false;

        String_View rhs = expr_next(e);
        char *pat = eval_sv_to_cstr_temp(e->ctx, rhs);
        char *subj = eval_sv_to_cstr_temp(e->ctx, lhs);
        EVAL_OOM_RETURN_IF_NULL(e->ctx, pat, false);
        EVAL_OOM_RETURN_IF_NULL(e->ctx, subj, false);

        regex_t re;
        if (regcomp(&re, pat, REG_EXTENDED) != 0) return false;
        int rc = regexec(&re, subj, 0, NULL, 0);
        regfree(&re);
        return rc == 0;
    }

    if (eval_sv_eq_ci_lit(op, "IN_LIST")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        String_View rhs = expr_next(e);
        String_View needle = sv_lookup_if_var(e->ctx, lhs);
        String_View list_text = sv_lookup_if_var(e->ctx, rhs);
        return sv_list_contains_item(list_text, needle);
    }

    if (eval_sv_eq_ci_lit(op, "PATH_EQUAL")) {
        expr_next(e);
        if (!expr_has(e)) return false;
        String_View rhs = expr_next(e);
        String_View lhs_path = sv_lookup_if_var(e->ctx, lhs);
        String_View rhs_path = sv_lookup_if_var(e->ctx, rhs);
        lhs_path = sv_path_normalize_temp(e->ctx, lhs_path);
        rhs_path = sv_path_normalize_temp(e->ctx, rhs_path);
        if (eval_should_stop(e->ctx)) return false;
        return sv_eq(lhs_path, rhs_path);
    }

    return eval_truthy(e->ctx, lhs);
}

static bool parse_and(Expr *e) {
    bool lhs = parse_cmp(e);
    while (expr_has(e) && eval_sv_eq_ci_lit(expr_peek(e), "AND")) {
        expr_next(e);
        bool rhs = parse_cmp(e);
        lhs = lhs && rhs;
    }
    return lhs;
}

static bool parse_expr(Expr *e) {
    bool lhs = parse_and(e);
    while (expr_has(e) && eval_sv_eq_ci_lit(expr_peek(e), "OR")) {
        expr_next(e);
        bool rhs = parse_and(e);
        lhs = lhs || rhs;
    }
    return lhs;
}

bool eval_condition(struct Evaluator_Context *ctx, const Args *raw_condition) {
    if (!ctx || !raw_condition) return false;

    SV_List toks = eval_resolve_args(ctx, raw_condition);
    if (eval_should_stop(ctx) || toks.count == 0) return false;

    Expr e = {0};
    e.ctx = ctx;
    e.toks = toks.items;
    e.count = toks.count;
    e.pos = 0;

    bool v = parse_expr(&e);

    if (e.pos != e.count) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_expr"),
                       nob_sv_from_cstr("if"),
                       e.origin,
                       nob_sv_from_cstr("Invalid if() syntax"),
                       nob_sv_from_cstr("Check operators and parentheses"));
        return false;
    }

    return v;
}
