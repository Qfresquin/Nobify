#ifndef EVAL_TEST_H_
#define EVAL_TEST_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct Evaluator_Context;

Eval_Result eval_handle_enable_testing(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_add_test(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_create_test_sourcelist(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_TEST_H_
