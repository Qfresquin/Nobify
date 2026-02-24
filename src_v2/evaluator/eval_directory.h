#ifndef EVAL_DIRECTORY_H_
#define EVAL_DIRECTORY_H_

#include <stdbool.h>

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_add_compile_options(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_add_definitions(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_add_link_options(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_link_libraries(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_include_directories(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_link_directories(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_DIRECTORY_H_