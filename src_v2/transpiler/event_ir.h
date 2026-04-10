// Canonical evaluator Event IR.
#ifndef EVENT_IR_H_
#define EVENT_IR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "nob.h"

typedef enum {
    EVENT_ROLE_NONE = 0,
    EVENT_ROLE_TRACE = 1u << 0,
    EVENT_ROLE_DIAGNOSTIC = 1u << 1,
    EVENT_ROLE_RUNTIME_EFFECT = 1u << 2,
    EVENT_ROLE_STATE = 1u << 3,
    EVENT_ROLE_BUILD_SEMANTIC = 1u << 4,
} Event_Role;

#define EVENT_FAMILY_LIST(X) \
    X(EVENT_FAMILY_TRACE, "trace") \
    X(EVENT_FAMILY_DIAG, "diag") \
    X(EVENT_FAMILY_DIRECTORY, "directory") \
    X(EVENT_FAMILY_BUILD_GRAPH, "build_graph") \
    X(EVENT_FAMILY_FLOW, "flow") \
    X(EVENT_FAMILY_SCOPE, "scope") \
    X(EVENT_FAMILY_POLICY, "policy") \
    X(EVENT_FAMILY_VAR, "var") \
    X(EVENT_FAMILY_FS, "fs") \
    X(EVENT_FAMILY_PROC, "proc") \
    X(EVENT_FAMILY_STRING, "string") \
    X(EVENT_FAMILY_LIST, "list") \
    X(EVENT_FAMILY_MATH, "math") \
    X(EVENT_FAMILY_PATH, "path") \
    X(EVENT_FAMILY_PROJECT, "project") \
    X(EVENT_FAMILY_TARGET, "target") \
    X(EVENT_FAMILY_TEST, "test") \
    X(EVENT_FAMILY_INSTALL, "install") \
    X(EVENT_FAMILY_CPACK, "cpack") \
    X(EVENT_FAMILY_PACKAGE, "package") \
    X(EVENT_FAMILY_EXPORT, "export")

typedef enum {
#define DECLARE_EVENT_FAMILY(kind, label) kind,
    EVENT_FAMILY_LIST(DECLARE_EVENT_FAMILY)
#undef DECLARE_EVENT_FAMILY
    // Canonical families are append-only; add new entries above this sentinel.
    EVENT_FAMILY_COUNT
} Event_Family;

