#ifndef EVAL_VARS_H_
#define EVAL_VARS_H_

#include "parser.h"
#include "evaluator.h"

bool eval_handle_set(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_unset(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_VARS_H_
