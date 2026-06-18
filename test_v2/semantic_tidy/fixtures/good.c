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
typedef unsigned int BM_Test_Id;
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
    BM_CPACK_GENERATOR_TGZ = 0,
    BM_CPACK_GENERATOR_TXZ,
    BM_CPACK_GENERATOR_ZIP,
    BM_CPACK_GENERATOR_UNSUPPORTED,
} BM_CPack_Generator_Kind;
typedef enum {
    BM_INSTALL_RULE_ITEM_PATH = 0,
    BM_INSTALL_RULE_ITEM_SCRIPT,
    BM_INSTALL_RULE_ITEM_CODE,
    BM_INSTALL_RULE_ITEM_EXPORT_ANDROID_MK,
    BM_INSTALL_RULE_ITEM_TAGGED_UNKNOWN,
} BM_Install_Rule_Item_Kind;
typedef enum {
    BM_TARGET_EXECUTABLE = 0,
    BM_TARGET_STATIC_LIBRARY,
    BM_TARGET_SHARED_LIBRARY,
    BM_TARGET_MODULE_LIBRARY,
    BM_TARGET_INTERFACE_LIBRARY,
    BM_TARGET_OBJECT_LIBRARY,
    BM_TARGET_UTILITY,
    BM_TARGET_UNKNOWN_LIBRARY,
} BM_Target_Kind;
typedef enum {
    BM_TARGET_LINK_INPUT_USAGE_ONLY = 0,
    BM_TARGET_LINK_INPUT_LINKABLE_ARTIFACT,
    BM_TARGET_LINK_INPUT_MODULE_LIBRARY,
    BM_TARGET_LINK_INPUT_IMPORTED_EXECUTABLE,
    BM_TARGET_LINK_INPUT_NOT_LINKABLE,
} BM_Target_Link_Input_Kind;
typedef enum {
    BM_TARGET_BUILD_EMISSION_NONE = 0,
    BM_TARGET_BUILD_EMISSION_ORDER_ONLY,
    BM_TARGET_BUILD_EMISSION_UTILITY,
    BM_TARGET_BUILD_EMISSION_STATIC_ARCHIVE,
    BM_TARGET_BUILD_EMISSION_LINK_EXECUTABLE,
    BM_TARGET_BUILD_EMISSION_LINK_SHARED_LIBRARY,
    BM_TARGET_BUILD_EMISSION_LINK_MODULE_LIBRARY,
    BM_TARGET_BUILD_EMISSION_UNSUPPORTED,
} BM_Target_Build_Emission_Kind;
typedef struct {
    int emits_artifact;
    int uses_archiver;
    int uses_linker;
    int requires_link_paths;
    int requires_position_independent_code_on_posix;
} BM_Target_Build_Emission_Metadata;
typedef enum {
    BM_INSTALL_TARGET_ARTIFACT_NONE = 0,
    BM_INSTALL_TARGET_ARTIFACT_RUNTIME,
    BM_INSTALL_TARGET_ARTIFACT_ARCHIVE,
    BM_INSTALL_TARGET_ARTIFACT_LIBRARY,
} BM_Install_Target_Artifact_Kind;
typedef struct {
    int interface_only;
    int emits_imported_noconfig;
    int emits_link_interface_languages;
    int emits_common_language_runtime;
    int emits_soname;
    int emits_no_soname;
} BM_Install_Export_Target_Metadata;
typedef enum {
    BM_CMAKE_IMPORTED_TARGET_DECL_UNSUPPORTED = 0,
    BM_CMAKE_IMPORTED_TARGET_DECL_EXECUTABLE,
    BM_CMAKE_IMPORTED_TARGET_DECL_LIBRARY,
} BM_CMake_Imported_Target_Declaration_Kind;
typedef struct {
    BM_CMake_Imported_Target_Declaration_Kind kind;
    String_View library_kind;
} BM_CMake_Imported_Target_Declaration;
typedef enum {
    BM_TARGET_SOURCE_LANGUAGE_NONE = 0,
    BM_TARGET_SOURCE_LANGUAGE_C,
    BM_TARGET_SOURCE_LANGUAGE_CXX,
    BM_TARGET_SOURCE_LANGUAGE_UNSUPPORTED,
} BM_Target_Source_Language_Kind;
typedef enum {
    BM_TARGET_LINK_LANGUAGE_NONE = 0,
    BM_TARGET_LINK_LANGUAGE_C,
    BM_TARGET_LINK_LANGUAGE_CXX,
    BM_TARGET_LINK_LANGUAGE_UNSUPPORTED,
} BM_Target_Link_Language_Kind;
typedef enum {
    BM_TARGET_INTERFACE_REQUIREMENT_LINK_LIBRARIES = 0,
} BM_Target_Interface_Requirement_Kind;
typedef enum {
    BM_LINK_ITEM_SPELLING_EMPTY = 0,
    BM_LINK_ITEM_SPELLING_FLAG,
    BM_LINK_ITEM_SPELLING_PATH,
    BM_LINK_ITEM_SPELLING_LINK_FILE,
    BM_LINK_ITEM_SPELLING_BARE_LIBRARY,
    BM_LINK_ITEM_SPELLING_UNSUPPORTED,
} BM_Link_Item_Spelling_Kind;
typedef enum {
    BM_COMPILE_DEFINITION_SPELLING_VALUE = 0,
    BM_COMPILE_DEFINITION_SPELLING_D_FLAG,
} BM_Compile_Definition_Spelling_Kind;
typedef struct {
    BM_Compile_Definition_Spelling_Kind kind;
    String_View value;
} BM_Compile_Definition_Spelling;
typedef enum {
    BM_COMPILE_OPTION_SPELLING_ARGUMENT = 0,
    BM_COMPILE_OPTION_SPELLING_STANDARD_FLAG,
} BM_Compile_Option_Spelling_Kind;
typedef struct {
    BM_Compile_Option_Spelling_Kind kind;
    String_View argument;
    String_View standard;
} BM_Compile_Option_Spelling;
typedef enum {
    BM_LINK_DIRECTORY_SPELLING_PATH = 0,
    BM_LINK_DIRECTORY_SPELLING_L_FLAG,
} BM_Link_Directory_Spelling_Kind;
typedef struct {
    BM_Link_Directory_Spelling_Kind kind;
    String_View path;
} BM_Link_Directory_Spelling;
typedef enum {
    BM_COMPILE_FEATURE_LANG_C = 0,
    BM_COMPILE_FEATURE_LANG_CXX,
} BM_Compile_Feature_Lang;
typedef enum {
    BM_TEST_PROPERTY_DISABLED = 0,
    BM_TEST_PROPERTY_LABELS,
} BM_Test_Property_Kind;
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
static BM_CPack_Generator_Kind bm_query_cpack_package_generator_kind(const Build_Model *model,
                                                                     BM_CPack_Package_Id id,
                                                                     unsigned long generator_index);
