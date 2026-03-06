#include "eval_report.h"

#include "eval_diag_classify.h"

#include <string.h>

void eval_report_reset(Evaluator_Context *ctx) {
    if (!ctx) return;
    Eval_Runtime_State *runtime = eval_runtime_slice(ctx);
    memset(&runtime->run_report, 0, sizeof(runtime->run_report));
    runtime->run_report.overall_status = EVAL_RUN_OK;
}

void eval_report_record_diag(Evaluator_Context *ctx,
                             Event_Diag_Severity input_sev,
                             Event_Diag_Severity effective_sev,
                             Eval_Diag_Code code) {
    if (!ctx) return;
    Eval_Runtime_State *runtime = eval_runtime_slice(ctx);
    Eval_Error_Class cls = eval_diag_error_class(code);

    if (input_sev == EV_DIAG_WARNING) runtime->run_report.warning_count++;
    if (effective_sev == EV_DIAG_ERROR) runtime->run_report.error_count++;

    if (cls == EVAL_ERR_CLASS_INPUT_ERROR) runtime->run_report.input_error_count++;
    else if (cls == EVAL_ERR_CLASS_ENGINE_LIMITATION) runtime->run_report.engine_limitation_count++;
    else if (cls == EVAL_ERR_CLASS_IO_ENV_ERROR) runtime->run_report.io_env_error_count++;
    else if (cls == EVAL_ERR_CLASS_POLICY_CONFLICT) runtime->run_report.policy_conflict_count++;

    if (eval_diag_counts_as_unsupported(code)) runtime->run_report.unsupported_count++;
    eval_report_finalize(ctx);
}

void eval_report_finalize(Evaluator_Context *ctx) {
    if (!ctx) return;
    Eval_Runtime_State *runtime = eval_runtime_slice(ctx);
    if (ctx->oom || ctx->stop_requested) runtime->run_report.overall_status = EVAL_RUN_FATAL;
    else if (runtime->run_report.error_count > 0) runtime->run_report.overall_status = EVAL_RUN_OK_WITH_ERRORS;
    else if (runtime->run_report.warning_count > 0) runtime->run_report.overall_status = EVAL_RUN_OK_WITH_WARNINGS;
    else runtime->run_report.overall_status = EVAL_RUN_OK;
}
