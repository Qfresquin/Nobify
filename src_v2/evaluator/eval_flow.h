#ifndef EVAL_FLOW_H_
#define EVAL_FLOW_H_

#include "parser.h"
#include "evaluator.h"

bool h_break(struct Evaluator_Context *ctx, const Node *node);
bool h_continue(struct Evaluator_Context *ctx, const Node *node);
bool h_return(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_FLOW_H_