static BM_Install_Rule_Item_Kind bm_query_install_rule_item_kind(const Build_Model *model,
                                                                 BM_Install_Rule_Id id);
static int bm_query_target_language_extensions_override(const Build_Model *model,
                                                        BM_Target_Id id,
                                                        BM_Compile_Feature_Lang lang,
                                                        int *out_extensions);
static BM_String_Span bm_query_target_public_headers(const Build_Model *model,
                                                     BM_Target_Id id);
static BM_Target_Source_Language_Kind bm_query_target_source_effective_language_kind(const Build_Model *model,
                                                                                    BM_Target_Id id,
                                                                                    unsigned long source_index);
static int bm_query_target_effective_export_name(const Build_Model *model,
                                                 BM_Target_Id id,
                                                 void *scratch,
                                                 String_View *out);
static int bm_query_export_has_artifact_targets(const Build_Model *model, int export_id);
static BM_Link_Item_Spelling_Kind bm_query_link_item_spelling_kind(String_View value);
static BM_Compile_Definition_Spelling bm_query_compile_definition_spelling(String_View value);
static BM_Compile_Option_Spelling bm_query_compile_option_spelling(String_View value);
static BM_Link_Directory_Spelling bm_query_link_directory_spelling(String_View value);
static int bm_target_kind_is_artifact_target(BM_Target_Kind kind);
static int bm_target_kind_is_non_emitting_build_target(BM_Target_Kind kind);
static int bm_target_kind_has_linkable_artifact(BM_Target_Kind kind);
static int bm_target_kind_requires_position_independent_code(BM_Target_Kind kind);
static int bm_target_kind_is_installable_target(BM_Target_Kind kind);
static BM_Target_Link_Input_Kind bm_target_kind_link_input_kind(BM_Target_Kind kind, int imported);
static BM_Target_Build_Emission_Kind bm_target_build_emission_kind(BM_Target_Kind kind, int imported, int alias);
static BM_Target_Build_Emission_Metadata bm_target_build_emission_metadata(BM_Target_Build_Emission_Kind kind);
static BM_Install_Target_Artifact_Kind bm_target_install_artifact_kind(BM_Target_Kind kind,
                                                                       int windows,
                                                                       int linker_artifact);
