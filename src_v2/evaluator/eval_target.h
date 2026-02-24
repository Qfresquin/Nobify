#ifndef EVAL_TARGET_H_
#define EVAL_TARGET_H_

#include <stdbool.h>

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_target_link_libraries(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_target_link_options(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_target_link_directories(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_target_include_directories(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_target_compile_definitions(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_target_compile_options(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_set_target_properties(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_set_property(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_TARGET_H_