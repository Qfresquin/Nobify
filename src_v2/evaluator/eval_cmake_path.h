#ifndef EVAL_CMAKE_PATH_H_
#define EVAL_CMAKE_PATH_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_cmake_path(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_CMAKE_PATH_H_