#ifndef EVAL_FLOW_H_
#define EVAL_FLOW_H_

#include "parser.h"
#include "evaluator.h"

bool eval_handle_break(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_continue(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_return(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_block(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_endblock(struct Evaluator_Context *ctx, const Node *node);
bool eval_unwind_blocks_for_return(struct Evaluator_Context *ctx);

#endif // EVAL_FLOW_H_

