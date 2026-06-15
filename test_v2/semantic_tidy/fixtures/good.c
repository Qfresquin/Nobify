typedef enum {
    EVAL_RESULT_OK = 0,
    EVAL_RESULT_FATAL,
} Eval_Result_Kind;

typedef struct {
    Eval_Result_Kind kind;
} Eval_Result;

typedef struct {
    int oom;
    int stop_requested;
    struct {
        const char *value;
    } semantic_state;
} EvalExecContext;

typedef struct Build_Model Build_Model;
typedef struct Build_Model_Draft Build_Model_Draft;
typedef struct BM_Builder BM_Builder;
typedef unsigned int BM_Target_Id;
typedef struct Nob_File_Paths Nob_File_Paths;

static int eval_should_stop(EvalExecContext *ctx);
static Eval_Result eval_result_fatal(void);
static Eval_Result eval_result_from_ctx(EvalExecContext *ctx);
static int eval_result_is_fatal(Eval_Result result);
static Eval_Result eval_result_merge(Eval_Result left, Eval_Result right);
static char *nob_temp_sprintf(const char *fmt, ...);
static char *copy_to_persistent(const char *value);
static const char *bm_query_target_name(const Build_Model *model, BM_Target_Id id);
static int eval_fs_glob(EvalExecContext *ctx, const char *pattern, Nob_File_Paths *out);

Eval_Result eval_handle_good(EvalExecContext *ctx, const void *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Eval_Result result = eval_result_from_ctx(ctx);
    if (eval_result_is_fatal(result)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_good_merge(EvalExecContext *ctx, const void *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Eval_Result left = eval_result_from_ctx(ctx);
    Eval_Result right = eval_result_from_ctx(ctx);
    return eval_result_merge(left, right);
}

static _Bool helper_has_real_success(EvalExecContext *ctx, _Bool ok) {
    if (eval_should_stop(ctx)) return 0;
    return ok;
}

static void helper_lifetime_good(EvalExecContext *ctx) {
    char *tmp = nob_temp_sprintf("%s", "value");
    ctx->semantic_state.value = copy_to_persistent(tmp);
}

static const char *helper_build_model_good(const Build_Model *model, BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_codegen_boundary_good(const Build_Model *model, BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_build_model_dependency_good(const Build_Model *model, BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static int helper_builder_records_fact_good(const Build_Model *model) {
    return model != 0;
}

static int helper_evaluator_build_model_boundary_good(EvalExecContext *ctx) {
    return ctx != 0;
}

static int helper_evaluator_build_model_lifecycle_good(EvalExecContext *ctx) {
    return ctx != 0;
}

static const char *helper_codegen_evaluator_boundary_good(const Build_Model *model,
                                                          BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_codegen_build_model_lifecycle_good(const Build_Model *model,
                                                             BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_query_readonly_good(const Build_Model *model, BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_query_frozen_boundary_good(const Build_Model *model, BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static int helper_validate_readonly_good(const Build_Model_Draft *draft) {
    return draft != 0;
}

static int helper_validate_draft_boundary_good(const Build_Model_Draft *draft) {
    return draft != 0;
}

static int helper_freeze_builder_boundary_good(const Build_Model_Draft *draft) {
    return draft != 0;
}

static int helper_builder_upstream_boundary_good(const void *stream) {
    return stream != 0;
}

static int helper_file_handler_enumeration_good(EvalExecContext *ctx,
                                                const char *pattern,
                                                Nob_File_Paths *out) {
    return eval_fs_glob(ctx, pattern, out);
}
