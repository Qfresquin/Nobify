#ifndef EVAL_FLOW_H_
#define EVAL_FLOW_H_

#include "parser.h"
#include "evaluator.h"

Eval_Result eval_handle_break(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_continue(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_return(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_block(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_endblock(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_execute_process(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_exec_program(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_cmake_language(struct EvalExecContext *ctx, const Node *node);
bool eval_unwind_blocks_for_return(struct EvalExecContext *ctx);

#endif // EVAL_FLOW_H_

