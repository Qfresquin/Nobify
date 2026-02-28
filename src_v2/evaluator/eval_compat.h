#ifndef EVAL_COMPAT_H_
#define EVAL_COMPAT_H_

#include "evaluator_internal.h"

String_View eval_compat_profile_to_sv(Eval_Compat_Profile profile);
bool eval_compat_set_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile);
void eval_refresh_runtime_compat(Evaluator_Context *ctx);
Cmake_Diag_Severity eval_compat_effective_severity(const Evaluator_Context *ctx, Cmake_Diag_Severity sev);
bool eval_compat_decide_on_diag(Evaluator_Context *ctx, Cmake_Diag_Severity effective_sev);

#endif // EVAL_COMPAT_H_
