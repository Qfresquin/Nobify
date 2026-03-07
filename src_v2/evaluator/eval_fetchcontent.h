#ifndef EVAL_FETCHCONTENT_H_
#define EVAL_FETCHCONTENT_H_

#include "parser.h"
#include "evaluator.h"

struct Evaluator_Context;

Eval_Result eval_handle_fetchcontent_declare(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_fetchcontent_getproperties(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_fetchcontent_makeavailable(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_fetchcontent_setpopulated(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_FETCHCONTENT_H_
