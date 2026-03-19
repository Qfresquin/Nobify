#ifndef EVAL_PROJECT_H_
#define EVAL_PROJECT_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_cmake_minimum_required(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_cmake_policy(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_enable_language(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_project(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_add_executable(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_add_library(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_PROJECT_H_