#define EVENT_KIND_LIST(X) \
    X(EVENT_DIAG, EVENT_FAMILY_DIAG, "diag", EVENT_ROLE_DIAGNOSTIC) \
    X(EVENT_COMMAND_BEGIN, EVENT_FAMILY_TRACE, "command_begin", EVENT_ROLE_TRACE) \
    X(EVENT_COMMAND_END, EVENT_FAMILY_TRACE, "command_end", EVENT_ROLE_TRACE) \
    X(EVENT_INCLUDE_BEGIN, EVENT_FAMILY_TRACE, "include_begin", EVENT_ROLE_TRACE) \
    X(EVENT_INCLUDE_END, EVENT_FAMILY_TRACE, "include_end", EVENT_ROLE_TRACE) \
    X(EVENT_ADD_SUBDIRECTORY_BEGIN, EVENT_FAMILY_TRACE, "add_subdirectory_begin", EVENT_ROLE_TRACE) \
    X(EVENT_ADD_SUBDIRECTORY_END, EVENT_FAMILY_TRACE, "add_subdirectory_end", EVENT_ROLE_TRACE) \
    X(EVENT_CMAKE_LANGUAGE_CALL, EVENT_FAMILY_TRACE, "cmake_language_call", EVENT_ROLE_TRACE) \
    X(EVENT_CMAKE_LANGUAGE_EVAL, EVENT_FAMILY_TRACE, "cmake_language_eval", EVENT_ROLE_TRACE) \
    X(EVENT_CMAKE_LANGUAGE_DEFER_QUEUE, EVENT_FAMILY_TRACE, "cmake_language_defer_queue", EVENT_ROLE_TRACE) \
    X(EVENT_DIRECTORY_ENTER, EVENT_FAMILY_DIRECTORY, "directory_enter", EVENT_ROLE_TRACE | EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_DIRECTORY_LEAVE, EVENT_FAMILY_DIRECTORY, "directory_leave", EVENT_ROLE_TRACE | EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_DIRECTORY_PROPERTY_MUTATE, EVENT_FAMILY_DIRECTORY, "directory_property_mutate", EVENT_ROLE_STATE | EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_GLOBAL_PROPERTY_MUTATE, EVENT_FAMILY_DIRECTORY, "global_property_mutate", EVENT_ROLE_STATE | EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_VAR_SET, EVENT_FAMILY_VAR, "var_set", EVENT_ROLE_STATE) \
    X(EVENT_VAR_UNSET, EVENT_FAMILY_VAR, "var_unset", EVENT_ROLE_STATE) \
    X(EVENT_SCOPE_PUSH, EVENT_FAMILY_SCOPE, "scope_push", EVENT_ROLE_STATE) \
    X(EVENT_SCOPE_POP, EVENT_FAMILY_SCOPE, "scope_pop", EVENT_ROLE_STATE) \
    X(EVENT_POLICY_PUSH, EVENT_FAMILY_POLICY, "policy_push", EVENT_ROLE_STATE) \
    X(EVENT_POLICY_POP, EVENT_FAMILY_POLICY, "policy_pop", EVENT_ROLE_STATE) \
    X(EVENT_POLICY_SET, EVENT_FAMILY_POLICY, "policy_set", EVENT_ROLE_STATE) \
    X(EVENT_FLOW_RETURN, EVENT_FAMILY_FLOW, "flow_return", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_IF_EVAL, EVENT_FAMILY_FLOW, "flow_if_eval", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_BRANCH_TAKEN, EVENT_FAMILY_FLOW, "flow_branch_taken", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_LOOP_BEGIN, EVENT_FAMILY_FLOW, "flow_loop_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_LOOP_END, EVENT_FAMILY_FLOW, "flow_loop_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_BREAK, EVENT_FAMILY_FLOW, "flow_break", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_CONTINUE, EVENT_FAMILY_FLOW, "flow_continue", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_DEFER_QUEUE, EVENT_FAMILY_FLOW, "flow_defer_queue", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_DEFER_FLUSH, EVENT_FAMILY_FLOW, "flow_defer_flush", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_BLOCK_BEGIN, EVENT_FAMILY_FLOW, "flow_block_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_BLOCK_END, EVENT_FAMILY_FLOW, "flow_block_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_FUNCTION_BEGIN, EVENT_FAMILY_FLOW, "flow_function_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_FUNCTION_END, EVENT_FAMILY_FLOW, "flow_function_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_MACRO_BEGIN, EVENT_FAMILY_FLOW, "flow_macro_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FLOW_MACRO_END, EVENT_FAMILY_FLOW, "flow_macro_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE) \
    X(EVENT_FS_WRITE_FILE, EVENT_FAMILY_FS, "fs_write_file", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_APPEND_FILE, EVENT_FAMILY_FS, "fs_append_file", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_READ_FILE, EVENT_FAMILY_FS, "fs_read_file", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_GLOB, EVENT_FAMILY_FS, "fs_glob", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_MKDIR, EVENT_FAMILY_FS, "fs_mkdir", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_REMOVE, EVENT_FAMILY_FS, "fs_remove", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_COPY, EVENT_FAMILY_FS, "fs_copy", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_RENAME, EVENT_FAMILY_FS, "fs_rename", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_CREATE_LINK, EVENT_FAMILY_FS, "fs_create_link", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_CHMOD, EVENT_FAMILY_FS, "fs_chmod", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_ARCHIVE_CREATE, EVENT_FAMILY_FS, "fs_archive_create", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_ARCHIVE_EXTRACT, EVENT_FAMILY_FS, "fs_archive_extract", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_TRANSFER_DOWNLOAD, EVENT_FAMILY_FS, "fs_transfer_download", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_FS_TRANSFER_UPLOAD, EVENT_FAMILY_FS, "fs_transfer_upload", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_PROC_EXEC_REQUEST, EVENT_FAMILY_PROC, "proc_exec_request", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_PROC_EXEC_RESULT, EVENT_FAMILY_PROC, "proc_exec_result", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_STRING_REPLACE, EVENT_FAMILY_STRING, "string_replace", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_STRING_CONFIGURE, EVENT_FAMILY_STRING, "string_configure", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_STRING_REGEX, EVENT_FAMILY_STRING, "string_regex", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_STRING_HASH, EVENT_FAMILY_STRING, "string_hash", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_STRING_TIMESTAMP, EVENT_FAMILY_STRING, "string_timestamp", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_LIST_APPEND, EVENT_FAMILY_LIST, "list_append", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_LIST_PREPEND, EVENT_FAMILY_LIST, "list_prepend", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_LIST_INSERT, EVENT_FAMILY_LIST, "list_insert", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_LIST_REMOVE, EVENT_FAMILY_LIST, "list_remove", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_LIST_TRANSFORM, EVENT_FAMILY_LIST, "list_transform", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_LIST_SORT, EVENT_FAMILY_LIST, "list_sort", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_MATH_EXPR, EVENT_FAMILY_MATH, "math_expr", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_PATH_NORMALIZE, EVENT_FAMILY_PATH, "path_normalize", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_PATH_COMPARE, EVENT_FAMILY_PATH, "path_compare", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_PATH_CONVERT, EVENT_FAMILY_PATH, "path_convert", EVENT_ROLE_RUNTIME_EFFECT) \
    X(EVENT_TEST_ENABLE, EVENT_FAMILY_TEST, "test_enable", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TEST_ADD, EVENT_FAMILY_TEST, "test_add", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_INSTALL_RULE_ADD, EVENT_FAMILY_INSTALL, "install_rule_add", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_CPACK_ADD_INSTALL_TYPE, EVENT_FAMILY_CPACK, "cpack_add_install_type", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_CPACK_ADD_COMPONENT_GROUP, EVENT_FAMILY_CPACK, "cpack_add_component_group", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_CPACK_ADD_COMPONENT, EVENT_FAMILY_CPACK, "cpack_add_component", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_CPACK_PACKAGE_DECLARE, EVENT_FAMILY_CPACK, "cpack_package_declare", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_CPACK_PACKAGE_ADD_GENERATOR, EVENT_FAMILY_CPACK, "cpack_package_add_generator", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_PACKAGE_FIND_RESULT, EVENT_FAMILY_PACKAGE, "package_find_result", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_PROJECT_DECLARE, EVENT_FAMILY_PROJECT, "project_declare", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_PROJECT_MINIMUM_REQUIRED, EVENT_FAMILY_PROJECT, "project_minimum_required", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_DECLARE, EVENT_FAMILY_TARGET, "target_declare", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_ADD_SOURCE, EVENT_FAMILY_TARGET, "target_add_source", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_SOURCE_MARK_GENERATED, EVENT_FAMILY_BUILD_GRAPH, "source_mark_generated", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_ADD_DEPENDENCY, EVENT_FAMILY_TARGET, "target_add_dependency", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_BUILD_STEP_DECLARE, EVENT_FAMILY_BUILD_GRAPH, "build_step_declare", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_BUILD_STEP_ADD_OUTPUT, EVENT_FAMILY_BUILD_GRAPH, "build_step_add_output", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_BUILD_STEP_ADD_BYPRODUCT, EVENT_FAMILY_BUILD_GRAPH, "build_step_add_byproduct", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_BUILD_STEP_ADD_DEPENDENCY, EVENT_FAMILY_BUILD_GRAPH, "build_step_add_dependency", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_BUILD_STEP_ADD_COMMAND, EVENT_FAMILY_BUILD_GRAPH, "build_step_add_command", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_PROP_SET, EVENT_FAMILY_TARGET, "target_prop_set", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_LINK_LIBRARIES, EVENT_FAMILY_TARGET, "target_link_libraries", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_LINK_OPTIONS, EVENT_FAMILY_TARGET, "target_link_options", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_LINK_DIRECTORIES, EVENT_FAMILY_TARGET, "target_link_directories", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_INCLUDE_DIRECTORIES, EVENT_FAMILY_TARGET, "target_include_directories", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_COMPILE_DEFINITIONS, EVENT_FAMILY_TARGET, "target_compile_definitions", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_TARGET_COMPILE_OPTIONS, EVENT_FAMILY_TARGET, "target_compile_options", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_EXPORT_INSTALL, EVENT_FAMILY_EXPORT, "export_install", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_EXPORT_BUILD_DECLARE, EVENT_FAMILY_EXPORT, "export_build_declare", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_EXPORT_BUILD_ADD_TARGET, EVENT_FAMILY_EXPORT, "export_build_add_target", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_EXPORT_PACKAGE_REGISTRY, EVENT_FAMILY_EXPORT, "export_package_registry", EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_REPLAY_ACTION_DECLARE, EVENT_FAMILY_BUILD_GRAPH, "replay_action_declare", EVENT_ROLE_RUNTIME_EFFECT | EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_REPLAY_ACTION_ADD_INPUT, EVENT_FAMILY_BUILD_GRAPH, "replay_action_add_input", EVENT_ROLE_RUNTIME_EFFECT | EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_REPLAY_ACTION_ADD_OUTPUT, EVENT_FAMILY_BUILD_GRAPH, "replay_action_add_output", EVENT_ROLE_RUNTIME_EFFECT | EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_REPLAY_ACTION_ADD_ARGV, EVENT_FAMILY_BUILD_GRAPH, "replay_action_add_argv", EVENT_ROLE_RUNTIME_EFFECT | EVENT_ROLE_BUILD_SEMANTIC) \
    X(EVENT_REPLAY_ACTION_ADD_ENV, EVENT_FAMILY_BUILD_GRAPH, "replay_action_add_env", EVENT_ROLE_RUNTIME_EFFECT | EVENT_ROLE_BUILD_SEMANTIC)

