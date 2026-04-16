#ifndef BUILD_MODEL_QUERY_H_
#define BUILD_MODEL_QUERY_H_

#include "build_model_types.h"
#include "bm_compile_features.h"

typedef struct BM_Query_Session BM_Query_Session;

typedef enum {
    BM_QUERY_USAGE_COMPILE = 0,
    BM_QUERY_USAGE_LINK,
} BM_Query_Usage_Mode;

typedef struct {
    String_View config;
    String_View platform_id;
    String_View compile_language;
    BM_Target_Id current_target_id;
    BM_Query_Usage_Mode usage_mode;
    bool build_interface_active;
    bool install_interface_active;
} BM_Query_Eval_Context;

typedef struct {
    size_t effective_item_hits;
    size_t effective_item_misses;
    size_t effective_value_hits;
    size_t effective_value_misses;
    size_t target_file_hits;
    size_t target_file_misses;
    size_t imported_link_language_hits;
    size_t imported_link_language_misses;
} BM_Query_Session_Stats;

BM_Query_Session *bm_query_session_create(Arena *arena, const Build_Model *model);
const BM_Query_Session_Stats *bm_query_session_stats(const BM_Query_Session *session);

bool bm_model_has_project(const Build_Model *model);
bool bm_target_id_is_valid(BM_Target_Id id);
bool bm_build_step_id_is_valid(BM_Build_Step_Id id);
bool bm_replay_action_id_is_valid(BM_Replay_Action_Id id);
bool bm_directory_id_is_valid(BM_Directory_Id id);
bool bm_test_id_is_valid(BM_Test_Id id);
bool bm_export_id_is_valid(BM_Export_Id id);
bool bm_package_id_is_valid(BM_Package_Id id);

size_t bm_query_directory_count(const Build_Model *model);
size_t bm_query_target_count(const Build_Model *model);
size_t bm_query_build_step_count(const Build_Model *model);
size_t bm_query_replay_action_count(const Build_Model *model);
size_t bm_query_test_count(const Build_Model *model);
size_t bm_query_install_rule_count(const Build_Model *model);
size_t bm_query_export_count(const Build_Model *model);
size_t bm_query_package_count(const Build_Model *model);
size_t bm_query_cpack_install_type_count(const Build_Model *model);
size_t bm_query_cpack_component_group_count(const Build_Model *model);
size_t bm_query_cpack_component_count(const Build_Model *model);
size_t bm_query_cpack_package_count(const Build_Model *model);

String_View bm_query_project_name(const Build_Model *model);
String_View bm_query_project_version(const Build_Model *model);
BM_String_Span bm_query_project_languages(const Build_Model *model);

