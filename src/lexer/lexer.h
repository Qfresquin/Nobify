#ifndef LEXER_H_
#define LEXER_H_

#include "nob.h"

typedef enum {
    TOKEN_END = 0,
    TOKEN_INVALID,
    TOKEN_LPAREN,     // (
    TOKEN_RPAREN,     // )
    TOKEN_SEMICOLON,  // ;
    TOKEN_STRING,     // "texto com \"escapes\""
    TOKEN_RAW_STRING, // [[texto bruto]] ou [=[...]=]
    TOKEN_IDENTIFIER, // add_executable, main.c, lib
    TOKEN_VAR,        // ${VAR} ou $ENV{VAR}
    TOKEN_GEN_EXP,    // $<TARGET_FILE:app>
} Token_Kind;

typedef struct {
    Token_Kind kind;
    String_View text;
    size_t line;
    size_t col;
    bool has_space_left; 
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} Token_List;

typedef struct {
    String_View content;
    size_t cursor;
    size_t line;
    size_t bol; 
} Lexer;

Lexer lexer_init(String_View content);
Token lexer_next(Lexer *l);
const char* token_kind_name(Token_Kind kind);

#endif // LEXER_H_