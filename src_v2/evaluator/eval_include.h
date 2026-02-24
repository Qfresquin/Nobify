#ifndef EVAL_INCLUDE_H_
#define EVAL_INCLUDE_H_

#include <stdbool.h>

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_include_guard(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_include(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_add_subdirectory(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_INCLUDE_H_