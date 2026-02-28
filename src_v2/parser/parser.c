// --- START OF FILE parser.c ---
#include "parser.h"
#include "nob.h"
#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>

// ============================================================================
// Infra interna de parsing
// ============================================================================

typedef struct {
    Arena *arena;
    bool oom;
    size_t fail_after_appends;
    size_t block_depth;
    size_t max_block_depth;
    size_t max_paren_depth;
} Parser_Context;

typedef enum {
    ARGS_COMMAND_DEFAULT = 0,
    ARGS_CONDITION_EXPR
} Parse_Args_Mode;

// --- Forward Declarations ---
static Node parse_statement(Parser_Context *ctx, Token_List *tokens, size_t *cursor);
static Node_List parse_block(Parser_Context *ctx, Token_List *tokens, size_t *cursor, const char **terminators, size_t term_count, String_View *found_terminator);

#define PARSER_DEFAULT_MAX_BLOCK_DEPTH 512
#define PARSER_DEFAULT_MAX_PAREN_DEPTH 2048

static inline bool parser_has_oom(const Parser_Context *ctx) {
    return !ctx || ctx->oom;
}

#define PARSER_OOM_RETURN(ctx, ret_value) \
    do { if (parser_has_oom((ctx))) return (ret_value); } while (0)

static size_t parser_fail_after_from_env(void) {
    const char *env = getenv("CMK2NOB_PARSER_FAIL_APPEND_AFTER");
    if (!env || env[0] == '\0') return SIZE_MAX;
    char *end = NULL;
    unsigned long long raw = strtoull(env, &end, 10);
    if (end == env || (end && *end != '\0')) return SIZE_MAX;
    if (raw > (unsigned long long)SIZE_MAX) return SIZE_MAX;
    return (size_t)raw;
}

static size_t parser_limit_from_env(const char *name, size_t fallback, size_t min_value) {
    const char *env = getenv(name);
    if (!env || env[0] == '\0') return fallback;
    char *end = NULL;
    unsigned long long raw = strtoull(env, &end, 10);
    if (end == env || (end && *end != '\0')) return fallback;
    if (raw > (unsigned long long)SIZE_MAX) return fallback;
    if ((size_t)raw < min_value) return min_value;
    return (size_t)raw;
}

static bool parser_report_oom(Parser_Context *ctx) {
    if (!ctx) return false;
    if (!ctx->oom) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, "internal",
            "falha de alocacao ao expandir array", "reduza o arquivo de entrada ou aumente memoria");
    }
    ctx->oom = true;
    return false;
}

static bool parser_consume_append_budget(Parser_Context *ctx) {
    if (!ctx) return false;
    if (ctx->fail_after_appends == SIZE_MAX) return true;
    if (ctx->fail_after_appends == 0) return false;
    ctx->fail_after_appends--;
    return true;
}

static bool parser_append_token(Parser_Context *ctx, Arg *arg, Token tok) {
    if (!ctx || !arg) return false;
    if (!parser_consume_append_budget(ctx)) return parser_report_oom(ctx);
    if (!arena_da_try_append(ctx->arena, arg, tok)) return parser_report_oom(ctx);
    return true;
}

static bool parser_append_arg(Parser_Context *ctx, Args *args, Arg arg) {
    if (!ctx || !args) return false;
    if (!parser_consume_append_budget(ctx)) return parser_report_oom(ctx);
    if (!arena_da_try_append(ctx->arena, args, arg)) return parser_report_oom(ctx);
    return true;
}

static bool parser_append_node(Parser_Context *ctx, Node_List *list, Node node) {
    if (!ctx || !list) return false;
    if (!parser_consume_append_budget(ctx)) return parser_report_oom(ctx);
    if (!arena_da_try_append(ctx->arena, list, node)) return parser_report_oom(ctx);
    return true;
}

