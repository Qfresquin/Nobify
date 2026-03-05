#ifndef EVAL_CPACK_H_
#define EVAL_CPACK_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct Evaluator_Context;

Eval_Result eval_handle_cpack_add_install_type(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_cpack_add_component_group(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_cpack_add_component(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_CPACK_H_
