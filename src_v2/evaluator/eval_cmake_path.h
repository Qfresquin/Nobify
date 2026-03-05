#ifndef EVAL_CMAKE_PATH_H_
#define EVAL_CMAKE_PATH_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct Evaluator_Context;

Eval_Result eval_handle_cmake_path(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_CMAKE_PATH_H_