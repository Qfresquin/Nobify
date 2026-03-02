#ifndef EVAL_HOST_H_
#define EVAL_HOST_H_

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_cmake_host_system_information(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_site_name(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_build_name(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_build_command(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_HOST_H_
