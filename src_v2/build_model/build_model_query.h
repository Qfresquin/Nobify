#ifndef BUILD_MODEL_QUERY_H_
#define BUILD_MODEL_QUERY_H_

#include "build_model_types.h"

bool bm_model_has_project(const Build_Model *model);
bool bm_target_id_is_valid(BM_Target_Id id);
bool bm_directory_id_is_valid(BM_Directory_Id id);
bool bm_test_id_is_valid(BM_Test_Id id);
bool bm_package_id_is_valid(BM_Package_Id id);

size_t bm_query_directory_count(const Build_Model *model);
size_t bm_query_target_count(const Build_Model *model);
size_t bm_query_test_count(const Build_Model *model);
size_t bm_query_install_rule_count(const Build_Model *model);
size_t bm_query_package_count(const Build_Model *model);
size_t bm_query_cpack_install_type_count(const Build_Model *model);
size_t bm_query_cpack_component_group_count(const Build_Model *model);
size_t bm_query_cpack_component_count(const Build_Model *model);

String_View bm_query_project_name(const Build_Model *model);
String_View bm_query_project_version(const Build_Model *model);
BM_String_Span bm_query_project_languages(const Build_Model *model);

BM_Directory_Id bm_query_root_directory(const Build_Model *model);
BM_Directory_Id bm_query_directory_parent(const Build_Model *model, BM_Directory_Id id);
String_View bm_query_directory_source_dir(const Build_Model *model, BM_Directory_Id id);
String_View bm_query_directory_binary_dir(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_include_directories_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_system_include_directories_raw(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_link_directories_raw(const Build_Model *model, BM_Directory_Id id);

BM_Target_Id bm_query_target_by_name(const Build_Model *model, String_View name);
BM_Test_Id bm_query_test_by_name(const Build_Model *model, String_View name);
BM_Package_Id bm_query_package_by_name(const Build_Model *model, String_View name);

BM_String_Item_Span bm_query_global_include_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_system_include_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_link_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_compile_definitions_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_compile_options_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_link_options_raw(const Build_Model *model);
BM_String_Span bm_query_global_raw_property_items(const Build_Model *model, String_View property_name);

String_View bm_query_target_name(const Build_Model *model, BM_Target_Id id);
BM_Target_Kind bm_query_target_kind(const Build_Model *model, BM_Target_Id id);
BM_Directory_Id bm_query_target_owner_directory(const Build_Model *model, BM_Target_Id id);
BM_String_Span bm_query_target_sources_raw(const Build_Model *model, BM_Target_Id id);
BM_Target_Id_Span bm_query_target_dependencies_explicit(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_libraries_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_include_directories_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_compile_definitions_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_compile_options_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_options_raw(const Build_Model *model, BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_directories_raw(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_output_name(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_prefix(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_suffix(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_archive_output_directory(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_library_output_directory(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_runtime_output_directory(const Build_Model *model, BM_Target_Id id);
String_View bm_query_target_folder(const Build_Model *model, BM_Target_Id id);

bool bm_query_target_effective_include_directories(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out);
bool bm_query_target_effective_compile_definitions(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out);
bool bm_query_target_effective_link_libraries(const Build_Model *model,
                                              BM_Target_Id id,
                                              Arena *scratch,
                                              BM_String_Span *out);

bool bm_query_testing_enabled(const Build_Model *model);
String_View bm_query_test_name(const Build_Model *model, BM_Test_Id id);
String_View bm_query_test_command(const Build_Model *model, BM_Test_Id id);

BM_Install_Rule_Kind bm_query_install_rule_kind(const Build_Model *model, BM_Install_Rule_Id id);
BM_Directory_Id bm_query_install_rule_owner_directory(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_item_raw(const Build_Model *model, BM_Install_Rule_Id id);
String_View bm_query_install_rule_destination(const Build_Model *model, BM_Install_Rule_Id id);
BM_Target_Id bm_query_install_rule_target(const Build_Model *model, BM_Install_Rule_Id id);

String_View bm_query_package_name(const Build_Model *model, BM_Package_Id id);
BM_Directory_Id bm_query_package_owner_directory(const Build_Model *model, BM_Package_Id id);
String_View bm_query_package_mode(const Build_Model *model, BM_Package_Id id);
String_View bm_query_package_found_path(const Build_Model *model, BM_Package_Id id);
bool bm_query_package_found(const Build_Model *model, BM_Package_Id id);
bool bm_query_package_required(const Build_Model *model, BM_Package_Id id);
bool bm_query_package_quiet(const Build_Model *model, BM_Package_Id id);

String_View bm_query_cpack_install_type_name(const Build_Model *model, BM_CPack_Install_Type_Id id);
String_View bm_query_cpack_install_type_display_name(const Build_Model *model, BM_CPack_Install_Type_Id id);
BM_Directory_Id bm_query_cpack_install_type_owner_directory(const Build_Model *model, BM_CPack_Install_Type_Id id);

String_View bm_query_cpack_component_group_name(const Build_Model *model, BM_CPack_Component_Group_Id id);
String_View bm_query_cpack_component_group_display_name(const Build_Model *model, BM_CPack_Component_Group_Id id);
String_View bm_query_cpack_component_group_description(const Build_Model *model, BM_CPack_Component_Group_Id id);
BM_CPack_Component_Group_Id bm_query_cpack_component_group_parent(const Build_Model *model, BM_CPack_Component_Group_Id id);
BM_Directory_Id bm_query_cpack_component_group_owner_directory(const Build_Model *model, BM_CPack_Component_Group_Id id);
bool bm_query_cpack_component_group_expanded(const Build_Model *model, BM_CPack_Component_Group_Id id);
bool bm_query_cpack_component_group_bold_title(const Build_Model *model, BM_CPack_Component_Group_Id id);

String_View bm_query_cpack_component_name(const Build_Model *model, BM_CPack_Component_Id id);
String_View bm_query_cpack_component_display_name(const Build_Model *model, BM_CPack_Component_Id id);
String_View bm_query_cpack_component_description(const Build_Model *model, BM_CPack_Component_Id id);
BM_CPack_Component_Group_Id bm_query_cpack_component_group(const Build_Model *model, BM_CPack_Component_Id id);
BM_CPack_Component_Id_Span bm_query_cpack_component_dependencies(const Build_Model *model, BM_CPack_Component_Id id);
BM_CPack_Install_Type_Id_Span bm_query_cpack_component_install_types(const Build_Model *model, BM_CPack_Component_Id id);
String_View bm_query_cpack_component_archive_file(const Build_Model *model, BM_CPack_Component_Id id);
String_View bm_query_cpack_component_plist(const Build_Model *model, BM_CPack_Component_Id id);
bool bm_query_cpack_component_required(const Build_Model *model, BM_CPack_Component_Id id);
bool bm_query_cpack_component_hidden(const Build_Model *model, BM_CPack_Component_Id id);
bool bm_query_cpack_component_disabled(const Build_Model *model, BM_CPack_Component_Id id);
bool bm_query_cpack_component_downloaded(const Build_Model *model, BM_CPack_Component_Id id);
BM_Directory_Id bm_query_cpack_component_owner_directory(const Build_Model *model, BM_CPack_Component_Id id);

#endif
