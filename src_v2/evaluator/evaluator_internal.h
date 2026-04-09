// --- START OF FILE evaluator_internal.h ---
#ifndef EVALUATOR_INTERNAL_H_
#define EVALUATOR_INTERNAL_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "nob.h"
#include "arena.h"
#include "arena_dyn.h"
#include "parser.h"
#include "evaluator.h"
#include "eval_policy_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVAL_VAR_CURRENT_LIST_FILE "CMAKE_CURRENT_LIST_FILE"
#define EVAL_VAR_CURRENT_LIST_DIR "CMAKE_CURRENT_LIST_DIR"
#define EVAL_VAR_CURRENT_SOURCE_DIR "CMAKE_CURRENT_SOURCE_DIR"
#define EVAL_VAR_CURRENT_BINARY_DIR "CMAKE_CURRENT_BINARY_DIR"
#define EVAL_VAR_NOBIFY_POLICY_STACK_DEPTH "NOBIFY_POLICY_STACK_DEPTH"
#define EVAL_VAR_NOBIFY_CONTINUE_ON_ERROR "CMAKE_NOBIFY_CONTINUE_ON_ERROR"
#define EVAL_VAR_NOBIFY_COMPAT_PROFILE "CMAKE_NOBIFY_COMPAT_PROFILE"
#define EVAL_VAR_NOBIFY_ERROR_BUDGET "CMAKE_NOBIFY_ERROR_BUDGET"
#define EVAL_VAR_NOBIFY_UNSUPPORTED_POLICY "CMAKE_NOBIFY_UNSUPPORTED_POLICY"
#define EVAL_VAR_NOBIFY_FILE_GLOB_STRICT "CMAKE_NOBIFY_FILE_GLOB_STRICT"
#define EVAL_VAR_NOBIFY_WHILE_MAX_ITERATIONS "CMAKE_NOBIFY_WHILE_MAX_ITERATIONS"
#define EVAL_VAR_NOBIFY_EXPAND_MAX_RECURSION "CMAKE_NOBIFY_EXPAND_MAX_RECURSION"

#define EVAL_POLICY_CMP0005 "CMP0005"
#define EVAL_POLICY_CMP0017 "CMP0017"
#define EVAL_POLICY_CMP0036 "CMP0036"
#define EVAL_POLICY_CMP0048 "CMP0048"
#define EVAL_POLICY_CMP0061 "CMP0061"
#define EVAL_POLICY_CMP0074 "CMP0074"
#define EVAL_POLICY_CMP0077 "CMP0077"
#define EVAL_POLICY_CMP0090 "CMP0090"
#define EVAL_POLICY_CMP0102 "CMP0102"
#define EVAL_POLICY_CMP0124 "CMP0124"
#define EVAL_POLICY_CMP0126 "CMP0126"
#define EVAL_POLICY_CMP0140 "CMP0140"
#define EVAL_POLICY_CMP0144 "CMP0144"
#define EVAL_POLICY_CMP0152 "CMP0152"
#define EVAL_POLICY_CMP0153 "CMP0153"
#define EVAL_POLICY_CMP0174 "CMP0174"

typedef String_View *SV_List;

typedef struct {
    String_View key;
    String_View value;
} Var_Binding;

typedef struct Eval_Var_Entry {
    char *key;
    String_View value;
} Eval_Var_Entry;

typedef struct {
    String_View data;
    String_View type;
    String_View doc;
} Eval_Cache_Value;

typedef struct Eval_Cache_Entry {
    char *key;
    Eval_Cache_Value value;
} Eval_Cache_Entry;

typedef struct Var_Scope {
    Eval_Var_Entry *vars;
} Var_Scope;

typedef enum {
    USER_CMD_FUNCTION = 0,
    USER_CMD_MACRO,
} User_Command_Kind;

typedef struct {
    String_View name;
    User_Command_Kind kind;
    String_View *params;
    Node_List body;
} User_Command;

typedef User_Command *User_Command_List;

typedef struct {
    String_View name;
    String_View declared_dir;
    Cmake_Target_Type target_type;
    bool imported;
    bool imported_global;
    bool alias;
    bool alias_global;
    String_View alias_of;
} Eval_Target_Record;

typedef Eval_Target_Record *Eval_Target_Record_List;

typedef struct {
    String_View name;
    String_View declared_dir;
    String_View working_directory;
} Eval_Test_Record;

typedef Eval_Test_Record *Eval_Test_Record_List;

typedef struct {
    String_View name;
    Eval_Native_Command_Handler handler;
    Eval_Command_Impl_Level implemented_level;
    Eval_Command_Fallback fallback_behavior;
    char *normalized_name;
    bool is_builtin;
} Eval_Native_Command;

typedef Eval_Native_Command *Eval_Native_Command_List;

typedef struct Eval_Native_Command_Index_Entry {
    char *key;
    size_t value;
} Eval_Native_Command_Index_Entry;

typedef Var_Binding *Macro_Binding_List;

typedef struct {
    Macro_Binding_List bindings;
} Macro_Frame;

typedef Macro_Frame *Macro_Frame_Stack;

typedef struct {
    bool variable_scope_pushed;
    bool policy_scope_pushed;
    String_View *propagate_vars;
    bool propagate_on_return;
} Block_Frame;

typedef Block_Frame *Block_Frame_Stack;

typedef struct {
    int guard_kind;
    size_t owner_file_depth;
    size_t owner_function_depth;
    String_View path;
#if defined(_WIN32)
    void *handle;
#else
    int fd;
#endif
} Eval_File_Lock;

typedef Eval_File_Lock *Eval_File_Lock_List;

typedef struct {
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View output_path;
    bool has_input;
    String_View input_path;
    bool has_content;
    String_View content;
    bool has_condition;
    String_View condition;
    bool has_target;
    String_View target;
    bool has_newline_style;
    String_View newline_style;
    bool use_source_permissions;
    bool no_source_permissions;
    bool has_file_permissions;
    unsigned int file_mode;
} Eval_File_Generate_Job;

typedef Eval_File_Generate_Job *Eval_File_Generate_Job_List;

typedef struct {
    unsigned char states[156];
} Eval_Policy_Level;

typedef struct {
    int major;
    int minor;
    int patch;
    int tweak;
} Eval_Semver;

typedef struct {
    unsigned long long total_virtual_mib;
    unsigned long long available_virtual_mib;
    unsigned long long total_physical_mib;
    unsigned long long available_physical_mib;
} Eval_Host_Memory_Info;

typedef enum {
    EVAL_CMDLINE_UNIX = 0,
    EVAL_CMDLINE_WINDOWS,
    EVAL_CMDLINE_NATIVE,
} Eval_Cmdline_Mode;

typedef struct {
    String_View text;
    bool is_set;
} Eval_Process_Env_Value;

typedef struct {
    char *key;
    Eval_Process_Env_Value value;
} Eval_Process_Env_Entry;

typedef Eval_Process_Env_Entry *Eval_Process_Env_Table;

typedef struct {
    String_View scope_upper;
    String_View property_upper;
    bool inherited;
    bool has_brief_docs;
    String_View brief_docs;
    bool has_full_docs;
    String_View full_docs;
    bool has_initialize_from_variable;
    String_View initialize_from_variable;
} Eval_Property_Definition;

typedef Eval_Property_Definition *Eval_Property_Definition_List;

typedef enum {
    EVAL_PROP_QUERY_VALUE = 0,
    EVAL_PROP_QUERY_SET,
    EVAL_PROP_QUERY_DEFINED,
    EVAL_PROP_QUERY_BRIEF_DOCS,
    EVAL_PROP_QUERY_FULL_DOCS,
} Eval_Property_Query_Mode;

typedef enum {
    EVAL_PROP_QUERY_MISSING_UNSET = 0,
    EVAL_PROP_QUERY_MISSING_VAR_NOTFOUND,
    EVAL_PROP_QUERY_MISSING_LITERAL_NOTFOUND,
} Eval_Property_Query_Missing_Behavior;

typedef struct {
    Cmake_Event_Origin origin;
    String_View id;
    String_View command_name;
    Args args;
} Eval_Deferred_Call;

typedef Eval_Deferred_Call *Eval_Deferred_Call_List;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    Eval_Deferred_Call_List calls;
} Eval_Deferred_Dir_Frame;

typedef Eval_Deferred_Dir_Frame *Eval_Deferred_Dir_Frame_Stack;

typedef enum {
    EVAL_RETURN_CTX_TOPLEVEL = 0,
    EVAL_RETURN_CTX_INCLUDE,
    EVAL_RETURN_CTX_FUNCTION,
    EVAL_RETURN_CTX_MACRO,
} Eval_Return_Context;

typedef enum {
    EVAL_FLOW_NONE = 0,
    EVAL_FLOW_BREAK,
    EVAL_FLOW_CONTINUE,
    EVAL_FLOW_RETURN,
} Eval_Flow_Signal;

typedef enum {
    EVAL_EXEC_CTX_ROOT = 0,
    EVAL_EXEC_CTX_INCLUDE,
    EVAL_EXEC_CTX_SUBDIRECTORY,
    EVAL_EXEC_CTX_FUNCTION,
    EVAL_EXEC_CTX_MACRO,
    EVAL_EXEC_CTX_BLOCK,
    EVAL_EXEC_CTX_DEFERRED,
} Eval_Exec_Context_Kind;

typedef struct {
    Eval_Exec_Context_Kind kind;
    Eval_Return_Context return_context;
    String_View source_dir;
    String_View binary_dir;
    String_View list_dir;
    const char *current_file;
    size_t loop_depth;
    Eval_Flow_Signal pending_flow;
} Eval_Exec_Context;

typedef Eval_Exec_Context *Eval_Exec_Context_Stack;

typedef struct {
    Var_Scope *scopes;
    // Invariant: visible_scope_depth <= arena_arr_len(scopes)
    size_t visible_scope_depth;
    Eval_Cache_Entry *cache_entries;
    Macro_Frame_Stack macro_frames;
    Block_Frame_Stack block_frames;
    String_View *return_propagate_vars;
} Eval_Scope_State;

typedef struct {
    Eval_Compat_Profile compat_profile;
    Eval_Unsupported_Policy unsupported_policy;
    size_t error_budget;
    // Snapshot refreshed at command-cycle boundary (eval_node entry).
    bool continue_on_error_snapshot;
    Eval_Run_Report run_report;
    bool in_variable_watch_notification;
} Eval_Runtime_State;

typedef enum {
    EVAL_FETCHCONTENT_TRANSPORT_NONE = 0,
    EVAL_FETCHCONTENT_TRANSPORT_URL,
    EVAL_FETCHCONTENT_TRANSPORT_GIT,
    EVAL_FETCHCONTENT_TRANSPORT_SVN,
    EVAL_FETCHCONTENT_TRANSPORT_HG,
    EVAL_FETCHCONTENT_TRANSPORT_CVS,
    EVAL_FETCHCONTENT_TRANSPORT_CUSTOM_DOWNLOAD,
} Eval_FetchContent_Transport;

typedef enum {
    EVAL_FETCHCONTENT_TRY_FIND_OPT_IN = 0,
    EVAL_FETCHCONTENT_TRY_FIND_ALWAYS,
    EVAL_FETCHCONTENT_TRY_FIND_NEVER,
} Eval_FetchContent_Try_Find_Mode;

typedef struct {
    String_View name;
    String_View canonical_name;
    SV_List args;
    String_View source_dir;
    String_View binary_dir;
    String_View subbuild_dir;
    String_View source_subdir;
    bool has_source_dir;
    bool has_binary_dir;
    bool has_subbuild_dir;
    bool exclude_from_all;
    bool system;
    bool quiet;
    bool override_find_package;
    bool has_find_package_args;
    SV_List find_package_args;
    Eval_FetchContent_Transport transport;
    SV_List urls;
    String_View url_hash;
    String_View url_md5;
    String_View download_name;
    bool has_download_command;
    SV_List download_command;
    bool download_no_extract;
    bool download_extract_timestamp;
    bool download_no_progress;
    bool has_timeout;
    size_t timeout_sec;
    bool has_inactivity_timeout;
    size_t inactivity_timeout_sec;
    String_View http_username;
    String_View http_password;
    SV_List http_headers;
    bool has_tls_verify;
    bool tls_verify;
    String_View tls_cainfo;
    bool has_netrc_mode;
    String_View netrc_mode;
    String_View netrc_file;
    String_View git_repository;
    String_View git_tag;
    String_View git_remote_name;
    bool git_shallow;
    bool git_progress;
    SV_List git_config;
    String_View git_remote_update_strategy;
    bool has_git_submodules;
    SV_List git_submodules;
    bool git_submodules_recurse;
    String_View svn_repository;
    String_View svn_revision;
    String_View svn_username;
    String_View svn_password;
    bool has_svn_trust_cert;
    bool svn_trust_cert;
    String_View hg_repository;
    String_View hg_tag;
    String_View cvs_repository;
    String_View cvs_module;
    String_View cvs_tag;
    bool has_update_command;
    SV_List update_command;
    bool has_update_disconnected;
    bool update_disconnected;
    bool has_patch_command;
    SV_List patch_command;
    Eval_FetchContent_Try_Find_Mode try_find_mode;
    bool find_package_targets_global;
} Eval_FetchContent_Declaration;

typedef Eval_FetchContent_Declaration *Eval_FetchContent_Declaration_List;

