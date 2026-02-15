#ifndef LOGIC_MODEL_H_
#define LOGIC_MODEL_H_

#include "arena.h"
#include "nob.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    LOGIC_OP_AND,
    LOGIC_OP_OR,
    LOGIC_OP_NOT,
    LOGIC_OP_COMPARE,
    LOGIC_OP_BOOL,
    LOGIC_OP_DEFINED,
    LOGIC_OP_LITERAL_TRUE,
    LOGIC_OP_LITERAL_FALSE
} Logic_Op_Type;

typedef enum {
    LOGIC_CMP_STREQUAL,
    LOGIC_CMP_EQUAL,
    LOGIC_CMP_LESS,
    LOGIC_CMP_GREATER,
    LOGIC_CMP_LESS_EQUAL,
    LOGIC_CMP_GREATER_EQUAL,
    LOGIC_CMP_VERSION_LESS,
    LOGIC_CMP_VERSION_GREATER,
    LOGIC_CMP_VERSION_EQUAL,
    LOGIC_CMP_VERSION_LESS_EQUAL,
    LOGIC_CMP_VERSION_GREATER_EQUAL
} Logic_Comparator;

typedef struct {
    String_View token;
    bool quoted;
} Logic_Operand;

typedef struct Logic_Node Logic_Node;
struct Logic_Node {
    Logic_Op_Type type;
    Logic_Node *left;
    Logic_Node *right;
    union {
        Logic_Operand operand;
        struct {
            Logic_Comparator op;
            Logic_Operand lhs;
            Logic_Operand rhs;
        } cmp;
    } as;
};

typedef struct {
    Arena *arena;
    const String_View *tokens;
    const bool *quoted_tokens;
    size_t count;
} Logic_Parse_Input;

typedef String_View (*Logic_Get_Var_Fn)(void *userdata, String_View name, bool *is_set);

typedef struct {
    Logic_Get_Var_Fn get_var;
    void *userdata;
} Logic_Eval_Context;

Logic_Node *logic_true(Arena *arena);
Logic_Node *logic_false(Arena *arena);
Logic_Node *logic_bool(Arena *arena, String_View token, bool quoted);
Logic_Node *logic_defined(Arena *arena, String_View token);
Logic_Node *logic_compare(Arena *arena, Logic_Comparator op, Logic_Operand lhs, Logic_Operand rhs);
Logic_Node *logic_not(Arena *arena, Logic_Node *node);
Logic_Node *logic_and(Arena *arena, Logic_Node *a, Logic_Node *b);
Logic_Node *logic_or(Arena *arena, Logic_Node *a, Logic_Node *b);

Logic_Node *logic_parse_expression(const Logic_Parse_Input *in, size_t *out_consumed);
bool logic_evaluate(const Logic_Node *node, const Logic_Eval_Context *ctx);
bool logic_parse_and_evaluate(const Logic_Parse_Input *in, const Logic_Eval_Context *ctx, bool *out_ok);

#endif // LOGIC_MODEL_H_