BM_Directory_Id bm_query_root_directory(const Build_Model *model);
BM_Directory_Id bm_query_directory_parent(const Build_Model *model, BM_Directory_Id id);
String_View bm_query_directory_source_dir(const Build_Model *model, BM_Directory_Id id);
String_View bm_query_directory_binary_dir(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_include_directories_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_system_include_directories_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_link_libraries_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_link_directories_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_compile_definitions_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_compile_options_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_link_options_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Span bm_query_directory_raw_property_items(const Build_Model *model, BM_Directory_Id id, String_View property_name);

BM_Target_Id bm_query_target_by_name(const Build_Model *model, String_View name);
BM_Test_Id bm_query_test_by_name(const Build_Model *model, String_View name);
BM_Package_Id bm_query_package_by_name(const Build_Model *model, String_View name);

BM_String_Item_Span bm_query_global_include_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_system_include_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_link_libraries_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_link_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_compile_definitions_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_compile_options_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_link_options_raw(const Build_Model *model);
BM_String_Span bm_query_global_raw_property_items(const Build_Model *model, String_View property_name);

String_View bm_query_target_name(const Build_Model *model, BM_Target_Id id);
BM_Target_Kind bm_query_target_kind(const Build_Model *model, BM_Target_Id id);
BM_Directory_Id bm_query_target_owner_directory(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_is_imported(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_is_imported_global(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_is_alias(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_is_alias_global(const Build_Model *model, BM_Target_Id id);
BM_Target_Id bm_query_target_alias_of(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_exclude_from_all(const Build_Model *model, BM_Target_Id id);
BM_String_Span bm_query_target_sources_raw(const Build_Model *model, BM_Target_Id id);
size_t bm_query_target_source_count(const Build_Model *model, BM_Target_Id id);
BM_Target_Source_Kind bm_query_target_source_kind(const Build_Model *model, BM_Target_Id id, size_t source_index);
BM_Visibility bm_query_target_source_visibility(const Build_Model *model, BM_Target_Id id, size_t source_index);
String_View bm_query_target_source_raw(const Build_Model *model, BM_Target_Id id, size_t source_index);
String_View bm_query_target_source_effective(const Build_Model *model, BM_Target_Id id, size_t source_index);
bool bm_query_target_source_generated(const Build_Model *model, BM_Target_Id id, size_t source_index);
bool bm_query_target_source_is_compile_input(const Build_Model *model, BM_Target_Id id, size_t source_index);
bool bm_query_target_source_header_file_only(const Build_Model *model, BM_Target_Id id, size_t source_index);
String_View bm_query_target_source_language(const Build_Model *model, BM_Target_Id id, size_t source_index);
BM_String_Item_Span bm_query_target_source_compile_definitions(const Build_Model *model, BM_Target_Id id, size_t source_index);
BM_String_Item_Span bm_query_target_source_compile_options(const Build_Model *model, BM_Target_Id id, size_t source_index);
BM_String_Item_Span bm_query_target_source_include_directories(const Build_Model *model, BM_Target_Id id, size_t source_index);
String_View bm_query_target_source_file_set_name(const Build_Model *model, BM_Target_Id id, size_t source_index);
BM_String_Span bm_query_target_source_raw_property_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         size_t source_index,
                                                         String_View property_name);
BM_Build_Step_Id bm_query_target_source_producer_step(const Build_Model *model, BM_Target_Id id, size_t source_index);
size_t bm_query_target_file_set_count(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_file_set_name(const Build_Model *model, BM_Target_Id id, size_t file_set_index);
BM_Target_File_Set_Kind bm_query_target_file_set_kind(const Build_Model *model, BM_Target_Id id, size_t file_set_index);
BM_Visibility bm_query_target_file_set_visibility(const Build_Model *model, BM_Target_Id id, size_t file_set_index);
BM_String_Span bm_query_target_file_set_base_dirs(const Build_Model *model, BM_Target_Id id, size_t file_set_index);
BM_String_Span bm_query_target_file_set_files_raw(const Build_Model *model, BM_Target_Id id, size_t file_set_index);
BM_String_Span bm_query_target_file_set_files_effective(const Build_Model *model, BM_Target_Id id, size_t file_set_index);
BM_Target_Id_Span bm_query_target_dependencies_explicit(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_libraries_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_include_directories_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_compile_definitions_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_compile_options_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_compile_features_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_options_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_directories_raw(const Build_Model *model, BM_Target_Id id);
size_t bm_query_target_raw_property_count(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_raw_property_name(const Build_Model *model, BM_Target_Id id, size_t property_index);
BM_String_Span bm_query_target_raw_property_items(const Build_Model *model, BM_Target_Id id, String_View property_name);
bool bm_query_target_property_value(const Build_Model *model,
                                    BM_Target_Id id,
                                    String_View property_name,
                                    Arena *scratch,
                                    String_View *out);
String_View bm_query_target_output_name(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_prefix(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_suffix(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_archive_output_directory(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_library_output_directory(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_runtime_output_directory(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_folder(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_c_standard(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_c_standard_required(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_c_extensions(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_cxx_standard(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_cxx_standard_required(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_cxx_extensions(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_win32_executable(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_macosx_bundle(const Build_Model *model, BM_Target_Id id);

BM_Build_Step_Kind bm_query_build_step_kind(const Build_Model *model, BM_Build_Step_Id id);
BM_Directory_Id bm_query_build_step_owner_directory(const Build_Model *model, BM_Build_Step_Id id);
BM_Target_Id bm_query_build_step_owner_target(const Build_Model *model, BM_Build_Step_Id id);
bool bm_query_build_step_append(const Build_Model *model, BM_Build_Step_Id id);
bool bm_query_build_step_verbatim(const Build_Model *model, BM_Build_Step_Id id);
bool bm_query_build_step_uses_terminal(const Build_Model *model, BM_Build_Step_Id id);
bool bm_query_build_step_command_expand_lists(const Build_Model *model, BM_Build_Step_Id id);
bool bm_query_build_step_depends_explicit_only(const Build_Model *model, BM_Build_Step_Id id);
bool bm_query_build_step_codegen(const Build_Model *model, BM_Build_Step_Id id);
String_View bm_query_build_step_working_directory(const Build_Model *model, BM_Build_Step_Id id);
String_View bm_query_build_step_comment(const Build_Model *model, BM_Build_Step_Id id);
String_View bm_query_build_step_main_dependency(const Build_Model *model, BM_Build_Step_Id id);
String_View bm_query_build_step_depfile(const Build_Model *model, BM_Build_Step_Id id);
String_View bm_query_build_step_job_pool(const Build_Model *model, BM_Build_Step_Id id);
String_View bm_query_build_step_job_server_aware(const Build_Model *model, BM_Build_Step_Id id);
BM_String_Span bm_query_build_step_outputs_raw(const Build_Model *model, BM_Build_Step_Id id);
BM_String_Span bm_query_build_step_outputs(const Build_Model *model, BM_Build_Step_Id id);
BM_String_Span bm_query_build_step_byproducts_raw(const Build_Model *model, BM_Build_Step_Id id);
BM_String_Span bm_query_build_step_byproducts(const Build_Model *model, BM_Build_Step_Id id);
BM_String_Span bm_query_build_step_dependency_tokens_raw(const Build_Model *model, BM_Build_Step_Id id);
BM_Target_Id_Span bm_query_build_step_target_dependencies(const Build_Model *model, BM_Build_Step_Id id);
BM_Build_Step_Id_Span bm_query_build_step_producer_dependencies(const Build_Model *model, BM_Build_Step_Id id);
BM_String_Span bm_query_build_step_file_dependencies(const Build_Model *model, BM_Build_Step_Id id);
size_t bm_query_build_step_command_count(const Build_Model *model, BM_Build_Step_Id id);
BM_String_Span bm_query_build_step_command_argv(const Build_Model *model, BM_Build_Step_Id id, size_t command_index);

BM_Replay_Action_Kind bm_query_replay_action_kind(const Build_Model *model, BM_Replay_Action_Id id);
BM_Replay_Opcode bm_query_replay_action_opcode(const Build_Model *model, BM_Replay_Action_Id id);
BM_Replay_Phase bm_query_replay_action_phase(const Build_Model *model, BM_Replay_Action_Id id);
BM_Directory_Id bm_query_replay_action_owner_directory(const Build_Model *model, BM_Replay_Action_Id id);
String_View bm_query_replay_action_working_directory(const Build_Model *model, BM_Replay_Action_Id id);
BM_String_Span bm_query_replay_action_inputs(const Build_Model *model, BM_Replay_Action_Id id);
BM_String_Span bm_query_replay_action_outputs(const Build_Model *model, BM_Replay_Action_Id id);
BM_String_Span bm_query_replay_action_argv(const Build_Model *model, BM_Replay_Action_Id id);
BM_String_Span bm_query_replay_action_environment(const Build_Model *model, BM_Replay_Action_Id id);

bool bm_query_target_effective_include_directories_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         Arena *scratch,
                                                         BM_String_Item_Span *out);
bool bm_query_target_effective_include_directories_items_with_context(const Build_Model *model,
                                                                      BM_Target_Id id,
                                                                      const BM_Query_Eval_Context *ctx,
                                                                      Arena *scratch,
                                                                      BM_String_Item_Span *out);
bool bm_query_target_effective_compile_definitions_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         Arena *scratch,
                                                         BM_String_Item_Span *out);
bool bm_query_target_effective_compile_definitions_items_with_context(const Build_Model *model,
                                                                      BM_Target_Id id,
                                                                      const BM_Query_Eval_Context *ctx,
                                                                      Arena *scratch,
                                                                      BM_String_Item_Span *out);
bool bm_query_target_effective_compile_options_items(const Build_Model *model,
                                                     BM_Target_Id id,
                                                     Arena *scratch,
                                                     BM_String_Item_Span *out);
bool bm_query_target_effective_compile_options_items_with_context(const Build_Model *model,
                                                                  BM_Target_Id id,
                                                                  const BM_Query_Eval_Context *ctx,
                                                                  Arena *scratch,
                                                                  BM_String_Item_Span *out);
bool bm_query_target_effective_link_libraries_items(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    Arena *scratch,
                                                    BM_String_Item_Span *out);
bool bm_query_target_effective_link_libraries_items_with_context(const Build_Model *model,
                                                                 BM_Target_Id id,
                                                                 const BM_Query_Eval_Context *ctx,
                                                                 Arena *scratch,
                                                                 BM_String_Item_Span *out);
bool bm_query_target_effective_link_options_items(const Build_Model *model,
                                                  BM_Target_Id id,
                                                  Arena *scratch,
                                                  BM_String_Item_Span *out);
bool bm_query_target_effective_link_options_items_with_context(const Build_Model *model,
                                                               BM_Target_Id id,
                                                               const BM_Query_Eval_Context *ctx,
                                                               Arena *scratch,
                                                               BM_String_Item_Span *out);
bool bm_query_target_effective_link_directories_items(const Build_Model *model,
                                                      BM_Target_Id id,
                                                      Arena *scratch,
                                                      BM_String_Item_Span *out);
bool bm_query_target_effective_link_directories_items_with_context(const Build_Model *model,
                                                                   BM_Target_Id id,
                                                                   const BM_Query_Eval_Context *ctx,
                                                                   Arena *scratch,
                                                                   BM_String_Item_Span *out);
bool bm_query_target_effective_compile_features_items(const Build_Model *model,
                                                      BM_Target_Id id,
                                                      const BM_Query_Eval_Context *ctx,
                                                      Arena *scratch,
                                                      BM_String_Item_Span *out);
bool bm_query_target_effective_include_directories(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out);
bool bm_query_target_effective_include_directories_with_context(const Build_Model *model,
                                                                BM_Target_Id id,
                                                                const BM_Query_Eval_Context *ctx,
                                                                Arena *scratch,
                                                                BM_String_Span *out);
bool bm_query_target_effective_compile_definitions(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out);
bool bm_query_target_effective_compile_definitions_with_context(const Build_Model *model,
                                                                BM_Target_Id id,
                                                                const BM_Query_Eval_Context *ctx,
                                                                Arena *scratch,
                                                                BM_String_Span *out);
bool bm_query_target_effective_compile_options(const Build_Model *model,
                                               BM_Target_Id id,
                                               Arena *scratch,
                                               BM_String_Span *out);
bool bm_query_target_effective_compile_options_with_context(const Build_Model *model,
                                                            BM_Target_Id id,
                                                            const BM_Query_Eval_Context *ctx,
                                                            Arena *scratch,
                                                            BM_String_Span *out);
bool bm_query_target_effective_link_libraries(const Build_Model *model,
                                              BM_Target_Id id,
                                              Arena *scratch,
                                              BM_String_Span *out);
bool bm_query_target_effective_link_libraries_with_context(const Build_Model *model,
                                                           BM_Target_Id id,
                                                           const BM_Query_Eval_Context *ctx,
                                                           Arena *scratch,
                                                           BM_String_Span *out);
bool bm_query_target_effective_link_options(const Build_Model *model,
                                            BM_Target_Id id,
                                            Arena *scratch,
                                            BM_String_Span *out);
bool bm_query_target_effective_link_options_with_context(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         const BM_Query_Eval_Context *ctx,
                                                         Arena *scratch,
                                                         BM_String_Span *out);
bool bm_query_target_effective_link_directories(const Build_Model *model,
                                                BM_Target_Id id,
                                                Arena *scratch,
                                                BM_String_Span *out);
bool bm_query_target_effective_link_directories_with_context(const Build_Model *model,
                                                             BM_Target_Id id,
                                                             const BM_Query_Eval_Context *ctx,
                                                             Arena *scratch,
                                                             BM_String_Span *out);
bool bm_query_target_effective_compile_features(const Build_Model *model,
                                                BM_Target_Id id,
                                                const BM_Query_Eval_Context *ctx,
                                                Arena *scratch,
                                                BM_String_Span *out);
bool bm_query_session_target_effective_include_directories_items(BM_Query_Session *session,
                                                                 BM_Target_Id id,
                                                                 const BM_Query_Eval_Context *ctx,
                                                                 BM_String_Item_Span *out);
bool bm_query_session_target_effective_compile_definitions_items(BM_Query_Session *session,
                                                                 BM_Target_Id id,
                                                                 const BM_Query_Eval_Context *ctx,
                                                                 BM_String_Item_Span *out);
bool bm_query_session_target_effective_compile_options_items(BM_Query_Session *session,
                                                             BM_Target_Id id,
                                                             const BM_Query_Eval_Context *ctx,
                                                             BM_String_Item_Span *out);
bool bm_query_session_target_effective_link_libraries_items(BM_Query_Session *session,
                                                            BM_Target_Id id,
                                                            const BM_Query_Eval_Context *ctx,
                                                            BM_String_Item_Span *out);
bool bm_query_session_target_effective_link_options_items(BM_Query_Session *session,
                                                          BM_Target_Id id,
                                                          const BM_Query_Eval_Context *ctx,
                                                          BM_String_Item_Span *out);
bool bm_query_session_target_effective_link_directories_items(BM_Query_Session *session,
                                                              BM_Target_Id id,
                                                              const BM_Query_Eval_Context *ctx,
                                                              BM_String_Item_Span *out);
bool bm_query_session_target_effective_compile_features_items(BM_Query_Session *session,
                                                              BM_Target_Id id,
                                                              const BM_Query_Eval_Context *ctx,
                                                              BM_String_Item_Span *out);
bool bm_query_session_target_effective_include_directories(BM_Query_Session *session,
                                                           BM_Target_Id id,
                                                           const BM_Query_Eval_Context *ctx,
                                                           BM_String_Span *out);
bool bm_query_session_target_effective_compile_definitions(BM_Query_Session *session,
                                                           BM_Target_Id id,
                                                           const BM_Query_Eval_Context *ctx,
                                                           BM_String_Span *out);
bool bm_query_session_target_effective_compile_options(BM_Query_Session *session,
                                                       BM_Target_Id id,
                                                       const BM_Query_Eval_Context *ctx,
                                                       BM_String_Span *out);
bool bm_query_session_target_effective_link_libraries(BM_Query_Session *session,
                                                      BM_Target_Id id,
                                                      const BM_Query_Eval_Context *ctx,
                                                      BM_String_Span *out);
bool bm_query_session_target_effective_link_options(BM_Query_Session *session,
                                                    BM_Target_Id id,
                                                    const BM_Query_Eval_Context *ctx,
                                                    BM_String_Span *out);
bool bm_query_session_target_effective_link_directories(BM_Query_Session *session,
                                                        BM_Target_Id id,
                                                        const BM_Query_Eval_Context *ctx,
                                                        BM_String_Span *out);
bool bm_query_session_target_effective_compile_features(BM_Query_Session *session,
                                                        BM_Target_Id id,
                                                        const BM_Query_Eval_Context *ctx,
                                                        BM_String_Span *out);
bool bm_query_target_effective_file(const Build_Model *model,
                                    BM_Target_Id id,
                                    const BM_Query_Eval_Context *ctx,
                                    Arena *scratch,
                                    String_View *out);
bool bm_query_target_effective_linker_file(const Build_Model *model,
                                           BM_Target_Id id,
                                           const BM_Query_Eval_Context *ctx,
                                           Arena *scratch,
                                           String_View *out);
bool bm_query_target_imported_link_languages(const Build_Model *model,
                                             BM_Target_Id id,
                                             const BM_Query_Eval_Context *ctx,
                                             Arena *scratch,
                                             BM_String_Span *out);
bool bm_query_session_target_effective_file(BM_Query_Session *session,
                                            BM_Target_Id id,
                                            const BM_Query_Eval_Context *ctx,
                                            String_View *out);
bool bm_query_session_target_effective_linker_file(BM_Query_Session *session,
                                                   BM_Target_Id id,
                                                   const BM_Query_Eval_Context *ctx,
                                                   String_View *out);
bool bm_query_session_target_imported_link_languages(BM_Query_Session *session,
                                                     BM_Target_Id id,
                                                     const BM_Query_Eval_Context *ctx,
                                                     BM_String_Span *out);

bool bm_query_testing_enabled(const Build_Model *model);
String_View bm_query_test_name(const Build_Model *model, BM_Test_Id id);
String_View bm_query_test_command(const Build_Model *model, BM_Test_Id id);
BM_Directory_Id bm_query_test_owner_directory(const Build_Model *model, BM_Test_Id id);
String_View bm_query_test_working_directory(const Build_Model *model, BM_Test_Id id);
bool bm_query_test_command_expand_lists(const Build_Model *model, BM_Test_Id id);
BM_String_Span bm_query_test_configurations(const Build_Model *model, BM_Test_Id id);

BM_Install_Rule_Kind bm_query_install_rule_kind(const Build_Model *model, BM_Install_Rule_Id id);
BM_Directory_Id bm_query_install_rule_owner_directory(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_item_raw(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_destination(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_component(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_namelink_component(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_export_name(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_archive_destination(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_library_destination(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_runtime_destination(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_includes_destination(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_public_header_destination(const Build_Model *model, BM_Install_Rule_Id id);
BM_Target_Id bm_query_install_rule_target(const Build_Model *model, BM_Install_Rule_Id id);
BM_Install_Rule_Id bm_query_install_rule_for_export_target(const Build_Model *model,
                                                           BM_Export_Id export_id,
                                                           BM_Target_Id target_id);
BM_Install_Rule_Id_Span bm_query_install_rules_for_component(const Build_Model *model,
                                                             String_View component,
                                                             Arena *scratch);

BM_Export_Kind bm_query_export_kind(const Build_Model *model, BM_Export_Id id);
BM_Export_Source_Kind bm_query_export_source_kind(const Build_Model *model, BM_Export_Id id);
BM_Directory_Id bm_query_export_owner_directory(const Build_Model *model, BM_Export_Id id);
String_View bm_query_export_name(const Build_Model *model, BM_Export_Id id);
String_View bm_query_export_namespace(const Build_Model *model, BM_Export_Id id);
String_View bm_query_export_destination(const Build_Model *model, BM_Export_Id id);
String_View bm_query_export_file_name(const Build_Model *model, BM_Export_Id id);
String_View bm_query_export_output_file_path(const Build_Model *model, BM_Export_Id id, Arena *scratch);
String_View bm_query_export_component(const Build_Model *model, BM_Export_Id id);
BM_Target_Id_Span bm_query_export_targets(const Build_Model *model, BM_Export_Id id);
bool bm_query_export_enabled(const Build_Model *model, BM_Export_Id id);
String_View bm_query_export_package_name(const Build_Model *model, BM_Export_Id id);
String_View bm_query_export_registry_prefix(const Build_Model *model, BM_Export_Id id);
String_View bm_query_export_cxx_modules_directory(const Build_Model *model, BM_Export_Id id);
bool bm_query_export_append(const Build_Model *model, BM_Export_Id id);
BM_Export_Id_Span bm_query_exports_for_component(const Build_Model *model,
                                                 String_View component,
                                                 Arena *scratch);

String_View bm_query_package_name(const Build_Model *model, BM_Package_Id id);
BM_Directory_Id bm_query_package_owner_directory(const Build_Model *model, BM_Package_Id id);
String_View bm_query_package_mode(const Build_Model *model, BM_Package_Id id);
String_View bm_query_package_found_path(const Build_Model *model, BM_Package_Id id);
bool bm_query_package_found(const Build_Model *model, BM_Package_Id id);
bool bm_query_package_required(const Build_Model *model, BM_Package_Id id);
bool bm_query_package_quiet(const Build_Model *model, BM_Package_Id id);

BM_CPack_Install_Type_Id bm_query_cpack_install_type_by_name(const Build_Model *model, String_View name);
String_View bm_query_cpack_install_type_name(const Build_Model *model, BM_CPack_Install_Type_Id id);
String_View bm_query_cpack_install_type_display_name(const Build_Model *model, BM_CPack_Install_Type_Id id);
BM_Directory_Id bm_query_cpack_install_type_owner_directory(const Build_Model *model, BM_CPack_Install_Type_Id id);

BM_CPack_Component_Group_Id bm_query_cpack_component_group_by_name(const Build_Model *model, String_View name);
String_View bm_query_cpack_component_group_name(const Build_Model *model, BM_CPack_Component_Group_Id id);
String_View bm_query_cpack_component_group_display_name(const Build_Model *model, BM_CPack_Component_Group_Id id);
String_View bm_query_cpack_component_group_description(const Build_Model *model, BM_CPack_Component_Group_Id id);
BM_CPack_Component_Group_Id bm_query_cpack_component_group_parent(const Build_Model *model, BM_CPack_Component_Group_Id id);
BM_Directory_Id bm_query_cpack_component_group_owner_directory(const Build_Model *model, BM_CPack_Component_Group_Id id);
bool bm_query_cpack_component_group_expanded(const Build_Model *model, BM_CPack_Component_Group_Id id);
bool bm_query_cpack_component_group_bold_title(const Build_Model *model, BM_CPack_Component_Group_Id id);

BM_CPack_Component_Id bm_query_cpack_component_by_name(const Build_Model *model, String_View name);
String_View bm_query_cpack_component_name(const Build_Model *model, BM_CPack_Component_Id id);
String_View bm_query_cpack_component_display_name(const Build_Model *model, BM_CPack_Component_Id id);
String_View bm_query_cpack_component_description(const Build_Model *model, BM_CPack_Component_Id id);
BM_CPack_Component_Group_Id bm_query_cpack_component_group(const Build_Model *model, BM_CPack_Component_Id id);
BM_CPack_Component_Id_Span bm_query_cpack_component_dependencies(const Build_Model *model, BM_CPack_Component_Id id);
BM_CPack_Install_Type_Id_Span bm_query_cpack_component_install_types(const Build_Model *model, BM_CPack_Component_Id id);
BM_Install_Rule_Id_Span bm_query_cpack_component_install_rules(const Build_Model *model,
                                                               BM_CPack_Component_Id id,
                                                               Arena *scratch);
BM_Export_Id_Span bm_query_cpack_component_exports(const Build_Model *model,
                                                   BM_CPack_Component_Id id,
                                                   Arena *scratch);
String_View bm_query_cpack_component_archive_file(const Build_Model *model, BM_CPack_Component_Id id);
String_View bm_query_cpack_component_plist(const Build_Model *model, BM_CPack_Component_Id id);
bool bm_query_cpack_component_required(const Build_Model *model, BM_CPack_Component_Id id);
bool bm_query_cpack_component_hidden(const Build_Model *model, BM_CPack_Component_Id id);
bool bm_query_cpack_component_disabled(const Build_Model *model, BM_CPack_Component_Id id);
bool bm_query_cpack_component_downloaded(const Build_Model *model, BM_CPack_Component_Id id);
BM_Directory_Id bm_query_cpack_component_owner_directory(const Build_Model *model, BM_CPack_Component_Id id);

BM_Directory_Id bm_query_cpack_package_owner_directory(const Build_Model *model, BM_CPack_Package_Id id);
String_View bm_query_cpack_package_name(const Build_Model *model, BM_CPack_Package_Id id);
String_View bm_query_cpack_package_version(const Build_Model *model, BM_CPack_Package_Id id);
String_View bm_query_cpack_package_file_name(const Build_Model *model, BM_CPack_Package_Id id);
String_View bm_query_cpack_package_output_directory(const Build_Model *model, BM_CPack_Package_Id id, Arena *scratch);
BM_String_Span bm_query_cpack_package_generators(const Build_Model *model, BM_CPack_Package_Id id);
bool bm_query_cpack_package_include_toplevel_directory(const Build_Model *model, BM_CPack_Package_Id id);
bool bm_query_cpack_package_archive_component_install(const Build_Model *model, BM_CPack_Package_Id id);
BM_String_Span bm_query_cpack_package_components_all(const Build_Model *model, BM_CPack_Package_Id id);

#endif
