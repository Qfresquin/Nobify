#ifndef BUILD_MODEL_CUSTOM_COMMAND_H_
#define BUILD_MODEL_CUSTOM_COMMAND_H_

#include "build_model_types.h"

Custom_Command* build_target_add_custom_command_ex(Build_Target *target,
                                                   Arena *arena,
                                                   bool pre_build,
                                                   String_View command,
                                                   String_View working_dir,
                                                   String_View comment);
Custom_Command* build_target_add_custom_command(Build_Target *target,
                                                Arena *arena,
                                                bool pre_build,
                                                String_View command);
Custom_Command* build_model_add_custom_command_output_ex(Build_Model *model,
                                                         Arena *arena,
                                                         String_View command,
                                                         String_View working_dir,
                                                         String_View comment);
Custom_Command* build_model_add_custom_command_output(Build_Model *model,
                                                      Arena *arena,
                                                      String_View output,
                                                      String_View command);
Custom_Command* build_model_find_output_custom_command_by_output(Build_Model *model, String_View output);

void build_custom_command_add_outputs(Custom_Command *cmd, Arena *arena, const String_List *items);
void build_custom_command_add_byproducts(Custom_Command *cmd, Arena *arena, const String_List *items);
void build_custom_command_add_depends(Custom_Command *cmd, Arena *arena, const String_List *items);
void build_custom_command_set_main_dependency(Custom_Command *cmd, String_View value);
void build_custom_command_set_main_dependency_if_empty(Custom_Command *cmd, String_View value);
void build_custom_command_set_depfile(Custom_Command *cmd, String_View value);
void build_custom_command_set_depfile_if_empty(Custom_Command *cmd, String_View value);
void build_custom_command_set_flags(Custom_Command *cmd,
                                    bool append,
                                    bool verbatim,
                                    bool uses_terminal,
                                    bool command_expand_lists,
                                    bool depends_explicit_only,
                                    bool codegen);
void build_custom_command_merge_flags(Custom_Command *cmd,
                                      bool append,
                                      bool verbatim,
                                      bool uses_terminal,
                                      bool command_expand_lists,
                                      bool depends_explicit_only,
                                      bool codegen);
void build_custom_command_add_command(Custom_Command *cmd, Arena *arena, String_View command);
void build_custom_command_append_command(Custom_Command *cmd, Arena *arena, String_View extra);
const String_List* build_custom_command_get_commands(const Custom_Command *cmd);

const Custom_Command* build_model_get_output_custom_commands(const Build_Model *model, size_t *out_count);
const Custom_Command* build_target_get_custom_commands(const Build_Target *target, bool pre_build, size_t *out_count);

bool build_path_is_absolute(String_View path);
String_View build_path_join(Arena *arena, String_View base, String_View rel);
String_View build_path_parent_dir(Arena *arena, String_View full_path);
String_View build_path_make_absolute(Arena *arena, String_View path);

#endif // BUILD_MODEL_CUSTOM_COMMAND_H_