static bool parser_append_elseif(Parser_Context *ctx, ElseIf_Clause_List *list, ElseIf_Clause clause) {
    if (!ctx || !list) return false;
    if (!parser_consume_append_budget(ctx)) return parser_report_oom(ctx);
    if (!arena_da_try_append(ctx->arena, list, clause)) return parser_report_oom(ctx);
    return true;
}

static unsigned char ascii_upper(unsigned char c) {
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - ('a' - 'A'));
    return c;
}

static void parser_emit_unexpected_token(Token t, const char *hint) {
    diag_log(DIAG_SEV_ERROR, "parser", "<input>", t.line, t.col, "token",
        nob_temp_sprintf("token inesperado no nivel de bloco: "SV_Fmt, SV_Arg(t.text)),
        hint ? hint : "remova token inesperado");
}

static void parser_sync_after_statement_error(Token_List *tokens, size_t *cursor, size_t start_line) {
    if (!tokens || !cursor) return;
    while (*cursor < tokens->count) {
        Token t = tokens->items[*cursor];
        if (t.line != start_line) break;
        (*cursor)++;
    }
}

static Node parser_make_empty_statement(size_t line, size_t col) {
    Node node = {0};
    node.kind = NODE_COMMAND;
    node.line = line;
    node.col = col;
    return node;
}

static bool match_token_text(Token t, const char *text) {
    if (t.kind != TOKEN_IDENTIFIER) return false;
    String_View sv = nob_sv_from_cstr(text);
    if (t.text.count != sv.count) return false;
    for (size_t i = 0; i < sv.count; i++) {
        unsigned char a = (unsigned char)t.text.data[i];
        unsigned char b = (unsigned char)sv.data[i];
        if (ascii_upper(a) != ascii_upper(b)) return false;
    }
    return true;
}

static bool sv_eq_ci_lit(String_View value, const char *lit) {
    String_View sv = nob_sv_from_cstr(lit);
    if (value.count != sv.count) return false;
    for (size_t i = 0; i < sv.count; i++) {
        unsigned char a = (unsigned char)value.data[i];
        unsigned char b = (unsigned char)sv.data[i];
        if (ascii_upper(a) != ascii_upper(b)) return false;
    }
    return true;
}

static bool sv_eq_ci_sv(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (ascii_upper((unsigned char)a.data[i]) != ascii_upper((unsigned char)b.data[i])) return false;
    }
    return true;
}

static String_View parser_first_arg_text(const Args *args) {
    if (!args || args->count == 0) return nob_sv_from_cstr("");
    if (args->items[0].count == 0) return nob_sv_from_cstr("");
    return args->items[0].items[0].text;
}

static void parser_warn_end_mismatch(size_t line, size_t col, const char *kw, String_View expected, String_View got) {
    diag_log(DIAG_SEV_WARNING, "parser", "<input>", line, col, kw,
             nob_temp_sprintf("%s() assinatura de fechamento divergente", kw),
             nob_temp_sprintf("esperado: "SV_Fmt" ; recebido: "SV_Fmt, SV_Arg(expected), SV_Arg(got)));
}

static bool is_cmake_bracket_literal(String_View sv) {
    if (sv.count < 4 || !sv.data) return false;
    if (sv.data[0] != '[') return false;

    size_t i = 1;
    while (i < sv.count && sv.data[i] == '=') i++;
    if (i >= sv.count || sv.data[i] != '[') return false;
    size_t open_len = i + 1;
    size_t eq_count = open_len - 2;

    if (sv.count < open_len + eq_count + 2) return false;
    size_t close_pos = sv.count - (eq_count + 2);
    if (sv.data[close_pos] != ']') return false;
    for (size_t k = 0; k < eq_count; k++) {
        if (sv.data[close_pos + 1 + k] != '=') return false;
    }
    return sv.data[sv.count - 1] == ']';
}

static bool parser_append_new_arg_with_token(Parser_Context *ctx, Args *args, Token tok, Arg_Kind kind) {
    Arg arg = {0};
    arg.kind = kind;
    if (!parser_append_token(ctx, &arg, tok)) return false;
    if (!parser_append_arg(ctx, args, arg)) return false;
    return true;
}

