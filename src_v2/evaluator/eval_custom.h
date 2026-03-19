#ifndef EVAL_CUSTOM_H_
#define EVAL_CUSTOM_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_add_custom_target(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_add_custom_command(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_CUSTOM_H_