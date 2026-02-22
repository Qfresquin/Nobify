#include "eval_stdlib.h"

#include "evaluator_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pcre/pcre2posix.h"

static String_View sv_join_no_sep_temp(Evaluator_Context *ctx, String_View *items, size_t count) {
    if (!ctx || count == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += items[i].count;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    if (!buf) {
        ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].count) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static size_t list_count_items(String_View list_sv) {
    if (list_sv.count == 0) return 0;
    size_t count = 1;
    for (size_t i = 0; i < list_sv.count; i++) {
        if (list_sv.data[i] == ';') count++;
    }
    return count;
}

static bool list_item_in_set(String_View item, String_View *set, size_t set_count) {
    for (size_t i = 0; i < set_count; i++) {
        if (eval_sv_key_eq(item, set[i])) return true;
    }
    return false;
}

typedef struct {
    const char *s;
    size_t pos;
    Cmake_Event_Origin origin;
    Evaluator_Context *ctx;
    String_View command;
    bool ok;
} Math_Parser;

static void math_skip_ws(Math_Parser *p) {
    while (p->s[p->pos] != '\0' && isspace((unsigned char)p->s[p->pos])) p->pos++;
}

static bool math_emit_error(Math_Parser *p, const char *cause) {
    eval_emit_diag(p->ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("math"),
                   p->command,
                   p->origin,
                   nob_sv_from_cstr(cause),
                   nob_sv_from_cstr("Use integer expression with + - * / % << >> & ^ | ~ and parentheses"));
    p->ok = false;
    return false;
}

static bool math_parse_expr(Math_Parser *p, long long *out);
static bool math_parse_bitor(Math_Parser *p, long long *out);
static bool math_parse_bitxor(Math_Parser *p, long long *out);
static bool math_parse_bitand(Math_Parser *p, long long *out);
static bool math_parse_shift(Math_Parser *p, long long *out);

static bool math_parse_number(Math_Parser *p, long long *out) {
    math_skip_ws(p);
    const char *start = p->s + p->pos;
    char *end = NULL;
    long long v = strtoll(start, &end, 0);
    if (end == start) return false;
    p->pos += (size_t)(end - start);
    *out = v;
    return true;
}

static bool math_parse_factor(Math_Parser *p, long long *out) {
    math_skip_ws(p);
    char c = p->s[p->pos];
    if (c == '\0') return math_emit_error(p, "Unexpected end of expression");

    if (c == '(') {
        p->pos++;
        if (!math_parse_expr(p, out)) return false;
        math_skip_ws(p);
        if (p->s[p->pos] != ')') return math_emit_error(p, "Missing ')' in expression");
        p->pos++;
        return true;
    }

    if (c == '+') {
        p->pos++;
        return math_parse_factor(p, out);
    }
    if (c == '-') {
        p->pos++;
        if (!math_parse_factor(p, out)) return false;
        *out = -*out;
        return true;
    }
    if (c == '~') {
        p->pos++;
        if (!math_parse_factor(p, out)) return false;
        *out = ~(*out);
        return true;
    }

    if (!math_parse_number(p, out)) return math_emit_error(p, "Invalid numeric token in expression");
    return true;
}

static bool math_parse_term(Math_Parser *p, long long *out) {
    if (!math_parse_factor(p, out)) return false;
    for (;;) {
        math_skip_ws(p);
        char op = p->s[p->pos];
        if (op != '*' && op != '/' && op != '%') break;
        p->pos++;

        long long rhs = 0;
        if (!math_parse_factor(p, &rhs)) return false;

        if ((op == '/' || op == '%') && rhs == 0) return math_emit_error(p, "Division by zero");
        if (op == '*') *out = *out * rhs;
        else if (op == '/') *out = *out / rhs;
        else *out = *out % rhs;
    }
    return true;
}

static bool math_parse_expr(Math_Parser *p, long long *out) {
    return math_parse_bitor(p, out);
}

static bool math_parse_shift(Math_Parser *p, long long *out) {
    if (!math_parse_term(p, out)) return false;
    for (;;) {
        math_skip_ws(p);
        bool is_shl = (p->s[p->pos] == '<' && p->s[p->pos + 1] == '<');
        bool is_shr = (p->s[p->pos] == '>' && p->s[p->pos + 1] == '>');
        if (!is_shl && !is_shr) break;
        p->pos += 2;

        long long rhs = 0;
        if (!math_parse_term(p, &rhs)) return false;
        if (rhs < 0) return math_emit_error(p, "Negative shift count");

        if (is_shl) *out = *out << rhs;
        else *out = *out >> rhs;
    }
    return true;
}

static bool math_parse_bitand(Math_Parser *p, long long *out) {
    if (!math_parse_shift(p, out)) return false;
    for (;;) {
        math_skip_ws(p);
        if (p->s[p->pos] != '&') break;
        p->pos++;

        long long rhs = 0;
        if (!math_parse_shift(p, &rhs)) return false;
        *out = *out & rhs;
    }
    return true;
}

static bool math_parse_bitxor(Math_Parser *p, long long *out) {
    if (!math_parse_bitand(p, out)) return false;
    for (;;) {
        math_skip_ws(p);
        if (p->s[p->pos] != '^') break;
        p->pos++;

        long long rhs = 0;
        if (!math_parse_bitand(p, &rhs)) return false;
        *out = *out ^ rhs;
    }
    return true;
}

static bool math_parse_bitor(Math_Parser *p, long long *out) {
    if (!math_parse_bitxor(p, out)) return false;
    for (;;) {
        math_skip_ws(p);
        char op = p->s[p->pos];
        if (op != '|') break;
        p->pos++;

        long long rhs = 0;
        if (!math_parse_bitxor(p, &rhs)) return false;
        *out = *out | rhs;
    }
    return true;
}

bool h_list(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return !eval_should_stop(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    if (eval_sv_eq_ci_lit(a.items[0], "APPEND")) {
        if (a.count < 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(APPEND) requires list variable name"),
                           nob_sv_from_cstr("Usage: list(APPEND <list> [elements...])"));
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        if (a.count == 2) return !eval_should_stop(ctx);

        String_View existing = eval_var_get(ctx, var);
        String_View appended = eval_sv_join_semi_temp(ctx, &a.items[2], a.count - 2);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        if (existing.count == 0) {
            (void)eval_var_set(ctx, var, appended);
            return !eval_should_stop(ctx);
        }

        size_t total = existing.count + 1 + appended.count;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        if (!buf) {
            ctx_oom(ctx);
            return !eval_should_stop(ctx);
        }
        memcpy(buf, existing.data, existing.count);
        buf[existing.count] = ';';
        memcpy(buf + existing.count + 1, appended.data, appended.count);
        buf[total] = '\0';
        (void)eval_var_set(ctx, var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REMOVE_ITEM")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(REMOVE_ITEM) requires list variable and at least one item"),
                           nob_sv_from_cstr("Usage: list(REMOVE_ITEM <list> <item>...)"));
            return !eval_should_stop(ctx);
        }

        String_View var = a.items[1];
        String_View current = eval_var_get(ctx, var);
        if (current.count == 0) return !eval_should_stop(ctx);

        size_t keep_count = 0;
        size_t keep_total = 0;

        const char *p = current.data;
        const char *end = current.data + current.count;
        while (p <= end) {
            const char *q = p;
            while (q < end && *q != ';') q++;
            String_View item = nob_sv_from_parts(p, (size_t)(q - p));
            if (!list_item_in_set(item, &a.items[2], a.count - 2)) {
                keep_count++;
                keep_total += item.count;
            }
            if (q == end) break;
            p = q + 1;
        }

        if (keep_count == 0) {
            (void)eval_var_set(ctx, var, nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        size_t total = keep_total + (keep_count - 1);
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        if (!buf) {
            ctx_oom(ctx);
            return !eval_should_stop(ctx);
        }

        size_t off = 0;
        size_t emitted = 0;
        p = current.data;
        while (p <= end) {
            const char *q = p;
            while (q < end && *q != ';') q++;
            String_View item = nob_sv_from_parts(p, (size_t)(q - p));
            if (!list_item_in_set(item, &a.items[2], a.count - 2)) {
                if (emitted > 0) buf[off++] = ';';
                if (item.count) {
                    memcpy(buf + off, item.data, item.count);
                    off += item.count;
                }
                emitted++;
            }
            if (q == end) break;
            p = q + 1;
        }
        buf[off] = '\0';
        (void)eval_var_set(ctx, var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "LENGTH")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                           nob_sv_from_cstr("list(LENGTH) requires list variable and output variable"),
                           nob_sv_from_cstr("Usage: list(LENGTH <list> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View list_var = a.items[1];
        String_View out_var = a.items[2];
        String_View list_value = eval_var_get(ctx, list_var);
        size_t len = list_count_items(list_value);

        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%zu", len);
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(num_buf));
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("list"), node->as.cmd.name, o,
                   nob_sv_from_cstr("Unsupported list() subcommand"),
                   nob_sv_from_cstr("Implemented: APPEND, REMOVE_ITEM, LENGTH"));
    eval_request_stop_on_error(ctx);
    return !eval_should_stop(ctx);
}

bool h_string(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return !eval_should_stop(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    if (eval_sv_eq_ci_lit(a.items[0], "REPLACE")) {
        if (a.count < 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(REPLACE) requires match, replace, out-var and input"),
                           nob_sv_from_cstr("Usage: string(REPLACE <match> <replace> <out-var> <input>...)"));
            return !eval_should_stop(ctx);
        }

        String_View match = a.items[1];
        String_View repl = a.items[2];
        String_View out_var = a.items[3];
        String_View input = (a.count > 4) ? eval_sv_join_semi_temp(ctx, &a.items[4], a.count - 4) : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        if (match.count == 0) {
            (void)eval_var_set(ctx, out_var, input);
            return !eval_should_stop(ctx);
        }

        size_t hits = 0;
        size_t i = 0;
        while (i + match.count <= input.count) {
            if (memcmp(input.data + i, match.data, match.count) == 0) {
                hits++;
                i += match.count;
            } else {
                i++;
            }
        }

        size_t total = input.count;
        if (repl.count >= match.count) {
            total += hits * (repl.count - match.count);
        } else {
            total -= hits * (match.count - repl.count);
        }
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        if (!buf) {
            ctx_oom(ctx);
            return !eval_should_stop(ctx);
        }

        size_t off = 0;
        i = 0;
        while (i < input.count) {
            if (i + match.count <= input.count && memcmp(input.data + i, match.data, match.count) == 0) {
                if (repl.count) {
                    memcpy(buf + off, repl.data, repl.count);
                    off += repl.count;
                }
                i += match.count;
            } else {
                buf[off++] = input.data[i++];
            }
        }
        buf[off] = '\0';
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "TOUPPER")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(TOUPPER) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(TOUPPER <input> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View out_var = a.items[a.count - 1];
        String_View input = (a.count == 3) ? a.items[1] : eval_sv_join_semi_temp(ctx, &a.items[1], a.count - 2);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        if (!buf) {
            ctx_oom(ctx);
            return !eval_should_stop(ctx);
        }
        for (size_t i = 0; i < input.count; i++) {
            buf[i] = (char)toupper((unsigned char)input.data[i]);
        }
        buf[input.count] = '\0';
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REGEX")) {
        if (a.count < 5 || !eval_sv_eq_ci_lit(a.items[1], "MATCH")) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("Only string(REGEX MATCH ...) is supported"),
                           nob_sv_from_cstr("Usage: string(REGEX MATCH <regex> <out-var> <input>...)"));
            return !eval_should_stop(ctx);
        }

        String_View pattern = a.items[2];
        String_View out_var = a.items[3];
        String_View input = (a.count > 4) ? eval_sv_join_semi_temp(ctx, &a.items[4], a.count - 4) : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        char *pat_buf = (char*)arena_alloc(eval_temp_arena(ctx), pattern.count + 1);
        if (!pat_buf) {
            ctx_oom(ctx);
            return !eval_should_stop(ctx);
        }
        memcpy(pat_buf, pattern.data, pattern.count);
        pat_buf[pattern.count] = '\0';

        char *in_buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        if (!in_buf) {
            ctx_oom(ctx);
            return !eval_should_stop(ctx);
        }
        memcpy(in_buf, input.data, input.count);
        in_buf[input.count] = '\0';

        regex_t re;
        if (regcomp(&re, pat_buf, REG_EXTENDED) != 0) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("Invalid regex pattern"), pattern);
            return !eval_should_stop(ctx);
        }

        regmatch_t m[1];
        int rc = regexec(&re, in_buf, 1, m, 0);
        regfree(&re);

        if (rc == 0 && m[0].rm_so >= 0 && m[0].rm_eo >= m[0].rm_so) {
            size_t mlen = (size_t)(m[0].rm_eo - m[0].rm_so);
            String_View match_sv = nob_sv_from_parts(in_buf + m[0].rm_so, mlen);
            (void)eval_var_set(ctx, out_var, match_sv);
        } else {
            (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(""));
        }
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                   nob_sv_from_cstr("Unsupported string() subcommand"),
                   nob_sv_from_cstr("Implemented: REPLACE, TOUPPER, REGEX MATCH"));
    eval_request_stop_on_error(ctx);
    return !eval_should_stop(ctx);
}