static bool parser_append_to_last_or_new(Parser_Context *ctx, Args *args, Token tok, Arg_Kind kind) {
    if (!ctx || !args) return false;
    if (args->count > 0 && !tok.has_space_left) {
        if (!parser_append_token(ctx, &args->items[args->count - 1], tok)) return false;
        return true;
    }
    return parser_append_new_arg_with_token(ctx, args, tok, kind);
}

static Arg_Kind parser_infer_arg_kind(Token tok);

static bool parser_append_with_condition_merge_rules(Parser_Context *ctx, Args *args, Token tok) {
    if (!ctx || !args) return false;
    bool can_merge = (args->count > 0 && !tok.has_space_left);
    if (can_merge) {
        Arg *last = &args->items[args->count - 1];
        if (last->count > 0) {
            Token prev = last->items[last->count - 1];
            if (prev.kind == TOKEN_LPAREN || prev.kind == TOKEN_RPAREN) {
                can_merge = false;
            }
        }
    }

    if (can_merge) {
        if (!parser_append_token(ctx, &args->items[args->count - 1], tok)) return false;
        return true;
    }
    return parser_append_new_arg_with_token(ctx, args, tok, parser_infer_arg_kind(tok));
}

static Arg_Kind parser_infer_arg_kind(Token tok) {
    if (tok.kind == TOKEN_STRING) return ARG_QUOTED;
    if (tok.kind == TOKEN_RAW_STRING || is_cmake_bracket_literal(tok.text)) return ARG_BRACKET;
    return ARG_UNQUOTED;
}

// Consome argumentos entre '(' e ')'
static Args parse_args(Parser_Context *ctx,
                      Token_List *tokens,
                      size_t *cursor,
                      Parse_Args_Mode mode,
                      size_t origin_line,
                      size_t origin_col,
                      bool *out_ok) {
    Args args = {0};
    bool ok = true;
    if (out_ok) *out_ok = true;
    PARSER_OOM_RETURN(ctx, args);
    bool closed = false;
    size_t depth = 0;
    size_t overflow_extra = 0;
    bool reported_paren_depth = false;

    if (*cursor >= tokens->count || tokens->items[*cursor].kind != TOKEN_LPAREN) {
        ok = false;
        if (out_ok) *out_ok = ok;
        return args;
    }
    (*cursor)++; // Pula '('
    depth = 1;

    while (*cursor < tokens->count) {
        Token t = tokens->items[*cursor];

        if (overflow_extra > 0) {
            if (t.kind == TOKEN_LPAREN) overflow_extra++;
            if (t.kind == TOKEN_RPAREN) overflow_extra--;
            (*cursor)++;
            continue;
        }

        if (t.kind == TOKEN_LPAREN) {
            if (depth >= ctx->max_paren_depth) {
                if (!reported_paren_depth) {
                    diag_log(DIAG_SEV_ERROR, "parser", "<input>", t.line, t.col, "args",
                        "profundidade maxima de parenteses excedida",
                        "reduza aninhamento ou ajuste CMK2NOB_PARSER_MAX_PAREN_DEPTH");
                    reported_paren_depth = true;
                }
                ok = false;
                overflow_extra = 1;
                (*cursor)++;
                continue;
            }
            depth++;
            if (mode == ARGS_CONDITION_EXPR) {
                if (!parser_append_new_arg_with_token(ctx, &args, t, ARG_UNQUOTED)) return (Args){0};
            } else {
                if (!parser_append_to_last_or_new(ctx, &args, t, ARG_UNQUOTED)) return (Args){0};
            }
            (*cursor)++;
            continue;
        }

        if (t.kind == TOKEN_RPAREN) {
            if (depth == 1) {
                (*cursor)++; // Consome ')' que fecha o comando
                closed = true;
                break;
            }
            depth--;
            if (mode == ARGS_CONDITION_EXPR) {
                if (!parser_append_new_arg_with_token(ctx, &args, t, ARG_UNQUOTED)) return (Args){0};
            } else {
                if (!parser_append_to_last_or_new(ctx, &args, t, ARG_UNQUOTED)) return (Args){0};
            }
            (*cursor)++;
            continue;
        }

        if (t.kind == TOKEN_SEMICOLON && depth == 1) {
            (*cursor)++; // Ignora ';' solto
            continue;
        }

        if (mode == ARGS_CONDITION_EXPR) {
            if (!parser_append_with_condition_merge_rules(ctx, &args, t)) return (Args){0};
        } else {
            if (!parser_append_to_last_or_new(ctx, &args, t, parser_infer_arg_kind(t))) return (Args){0};
        }
        (*cursor)++;
    }

    if (!closed && *cursor >= tokens->count) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", origin_line, origin_col, "args",
            "argumento nao fechado, esperado ')'", "feche parenteses no comando atual");
        ok = false;
    }
    if (out_ok) *out_ok = ok;
    return args;
}

