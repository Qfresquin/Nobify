#ifndef EVAL_PACKAGE_H_
#define EVAL_PACKAGE_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_find_package(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_find_program(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_find_file(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_find_path(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_find_library(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_PACKAGE_H_