typedef enum {
#define DECLARE_EVENT_KIND(kind, family, label, roles) kind,
    EVENT_KIND_LIST(DECLARE_EVENT_KIND)
#undef DECLARE_EVENT_KIND
    // Canonical kinds are append-only; add new entries above this sentinel.
    EVENT_KIND_COUNT
} Event_Kind;

typedef enum {
    EVENT_DIAG_SEVERITY_NOTE = 0,
    EVENT_DIAG_SEVERITY_WARNING,
    EVENT_DIAG_SEVERITY_ERROR,
} Event_Diag_Severity;

typedef enum {
    EVENT_SCOPE_KIND_DIRECTORY = 0,
    EVENT_SCOPE_KIND_FUNCTION,
    EVENT_SCOPE_KIND_MACRO,
    EVENT_SCOPE_KIND_BLOCK,
    EVENT_SCOPE_KIND_LOOP,
    EVENT_SCOPE_KIND_CALL,
} Event_Scope_Kind;

typedef enum {
    EVENT_VAR_TARGET_CURRENT = 0,
    EVENT_VAR_TARGET_CACHE,
    EVENT_VAR_TARGET_ENV,
} Event_Var_Target_Kind;

typedef enum {
    EVENT_POLICY_VALUE_UNSPECIFIED = 0,
    EVENT_POLICY_VALUE_OLD,
    EVENT_POLICY_VALUE_NEW,
    EVENT_POLICY_VALUE_WARN,
    EVENT_POLICY_VALUE_REQUIRED,
} Event_Policy_Value;

typedef enum {
    EVENT_RETURN_CONTEXT_FUNCTION = 0,
    EVENT_RETURN_CONTEXT_BLOCK,
    EVENT_RETURN_CONTEXT_DIRECTORY,
    EVENT_RETURN_CONTEXT_TOP_LEVEL,
} Event_Return_Context;

typedef enum {
    EVENT_COMMAND_DISPATCH_BUILTIN = 0,
    EVENT_COMMAND_DISPATCH_FUNCTION,
    EVENT_COMMAND_DISPATCH_MACRO,
    EVENT_COMMAND_DISPATCH_UNKNOWN,
} Event_Command_Dispatch_Kind;

typedef enum {
    EVENT_COMMAND_STATUS_SUCCESS = 0,
    EVENT_COMMAND_STATUS_ERROR,
    EVENT_COMMAND_STATUS_UNSUPPORTED,
} Event_Command_Status;

typedef enum {
    EVENT_BUILD_STEP_OUTPUT_RULE = 0,
    EVENT_BUILD_STEP_CUSTOM_TARGET,
    EVENT_BUILD_STEP_TARGET_PRE_BUILD,
    EVENT_BUILD_STEP_TARGET_PRE_LINK,
    EVENT_BUILD_STEP_TARGET_POST_BUILD,
} Event_Build_Step_Kind;