static bool is_valid_command_name(String_View name) {
    if (name.count == 0) return false;
    unsigned char c0 = (unsigned char)name.data[0];
    if (!(isalpha(c0) || c0 == '_')) return false;
    for (size_t i = 1; i < name.count; i++) {
        unsigned char c = (unsigned char)name.data[i];
        if (!(isalnum(c) || c == '_')) return false;
    }
    return true;
}

// --- Block Parsers ---

static Node parse_if(Parser_Context *ctx, Token_List *tokens, size_t *cursor, size_t line, size_t col) {
    Node node = {0};
    PARSER_OOM_RETURN(ctx, node);
    node.kind = NODE_IF;
    node.line = line;
    node.col = col;

    bool cond_ok = true;
    node.as.if_stmt.condition = parse_args(ctx, tokens, cursor, ARGS_CONDITION_EXPR, line, col, &cond_ok);
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (!cond_ok) return parser_make_empty_statement(line, col);

    const char *terminators[] = {"else", "elseif", "endif"};
    String_View found_term = {0};

    node.as.if_stmt.then_block = parse_block(ctx, tokens, cursor, terminators, 3, &found_term);
    PARSER_OOM_RETURN(ctx, (Node){0});

    while (sv_eq_ci_lit(found_term, "elseif")) {
        ElseIf_Clause clause = {0};
        bool elseif_ok = true;
        clause.condition = parse_args(ctx, tokens, cursor, ARGS_CONDITION_EXPR, line, col, &elseif_ok);
        PARSER_OOM_RETURN(ctx, (Node){0});
        if (!elseif_ok) return parser_make_empty_statement(line, col);

        String_View elseif_term = {0};
        clause.block = parse_block(ctx, tokens, cursor, terminators, 3, &elseif_term);
        PARSER_OOM_RETURN(ctx, (Node){0});

        if (!parser_append_elseif(ctx, &node.as.if_stmt.elseif_clauses, clause)) return (Node){0};
        found_term = elseif_term;
    }

    if (sv_eq_ci_lit(found_term, "else")) {
        bool else_args_ok = true;
        Args else_args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, line, col, &else_args_ok);
        (void)else_args;
        PARSER_OOM_RETURN(ctx, (Node){0});
        if (!else_args_ok) return parser_make_empty_statement(line, col);

        const char *else_terms[] = {"endif"};
        String_View end_term = {0};
        node.as.if_stmt.else_block = parse_block(ctx, tokens, cursor, else_terms, 1, &end_term);
        PARSER_OOM_RETURN(ctx, (Node){0});
        if (!sv_eq_ci_lit(end_term, "endif")) {
            diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "if",
                "if sem terminador endif()", "adicione endif() ao bloco");
            return parser_make_empty_statement(line, col);
        } else if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
            bool end_args_ok = true;
            Args end_args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, line, col, &end_args_ok);
            PARSER_OOM_RETURN(ctx, (Node){0});
            if (!end_args_ok) return parser_make_empty_statement(line, col);
            String_View expected = parser_first_arg_text(&node.as.if_stmt.condition);
            String_View got = parser_first_arg_text(&end_args);
            if (expected.count > 0 && got.count > 0 && !sv_eq_ci_sv(expected, got)) {
                parser_warn_end_mismatch(line, col, "endif", expected, got);
            }
        }
    } else if (sv_eq_ci_lit(found_term, "endif")) {
        if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
            bool end_args_ok = true;
            Args end_args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, line, col, &end_args_ok);
            PARSER_OOM_RETURN(ctx, (Node){0});
            if (!end_args_ok) return parser_make_empty_statement(line, col);
            String_View expected = parser_first_arg_text(&node.as.if_stmt.condition);
            String_View got = parser_first_arg_text(&end_args);
            if (expected.count > 0 && got.count > 0 && !sv_eq_ci_sv(expected, got)) {
                parser_warn_end_mismatch(line, col, "endif", expected, got);
            }
        }
    } else {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "if",
            "if sem terminador endif()", "adicione endif() ao bloco");
        return parser_make_empty_statement(line, col);
    }
    return node;
}

