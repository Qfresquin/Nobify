#ifndef EVAL_DIAG_CLASSIFY_H_
#define EVAL_DIAG_CLASSIFY_H_

#include "evaluator.h"

typedef struct {
    Eval_Diag_Code code;
    String_View public_name;
    Cmake_Diag_Severity default_severity;
    Eval_Error_Class error_class;
    bool counts_as_unsupported;
    String_View hint_contract;
} Eval_Diag_Spec;

const Eval_Diag_Spec *eval_diag_spec(Eval_Diag_Code code);
String_View eval_diag_code_to_sv(Eval_Diag_Code code);
Eval_Error_Class eval_diag_error_class(Eval_Diag_Code code);
Cmake_Diag_Severity eval_diag_default_severity(Eval_Diag_Code code);
bool eval_diag_counts_as_unsupported(Eval_Diag_Code code);
String_View eval_error_class_to_sv(Eval_Error_Class cls);

#endif // EVAL_DIAG_CLASSIFY_H_
