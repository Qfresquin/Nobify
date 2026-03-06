#ifndef EVAL_DIAG_REGISTRY_H_
#define EVAL_DIAG_REGISTRY_H_

#define EVAL_DIAG_CODE_LIST(X) \
    X(EVAL_DIAG_PARSE_ERROR,           "EVAL_DIAG_PARSE_ERROR",           EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Malformed syntax, regex, or parseable token") \
    X(EVAL_DIAG_UNKNOWN_COMMAND,       "EVAL_DIAG_UNKNOWN_COMMAND",       EV_DIAG_WARNING, EVAL_ERR_CLASS_ENGINE_LIMITATION, true,  "Unknown command policy outcome") \
    X(EVAL_DIAG_MISSING_REQUIRED,      "EVAL_DIAG_MISSING_REQUIRED",      EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Missing required argument, keyword, or value") \
    X(EVAL_DIAG_UNEXPECTED_ARGUMENT,   "EVAL_DIAG_UNEXPECTED_ARGUMENT",   EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Unexpected or extraneous argument") \
    X(EVAL_DIAG_DUPLICATE_ARGUMENT,    "EVAL_DIAG_DUPLICATE_ARGUMENT",    EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Duplicate keyword or repeated option") \
    X(EVAL_DIAG_INVALID_VALUE,         "EVAL_DIAG_INVALID_VALUE",         EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Invalid token, value, or option selection") \
    X(EVAL_DIAG_INVALID_STATE,         "EVAL_DIAG_INVALID_STATE",         EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Command state or runtime precondition violation") \
    X(EVAL_DIAG_INVALID_CONTEXT,       "EVAL_DIAG_INVALID_CONTEXT",       EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Invalid scope or execution context") \
    X(EVAL_DIAG_OUT_OF_RANGE,          "EVAL_DIAG_OUT_OF_RANGE",          EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Index, count, or numeric bound violation") \
    X(EVAL_DIAG_CONFLICTING_OPTIONS,   "EVAL_DIAG_CONFLICTING_OPTIONS",   EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Mutually incompatible options or modes") \
    X(EVAL_DIAG_NOT_FOUND,             "EVAL_DIAG_NOT_FOUND",             EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Referenced entity could not be found") \
    X(EVAL_DIAG_UNSUPPORTED_OPERATION, "EVAL_DIAG_UNSUPPORTED_OPERATION", EV_DIAG_ERROR,   EVAL_ERR_CLASS_ENGINE_LIMITATION, true,  "Unsupported operation in evaluator") \
    X(EVAL_DIAG_NOT_IMPLEMENTED,       "EVAL_DIAG_NOT_IMPLEMENTED",       EV_DIAG_ERROR,   EVAL_ERR_CLASS_ENGINE_LIMITATION, true,  "Known but not yet implemented evaluator behavior") \
    X(EVAL_DIAG_IO_FAILURE,            "EVAL_DIAG_IO_FAILURE",            EV_DIAG_ERROR,   EVAL_ERR_CLASS_IO_ENV_ERROR,       false, "Filesystem, process, or environment I/O failure") \
    X(EVAL_DIAG_POLICY_CONFLICT,       "EVAL_DIAG_POLICY_CONFLICT",       EV_DIAG_ERROR,   EVAL_ERR_CLASS_POLICY_CONFLICT,    false, "Policy stack, CMP rule, or policy-controlled conflict") \
    X(EVAL_DIAG_NUMERIC_OVERFLOW,      "EVAL_DIAG_NUMERIC_OVERFLOW",      EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Arithmetic overflow or invalid numeric range") \
    X(EVAL_DIAG_SCRIPT_WARNING,        "EVAL_DIAG_SCRIPT_WARNING",        EV_DIAG_WARNING, EVAL_ERR_CLASS_INPUT_ERROR,        false, "Script-authored warning diagnostic") \
    X(EVAL_DIAG_SCRIPT_ERROR,          "EVAL_DIAG_SCRIPT_ERROR",          EV_DIAG_ERROR,   EVAL_ERR_CLASS_INPUT_ERROR,        false, "Script-authored error diagnostic") \
    X(EVAL_DIAG_LEGACY_WARNING,        "EVAL_DIAG_LEGACY_WARNING",        EV_DIAG_WARNING, EVAL_ERR_CLASS_INPUT_ERROR,        false, "Legacy, deprecated, or ignored behavior warning")

#define EVAL_ERROR_CLASS_LIST(X) \
    X(EVAL_ERR_CLASS_NONE, "NONE") \
    X(EVAL_ERR_CLASS_INPUT_ERROR, "INPUT_ERROR") \
    X(EVAL_ERR_CLASS_ENGINE_LIMITATION, "ENGINE_LIMITATION") \
    X(EVAL_ERR_CLASS_IO_ENV_ERROR, "IO_ENV_ERROR") \
    X(EVAL_ERR_CLASS_POLICY_CONFLICT, "POLICY_CONFLICT")

#endif // EVAL_DIAG_REGISTRY_H_