static Node parse_foreach(Parser_Context *ctx, Token_List *tokens, size_t *cursor, size_t line, size_t col) {
    Node node = {0};
    PARSER_OOM_RETURN(ctx, node);
    node.kind = NODE_FOREACH;
    node.line = line;
    node.col = col;
    
    bool args_ok = true;
    node.as.foreach_stmt.args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, line, col, &args_ok);
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (!args_ok) return parser_make_empty_statement(line, col);

    const char *terminators[] = {"endforeach"};
    String_View found_term = {0};
    node.as.foreach_stmt.body = parse_block(ctx, tokens, cursor, terminators, 1, &found_term);
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (!sv_eq_ci_lit(found_term, "endforeach")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "foreach",
            "foreach sem terminador endforeach()", "adicione endforeach() ao bloco");
        return parser_make_empty_statement(line, col);
    }

    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        bool end_args_ok = true;
        Args end_args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, line, col, &end_args_ok);
        PARSER_OOM_RETURN(ctx, (Node){0});
        if (!end_args_ok) return parser_make_empty_statement(line, col);
        String_View var_name = parser_first_arg_text(&node.as.foreach_stmt.args);
        String_View got = parser_first_arg_text(&end_args);
        if (var_name.count > 0 && got.count > 0 && !sv_eq_ci_sv(var_name, got)) {
            parser_warn_end_mismatch(line, col, "endforeach", var_name, got);
        }
    }
    return node;
}

static Node parse_while(Parser_Context *ctx, Token_List *tokens, size_t *cursor, size_t line, size_t col) {
    Node node = {0};
    PARSER_OOM_RETURN(ctx, node);
    node.kind = NODE_WHILE;
    node.line = line;
    node.col = col;

    bool cond_ok = true;
    node.as.while_stmt.condition = parse_args(ctx, tokens, cursor, ARGS_CONDITION_EXPR, line, col, &cond_ok);
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (!cond_ok) return parser_make_empty_statement(line, col);

    const char *terminators[] = {"endwhile"};
    String_View found_term = {0};
    node.as.while_stmt.body = parse_block(ctx, tokens, cursor, terminators, 1, &found_term);
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (!sv_eq_ci_lit(found_term, "endwhile")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "while",
            "while sem terminador endwhile()", "adicione endwhile() ao bloco");
        return parser_make_empty_statement(line, col);
    }

    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        bool end_args_ok = true;
        Args end_args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, line, col, &end_args_ok);
        PARSER_OOM_RETURN(ctx, (Node){0});
        if (!end_args_ok) return parser_make_empty_statement(line, col);
        String_View expected = parser_first_arg_text(&node.as.while_stmt.condition);
        String_View got = parser_first_arg_text(&end_args);
        if (expected.count > 0 && got.count > 0 && !sv_eq_ci_sv(expected, got)) {
            parser_warn_end_mismatch(line, col, "endwhile", expected, got);
        }
    }
    return node;
}