typedef enum {
    EVENT_REPLAY_PHASE_CONFIGURE = 0,
    EVENT_REPLAY_PHASE_BUILD,
    EVENT_REPLAY_PHASE_TEST,
    EVENT_REPLAY_PHASE_INSTALL,
    EVENT_REPLAY_PHASE_EXPORT,
    EVENT_REPLAY_PHASE_PACKAGE,
    EVENT_REPLAY_PHASE_HOST_ONLY,
} Event_Replay_Phase;

typedef enum {
    EVENT_REPLAY_ACTION_FILESYSTEM = 0,
    EVENT_REPLAY_ACTION_PROCESS,
    EVENT_REPLAY_ACTION_PROBE,
    EVENT_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION,
    EVENT_REPLAY_ACTION_TEST_DRIVER,
    EVENT_REPLAY_ACTION_HOST_EFFECT,
} Event_Replay_Action_Kind;

typedef enum {
    EVENT_REPLAY_OPCODE_NONE = 0,
    EVENT_REPLAY_OPCODE_FS_MKDIR,
    EVENT_REPLAY_OPCODE_FS_WRITE_TEXT,
    EVENT_REPLAY_OPCODE_FS_APPEND_TEXT,
    EVENT_REPLAY_OPCODE_FS_COPY_FILE,
    EVENT_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL,
    EVENT_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR,
    EVENT_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR,
    EVENT_REPLAY_OPCODE_HOST_LOCK_ACQUIRE,
    EVENT_REPLAY_OPCODE_HOST_LOCK_RELEASE,
    EVENT_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE,
    EVENT_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT,
    EVENT_REPLAY_OPCODE_PROBE_TRY_RUN,
    EVENT_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR,
    EVENT_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE,
    EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY,
    EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL,
    EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF,
    EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF,
    EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST,
    EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP,
    EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL,
    EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL,
} Event_Replay_Opcode;

typedef enum {
    EVENT_EXPORT_SOURCE_INSTALL_EXPORT = 0,
    EVENT_EXPORT_SOURCE_TARGETS,
    EVENT_EXPORT_SOURCE_EXPORT_SET,
    EVENT_EXPORT_SOURCE_PACKAGE,
} Event_Export_Source_Kind;

typedef enum {
    EVENT_PROPERTY_MUTATE_SET = 0,
    EVENT_PROPERTY_MUTATE_APPEND_LIST,
    EVENT_PROPERTY_MUTATE_APPEND_STRING,
    EVENT_PROPERTY_MUTATE_PREPEND_LIST,
} Event_Property_Mutate_Op;

typedef enum {
    EVENT_PROPERTY_MODIFIER_NONE = 0,
    EVENT_PROPERTY_MODIFIER_BEFORE = 1u << 0,
    EVENT_PROPERTY_MODIFIER_SYSTEM = 1u << 1,
} Event_Property_Modifier_Flags;

// Transitional legacy enums still referenced by evaluator modules that have not
// yet been migrated off the frozen structural contract.
typedef enum {
    EV_PROP_SET = 0,
    EV_PROP_APPEND_LIST,
    EV_PROP_APPEND_STRING,
    EV_PROP_PREPEND_LIST,
} Cmake_Target_Property_Op;

typedef enum {
    EV_VISIBILITY_UNSPECIFIED = 0,
    EV_VISIBILITY_PRIVATE,
    EV_VISIBILITY_PUBLIC,
    EV_VISIBILITY_INTERFACE,
} Cmake_Visibility;

typedef enum {
    EV_TARGET_EXECUTABLE = 0,
    EV_TARGET_LIBRARY_STATIC,
    EV_TARGET_LIBRARY_SHARED,
    EV_TARGET_LIBRARY_MODULE,
    EV_TARGET_LIBRARY_INTERFACE,
    EV_TARGET_LIBRARY_OBJECT,
    EV_TARGET_LIBRARY_UNKNOWN,
} Cmake_Target_Type;

typedef enum {
    EV_INSTALL_RULE_TARGET = 0,
    EV_INSTALL_RULE_FILE,
    EV_INSTALL_RULE_PROGRAM,
    EV_INSTALL_RULE_DIRECTORY,
} Cmake_Install_Rule_Type;

typedef struct {
    String_View file_path;
    size_t line;
    size_t col;
} Event_Origin;

typedef struct {
    Event_Kind kind;
    Event_Family family;
    const char *label;
    uint32_t role_mask;
    uint16_t default_version;
} Event_Kind_Meta;

typedef struct {
    Event_Diag_Severity severity;
    String_View component;
    String_View command;
    String_View code;
    String_View error_class;
    String_View cause;
    String_View hint;
} Event_Diag;

typedef struct {
    String_View command_name;
    Event_Command_Dispatch_Kind dispatch_kind;
    uint32_t argc;
} Event_Command_Begin;

typedef struct {
    String_View command_name;
    Event_Command_Dispatch_Kind dispatch_kind;
    uint32_t argc;
    Event_Command_Status status;
} Event_Command_End;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
} Event_Directory_Enter;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
} Event_Directory_Leave;

typedef struct {
    String_View property_name;
    Event_Property_Mutate_Op op;
    uint32_t modifier_flags;
    String_View *items;
    size_t item_count;
} Event_Directory_Property_Mutate;

typedef Event_Directory_Property_Mutate Event_Global_Property_Mutate;

typedef struct {
    String_View key;
    String_View value;
    Event_Var_Target_Kind target_kind;
} Event_Var_Set;

typedef struct {
    String_View key;
    Event_Var_Target_Kind target_kind;
} Event_Var_Unset;

