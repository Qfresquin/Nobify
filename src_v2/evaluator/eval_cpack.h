#ifndef EVAL_CPACK_H_
#define EVAL_CPACK_H_

#include <stdbool.h>

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_cpack_add_install_type(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_cpack_add_component_group(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_cpack_add_component(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_CPACK_H_
