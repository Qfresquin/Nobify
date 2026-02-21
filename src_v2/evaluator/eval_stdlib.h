#ifndef EVAL_STDLIB_H_
#define EVAL_STDLIB_H_

#include "parser.h"
#include "evaluator.h"

// Handlers da "biblioteca padrão" do CMake.
bool h_list(struct Evaluator_Context *ctx, const Node *node);
bool h_string(struct Evaluator_Context *ctx, const Node *node);
bool h_math(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_STDLIB_H_

