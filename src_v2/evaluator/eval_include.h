#ifndef EVAL_INCLUDE_H_
#define EVAL_INCLUDE_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_include_guard(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_include(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_add_subdirectory(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_INCLUDE_H_