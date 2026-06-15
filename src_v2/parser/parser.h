// --- START OF FILE parser.h ---
#ifndef PARSER_H_
#define PARSER_H_

#include "lexer.h"
#include "arena.h" 
#include <stddef.h>

// --- Argument Structures ---

// Tracks how an argument was quoted in the original CMake source.
typedef enum {
    ARG_UNQUOTED = 0, // Plain argument, e.g. lib${V}.a; can still split on ';'
    ARG_QUOTED,       // Quoted argument, e.g. "lib${V}.a"; protects ';'
    ARG_BRACKET       // Bracket argument, e.g. [[raw text]]; no expansion or ';' splitting
} Arg_Kind;

typedef struct {
    Token *items;
    Arg_Kind kind;    // Preserves the original quoting style of this argument
} Arg;

typedef Arg *Args;

// --- AST Structures ---

typedef enum {
    NODE_COMMAND,   // set(), project(), add_executable()
    NODE_IF,        // if() ... elseif() ... else() ... endif()
    NODE_FOREACH,   // foreach() ... endforeach()
    NODE_WHILE,     // while() ... endwhile()
    NODE_FUNCTION,  // function() ... endfunction()
    NODE_MACRO      // macro() ... endmacro()
} Node_Kind;

typedef struct Node Node;
typedef Node *Node_List;

typedef struct {
    Args condition;
    Node_List block;
} ElseIf_Clause;

typedef ElseIf_Clause *ElseIf_Clause_List;

struct Node {
    Node_Kind kind;
    
    // Source location used for diagnostics and debugging output.
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

// Parsing result: the root block of statements.
typedef Node_List Ast_Root;

typedef struct {
    // SIZE_MAX disables append-budget fault injection.
    size_t fail_after_appends;
    size_t max_block_depth;
    size_t max_paren_depth;
} Parser_Options;

Parser_Options parser_default_options(void);

// Main parser entry point.
// Recovery: emits diagnostics for invalid syntax and tries to continue.
Ast_Root parse_tokens(Arena *arena, Token_List tokens);
Ast_Root parse_tokens_with_options(Arena *arena, Token_List tokens, Parser_Options options);

// With arena allocation, ast_free() is a no-op; destroy the arena instead.
void ast_free(Ast_Root root);

// Helper for debug printing of the AST.
void print_ast(Ast_Root root, int indent);

#endif // PARSER_H_
