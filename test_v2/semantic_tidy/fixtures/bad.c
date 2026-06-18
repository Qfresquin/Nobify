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

typedef struct Build_Model_Draft {
    int target_count;
} Build_Model_Draft;

typedef struct BM_Builder BM_Builder;

typedef enum {
    EVENT_TARGET_DECLARE = 1,
} Event_Kind;

typedef struct Event_Stream Event_Stream;
typedef struct EvalSession EvalSession;
typedef struct EvalRunResult {
    int ok;
} EvalRunResult;
typedef struct Ast_Root {
    int node_count;
} Ast_Root;
typedef struct EvalExec_Request EvalExec_Request;

typedef struct Nob_Codegen_Options Nob_Codegen_Options;
typedef struct Nob_File_Paths Nob_File_Paths;
typedef struct {
    const char *data;
    unsigned long count;
} String_View;
typedef struct {
    const String_View *items;
    unsigned long count;
} BM_String_Span;
typedef struct BM_Query_Session BM_Query_Session;
typedef struct BM_Query_Eval_Context BM_Query_Eval_Context;
typedef unsigned int BM_Target_Id;
typedef unsigned int BM_Build_Step_Id;
typedef unsigned int BM_Install_Rule_Id;
typedef unsigned int BM_CPack_Package_Id;
typedef unsigned int BM_Test_Id;
typedef enum {
    NOB_FILE_REGULAR = 0,
    NOB_FILE_DIRECTORY,
    NOB_FILE_SYMLINK,
} Nob_File_Type;
static int nob_codegen_render(const Build_Model *model, const Nob_Codegen_Options *options);
static int nob_read_entire_dir(const char *path, Nob_File_Paths *out);
static int nob_read_entire_file(const char *path, void *out);
static int nob_write_entire_file(const char *path, const char *data, unsigned long len);
static int nob_file_exists(const char *path);
static Nob_File_Type nob_get_file_type(const char *path);
static int nob_mkdir_if_not_exists(const char *path);
static int nob_walk_dir(const char *path, void *callback);
static const char *bm_query_target_name(const Build_Model *model, BM_Target_Id id);
static int bm_builder_current_directory_id(BM_Builder *builder);
static Build_Model_Draft *bm_builder_finalize(BM_Builder *builder);
static int cg_sv_has_prefix(String_View sv, const char *prefix);
static int cg_ends_with(String_View sv, const char *suffix);
static int cg_target_is_supported_concrete(int kind);
static int cg_target_is_non_emitting(int kind);
static int cg_target_kind_is_linkable_artifact(int kind);
static int cg_target_needs_pic(void *ctx, int kind);
static int bm_target_kind_is_artifact_target(int kind);
static int bm_target_kind_requires_position_independent_code(int kind);
static int bm_target_kind_is_installable_target(int kind);
static int bm_query_target_kind(const Build_Model *model, BM_Target_Id id);
static int bm_target_kind_link_input_kind(int kind, int imported);
static int bm_target_install_artifact_kind(int kind, int windows, int linker_artifact);
static int bm_target_install_export_metadata(int kind, int windows);
static int bm_target_cmake_imported_declaration(int kind);
static int bm_query_target_is_imported(const Build_Model *model, BM_Target_Id id);
static int bm_query_target_is_alias(const Build_Model *model, BM_Target_Id id);
static int bm_target_build_emission_kind(int kind, int imported, int alias);
static int bm_target_build_emission_metadata(int kind);
static String_View bm_query_install_rule_item_raw(const Build_Model *model, BM_Install_Rule_Id id);
static int bm_query_install_rule_item_kind(const Build_Model *model, BM_Install_Rule_Id id);
static BM_String_Span bm_query_cpack_package_generators(const Build_Model *model, BM_CPack_Package_Id id);
static int bm_query_cpack_package_generator_kind(const Build_Model *model,
                                                 BM_CPack_Package_Id id,
                                                 unsigned long generator_index);
