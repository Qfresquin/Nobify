// --- START OF FILE parser.c ---
#include "parser.h"
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
} Parser_Context;

// --- Forward Declarations ---
static Node parse_statement(Parser_Context *ctx, Token_List *tokens, size_t *cursor);
static Node_List parse_block(Parser_Context *ctx, Token_List *tokens, size_t *cursor, const char **terminators, size_t term_count, String_View *found_terminator);

static size_t parser_fail_after_from_env(void) {
    const char *env = getenv("CMK2NOB_PARSER_FAIL_APPEND_AFTER");
    if (!env || env[0] == '\0') return SIZE_MAX;
    char *end = NULL;
    unsigned long long raw = strtoull(env, &end, 10);
    if (end == env || (end && *end != '\0')) return SIZE_MAX;
    if (raw > (unsigned long long)SIZE_MAX) return SIZE_MAX;
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

static bool match_token_text(Token t, const char *text) {
    if (t.kind != TOKEN_IDENTIFIER) return false;
    String_View sv = nob_sv_from_cstr(text);
    if (t.text.count != sv.count) return false;
    for (size_t i = 0; i < sv.count; i++) {
        unsigned char a = (unsigned char)t.text.data[i];
        unsigned char b = (unsigned char)sv.data[i];
        if (toupper(a) != toupper(b)) return false;
    }
    return true;
}

static bool sv_eq_ci_lit(String_View value, const char *lit) {
    String_View sv = nob_sv_from_cstr(lit);
    if (value.count != sv.count) return false;
    for (size_t i = 0; i < sv.count; i++) {
        unsigned char a = (unsigned char)value.data[i];
        unsigned char b = (unsigned char)sv.data[i];
        if (toupper(a) != toupper(b)) return false;
    }
    return true;
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

// Consome argumentos entre '(' e ')'
static Args parse_args(Parser_Context *ctx, Token_List *tokens, size_t *cursor) {
    Args args = {0};
    if (!ctx || ctx->oom) return args;
    bool closed = false;
    int depth = 0;
    
    if (*cursor >= tokens->count || tokens->items[*cursor].kind != TOKEN_LPAREN) {
        return args;
    }
    (*cursor)++; // Pula '('
    depth = 1;

    while (*cursor < tokens->count) {
        Token t = tokens->items[*cursor];

        if (t.kind == TOKEN_LPAREN) {
            depth++;
            Arg paren_arg = {0};
            paren_arg.kind = ARG_UNQUOTED;
            if (!parser_append_token(ctx, &paren_arg, t)) return (Args){0};
            if (!parser_append_arg(ctx, &args, paren_arg)) return (Args){0};
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
            Arg paren_arg = {0};
            paren_arg.kind = ARG_UNQUOTED;
            if (!parser_append_token(ctx, &paren_arg, t)) return (Args){0};
            if (!parser_append_arg(ctx, &args, paren_arg)) return (Args){0};
            (*cursor)++;
            continue;
        }

        if (t.kind == TOKEN_SEMICOLON && depth == 1) {
            (*cursor)++; // Ignora ';' solto
            continue;
        }

        // Parse de um argumento
        Arg current_arg = {0};
        
        // --- Detecção de Quoting (Novo) ---
        // Se o seu lexer fornece um TOKEN_STRING específico, você pode usar:
        // if (t.kind == TOKEN_STRING) current_arg.kind = ARG_QUOTED;
        // Caso contrário, fazemos uma heurística rápida olhando o texto:
        if (t.text.count > 0 && t.text.data[0] == '"') {
            current_arg.kind = ARG_QUOTED;
        } else if (t.kind == TOKEN_RAW_STRING || is_cmake_bracket_literal(t.text)) {
            current_arg.kind = ARG_BRACKET;
        } else {
            current_arg.kind = ARG_UNQUOTED;
        }
        
        if (!parser_append_token(ctx, &current_arg, t)) return (Args){0};
        (*cursor)++;

        while (*cursor < tokens->count) {
            Token next = tokens->items[*cursor];
            if (next.kind == TOKEN_LPAREN || next.kind == TOKEN_RPAREN || next.kind == TOKEN_SEMICOLON) break;
            
            if (!next.has_space_left) {
                if (!parser_append_token(ctx, &current_arg, next)) return (Args){0};
                (*cursor)++;
            } else {
                break;
            }
        }
        if (!parser_append_arg(ctx, &args, current_arg)) return (Args){0};
    }
    if (!closed && *cursor >= tokens->count) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, "args",
            "argumento nao fechado, esperado ')'", "feche parenteses no comando atual");
    }
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

// --- Funções de Parsing de Blocos Específicos (Atualizadas para receber Origem) ---

static Node parse_if(Parser_Context *ctx, Token_List *tokens, size_t *cursor, size_t line, size_t col) {
    Node node = {0};
    if (!ctx || ctx->oom) return node;
    node.kind = NODE_IF;
    node.line = line;
    node.col = col;

    node.as.if_stmt.condition = parse_args(ctx, tokens, cursor);
    if (ctx->oom) return (Node){0};

    const char *terminators[] = {"else", "elseif", "endif"};
    String_View found_term = {0};

    node.as.if_stmt.then_block = parse_block(ctx, tokens, cursor, terminators, 3, &found_term);
    if (ctx->oom) return (Node){0};

    while (sv_eq_ci_lit(found_term, "elseif")) {
        ElseIf_Clause clause = {0};
        clause.condition = parse_args(ctx, tokens, cursor);
        if (ctx->oom) return (Node){0};

        String_View elseif_term = {0};
        clause.block = parse_block(ctx, tokens, cursor, terminators, 3, &elseif_term);
        if (ctx->oom) return (Node){0};

        if (!parser_append_elseif(ctx, &node.as.if_stmt.elseif_clauses, clause)) return (Node){0};
        found_term = elseif_term;
    }

    if (sv_eq_ci_lit(found_term, "else")) {
        Args else_args = parse_args(ctx, tokens, cursor);
        (void)else_args;
        if (ctx->oom) return (Node){0};

        const char *else_terms[] = {"endif"};
        String_View end_term = {0};
        node.as.if_stmt.else_block = parse_block(ctx, tokens, cursor, else_terms, 1, &end_term);
        if (ctx->oom) return (Node){0};
        if (!sv_eq_ci_lit(end_term, "endif")) {
            diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "if",
                "if sem terminador endif()", "adicione endif() ao bloco");
        }
    } else if (!sv_eq_ci_lit(found_term, "endif")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "if",
            "if sem terminador endif()", "adicione endif() ao bloco");
    }
    return node;
}

