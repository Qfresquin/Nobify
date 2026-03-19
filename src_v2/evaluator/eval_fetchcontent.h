#ifndef EVAL_FETCHCONTENT_H_
#define EVAL_FETCHCONTENT_H_

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_fetchcontent_declare(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_fetchcontent_getproperties(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_fetchcontent_makeavailable(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_fetchcontent_populate(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_fetchcontent_setpopulated(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_FETCHCONTENT_H_
