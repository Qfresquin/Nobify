#ifndef EVAL_PROJECT_H_
#define EVAL_PROJECT_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct Evaluator_Context;

Eval_Result eval_handle_cmake_minimum_required(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_cmake_policy(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_enable_language(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_project(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_add_executable(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_add_library(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_PROJECT_H_
