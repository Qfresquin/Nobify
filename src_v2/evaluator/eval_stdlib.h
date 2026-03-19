#ifndef EVAL_STDLIB_H_
#define EVAL_STDLIB_H_

#include "parser.h"
#include "evaluator.h"

// Handlers da "biblioteca padrão" do CMake.
Eval_Result eval_handle_list(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_string(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_math(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_STDLIB_H_