static int bm_query_target_property_value(const Build_Model *model,
                                          BM_Target_Id id,
                                          String_View property_name,
                                          void *scratch,
                                          String_View *out);
enum {
    BM_TARGET_EXECUTABLE = 0,
    BM_TARGET_STATIC_LIBRARY = 1,
    BM_TARGET_SHARED_LIBRARY = 2,
    BM_TARGET_MODULE_LIBRARY = 3,
    BM_TARGET_INTERFACE_LIBRARY = 4,
    BM_TARGET_OBJECT_LIBRARY = 5,
    BM_TARGET_UTILITY = 6,
    BM_TARGET_UNKNOWN_LIBRARY = 7,
};
static BM_String_Span bm_query_target_raw_property_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         String_View property_name);
static String_View bm_query_target_source_effective_language(const Build_Model *model,
                                                             BM_Target_Id id,
                                                             unsigned long source_index);
static int bm_query_test_effective_property_items(const Build_Model *model,
                                                  BM_Test_Id id,
                                                  String_View property_name,
                                                  void *scratch,
                                                  BM_String_Span *out);
static int bm_query_target_modeled_property_value(const Build_Model *model,
                                                  BM_Target_Id id,
                                                  String_View property_name,
                                                  void *scratch,
                                                  String_View *out);
static int bm_query_session_target_effective_link_language(BM_Query_Session *session,
                                                           BM_Target_Id id,
                                                           const BM_Query_Eval_Context *ctx,
                                                           String_View *out);
static char *getenv(const char *name);
static long time(long *out);
static int nob_sv_eq(String_View left, String_View right);
static String_View nob_sv_from_cstr(const char *value);
static EvalRunResult eval_session_run(EvalSession *session,
                                      const EvalExec_Request *request,
                                      Ast_Root ast);

static int eval_should_stop(EvalExecContext *ctx);
static Eval_Result eval_result_fatal(void);
static Eval_Result eval_result_from_ctx(EvalExecContext *ctx);
static char *nob_temp_sprintf(const char *fmt, ...);

static int g_bad_semantic_cache;

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

static int helper_bad_codegen_event_ir_boundary(const Event_Stream *stream, Event_Kind kind) {
    (void)stream;
    return kind == EVENT_TARGET_DECLARE;
}

static int helper_bad_build_model_codegen_dependency(const Build_Model *model,
                                                     const Nob_Codegen_Options *options) {
    return nob_codegen_render(model, options);
}

static int helper_bad_file_handler_direct_enumeration(const char *path, Nob_File_Paths *out) {
    return nob_read_entire_dir(path, out);
}