typedef struct {
    Event_Scope_Kind scope_kind;
    uint32_t depth_before;
    uint32_t depth_after;
} Event_Scope_Push;

typedef struct {
    Event_Scope_Kind scope_kind;
    uint32_t depth_before;
    uint32_t depth_after;
} Event_Scope_Pop;

typedef struct {
    uint32_t depth_before;
    uint32_t depth_after;
} Event_Policy_Push;

typedef struct {
    uint32_t depth_before;
    uint32_t depth_after;
} Event_Policy_Pop;

typedef struct {
    String_View policy_id;
    Event_Policy_Value value;
} Event_Policy_Set;

typedef struct {
    Event_Return_Context return_context;
    bool has_propagate;
    String_View *propagate_vars;
    size_t propagate_count;
} Event_Flow_Return;

typedef struct {
    bool result;
} Event_Flow_If_Eval;

typedef struct {
    String_View branch_kind;
} Event_Flow_Branch_Taken;

typedef struct {
    String_View loop_kind;
} Event_Flow_Loop_Begin;

typedef struct {
    String_View loop_kind;
    uint32_t iterations;
} Event_Flow_Loop_End;

typedef struct {
    uint32_t loop_depth;
} Event_Flow_Break;

typedef struct {
    uint32_t loop_depth;
} Event_Flow_Continue;

typedef struct {
    String_View defer_id;
    String_View command_name;
} Event_Flow_Defer_Queue;

typedef struct {
    uint32_t call_count;
} Event_Flow_Defer_Flush;

typedef struct {
    bool variable_scope_pushed;
    bool policy_scope_pushed;
    bool has_propagate_vars;
} Event_Flow_Block_Begin;

typedef struct {
    bool propagate_on_return;
    bool had_propagate_vars;
} Event_Flow_Block_End;

typedef struct {
    String_View name;
    uint32_t argc;
} Event_Flow_Function_Begin;

typedef struct {
    String_View name;
    bool returned;
} Event_Flow_Function_End;

typedef struct {
    String_View name;
    uint32_t argc;
} Event_Flow_Macro_Begin;

typedef struct {
    String_View name;
    bool returned;
} Event_Flow_Macro_End;

typedef struct {
    String_View path;
} Event_Fs_Write_File;

typedef struct {
    String_View path;
} Event_Fs_Append_File;

typedef struct {
    String_View path;
    String_View out_var;
} Event_Fs_Read_File;

typedef struct {
    String_View out_var;
    String_View base_dir;
    bool recursive;
} Event_Fs_Glob;

typedef struct {
    String_View path;
} Event_Fs_Mkdir;

typedef struct {
    String_View path;
    bool recursive;
} Event_Fs_Remove;

typedef struct {
    String_View source;
    String_View destination;
} Event_Fs_Copy;

typedef struct {
    String_View source;
    String_View destination;
} Event_Fs_Rename;

typedef struct {
    String_View source;
    String_View destination;
    bool symbolic;
} Event_Fs_Create_Link;

typedef struct {
    String_View path;
    bool recursive;
} Event_Fs_Chmod;

typedef struct {
    String_View path;
} Event_Fs_Archive_Create;

typedef struct {
    String_View path;
    String_View destination;
} Event_Fs_Archive_Extract;

typedef struct {
    String_View source;
    String_View destination;
} Event_Fs_Transfer_Download;

typedef struct {
    String_View source;
    String_View destination;
} Event_Fs_Transfer_Upload;

typedef struct {
    String_View command;
    String_View working_directory;
} Event_Proc_Exec_Request;

typedef struct {
    String_View command;
    String_View result_code;
    String_View stdout_text;
    String_View stderr_text;
    bool had_error;
} Event_Proc_Exec_Result;

typedef struct {
    String_View out_var;
} Event_String_Replace;

typedef struct {
    String_View out_var;
} Event_String_Configure;

typedef struct {
    String_View mode;
    String_View out_var;
} Event_String_Regex;

typedef struct {
    String_View algorithm;
    String_View out_var;
} Event_String_Hash;

typedef struct {
    String_View out_var;
} Event_String_Timestamp;

typedef struct {
    String_View list_var;
} Event_List_Append;

typedef struct {
    String_View list_var;
} Event_List_Prepend;

typedef struct {
    String_View list_var;
} Event_List_Insert;

typedef struct {
    String_View list_var;
} Event_List_Remove;

typedef struct {
    String_View list_var;
} Event_List_Transform;

typedef struct {
    String_View list_var;
} Event_List_Sort;

typedef struct {
    String_View out_var;
    String_View format;
} Event_Math_Expr;

typedef struct {
    String_View out_var;
} Event_Path_Normalize;

typedef struct {
    String_View out_var;
} Event_Path_Compare;

typedef struct {
    String_View out_var;
} Event_Path_Convert;

typedef struct {
    bool enabled;
} Event_Test_Enable;

typedef struct {
    String_View name;
    String_View command;
    String_View working_dir;
    bool command_expand_lists;
    String_View *configurations;
    size_t configuration_count;
} Event_Test_Add;

typedef struct {
    Cmake_Install_Rule_Type rule_type;
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
} Event_Install_Rule_Add;

typedef struct {
    String_View export_name;
    String_View destination;
    String_View export_namespace;
    String_View file_name;
    String_View component;
} Event_Export_Install;

typedef struct {
    String_View export_key;
    Event_Export_Source_Kind source_kind;
    String_View logical_name;
    String_View file_path;
    String_View export_namespace;
    bool append;
    String_View cxx_modules_directory;
} Event_Export_Build_Declare;

typedef struct {
    String_View export_key;
    String_View target_name;
} Event_Export_Build_Add_Target;

