#ifndef EVAL_DIAG_CLASSIFY_H_
#define EVAL_DIAG_CLASSIFY_H_

#include "evaluator.h"

void eval_diag_classify(String_View component,
                        String_View cause,
                        Cmake_Diag_Severity sev,
                        Eval_Diag_Code *out_code,
                        Eval_Error_Class *out_class);

String_View eval_diag_code_to_sv(Eval_Diag_Code code);
String_View eval_error_class_to_sv(Eval_Error_Class cls);

#endif // EVAL_DIAG_CLASSIFY_H_