typedef struct {
    String_View name;
    String_View canonical_name;
    bool population_processed;
    bool populated;
    bool source_dir_known;
    bool binary_dir_known;
    bool resolved_by_provider;
    bool resolved_by_find_package;
    String_View source_dir;
    String_View binary_dir;
} Eval_FetchContent_State;

typedef Eval_FetchContent_State *Eval_FetchContent_State_List;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    String_View parent_source_dir;
    String_View parent_binary_dir;
    SV_List declared_targets;
    SV_List declared_tests;
    Var_Binding *definition_bindings;
    SV_List macro_names;
    SV_List listfile_stack;
} Eval_Directory_Node;

typedef Eval_Directory_Node *Eval_Directory_Node_List;

typedef struct {
    Eval_Directory_Node_List nodes;
} Eval_Directory_Graph;

typedef struct {
    String_View scope_upper;
    String_View object_id;
    String_View property_upper;
    String_View value;
} Eval_Property_Record;

typedef Eval_Property_Record *Eval_Property_Record_List;

typedef struct {
    Eval_Property_Record_List records;
} Eval_Property_Engine;

typedef struct {
    Arena *arena;
    Eval_Target_Record_List records;
    SV_List aliases;
} Eval_Target_Model;

typedef struct {
    Arena *arena;
    Eval_Test_Record_List records;
} Eval_Test_Model;

typedef struct {
    SV_List components;
} Eval_Install_Model;

typedef struct {
    SV_List exports;
} Eval_Export_Model;

typedef struct {
    String_View command_name;
    bool supports_find_package;
    bool supports_fetchcontent_makeavailable_serial;
    size_t active_find_package_depth;
    size_t active_fetchcontent_makeavailable_depth;
} Eval_Dependency_Provider_State;

typedef struct {
    String_View package_name;
    String_View prefix;
} Eval_Package_Registry_Entry;

typedef Eval_Package_Registry_Entry *Eval_Package_Registry_Entry_List;

typedef struct {
    Eval_Dependency_Provider_State dependency_provider;
    SV_List active_find_packages;
    SV_List found_packages;
    SV_List not_found_packages;
    Eval_Package_Registry_Entry_List registry_entries;
} Eval_Package_Model;

typedef struct {
    String_View client_name;
    String_View client_query_file;
    String_View kind_upper;
    String_View kind_json;
    String_View versions;
    bool shared_query;
} Eval_File_Api_Query_Record;

typedef Eval_File_Api_Query_Record *Eval_File_Api_Query_Record_List;

typedef struct {
    Eval_File_Api_Query_Record_List queries;
    size_t next_reply_nonce;
} Eval_File_Api_Model;

typedef struct {
    Eval_FetchContent_Declaration_List declarations;
    Eval_FetchContent_State_List states;
    SV_List active_makeavailable;
} Eval_FetchContent_Model;

typedef struct {
    Eval_Directory_Graph directories;
    Eval_Property_Engine properties;
    Eval_Target_Model targets;
    Eval_Test_Model tests;
    Eval_Install_Model install;
    Eval_Export_Model export_state;
    Eval_Package_Model package;
    Eval_File_Api_Model file_api;
    Eval_FetchContent_Model fetchcontent;
} Eval_Semantic_State;

typedef struct {
    Arena *user_commands_arena;
    User_Command_List user_commands;
    SV_List watched_variables;
    SV_List watched_variable_commands;
} Eval_Command_State;

typedef struct {
    Eval_File_Lock_List file_locks;
    Eval_File_Generate_Job_List file_generate_jobs;
    Eval_Deferred_Dir_Frame_Stack deferred_dirs;
    SV_List generated_deferred_ids;
    size_t next_deferred_call_id;
    size_t next_build_step_id;
    size_t next_replay_action_id;
    size_t next_export_id;
    size_t next_cpack_package_id;
} Eval_File_State;

typedef struct {
    Eval_Process_Env_Table env_overrides;
} Eval_Process_State;

typedef struct {
    String_View producer;
    String_View kind;
    String_View status;
    String_View base_dir;
    String_View primary_path;
    String_View aux_paths;
} Eval_Canonical_Artifact;

typedef Eval_Canonical_Artifact *Eval_Canonical_Artifact_List;

typedef enum {
    EVAL_CTEST_STEP_START = 0,
    EVAL_CTEST_STEP_UPDATE,
    EVAL_CTEST_STEP_CONFIGURE,
    EVAL_CTEST_STEP_BUILD,
    EVAL_CTEST_STEP_TEST,
    EVAL_CTEST_STEP_COVERAGE,
    EVAL_CTEST_STEP_MEMCHECK,
    EVAL_CTEST_STEP_UPLOAD,
    EVAL_CTEST_STEP_SUBMIT,
} Eval_Ctest_Step_Kind;

typedef struct {
    Eval_Ctest_Step_Kind kind;
    String_View command_name;
    String_View submit_part;
    String_View status;
    String_View model;
    String_View track;
    String_View source_dir;
    String_View build_dir;
    String_View testing_dir;
    String_View tag;
    String_View tag_file;
    String_View tag_dir;
    String_View manifest;
    String_View output_junit;
    String_View submit_files;
    bool has_primary_artifact;
    size_t primary_artifact_index;
} Eval_Ctest_Step_Record;

typedef Eval_Ctest_Step_Record *Eval_Ctest_Step_Record_List;

typedef struct {
    Eval_Canonical_Artifact_List artifacts;
    Eval_Ctest_Step_Record_List ctest_steps;
} Eval_Canonical_State;

typedef struct {
    Eval_Canonical_Artifact_List artifacts;
    Eval_Ctest_Step_Record_List ctest_steps;
} Eval_Canonical_Draft;

typedef struct {
    Eval_Var_Entry *entries;
    size_t count;
} Eval_Var_Table_Snapshot;

typedef struct {
    Eval_Cache_Entry *entries;
    size_t count;
} Eval_Cache_Table_Snapshot;

typedef struct {
    Eval_Process_Env_Entry *entries;
    size_t count;
} Eval_Process_Env_Table_Snapshot;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    String_View parent_source_dir;
    String_View parent_binary_dir;
    String_View *declared_targets;
    size_t declared_target_count;
    String_View *declared_tests;
    size_t declared_test_count;
    Var_Binding *definition_bindings;
    size_t definition_binding_count;
    String_View *macro_names;
    size_t macro_name_count;
    String_View *listfile_stack;
    size_t listfile_stack_count;
} Eval_Directory_Node_Snapshot;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    Eval_Deferred_Call *calls;
    size_t call_count;
} Eval_Deferred_Dir_Frame_Snapshot;

typedef enum {
    EVAL_TX_PROJECTION_EVENT = 0,
    EVAL_TX_PROJECTION_DIAG,
} Eval_Tx_Projection_Kind;

typedef struct {
    Event_Diag_Severity input_severity;
    Event_Diag_Severity effective_severity;
    Eval_Diag_Code code;
    uint32_t scope_depth;
    uint32_t policy_depth;
    String_View component;
    String_View command;
    Event_Origin origin;
    String_View cause;
    String_View hint;
} Eval_Tx_Diag_Projection;

typedef struct {
    Eval_Tx_Projection_Kind kind;
    union {
        Event event;
        Eval_Tx_Diag_Projection diag;
    } as;
} Eval_Tx_Projection;

typedef Eval_Tx_Projection *Eval_Tx_Projection_List;

typedef struct Eval_Command_Transaction {
    struct Eval_Command_Transaction *parent;
    Arena_Mark mark;
    bool active;
    bool preserve_scope_vars_on_failure;
    bool saw_error_diag;
    size_t pending_warning_count;
    size_t pending_error_count;
    Eval_Tx_Projection_List projections;

    Eval_Var_Table_Snapshot *scope_vars;
    size_t scope_var_count;
    size_t visible_scope_depth;

    Eval_Cache_Table_Snapshot cache_entries;
    Eval_Process_Env_Table_Snapshot env_overrides;

    String_View *message_check_stack;
    size_t message_check_count;

    Eval_Directory_Node_Snapshot *directory_nodes;
    size_t directory_node_count;
    Eval_Property_Record *property_records;
    size_t property_record_count;
    Eval_Target_Record *target_records;
    size_t target_record_count;
    String_View *target_aliases;
    size_t target_alias_count;
    Eval_Test_Record *test_records;
    size_t test_record_count;
    String_View *install_components;
    size_t install_component_count;
    String_View *export_entries;
    size_t export_count;
    String_View *active_find_packages;
    size_t active_find_package_count;
    String_View *found_packages;
    size_t found_package_count;
    String_View *not_found_packages;
    size_t not_found_package_count;
    Eval_Package_Registry_Entry *package_registry_entries;
    size_t package_registry_entry_count;
    Eval_Dependency_Provider_State dependency_provider;
    Eval_File_Api_Query_Record *file_api_queries;
    size_t file_api_query_count;
    size_t file_api_next_reply_nonce;
    Eval_FetchContent_Declaration *fetchcontent_declarations;
    size_t fetchcontent_declaration_count;
    Eval_FetchContent_State *fetchcontent_states;
    size_t fetchcontent_state_count;
    String_View *active_makeavailable;
    size_t active_makeavailable_count;

    User_Command *user_commands;
    size_t user_command_count;
    String_View *watched_variables;
    size_t watched_variable_count;
    String_View *watched_variable_commands;
    size_t watched_variable_command_count;
    Eval_Property_Definition *property_definitions;
    size_t property_definition_count;

    Eval_File_Generate_Job *file_generate_jobs;
    size_t file_generate_job_count;
    Eval_Deferred_Dir_Frame_Snapshot *deferred_dirs;
    size_t deferred_dir_count;
    String_View *generated_deferred_ids;
    size_t generated_deferred_id_count;
    size_t next_deferred_call_id;
    Eval_Canonical_Artifact *canonical_artifacts;
    size_t canonical_artifact_count;
    Eval_Ctest_Step_Record *ctest_steps;
    size_t ctest_step_count;

    bool cpack_module_loaded;
    bool cpack_component_module_loaded;
    bool fetchcontent_module_loaded;
} Eval_Command_Transaction;

typedef struct {
    EvalRegistry *registry;
    const EvalServices *services;
    Eval_Scope_State scope_state;
    Eval_Semantic_State semantic_state;
    Eval_Command_State command_state;
    Eval_Process_State process_state;
    Eval_Canonical_State canonical_state;
    Eval_Property_Definition_List property_definitions;
    Eval_Policy_Level *policy_levels;
    size_t visible_policy_depth;
    Eval_Runtime_State runtime_state;
    Arena *transaction_arena;
    bool cpack_module_loaded;
    bool cpack_component_module_loaded;
    bool fetchcontent_module_loaded;
} EvalSessionState;

struct EvalExecContext {
    struct EvalSession *session;
    Arena *arena;          // TEMP ARENA: Limpa a cada statement (usado p/ expansão de args)
    Arena *event_arena;    // PERSISTENT ARENA: storage interno do evaluator; Event_Stream owna payloads só no push
    Arena *transaction_arena;
    Cmake_Event_Stream *stream;
    EvalRegistry *registry;
    const EvalServices *services;

    String_View source_dir;
    String_View binary_dir;
    Eval_Exec_Mode mode;

    const char *current_file;

    Eval_Scope_State scope_state;

    SV_List message_check_stack;
    Eval_Semantic_State semantic_state;
    Eval_Command_State command_state;
    Eval_File_State file_state;
    Eval_Process_State process_state;
    Eval_Canonical_State canonical_state;
    Eval_Property_Definition_List property_definitions;
    Eval_Policy_Level *policy_levels;
    Eval_Exec_Context_Stack exec_contexts;
    // Invariant: visible_policy_depth <= arena_arr_len(policy_levels)
    size_t visible_policy_depth;
    bool cpack_module_loaded;
    bool cpack_component_module_loaded;
    bool fetchcontent_module_loaded;
    size_t file_eval_depth;
    size_t function_eval_depth;

    Eval_Return_Context return_context;
    Eval_Runtime_State runtime_state;
    Eval_Command_Transaction *active_transaction;
    String_View dependency_provider_context_file;

    bool oom;
    bool stop_requested;
};

struct EvalRegistry {
    Arena *arena;
    Eval_Native_Command_List native_commands;
    Eval_Native_Command_Index_Entry *native_command_index;
    bool builtins_seeded;
    bool mutation_blocked;
};

struct EvalSession {
    EvalSessionState state;
    Arena *persistent_arena;
    String_View source_root;
    String_View binary_root;
    bool enable_export_host_effects;
    bool owns_registry;
    Eval_Run_Report last_run_report;
};

// ---- controle e memória ----
bool eval_should_stop(EvalExecContext *ctx);
void eval_request_stop(EvalExecContext *ctx);
void eval_request_stop_on_error(EvalExecContext *ctx);
void eval_reset_stop_request(EvalExecContext *ctx);
bool eval_continue_on_error(EvalExecContext *ctx);
void eval_refresh_runtime_compat(EvalExecContext *ctx);
bool eval_compat_set_profile(EvalExecContext *ctx, Eval_Compat_Profile profile);
Cmake_Diag_Severity eval_compat_effective_severity(const EvalExecContext *ctx, Cmake_Diag_Severity sev);
bool eval_compat_decide_on_diag(EvalExecContext *ctx, Cmake_Diag_Severity effective_sev);
String_View eval_compat_profile_to_sv(Eval_Compat_Profile profile);
bool ctx_oom(EvalExecContext *ctx);
size_t eval_pending_warning_count(const EvalExecContext *ctx);
size_t eval_pending_error_count(const EvalExecContext *ctx);
bool eval_sv_eq_ci_lit(String_View a, const char *lit);
String_View eval_policy_get_effective(EvalExecContext *ctx, String_View policy_id);

