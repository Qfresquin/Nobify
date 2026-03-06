#ifndef EVAL_EXEC_CORE_H_
#define EVAL_EXEC_CORE_H_

#include "evaluator_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

Eval_Result eval_execute_node_list(Evaluator_Context *ctx, const Node_List *list);

#ifdef __cplusplus
}
#endif

#endif // EVAL_EXEC_CORE_H_
