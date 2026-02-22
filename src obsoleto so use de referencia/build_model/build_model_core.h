#ifndef BUILD_MODEL_CORE_H_
#define BUILD_MODEL_CORE_H_

#include "build_model_types.h"
#include <stdio.h>

Build_Model* build_model_create(Arena *arena);

Build_Target* build_model_add_target(Build_Model *model, String_View name, Target_Type type);
Build_Target* build_model_find_target(Build_Model *model, String_View name);
int build_model_find_target_index(const Build_Model *model, String_View name);

void build_target_add_source(Build_Target *target, Arena *arena, String_View source);
void build_target_add_dependency(Build_Target *target, Arena *arena, String_View dep_name);
void build_target_add_object_dependency(Build_Target *target, Arena *arena, String_View dep_target_name);
void build_target_add_interface_dependency(Build_Target *target, Arena *arena, String_View dep_name);

void build_target_add_definition(Build_Target *target,
                                 Arena *arena,
                                 String_View definition,
                                 Visibility visibility,
                                 Build_Config config);
void build_target_add_include_directory(Build_Target *target,
                                        Arena *arena,
                                        String_View directory,
                                        Visibility visibility,
                                        Build_Config config);
void build_target_add_library(Build_Target *target,
                              Arena *arena,
                              String_View library,
                              Visibility visibility);
void build_target_add_compile_option(Build_Target *target,
                                     Arena *arena,
                                     String_View option,
                                     Visibility visibility,
                                     Build_Config config);
void build_target_add_link_option(Build_Target *target,
                                  Arena *arena,
                                  String_View option,
                                  Visibility visibility,
                                  Build_Config config);
void build_target_add_link_directory(Build_Target *target,
                                     Arena *arena,
                                     String_View directory,
                                     Visibility visibility,
                                     Build_Config config);

void build_target_add_conditional_compile_definition(Build_Target *target,
                                                     Arena *arena,
                                                     String_View definition,
                                                     Logic_Node *condition);
void build_target_add_conditional_compile_option(Build_Target *target,
                                                 Arena *arena,
                                                 String_View option,
                                                 Logic_Node *condition);
void build_target_add_conditional_include_directory(Build_Target *target,
                                                    Arena *arena,
                                                    String_View directory,
                                                    Logic_Node *condition);
void build_target_add_conditional_link_library(Build_Target *target,
                                               Arena *arena,
                                               String_View library,
                                               Logic_Node *condition);
void build_target_add_conditional_link_option(Build_Target *target,
                                              Arena *arena,
                                              String_View option,
                                              Logic_Node *condition);
void build_target_add_conditional_link_directory(Build_Target *target,
                                                 Arena *arena,
                                                 String_View directory,
                                                 Logic_Node *condition);

void build_target_collect_effective_compile_definitions(Build_Target *target,
                                                        Arena *arena,
                                                        const Logic_Eval_Context *logic_ctx,
                                                        String_List *out);
void build_target_collect_effective_compile_options(Build_Target *target,
                                                    Arena *arena,
                                                    const Logic_Eval_Context *logic_ctx,
                                                    String_List *out);
void build_target_collect_effective_include_directories(Build_Target *target,
                                                        Arena *arena,
                                                        const Logic_Eval_Context *logic_ctx,
                                                        String_List *out);
void build_target_collect_effective_link_libraries(Build_Target *target,
                                                   Arena *arena,
                                                   const Logic_Eval_Context *logic_ctx,
                                                   String_List *out);
void build_target_collect_effective_link_options(Build_Target *target,
                                                 Arena *arena,
                                                 const Logic_Eval_Context *logic_ctx,
                                                 String_List *out);
void build_target_collect_effective_link_directories(Build_Target *target,
                                                     Arena *arena,
                                                     const Logic_Eval_Context *logic_ctx,
                                                     String_List *out);

void build_target_set_property(Build_Target *target,
                               Arena *arena,
                               String_View key,
                               String_View value);
String_View build_target_get_property(Build_Target *target, String_View key);
void build_target_set_property_smart(Build_Target *target,
                                     Arena *arena,
                                     String_View key,
                                     String_View value);
String_View build_target_get_property_computed(Build_Target *target,
                                               String_View key,
                                               String_View default_config);

Found_Package* build_model_add_package(Build_Model *model, String_View name, bool found);

Build_Test* build_model_add_test(Build_Model *model,
                                 String_View name,
                                 String_View command,
                                 String_View working_directory,
                                 bool command_expand_lists);
Build_Test* build_model_find_test_by_name(Build_Model *model, String_View test_name);