static inline Eval_Result eval_result_ok(void) {
    Eval_Result r = { .kind = EVAL_RESULT_OK };
    return r;
}

static inline Eval_Result eval_result_soft_error(void) {
    Eval_Result r = { .kind = EVAL_RESULT_SOFT_ERROR };
    return r;
}

static inline Eval_Result eval_result_fatal(void) {
    Eval_Result r = { .kind = EVAL_RESULT_FATAL };
    return r;
}

static inline Eval_Result eval_result_merge(Eval_Result lhs, Eval_Result rhs) {
    return (rhs.kind > lhs.kind) ? rhs : lhs;
}

static inline Eval_Result eval_result_from_ctx(EvalExecContext *ctx) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    return eval_pending_error_count(ctx) > 0 ? eval_result_soft_error() : eval_result_ok();
}

static inline Eval_Result eval_result_ok_if_running(EvalExecContext *ctx) {
    return eval_should_stop(ctx) ? eval_result_fatal() : eval_result_ok();
}

static inline size_t eval_scope_visible_depth(const EvalExecContext *ctx) {
    return ctx ? ctx->scope_state.visible_scope_depth : 0;
}

static inline size_t eval_policy_visible_depth(const EvalExecContext *ctx) {
    return ctx ? ctx->visible_policy_depth : 0;
}

static inline String_View eval_policy_effective_id(EvalExecContext *ctx, const char *policy_id) {
    if (!ctx || !policy_id) return nob_sv_from_cstr("");
    return eval_policy_get_effective(ctx, nob_sv_from_cstr(policy_id));
}

static inline bool eval_policy_is_new(EvalExecContext *ctx, const char *policy_id) {
    return eval_sv_eq_ci_lit(eval_policy_effective_id(ctx, policy_id), "NEW");
}

static inline bool eval_policy_is_old(EvalExecContext *ctx, const char *policy_id) {
    return eval_sv_eq_ci_lit(eval_policy_effective_id(ctx, policy_id), "OLD");
}

static inline bool eval_scope_use_parent_view(EvalExecContext *ctx, size_t *saved_depth) {
    if (!ctx || !saved_depth) return false;
    size_t depth = eval_scope_visible_depth(ctx);
    if (depth <= 1) return false;
    *saved_depth = depth;
    ctx->scope_state.visible_scope_depth = depth - 1;
    return true;
}

static inline bool eval_scope_use_global_view(EvalExecContext *ctx, size_t *saved_depth) {
    if (!ctx || !saved_depth) return false;
    size_t depth = eval_scope_visible_depth(ctx);
    if (depth == 0) return false;
    *saved_depth = depth;
    ctx->scope_state.visible_scope_depth = 1;
    return true;
}

static inline void eval_scope_restore_view(EvalExecContext *ctx, size_t saved_depth) {
    if (ctx) ctx->scope_state.visible_scope_depth = saved_depth;
}

static inline Eval_Scope_State *eval_scope_slice(EvalExecContext *ctx) {
    return ctx ? &ctx->scope_state : NULL;
}

static inline const Eval_Scope_State *eval_scope_slice_const(const EvalExecContext *ctx) {
    return ctx ? &ctx->scope_state : NULL;
}

static inline Eval_Runtime_State *eval_runtime_slice(EvalExecContext *ctx) {
    return ctx ? &ctx->runtime_state : NULL;
}

static inline bool eval_exec_is_project_mode(const EvalExecContext *ctx) {
    return ctx && ctx->mode == EVAL_EXEC_MODE_PROJECT;
}

static inline const Eval_Runtime_State *eval_runtime_slice_const(const EvalExecContext *ctx) {
    return ctx ? &ctx->runtime_state : NULL;
}

static inline Eval_Semantic_State *eval_semantic_slice(EvalExecContext *ctx) {
    return ctx ? &ctx->semantic_state : NULL;
}

static inline const Eval_Semantic_State *eval_semantic_slice_const(const EvalExecContext *ctx) {
    return ctx ? &ctx->semantic_state : NULL;
}

static inline Eval_Command_State *eval_command_slice(EvalExecContext *ctx) {
    return ctx ? &ctx->command_state : NULL;
}

static inline const Eval_Command_State *eval_command_slice_const(const EvalExecContext *ctx) {
    return ctx ? &ctx->command_state : NULL;
}

static inline Eval_File_State *eval_file_slice(EvalExecContext *ctx) {
    return ctx ? &ctx->file_state : NULL;
}

static inline const Eval_File_State *eval_file_slice_const(const EvalExecContext *ctx) {
    return ctx ? &ctx->file_state : NULL;
}

static inline Eval_Process_State *eval_process_slice(EvalExecContext *ctx) {
    return ctx ? &ctx->process_state : NULL;
}

static inline const Eval_Process_State *eval_process_slice_const(const EvalExecContext *ctx) {
    return ctx ? &ctx->process_state : NULL;
}

static inline Eval_Canonical_State *eval_canonical_slice(EvalExecContext *ctx) {
    return ctx ? &ctx->canonical_state : NULL;
}

static inline const Eval_Canonical_State *eval_canonical_slice_const(const EvalExecContext *ctx) {
    return ctx ? &ctx->canonical_state : NULL;
}

static inline bool eval_mark_oom_if_null(EvalExecContext *ctx, const void *ptr) {
    if (ptr) return false;
    ctx_oom(ctx);
    return true;
}

#define EVAL_OOM_RETURN_IF_NULL(ctx, ptr, ret_value) \
    do { if (eval_mark_oom_if_null((ctx), (ptr))) return (ret_value); } while (0)

#define EVAL_OOM_RETURN_VOID_IF_NULL(ctx, ptr) \
    do { if (eval_mark_oom_if_null((ctx), (ptr))) return; } while (0)

#define EVAL_ARR_PUSH(ctx, arena, arr, value) \
    (arena_arr_push((arena), (arr), (value)) ? true : ctx_oom((ctx)))

Eval_Result eval_emit_diag(EvalExecContext *ctx,
                           Eval_Diag_Code code,
                           String_View component,
                           String_View command,
                           Cmake_Event_Origin origin,
                           String_View cause,
                           String_View hint);
Eval_Result eval_emit_diag_with_severity(EvalExecContext *ctx,
                                         Cmake_Diag_Severity severity,
                                         Eval_Diag_Code code,
                                         String_View component,
                                         String_View command,
                                         Cmake_Event_Origin origin,
                                         String_View cause,
                                         String_View hint);

#define EVAL_NODE_ORIGIN_DIAG_RESULT(ctx, node, origin, code, component_lit, cause, hint) \
    eval_emit_diag((ctx),                                                                   \
                   (code),                                                                  \
                   nob_sv_from_cstr((component_lit)),                                       \
                   (node)->as.cmd.name,                                                     \
                   (origin),                                                                \
                   (cause),                                                                 \
                   (hint))

#define EVAL_NODE_ORIGIN_DIAG_RESULT_SEV(ctx, node, origin, severity, code, component_lit, cause, hint) \
    eval_emit_diag_with_severity((ctx),                                                                   \
                                 (severity),                                                              \
                                 (code),                                                                  \
                                 nob_sv_from_cstr((component_lit)),                                       \
                                 (node)->as.cmd.name,                                                     \
                                 (origin),                                                                \
                                 (cause),                                                                 \
                                 (hint))

#define EVAL_NODE_ORIGIN_DIAG_BOOL(ctx, node, origin, code, component_lit, cause, hint) \
    eval_diag_emit_bool((ctx),                                                                  \
                        (code),                                                                 \
                        nob_sv_from_cstr((component_lit)),                                      \
                        (node)->as.cmd.name,                                                    \
                        (origin),                                                               \
                        (cause),                                                                \
                        (hint))

#define EVAL_NODE_ORIGIN_DIAG_BOOL_SEV(ctx, node, origin, severity, code, component_lit, cause, hint) \
    eval_diag_emit_with_severity_bool((ctx),                                                            \
                                      (severity),                                                       \
                                      (code),                                                           \
                                      nob_sv_from_cstr((component_lit)),                                \
                                      (node)->as.cmd.name,                                              \
                                      (origin),                                                         \
                                      (cause),                                                          \
                                      (hint))

#define EVAL_NODE_ORIGIN_DIAG_EMIT(ctx, node, origin, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_BOOL((ctx), (node), (origin), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, severity, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_BOOL_SEV((ctx), (node), (origin), (severity), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_DIAG_RESULT(ctx, node, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_RESULT((ctx),                                     \
                                 (node),                                    \
                                 eval_origin_from_node((ctx), (node)),      \
                                 (code),                                    \
                                 (component_lit),                           \
                                 (cause),                                   \
                                 (hint))

#define EVAL_NODE_DIAG_RESULT_SEV(ctx, node, severity, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_RESULT_SEV((ctx),                                                \
                                     (node),                                               \
                                     eval_origin_from_node((ctx), (node)),                 \
                                     (severity),                                           \
                                     (code),                                               \
                                     (component_lit),                                      \
                                     (cause),                                              \
                                     (hint))

