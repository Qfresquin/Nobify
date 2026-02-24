#ifndef EVAL_INSTALL_H_
#define EVAL_INSTALL_H_

#include <stdbool.h>

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_install(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_INSTALL_H_