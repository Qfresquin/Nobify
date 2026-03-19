#ifndef EVAL_VARS_H_
#define EVAL_VARS_H_

#include "parser.h"
#include "evaluator.h"

Eval_Result eval_handle_set(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_option(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_mark_as_advanced(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_separate_arguments(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_cmake_parse_arguments(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_load_cache(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_unset(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_VARS_H_