static Node parse_function_macro(Parser_Context *ctx, Token_List *tokens, size_t *cursor, bool is_macro, size_t line, size_t col) {
    Node node = {0};
    PARSER_OOM_RETURN(ctx, node);
    node.kind = is_macro ? NODE_MACRO : NODE_FUNCTION;
    node.line = line;
    node.col = col;
    
    bool def_args_ok = true;
    Args all_args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, line, col, &def_args_ok);
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (!def_args_ok) return parser_make_empty_statement(line, col);
    
    if (all_args.count > 0) {
        if (all_args.items[0].count > 0) {
            node.as.func_def.name = all_args.items[0].items[0].text;
        }
        node.as.func_def.params.items = all_args.items + 1;
        node.as.func_def.params.count = all_args.count - 1;
        node.as.func_def.params.capacity = all_args.count - 1;
    }

    const char *terms[] = { is_macro ? "endmacro" : "endfunction" };
    String_View found_term = {0};
    node.as.func_def.body = parse_block(ctx, tokens, cursor, terms, 1, &found_term);
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (!sv_eq_ci_lit(found_term, is_macro ? "endmacro" : "endfunction")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, is_macro ? "macro" : "function",
            "bloco sem terminador", is_macro ? "adicione endmacro()" : "adicione endfunction()");
        return parser_make_empty_statement(line, col);
    }

    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        bool end_args_ok = true;
        Args end_args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, line, col, &end_args_ok);
        PARSER_OOM_RETURN(ctx, (Node){0});
        if (!end_args_ok) return parser_make_empty_statement(line, col);
        String_View got = parser_first_arg_text(&end_args);
        if (node.as.func_def.name.count > 0 && got.count > 0 && !sv_eq_ci_sv(node.as.func_def.name, got)) {
            parser_warn_end_mismatch(line, col, is_macro ? "endmacro" : "endfunction", node.as.func_def.name, got);
        }
    }
    return node;
}

// --- Generic Block Parser ---

static Node_List parse_block(Parser_Context *ctx, Token_List *tokens, size_t *cursor, const char **terminators, size_t term_count, String_View *found_terminator) {
    Node_List list = {0};
    PARSER_OOM_RETURN(ctx, (Node_List){0});
    if (ctx->block_depth >= ctx->max_block_depth) {
        size_t line = 0;
        size_t col = 0;
        if (*cursor < tokens->count) {
            line = tokens->items[*cursor].line;
            col = tokens->items[*cursor].col;
        }
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "depth",
            "profundidade maxima de blocos excedida",
            "reduza aninhamento ou ajuste CMK2NOB_PARSER_MAX_BLOCK_DEPTH");
        while (*cursor < tokens->count) {
            Token t = tokens->items[*cursor];
            if (t.kind == TOKEN_IDENTIFIER && terminators != NULL) {
                for (size_t i = 0; i < term_count; ++i) {
                    if (match_token_text(t, terminators[i])) {
                        if (found_terminator) *found_terminator = t.text;
                        (*cursor)++;
                        return list;
                    }
                }
            }
            (*cursor)++;
        }
        return list;
    }
    ctx->block_depth++;

    while (*cursor < tokens->count) {
        Token t = tokens->items[*cursor];

        if (t.kind == TOKEN_LPAREN || t.kind == TOKEN_RPAREN) {
            parser_emit_unexpected_token(t, "parenteses no nivel de bloco exigem um comando valido");
            (*cursor)++;
            continue;
        }

        if (t.kind == TOKEN_IDENTIFIER && terminators != NULL) {
            for (size_t i = 0; i < term_count; ++i) {
                if (match_token_text(t, terminators[i])) {
                    if (found_terminator) *found_terminator = t.text;
                    (*cursor)++;
                    ctx->block_depth--;
                    return list;
                }
            }
        }

        Node node = parse_statement(ctx, tokens, cursor);
        if (ctx->oom) {
            ctx->block_depth--;
            return (Node_List){0};
        }
        if (node.kind != NODE_COMMAND || node.as.cmd.name.count > 0) {
            if (!parser_append_node(ctx, &list, node)) {
                ctx->block_depth--;
                return (Node_List){0};
            }
        }
    }

    ctx->block_depth--;
    return list;
}