static Node parse_foreach(Parser_Context *ctx, Token_List *tokens, size_t *cursor, size_t line, size_t col) {
    Node node = {0};
    if (!ctx || ctx->oom) return node;
    node.kind = NODE_FOREACH;
    node.line = line;
    node.col = col;
    
    node.as.foreach_stmt.args = parse_args(ctx, tokens, cursor);
    if (ctx->oom) return (Node){0};

    const char *terminators[] = {"endforeach"};
    String_View found_term = {0};
    node.as.foreach_stmt.body = parse_block(ctx, tokens, cursor, terminators, 1, &found_term);
    if (ctx->oom) return (Node){0};
    if (!sv_eq_ci_lit(found_term, "endforeach")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "foreach",
            "foreach sem terminador endforeach()", "adicione endforeach() ao bloco");
    }

    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        Args ignore = parse_args(ctx, tokens, cursor); (void)ignore;
        if (ctx->oom) return (Node){0};
    }
    return node;
}

static Node parse_while(Parser_Context *ctx, Token_List *tokens, size_t *cursor, size_t line, size_t col) {
    Node node = {0};
    if (!ctx || ctx->oom) return node;
    node.kind = NODE_WHILE;
    node.line = line;
    node.col = col;

    node.as.while_stmt.condition = parse_args(ctx, tokens, cursor);
    if (ctx->oom) return (Node){0};

    const char *terminators[] = {"endwhile"};
    String_View found_term = {0};
    node.as.while_stmt.body = parse_block(ctx, tokens, cursor, terminators, 1, &found_term);
    if (ctx->oom) return (Node){0};
    if (!sv_eq_ci_lit(found_term, "endwhile")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, "while",
            "while sem terminador endwhile()", "adicione endwhile() ao bloco");
    }

    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        Args ignore = parse_args(ctx, tokens, cursor); (void)ignore;
        if (ctx->oom) return (Node){0};
    }
    return node;
}

