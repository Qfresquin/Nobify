#ifndef EVAL_META_H_
#define EVAL_META_H_

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_export(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_cmake_file_api(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_include_external_msproject(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_META_H_
