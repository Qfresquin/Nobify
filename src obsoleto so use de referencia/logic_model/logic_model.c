#include "logic_model.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool logic_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        unsigned char ca = (unsigned char)a.data[i];
        unsigned char cb = (unsigned char)b.data[i];
        if (toupper(ca) != toupper(cb)) return false;
    }
    return true;
}

static bool logic_sv_eq_ci_lit(String_View a, const char *lit) {
    return logic_sv_eq_ci(a, sv_from_cstr(lit));
}

static String_View logic_trim(String_View sv) {
    size_t begin = 0;
    size_t end = sv.count;
    while (begin < end && isspace((unsigned char)sv.data[begin])) begin++;
    while (end > begin && isspace((unsigned char)sv.data[end - 1])) end--;
    return nob_sv_from_parts(sv.data + begin, end - begin);
}

static bool logic_sv_ends_with_ci(String_View sv, String_View suffix) {
    if (suffix.count > sv.count) return false;
    String_View tail = nob_sv_from_parts(sv.data + (sv.count - suffix.count), suffix.count);
    return logic_sv_eq_ci(tail, suffix);
}

static bool logic_cmake_string_is_false(String_View value) {
    String_View v = logic_trim(value);
    if (v.count == 0) return true;
    if (nob_sv_eq(v, sv_from_cstr("0"))) return true;
    if (logic_sv_eq_ci(v, sv_from_cstr("FALSE"))) return true;
    if (logic_sv_eq_ci(v, sv_from_cstr("OFF"))) return true;
    if (logic_sv_eq_ci(v, sv_from_cstr("NO"))) return true;
    if (logic_sv_eq_ci(v, sv_from_cstr("N"))) return true;
    if (logic_sv_eq_ci(v, sv_from_cstr("IGNORE"))) return true;
    if (logic_sv_eq_ci(v, sv_from_cstr("NOTFOUND"))) return true;
    if (logic_sv_ends_with_ci(v, sv_from_cstr("-NOTFOUND"))) return true;
    return false;
}

static bool logic_sv_is_i64_literal(String_View sv) {
    if (sv.count == 0) return false;
    size_t i = 0;
    if (sv.data[i] == '+' || sv.data[i] == '-') {
        i++;
        if (i >= sv.count) return false;
    }
    for (; i < sv.count; i++) {
        if (!isdigit((unsigned char)sv.data[i])) return false;
    }
    return true;
}

static bool logic_parse_i64_sv(String_View value, long long *out) {
    const char *cstr = nob_temp_sv_to_cstr(value);
    char *end = NULL;
    long long num = strtoll(cstr, &end, 10);
    if (!end || *end != '\0') return false;
    if (out) *out = num;
    return true;
}

static int logic_parse_version_part(const char *s, size_t len, size_t *idx) {
    long long val = 0;
    bool has_digit = false;
    while (*idx < len && s[*idx] >= '0' && s[*idx] <= '9') {
        has_digit = true;
        val = val * 10 + (s[*idx] - '0');
        (*idx)++;
    }
    return has_digit ? (int)val : 0;
}

static int logic_compare_versions_sv(String_View a, String_View b) {
    const char *as = nob_temp_sv_to_cstr(a);
    const char *bs = nob_temp_sv_to_cstr(b);
    size_t al = strlen(as), bl = strlen(bs);
    size_t ai = 0, bi = 0;

    while (ai < al || bi < bl) {
        int ap = logic_parse_version_part(as, al, &ai);
        int bp = logic_parse_version_part(bs, bl, &bi);

        if (ap < bp) return -1;
        if (ap > bp) return 1;

        if (ai < al && as[ai] == '.') ai++;
        if (bi < bl && bs[bi] == '.') bi++;

        while (ai < al && as[ai] != '.' && (as[ai] < '0' || as[ai] > '9')) ai++;
        while (bi < bl && bs[bi] != '.' && (bs[bi] < '0' || bs[bi] > '9')) bi++;
    }

    return 0;
}

static Logic_Node *logic_node_alloc(Arena *arena, Logic_Op_Type type) {
    if (!arena) return NULL;
    Logic_Node *node = arena_alloc_zero(arena, sizeof(*node));
    if (!node) return NULL;
    node->type = type;
    return node;
}

Logic_Node *logic_true(Arena *arena) {
    return logic_node_alloc(arena, LOGIC_OP_LITERAL_TRUE);
}

Logic_Node *logic_false(Arena *arena) {
    return logic_node_alloc(arena, LOGIC_OP_LITERAL_FALSE);
}

