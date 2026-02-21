// --- START OF FILE parser.h ---
#ifndef PARSER_H_
#define PARSER_H_

#include "lexer.h"
#include "arena.h" 

// --- Estruturas de Argumentos ---

// Novo: Define como o argumento foi encapsulado no arquivo CMake
typedef enum {
    ARG_UNQUOTED = 0, // Argumento normal (ex: lib${V}.a) - Sujeito a quebra por ';'
    ARG_QUOTED,       // Entre aspas (ex: "lib${V}.a") - Protege os ';'
    ARG_BRACKET       // Entre colchetes (ex: [[texto puro]]) - Ignora expansão e ';'
} Arg_Kind;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
    Arg_Kind kind;    // Rastreia o tipo de quoting deste argumento
} Arg;

typedef struct {
    Arg *items;
    size_t count;
    size_t capacity;
} Args;

// --- Estruturas da AST ---

typedef enum {
    NODE_COMMAND,   // set(), project(), add_executable()
    NODE_IF,        // if() ... elseif() ... else() ... endif()
    NODE_FOREACH,   // foreach() ... endforeach()
    NODE_WHILE,     // while() ... endwhile()
    NODE_FUNCTION,  // function() ... endfunction()
    NODE_MACRO      // macro() ... endmacro()
} Node_Kind;

typedef struct Node Node;

typedef struct {
    Node *items;
    size_t count;
    size_t capacity;
} Node_List;

typedef struct {
    Args condition;
    Node_List block;
} ElseIf_Clause;

typedef struct {
    ElseIf_Clause *items;
    size_t count;
    size_t capacity;
} ElseIf_Clause_List;

struct Node {
    Node_Kind kind;
    
    // Novo: Rastreio de Origem (Essencial para o Evaluator_v2 e Diagnósticos)
    size_t line;
    size_t col;
    
    union {
        struct {
            String_View name;
            Args args;
        } cmd;

        struct {
            Args condition;       
            Node_List then_block; 
            ElseIf_Clause_List elseif_clauses;
            Node_List else_block; 
        } if_stmt;

        struct {
            Args args;            
            Node_List body;       
        } foreach_stmt;

        struct {
            Args condition;       
            Node_List body;       
        } while_stmt;

        struct {
            String_View name;     
            Args params;          
            Node_List body;       
        } func_def;
    } as;
};

// O resultado do parsing é o bloco raiz (Root Block)
typedef Node_List Ast_Root;

// Função principal do Parser
Ast_Root parse_tokens(Arena *arena, Token_List tokens);

// Função para liberar memória da AST
void ast_free(Ast_Root root);

// Função auxiliar para imprimir a AST (Debug)
void print_ast(Ast_Root root, int indent);

#endif // PARSER_H_
