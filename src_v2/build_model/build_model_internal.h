#ifndef BUILD_MODEL_INTERNAL_H_
#define BUILD_MODEL_INTERNAL_H_

#include <ctype.h>
#include <string.h>

#include "arena_dyn.h"
#include "build_model_builder.h"
#include "build_model_freeze.h"
#include "build_model_query.h"
#include "build_model_validate.h"
#include "../diagnostics/diagnostics.h"

typedef struct {
    String_View name;
    uint32_t id;
} BM_Name_Index_Entry;

typedef struct BM_Raw_Property_Record BM_Raw_Property_Record;

typedef struct {
    BM_Target_Source_Kind kind;
    BM_Visibility visibility;
    String_View raw_path;
    String_View effective_path;
    String_View file_set_name;
    bool generated;
    bool header_file_only;
    String_View language;
    BM_Build_Step_Id producer_step_id;
    BM_String_Item_View *compile_definitions;
    BM_String_Item_View *compile_options;
    BM_String_Item_View *include_directories;
    BM_Raw_Property_Record *raw_properties;
    BM_Provenance provenance;
} BM_Target_Source_Record;

typedef struct {
    String_View name;
    BM_Target_File_Set_Kind kind;
    BM_Visibility visibility;
    String_View *base_dirs;
    size_t *source_indices;
    String_View *raw_files;
    String_View *effective_files;
    BM_Provenance provenance;
} BM_Target_File_Set_Record;

typedef struct {
    String_View *argv;
} BM_Build_Step_Command_Record;

typedef struct {
    BM_Build_Step_Id id;
    String_View step_key;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    BM_Build_Step_Kind kind;
    String_View owner_target_name;
    BM_Target_Id owner_target_id;
    bool append;
    bool verbatim;
    bool uses_terminal;
    bool command_expand_lists;
    bool depends_explicit_only;
    bool codegen;
    String_View working_directory;
    String_View comment;
    String_View main_dependency;
    String_View depfile;
    String_View job_pool;
    String_View job_server_aware;
    String_View *raw_outputs;
    String_View *effective_outputs;
    String_View *raw_byproducts;
    String_View *effective_byproducts;
    String_View *raw_dependency_tokens;
    BM_Target_Id *resolved_target_dependencies;
    BM_Build_Step_Id *resolved_producer_dependencies;
    String_View *resolved_file_dependencies;
    BM_Build_Step_Command_Record *commands;
} BM_Build_Step_Record;

typedef struct {
    BM_Replay_Action_Id id;
    String_View action_key;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    BM_Replay_Action_Kind kind;
    BM_Replay_Opcode opcode;
    BM_Replay_Phase phase;
    String_View working_directory;
    String_View *inputs;
    String_View *outputs;
    String_View *argv;
    String_View *environment;
} BM_Replay_Action_Record;

typedef struct {
    String_View path;
    String_View directory_source_dir;
    String_View directory_binary_dir;
    bool generated;
    BM_Provenance provenance;
} BM_Source_Generated_Mark_Record;

typedef struct {
    String_View path;
    String_View directory_source_dir;
    String_View directory_binary_dir;
    String_View key;
    String_View value;
    Event_Property_Mutate_Op op;
    BM_Provenance provenance;
} BM_Source_Property_Mutation_Record;

struct BM_Raw_Property_Record {
    String_View name;
    Event_Property_Mutate_Op op;
    uint32_t flags;
    String_View *items;
    BM_Provenance provenance;
};

typedef struct {
    bool present;
    String_View name;
    String_View version;
    String_View description;
    String_View homepage_url;
    String_View *languages;
    BM_Provenance declaration_provenance;
    String_View minimum_required_version;
    bool minimum_required_fatal_if_too_old;
    BM_Provenance minimum_required_provenance;
} BM_Project_Record;

typedef struct {
    BM_String_Item_View *include_directories;
    BM_String_Item_View *system_include_directories;
    BM_String_Item_View *link_libraries;
    BM_String_Item_View *link_directories;
    BM_String_Item_View *compile_definitions;
    BM_String_Item_View *compile_options;
    BM_String_Item_View *link_options;
    BM_Raw_Property_Record *raw_properties;
} BM_Global_Property_State;