typedef struct {
    String_View package_name;
    String_View prefix;
    bool enabled;
} Event_Export_Package_Registry;

typedef struct {
    String_View name;
    String_View display_name;
} Event_Cpack_Add_Install_Type;

typedef struct {
    String_View name;
    String_View display_name;
    String_View description;
    String_View parent_group;
    bool expanded;
    bool bold_title;
} Event_Cpack_Add_Component_Group;

typedef struct {
    String_View name;
    String_View display_name;
    String_View description;
    String_View group;
    String_View depends;
    String_View install_types;
    String_View archive_file;
    String_View plist;
    bool required;
    bool hidden;
    bool disabled;
    bool downloaded;
} Event_Cpack_Add_Component;

typedef struct {
    String_View package_key;
    String_View package_name;
    String_View package_version;
    String_View package_file_name;
    String_View package_directory;
    bool include_toplevel_directory;
    bool archive_component_install;
    String_View components_all;
} Event_Cpack_Package_Declare;

typedef struct {
    String_View package_key;
    String_View generator;
} Event_Cpack_Package_Add_Generator;

typedef struct {
    String_View package_name;
    String_View mode;
    union {
        String_View found_path;
        String_View location; // legacy alias
    };
    bool found;
    bool required;
    bool quiet;
} Event_Package_Find_Result;

typedef struct {
    String_View name;
    String_View version;
    String_View description;
    String_View homepage_url;
    String_View languages;
} Event_Project_Declare;

typedef struct {
    String_View version;
    bool fatal_if_too_old;
} Event_Project_Minimum_Required;

typedef struct {
    String_View name;
    union {
        Cmake_Target_Type target_type;
        Cmake_Target_Type type; // legacy alias
    };
    bool imported;
    bool alias;
    String_View alias_of;
} Event_Target_Declare;

typedef struct {
    String_View target_name;
    String_View path;
} Event_Target_Add_Source;

typedef struct {
    String_View path;
    String_View directory_source_dir;
    String_View directory_binary_dir;
    bool generated;
} Event_Source_Mark_Generated;

typedef struct {
    String_View target_name;
    String_View dependency_name;
} Event_Target_Add_Dependency;

typedef struct {
    String_View step_key;
    Event_Build_Step_Kind step_kind;
    String_View owner_target_name;
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
} Event_Build_Step_Declare;

typedef struct {
    String_View step_key;
    String_View path;
} Event_Build_Step_Add_Output;

typedef struct {
    String_View step_key;
    String_View path;
} Event_Build_Step_Add_Byproduct;

typedef struct {
    String_View step_key;
    String_View item;
} Event_Build_Step_Add_Dependency;

typedef struct {
    String_View step_key;
    uint32_t command_index;
    String_View *argv;
    size_t argc;
} Event_Build_Step_Add_Command;

typedef struct {
    String_View action_key;
    Event_Replay_Action_Kind action_kind;
    Event_Replay_Opcode opcode;
    Event_Replay_Phase phase;
    String_View working_directory;
} Event_Replay_Action_Declare;

typedef struct {
    String_View action_key;
    String_View path;
} Event_Replay_Action_Add_Input;

typedef struct {
    String_View action_key;
    String_View path;
} Event_Replay_Action_Add_Output;

typedef struct {
    String_View action_key;
    uint32_t arg_index;
    String_View value;
} Event_Replay_Action_Add_Argv;

typedef struct {
    String_View action_key;
    String_View key;
    String_View value;
} Event_Replay_Action_Add_Env;

typedef struct {
    String_View target_name;
    String_View key;
    String_View value;
    Cmake_Target_Property_Op op;
} Event_Target_Prop_Set;

typedef struct {
    String_View target_name;
    Cmake_Visibility visibility;
    String_View item;
} Event_Target_Link_Libraries;

typedef struct {
    String_View target_name;
    Cmake_Visibility visibility;
    String_View item;
    bool is_before;
} Event_Target_Link_Options;

typedef struct {
    String_View target_name;
    Cmake_Visibility visibility;
    String_View path;
} Event_Target_Link_Directories;

typedef struct {
    String_View target_name;
    Cmake_Visibility visibility;
    String_View path;
    bool is_system;
    bool is_before;
} Event_Target_Include_Directories;

typedef struct {
    String_View target_name;
    Cmake_Visibility visibility;
    String_View item;
} Event_Target_Compile_Definitions;

typedef struct {
    String_View target_name;
    Cmake_Visibility visibility;
    String_View item;
    bool is_before;
} Event_Target_Compile_Options;

typedef struct {
    String_View path;
    bool no_policy_scope;
} Event_Include_Begin;

typedef struct {
    String_View path;
    bool success;
} Event_Include_End;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    bool exclude_from_all;
    bool system;
} Event_Add_Subdirectory_Begin;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    bool success;
} Event_Add_Subdirectory_End;

typedef struct {
    String_View command_name;
} Event_Cmake_Language_Call;

typedef struct {
    String_View code;
} Event_Cmake_Language_Eval;

typedef struct {
    String_View defer_id;
    String_View command_name;
} Event_Cmake_Language_Defer_Queue;

typedef struct {
    Event_Kind kind;
    uint16_t version;
    uint32_t flags;
    uint64_t seq;
    uint32_t scope_depth;
    uint32_t policy_depth;
    Event_Origin origin;
} Event_Header;

