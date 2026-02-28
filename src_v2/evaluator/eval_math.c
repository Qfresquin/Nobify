#include "eval_stdlib.h"

#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

static bool math_checked_neg(Math_Parser *p, long long value, long long *out) {
    if (value == LLONG_MIN) return math_emit_error(p, "Integer overflow in unary minus");
    *out = -value;
    return true;
}

static bool math_checked_add(Math_Parser *p, long long lhs, long long rhs, long long *out) {
    if ((rhs > 0 && lhs > LLONG_MAX - rhs) ||
        (rhs < 0 && lhs < LLONG_MIN - rhs)) {
        return math_emit_error(p, "Integer overflow in addition");
    }
    *out = lhs + rhs;
    return true;
}

static bool math_checked_sub(Math_Parser *p, long long lhs, long long rhs, long long *out) {
    if ((rhs > 0 && lhs < LLONG_MIN + rhs) ||
        (rhs < 0 && lhs > LLONG_MAX + rhs)) {
        return math_emit_error(p, "Integer overflow in subtraction");
    }
    *out = lhs - rhs;
    return true;
}

static bool math_checked_mul(Math_Parser *p, long long lhs, long long rhs, long long *out) {
    if (lhs == 0 || rhs == 0) {
        *out = 0;
        return true;
    }
    if ((lhs == -1 && rhs == LLONG_MIN) || (rhs == -1 && lhs == LLONG_MIN)) {
        return math_emit_error(p, "Integer overflow in multiplication");
    }

    if (lhs > 0) {
        if (rhs > 0) {
            if (lhs > LLONG_MAX / rhs) return math_emit_error(p, "Integer overflow in multiplication");
        } else {
            if (rhs < LLONG_MIN / lhs) return math_emit_error(p, "Integer overflow in multiplication");
        }
    } else {
        if (rhs > 0) {
            if (lhs < LLONG_MIN / rhs) return math_emit_error(p, "Integer overflow in multiplication");
        } else {
            if (lhs < LLONG_MAX / rhs) return math_emit_error(p, "Integer overflow in multiplication");
        }
    }

    *out = lhs * rhs;
    return true;
}

static bool math_checked_div(Math_Parser *p, long long lhs, long long rhs, long long *out) {
    if (rhs == 0) return math_emit_error(p, "Division by zero");
    if (lhs == LLONG_MIN && rhs == -1) return math_emit_error(p, "Integer overflow in division");
    *out = lhs / rhs;
    return true;
}

static bool math_checked_mod(Math_Parser *p, long long lhs, long long rhs, long long *out) {
    if (rhs == 0) return math_emit_error(p, "Division by zero");
    if (lhs == LLONG_MIN && rhs == -1) return math_emit_error(p, "Integer overflow in modulo");
    *out = lhs % rhs;
    return true;
}

static bool math_parse_expr(Math_Parser *p, long long *out);
static bool math_parse_bitor(Math_Parser *p, long long *out);
static bool math_parse_bitxor(Math_Parser *p, long long *out);
static bool math_parse_bitand(Math_Parser *p, long long *out);
static bool math_parse_shift(Math_Parser *p, long long *out);
static bool math_parse_addsub(Math_Parser *p, long long *out);

static bool math_parse_number(Math_Parser *p, long long *out) {
    math_skip_ws(p);
    const char *start = p->s + p->pos;
    char *end = NULL;
    errno = 0;
    long long v = strtoll(start, &end, 0);
    if (end == start) return false;
    if (errno == ERANGE) return math_emit_error(p, "Integer literal out of range in expression");
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
        return math_checked_neg(p, *out, out);
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

        if (op == '*') {
            if (!math_checked_mul(p, *out, rhs, out)) return false;
        } else if (op == '/') {
            if (!math_checked_div(p, *out, rhs, out)) return false;
        } else {
            if (!math_checked_mod(p, *out, rhs, out)) return false;
        }
    }
    return true;
}

static bool math_parse_expr(Math_Parser *p, long long *out) {
    return math_parse_bitor(p, out);
}

static bool math_parse_shift(Math_Parser *p, long long *out) {
    if (!math_parse_addsub(p, out)) return false;
    for (;;) {
        math_skip_ws(p);
        bool is_shl = (p->s[p->pos] == '<' && p->s[p->pos + 1] == '<');
        bool is_shr = (p->s[p->pos] == '>' && p->s[p->pos + 1] == '>');
        if (!is_shl && !is_shr) break;
        p->pos += 2;

        long long rhs = 0;
        if (!math_parse_addsub(p, &rhs)) return false;
        if (rhs < 0) return math_emit_error(p, "Negative shift count");
        if (rhs >= (long long)(sizeof(long long) * CHAR_BIT)) {
            return math_emit_error(p, "Shift count is out of range for 64-bit integer");
        }

        if (is_shl) {
            if (rhs > 0 && *out >= 0 && *out > (LLONG_MAX >> rhs)) {
                return math_emit_error(p, "Integer overflow in left shift");
            }
            *out = (long long)(((unsigned long long)*out) << rhs);
        } else {
            *out = *out >> rhs;
        }
    }
    return true;
}

static bool math_parse_addsub(Math_Parser *p, long long *out) {
    if (!math_parse_term(p, out)) return false;
    for (;;) {
        math_skip_ws(p);
        char op = p->s[p->pos];
        if (op != '+' && op != '-') break;
        p->pos++;

        long long rhs = 0;
        if (!math_parse_term(p, &rhs)) return false;
        if (op == '+') {
            if (!math_checked_add(p, *out, rhs, out)) return false;
        } else {
            if (!math_checked_sub(p, *out, rhs, out)) return false;
        }
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

bool eval_handle_math(Evaluator_Context *ctx, const Node *node) {
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
    String_View expr_sv = (a.count == 3) ? a.items[2] : svu_join_no_sep_temp(ctx, &a.items[2], a.count - 2);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    char *expr_buf = (char*)arena_alloc(eval_temp_arena(ctx), expr_sv.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, expr_buf, !eval_should_stop(ctx));
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

