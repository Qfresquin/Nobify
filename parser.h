#ifndef PARSER_H_
#define PARSER_H_

#include "lexer.h"
#include "arena.h" 

// --- Estruturas de Argumentos (Mantidas do original) ---

// Um argumento lógico (pode ser "texto" ou ${VAR} ou concatenado "lib${V}.a")
typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} Arg;

typedef struct {
    Arg *items;
    size_t count;
    size_t capacity;
} Args;

// --- Estruturas da AST (Novas) ---

typedef enum {
    NODE_COMMAND,   // set(), project(), add_executable()
    NODE_IF,        // if() ... elseif() ... else() ... endif()
    NODE_FOREACH,   // foreach() ... endforeach()
    NODE_WHILE,     // while() ... endwhile()
    NODE_FUNCTION,  // function() ... endfunction()
    NODE_MACRO      // macro() ... endmacro()
} Node_Kind;

typedef struct Node Node; // Forward declaration para uso na Node_List

// Lista de nós (representa um bloco de código, ex: corpo de um if)
typedef struct {
    Node *items;
    size_t count;
    size_t capacity;
} Node_List;

struct Node {
    Node_Kind kind;
    
    union {
        // Comando Simples
        struct {
            String_View name;
            Args args;
        } cmd;

        // Controle de Fluxo: IF / ELSEIF / ELSE
        // Nota: 'elseif' é tratado como um novo NODE_IF aninhado dentro do 'else_block'
        struct {
            Args condition;       // Argumentos do if (ex: WIN32 OR APPLE)
            Node_List then_block; // Bloco executado se verdadeiro
            Node_List else_block; // Bloco else (pode conter outro IF se for elseif)
        } if_stmt;

        // Loop: FOREACH
        struct {
            Args args;            // Argumentos (ex: VAR IN ITEMS a b c)
            Node_List body;       // Bloco repetido
        } foreach_stmt;

        // Loop: WHILE
        struct {
            Args condition;       // Condicao do while
            Node_List body;       // Bloco repetido
        } while_stmt;

        // Definição: FUNCTION / MACRO
        struct {
            String_View name;     // Nome da função/macro
            Args params;          // Lista de parâmetros
            Node_List body;       // Corpo da função
        } func_def;
    } as;
};

// O resultado do parsing é o bloco raiz (Root Block)
typedef Node_List Ast_Root;

// Função principal do Parser (arena obrigatória)
Ast_Root parse_tokens(Arena *arena, Token_List tokens);

// Função para liberar memória da AST
void ast_free(Ast_Root root);

// Função auxiliar para imprimir a AST (Debug)
void print_ast(Ast_Root root, int indent);



#endif // PARSER_H_
