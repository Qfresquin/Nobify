#ifndef EVAL_INSTALL_H_
#define EVAL_INSTALL_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_install(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_INSTALL_H_