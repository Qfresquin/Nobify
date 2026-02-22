#include "math_parser.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t pos;
    Math_Eval_Status status;
} Math_Parser;

static long long math_wrap_add(long long a, long long b) {
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    return (long long)(ua + ub);
}

static long long math_wrap_sub(long long a, long long b) {
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    return (long long)(ua - ub);
}

static long long math_wrap_mul(long long a, long long b) {
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    return (long long)(ua * ub);
}

static long long math_wrap_neg(long long a) {
    unsigned long long ua = (unsigned long long)a;
    return (long long)(0ULL - ua);
}

static void math_skip_ws(Math_Parser *p) {
    while (p->text[p->pos] && isspace((unsigned char)p->text[p->pos])) p->pos++;
}

static bool math_match_ch(Math_Parser *p, char ch) {
    math_skip_ws(p);
    if (p->text[p->pos] != ch) return false;
    p->pos++;
    return true;
}

static bool math_match_str(Math_Parser *p, const char *lit) {
    math_skip_ws(p);
    size_t len = strlen(lit);
    if (strncmp(p->text + p->pos, lit, len) != 0) return false;
    p->pos += len;
    return true;
}

static long long math_parse_expr_or(Math_Parser *p);

static long long math_parse_primary(Math_Parser *p) {
    if (p->status != MATH_EVAL_OK) return 0;
    math_skip_ws(p);

    if (math_match_ch(p, '(')) {
        long long value = math_parse_expr_or(p);
        if (!math_match_ch(p, ')')) p->status = MATH_EVAL_INVALID_EXPR;
        return value;
    }

    const char *start = p->text + p->pos;
    char *endptr = NULL;
    errno = 0;
    long long value = strtoll(start, &endptr, 0);
    if (!endptr || endptr == start) {
        p->status = MATH_EVAL_INVALID_EXPR;
        return 0;
    }
    if (errno == ERANGE) {
        p->status = MATH_EVAL_RANGE;
        return 0;
    }

    p->pos += (size_t)(endptr - start);
    return value;
}

static long long math_parse_unary(Math_Parser *p) {
    if (p->status != MATH_EVAL_OK) return 0;
    if (math_match_ch(p, '+')) return +math_parse_unary(p);
    if (math_match_ch(p, '-')) return math_wrap_neg(math_parse_unary(p));
    if (math_match_ch(p, '~')) return ~math_parse_unary(p);
    return math_parse_primary(p);
}

static long long math_parse_mul(Math_Parser *p) {
    long long lhs = math_parse_unary(p);
    while (p->status == MATH_EVAL_OK) {
        if (math_match_ch(p, '*')) {
            lhs = math_wrap_mul(lhs, math_parse_unary(p));
        } else if (math_match_ch(p, '/')) {
            long long rhs = math_parse_unary(p);
            if (rhs == 0) {
                p->status = MATH_EVAL_DIV_ZERO;
                return 0;
            }
            if (lhs == LLONG_MIN && rhs == -1) {
                p->status = MATH_EVAL_RANGE;
                return 0;
            }
            lhs /= rhs;
        } else if (math_match_ch(p, '%')) {
            long long rhs = math_parse_unary(p);
            if (rhs == 0) {
                p->status = MATH_EVAL_DIV_ZERO;
                return 0;
            }
            if (lhs == LLONG_MIN && rhs == -1) {
                p->status = MATH_EVAL_RANGE;
                return 0;
            }
            lhs %= rhs;
        } else {
            break;
        }
    }
    return lhs;
}

static long long math_parse_add(Math_Parser *p) {
    long long lhs = math_parse_mul(p);
    while (p->status == MATH_EVAL_OK) {
        if (math_match_ch(p, '+')) {
            lhs = math_wrap_add(lhs, math_parse_mul(p));
        } else if (math_match_ch(p, '-')) {
            lhs = math_wrap_sub(lhs, math_parse_mul(p));
        } else {
            break;
        }
    }
    return lhs;
}

static long long math_parse_shift(Math_Parser *p) {
    long long lhs = math_parse_add(p);
    while (p->status == MATH_EVAL_OK) {
        if (math_match_str(p, "<<")) {
            long long rhs = math_parse_add(p);
            unsigned long long shift = ((unsigned long long)rhs) & 63ULL;
            lhs = (long long)(((unsigned long long)lhs) << shift);
        } else if (math_match_str(p, ">>")) {
            long long rhs = math_parse_add(p);
            unsigned long long shift = ((unsigned long long)rhs) & 63ULL;
            lhs >>= (int)shift;
        } else {
            break;
        }
    }
    return lhs;
}

static long long math_parse_and(Math_Parser *p) {
    long long lhs = math_parse_shift(p);
    while (p->status == MATH_EVAL_OK) {
        if (math_match_ch(p, '&')) {
            lhs &= math_parse_shift(p);
        } else {
            break;
        }
    }
    return lhs;
}

static long long math_parse_xor(Math_Parser *p) {
    long long lhs = math_parse_and(p);
    while (p->status == MATH_EVAL_OK) {
        if (math_match_ch(p, '^')) {
            lhs ^= math_parse_and(p);
        } else {
            break;
        }
    }
    return lhs;
}

static long long math_parse_expr_or(Math_Parser *p) {
    long long lhs = math_parse_xor(p);
    while (p->status == MATH_EVAL_OK) {
        if (math_match_ch(p, '|')) {
            lhs |= math_parse_xor(p);
        } else {
            break;
        }
    }
    return lhs;
}

Math_Eval_Status math_eval_i64(String_View expr, long long *out_value) {
    if (!out_value) return MATH_EVAL_INVALID_EXPR;

    Math_Parser parser = {0};
    parser.text = nob_temp_sv_to_cstr(expr);
    parser.status = MATH_EVAL_OK;

    long long value = math_parse_expr_or(&parser);
    math_skip_ws(&parser);
    if (parser.status != MATH_EVAL_OK) return parser.status;
    if (parser.text[parser.pos] != '\0') return MATH_EVAL_INVALID_EXPR;

    *out_value = value;
    return MATH_EVAL_OK;
}