void build_model_set_cache_variable(Build_Model *model,
                                    String_View key,
                                    String_View value,
                                    String_View type,
                                    String_View docstring);
String_View build_model_get_cache_variable(Build_Model *model, String_View key);
bool build_model_has_cache_variable(const Build_Model *model, String_View key);
bool build_model_unset_cache_variable(Build_Model *model, String_View key);
void build_model_set_env_var(Build_Model *model, Arena *arena, String_View key, String_View value);
String_View build_model_get_env_var(const Build_Model *model, String_View key);
bool build_model_has_env_var(const Build_Model *model, String_View key);
bool build_model_unset_env_var(Build_Model *model, String_View key);

bool build_model_validate_dependencies(Build_Model *model);
Build_Target** build_model_topological_sort(Build_Model *model, size_t *count);
void build_model_dump(Build_Model *model, FILE *output);

void build_target_set_flag(Build_Target *target, Target_Flag flag, bool value);
void build_target_set_alias(Build_Target *target, Arena *arena, String_View aliased_name);

void build_model_set_project_info(Build_Model *model, String_View name, String_View version);
void build_model_set_default_config(Build_Model *model, String_View config);
void build_model_enable_language(Build_Model *model, Arena *arena, String_View lang);
void build_model_set_testing_enabled(Build_Model *model, bool enabled);
void build_model_set_install_enabled(Build_Model *model, bool enabled);

void build_model_add_global_definition(Build_Model *model, Arena *arena, String_View def);
void build_model_add_global_compile_option(Build_Model *model, Arena *arena, String_View opt);
void build_model_add_global_link_option(Build_Model *model, Arena *arena, String_View opt);
void build_model_process_global_definition_arg(Build_Model *model, Arena *arena, String_View arg);
void build_model_add_include_directory(Build_Model *model, Arena *arena, String_View dir, bool is_system);
void build_model_add_link_directory(Build_Model *model, Arena *arena, String_View dir);
void build_model_add_global_link_library(Build_Model *model, Arena *arena, String_View lib);
void build_model_remove_global_definition(Build_Model *model, String_View def);

void build_model_set_install_prefix(Build_Model *model, String_View prefix);
void build_model_add_install_rule(Build_Model *model,
                                  Arena *arena,
                                  Install_Rule_Type type,
                                  String_View item,
                                  String_View destination);

Build_Config build_model_config_from_string(String_View cfg);
String_View build_model_config_suffix(Build_Config cfg);
String_View build_model_get_default_config(const Build_Model *model);
Arena* build_model_get_arena(Build_Model *model);
bool build_model_is_windows(const Build_Model *model);
bool build_model_is_unix(const Build_Model *model);
bool build_model_is_apple(const Build_Model *model);
bool build_model_is_linux(const Build_Model *model);
String_View build_model_get_system_name(const Build_Model *model);
String_View build_model_get_project_name(const Build_Model *model);
String_View build_model_get_project_version(const Build_Model *model);
const String_List* build_model_get_string_list(const Build_Model *model, Build_Model_List_Kind kind);
const String_List* build_model_get_install_rule_list(const Build_Model *model, Install_Rule_Type type);
size_t build_model_get_cache_variable_count(const Build_Model *model);
String_View build_model_get_cache_variable_name_at(const Build_Model *model, size_t index);
size_t build_model_get_target_count(const Build_Model *model);
Build_Target* build_model_get_target_at(Build_Model *model, size_t index);
String_View build_model_get_install_prefix(const Build_Model *model);
bool build_model_has_install_prefix(const Build_Model *model);
bool build_model_is_testing_enabled(const Build_Model *model);
size_t build_model_get_test_count(const Build_Model *model);
Build_Test* build_model_get_test_at(Build_Model *model, size_t index);
String_View build_target_get_name(const Build_Target *target);
Target_Type build_target_get_type(const Build_Target *target);
bool build_target_has_source(const Build_Target *target, String_View source);
const String_List* build_target_get_string_list(const Build_Target *target, Build_Target_List_Kind kind);
void build_target_reset_derived_property(Build_Target *target, Build_Target_Derived_Property_Kind kind);
bool build_target_is_exclude_from_all(const Build_Target *target);

String_View build_test_get_name(const Build_Test *test);
String_View build_test_get_command(const Build_Test *test);
String_View build_test_get_working_directory(const Build_Test *test);
bool build_test_get_command_expand_lists(const Build_Test *test);
void build_test_set_command_expand_lists(Build_Test *test, bool value);

#endif // BUILD_MODEL_CORE_H_