typedef struct {
    BM_Directory_Id id;
    BM_Directory_Id parent_id;
    BM_Directory_Id owner_directory_id;
    String_View source_dir;
    String_View binary_dir;
    BM_Provenance provenance;
    BM_String_Item_View *include_directories;
    BM_String_Item_View *system_include_directories;
    BM_String_Item_View *link_libraries;
    BM_String_Item_View *link_directories;
    BM_String_Item_View *compile_definitions;
    BM_String_Item_View *compile_options;
    BM_String_Item_View *link_options;
    BM_Raw_Property_Record *raw_properties;
} BM_Directory_Record;

typedef struct {
    BM_Target_Id id;
    String_View name;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    BM_Target_Kind kind;
    bool declared;
    bool imported;
    bool imported_global;
    bool alias;
    bool alias_global;
    bool exclude_from_all;
    bool win32_executable;
    bool macosx_bundle;
    String_View alias_of_name;
    BM_Target_Id alias_of_id;
    String_View *sources;
    BM_Target_Source_Record *source_records;
    BM_Target_File_Set_Record *file_sets;
    String_View *explicit_dependency_names;
    BM_Target_Id *explicit_dependency_ids;
    BM_String_Item_View *link_libraries;
    BM_String_Item_View *link_options;
    BM_String_Item_View *link_directories;
    BM_String_Item_View *include_directories;
    BM_String_Item_View *compile_definitions;
    BM_String_Item_View *compile_options;
    BM_String_Item_View *compile_features;
    String_View output_name;
    String_View prefix;
    String_View suffix;
    String_View archive_output_directory;
    String_View library_output_directory;
    String_View runtime_output_directory;
    String_View folder;
    BM_Raw_Property_Record *raw_properties;
} BM_Target_Record;

typedef struct {
    BM_Test_Id id;
    String_View name;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    String_View command;
    String_View working_dir;
    bool command_expand_lists;
    String_View *configurations;
} BM_Test_Record;

typedef struct {
    BM_Install_Rule_Id id;
    BM_Install_Rule_Kind kind;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    String_View item;
    String_View destination;
    String_View component;
    String_View namelink_component;
    String_View export_name;
    String_View archive_destination;
    String_View library_destination;
    String_View runtime_destination;
    String_View includes_destination;
    String_View public_header_destination;
    BM_Target_Id resolved_target_id;
} BM_Install_Rule_Record;

typedef struct {
    BM_Export_Id id;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    BM_Export_Kind kind;
    BM_Export_Source_Kind source_kind;
    String_View export_key;
    String_View name;
    String_View export_namespace;
    String_View destination;
    String_View file_name;
    String_View component;
    String_View output_file_path;
    String_View cxx_modules_directory;
    String_View registry_prefix;
    bool enabled;
    bool append;
    String_View *target_names;
    BM_Target_Id *target_ids;
} BM_Export_Record;

typedef struct {
    BM_Package_Id id;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    String_View package_name;
    String_View mode;
    String_View found_path;
    bool found;
    bool required;
    bool quiet;
} BM_Package_Record;

typedef struct {
    BM_CPack_Install_Type_Id id;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    String_View name;
    String_View display_name;
} BM_CPack_Install_Type_Record;

typedef struct {
    BM_CPack_Component_Group_Id id;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    String_View name;
    String_View display_name;
    String_View description;
    String_View parent_group_name;
    BM_CPack_Component_Group_Id parent_group_id;
    bool expanded;
    bool bold_title;
} BM_CPack_Component_Group_Record;

typedef struct {
    BM_CPack_Component_Id id;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    String_View name;
    String_View display_name;
    String_View description;
    String_View group_name;
    BM_CPack_Component_Group_Id group_id;
    String_View *dependency_names;
    BM_CPack_Component_Id *dependency_ids;
    String_View *install_type_names;
    BM_CPack_Install_Type_Id *install_type_ids;
    String_View archive_file;
    String_View plist;
    bool required;
    bool hidden;
    bool disabled;
    bool downloaded;
} BM_CPack_Component_Record;

