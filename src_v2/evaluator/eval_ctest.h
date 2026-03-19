#ifndef EVAL_CTEST_H_
#define EVAL_CTEST_H_

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_ctest_build(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_configure(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_coverage(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_empty_binary_directory(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_memcheck(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_read_custom_files(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_run_script(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_sleep(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_start(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_submit(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_test(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_update(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_ctest_upload(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_CTEST_H_
