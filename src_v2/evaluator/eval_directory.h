#ifndef EVAL_DIRECTORY_H_
#define EVAL_DIRECTORY_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_add_compile_options(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_add_compile_definitions(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_add_definitions(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_add_link_options(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_remove_definitions(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_include_regular_expression(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_get_filename_component(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_link_libraries(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_include_directories(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_link_directories(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_DIRECTORY_H_
