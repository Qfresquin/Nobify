#ifndef EVAL_CPACK_H_
#define EVAL_CPACK_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_cpack_add_install_type(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_cpack_add_component_group(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_cpack_add_component(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_CPACK_H_
