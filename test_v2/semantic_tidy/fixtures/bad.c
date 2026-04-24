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

typedef struct Build_Model {
    int target_count;
} Build_Model;

static int eval_should_stop(EvalExecContext *ctx);
static Eval_Result eval_result_fatal(void);
static Eval_Result eval_result_from_ctx(EvalExecContext *ctx);
static char *nob_temp_sprintf(const char *fmt, ...);

int eval_handle_bad_signature(EvalExecContext *ctx, const void *node) {
    (void)ctx;
    (void)node;
    return 0;
}

Eval_Result eval_handle_bad_discard(EvalExecContext *ctx, const void *node) {
    (void)node;
    eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_bad_unused_local(EvalExecContext *ctx, const void *node) {
    (void)node;
    Eval_Result result = eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_bad_shape_no_guard(EvalExecContext *ctx, const void *node) {
    (void)node;
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_bad_shape_return(EvalExecContext *ctx, const void *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    return (Eval_Result){0};
}

static int helper_bad_eval_result_flatten(EvalExecContext *ctx) {
    Eval_Result result = eval_result_from_ctx(ctx);
    return result.kind;
}

static _Bool helper_bad_stop_direct(EvalExecContext *ctx) {
    return !eval_should_stop(ctx);
}

static _Bool helper_bad_stop_alias(EvalExecContext *ctx) {
    _Bool stopped = eval_should_stop(ctx);
    return stopped;
}

static _Bool helper_bad_stop_control(EvalExecContext *ctx) {
    if (eval_should_stop(ctx)) return 0;
    return 1;
}

static void helper_bad_state_write(EvalExecContext *ctx) {
    EvalExecContext *alias = ctx;
    alias->oom = 1;
}

static int helper_bad_build_model_field(const Build_Model *model) {
    return model->target_count;
}

static void helper_bad_lifetime(EvalExecContext *ctx) {
    char *tmp = nob_temp_sprintf("%s", "value");
    ctx->semantic_state.value = tmp;
}
