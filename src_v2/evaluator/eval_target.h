#ifndef EVAL_TARGET_H_
#define EVAL_TARGET_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_target_link_libraries(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_add_dependencies(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_target_link_options(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_target_link_directories(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_target_include_directories(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_target_compile_definitions(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_target_compile_options(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_target_sources(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_target_compile_features(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_target_precompile_headers(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_source_group(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_define_property(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_get_property(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_get_cmake_property(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_get_directory_property(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_get_source_file_property(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_get_target_property(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_get_test_property(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_set_directory_properties(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_set_source_files_properties(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_set_tests_properties(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_set_target_properties(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_set_property(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_TARGET_H_
