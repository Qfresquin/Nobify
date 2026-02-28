#ifndef EVAL_REPORT_H_
#define EVAL_REPORT_H_

#include "evaluator_internal.h"

void eval_report_reset(Evaluator_Context *ctx);
void eval_report_record_diag(Evaluator_Context *ctx,
                             Cmake_Diag_Severity sev,
                             Eval_Diag_Code code,
                             Eval_Error_Class cls);
void eval_report_finalize(Evaluator_Context *ctx);

#endif // EVAL_REPORT_H_
