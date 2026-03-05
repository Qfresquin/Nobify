#ifndef EVAL_TARGET_H_
#define EVAL_TARGET_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct Evaluator_Context;

Eval_Result eval_handle_target_link_libraries(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_add_dependencies(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_target_link_options(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_target_link_directories(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_target_include_directories(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_target_compile_definitions(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_target_compile_options(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_target_sources(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_target_compile_features(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_target_precompile_headers(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_source_group(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_define_property(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_get_property(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_get_cmake_property(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_get_directory_property(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_get_source_file_property(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_get_target_property(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_get_test_property(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_set_directory_properties(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_set_source_files_properties(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_set_tests_properties(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_set_target_properties(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_set_property(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_TARGET_H_