typedef struct {
    BM_CPack_Package_Id id;
    BM_Directory_Id owner_directory_id;
    BM_Provenance provenance;
    String_View package_key;
    String_View package_name;
    String_View package_version;
    String_View package_file_name;
    String_View package_directory;
    String_View *generators;
    String_View *components_all;
    bool include_toplevel_directory;
    bool archive_component_install;
} BM_CPack_Package_Record;

struct Build_Model_Draft {
    Arena *arena;
    Diag_Sink *sink;
    bool has_semantic_entities;
    bool testing_enabled;
    BM_Project_Record project;
    BM_Global_Property_State global_properties;
    BM_Directory_Record *directories;
    BM_Directory_Id root_directory_id;
    BM_Target_Record *targets;
    BM_Build_Step_Record *build_steps;
    BM_Replay_Action_Record *replay_actions;
    BM_Source_Generated_Mark_Record *generated_source_marks;
    BM_Source_Property_Mutation_Record *source_property_mutations;
    BM_Test_Record *tests;
    BM_Install_Rule_Record *install_rules;
    BM_Export_Record *exports;
    BM_Package_Record *packages;
    BM_CPack_Install_Type_Record *cpack_install_types;
    BM_CPack_Component_Group_Record *cpack_component_groups;
    BM_CPack_Component_Record *cpack_components;
    BM_CPack_Package_Record *cpack_packages;
    BM_Name_Index_Entry *target_name_index;
    BM_Name_Index_Entry *test_name_index;
    BM_Name_Index_Entry *package_name_index;
};

struct Build_Model {
    Arena *arena;
    bool testing_enabled;
    BM_Project_Record project;
    BM_Global_Property_State global_properties;
    BM_Directory_Record *directories;
    BM_Directory_Id root_directory_id;
    BM_Target_Record *targets;
    BM_Build_Step_Record *build_steps;
    BM_Replay_Action_Record *replay_actions;
    BM_Test_Record *tests;
    BM_Install_Rule_Record *install_rules;
    BM_Export_Record *exports;
    BM_Package_Record *packages;
    BM_CPack_Install_Type_Record *cpack_install_types;
    BM_CPack_Component_Group_Record *cpack_component_groups;
    BM_CPack_Component_Record *cpack_components;
    BM_CPack_Package_Record *cpack_packages;
    BM_Name_Index_Entry *target_name_index;
    BM_Name_Index_Entry *test_name_index;
    BM_Name_Index_Entry *package_name_index;
};

struct BM_Builder {
    Arena *arena;
    Diag_Sink *sink;
    bool has_fatal_error;
    Build_Model_Draft *draft;
    BM_Directory_Id *directory_stack;
};

bool bm_copy_string(Arena *arena, String_View input, String_View *out);
BM_Provenance bm_provenance_from_event(Arena *arena, const Event *ev);
bool bm_split_cmake_list(Arena *arena, String_View raw, String_View **out_items);
bool bm_add_name_index(Arena *arena, BM_Name_Index_Entry **index, String_View name, uint32_t id);
bool bm_sv_eq_ci_lit(String_View sv, const char *lit);
bool bm_sv_truthy(String_View sv);
bool bm_string_view_is_empty(String_View sv);
bool bm_path_is_abs(String_View path);
bool bm_normalize_path(Arena *arena, String_View path, String_View *out);
bool bm_path_join(Arena *arena, String_View lhs, String_View rhs, String_View *out);
bool bm_path_rebase(Arena *arena, String_View base_dir, String_View path, String_View *out);
BM_Target_Kind bm_target_kind_from_event(Cmake_Target_Type type);
BM_Build_Step_Kind bm_build_step_kind_from_event(Event_Build_Step_Kind kind);
BM_Replay_Phase bm_replay_phase_from_event(Event_Replay_Phase phase);
BM_Replay_Action_Kind bm_replay_action_kind_from_event(Event_Replay_Action_Kind kind);
BM_Replay_Opcode bm_replay_opcode_from_event(Event_Replay_Opcode opcode);
BM_Visibility bm_visibility_from_event(Cmake_Visibility visibility);
BM_Install_Rule_Kind bm_install_rule_kind_from_event(Cmake_Install_Rule_Type kind);
BM_Directory_Id bm_builder_current_directory_id(const BM_Builder *builder);
BM_Directory_Record *bm_draft_get_directory(Build_Model_Draft *draft, BM_Directory_Id id);
const BM_Directory_Record *bm_draft_get_directory_const(const Build_Model_Draft *draft, BM_Directory_Id id);
BM_Build_Step_Record *bm_draft_find_build_step(Build_Model_Draft *draft, String_View step_key);
const BM_Build_Step_Record *bm_draft_find_build_step_const(const Build_Model_Draft *draft, String_View step_key);
BM_Replay_Action_Record *bm_draft_find_replay_action(Build_Model_Draft *draft, String_View action_key);
const BM_Replay_Action_Record *bm_draft_find_replay_action_const(const Build_Model_Draft *draft,
                                                                 String_View action_key);