static Node parse_function_macro(Parser_Context *ctx, Token_List *tokens, size_t *cursor, bool is_macro, size_t line, size_t col) {
    Node node = {0};
    if (!ctx || ctx->oom) return node;
    node.kind = is_macro ? NODE_MACRO : NODE_FUNCTION;
    node.line = line;
    node.col = col;
    
    Args all_args = parse_args(ctx, tokens, cursor);
    if (ctx->oom) return (Node){0};
    
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
    if (ctx->oom) return (Node){0};
    if (!sv_eq_ci_lit(found_term, is_macro ? "endmacro" : "endfunction")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", line, col, is_macro ? "macro" : "function",
            "bloco sem terminador", is_macro ? "adicione endmacro()" : "adicione endfunction()");
    }

    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        Args ignore = parse_args(ctx, tokens, cursor); (void)ignore;
        if (ctx->oom) return (Node){0};
    }
    return node;
}

// --- Parser Genérico de Bloco ---

static Node_List parse_block(Parser_Context *ctx, Token_List *tokens, size_t *cursor, const char **terminators, size_t term_count, String_View *found_terminator) {
    Node_List list = {0};
    if (!ctx || ctx->oom) return (Node_List){0};

    while (*cursor < tokens->count) {
        Token t = tokens->items[*cursor];

        if (t.kind == TOKEN_LPAREN || t.kind == TOKEN_RPAREN) {
            (*cursor)++;
            continue;
        }

        if (t.kind == TOKEN_IDENTIFIER && terminators != NULL) {
            for (size_t i = 0; i < term_count; ++i) {
                if (match_token_text(t, terminators[i])) {
                    if (found_terminator) *found_terminator = t.text;
                    (*cursor)++;
                    return list;
                }
            }
        }

        Node node = parse_statement(ctx, tokens, cursor);
        if (ctx->oom) return (Node_List){0};
        if (node.kind != NODE_COMMAND || node.as.cmd.name.count > 0) {
            if (!parser_append_node(ctx, &list, node)) return (Node_List){0};
        }
    }

    return list;
}

// --- Dispatcher Principal ---

static Node parse_statement(Parser_Context *ctx, Token_List *tokens, size_t *cursor) {
    if (!ctx || ctx->oom) return (Node){0};
    if (*cursor >= tokens->count) {
        Node node = {0};
        node.kind = NODE_COMMAND;
        return node;
    }
    
    Token t = tokens->items[*cursor];
    
    if (t.kind != TOKEN_IDENTIFIER) {
        Node node = {0};
        node.kind = NODE_COMMAND; 
        node.line = t.line; // Origem salva
        node.col = t.col;
        if (t.kind == TOKEN_INVALID) {
            diag_log(DIAG_SEV_ERROR, "parser", "<input>", t.line, t.col, "token",
                nob_temp_sprintf("token invalido: "SV_Fmt, SV_Arg(t.text)),
                "revise caractere/token na posicao informada");
        }
        (*cursor)++;
        return node;
    }
    
    (*cursor)++; // Consome o nome do comando

    if (!is_valid_command_name(t.text)) {
        Node node = {0};
        node.kind = NODE_COMMAND;
        node.line = t.line;
        node.col = t.col;
        return node;
    }

    if (*cursor >= tokens->count || tokens->items[*cursor].kind != TOKEN_LPAREN) {
        Node node = {0};
        node.kind = NODE_COMMAND; 
        node.line = t.line;
        node.col = t.col;
        return node;
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
    node.as.cmd.args = parse_args(ctx, tokens, cursor);
    if (ctx->oom) return (Node){0};
    return node;
}

// --- Interface Pública ---

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

    Ast_Root root = parse_block(&ctx, &tokens, &cursor, NULL, 0, NULL);
    if (ctx.oom) return (Ast_Root){0};
    return root;
}

// Impressão visual da árvore (melhorada com linha e quoting)
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