bool h_math(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return !eval_should_stop(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    if (!eval_sv_eq_ci_lit(a.items[0], "EXPR")) {
        eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("math"), node->as.cmd.name, o,
                       nob_sv_from_cstr("Unsupported math() subcommand"),
                       nob_sv_from_cstr("Implemented: EXPR"));
        return !eval_should_stop(ctx);
    }

    if (a.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("math"), node->as.cmd.name, o,
                       nob_sv_from_cstr("math(EXPR) requires output variable and expression"),
                       nob_sv_from_cstr("Usage: math(EXPR <out-var> <expression>)"));
        return !eval_should_stop(ctx);
    }

    String_View out_var = a.items[1];
    String_View expr_sv = (a.count == 3) ? a.items[2] : sv_join_no_sep_temp(ctx, &a.items[2], a.count - 2);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    char *expr_buf = (char*)arena_alloc(eval_temp_arena(ctx), expr_sv.count + 1);
    if (!expr_buf) {
        ctx_oom(ctx);
        return !eval_should_stop(ctx);
    }
    memcpy(expr_buf, expr_sv.data, expr_sv.count);
    expr_buf[expr_sv.count] = '\0';

    Math_Parser p = {
        .s = expr_buf,
        .pos = 0,
        .origin = o,
        .ctx = ctx,
        .command = node->as.cmd.name,
        .ok = true,
    };

    long long value = 0;
    if (!math_parse_expr(&p, &value)) return !eval_should_stop(ctx);
    math_skip_ws(&p);
    if (p.s[p.pos] != '\0') {
        (void)math_emit_error(&p, "Unexpected trailing tokens in expression");
        return !eval_should_stop(ctx);
    }

    char out_buf[64];
    snprintf(out_buf, sizeof(out_buf), "%lld", value);
    (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(out_buf));
    return !eval_should_stop(ctx);
}