typedef struct {
    Event_Header h;
    union {
        Event_Diag diag;
        Event_Command_Begin command_begin;
        Event_Command_Begin command_call; // legacy alias
        Event_Command_End command_end;
        Event_Include_Begin include_begin;
        Event_Include_End include_end;
        Event_Add_Subdirectory_Begin add_subdirectory_begin;
        Event_Add_Subdirectory_End add_subdirectory_end;
        Event_Cmake_Language_Call cmake_language_call;
        Event_Cmake_Language_Eval cmake_language_eval;
        Event_Cmake_Language_Defer_Queue cmake_language_defer_queue;
        Event_Directory_Enter directory_enter;
        Event_Directory_Enter dir_push; // legacy alias
        Event_Directory_Leave directory_leave;
        Event_Directory_Leave dir_pop; // legacy alias
        Event_Directory_Property_Mutate directory_property_mutate;
        Event_Global_Property_Mutate global_property_mutate;
        Event_Var_Set var_set;
        Event_Var_Unset var_unset;
        Event_Scope_Push scope_push;
        Event_Scope_Pop scope_pop;
        Event_Policy_Push policy_push;
        Event_Policy_Pop policy_pop;
        Event_Policy_Set policy_set;
        Event_Flow_Return flow_return;
        Event_Flow_If_Eval flow_if_eval;
        Event_Flow_Branch_Taken flow_branch_taken;
        Event_Flow_Loop_Begin flow_loop_begin;
        Event_Flow_Loop_End flow_loop_end;
        Event_Flow_Break flow_break;
        Event_Flow_Continue flow_continue;
        Event_Flow_Defer_Queue flow_defer_queue;
        Event_Flow_Defer_Flush flow_defer_flush;
        Event_Flow_Block_Begin flow_block_begin;
        Event_Flow_Block_End flow_block_end;
        Event_Flow_Function_Begin flow_function_begin;
        Event_Flow_Function_End flow_function_end;
        Event_Flow_Macro_Begin flow_macro_begin;
        Event_Flow_Macro_End flow_macro_end;
        Event_Fs_Write_File fs_write_file;
        Event_Fs_Append_File fs_append_file;
        Event_Fs_Read_File fs_read_file;
        Event_Fs_Glob fs_glob;
        Event_Fs_Mkdir fs_mkdir;
        Event_Fs_Remove fs_remove;
        Event_Fs_Copy fs_copy;
        Event_Fs_Rename fs_rename;
        Event_Fs_Create_Link fs_create_link;
        Event_Fs_Chmod fs_chmod;
        Event_Fs_Archive_Create fs_archive_create;
        Event_Fs_Archive_Extract fs_archive_extract;
        Event_Fs_Transfer_Download fs_transfer_download;
        Event_Fs_Transfer_Upload fs_transfer_upload;
        Event_Proc_Exec_Request proc_exec_request;
        Event_Proc_Exec_Result proc_exec_result;
        Event_String_Replace string_replace;
        Event_String_Configure string_configure;
        Event_String_Regex string_regex;
        Event_String_Hash string_hash;
        Event_String_Timestamp string_timestamp;
        Event_List_Append list_append;
        Event_List_Prepend list_prepend;
        Event_List_Insert list_insert;
        Event_List_Remove list_remove;
        Event_List_Transform list_transform;
        Event_List_Sort list_sort;
        Event_Math_Expr math_expr;
        Event_Path_Normalize path_normalize;
        Event_Path_Compare path_compare;
        Event_Path_Convert path_convert;
        Event_Test_Enable test_enable;
        Event_Test_Enable testing_enable; // legacy alias
        Event_Test_Add test_add;
        Event_Install_Rule_Add install_rule_add;
        Event_Install_Rule_Add install_add_rule; // legacy alias
        Event_Cpack_Add_Install_Type cpack_add_install_type;
        Event_Cpack_Add_Component_Group cpack_add_component_group;
        Event_Cpack_Add_Component cpack_add_component;
        Event_Cpack_Package_Declare cpack_package_declare;
        Event_Cpack_Package_Add_Generator cpack_package_add_generator;
        Event_Package_Find_Result package_find_result;
        Event_Package_Find_Result find_package; // legacy alias
        Event_Project_Declare project_declare;
        Event_Project_Minimum_Required project_minimum_required;
        Event_Target_Declare target_declare;
        Event_Target_Add_Source target_add_source;
        Event_Source_Mark_Generated source_mark_generated;
        Event_Target_Add_Dependency target_add_dependency;
        Event_Build_Step_Declare build_step_declare;
        Event_Build_Step_Add_Output build_step_add_output;
        Event_Build_Step_Add_Byproduct build_step_add_byproduct;
        Event_Build_Step_Add_Dependency build_step_add_dependency;
        Event_Build_Step_Add_Command build_step_add_command;
        Event_Replay_Action_Declare replay_action_declare;
        Event_Replay_Action_Add_Input replay_action_add_input;
        Event_Replay_Action_Add_Output replay_action_add_output;
        Event_Replay_Action_Add_Argv replay_action_add_argv;
        Event_Replay_Action_Add_Env replay_action_add_env;
        Event_Target_Prop_Set target_prop_set;
        Event_Target_Link_Libraries target_link_libraries;
        Event_Target_Link_Options target_link_options;
        Event_Target_Link_Directories target_link_directories;
        Event_Target_Include_Directories target_include_directories;
        Event_Target_Compile_Definitions target_compile_definitions;
        Event_Target_Compile_Options target_compile_options;
        Event_Export_Install export_install;
        Event_Export_Build_Declare export_build_declare;
        Event_Export_Build_Add_Target export_build_add_target;
        Event_Export_Package_Registry export_package_registry;
    } as;
} Event;

typedef struct {
    Arena *arena;
    Event *items;
    size_t count; // compatibility mirror for arena_arr_len(items)
    uint64_t next_seq;
} Event_Stream;

typedef struct {
    const Event_Stream *stream;
    size_t index;
    const Event *current;
} Event_Stream_Iterator;

