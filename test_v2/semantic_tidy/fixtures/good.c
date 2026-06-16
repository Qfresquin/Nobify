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
typedef unsigned int BM_Build_Step_Id;
typedef unsigned int BM_Install_Rule_Id;
typedef unsigned int BM_CPack_Package_Id;
typedef struct {
    const char *data;
    unsigned long count;
} String_View;
typedef struct {
    const String_View *items;
    unsigned long count;
} BM_String_Span;
typedef struct Nob_File_Paths Nob_File_Paths;
typedef enum {
    BM_BUILD_STEP_COMMAND_TOOL_LITERAL = 0,
    BM_BUILD_STEP_COMMAND_TOOL_CMAKE,
    BM_BUILD_STEP_COMMAND_TOOL_CPACK,
} BM_Build_Step_Command_Tool;
typedef enum {
    BM_CPACK_COMPONENTS_GROUPING_ONE_PER_GROUP = 0,
    BM_CPACK_COMPONENTS_GROUPING_IGNORE,
    BM_CPACK_COMPONENTS_GROUPING_ALL_COMPONENTS_IN_ONE,
    BM_CPACK_COMPONENTS_GROUPING_INVALID,
} BM_CPack_Components_Grouping;
typedef enum {
    BM_INSTALL_RULE_ITEM_PATH = 0,
    BM_INSTALL_RULE_ITEM_SCRIPT,
    BM_INSTALL_RULE_ITEM_CODE,
    BM_INSTALL_RULE_ITEM_EXPORT_ANDROID_MK,
    BM_INSTALL_RULE_ITEM_TAGGED_UNKNOWN,
} BM_Install_Rule_Item_Kind;
typedef enum {
    BM_COMPILE_FEATURE_LANG_C = 0,
    BM_COMPILE_FEATURE_LANG_CXX,
} BM_Compile_Feature_Lang;
typedef struct {
    int exists;
    int type;
} Eval_Fs_Stat;

static int eval_should_stop(EvalExecContext *ctx);
static Eval_Result eval_result_fatal(void);
static Eval_Result eval_result_from_ctx(EvalExecContext *ctx);
static int eval_result_is_fatal(Eval_Result result);
static Eval_Result eval_result_merge(Eval_Result left, Eval_Result right);
static int eval_service_write_file(EvalExecContext *ctx,
                                   const char *path,
                                   const char *data,
                                   int append);
static int eval_service_read_file(EvalExecContext *ctx, const char *path, const char **out);
static int eval_service_stat(EvalExecContext *ctx, const char *path, int follow_symlinks, Eval_Fs_Stat *out);
static int eval_service_mkdir(EvalExecContext *ctx, const char *path);
static int eval_service_remove(EvalExecContext *ctx, const char *path, int recursive);
static int eval_service_glob(EvalExecContext *ctx, const void *req, void *out);
static char *nob_temp_sprintf(const char *fmt, ...);
static char *copy_to_persistent(const char *value);
static const char *bm_query_target_name(const Build_Model *model, BM_Target_Id id);
static int bm_query_build_step_effective_command_tool(const Build_Model *model,
                                                      BM_Build_Step_Id id,
                                                      unsigned long command_index,
                                                      const void *ctx,
                                                      void *scratch,
                                                      BM_Build_Step_Command_Tool *out);
static BM_CPack_Components_Grouping bm_query_cpack_package_components_grouping_kind(const Build_Model *model,
                                                                                    BM_CPack_Package_Id id);
static BM_Install_Rule_Item_Kind bm_query_install_rule_item_kind(const Build_Model *model,
                                                                 BM_Install_Rule_Id id);
static int bm_query_target_language_extensions_override(const Build_Model *model,
                                                        BM_Target_Id id,
                                                        BM_Compile_Feature_Lang lang,
                                                        int *out_extensions);
static BM_String_Span bm_query_target_public_headers(const Build_Model *model,
                                                     BM_Target_Id id);
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

static int helper_evaluator_host_service_boundary_good(EvalExecContext *ctx,
                                                       const char *path) {
    return eval_service_write_file(ctx, path, "", 0);
}

static int helper_evaluator_host_service_boundary_read_good(EvalExecContext *ctx,
                                                            const char *path,
                                                            const char **out) {
    return eval_service_read_file(ctx, path, out);
}

static int helper_evaluator_host_service_boundary_cache_load_good(EvalExecContext *ctx,
                                                                  const char *path,
                                                                  const char **out) {
    return eval_service_read_file(ctx, path, out);
}

static int helper_evaluator_host_service_boundary_find_item_type_good(EvalExecContext *ctx,
                                                                      const char *path) {
    Eval_Fs_Stat st = {0};
    if (!eval_service_stat(ctx, path, 0, &st) || !st.exists) return 0;
    return st.type;
}

static int helper_evaluator_host_service_boundary_ctest_cleanup_good(EvalExecContext *ctx,
                                                                     const char *path) {
    return eval_service_remove(ctx, path, 1) && eval_service_mkdir(ctx, path);
}

static int helper_evaluator_host_service_boundary_meta_dir_good(EvalExecContext *ctx,
                                                                const void *req,
                                                                void *out) {
    return eval_service_glob(ctx, req, out);
}

static const char *helper_codegen_evaluator_boundary_good(const Build_Model *model,
                                                          BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_codegen_build_model_lifecycle_good(const Build_Model *model,
                                                             BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_codegen_render_host_effect_good(const Build_Model *model,
                                                          BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_codegen_path_resolution_host_effect_good(const Build_Model *model,
                                                                   BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static int helper_pure_layer_ambient_env_good(const void *explicit_options) {
    return explicit_options != 0;
}

static int helper_codegen_build_step_tool_heuristic_good(const Build_Model *model,
                                                         BM_Build_Step_Id id,
                                                         BM_Build_Step_Command_Tool *out) {
    return bm_query_build_step_effective_command_tool(model, id, 0, 0, 0, out);
}

static int helper_codegen_cpack_grouping_heuristic_good(const Build_Model *model,
                                                        BM_CPack_Package_Id id) {
    return bm_query_cpack_package_components_grouping_kind(model, id) != BM_CPACK_COMPONENTS_GROUPING_INVALID;
}

static int helper_codegen_install_pseudo_item_heuristic_good(const Build_Model *model,
                                                             BM_Install_Rule_Id id) {
    return bm_query_install_rule_item_kind(model, id) != BM_INSTALL_RULE_ITEM_TAGGED_UNKNOWN;
}

static int helper_codegen_language_extensions_raw_property_good(const Build_Model *model,
                                                                BM_Target_Id id,
                                                                int *out_extensions) {
    return bm_query_target_language_extensions_override(model, id, BM_COMPILE_FEATURE_LANG_CXX, out_extensions);
}

static int helper_codegen_public_header_raw_property_good(const Build_Model *model,
                                                          BM_Target_Id id) {
    return bm_query_target_public_headers(model, id).count > 0;
}

static const char *helper_codegen_public_host_effect_good(const Build_Model *model,
                                                          BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_pipeline_orchestration_boundary_good(const Build_Model *model,
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
