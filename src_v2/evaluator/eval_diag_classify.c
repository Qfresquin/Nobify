#include "eval_diag_classify.h"

#include "evaluator_internal.h"
#include "eval_diag_registry.h"

typedef struct {
    Eval_Diag_Code code;
    const char *public_name;
    Cmake_Diag_Severity default_severity;
    Eval_Error_Class error_class;
    bool counts_as_unsupported;
    const char *hint_contract;
} Eval_Diag_Spec_Def;

static const Eval_Diag_Spec_Def k_eval_diag_specs[] = {
#define EVAL_DIAG_SPEC_ROW(code, text, default_sev, error_class, counts_as_unsupported, hint_contract) \
    {code, text, default_sev, error_class, counts_as_unsupported, hint_contract},
    EVAL_DIAG_CODE_LIST(EVAL_DIAG_SPEC_ROW)
#undef EVAL_DIAG_SPEC_ROW
};

static const Eval_Diag_Spec_Def *eval_diag_spec_def(Eval_Diag_Code code) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(k_eval_diag_specs); i++) {
        if (k_eval_diag_specs[i].code == code) return &k_eval_diag_specs[i];
    }
    return NULL;
}

const Eval_Diag_Spec *eval_diag_spec(Eval_Diag_Code code) {
    static Eval_Diag_Spec out = {0};
    const Eval_Diag_Spec_Def *def = eval_diag_spec_def(code);
    if (!def) return NULL;

    out.code = def->code;
    out.public_name = nob_sv_from_cstr(def->public_name);
    out.default_severity = def->default_severity;
    out.error_class = def->error_class;
    out.counts_as_unsupported = def->counts_as_unsupported;
    out.hint_contract = nob_sv_from_cstr(def->hint_contract);
    return &out;
}

String_View eval_diag_code_to_sv(Eval_Diag_Code code) {
    const Eval_Diag_Spec_Def *def = eval_diag_spec_def(code);
    return nob_sv_from_cstr(def ? def->public_name : "EVAL_DIAG_UNKNOWN");
}

Eval_Error_Class eval_diag_error_class(Eval_Diag_Code code) {
    const Eval_Diag_Spec_Def *def = eval_diag_spec_def(code);
    return def ? def->error_class : EVAL_ERR_CLASS_NONE;
}

Cmake_Diag_Severity eval_diag_default_severity(Eval_Diag_Code code) {
    const Eval_Diag_Spec_Def *def = eval_diag_spec_def(code);
    return def ? def->default_severity : EV_DIAG_ERROR;
}

bool eval_diag_counts_as_unsupported(Eval_Diag_Code code) {
    const Eval_Diag_Spec_Def *def = eval_diag_spec_def(code);
    return def ? def->counts_as_unsupported : false;
}

String_View eval_error_class_to_sv(Eval_Error_Class cls) {
    switch (cls) {
#define EVAL_ERROR_CLASS_CASE(code, text) case code: return nob_sv_from_cstr(text);
        EVAL_ERROR_CLASS_LIST(EVAL_ERROR_CLASS_CASE)
#undef EVAL_ERROR_CLASS_CASE
    }
    return nob_sv_from_cstr("NONE");
}
