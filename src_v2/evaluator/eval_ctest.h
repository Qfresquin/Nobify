#ifndef EVAL_CTEST_H_
#define EVAL_CTEST_H_

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_ctest_build(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_configure(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_coverage(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_empty_binary_directory(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_memcheck(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_read_custom_files(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_run_script(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_sleep(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_start(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_submit(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_test(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_update(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_ctest_upload(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_CTEST_H_
