#include "eval_diag_classify.h"

#include "evaluator_internal.h"

#include <ctype.h>

static bool eval_sv_contains_ci_lit(String_View haystack, const char *needle) {
    if (!needle || needle[0] == '\0') return false;
    String_View n = nob_sv_from_cstr(needle);
    if (n.count == 0 || haystack.count < n.count) return false;
    for (size_t i = 0; i + n.count <= haystack.count; i++) {
        bool match = true;
        for (size_t k = 0; k < n.count; k++) {
            unsigned char a = (unsigned char)haystack.data[i + k];
            unsigned char b = (unsigned char)n.data[k];
            if (toupper(a) != toupper(b)) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

void eval_diag_classify(String_View component,
                        String_View cause,
                        Cmake_Diag_Severity sev,
                        Eval_Diag_Code *out_code,
                        Eval_Error_Class *out_class) {
    Eval_Diag_Code code = EVAL_ERR_SEMANTIC;
    Eval_Error_Class cls = EVAL_ERR_CLASS_INPUT_ERROR;

    if (eval_sv_eq_ci_lit(component, "parser") || eval_sv_eq_ci_lit(component, "lexer")) {
        code = EVAL_ERR_PARSE;
        cls = EVAL_ERR_CLASS_INPUT_ERROR;
    }
    if (eval_sv_contains_ci_lit(cause, "unsupported") || eval_sv_contains_ci_lit(cause, "unknown command")) {
        code = EVAL_ERR_UNSUPPORTED;
        cls = EVAL_ERR_CLASS_ENGINE_LIMITATION;
    }
    if (eval_sv_contains_ci_lit(cause, "policy")) {
        cls = EVAL_ERR_CLASS_POLICY_CONFLICT;
    }
    if (eval_sv_eq_ci_lit(component, "eval_file") ||
        eval_sv_contains_ci_lit(cause, "failed to read") ||
        eval_sv_contains_ci_lit(cause, "security violation") ||
        eval_sv_contains_ci_lit(cause, "remote url")) {
        cls = EVAL_ERR_CLASS_IO_ENV_ERROR;
    }
    if (sev == EV_DIAG_WARNING &&
        (eval_sv_contains_ci_lit(cause, "legacy") ||
         eval_sv_contains_ci_lit(cause, "ignored") ||
         eval_sv_contains_ci_lit(cause, "deprecated"))) {
        code = EVAL_WARN_LEGACY;
    }

    if (out_code) *out_code = code;
    if (out_class) *out_class = cls;
}

String_View eval_diag_code_to_sv(Eval_Diag_Code code) {
    switch (code) {
        case EVAL_ERR_PARSE: return nob_sv_from_cstr("EVAL_ERR_PARSE");
        case EVAL_ERR_SEMANTIC: return nob_sv_from_cstr("EVAL_ERR_SEMANTIC");
        case EVAL_ERR_UNSUPPORTED: return nob_sv_from_cstr("EVAL_ERR_UNSUPPORTED");
        case EVAL_WARN_LEGACY: return nob_sv_from_cstr("EVAL_WARN_LEGACY");
        default: return nob_sv_from_cstr("EVAL_ERR_NONE");
    }
}

String_View eval_error_class_to_sv(Eval_Error_Class cls) {
    switch (cls) {
        case EVAL_ERR_CLASS_INPUT_ERROR: return nob_sv_from_cstr("INPUT_ERROR");
        case EVAL_ERR_CLASS_ENGINE_LIMITATION: return nob_sv_from_cstr("ENGINE_LIMITATION");
        case EVAL_ERR_CLASS_IO_ENV_ERROR: return nob_sv_from_cstr("IO_ENV_ERROR");
        case EVAL_ERR_CLASS_POLICY_CONFLICT: return nob_sv_from_cstr("POLICY_CONFLICT");
        default: return nob_sv_from_cstr("NONE");
    }
}
