#ifndef EVAL_META_H_
#define EVAL_META_H_

#include "parser.h"

struct Evaluator_Context;

bool eval_handle_export(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_cmake_file_api(struct Evaluator_Context *ctx, const Node *node);
bool eval_handle_include_external_msproject(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_META_H_
