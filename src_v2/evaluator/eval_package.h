#ifndef EVAL_PACKAGE_H_
#define EVAL_PACKAGE_H_

#include <stdbool.h>

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_find_package(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_find_program(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_find_file(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_find_path(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_find_library(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_PACKAGE_H_