BM_Target_Record *bm_draft_find_target(Build_Model_Draft *draft, String_View name);
const BM_Target_Record *bm_draft_find_target_const(const Build_Model_Draft *draft, String_View name);
BM_Target_Id bm_draft_find_target_id(const Build_Model_Draft *draft, String_View name);
BM_Test_Id bm_draft_find_test_id(const Build_Model_Draft *draft, String_View name);
BM_Package_Id bm_draft_find_package_id(const Build_Model_Draft *draft, String_View name);
BM_CPack_Install_Type_Id bm_draft_find_install_type_id(const Build_Model_Draft *draft, String_View name);
BM_CPack_Component_Group_Id bm_draft_find_component_group_id(const Build_Model_Draft *draft, String_View name);
BM_CPack_Component_Id bm_draft_find_component_id(const Build_Model_Draft *draft, String_View name);
BM_Export_Id bm_draft_find_export_id(const Build_Model_Draft *draft, String_View name);
bool bm_builder_error(BM_Builder *builder, const Event *ev, const char *cause, const char *hint);
void bm_builder_warn(BM_Builder *builder, const Event *ev, const char *cause, const char *hint);
bool bm_diag_error(Diag_Sink *sink,
                   BM_Provenance provenance,
                   const char *component,
                   const char *command,
                   const char *cause,
                   const char *hint);
void bm_diag_warn(Diag_Sink *sink,
                  BM_Provenance provenance,
                  const char *component,
                  const char *command,
                  const char *cause,
                  const char *hint);
bool bm_append_string(Arena *arena, String_View **items, String_View item);
bool bm_append_item(Arena *arena, BM_String_Item_View **items, BM_String_Item_View item);
bool bm_apply_item_mutation(Arena *arena,
                            BM_String_Item_View **dest,
                            const BM_String_Item_View *items,
                            size_t count,
                            Event_Property_Mutate_Op op);
bool bm_record_raw_property(Arena *arena,
                            BM_Raw_Property_Record **records,
                            String_View name,
                            Event_Property_Mutate_Op op,
                            uint32_t flags,
                            const String_View *items,
                            size_t item_count,
                            BM_Provenance provenance);

bool bm_builder_handle_directory_event(BM_Builder *builder, const Event *ev);
bool bm_builder_handle_project_event(BM_Builder *builder, const Event *ev);
bool bm_builder_handle_target_event(BM_Builder *builder, const Event *ev);
bool bm_builder_handle_build_graph_event(BM_Builder *builder, const Event *ev);
bool bm_builder_handle_replay_event(BM_Builder *builder, const Event *ev);
bool bm_builder_handle_test_event(BM_Builder *builder, const Event *ev);
bool bm_builder_handle_install_event(BM_Builder *builder, const Event *ev);
bool bm_builder_handle_export_event(BM_Builder *builder, const Event *ev);
bool bm_builder_handle_package_event(BM_Builder *builder, const Event *ev);
bool bm_validate_explicit_cycles(const Build_Model_Draft *draft,
                                 Arena *scratch,
                                 Diag_Sink *sink,
                                 bool *had_error);
bool bm_validate_execution_graph(const Build_Model *model,
                                 Arena *scratch,
                                 Diag_Sink *sink,
                                 bool *had_error);

#endif
