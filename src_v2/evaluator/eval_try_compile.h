#ifndef EVAL_TRY_COMPILE_H_
#define EVAL_TRY_COMPILE_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

// Handler dedicado de try_compile(), extraido do dispatcher para melhorar manutencao.
Eval_Result eval_handle_try_compile(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_try_run(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_TRY_COMPILE_H_