// --- Dispatcher Principal ---

static Node parse_statement(Parser_Context *ctx, Token_List *tokens, size_t *cursor) {
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (*cursor >= tokens->count) {
        return parser_make_empty_statement(0, 0);
    }
    
    Token t = tokens->items[*cursor];
    
    if (t.kind != TOKEN_IDENTIFIER) {
        Node node = parser_make_empty_statement(t.line, t.col);
        if (t.kind == TOKEN_INVALID) {
            diag_log(DIAG_SEV_ERROR, "parser", "<input>", t.line, t.col, "token",
                nob_temp_sprintf("token invalido: "SV_Fmt, SV_Arg(t.text)),
                "revise caractere/token na posicao informada");
        } else {
            diag_log(DIAG_SEV_ERROR, "parser", "<input>", t.line, t.col, "token",
                nob_temp_sprintf("token inesperado no inicio de statement: "SV_Fmt, SV_Arg(t.text)),
                "inicie statement com nome_de_comando(...)");
        }
        parser_sync_after_statement_error(tokens, cursor, t.line);
        return node;
    }
    
    (*cursor)++; // Consome o nome do comando

    if (!is_valid_command_name(t.text)) {
        Node node = parser_make_empty_statement(t.line, t.col);
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", t.line, t.col, "command",
            nob_temp_sprintf("nome de comando invalido: "SV_Fmt, SV_Arg(t.text)),
            "use [A-Za-z_][A-Za-z0-9_]* para nome de comando");
        parser_sync_after_statement_error(tokens, cursor, t.line);
        return node;
    }

    if (*cursor >= tokens->count || tokens->items[*cursor].kind != TOKEN_LPAREN) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", t.line, t.col, "command",
            nob_temp_sprintf("comando sem parenteses: "SV_Fmt, SV_Arg(t.text)),
            "use sintaxe nome_do_comando(...)");
        parser_sync_after_statement_error(tokens, cursor, t.line);
        return parser_make_empty_statement(t.line, t.col);
    }

    // Passa a origem (linha e coluna) para os sub-parsers
    if (match_token_text(t, "if")) return parse_if(ctx, tokens, cursor, t.line, t.col);
    if (match_token_text(t, "foreach")) return parse_foreach(ctx, tokens, cursor, t.line, t.col);
    if (match_token_text(t, "while")) return parse_while(ctx, tokens, cursor, t.line, t.col);
    if (match_token_text(t, "function")) return parse_function_macro(ctx, tokens, cursor, false, t.line, t.col);
    if (match_token_text(t, "macro")) return parse_function_macro(ctx, tokens, cursor, true, t.line, t.col);

    // Comando Comum (set, project, etc)
    Node node = {0};
    node.kind = NODE_COMMAND;
    node.line = t.line; // Origem Salva!
    node.col = t.col;
    node.as.cmd.name = t.text;
    bool args_ok = true;
    node.as.cmd.args = parse_args(ctx, tokens, cursor, ARGS_COMMAND_DEFAULT, t.line, t.col, &args_ok);
    PARSER_OOM_RETURN(ctx, (Node){0});
    if (!args_ok) return parser_make_empty_statement(t.line, t.col);
    return node;
}

// --- Public Interface ---

