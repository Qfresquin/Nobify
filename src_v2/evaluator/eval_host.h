#ifndef EVAL_HOST_H_
#define EVAL_HOST_H_

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_cmake_host_system_information(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_site_name(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_build_name(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_build_command(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_HOST_H_
