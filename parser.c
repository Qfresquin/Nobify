#include "parser.h"
#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include <ctype.h>

// ============================================================================
// MACROS E UTILITÁRIOS
// ============================================================================

// Macro para append em listas dinâmicas usando a Arena.
// Substitui o nob_da_append padrão que usava malloc/realloc do sistema.
#define parser_da_append(arena, list, item) do { \
    if (!arena_da_reserve((arena), (void**)&(list)->items, &(list)->capacity, sizeof(*(list)->items), (list)->count + 1)) { \
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, "internal", \
            "falha de alocacao ao expandir array", "reduza o arquivo de entrada ou aumente memoria"); \
        break; \
    } \
    (list)->items[(list)->count++] = (item); \
} while(0)

// --- Forward Declarations ---
static Node parse_statement(Arena *arena, Token_List *tokens, size_t *cursor);
static Node_List parse_block(Arena *arena, Token_List *tokens, size_t *cursor, const char **terminators, size_t term_count, String_View *found_terminator);

// --- Helpers de Parsing ---

// Verifica se o token atual corresponde a uma string
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

// Consome argumentos entre '(' e ')'
static Args parse_args(Arena *arena, Token_List *tokens, size_t *cursor) {
    Args args = {0};
    bool closed = false;
    int depth = 0;
    
    // Verifica e consome '('
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
            parser_da_append(arena, &paren_arg, t);
            parser_da_append(arena, &args, paren_arg);
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
            parser_da_append(arena, &paren_arg, t);
            parser_da_append(arena, &args, paren_arg);
            (*cursor)++;
            continue;
        }

        if (t.kind == TOKEN_SEMICOLON && depth == 1) {
            (*cursor)++; // Ignora ';' solto
            continue;
        }

        // Parse de um argumento (cola tokens adjacentes sem espaço)
        Arg current_arg = {0};
        parser_da_append(arena, &current_arg, t);
        (*cursor)++;

        while (*cursor < tokens->count) {
            Token next = tokens->items[*cursor];
            if (next.kind == TOKEN_LPAREN || next.kind == TOKEN_RPAREN || next.kind == TOKEN_SEMICOLON) break;
            
            if (!next.has_space_left) {
                parser_da_append(arena, &current_arg, next);
                (*cursor)++;
            } else {
                break;
            }
        }
        parser_da_append(arena, &args, current_arg);
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

// --- Funções de Parsing de Blocos Específicos ---

// Parse: if(...) ... [elseif(...) ...] [else() ...] endif()
static Node parse_if(Arena *arena, Token_List *tokens, size_t *cursor) {
    Node node = {0};
    node.kind = NODE_IF;

    // 1. Condição
    node.as.if_stmt.condition = parse_args(arena, tokens, cursor);

    // 2. Bloco THEN
    const char *terminators[] = {"else", "elseif", "endif"};
    String_View found_term = {0};
    
    node.as.if_stmt.then_block = parse_block(arena, tokens, cursor, terminators, 3, &found_term);

    // 3. Lógica ELSE / ELSEIF
    if (sv_eq_ci_lit(found_term, "else")) {
        // Parse args do else()
        Args else_args = parse_args(arena, tokens, cursor); 
        (void)else_args; 

        // O bloco ELSE vai até 'endif'
        const char *else_terms[] = {"endif"};
        String_View end_term = {0};
        node.as.if_stmt.else_block = parse_block(arena, tokens, cursor, else_terms, 1, &end_term);
        if (!sv_eq_ci_lit(end_term, "endif")) {
            diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, "if",
                "if sem terminador endif()", "adicione endif() ao bloco");
        }
    } 
    else if (sv_eq_ci_lit(found_term, "elseif")) {
        // ELSEIF: Tratamos como um novo IF aninhado dentro do bloco ELSE
        Node nested_if = parse_if(arena, tokens, cursor);
        parser_da_append(arena, &node.as.if_stmt.else_block, nested_if);
    }
    else if (!sv_eq_ci_lit(found_term, "endif")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, "if",
            "if sem terminador endif()", "adicione endif() ao bloco");
    }
    
    // Se encontrou 'endif', acabou.
    return node;
}

static Node parse_foreach(Arena *arena, Token_List *tokens, size_t *cursor) {
    Node node = {0};
    node.kind = NODE_FOREACH;
    
    node.as.foreach_stmt.args = parse_args(arena, tokens, cursor);

    const char *terminators[] = {"endforeach"};
    String_View found_term = {0};
    node.as.foreach_stmt.body = parse_block(arena, tokens, cursor, terminators, 1, &found_term);
    if (!sv_eq_ci_lit(found_term, "endforeach")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, "foreach",
            "foreach sem terminador endforeach()", "adicione endforeach() ao bloco");
    }

    // Consome argumentos do endforeach() se houver
    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        Args ignore = parse_args(arena, tokens, cursor);
        (void)ignore;
    }

    return node;
}

static Node parse_while(Arena *arena, Token_List *tokens, size_t *cursor) {
    Node node = {0};
    node.kind = NODE_WHILE;

    node.as.while_stmt.condition = parse_args(arena, tokens, cursor);

    const char *terminators[] = {"endwhile"};
    String_View found_term = {0};
    node.as.while_stmt.body = parse_block(arena, tokens, cursor, terminators, 1, &found_term);
    if (!sv_eq_ci_lit(found_term, "endwhile")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, "while",
            "while sem terminador endwhile()", "adicione endwhile() ao bloco");
    }

    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        Args ignore = parse_args(arena, tokens, cursor);
        (void)ignore;
    }

    return node;
}