Ast_Root parse_tokens(Arena *arena, Token_List tokens) {
    if (!arena) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, "parse_tokens",
            "arena obrigatoria ausente",
            "inicialize Arena e passe ponteiro valido para parse_tokens");
        Ast_Root empty = {0};
        return empty;
    }
    size_t cursor = 0;
    Parser_Context ctx = {0};
    ctx.arena = arena;
    ctx.oom = false;
    ctx.fail_after_appends = parser_fail_after_from_env();
    ctx.block_depth = 0;
    ctx.max_block_depth = parser_limit_from_env("CMK2NOB_PARSER_MAX_BLOCK_DEPTH", PARSER_DEFAULT_MAX_BLOCK_DEPTH, 1);
    ctx.max_paren_depth = parser_limit_from_env("CMK2NOB_PARSER_MAX_PAREN_DEPTH", PARSER_DEFAULT_MAX_PAREN_DEPTH, 1);

    Ast_Root root = parse_block(&ctx, &tokens, &cursor, NULL, 0, NULL);
    if (ctx.oom) return (Ast_Root){0};
    return root;
}

void ast_free(Ast_Root root) {
    (void)root;
    // AST memory is owned by the arena passed to parse_tokens().
}

// AST pretty-print helper
void print_ast_list(Node_List list, int indent);

void print_indent(int indent) {
    for(int i=0; i<indent; ++i) printf("  ");
}

void print_node(Node *node, int indent) {
    print_indent(indent);
    switch (node->kind) {
        case NODE_COMMAND:
            printf("\x1b[36mCMD\x1b[0m "SV_Fmt" (L:%zu)\n", SV_Arg(node->as.cmd.name), node->line);
            // Mostrar os args de forma compacta para debug
            for(size_t i=0; i < node->as.cmd.args.count; i++) {
                print_indent(indent + 1);
                Arg a = node->as.cmd.args.items[i];
                const char* k = (a.kind == ARG_QUOTED) ? "QUOTED" : (a.kind == ARG_BRACKET ? "BRACKET" : "UNQUOTED");
                printf("- [%s] ", k);
                for(size_t j=0; j < a.count; j++) {
                    printf(SV_Fmt, SV_Arg(a.items[j].text));
                }
                printf("\n");
            }
            break;
        case NODE_IF:
            printf("\x1b[35mIF\x1b[0m (L:%zu)\n", node->line);
            print_ast_list(node->as.if_stmt.then_block, indent + 1);
            for (size_t i = 0; i < node->as.if_stmt.elseif_clauses.count; i++) {
                print_indent(indent);
                printf("\x1b[35mELSEIF\x1b[0m\n");
                print_ast_list(node->as.if_stmt.elseif_clauses.items[i].block, indent + 1);
            }
            if (node->as.if_stmt.else_block.count > 0) {
                print_indent(indent);
                printf("\x1b[35mELSE\x1b[0m\n");
                print_ast_list(node->as.if_stmt.else_block, indent + 1);
            }
            break;
        case NODE_FOREACH:
            printf("\x1b[32mFOREACH\x1b[0m (L:%zu)\n", node->line);
            print_ast_list(node->as.foreach_stmt.body, indent + 1);
            break;
        case NODE_WHILE:
            printf("\x1b[32mWHILE\x1b[0m (L:%zu)\n", node->line);
            print_ast_list(node->as.while_stmt.body, indent + 1);
            break;
        case NODE_FUNCTION:
            printf("\x1b[33mFUNCTION\x1b[0m "SV_Fmt" (L:%zu)\n", SV_Arg(node->as.func_def.name), node->line);
            print_ast_list(node->as.func_def.body, indent + 1);
            break;
        case NODE_MACRO:
             printf("\x1b[33mMACRO\x1b[0m "SV_Fmt" (L:%zu)\n", SV_Arg(node->as.func_def.name), node->line);
             print_ast_list(node->as.func_def.body, indent + 1);
             break;
    }
}

void print_ast_list(Node_List list, int indent) {
    for (size_t i = 0; i < list.count; ++i) {
        print_node(&list.items[i], indent);
    }
}

void print_ast(Ast_Root root, int indent) {
    print_ast_list(root, indent);
}

