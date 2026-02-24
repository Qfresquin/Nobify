// --- START OF FILE eval_file.h ---
#ifndef EVAL_FILE_H_
#define EVAL_FILE_H_

#include "parser.h"
#include "evaluator.h"

// Handler principal para o comando file()
bool eval_handle_file(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_FILE_H_