typedef Event_Directory_Enter Event_Dir_Push;
typedef Event_Directory_Leave Event_Dir_Pop;
typedef Event_Command_Begin Event_Command_Call;

// Transitional compatibility aliases while evaluator modules migrate from the
// frozen structural IR to the semantic contract.
typedef Event Cmake_Event;
typedef Event_Kind Cmake_Event_Kind;
typedef Event_Origin Cmake_Event_Origin;
typedef Event_Stream Cmake_Event_Stream;
typedef Event_Stream_Iterator Cmake_Event_Stream_Iterator;
typedef Event_Diag_Severity Cmake_Diag_Severity;

#define EV_DIAG_NOTE EVENT_DIAG_SEVERITY_NOTE
#define EV_DIAG_WARNING EVENT_DIAG_SEVERITY_WARNING
#define EV_DIAG_ERROR EVENT_DIAG_SEVERITY_ERROR

// Legacy event-kind aliases used by older tests/helpers.
#define EV_DIAGNOSTIC EVENT_DIAG
#define EV_PROJECT_DECLARE EVENT_PROJECT_DECLARE
#define EV_VAR_SET EVENT_VAR_SET
#define EV_SET_CACHE_ENTRY EVENT_VAR_SET
#define EV_TARGET_DECLARE EVENT_TARGET_DECLARE
#define EV_TARGET_ADD_SOURCE EVENT_TARGET_ADD_SOURCE
#define EV_TARGET_ADD_DEPENDENCY EVENT_TARGET_ADD_DEPENDENCY
#define EV_TARGET_PROP_SET EVENT_TARGET_PROP_SET
#define EV_TARGET_INCLUDE_DIRECTORIES EVENT_TARGET_INCLUDE_DIRECTORIES
#define EV_TARGET_COMPILE_DEFINITIONS EVENT_TARGET_COMPILE_DEFINITIONS
#define EV_TARGET_COMPILE_OPTIONS EVENT_TARGET_COMPILE_OPTIONS
#define EV_TARGET_LINK_LIBRARIES EVENT_TARGET_LINK_LIBRARIES
#define EV_TARGET_LINK_OPTIONS EVENT_TARGET_LINK_OPTIONS
#define EV_TARGET_LINK_DIRECTORIES EVENT_TARGET_LINK_DIRECTORIES
#define EV_DIR_PUSH EVENT_DIRECTORY_ENTER
#define EV_DIR_POP EVENT_DIRECTORY_LEAVE
#define EV_TESTING_ENABLE EVENT_TEST_ENABLE
#define EV_TEST_ADD EVENT_TEST_ADD
#define EV_INSTALL_ADD_RULE EVENT_INSTALL_RULE_ADD
#define EV_CPACK_ADD_INSTALL_TYPE EVENT_CPACK_ADD_INSTALL_TYPE
#define EV_CPACK_ADD_COMPONENT_GROUP EVENT_CPACK_ADD_COMPONENT_GROUP
#define EV_CPACK_ADD_COMPONENT EVENT_CPACK_ADD_COMPONENT
#define EV_CPACK_PACKAGE_DECLARE EVENT_CPACK_PACKAGE_DECLARE
#define EV_CPACK_PACKAGE_ADD_GENERATOR EVENT_CPACK_PACKAGE_ADD_GENERATOR
#define EV_FIND_PACKAGE EVENT_PACKAGE_FIND_RESULT
#define EV_COMMAND_CALL EVENT_COMMAND_BEGIN

// Source compatibility aliases for renamed canonical kinds.
#define EVENT_DIR_PUSH EVENT_DIRECTORY_ENTER
#define EVENT_DIR_POP EVENT_DIRECTORY_LEAVE
#define EVENT_COMMAND_CALL EVENT_COMMAND_BEGIN

// Removed kinds kept as non-emitted sentinels for source compatibility.
#define EV_CUSTOM_COMMAND_TARGET ((Event_Kind)0x7FF0)
#define EV_CUSTOM_COMMAND_OUTPUT ((Event_Kind)0x7FF1)
#define EV_DIRECTORY_INCLUDE_DIRECTORIES ((Event_Kind)0x7FF2)
#define EV_DIRECTORY_LINK_DIRECTORIES ((Event_Kind)0x7FF3)
#define EV_GLOBAL_COMPILE_DEFINITIONS ((Event_Kind)0x7FF4)
#define EV_GLOBAL_COMPILE_OPTIONS ((Event_Kind)0x7FF5)
#define EV_GLOBAL_LINK_OPTIONS ((Event_Kind)0x7FF6)
#define EV_GLOBAL_LINK_LIBRARIES ((Event_Kind)0x7FF7)
#define EV_UNKNOWN ((Event_Kind)0x7FFF)

Event_Stream *event_stream_create(Arena *arena);
// Append one canonical event into the stream. This is the only supported
// payload ownership boundary: strings and string arrays are deep-copied into
// the stream arena on success.
bool event_stream_push(Event_Stream *stream, const Event *ev);
bool event_copy_into_arena(Arena *arena, Event *ev);
Event_Stream_Iterator event_stream_iter(const Event_Stream *stream);
bool event_stream_next(Event_Stream_Iterator *it);

const Event_Kind_Meta *event_kind_meta(Event_Kind kind);
bool event_kind_has_role(Event_Kind kind, Event_Role role);
uint32_t event_kind_role_mask(Event_Kind kind);
Event_Family event_kind_family(Event_Kind kind);
const char *event_family_name(Event_Family family);
const char *event_kind_name(Event_Kind kind);
void event_stream_dump(const Event_Stream *stream);

#endif // EVENT_IR_H_
