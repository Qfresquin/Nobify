#include "eval_report.h"

#include <string.h>

void eval_report_reset(Evaluator_Context *ctx) {
    if (!ctx) return;
    memset(&ctx->run_report, 0, sizeof(ctx->run_report));
    ctx->run_report.overall_status = EVAL_RUN_OK;
}

void eval_report_record_diag(Evaluator_Context *ctx,
                             Cmake_Diag_Severity sev,
                             Eval_Diag_Code code,
                             Eval_Error_Class cls) {
    if (!ctx) return;
    if (sev == EV_DIAG_WARNING) ctx->run_report.warning_count++;
    else ctx->run_report.error_count++;

    if (cls == EVAL_ERR_CLASS_INPUT_ERROR) ctx->run_report.input_error_count++;
    else if (cls == EVAL_ERR_CLASS_ENGINE_LIMITATION) ctx->run_report.engine_limitation_count++;
    else if (cls == EVAL_ERR_CLASS_IO_ENV_ERROR) ctx->run_report.io_env_error_count++;
    else if (cls == EVAL_ERR_CLASS_POLICY_CONFLICT) ctx->run_report.policy_conflict_count++;

    if (code == EVAL_ERR_UNSUPPORTED) ctx->run_report.unsupported_count++;
    eval_report_finalize(ctx);
}

void eval_report_finalize(Evaluator_Context *ctx) {
    if (!ctx) return;
    if (ctx->oom || ctx->stop_requested) ctx->run_report.overall_status = EVAL_RUN_FATAL;
    else if (ctx->run_report.error_count > 0) ctx->run_report.overall_status = EVAL_RUN_OK_WITH_ERRORS;
    else if (ctx->run_report.warning_count > 0) ctx->run_report.overall_status = EVAL_RUN_OK_WITH_WARNINGS;
    else ctx->run_report.overall_status = EVAL_RUN_OK;
}