Logic_Node *logic_bool(Arena *arena, String_View token, bool quoted) {
    Logic_Node *node = logic_node_alloc(arena, LOGIC_OP_BOOL);
    if (!node) return NULL;
    node->as.operand.token = token;
    node->as.operand.quoted = quoted;
    return node;
}

Logic_Node *logic_defined(Arena *arena, String_View token) {
    Logic_Node *node = logic_node_alloc(arena, LOGIC_OP_DEFINED);
    if (!node) return NULL;
    node->as.operand.token = token;
    node->as.operand.quoted = false;
    return node;
}

Logic_Node *logic_compare(Arena *arena, Logic_Comparator op, Logic_Operand lhs, Logic_Operand rhs) {
    Logic_Node *node = logic_node_alloc(arena, LOGIC_OP_COMPARE);
    if (!node) return NULL;
    node->as.cmp.op = op;
    node->as.cmp.lhs = lhs;
    node->as.cmp.rhs = rhs;
    return node;
}

Logic_Node *logic_not(Arena *arena, Logic_Node *node) {
    Logic_Node *out = logic_node_alloc(arena, LOGIC_OP_NOT);
    if (!out) return NULL;
    out->left = node;
    return out;
}

Logic_Node *logic_and(Arena *arena, Logic_Node *a, Logic_Node *b) {
    if (!a) return b;
    if (!b) return a;
    Logic_Node *out = logic_node_alloc(arena, LOGIC_OP_AND);
    if (!out) return NULL;
    out->left = a;
    out->right = b;
    return out;
}

Logic_Node *logic_or(Arena *arena, Logic_Node *a, Logic_Node *b) {
    if (!a) return b;
    if (!b) return a;
    Logic_Node *out = logic_node_alloc(arena, LOGIC_OP_OR);
    if (!out) return NULL;
    out->left = a;
    out->right = b;
    return out;
}

typedef struct {
    const Logic_Parse_Input *in;
    size_t idx;
    bool ok;
} Logic_Parser;

static bool logic_match_comparator(String_View token, Logic_Comparator *out_cmp) {
    if (!out_cmp) return false;
    if (logic_sv_eq_ci_lit(token, "STREQUAL")) { *out_cmp = LOGIC_CMP_STREQUAL; return true; }
    if (logic_sv_eq_ci_lit(token, "EQUAL")) { *out_cmp = LOGIC_CMP_EQUAL; return true; }
    if (logic_sv_eq_ci_lit(token, "LESS")) { *out_cmp = LOGIC_CMP_LESS; return true; }
    if (logic_sv_eq_ci_lit(token, "GREATER")) { *out_cmp = LOGIC_CMP_GREATER; return true; }
    if (logic_sv_eq_ci_lit(token, "LESS_EQUAL")) { *out_cmp = LOGIC_CMP_LESS_EQUAL; return true; }
    if (logic_sv_eq_ci_lit(token, "GREATER_EQUAL")) { *out_cmp = LOGIC_CMP_GREATER_EQUAL; return true; }
    if (logic_sv_eq_ci_lit(token, "VERSION_LESS")) { *out_cmp = LOGIC_CMP_VERSION_LESS; return true; }
    if (logic_sv_eq_ci_lit(token, "VERSION_GREATER")) { *out_cmp = LOGIC_CMP_VERSION_GREATER; return true; }
    if (logic_sv_eq_ci_lit(token, "VERSION_EQUAL")) { *out_cmp = LOGIC_CMP_VERSION_EQUAL; return true; }
    if (logic_sv_eq_ci_lit(token, "VERSION_LESS_EQUAL")) { *out_cmp = LOGIC_CMP_VERSION_LESS_EQUAL; return true; }
    if (logic_sv_eq_ci_lit(token, "VERSION_GREATER_EQUAL")) { *out_cmp = LOGIC_CMP_VERSION_GREATER_EQUAL; return true; }
    return false;
}

static String_View logic_curr_token(Logic_Parser *p) {
    return p->in->tokens[p->idx];
}

static bool logic_curr_quoted(Logic_Parser *p) {
    return p->in->quoted_tokens ? p->in->quoted_tokens[p->idx] : false;
}

static bool logic_has_token(Logic_Parser *p) {
    return p->idx < p->in->count;
}

static Logic_Node *logic_parse_or_expr(Logic_Parser *p);

