#ifndef EVAL_TEST_H_
#define EVAL_TEST_H_

#include <stdbool.h>

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_enable_testing(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_add_test(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_TEST_H_