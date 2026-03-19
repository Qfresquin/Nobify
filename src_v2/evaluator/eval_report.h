#ifndef EVAL_REPORT_H_
#define EVAL_REPORT_H_

#include "evaluator_internal.h"

void eval_report_reset(EvalExecContext *ctx);
void eval_report_record_diag(EvalExecContext *ctx,
                             Event_Diag_Severity input_sev,
                             Event_Diag_Severity effective_sev,
                             Eval_Diag_Code code);
void eval_report_finalize(EvalExecContext *ctx);

#endif // EVAL_REPORT_H_