static Logic_Node *logic_parse_atom_expr(Logic_Parser *p) {
    Arena *arena = p->in->arena;
    if (!logic_has_token(p)) {
        p->ok = false;
        return logic_false(arena);
    }

    String_View lhs_tok = logic_curr_token(p);
    bool lhs_quoted = logic_curr_quoted(p);
    p->idx++;

    if (nob_sv_eq(lhs_tok, sv_from_cstr("("))) {
        Logic_Node *node = logic_parse_or_expr(p);
        if (logic_has_token(p) && nob_sv_eq(logic_curr_token(p), sv_from_cstr(")"))) {
            p->idx++;
        } else {
            p->ok = false;
        }
        return node ? node : logic_false(arena);
    }

    if (nob_sv_eq(lhs_tok, sv_from_cstr(")"))) {
        p->ok = false;
        return logic_false(arena);
    }

    if (logic_sv_eq_ci_lit(lhs_tok, "DEFINED")) {
        if (!logic_has_token(p)) {
            p->ok = false;
            return logic_false(arena);
        }
        String_View var_name = logic_curr_token(p);
        p->idx++;
        return logic_defined(arena, var_name);
    }

    Logic_Comparator cmp = LOGIC_CMP_STREQUAL;
    if (logic_has_token(p) && logic_match_comparator(logic_curr_token(p), &cmp)) {
        p->idx++;
        if (!logic_has_token(p)) {
            p->ok = false;
            return logic_false(arena);
        }
        String_View rhs_tok = logic_curr_token(p);
        bool rhs_quoted = logic_curr_quoted(p);
        p->idx++;

        Logic_Operand lhs = { .token = lhs_tok, .quoted = lhs_quoted };
        Logic_Operand rhs = { .token = rhs_tok, .quoted = rhs_quoted };
        return logic_compare(arena, cmp, lhs, rhs);
    }

    return logic_bool(arena, lhs_tok, lhs_quoted);
}

static Logic_Node *logic_parse_not_expr(Logic_Parser *p) {
    if (logic_has_token(p) && logic_sv_eq_ci_lit(logic_curr_token(p), "NOT")) {
        p->idx++;
        return logic_not(p->in->arena, logic_parse_not_expr(p));
    }
    return logic_parse_atom_expr(p);
}

static Logic_Node *logic_parse_and_expr(Logic_Parser *p) {
    Logic_Node *lhs = logic_parse_not_expr(p);
    while (logic_has_token(p) && logic_sv_eq_ci_lit(logic_curr_token(p), "AND")) {
        p->idx++;
        Logic_Node *rhs = logic_parse_not_expr(p);
        lhs = logic_and(p->in->arena, lhs, rhs);
    }
    return lhs;
}

static Logic_Node *logic_parse_or_expr(Logic_Parser *p) {
    Logic_Node *lhs = logic_parse_and_expr(p);
    while (logic_has_token(p) && logic_sv_eq_ci_lit(logic_curr_token(p), "OR")) {
        p->idx++;
        Logic_Node *rhs = logic_parse_and_expr(p);
        lhs = logic_or(p->in->arena, lhs, rhs);
    }
    return lhs;
}

Logic_Node *logic_parse_expression(const Logic_Parse_Input *in, size_t *out_consumed) {
    if (out_consumed) *out_consumed = 0;
    if (!in || !in->arena || !in->tokens || in->count == 0) return NULL;

    Logic_Parser parser = {0};
    parser.in = in;
    parser.idx = 0;
    parser.ok = true;

    Logic_Node *node = logic_parse_or_expr(&parser);
    if (!node || !parser.ok) return NULL;

    if (out_consumed) *out_consumed = parser.idx;
    return node;
}

static String_View logic_get_var(const Logic_Eval_Context *ctx, String_View name, bool *is_set) {
    if (is_set) *is_set = false;
    if (!ctx || !ctx->get_var || name.count == 0) return sv_from_cstr("");
    return ctx->get_var(ctx->userdata, name, is_set);
}