static BM_Install_Export_Target_Metadata bm_target_install_export_metadata(BM_Target_Kind kind, int windows);
static BM_CMake_Imported_Target_Declaration bm_target_cmake_imported_declaration(BM_Target_Kind kind);
static String_View bm_query_test_effective_property_kind_first(const Build_Model *model,
                                                               BM_Test_Id id,
                                                               BM_Test_Property_Kind kind,
                                                               void *scratch,
                                                               BM_String_Span *out_span);
static int bm_query_target_interface_requirement_value(const Build_Model *model,
                                                       BM_Target_Id id,
                                                       BM_Target_Interface_Requirement_Kind kind,
                                                       void *scratch,
                                                       String_View *out);
static int bm_query_session_target_effective_link_language_kind(BM_Query_Session *session,
                                                                BM_Target_Id id,
                                                                const BM_Query_Eval_Context *ctx,
                                                                BM_Target_Link_Language_Kind *out);
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

static int helper_codegen_cpack_generator_heuristic_good(const Build_Model *model,
                                                         BM_CPack_Package_Id id) {
    return bm_query_cpack_package_generator_kind(model, id, 0) != BM_CPACK_GENERATOR_UNSUPPORTED;
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

static int helper_codegen_source_language_query_good(const Build_Model *model,
                                                     BM_Target_Id id,
                                                     unsigned long source_index) {
    return bm_query_target_source_effective_language_kind(model, id, source_index) ==
           BM_TARGET_SOURCE_LANGUAGE_CXX;
}

static int helper_codegen_test_property_string_query_good(const Build_Model *model,
                                                          BM_Test_Id id,
                                                          void *scratch) {
    return bm_query_test_effective_property_kind_first(model,
                                                       id,
                                                       BM_TEST_PROPERTY_DISABLED,
                                                       scratch,
                                                       0).count > 0;
}

static int helper_codegen_interface_requirement_string_query_good(const Build_Model *model,
                                                                  BM_Target_Id id,
                                                                  void *scratch,
                                                                  String_View *out) {
    return bm_query_target_interface_requirement_value(model,
                                                       id,
                                                       BM_TARGET_INTERFACE_REQUIREMENT_LINK_LIBRARIES,
                                                       scratch,
                                                       out);
}

static int helper_codegen_compile_definition_spelling_heuristic_good(String_View item) {
    return bm_query_compile_definition_spelling(item).kind == BM_COMPILE_DEFINITION_SPELLING_D_FLAG;
}

static int helper_codegen_compile_option_spelling_heuristic_good(String_View item) {
    return bm_query_compile_option_spelling(item).kind == BM_COMPILE_OPTION_SPELLING_STANDARD_FLAG;
}

static int helper_codegen_link_directory_spelling_heuristic_good(String_View item) {
    return bm_query_link_directory_spelling(item).kind == BM_LINK_DIRECTORY_SPELLING_L_FLAG;
}

static int helper_codegen_target_kind_capability_heuristic_good(BM_Target_Kind kind) {
    return bm_target_kind_is_artifact_target(kind) ||
           bm_target_kind_is_non_emitting_build_target(kind) ||
           bm_target_kind_has_linkable_artifact(kind) ||
           bm_target_kind_requires_position_independent_code(kind);
}

static int helper_codegen_install_target_kind_heuristic_good(BM_Target_Kind kind) {
    return bm_target_kind_is_installable_target(kind);
}

static int helper_codegen_export_artifact_target_heuristic_good(const Build_Model *model, int export_id) {
    return bm_query_export_has_artifact_targets(model, export_id);
}

static int helper_codegen_precompile_header_target_kind_heuristic_good(BM_Target_Kind kind) {
    return !bm_target_kind_is_artifact_target(kind);
}

static int helper_codegen_link_input_target_kind_heuristic_good(BM_Target_Kind kind, int imported) {
    BM_Target_Link_Input_Kind input_kind = bm_target_kind_link_input_kind(kind, imported);
    return input_kind == BM_TARGET_LINK_INPUT_LINKABLE_ARTIFACT ||
           input_kind == BM_TARGET_LINK_INPUT_USAGE_ONLY;
}

static int helper_codegen_build_emission_target_kind_heuristic_good(BM_Target_Kind kind, int imported, int alias) {
    BM_Target_Build_Emission_Kind emission_kind = bm_target_build_emission_kind(kind, imported, alias);
    BM_Target_Build_Emission_Metadata metadata = bm_target_build_emission_metadata(emission_kind);
    return metadata.emits_artifact ||
           metadata.uses_archiver ||
           metadata.uses_linker ||
           metadata.requires_link_paths ||
           metadata.requires_position_independent_code_on_posix ||
           emission_kind == BM_TARGET_BUILD_EMISSION_UTILITY;
}

static int helper_codegen_install_artifact_target_kind_heuristic_good(BM_Target_Kind kind,
                                                                      int windows,
                                                                      int linker_artifact) {
    BM_Install_Target_Artifact_Kind artifact_kind =
        bm_target_install_artifact_kind(kind, windows, linker_artifact);
    return artifact_kind == BM_INSTALL_TARGET_ARTIFACT_RUNTIME ||
           artifact_kind == BM_INSTALL_TARGET_ARTIFACT_ARCHIVE ||
           artifact_kind == BM_INSTALL_TARGET_ARTIFACT_LIBRARY;
}

static int helper_codegen_install_export_metadata_target_kind_heuristic_good(BM_Target_Kind kind,
                                                                             int windows) {
    BM_Install_Export_Target_Metadata metadata = bm_target_install_export_metadata(kind, windows);
    return metadata.interface_only ||
           metadata.emits_imported_noconfig ||
           metadata.emits_link_interface_languages ||
           metadata.emits_common_language_runtime ||
           metadata.emits_soname ||
           metadata.emits_no_soname;
}

static int helper_codegen_cmake_imported_declaration_target_kind_heuristic_good(BM_Target_Kind kind) {
    BM_CMake_Imported_Target_Declaration declaration = bm_target_cmake_imported_declaration(kind);
    return declaration.kind == BM_CMAKE_IMPORTED_TARGET_DECL_EXECUTABLE ||
           declaration.kind == BM_CMAKE_IMPORTED_TARGET_DECL_LIBRARY ||
           declaration.library_kind.count > 0;
}

static int helper_codegen_link_language_string_query_good(BM_Query_Session *session,
                                                          BM_Target_Id id,
                                                          const BM_Query_Eval_Context *ctx) {
    BM_Target_Link_Language_Kind kind = BM_TARGET_LINK_LANGUAGE_NONE;
    if (!bm_query_session_target_effective_link_language_kind(session, id, ctx, &kind)) return 0;
    return kind == BM_TARGET_LINK_LANGUAGE_CXX;
}

static int helper_codegen_export_name_string_query_good(const Build_Model *model,
                                                        BM_Target_Id id,
                                                        void *scratch,
                                                        String_View *out) {
    return bm_query_target_effective_export_name(model, id, scratch, out);
}

static int helper_codegen_link_item_spelling_heuristic_good(String_View item) {
    return bm_query_link_item_spelling_kind(item) == BM_LINK_ITEM_SPELLING_LINK_FILE;
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