#define EVAL_NODE_DIAG_BOOL(ctx, node, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_BOOL((ctx), (node), eval_origin_from_node((ctx), (node)), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_DIAG_BOOL_SEV(ctx, node, severity, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_BOOL_SEV((ctx), (node), eval_origin_from_node((ctx), (node)), (severity), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_DIAG_EMIT(ctx, node, code, component_lit, cause, hint) \
    EVAL_NODE_DIAG_BOOL((ctx), (node), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_DIAG_EMIT_SEV(ctx, node, severity, code, component_lit, cause, hint) \
    EVAL_NODE_DIAG_BOOL_SEV((ctx), (node), (severity), (code), (component_lit), (cause), (hint))

#define EVAL_DIAG_RESULT(ctx, code, component, command, origin, cause, hint) \
    eval_emit_diag((ctx), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_RESULT_SEV(ctx, severity, code, component, command, origin, cause, hint) \
    eval_emit_diag_with_severity((ctx), (severity), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_BOOL(ctx, code, component, command, origin, cause, hint) \
    eval_diag_emit_bool((ctx), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_BOOL_SEV(ctx, severity, code, component, command, origin, cause, hint) \
    eval_diag_emit_with_severity_bool((ctx), (severity), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_EMIT(ctx, code, component, command, origin, cause, hint) \
    EVAL_DIAG_BOOL((ctx), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_EMIT_SEV(ctx, severity, code, component, command, origin, cause, hint) \
    EVAL_DIAG_BOOL_SEV((ctx), (severity), (code), (component), (command), (origin), (cause), (hint))

static inline bool eval_diag_emit_bool(EvalExecContext *ctx,
                                       Eval_Diag_Code code,
                                       String_View component,
                                       String_View command,
                                       Cmake_Event_Origin origin,
                                       String_View cause,
                                       String_View hint) {
    return !eval_result_is_fatal(eval_emit_diag(ctx, code, component, command, origin, cause, hint));
}

static inline bool eval_diag_emit_with_severity_bool(EvalExecContext *ctx,
                                                     Cmake_Diag_Severity severity,
                                                     Eval_Diag_Code code,
                                                     String_View component,
                                                     String_View command,
                                                     Cmake_Event_Origin origin,
                                                     String_View cause,
                                                     String_View hint) {
    return !eval_result_is_fatal(eval_emit_diag_with_severity(ctx, severity, code, component, command, origin, cause, hint));
}

static inline void eval_clear_return_state(EvalExecContext *ctx) {
    if (!ctx) return;
    if (ctx->exec_contexts && arena_arr_len(ctx->exec_contexts) > 0) {
        Eval_Exec_Context *current = &ctx->exec_contexts[arena_arr_len(ctx->exec_contexts) - 1];
        if (current->pending_flow == EVAL_FLOW_RETURN) current->pending_flow = EVAL_FLOW_NONE;
    }
    ctx->scope_state.return_propagate_vars = NULL;
}

Arena *eval_temp_arena(EvalExecContext *ctx);
Arena *eval_event_arena(EvalExecContext *ctx);

static inline bool eval_sv_arr_push_temp(EvalExecContext *ctx, String_View **arr, String_View sv) {
    if (!ctx || !arr) return false;
    return EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), *arr, sv);
}

String_View sv_copy_to_temp_arena(EvalExecContext *ctx, String_View sv);
String_View sv_copy_to_event_arena(EvalExecContext *ctx, String_View sv);
String_View sv_copy_to_arena(Arena *arena, String_View sv);
char *eval_sv_to_cstr_temp(EvalExecContext *ctx, String_View sv);

static inline bool eval_sv_arr_push_event(EvalExecContext *ctx, String_View **arr, String_View sv) {
    if (!ctx || !arr) return false;
    String_View copied = sv_copy_to_event_arena(ctx, sv);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, *arr, copied);
}

// ---- rastreio de origem (NOVO) ----
Event_Origin eval_origin_from_node(const EvalExecContext *ctx, const Node *node);

Eval_Exec_Context *eval_exec_current(EvalExecContext *ctx);
const Eval_Exec_Context *eval_exec_current_const(const EvalExecContext *ctx);
bool eval_exec_push(EvalExecContext *ctx, Eval_Exec_Context frame);
void eval_exec_pop(EvalExecContext *ctx);
bool eval_exec_request_break(EvalExecContext *ctx);
bool eval_exec_request_continue(EvalExecContext *ctx);
bool eval_exec_request_return(EvalExecContext *ctx);
Eval_Flow_Signal eval_exec_pending_flow(const EvalExecContext *ctx);
void eval_exec_clear_pending_flow(EvalExecContext *ctx);
void eval_exec_clear_all_flow(EvalExecContext *ctx);
size_t eval_exec_current_loop_depth(const EvalExecContext *ctx);
bool eval_exec_has_active_kind(const EvalExecContext *ctx, Eval_Exec_Context_Kind kind);
bool eval_exec_is_file_scope(const EvalExecContext *ctx);
bool eval_exec_publish_current_vars(EvalExecContext *ctx);

// ---- args e expansão (NOVO COMPORTAMENTO) ----
// Retorna a lista expandida e dividida por ';' (alocada na TEMP ARENA)
String_View eval_resolve_quoted_arg_temp(EvalExecContext *ctx, String_View flat, bool expand_vars);
SV_List eval_resolve_args(EvalExecContext *ctx, const Args *raw_args);
SV_List eval_resolve_args_literal(EvalExecContext *ctx, const Args *raw_args);

// ---- diagnostics / events ----
bool eval_emit_event(EvalExecContext *ctx, Event ev);
bool eval_emit_event_allow_stopped(EvalExecContext *ctx, Event ev);
static inline bool emit_event(EvalExecContext *ctx, Event ev) {
    return eval_emit_event(ctx, ev);
}

static inline String_View *eval_sv_list_copy_to_event_arena(EvalExecContext *ctx, const SV_List *list) {
    if (!ctx || !list || arena_arr_len(*list) == 0) return NULL;
    String_View *items = arena_alloc_array(eval_event_arena(ctx), String_View, arena_arr_len(*list));
    EVAL_OOM_RETURN_IF_NULL(ctx, items, NULL);
    for (size_t i = 0; i < arena_arr_len(*list); ++i) {
        items[i] = sv_copy_to_event_arena(ctx, (*list)[i]);
        if (eval_should_stop(ctx)) return NULL;
    }
    return items;
}

static inline String_View eval_alloc_build_step_key(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    char *tmp = nob_temp_sprintf("step_%zu", ctx->file_state.next_build_step_id++);
    char *buf = tmp ? arena_strndup(eval_event_arena(ctx), tmp, strlen(tmp)) : NULL;
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    return nob_sv_from_cstr(buf);
}

static inline String_View eval_alloc_replay_action_key(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    char *tmp = nob_temp_sprintf("replay_%zu", ctx->file_state.next_replay_action_id++);
    char *buf = tmp ? arena_strndup(eval_event_arena(ctx), tmp, strlen(tmp)) : NULL;
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    return nob_sv_from_cstr(buf);
}

static inline String_View eval_alloc_export_key(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    char *tmp = nob_temp_sprintf("export_%zu", ctx->file_state.next_export_id++);
    char *buf = tmp ? arena_strndup(eval_event_arena(ctx), tmp, strlen(tmp)) : NULL;
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    return nob_sv_from_cstr(buf);
}

static inline String_View eval_alloc_cpack_package_key(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    char *tmp = nob_temp_sprintf("cpack_package_%zu", ctx->file_state.next_cpack_package_id++);
    char *buf = tmp ? arena_strndup(eval_event_arena(ctx), tmp, strlen(tmp)) : NULL;
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    return nob_sv_from_cstr(buf);
}

static inline bool eval_enable_export_host_effects(const EvalExecContext *ctx) {
    return ctx && ctx->session ? ctx->session->enable_export_host_effects : true;
}

static inline bool eval_emit_source_mark_generated(EvalExecContext *ctx,
                                                   Event_Origin origin,
                                                   String_View path,
                                                   String_View directory_source_dir,
                                                   String_View directory_binary_dir,
                                                   bool generated) {
    Event ev = {0};
    ev.h.kind = EVENT_SOURCE_MARK_GENERATED;
    ev.h.origin = origin;
    ev.as.source_mark_generated.path = sv_copy_to_event_arena(ctx, path);
    ev.as.source_mark_generated.directory_source_dir = sv_copy_to_event_arena(ctx, directory_source_dir);
    ev.as.source_mark_generated.directory_binary_dir = sv_copy_to_event_arena(ctx, directory_binary_dir);
    ev.as.source_mark_generated.generated = generated;
    return emit_event(ctx, ev);
}

static inline bool eval_emit_build_step_declare(EvalExecContext *ctx,
                                                Event_Origin origin,
                                                String_View step_key,
                                                Event_Build_Step_Kind step_kind,
                                                String_View owner_target_name,
                                                bool append,
                                                bool verbatim,
                                                bool uses_terminal,
                                                bool command_expand_lists,
                                                bool depends_explicit_only,
                                                bool codegen,
                                                String_View working_directory,
                                                String_View comment,
                                                String_View main_dependency,
                                                String_View depfile,
                                                String_View job_pool,
                                                String_View job_server_aware) {
    Event ev = {0};
    ev.h.kind = EVENT_BUILD_STEP_DECLARE;
    ev.h.origin = origin;
    ev.as.build_step_declare.step_key = sv_copy_to_event_arena(ctx, step_key);
    ev.as.build_step_declare.step_kind = step_kind;
    ev.as.build_step_declare.owner_target_name = sv_copy_to_event_arena(ctx, owner_target_name);
    ev.as.build_step_declare.append = append;
    ev.as.build_step_declare.verbatim = verbatim;
    ev.as.build_step_declare.uses_terminal = uses_terminal;
    ev.as.build_step_declare.command_expand_lists = command_expand_lists;
    ev.as.build_step_declare.depends_explicit_only = depends_explicit_only;
    ev.as.build_step_declare.codegen = codegen;
    ev.as.build_step_declare.working_directory = sv_copy_to_event_arena(ctx, working_directory);
    ev.as.build_step_declare.comment = sv_copy_to_event_arena(ctx, comment);
    ev.as.build_step_declare.main_dependency = sv_copy_to_event_arena(ctx, main_dependency);
    ev.as.build_step_declare.depfile = sv_copy_to_event_arena(ctx, depfile);
    ev.as.build_step_declare.job_pool = sv_copy_to_event_arena(ctx, job_pool);
    ev.as.build_step_declare.job_server_aware = sv_copy_to_event_arena(ctx, job_server_aware);
    return emit_event(ctx, ev);
}

static inline bool eval_emit_build_step_add_output(EvalExecContext *ctx,
                                                   Event_Origin origin,
                                                   String_View step_key,
                                                   String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_BUILD_STEP_ADD_OUTPUT;
    ev.h.origin = origin;
    ev.as.build_step_add_output.step_key = sv_copy_to_event_arena(ctx, step_key);
    ev.as.build_step_add_output.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}

static inline bool eval_emit_build_step_add_byproduct(EvalExecContext *ctx,
                                                      Event_Origin origin,
                                                      String_View step_key,
                                                      String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_BUILD_STEP_ADD_BYPRODUCT;
    ev.h.origin = origin;
    ev.as.build_step_add_byproduct.step_key = sv_copy_to_event_arena(ctx, step_key);
    ev.as.build_step_add_byproduct.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}

static inline bool eval_emit_build_step_add_dependency(EvalExecContext *ctx,
                                                       Event_Origin origin,
                                                       String_View step_key,
                                                       String_View item) {
    Event ev = {0};
    ev.h.kind = EVENT_BUILD_STEP_ADD_DEPENDENCY;
    ev.h.origin = origin;
    ev.as.build_step_add_dependency.step_key = sv_copy_to_event_arena(ctx, step_key);
    ev.as.build_step_add_dependency.item = sv_copy_to_event_arena(ctx, item);
    return emit_event(ctx, ev);
}

static inline bool eval_emit_build_step_add_command(EvalExecContext *ctx,
                                                    Event_Origin origin,
                                                    String_View step_key,
                                                    uint32_t command_index,
                                                    const SV_List *argv) {
    Event ev = {0};
    ev.h.kind = EVENT_BUILD_STEP_ADD_COMMAND;
    ev.h.origin = origin;
    ev.as.build_step_add_command.step_key = sv_copy_to_event_arena(ctx, step_key);
    ev.as.build_step_add_command.command_index = command_index;
    ev.as.build_step_add_command.argc = argv ? arena_arr_len(*argv) : 0;
    ev.as.build_step_add_command.argv = eval_sv_list_copy_to_event_arena(ctx, argv);
    if (eval_should_stop(ctx)) return false;
    return emit_event(ctx, ev);
}

static inline bool eval_emit_replay_action_declare(EvalExecContext *ctx,
                                                   Event_Origin origin,
                                                   String_View action_key,
                                                   Event_Replay_Action_Kind action_kind,
                                                   Event_Replay_Opcode opcode,
                                                   Event_Replay_Phase phase,
                                                   String_View working_directory) {
    Event ev = {0};
    ev.h.kind = EVENT_REPLAY_ACTION_DECLARE;
    ev.h.origin = origin;
    ev.as.replay_action_declare.action_key = sv_copy_to_event_arena(ctx, action_key);
    ev.as.replay_action_declare.action_kind = action_kind;
    ev.as.replay_action_declare.opcode = opcode;
    ev.as.replay_action_declare.phase = phase;
    ev.as.replay_action_declare.working_directory = sv_copy_to_event_arena(ctx, working_directory);
    return emit_event(ctx, ev);
}

static inline bool eval_emit_replay_action_add_input(EvalExecContext *ctx,
                                                     Event_Origin origin,
                                                     String_View action_key,
                                                     String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_REPLAY_ACTION_ADD_INPUT;
    ev.h.origin = origin;
    ev.as.replay_action_add_input.action_key = sv_copy_to_event_arena(ctx, action_key);
    ev.as.replay_action_add_input.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}

static inline bool eval_emit_replay_action_add_output(EvalExecContext *ctx,
                                                      Event_Origin origin,
                                                      String_View action_key,
                                                      String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_REPLAY_ACTION_ADD_OUTPUT;
    ev.h.origin = origin;
    ev.as.replay_action_add_output.action_key = sv_copy_to_event_arena(ctx, action_key);
    ev.as.replay_action_add_output.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}

static inline bool eval_emit_replay_action_add_argv(EvalExecContext *ctx,
                                                    Event_Origin origin,
                                                    String_View action_key,
                                                    uint32_t arg_index,
                                                    String_View value) {
    Event ev = {0};
    ev.h.kind = EVENT_REPLAY_ACTION_ADD_ARGV;
    ev.h.origin = origin;
    ev.as.replay_action_add_argv.action_key = sv_copy_to_event_arena(ctx, action_key);
    ev.as.replay_action_add_argv.arg_index = arg_index;
    ev.as.replay_action_add_argv.value = sv_copy_to_event_arena(ctx, value);
    return emit_event(ctx, ev);
}

static inline bool eval_emit_replay_action_add_env(EvalExecContext *ctx,
                                                   Event_Origin origin,
                                                   String_View action_key,
                                                   String_View key,
                                                   String_View value) {
    Event ev = {0};
    ev.h.kind = EVENT_REPLAY_ACTION_ADD_ENV;
    ev.h.origin = origin;
    ev.as.replay_action_add_env.action_key = sv_copy_to_event_arena(ctx, action_key);
    ev.as.replay_action_add_env.key = sv_copy_to_event_arena(ctx, key);
    ev.as.replay_action_add_env.value = sv_copy_to_event_arena(ctx, value);
    return emit_event(ctx, ev);
}

static inline String_View eval_replay_mode_octal_temp(EvalExecContext *ctx, unsigned int mode) {
    char *buf = NULL;
    if (!ctx) return nob_sv_from_cstr("");
    buf = arena_alloc(eval_temp_arena(ctx), 8);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    snprintf(buf, 8, "%04o", mode & 0777u);
    return nob_sv_from_cstr(buf);
}

static inline bool eval_begin_replay_action(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            Event_Replay_Action_Kind action_kind,
                                            Event_Replay_Opcode opcode,
                                            Event_Replay_Phase phase,
                                            String_View working_directory,
                                            String_View *out_action_key) {
    String_View action_key = nob_sv_from_cstr("");
    if (out_action_key) *out_action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    action_key = eval_alloc_replay_action_key(ctx);
    if (eval_should_stop(ctx) || action_key.count == 0) return false;
    if (!eval_emit_replay_action_declare(ctx, origin, action_key, action_kind, opcode, phase, working_directory)) {
        return false;
    }
    if (out_action_key) *out_action_key = action_key;
    return true;
}

static inline bool eval_emit_target_prop_set(EvalExecContext *ctx,
                                             Event_Origin origin,
                                             String_View target_name,
                                             String_View key,
                                             String_View value,
                                             Cmake_Target_Property_Op op) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_PROP_SET;
    ev.h.origin = origin;
    ev.as.target_prop_set.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_prop_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.target_prop_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.target_prop_set.op = op;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_declare(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View name,
                                            Cmake_Target_Type target_type,
                                            bool imported,
                                            bool alias,
                                            String_View alias_of) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_DECLARE;
    ev.h.origin = origin;
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.target_type = target_type;
    ev.as.target_declare.imported = imported;
    ev.as.target_declare.alias = alias;
    ev.as.target_declare.alias_of = sv_copy_to_event_arena(ctx, alias_of);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_dependency(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               String_View target_name,
                                               String_View dependency_name) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_ADD_DEPENDENCY;
    ev.h.origin = origin;
    ev.as.target_add_dependency.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_add_dependency.dependency_name = sv_copy_to_event_arena(ctx, dependency_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_add_source(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               String_View target_name,
                                               String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_ADD_SOURCE;
    ev.h.origin = origin;
    ev.as.target_add_source.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_link_libraries(EvalExecContext *ctx,
                                                   Event_Origin origin,
                                                   String_View target_name,
                                                   Cmake_Visibility visibility,
                                                   String_View item) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_LINK_LIBRARIES;
    ev.h.origin = origin;
    ev.as.target_link_libraries.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_link_libraries.visibility = visibility;
    ev.as.target_link_libraries.item = sv_copy_to_event_arena(ctx, item);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_link_options(EvalExecContext *ctx,
                                                 Event_Origin origin,
                                                 String_View target_name,
                                                 Cmake_Visibility visibility,
                                                 String_View item,
                                                 bool is_before) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_LINK_OPTIONS;
    ev.h.origin = origin;
    ev.as.target_link_options.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_link_options.visibility = visibility;
    ev.as.target_link_options.item = sv_copy_to_event_arena(ctx, item);
    ev.as.target_link_options.is_before = is_before;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_link_directories(EvalExecContext *ctx,
                                                     Event_Origin origin,
                                                     String_View target_name,
                                                     Cmake_Visibility visibility,
                                                     String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_LINK_DIRECTORIES;
    ev.h.origin = origin;
    ev.as.target_link_directories.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_link_directories.visibility = visibility;
    ev.as.target_link_directories.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_include_directories(EvalExecContext *ctx,
                                                        Event_Origin origin,
                                                        String_View target_name,
                                                        Cmake_Visibility visibility,
                                                        String_View path,
                                                        bool is_system,
                                                        bool is_before) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_INCLUDE_DIRECTORIES;
    ev.h.origin = origin;
    ev.as.target_include_directories.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_include_directories.visibility = visibility;
    ev.as.target_include_directories.path = sv_copy_to_event_arena(ctx, path);
    ev.as.target_include_directories.is_system = is_system;
    ev.as.target_include_directories.is_before = is_before;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_compile_definitions(EvalExecContext *ctx,
                                                        Event_Origin origin,
                                                        String_View target_name,
                                                        Cmake_Visibility visibility,
                                                        String_View item) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_COMPILE_DEFINITIONS;
    ev.h.origin = origin;
    ev.as.target_compile_definitions.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_compile_definitions.visibility = visibility;
    ev.as.target_compile_definitions.item = sv_copy_to_event_arena(ctx, item);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_compile_options(EvalExecContext *ctx,
                                                    Event_Origin origin,
                                                    String_View target_name,
                                                    Cmake_Visibility visibility,
                                                    String_View item,
                                                    bool is_before) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_COMPILE_OPTIONS;
    ev.h.origin = origin;
    ev.as.target_compile_options.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_compile_options.visibility = visibility;
    ev.as.target_compile_options.item = sv_copy_to_event_arena(ctx, item);
    ev.as.target_compile_options.is_before = is_before;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_set_current(EvalExecContext *ctx,
                                             Event_Origin origin,
                                             String_View key,
                                             String_View value) {
    Event ev = {0};
    ev.h.kind = EVENT_VAR_SET;
    ev.h.version = 2;
    ev.h.origin = origin;
    ev.as.var_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.var_set.target_kind = EVENT_VAR_TARGET_CURRENT;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_unset_current(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               String_View key) {
    Event ev = {0};
    ev.h.kind = EVENT_VAR_UNSET;
    ev.h.version = 2;
    ev.h.origin = origin;
    ev.as.var_unset.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_unset.target_kind = EVENT_VAR_TARGET_CURRENT;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_set_cache(EvalExecContext *ctx,
                                           Event_Origin origin,
                                           String_View key,
                                           String_View value) {
    Event ev = {0};
    ev.h.kind = EVENT_VAR_SET;
    ev.h.version = 2;
    ev.h.origin = origin;
    ev.as.var_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.var_set.target_kind = EVENT_VAR_TARGET_CACHE;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_unset_cache(EvalExecContext *ctx,
                                             Event_Origin origin,
                                             String_View key) {
    Event ev = {0};
    ev.h.kind = EVENT_VAR_UNSET;
    ev.h.version = 2;
    ev.h.origin = origin;
    ev.as.var_unset.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_unset.target_kind = EVENT_VAR_TARGET_CACHE;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_test_enable(EvalExecContext *ctx,
                                         Event_Origin origin) {
    Event ev = {0};
    ev.h.kind = EVENT_TEST_ENABLE;
    ev.h.origin = origin;
    ev.as.test_enable.enabled = true;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_test_add(EvalExecContext *ctx,
                                      Event_Origin origin,
                                      String_View name,
                                      String_View command,
                                      String_View working_dir,
                                      bool command_expand_lists,
                                      const SV_List *configurations) {
    Event ev = {0};
    ev.h.kind = EVENT_TEST_ADD;
    ev.h.origin = origin;
    ev.as.test_add.name = sv_copy_to_event_arena(ctx, name);
    ev.as.test_add.command = sv_copy_to_event_arena(ctx, command);
    ev.as.test_add.working_dir = sv_copy_to_event_arena(ctx, working_dir);
    ev.as.test_add.command_expand_lists = command_expand_lists;
    ev.as.test_add.configuration_count = configurations ? arena_arr_len(*configurations) : 0;
    ev.as.test_add.configurations = eval_sv_list_copy_to_event_arena(ctx, configurations);
    if (eval_should_stop(ctx)) return false;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_install_rule_add(EvalExecContext *ctx,
                                              Event_Origin origin,
                                              Cmake_Install_Rule_Type rule_type,
                                              String_View item,
                                              String_View destination,
                                              String_View component,
                                              String_View namelink_component,
                                              String_View export_name,
                                              String_View archive_destination,
                                              String_View library_destination,
                                              String_View runtime_destination,
                                              String_View includes_destination,
                                              String_View public_header_destination) {
    Event ev = {0};
    ev.h.kind = EVENT_INSTALL_RULE_ADD;
    ev.h.origin = origin;
    ev.as.install_rule_add.rule_type = rule_type;
    ev.as.install_rule_add.item = sv_copy_to_event_arena(ctx, item);
    ev.as.install_rule_add.destination = sv_copy_to_event_arena(ctx, destination);
    ev.as.install_rule_add.component = sv_copy_to_event_arena(ctx, component);
    ev.as.install_rule_add.namelink_component = sv_copy_to_event_arena(ctx, namelink_component);
    ev.as.install_rule_add.export_name = sv_copy_to_event_arena(ctx, export_name);
    ev.as.install_rule_add.archive_destination = sv_copy_to_event_arena(ctx, archive_destination);
    ev.as.install_rule_add.library_destination = sv_copy_to_event_arena(ctx, library_destination);
    ev.as.install_rule_add.runtime_destination = sv_copy_to_event_arena(ctx, runtime_destination);
    ev.as.install_rule_add.includes_destination = sv_copy_to_event_arena(ctx, includes_destination);
    ev.as.install_rule_add.public_header_destination = sv_copy_to_event_arena(ctx, public_header_destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_export_install(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View export_name,
                                            String_View destination,
                                            String_View export_namespace,
                                            String_View file_name,
                                            String_View component) {
    Event ev = {0};
    ev.h.kind = EVENT_EXPORT_INSTALL;
    ev.h.origin = origin;
    ev.as.export_install.export_name = sv_copy_to_event_arena(ctx, export_name);
    ev.as.export_install.destination = sv_copy_to_event_arena(ctx, destination);
    ev.as.export_install.export_namespace = sv_copy_to_event_arena(ctx, export_namespace);
    ev.as.export_install.file_name = sv_copy_to_event_arena(ctx, file_name);
    ev.as.export_install.component = sv_copy_to_event_arena(ctx, component);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_export_build_declare(EvalExecContext *ctx,
                                                  Event_Origin origin,
                                                  String_View export_key,
                                                  Event_Export_Source_Kind source_kind,
                                                  String_View logical_name,
                                                  String_View file_path,
                                                  String_View export_namespace,
                                                  bool append,
                                                  String_View cxx_modules_directory) {
    Event ev = {0};
    ev.h.kind = EVENT_EXPORT_BUILD_DECLARE;
    ev.h.origin = origin;
    ev.as.export_build_declare.export_key = sv_copy_to_event_arena(ctx, export_key);
    ev.as.export_build_declare.source_kind = source_kind;
    ev.as.export_build_declare.logical_name = sv_copy_to_event_arena(ctx, logical_name);
    ev.as.export_build_declare.file_path = sv_copy_to_event_arena(ctx, file_path);
    ev.as.export_build_declare.export_namespace = sv_copy_to_event_arena(ctx, export_namespace);
    ev.as.export_build_declare.append = append;
    ev.as.export_build_declare.cxx_modules_directory = sv_copy_to_event_arena(ctx, cxx_modules_directory);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_export_build_add_target(EvalExecContext *ctx,
                                                     Event_Origin origin,
                                                     String_View export_key,
                                                     String_View target_name) {
    Event ev = {0};
    ev.h.kind = EVENT_EXPORT_BUILD_ADD_TARGET;
    ev.h.origin = origin;
    ev.as.export_build_add_target.export_key = sv_copy_to_event_arena(ctx, export_key);
    ev.as.export_build_add_target.target_name = sv_copy_to_event_arena(ctx, target_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_export_package_registry(EvalExecContext *ctx,
                                                     Event_Origin origin,
                                                     String_View package_name,
                                                     String_View prefix,
                                                     bool enabled) {
    Event ev = {0};
    ev.h.kind = EVENT_EXPORT_PACKAGE_REGISTRY;
    ev.h.origin = origin;
    ev.as.export_package_registry.package_name = sv_copy_to_event_arena(ctx, package_name);
    ev.as.export_package_registry.prefix = sv_copy_to_event_arena(ctx, prefix);
    ev.as.export_package_registry.enabled = enabled;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cpack_add_install_type(EvalExecContext *ctx,
                                                    Event_Origin origin,
                                                    String_View name,
                                                    String_View display_name) {
    Event ev = {0};
    ev.h.kind = EVENT_CPACK_ADD_INSTALL_TYPE;
    ev.h.origin = origin;
    ev.as.cpack_add_install_type.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_install_type.display_name = sv_copy_to_event_arena(ctx, display_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cpack_add_component_group(EvalExecContext *ctx,
                                                       Event_Origin origin,
                                                       String_View name,
                                                       String_View display_name,
                                                       String_View description,
                                                       String_View parent_group,
                                                       bool expanded,
                                                       bool bold_title) {
    Event ev = {0};
    ev.h.kind = EVENT_CPACK_ADD_COMPONENT_GROUP;
    ev.h.origin = origin;
    ev.as.cpack_add_component_group.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_component_group.display_name = sv_copy_to_event_arena(ctx, display_name);
    ev.as.cpack_add_component_group.description = sv_copy_to_event_arena(ctx, description);
    ev.as.cpack_add_component_group.parent_group = sv_copy_to_event_arena(ctx, parent_group);
    ev.as.cpack_add_component_group.expanded = expanded;
    ev.as.cpack_add_component_group.bold_title = bold_title;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cpack_add_component(EvalExecContext *ctx,
                                                 Event_Origin origin,
                                                 String_View name,
                                                 String_View display_name,
                                                 String_View description,
                                                 String_View group,
                                                 String_View depends,
                                                 String_View install_types,
                                                 String_View archive_file,
                                                 String_View plist,
                                                 bool required,
                                                 bool hidden,
                                                 bool disabled,
                                                 bool downloaded) {
    Event ev = {0};
    ev.h.kind = EVENT_CPACK_ADD_COMPONENT;
    ev.h.origin = origin;
    ev.as.cpack_add_component.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_component.display_name = sv_copy_to_event_arena(ctx, display_name);
    ev.as.cpack_add_component.description = sv_copy_to_event_arena(ctx, description);
    ev.as.cpack_add_component.group = sv_copy_to_event_arena(ctx, group);
    ev.as.cpack_add_component.depends = sv_copy_to_event_arena(ctx, depends);
    ev.as.cpack_add_component.install_types = sv_copy_to_event_arena(ctx, install_types);
    ev.as.cpack_add_component.archive_file = sv_copy_to_event_arena(ctx, archive_file);
    ev.as.cpack_add_component.plist = sv_copy_to_event_arena(ctx, plist);
    ev.as.cpack_add_component.required = required;
    ev.as.cpack_add_component.hidden = hidden;
    ev.as.cpack_add_component.disabled = disabled;
    ev.as.cpack_add_component.downloaded = downloaded;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cpack_package_declare(EvalExecContext *ctx,
                                                   Event_Origin origin,
                                                   String_View package_key,
                                                   String_View package_name,
                                                   String_View package_version,
                                                   String_View package_file_name,
                                                   String_View package_directory,
                                                   bool include_toplevel_directory,
                                                   bool archive_component_install,
                                                   String_View components_all) {
    Event ev = {0};
    ev.h.kind = EVENT_CPACK_PACKAGE_DECLARE;
    ev.h.origin = origin;
    ev.as.cpack_package_declare.package_key = sv_copy_to_event_arena(ctx, package_key);
    ev.as.cpack_package_declare.package_name = sv_copy_to_event_arena(ctx, package_name);
    ev.as.cpack_package_declare.package_version = sv_copy_to_event_arena(ctx, package_version);
    ev.as.cpack_package_declare.package_file_name = sv_copy_to_event_arena(ctx, package_file_name);
    ev.as.cpack_package_declare.package_directory = sv_copy_to_event_arena(ctx, package_directory);
    ev.as.cpack_package_declare.include_toplevel_directory = include_toplevel_directory;
    ev.as.cpack_package_declare.archive_component_install = archive_component_install;
    ev.as.cpack_package_declare.components_all = sv_copy_to_event_arena(ctx, components_all);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cpack_package_add_generator(EvalExecContext *ctx,
                                                         Event_Origin origin,
                                                         String_View package_key,
                                                         String_View generator) {
    Event ev = {0};
    ev.h.kind = EVENT_CPACK_PACKAGE_ADD_GENERATOR;
    ev.h.origin = origin;
    ev.as.cpack_package_add_generator.package_key = sv_copy_to_event_arena(ctx, package_key);
    ev.as.cpack_package_add_generator.generator = sv_copy_to_event_arena(ctx, generator);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_package_find_result(EvalExecContext *ctx,
                                                 Event_Origin origin,
                                                 String_View package_name,
                                                 String_View mode,
                                                 String_View found_path,
                                                 bool found,
                                                 bool required,
                                                 bool quiet) {
    Event ev = {0};
    ev.h.kind = EVENT_PACKAGE_FIND_RESULT;
    ev.h.origin = origin;
    ev.as.package_find_result.package_name = sv_copy_to_event_arena(ctx, package_name);
    ev.as.package_find_result.mode = sv_copy_to_event_arena(ctx, mode);
    ev.as.package_find_result.found_path = sv_copy_to_event_arena(ctx, found_path);
    ev.as.package_find_result.found = found;
    ev.as.package_find_result.required = required;
    ev.as.package_find_result.quiet = quiet;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_project_declare(EvalExecContext *ctx,
                                             Event_Origin origin,
                                             String_View name,
                                             String_View version,
                                             String_View description,
                                             String_View homepage_url,
                                             String_View languages) {
    Event ev = {0};
    ev.h.kind = EVENT_PROJECT_DECLARE;
    ev.h.origin = origin;
    ev.as.project_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.project_declare.version = sv_copy_to_event_arena(ctx, version);
    ev.as.project_declare.description = sv_copy_to_event_arena(ctx, description);
    ev.as.project_declare.homepage_url = sv_copy_to_event_arena(ctx, homepage_url);
    ev.as.project_declare.languages = sv_copy_to_event_arena(ctx, languages);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_project_minimum_required(EvalExecContext *ctx,
                                                      Event_Origin origin,
                                                      String_View version,
                                                      bool fatal_if_too_old) {
    Event ev = {0};
    ev.h.kind = EVENT_PROJECT_MINIMUM_REQUIRED;
    ev.h.origin = origin;
    ev.as.project_minimum_required.version = sv_copy_to_event_arena(ctx, version);
    ev.as.project_minimum_required.fatal_if_too_old = fatal_if_too_old;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_include_begin(EvalExecContext *ctx,
                                           Event_Origin origin,
                                           String_View path,
                                           bool no_policy_scope) {
    Event ev = {0};
    ev.h.kind = EVENT_INCLUDE_BEGIN;
    ev.h.origin = origin;
    ev.as.include_begin.path = sv_copy_to_event_arena(ctx, path);
    ev.as.include_begin.no_policy_scope = no_policy_scope;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_include_end(EvalExecContext *ctx,
                                         Event_Origin origin,
                                         String_View path,
                                         bool success) {
    Event ev = {0};
    ev.h.kind = EVENT_INCLUDE_END;
    ev.h.origin = origin;
    ev.as.include_end.path = sv_copy_to_event_arena(ctx, path);
    ev.as.include_end.success = success;
    return eval_emit_event_allow_stopped(ctx, ev);
}
static inline bool eval_emit_add_subdirectory_begin(EvalExecContext *ctx,
                                                    Event_Origin origin,
                                                    String_View source_dir,
                                                    String_View binary_dir,
                                                    bool exclude_from_all,
                                                    bool system) {
    Event ev = {0};
    ev.h.kind = EVENT_ADD_SUBDIRECTORY_BEGIN;
    ev.h.origin = origin;
    ev.as.add_subdirectory_begin.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.add_subdirectory_begin.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    ev.as.add_subdirectory_begin.exclude_from_all = exclude_from_all;
    ev.as.add_subdirectory_begin.system = system;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_add_subdirectory_end(EvalExecContext *ctx,
                                                  Event_Origin origin,
                                                  String_View source_dir,
                                                  String_View binary_dir,
                                                  bool success) {
    Event ev = {0};
    ev.h.kind = EVENT_ADD_SUBDIRECTORY_END;
    ev.h.origin = origin;
    ev.as.add_subdirectory_end.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.add_subdirectory_end.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    ev.as.add_subdirectory_end.success = success;
    return eval_emit_event_allow_stopped(ctx, ev);
}
static inline bool eval_emit_directory_property_mutate(EvalExecContext *ctx,
                                                       Event_Origin origin,
                                                       String_View property_name,
                                                       Event_Property_Mutate_Op op,
                                                       uint32_t modifier_flags,
                                                       String_View *items,
                                                       size_t item_count) {
    Event ev = {0};
    ev.h.kind = EVENT_DIRECTORY_PROPERTY_MUTATE;
    ev.h.origin = origin;
    ev.as.directory_property_mutate.property_name = property_name;
    ev.as.directory_property_mutate.op = op;
    ev.as.directory_property_mutate.modifier_flags = modifier_flags;
    ev.as.directory_property_mutate.items = items;
    ev.as.directory_property_mutate.item_count = item_count;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_global_property_mutate(EvalExecContext *ctx,
                                                    Event_Origin origin,
                                                    String_View property_name,
                                                    Event_Property_Mutate_Op op,
                                                    uint32_t modifier_flags,
                                                    String_View *items,
                                                    size_t item_count) {
    Event ev = {0};
    ev.h.kind = EVENT_GLOBAL_PROPERTY_MUTATE;
    ev.h.origin = origin;
    ev.as.global_property_mutate.property_name = property_name;
    ev.as.global_property_mutate.op = op;
    ev.as.global_property_mutate.modifier_flags = modifier_flags;
    ev.as.global_property_mutate.items = items;
    ev.as.global_property_mutate.item_count = item_count;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_dir_push(EvalExecContext *ctx,
                                      Event_Origin origin,
                                      String_View source_dir,
                                      String_View binary_dir) {
    Event ev = {0};
    ev.h.kind = EVENT_DIR_PUSH;
    ev.h.origin = origin;
    ev.as.dir_push.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.dir_push.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_dir_pop(EvalExecContext *ctx,
                                     Event_Origin origin,
                                     String_View source_dir,
                                     String_View binary_dir) {
    Event ev = {0};
    ev.h.kind = EVENT_DIR_POP;
    ev.h.origin = origin;
    ev.as.dir_pop.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.dir_pop.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    return eval_emit_event_allow_stopped(ctx, ev);
}
static inline bool eval_emit_command_begin(EvalExecContext *ctx,
                                           Event_Origin origin,
                                           String_View command_name,
                                           Event_Command_Dispatch_Kind dispatch_kind,
                                           uint32_t argc) {
    Event ev = {0};
    ev.h.kind = EVENT_COMMAND_BEGIN;
    ev.h.origin = origin;
    ev.as.command_begin.command_name = command_name;
    ev.as.command_begin.dispatch_kind = dispatch_kind;
    ev.as.command_begin.argc = argc;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_command_end(EvalExecContext *ctx,
                                         Event_Origin origin,
                                         String_View command_name,
                                         Event_Command_Dispatch_Kind dispatch_kind,
                                         uint32_t argc,
                                         Event_Command_Status status) {
    Event ev = {0};
    ev.h.kind = EVENT_COMMAND_END;
    ev.h.origin = origin;
    ev.as.command_end.command_name = command_name;
    ev.as.command_end.dispatch_kind = dispatch_kind;
    ev.as.command_end.argc = argc;
    ev.as.command_end.status = status;
    return eval_emit_event_allow_stopped(ctx, ev);
}
static inline bool eval_emit_command_call(EvalExecContext *ctx,
                                          Event_Origin origin,
                                          String_View command_name) {
    return eval_emit_command_begin(ctx,
                                   origin,
                                   command_name,
                                   EVENT_COMMAND_DISPATCH_BUILTIN,
                                   0);
}
static inline bool eval_emit_cmake_language_call(EvalExecContext *ctx,
                                                 Event_Origin origin,
                                                 String_View command_name) {
    Event ev = {0};
    ev.h.kind = EVENT_CMAKE_LANGUAGE_CALL;
    ev.h.origin = origin;
    ev.as.cmake_language_call.command_name = sv_copy_to_event_arena(ctx, command_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cmake_language_eval(EvalExecContext *ctx,
                                                 Event_Origin origin,
                                                 String_View code) {
    Event ev = {0};
    ev.h.kind = EVENT_CMAKE_LANGUAGE_EVAL;
    ev.h.origin = origin;
    ev.as.cmake_language_eval.code = sv_copy_to_event_arena(ctx, code);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cmake_language_defer_queue(EvalExecContext *ctx,
                                                        Event_Origin origin,
                                                        String_View defer_id,
                                                        String_View command_name) {
    Event ev = {0};
    ev.h.kind = EVENT_CMAKE_LANGUAGE_DEFER_QUEUE;
    ev.h.origin = origin;
    ev.as.cmake_language_defer_queue.defer_id = sv_copy_to_event_arena(ctx, defer_id);
    ev.as.cmake_language_defer_queue.command_name = sv_copy_to_event_arena(ctx, command_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_write_file(EvalExecContext *ctx,
                                           Event_Origin origin,
                                           String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_WRITE_FILE;
    ev.h.origin = origin;
    ev.as.fs_write_file.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_append_file(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_APPEND_FILE;
    ev.h.origin = origin;
    ev.as.fs_append_file.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_read_file(EvalExecContext *ctx,
                                          Event_Origin origin,
                                          String_View path,
                                          String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_READ_FILE;
    ev.h.origin = origin;
    ev.as.fs_read_file.path = sv_copy_to_event_arena(ctx, path);
    ev.as.fs_read_file.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_glob(EvalExecContext *ctx,
                                     Event_Origin origin,
                                     String_View out_var,
                                     String_View base_dir,
                                     bool recursive) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_GLOB;
    ev.h.origin = origin;
    ev.as.fs_glob.out_var = sv_copy_to_event_arena(ctx, out_var);
    ev.as.fs_glob.base_dir = sv_copy_to_event_arena(ctx, base_dir);
    ev.as.fs_glob.recursive = recursive;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_mkdir(EvalExecContext *ctx,
                                      Event_Origin origin,
                                      String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_MKDIR;
    ev.h.origin = origin;
    ev.as.fs_mkdir.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_remove(EvalExecContext *ctx,
                                       Event_Origin origin,
                                       String_View path,
                                       bool recursive) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_REMOVE;
    ev.h.origin = origin;
    ev.as.fs_remove.path = sv_copy_to_event_arena(ctx, path);
    ev.as.fs_remove.recursive = recursive;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_copy(EvalExecContext *ctx,
                                     Event_Origin origin,
                                     String_View source,
                                     String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_COPY;
    ev.h.origin = origin;
    ev.as.fs_copy.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_copy.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_rename(EvalExecContext *ctx,
                                       Event_Origin origin,
                                       String_View source,
                                       String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_RENAME;
    ev.h.origin = origin;
    ev.as.fs_rename.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_rename.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_create_link(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View source,
                                            String_View destination,
                                            bool symbolic) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_CREATE_LINK;
    ev.h.origin = origin;
    ev.as.fs_create_link.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_create_link.destination = sv_copy_to_event_arena(ctx, destination);
    ev.as.fs_create_link.symbolic = symbolic;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_chmod(EvalExecContext *ctx,
                                      Event_Origin origin,
                                      String_View path,
                                      bool recursive) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_CHMOD;
    ev.h.origin = origin;
    ev.as.fs_chmod.path = sv_copy_to_event_arena(ctx, path);
    ev.as.fs_chmod.recursive = recursive;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_archive_create(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_ARCHIVE_CREATE;
    ev.h.origin = origin;
    ev.as.fs_archive_create.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_archive_extract(EvalExecContext *ctx,
                                                Event_Origin origin,
                                                String_View path,
                                                String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_ARCHIVE_EXTRACT;
    ev.h.origin = origin;
    ev.as.fs_archive_extract.path = sv_copy_to_event_arena(ctx, path);
    ev.as.fs_archive_extract.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_transfer_download(EvalExecContext *ctx,
                                                  Event_Origin origin,
                                                  String_View source,
                                                  String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_TRANSFER_DOWNLOAD;
    ev.h.origin = origin;
    ev.as.fs_transfer_download.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_transfer_download.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_transfer_upload(EvalExecContext *ctx,
                                                Event_Origin origin,
                                                String_View source,
                                                String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_TRANSFER_UPLOAD;
    ev.h.origin = origin;
    ev.as.fs_transfer_upload.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_transfer_upload.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_proc_exec_request(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               String_View command,
                                               String_View working_directory) {
    Event ev = {0};
    ev.h.kind = EVENT_PROC_EXEC_REQUEST;
    ev.h.origin = origin;
    ev.as.proc_exec_request.command = sv_copy_to_event_arena(ctx, command);
    ev.as.proc_exec_request.working_directory = sv_copy_to_event_arena(ctx, working_directory);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_proc_exec_result(EvalExecContext *ctx,
                                              Event_Origin origin,
                                              String_View command,
                                              String_View result_code,
                                              String_View stdout_text,
                                              String_View stderr_text,
                                              bool had_error) {
    Event ev = {0};
    ev.h.kind = EVENT_PROC_EXEC_RESULT;
    ev.h.origin = origin;
    ev.as.proc_exec_result.command = sv_copy_to_event_arena(ctx, command);
    ev.as.proc_exec_result.result_code = sv_copy_to_event_arena(ctx, result_code);
    ev.as.proc_exec_result.stdout_text = sv_copy_to_event_arena(ctx, stdout_text);
    ev.as.proc_exec_result.stderr_text = sv_copy_to_event_arena(ctx, stderr_text);
    ev.as.proc_exec_result.had_error = had_error;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_if_eval(EvalExecContext *ctx,
                                          Event_Origin origin,
                                          bool result) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_IF_EVAL;
    ev.h.origin = origin;
    ev.as.flow_if_eval.result = result;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_branch_taken(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               String_View branch_kind) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_BRANCH_TAKEN;
    ev.h.origin = origin;
    ev.as.flow_branch_taken.branch_kind = sv_copy_to_event_arena(ctx, branch_kind);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_loop_begin(EvalExecContext *ctx,
                                             Event_Origin origin,
                                             String_View loop_kind) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_LOOP_BEGIN;
    ev.h.origin = origin;
    ev.as.flow_loop_begin.loop_kind = sv_copy_to_event_arena(ctx, loop_kind);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_loop_end(EvalExecContext *ctx,
                                           Event_Origin origin,
                                           String_View loop_kind,
                                           uint32_t iterations) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_LOOP_END;
    ev.h.origin = origin;
    ev.as.flow_loop_end.loop_kind = sv_copy_to_event_arena(ctx, loop_kind);
    ev.as.flow_loop_end.iterations = iterations;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_break(EvalExecContext *ctx,
                                        Event_Origin origin,
                                        uint32_t loop_depth) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_BREAK;
    ev.h.origin = origin;
    ev.as.flow_break.loop_depth = loop_depth;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_continue(EvalExecContext *ctx,
                                           Event_Origin origin,
                                           uint32_t loop_depth) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_CONTINUE;
    ev.h.origin = origin;
    ev.as.flow_continue.loop_depth = loop_depth;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_defer_queue(EvalExecContext *ctx,
                                              Event_Origin origin,
                                              String_View defer_id,
                                              String_View command_name) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_DEFER_QUEUE;
    ev.h.origin = origin;
    ev.as.flow_defer_queue.defer_id = sv_copy_to_event_arena(ctx, defer_id);
    ev.as.flow_defer_queue.command_name = sv_copy_to_event_arena(ctx, command_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_defer_flush(EvalExecContext *ctx,
                                              Event_Origin origin,
                                              uint32_t call_count) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_DEFER_FLUSH;
    ev.h.origin = origin;
    ev.as.flow_defer_flush.call_count = call_count;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_block_begin(EvalExecContext *ctx,
                                              Event_Origin origin,
                                              bool variable_scope_pushed,
                                              bool policy_scope_pushed,
                                              bool has_propagate_vars) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_BLOCK_BEGIN;
    ev.h.origin = origin;
    ev.as.flow_block_begin.variable_scope_pushed = variable_scope_pushed;
    ev.as.flow_block_begin.policy_scope_pushed = policy_scope_pushed;
    ev.as.flow_block_begin.has_propagate_vars = has_propagate_vars;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_block_end(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            bool propagate_on_return,
                                            bool had_propagate_vars) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_BLOCK_END;
    ev.h.origin = origin;
    ev.as.flow_block_end.propagate_on_return = propagate_on_return;
    ev.as.flow_block_end.had_propagate_vars = had_propagate_vars;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_function_begin(EvalExecContext *ctx,
                                                 Event_Origin origin,
                                                 String_View name,
                                                 uint32_t argc) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_FUNCTION_BEGIN;
    ev.h.origin = origin;
    ev.as.flow_function_begin.name = sv_copy_to_event_arena(ctx, name);
    ev.as.flow_function_begin.argc = argc;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_function_end(EvalExecContext *ctx,
                                               Event_Origin origin,
                                               String_View name,
                                               bool returned) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_FUNCTION_END;
    ev.h.origin = origin;
    ev.as.flow_function_end.name = sv_copy_to_event_arena(ctx, name);
    ev.as.flow_function_end.returned = returned;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_macro_begin(EvalExecContext *ctx,
                                              Event_Origin origin,
                                              String_View name,
                                              uint32_t argc) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_MACRO_BEGIN;
    ev.h.origin = origin;
    ev.as.flow_macro_begin.name = sv_copy_to_event_arena(ctx, name);
    ev.as.flow_macro_begin.argc = argc;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_macro_end(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View name,
                                            bool returned) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_MACRO_END;
    ev.h.origin = origin;
    ev.as.flow_macro_end.name = sv_copy_to_event_arena(ctx, name);
    ev.as.flow_macro_end.returned = returned;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_replace(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_REPLACE;
    ev.h.origin = origin;
    ev.as.string_replace.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_configure(EvalExecContext *ctx,
                                              Event_Origin origin,
                                              String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_CONFIGURE;
    ev.h.origin = origin;
    ev.as.string_configure.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_regex(EvalExecContext *ctx,
                                          Event_Origin origin,
                                          String_View mode,
                                          String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_REGEX;
    ev.h.origin = origin;
    ev.as.string_regex.mode = sv_copy_to_event_arena(ctx, mode);
    ev.as.string_regex.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_hash(EvalExecContext *ctx,
                                         Event_Origin origin,
                                         String_View algorithm,
                                         String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_HASH;
    ev.h.origin = origin;
    ev.as.string_hash.algorithm = sv_copy_to_event_arena(ctx, algorithm);
    ev.as.string_hash.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_timestamp(EvalExecContext *ctx,
                                              Event_Origin origin,
                                              String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_TIMESTAMP;
    ev.h.origin = origin;
    ev.as.string_timestamp.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_append(EvalExecContext *ctx,
                                         Event_Origin origin,
                                         String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_APPEND;
    ev.h.origin = origin;
    ev.as.list_append.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_prepend(EvalExecContext *ctx,
                                          Event_Origin origin,
                                          String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_PREPEND;
    ev.h.origin = origin;
    ev.as.list_prepend.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_insert(EvalExecContext *ctx,
                                         Event_Origin origin,
                                         String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_INSERT;
    ev.h.origin = origin;
    ev.as.list_insert.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_remove(EvalExecContext *ctx,
                                         Event_Origin origin,
                                         String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_REMOVE;
    ev.h.origin = origin;
    ev.as.list_remove.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_transform(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_TRANSFORM;
    ev.h.origin = origin;
    ev.as.list_transform.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_sort(EvalExecContext *ctx,
                                       Event_Origin origin,
                                       String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_SORT;
    ev.h.origin = origin;
    ev.as.list_sort.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_math_expr(EvalExecContext *ctx,
                                       Event_Origin origin,
                                       String_View out_var,
                                       String_View format) {
    Event ev = {0};
    ev.h.kind = EVENT_MATH_EXPR;
    ev.h.origin = origin;
    ev.as.math_expr.out_var = sv_copy_to_event_arena(ctx, out_var);
    ev.as.math_expr.format = sv_copy_to_event_arena(ctx, format);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_path_normalize(EvalExecContext *ctx,
                                            Event_Origin origin,
                                            String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_PATH_NORMALIZE;
    ev.h.origin = origin;
    ev.as.path_normalize.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_path_compare(EvalExecContext *ctx,
                                          Event_Origin origin,
                                          String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_PATH_COMPARE;
    ev.h.origin = origin;
    ev.as.path_compare.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_path_convert(EvalExecContext *ctx,
                                          Event_Origin origin,
                                          String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_PATH_CONVERT;
    ev.h.origin = origin;
    ev.as.path_convert.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
Eval_Result eval_emit_diag(EvalExecContext *ctx,
                           Eval_Diag_Code code,
                           String_View component,
                           String_View command,
                           Event_Origin origin,
                           String_View cause,
                           String_View hint);
Eval_Result eval_emit_diag_with_severity(EvalExecContext *ctx,
                                         Event_Diag_Severity sev,
                                         Eval_Diag_Code code,
                                         String_View component,
                                         String_View command,
                                         Event_Origin origin,
                                         String_View cause,
                                         String_View hint);
String_View eval_diag_code_to_sv(Eval_Diag_Code code);
Eval_Error_Class eval_diag_error_class(Eval_Diag_Code code);
Cmake_Diag_Severity eval_diag_default_severity(Eval_Diag_Code code);
bool eval_diag_counts_as_unsupported(Eval_Diag_Code code);
String_View eval_error_class_to_sv(Eval_Error_Class cls);
void eval_report_reset(EvalExecContext *ctx);
void eval_report_record_diag(EvalExecContext *ctx,
                             Event_Diag_Severity input_sev,
                             Event_Diag_Severity effective_sev,
                             Eval_Diag_Code code);
void eval_report_finalize(EvalExecContext *ctx);
bool eval_command_caps_lookup(const EvalExecContext *ctx, String_View name, Command_Capability *out_capability);
bool eval_append_configure_log(EvalExecContext *ctx, const Node *node, String_View msg);
bool eval_command_tx_begin(EvalExecContext *ctx, Eval_Command_Transaction *tx);
void eval_command_tx_preserve_scope_vars_on_failure(EvalExecContext *ctx);
bool eval_command_tx_finish(EvalExecContext *ctx, Eval_Command_Transaction *tx, bool commit_state);
bool eval_command_tx_push_event(EvalExecContext *ctx, const Event *ev, bool allow_stopped);
bool eval_command_tx_push_diag(EvalExecContext *ctx,
                               Event_Diag_Severity input_sev,
                               Event_Diag_Severity effective_sev,
                               Eval_Diag_Code code,
                               String_View component,
                               String_View command,
                               Event_Origin origin,
                               String_View cause,
                               String_View hint);
bool eval_service_read_file(EvalExecContext *ctx,
                            String_View path,
                            String_View *out_contents,
                            bool *out_found);
bool eval_service_write_file(EvalExecContext *ctx,
                             String_View path,
                             String_View contents,
                             bool append);
bool eval_service_mkdir(EvalExecContext *ctx, String_View path);
bool eval_service_file_exists(EvalExecContext *ctx, String_View path, bool *out_exists);
bool eval_service_copy_file(EvalExecContext *ctx, String_View src, String_View dst);
bool eval_service_host_read_file(EvalExecContext *ctx,
                                 String_View path,
                                 String_View *out_contents,
                                 bool *out_found);
bool eval_service_host_query_windows_registry(
    EvalExecContext *ctx,
    const Eval_Windows_Registry_Query_Request *request,
    Eval_Windows_Registry_Query_Result *out_result);

// ---- vars ----
String_View eval_var_get_visible(EvalExecContext *ctx, String_View key);
static inline String_View eval_var_get(EvalExecContext *ctx, String_View key) {
    return eval_var_get_visible(ctx, key);
}
bool eval_var_defined_visible(EvalExecContext *ctx, String_View key);
bool eval_var_set_current(EvalExecContext *ctx, String_View key, String_View value);
bool eval_var_unset_current(EvalExecContext *ctx, String_View key);
bool eval_var_defined_current(EvalExecContext *ctx, String_View key);
bool eval_var_collect_visible_names(EvalExecContext *ctx, SV_List *out_names);
bool eval_cache_defined(EvalExecContext *ctx, String_View key);
bool eval_cache_set(EvalExecContext *ctx,
                    String_View key,
                    String_View value,
                    String_View type,
                    String_View doc);

static inline String_View eval_current_source_dir(EvalExecContext *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR));
    const Eval_Exec_Context *exec = eval_exec_current_const(ctx);
    if (v.count == 0 && exec && exec->source_dir.count > 0) v = exec->source_dir;
    if (v.count == 0 && ctx) v = ctx->source_dir;
    return v;
}

static inline String_View eval_current_binary_dir(EvalExecContext *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR));
    const Eval_Exec_Context *exec = eval_exec_current_const(ctx);
    if (v.count == 0 && exec && exec->binary_dir.count > 0) v = exec->binary_dir;
    if (v.count == 0 && ctx) v = ctx->binary_dir;
    return v;
}

static inline String_View eval_current_list_dir(EvalExecContext *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR));
    const Eval_Exec_Context *exec = eval_exec_current_const(ctx);
    if (v.count == 0 && exec && exec->list_dir.count > 0) v = exec->list_dir;
    if (v.count == 0 && ctx) v = ctx->source_dir;
    return v;
}

static inline String_View eval_current_list_file(EvalExecContext *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE));
    const Eval_Exec_Context *exec = eval_exec_current_const(ctx);
    if (v.count == 0 && exec && exec->current_file) v = nob_sv_from_cstr(exec->current_file);
    if (v.count == 0 && ctx && ctx->current_file) v = nob_sv_from_cstr(ctx->current_file);
    return v;
}

// ---- targets ----
bool eval_directory_register_node(EvalExecContext *ctx,
                                  String_View source_dir,
                                  String_View binary_dir,
                                  String_View parent_source_dir,
                                  String_View parent_binary_dir);
bool eval_directory_parent(EvalExecContext *ctx, String_View source_dir, String_View *out_parent_source_dir);
bool eval_directory_binary_dir(EvalExecContext *ctx, String_View source_dir, String_View *out_binary_dir);
bool eval_directory_note_target(EvalExecContext *ctx, String_View source_dir, String_View target_name);
bool eval_directory_note_test(EvalExecContext *ctx, String_View source_dir, String_View test_name);
bool eval_directory_capture_current_scope(EvalExecContext *ctx);
bool eval_target_known(EvalExecContext *ctx, String_View name);
bool eval_target_visible(EvalExecContext *ctx, String_View name);
bool eval_target_register(EvalExecContext *ctx, String_View name);
bool eval_target_set_type(EvalExecContext *ctx, String_View name, Cmake_Target_Type target_type);
bool eval_target_get_type(EvalExecContext *ctx, String_View name, Cmake_Target_Type *out_target_type);
bool eval_target_set_imported(EvalExecContext *ctx, String_View name, bool imported);
bool eval_target_set_imported_global(EvalExecContext *ctx, String_View name, bool imported_global);
bool eval_target_declared_dir(EvalExecContext *ctx, String_View name, String_View *out_dir);
bool eval_target_is_imported(EvalExecContext *ctx, String_View name);
bool eval_target_is_imported_global(EvalExecContext *ctx, String_View name);
bool eval_target_alias_known(EvalExecContext *ctx, String_View name);
bool eval_target_alias_register(EvalExecContext *ctx, String_View name);
bool eval_target_set_alias(EvalExecContext *ctx, String_View alias_name, String_View real_target);
bool eval_target_alias_of(EvalExecContext *ctx, String_View name, String_View *out_real_target);
bool eval_target_alias_is_global(EvalExecContext *ctx, String_View name);
bool eval_test_known(EvalExecContext *ctx, String_View name);
bool eval_test_known_in_directory(EvalExecContext *ctx, String_View name, String_View declared_dir);
bool eval_test_register(EvalExecContext *ctx, String_View name, String_View declared_dir);
bool eval_test_set_working_directory(EvalExecContext *ctx,
                                     String_View name,
                                     String_View declared_dir,
                                     String_View working_directory);
bool eval_test_working_directory(EvalExecContext *ctx,
                                 String_View name,
                                 String_View declared_dir,
                                 String_View *out_working_directory);
bool eval_install_component_known(EvalExecContext *ctx, String_View name);
bool eval_install_component_register(EvalExecContext *ctx, String_View name);
bool eval_property_define(EvalExecContext *ctx, const Eval_Property_Definition *definition);
bool eval_property_is_defined(EvalExecContext *ctx, String_View scope_upper, String_View property_name);
bool eval_target_apply_defined_initializers(EvalExecContext *ctx, Event_Origin origin, String_View target_name);
bool eval_property_write(EvalExecContext *ctx,
                         Event_Origin origin,
                         String_View scope_upper,
                         String_View object_id,
                         String_View property_name,
                         String_View value,
                         Cmake_Target_Property_Op op,
                         bool emit_var_event);
bool eval_property_query_mode_parse(EvalExecContext *ctx,
                                    const Node *node,
                                    Event_Origin origin,
                                    String_View token,
                                    Eval_Property_Query_Mode *out_mode);
bool eval_property_query(EvalExecContext *ctx,
                         const Node *node,
                         Event_Origin origin,
                         String_View out_var,
                         String_View scope_upper,
                         String_View object_id,
                         String_View validation_object,
                         String_View property_name,
                         Eval_Property_Query_Mode mode,
                         String_View inherit_directory,
                         bool validate_object,
                         Eval_Property_Query_Missing_Behavior missing_behavior);
bool eval_property_query_cmake(EvalExecContext *ctx,
                               const Node *node,
                               Event_Origin origin,
                               String_View out_var,
                               String_View property_name);
bool eval_property_engine_set(EvalExecContext *ctx,
                              String_View scope_upper,
                              String_View object_id,
                              String_View property_upper,
                              String_View value);
bool eval_property_engine_get(EvalExecContext *ctx,
                              String_View scope_upper,
                              String_View object_id,
                              String_View property_upper,
                              String_View *out_value,
                              bool *out_set);

// ---- native commands ----
Eval_Native_Command *eval_native_cmd_find(EvalExecContext *ctx, String_View name);
const Eval_Native_Command *eval_native_cmd_find_const(const EvalExecContext *ctx, String_View name);
const Eval_Native_Command *eval_registry_find_const(const EvalRegistry *registry, String_View name);
bool eval_registry_register_internal(EvalRegistry *registry,
                                     const EvalNativeCommandDef *def,
                                     bool is_builtin);
bool eval_registry_unregister_internal(EvalRegistry *registry,
                                       String_View name,
                                       bool allow_builtin_remove);
bool eval_native_cmd_register_internal(EvalExecContext *ctx,
                                       const EvalNativeCommandDef *def,
                                       bool is_builtin,
                                       bool allow_during_run);
bool eval_native_cmd_unregister_internal(EvalExecContext *ctx,
                                         String_View name,
                                         bool allow_builtin_remove,
                                         bool allow_during_run);

// ---- user commands ----
bool eval_user_cmd_register(EvalExecContext *ctx, const Node *node);
bool eval_user_cmd_invoke(EvalExecContext *ctx, String_View name, const SV_List *args, Cmake_Event_Origin origin);
User_Command *eval_user_cmd_find(EvalExecContext *ctx, String_View name);

// ---- utilitários compartilhados ----
bool eval_sv_key_eq(String_View a, String_View b);
bool eval_sv_eq_ci_lit(String_View a, const char *lit);
bool eval_directory_register_known(EvalExecContext *ctx, String_View dir);
bool eval_directory_is_known(EvalExecContext *ctx, String_View dir);
String_View eval_directory_known_source_dir_temp(EvalExecContext *ctx, String_View dir);
String_View eval_test_scoped_marker_key_temp(EvalExecContext *ctx,
                                             String_View scope_dir,
                                             String_View test_name);
String_View eval_normalize_compile_definition_item(String_View item);
String_View eval_current_source_dir_for_paths(EvalExecContext *ctx);
String_View eval_property_upper_name_temp(EvalExecContext *ctx, String_View name);
bool eval_property_scope_upper_temp(EvalExecContext *ctx, String_View raw_scope, String_View *out_scope_upper);
String_View eval_property_scoped_object_id_temp(EvalExecContext *ctx,
                                                const char *prefix,
                                                String_View scope_object,
                                                String_View item_object);
const Eval_Property_Definition *eval_property_definition_find(EvalExecContext *ctx,
                                                              String_View scope_upper,
                                                              String_View property_name);
bool eval_test_exists_in_directory_scope(EvalExecContext *ctx, String_View test_name, String_View scope_dir);
bool eval_semver_parse_strict(String_View version_token, Eval_Semver *out_version);
int eval_semver_compare(const Eval_Semver *lhs, const Eval_Semver *rhs);
String_View eval_detect_host_system_name(void);
String_View eval_detect_host_processor(void);
bool eval_host_hostname_temp(EvalExecContext *ctx, String_View *out_hostname);
bool eval_host_memory_info(Eval_Host_Memory_Info *out_info);
bool eval_host_logical_cores(size_t *out_count);
String_View eval_host_os_release_temp(EvalExecContext *ctx);
String_View eval_host_os_version_temp(EvalExecContext *ctx);
String_View eval_sv_join_semi_temp(EvalExecContext *ctx, String_View *items, size_t count);
bool eval_sv_split_semicolon_genex_aware(Arena *arena, String_View input, SV_List *out);
bool eval_split_shell_like_temp(EvalExecContext *ctx, String_View input, SV_List *out);
bool eval_split_command_line_temp(EvalExecContext *ctx, Eval_Cmdline_Mode mode, String_View input, SV_List *out_tokens);
bool eval_split_program_from_command_line_temp(EvalExecContext *ctx,
                                               Eval_Cmdline_Mode mode,
                                               String_View input,
                                               String_View *out_program,
                                               String_View *out_args);
bool eval_find_program_full_path_temp(EvalExecContext *ctx,
                                      String_View token,
                                      String_View *out_program,
                                      bool *out_found);
bool eval_process_run_capture(EvalExecContext *ctx, const Eval_Process_Run_Request *req, Eval_Process_Run_Result *out);
bool eval_process_run_nob_cmd_capture(EvalExecContext *ctx,
                                      const Nob_Cmd *cmd,
                                      String_View working_directory,
                                      String_View stdin_data,
                                      Eval_Process_Run_Result *out);
bool eval_process_env_set(EvalExecContext *ctx, String_View name, String_View value);
bool eval_process_env_unset(EvalExecContext *ctx, String_View name);
String_View eval_process_cwd_temp(EvalExecContext *ctx);
bool eval_sv_is_abs_path(String_View p);
String_View eval_sv_path_join(Arena *arena, String_View a, String_View b);
String_View eval_sv_path_normalize_temp(EvalExecContext *ctx, String_View input);
bool eval_list_dir_sources_sorted_temp(EvalExecContext *ctx, String_View dir, SV_List *out_sources);
bool eval_mkdirs_for_parent(EvalExecContext *ctx, String_View path);
bool eval_write_text_file(EvalExecContext *ctx, String_View path, String_View contents, bool append);
bool eval_ctest_publish_metadata(EvalExecContext *ctx, String_View command_name, const SV_List *argv, String_View status);
bool eval_legacy_publish_args(EvalExecContext *ctx, String_View command_name, const SV_List *argv);
void eval_canonical_draft_init(Eval_Canonical_Draft *draft);
bool eval_canonical_draft_add_artifact(EvalExecContext *ctx,
                                       Eval_Canonical_Draft *draft,
                                       const Eval_Canonical_Artifact *artifact,
                                       size_t *out_index);
bool eval_canonical_draft_add_ctest_step(EvalExecContext *ctx,
                                         Eval_Canonical_Draft *draft,
                                         const Eval_Ctest_Step_Record *step);
bool eval_canonical_draft_commit(EvalExecContext *ctx, const Eval_Canonical_Draft *draft);
const Eval_Canonical_Artifact *eval_canonical_artifact_at(const EvalExecContext *ctx, size_t index);
const Eval_Ctest_Step_Record *eval_ctest_find_last_step_by_command(const EvalExecContext *ctx,
                                                                   String_View command_name);
const Eval_Ctest_Step_Record *eval_ctest_find_last_step_by_part(const EvalExecContext *ctx,
                                                                String_View submit_part);
String_View eval_ctest_step_submit_files(EvalExecContext *ctx, const Eval_Ctest_Step_Record *step);
String_View eval_path_resolve_for_cmake_arg(EvalExecContext *ctx,
                                            String_View raw_path,
                                            String_View base_dir,
                                            bool preserve_generator_expressions);
const char *eval_getenv_temp(EvalExecContext *ctx, const char *name);
bool eval_has_env(EvalExecContext *ctx, const char *name);
bool eval_real_path_resolve_temp(EvalExecContext *ctx,
                                 String_View path,
                                 bool cmp0152_new,
                                 String_View *out_path);

// ---- macro argument substitution (textual) ----
bool eval_macro_frame_push(EvalExecContext *ctx);
void eval_macro_frame_pop(EvalExecContext *ctx);
bool eval_macro_bind_set(EvalExecContext *ctx, String_View key, String_View value);
bool eval_macro_bind_get(EvalExecContext *ctx, String_View key, String_View *out_value);

// ---- Gerenciamento de Escopo ----
bool eval_scope_push(EvalExecContext *ctx);
void eval_scope_pop(EvalExecContext *ctx);

bool eval_defer_push_directory(EvalExecContext *ctx, String_View source_dir, String_View binary_dir);
bool eval_defer_pop_directory(EvalExecContext *ctx);
bool eval_defer_flush_current_directory(EvalExecContext *ctx);

bool eval_policy_is_id(String_View policy_id);
bool eval_policy_is_known(String_View policy_id);
bool eval_policy_get_intro_version(String_View policy_id, int *major, int *minor, int *patch);
bool eval_policy_push(EvalExecContext *ctx);
bool eval_policy_pop(EvalExecContext *ctx);
bool eval_policy_set_status(EvalExecContext *ctx, String_View policy_id, Eval_Policy_Status status);
bool eval_policy_set(EvalExecContext *ctx, String_View policy_id, String_View value);
String_View eval_policy_get_effective(EvalExecContext *ctx, String_View policy_id);

// ---- Execução Externa (Subdiretórios e Includes) ----
// Retorna false em caso de OOM ou erro fatal.
Eval_Result eval_execute_file(EvalExecContext *ctx, String_View file_path, bool is_add_subdirectory, String_View explicit_bin_dir);
Eval_Result eval_run_ast_inline(EvalExecContext *ctx, Ast_Root ast);

#ifdef __cplusplus
}
#endif

#endif // EVALUATOR_INTERNAL_H_