static String_View logic_resolve_operand(const Logic_Eval_Context *ctx, Logic_Operand operand, bool comparison_operand) {
    if (operand.quoted) return operand.token;

    bool is_set = false;
    String_View var_value = logic_get_var(ctx, operand.token, &is_set);
    if (is_set) return var_value;

    if (logic_sv_is_i64_literal(operand.token)) return operand.token;
    if (logic_sv_eq_ci_lit(operand.token, "TRUE") ||
        logic_sv_eq_ci_lit(operand.token, "FALSE") ||
        logic_sv_eq_ci_lit(operand.token, "ON") ||
        logic_sv_eq_ci_lit(operand.token, "OFF") ||
        logic_sv_eq_ci_lit(operand.token, "YES") ||
        logic_sv_eq_ci_lit(operand.token, "NO") ||
        logic_sv_eq_ci_lit(operand.token, "Y") ||
        logic_sv_eq_ci_lit(operand.token, "N") ||
        logic_sv_eq_ci_lit(operand.token, "IGNORE") ||
        logic_sv_eq_ci_lit(operand.token, "NOTFOUND") ||
        logic_sv_ends_with_ci(operand.token, sv_from_cstr("-NOTFOUND"))) {
        return operand.token;
    }

    if (comparison_operand) return operand.token;
    return sv_from_cstr("");
}

static bool logic_evaluate_comparison(Logic_Comparator op, String_View lhs, String_View rhs) {
    if (op == LOGIC_CMP_STREQUAL) {
        return nob_sv_eq(lhs, rhs);
    }

    if (op == LOGIC_CMP_EQUAL || op == LOGIC_CMP_LESS || op == LOGIC_CMP_GREATER ||
        op == LOGIC_CMP_LESS_EQUAL || op == LOGIC_CMP_GREATER_EQUAL) {
        long long li = 0;
        long long ri = 0;
        bool ln = logic_parse_i64_sv(lhs, &li);
        bool rn = logic_parse_i64_sv(rhs, &ri);
        if (ln && rn) {
            if (op == LOGIC_CMP_EQUAL) return li == ri;
            if (op == LOGIC_CMP_LESS) return li < ri;
            if (op == LOGIC_CMP_GREATER) return li > ri;
            if (op == LOGIC_CMP_LESS_EQUAL) return li <= ri;
            return li >= ri;
        }

        int cmp = strcmp(nob_temp_sv_to_cstr(lhs), nob_temp_sv_to_cstr(rhs));
        if (op == LOGIC_CMP_EQUAL) return cmp == 0;
        if (op == LOGIC_CMP_LESS) return cmp < 0;
        if (op == LOGIC_CMP_GREATER) return cmp > 0;
        if (op == LOGIC_CMP_LESS_EQUAL) return cmp <= 0;
        return cmp >= 0;
    }

    int vcmp = logic_compare_versions_sv(lhs, rhs);
    if (op == LOGIC_CMP_VERSION_LESS) return vcmp < 0;
    if (op == LOGIC_CMP_VERSION_GREATER) return vcmp > 0;
    if (op == LOGIC_CMP_VERSION_EQUAL) return vcmp == 0;
    if (op == LOGIC_CMP_VERSION_LESS_EQUAL) return vcmp <= 0;
    return vcmp >= 0;
}

bool logic_evaluate(const Logic_Node *node, const Logic_Eval_Context *ctx) {
    if (!node) return true;

    switch (node->type) {
    case LOGIC_OP_LITERAL_TRUE:
        return true;
    case LOGIC_OP_LITERAL_FALSE:
        return false;
    case LOGIC_OP_NOT:
        return !logic_evaluate(node->left, ctx);
    case LOGIC_OP_AND:
        return logic_evaluate(node->left, ctx) && logic_evaluate(node->right, ctx);
    case LOGIC_OP_OR:
        return logic_evaluate(node->left, ctx) || logic_evaluate(node->right, ctx);
    case LOGIC_OP_BOOL: {
        String_View value = logic_resolve_operand(ctx, node->as.operand, false);
        return !logic_cmake_string_is_false(value);
    }
    case LOGIC_OP_DEFINED: {
        bool is_set = false;
        (void)logic_get_var(ctx, node->as.operand.token, &is_set);
        return is_set;
    }
    case LOGIC_OP_COMPARE: {
        String_View lhs = logic_resolve_operand(ctx, node->as.cmp.lhs, true);
        String_View rhs = logic_resolve_operand(ctx, node->as.cmp.rhs, true);
        return logic_evaluate_comparison(node->as.cmp.op, lhs, rhs);
    }
    default:
        return false;
    }
}

bool logic_parse_and_evaluate(const Logic_Parse_Input *in, const Logic_Eval_Context *ctx, bool *out_ok) {
    if (out_ok) *out_ok = false;
    if (!in || !ctx || !in->arena || !in->tokens || in->count == 0) return false;

    size_t consumed = 0;
    Logic_Node *root = logic_parse_expression(in, &consumed);
    if (!root) return false;
    if (consumed == 0) return false;
    if (out_ok) *out_ok = true;
    return logic_evaluate(root, ctx);
}