static Node parse_function_macro(Arena *arena, Token_List *tokens, size_t *cursor, bool is_macro) {
    Node node = {0};
    node.kind = is_macro ? NODE_MACRO : NODE_FUNCTION;
    
    Args all_args = parse_args(arena, tokens, cursor);
    
    // O primeiro argumento é o nome, o resto são parametros
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
    node.as.func_def.body = parse_block(arena, tokens, cursor, terms, 1, &found_term);
    if (!sv_eq_ci_lit(found_term, is_macro ? "endmacro" : "endfunction")) {
        diag_log(DIAG_SEV_ERROR, "parser", "<input>", 0, 0, is_macro ? "macro" : "function",
            "bloco sem terminador", is_macro ? "adicione endmacro()" : "adicione endfunction()");
    }

    if (*cursor < tokens->count && tokens->items[*cursor].kind == TOKEN_LPAREN) {
        Args ignore = parse_args(arena, tokens, cursor); (void)ignore;
    }

    return node;
}

// --- Parser Genérico de Bloco ---

static Node_List parse_block(Arena *arena, Token_List *tokens, size_t *cursor, const char **terminators, size_t term_count, String_View *found_terminator) {
    Node_List list = {0};

    while (*cursor < tokens->count) {
        Token t = tokens->items[*cursor];

        // Ignorar tokens de parênteses que não fazem parte de comandos
        if (t.kind == TOKEN_LPAREN || t.kind == TOKEN_RPAREN) {
            (*cursor)++;
            continue;
        }

        // 1. Verifica se é um terminador
        if (t.kind == TOKEN_IDENTIFIER && terminators != NULL) {
            for (size_t i = 0; i < term_count; ++i) {
                if (match_token_text(t, terminators[i])) {
                    if (found_terminator) *found_terminator = t.text;
                    (*cursor)++; // Consome o terminador
                    return list;
                }
            }
        }

        // 2. Parse do comando/declaração
        Node node = parse_statement(arena, tokens, cursor);
        // Só adiciona se não for um nó vazio
        if (node.kind != NODE_COMMAND || node.as.cmd.name.count > 0) {
            parser_da_append(arena, &list, node);
        }
    }
    
    return list;
}

// --- Dispatcher Principal ---

static Node parse_statement(Arena *arena, Token_List *tokens, size_t *cursor) {
    if (*cursor >= tokens->count) {
        Node node = {0};
        node.kind = NODE_COMMAND;
        return node;
    }
    
    Token t = tokens->items[*cursor];
    
    if (t.kind != TOKEN_IDENTIFIER) {
        Node node = {0};
        node.kind = NODE_COMMAND; // no vazio: descartado por parse_block
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
        node.kind = NODE_COMMAND; // no vazio: descartado por parse_block
        return node;
    }

    // Em CMake, invocacao de comando exige parenteses: cmd(...)
    if (*cursor >= tokens->count || tokens->items[*cursor].kind != TOKEN_LPAREN) {
        Node node = {0};
        node.kind = NODE_COMMAND; // no vazio: descartado por parse_block
        return node;
    }

    if (match_token_text(t, "if")) {
        return parse_if(arena, tokens, cursor);
    }
    if (match_token_text(t, "foreach")) {
        return parse_foreach(arena, tokens, cursor);
    }
    if (match_token_text(t, "while")) {
        return parse_while(arena, tokens, cursor);
    }
    if (match_token_text(t, "function")) {
        return parse_function_macro(arena, tokens, cursor, false);
    }
    if (match_token_text(t, "macro")) {
        return parse_function_macro(arena, tokens, cursor, true);
    }

    // Comando Comum (set, project, etc)
    Node node = {0};
    node.kind = NODE_COMMAND;
    node.as.cmd.name = t.text;
    node.as.cmd.args = parse_args(arena, tokens, cursor);
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
    return parse_block(arena, &tokens, &cursor, NULL, 0, NULL);
}


// Impressão visual da árvore
void print_ast_list(Node_List list, int indent);

void print_indent(int indent) {
    for(int i=0; i<indent; ++i) printf("  ");
}

void print_node(Node *node, int indent) {
    print_indent(indent);
    switch (node->kind) {
        case NODE_COMMAND:
            printf("\x1b[36mCMD\x1b[0m "SV_Fmt, SV_Arg(node->as.cmd.name));
            printf("\n");
            break;
        case NODE_IF:
            printf("\x1b[35mIF\x1b[0m\n");
            print_ast_list(node->as.if_stmt.then_block, indent + 1);
            if (node->as.if_stmt.else_block.count > 0) {
                print_indent(indent);
                printf("\x1b[35mELSE\x1b[0m\n");
                print_ast_list(node->as.if_stmt.else_block, indent + 1);
            }
            break;
        case NODE_FOREACH:
            printf("\x1b[32mFOREACH\x1b[0m\n");
            print_ast_list(node->as.foreach_stmt.body, indent + 1);
            break;
        case NODE_WHILE:
            printf("\x1b[32mWHILE\x1b[0m\n");
            print_ast_list(node->as.while_stmt.body, indent + 1);
            break;
        case NODE_FUNCTION:
            printf("\x1b[33mFUNCTION\x1b[0m "SV_Fmt"\n", SV_Arg(node->as.func_def.name));
            print_ast_list(node->as.func_def.body, indent + 1);
            break;
        case NODE_MACRO:
             printf("\x1b[33mMACRO\x1b[0m "SV_Fmt"\n", SV_Arg(node->as.func_def.name));
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
