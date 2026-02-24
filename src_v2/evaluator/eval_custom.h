#ifndef EVAL_CUSTOM_H_
#define EVAL_CUSTOM_H_

#include <stdbool.h>

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_add_custom_target(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_add_custom_command(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_CUSTOM_H_