static const char *helper_bad_builder_query_shortcut(const Build_Model *model, BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static const char *helper_bad_evaluator_build_model_dependency(const Build_Model *model,
                                                               BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static int helper_bad_codegen_evaluator_dependency(EvalExecContext *ctx) {
    return eval_should_stop(ctx);
}

static int helper_bad_codegen_parser_dependency(Ast_Root *ast) {
    return ast->node_count;
}

static int helper_bad_parser_downstream_dependency(Event_Stream *stream) {
    return stream != 0;
}

static const char *helper_bad_event_ir_downstream_dependency(const Build_Model *model,
                                                            BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static int helper_bad_evaluator_host_service_boundary(const char *path) {
    if (!nob_file_exists(path)) return nob_write_entire_file(path, "", 0);
    return 1;
}

static int helper_bad_evaluator_host_service_boundary_read(const char *path, void *out) {
    return nob_read_entire_file(path, out);
}

static int helper_bad_evaluator_host_service_boundary_cache_load(const char *path, void *out) {
    return nob_read_entire_file(path, out);
}

static int helper_bad_evaluator_host_service_boundary_find_item_type(const char *path) {
    if (!nob_file_exists(path)) return 0;
    return nob_get_file_type(path) == NOB_FILE_DIRECTORY;
}

static int helper_bad_evaluator_host_service_boundary_program_lookup(const char *path) {
    if (!nob_file_exists(path)) return 0;
    return nob_get_file_type(path) == NOB_FILE_REGULAR;
}

static int helper_bad_evaluator_host_service_boundary_ctest_cleanup(const char *path) {
    return nob_mkdir_if_not_exists(path);
}

static int helper_bad_evaluator_host_service_boundary_meta_dir(const char *path, Nob_File_Paths *out) {
    return nob_read_entire_dir(path, out);
}

static int helper_bad_evaluator_host_service_boundary_builtin_root(const char *path, Nob_File_Paths *out) {
    return nob_read_entire_dir(path, out);
}

static int helper_bad_evaluator_host_service_boundary_walk(const char *path) {
    return nob_walk_dir(path, 0);
}

static int helper_bad_pipeline_orchestration_boundary(EvalSession *session,
                                                      const EvalExec_Request *request,
                                                      Ast_Root ast,
                                                      BM_Builder *builder,
                                                      const Build_Model *model,
                                                      const Nob_Codegen_Options *options) {
    eval_session_run(session, request, ast);
    bm_builder_finalize(builder);
    return nob_codegen_render(model, options);
}

static int helper_bad_codegen_render_host_effect(const char *path) {
    return nob_write_entire_file(path, "", 0);
}

static int helper_bad_codegen_path_resolution_host_effect(const char *path) {
    return nob_file_exists(path);
}

static int helper_bad_pure_layer_ambient_env(const char *name) {
    return getenv(name) != 0;
}

static int helper_bad_pure_layer_host_effect(const char *path) {
    return nob_read_entire_file(path, 0);
}

static long helper_bad_pure_layer_ambient_nondeterminism(void) {
    return time(0);
}

static int helper_bad_codegen_build_step_tool_heuristic(String_View first_arg) {
    return nob_sv_eq(first_arg, nob_sv_from_cstr("cmake")) ||
           nob_sv_eq(first_arg, nob_sv_from_cstr("cpack"));
}

static int helper_bad_codegen_cpack_grouping_heuristic(String_View grouping) {
    return nob_sv_eq(grouping, nob_sv_from_cstr("ONE_PER_GROUP")) ||
           nob_sv_eq(grouping, nob_sv_from_cstr("IGNORE")) ||
           nob_sv_eq(grouping, nob_sv_from_cstr("ALL_COMPONENTS_IN_ONE"));
}

static int helper_bad_codegen_cpack_generator_heuristic(String_View generator) {
    return nob_sv_eq(generator, nob_sv_from_cstr("TGZ")) ||
           nob_sv_eq(generator, nob_sv_from_cstr("TXZ")) ||
           nob_sv_eq(generator, nob_sv_from_cstr("ZIP"));
}

static int helper_bad_codegen_install_pseudo_item_heuristic(String_View item) {
    return cg_sv_has_prefix(item, "SCRIPT::") ||
           cg_sv_has_prefix(item, "CODE::") ||
           cg_sv_has_prefix(item, "EXPORT_ANDROID_MK::");
}

static int helper_bad_codegen_compile_definition_spelling_heuristic(String_View item) {
    return cg_sv_has_prefix(item, "-D");
}

static int helper_bad_codegen_compile_option_spelling_heuristic(String_View item) {
    return cg_sv_has_prefix(item, "-std=");
}

static int helper_bad_codegen_link_directory_spelling_heuristic(String_View item) {
    return cg_sv_has_prefix(item, "-L");
}

static int helper_bad_codegen_language_extensions_raw_property(const Build_Model *model,
                                                               BM_Target_Id id) {
    return bm_query_target_raw_property_items(model, id, nob_sv_from_cstr("CXX_EXTENSIONS")).count > 0 ||
           bm_query_target_raw_property_items(model, id, nob_sv_from_cstr("C_EXTENSIONS")).count > 0;
}

static int helper_bad_codegen_public_header_raw_property(const Build_Model *model,
                                                         BM_Target_Id id) {
    return bm_query_target_raw_property_items(model, id, nob_sv_from_cstr("PUBLIC_HEADER")).count > 0;
}

static int helper_bad_codegen_source_language_query(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    unsigned long source_index) {
    return bm_query_target_source_effective_language(model, id, source_index).count > 0;
}

static int helper_bad_codegen_test_property_string_query(const Build_Model *model,
                                                         BM_Test_Id id,
                                                         void *scratch,
                                                         BM_String_Span *out) {
    return bm_query_test_effective_property_items(model,
                                                  id,
                                                  nob_sv_from_cstr("DISABLED"),
                                                  scratch,
                                                  out);
}

static int helper_bad_codegen_interface_requirement_string_query(const Build_Model *model,
                                                                 BM_Target_Id id,
                                                                 void *scratch,
                                                                 String_View *out) {
    return bm_query_target_modeled_property_value(model,
                                                  id,
                                                  nob_sv_from_cstr("INTERFACE_LINK_LIBRARIES"),
                                                  scratch,
                                                  out);
}

static int helper_bad_codegen_link_language_string_query(BM_Query_Session *session,
                                                         BM_Target_Id id,
                                                         const BM_Query_Eval_Context *ctx) {
    String_View language = {0};
    if (!bm_query_session_target_effective_link_language(session, id, ctx, &language)) return 0;
    return nob_sv_eq(language, nob_sv_from_cstr("CXX"));
}

static int helper_bad_codegen_export_name_string_query(const Build_Model *model,
                                                       BM_Target_Id id,
                                                       void *scratch,
                                                       String_View *out) {
    return bm_query_target_modeled_property_value(model,
                                                  id,
                                                  nob_sv_from_cstr("EXPORT_NAME"),
                                                  scratch,
                                                  out);
}

static int helper_bad_codegen_link_item_spelling_heuristic(String_View item) {
    return cg_ends_with(item, ".a") ||
           cg_ends_with(item, ".lib") ||
           cg_ends_with(item, ".dll") ||
           cg_ends_with(item, ".so") ||
           cg_ends_with(item, ".dylib") ||
           cg_ends_with(item, ".o") ||
           cg_ends_with(item, ".obj");
}

static int helper_bad_codegen_target_kind_capability_heuristic(void *ctx, int kind) {
    return cg_target_is_supported_concrete(kind) ||
           cg_target_is_non_emitting(kind) ||
           cg_target_kind_is_linkable_artifact(kind) ||
           cg_target_needs_pic(ctx, kind);
}

static int helper_bad_codegen_install_target_kind_heuristic(int kind) {
    return kind == BM_TARGET_OBJECT_LIBRARY ||
           kind == BM_TARGET_UTILITY ||
           kind == BM_TARGET_UNKNOWN_LIBRARY;
}

static int helper_bad_codegen_install_target_validation_target_kind_query(const Build_Model *model,
                                                                          BM_Target_Id id) {
    return bm_target_kind_is_installable_target(bm_query_target_kind(model, id));
}

static int helper_bad_codegen_export_artifact_target_heuristic(int kind) {
    return kind != BM_TARGET_INTERFACE_LIBRARY;
}

static int helper_bad_codegen_precompile_header_target_kind_heuristic(int kind) {
    return kind == BM_TARGET_INTERFACE_LIBRARY ||
           kind == BM_TARGET_UTILITY;
}

static int helper_bad_codegen_link_input_target_kind_heuristic(int kind) {
    return kind == BM_TARGET_MODULE_LIBRARY ||
           kind == BM_TARGET_EXECUTABLE;
}

static int helper_bad_codegen_link_input_target_kind_query(int kind, int imported) {
    return bm_target_kind_link_input_kind(kind, imported);
}

static int helper_bad_codegen_build_emission_target_kind_heuristic(int kind) {
    return kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_EXECUTABLE ||
           kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY ||
           kind == BM_TARGET_INTERFACE_LIBRARY ||
           kind == BM_TARGET_UTILITY;
}

static int helper_bad_codegen_build_emission_metadata_target_kind_query(int kind) {
    return bm_target_kind_is_artifact_target(kind) ||
           bm_target_kind_requires_position_independent_code(kind);
}

static int helper_bad_codegen_build_emission_target_kind_query(const Build_Model *model,
                                                               BM_Target_Id id) {
    int kind = bm_query_target_kind(model, id);
    return bm_target_build_emission_metadata(
        bm_target_build_emission_kind(kind,
                                      bm_query_target_is_imported(model, id),
                                      bm_query_target_is_alias(model, id)));
}

static int helper_bad_codegen_install_artifact_target_kind_heuristic(int kind) {
    return kind == BM_TARGET_EXECUTABLE ||
           kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY;
}

static int helper_bad_codegen_install_artifact_target_kind_query(int kind, int windows) {
    return bm_target_install_artifact_kind(kind, windows, 0);
}

static int helper_bad_codegen_install_export_metadata_target_kind_heuristic(int kind) {
    return kind == BM_TARGET_INTERFACE_LIBRARY ||
           kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY;
}

static int helper_bad_codegen_install_export_metadata_target_kind_query(int kind, int windows) {
    return bm_target_install_export_metadata(kind, windows);
}

static int helper_bad_codegen_cmake_imported_declaration_target_kind_heuristic(int kind) {
    return kind == BM_TARGET_EXECUTABLE ||
           kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY ||
           kind == BM_TARGET_INTERFACE_LIBRARY;
}

static int helper_bad_codegen_cmake_imported_declaration_target_kind_query(int kind) {
    return bm_target_cmake_imported_declaration(kind);
}

static int helper_bad_codegen_cmake_imported_declaration_target_kind_query_from_model(const Build_Model *model,
                                                                                      BM_Target_Id id) {
    return bm_target_cmake_imported_declaration(bm_query_target_kind(model, id));
}

static int helper_bad_codegen_typed_build_model_payload_query_install(const Build_Model *model,
                                                                      BM_Install_Rule_Id id) {
    return bm_query_install_rule_item_raw(model, id).count > 0 ||
           bm_query_install_rule_item_kind(model, id) != 0;
}

static int helper_bad_codegen_typed_build_model_payload_query_package(const Build_Model *model,
                                                                      BM_CPack_Package_Id id) {
    BM_String_Span generators = bm_query_cpack_package_generators(model, id);
    return generators.count > 0 && bm_query_cpack_package_generator_kind(model, id, 0) == 0;
}

static int helper_bad_codegen_typed_build_model_payload_query_genex_property(const Build_Model *model,
                                                                             BM_Target_Id id,
                                                                             String_View property_name,
                                                                             void *scratch) {
    String_View out = {0};
    return bm_query_target_property_value(model, id, property_name, scratch, &out);
}

static int helper_bad_codegen_public_host_effect(const char *path) {
    return nob_write_entire_file(path, "", 0);
}

static Build_Model_Draft *helper_bad_codegen_build_model_lifecycle(BM_Builder *builder) {
    return bm_builder_finalize(builder);
}

static Build_Model_Draft *helper_bad_evaluator_build_model_lifecycle(BM_Builder *builder) {
    return bm_builder_finalize(builder);
}

static int helper_bad_query_mutates_frozen_model(Build_Model *model) {
    model->target_count = 1;
    return model->target_count;
}

static int helper_bad_query_frozen_boundary_uses_draft(Build_Model_Draft *draft) {
    return draft->target_count;
}

static int helper_bad_validate_mutates_draft(Build_Model_Draft *draft) {
    draft->target_count = 0;
    return draft->target_count;
}

static const char *helper_bad_validate_draft_boundary_query(const Build_Model *model,
                                                            BM_Target_Id id) {
    return bm_query_target_name(model, id);
}

static int helper_bad_freeze_builder_state(BM_Builder *builder) {
    return bm_builder_current_directory_id(builder);
}

static int helper_bad_builder_upstream_evaluator_shortcut(EvalExecContext *ctx) {
    return eval_should_stop(ctx);
}
