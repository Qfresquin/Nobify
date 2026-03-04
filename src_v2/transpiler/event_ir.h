// Canonical semantic Event IR.
#ifndef EVENT_IR_H_
#define EVENT_IR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nob.h"
#include "arena.h"

#define EVENT_FAMILY_LIST(X) \
    X(EVENT_FAMILY_DIAG, "diag") \
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
    X(EVENT_FAMILY_META, "meta")

typedef enum {
#define DECLARE_EVENT_FAMILY(kind, label) kind,
    EVENT_FAMILY_LIST(DECLARE_EVENT_FAMILY)
#undef DECLARE_EVENT_FAMILY
} Event_Family;

#define EVENT_KIND_LIST(X) \
    X(EVENT_DIAG, EVENT_FAMILY_DIAG, "diag") \
    X(EVENT_VAR_SET, EVENT_FAMILY_VAR, "var_set") \
    X(EVENT_VAR_UNSET, EVENT_FAMILY_VAR, "var_unset") \
    X(EVENT_SCOPE_PUSH, EVENT_FAMILY_SCOPE, "scope_push") \
    X(EVENT_SCOPE_POP, EVENT_FAMILY_SCOPE, "scope_pop") \
    X(EVENT_POLICY_PUSH, EVENT_FAMILY_POLICY, "policy_push") \
    X(EVENT_POLICY_POP, EVENT_FAMILY_POLICY, "policy_pop") \
    X(EVENT_POLICY_SET, EVENT_FAMILY_POLICY, "policy_set") \
    X(EVENT_FLOW_RETURN, EVENT_FAMILY_FLOW, "flow_return") \
    X(EVENT_FLOW_IF_EVAL, EVENT_FAMILY_FLOW, "flow_if_eval") \
    X(EVENT_FLOW_BRANCH_TAKEN, EVENT_FAMILY_FLOW, "flow_branch_taken") \
    X(EVENT_FLOW_LOOP_BEGIN, EVENT_FAMILY_FLOW, "flow_loop_begin") \
    X(EVENT_FLOW_LOOP_END, EVENT_FAMILY_FLOW, "flow_loop_end") \
    X(EVENT_FLOW_BREAK, EVENT_FAMILY_FLOW, "flow_break") \
    X(EVENT_FLOW_CONTINUE, EVENT_FAMILY_FLOW, "flow_continue") \
    X(EVENT_FLOW_DEFER_QUEUE, EVENT_FAMILY_FLOW, "flow_defer_queue") \
    X(EVENT_FLOW_DEFER_FLUSH, EVENT_FAMILY_FLOW, "flow_defer_flush") \
    X(EVENT_FS_WRITE_FILE, EVENT_FAMILY_FS, "fs_write_file") \
    X(EVENT_FS_APPEND_FILE, EVENT_FAMILY_FS, "fs_append_file") \
    X(EVENT_FS_READ_FILE, EVENT_FAMILY_FS, "fs_read_file") \
    X(EVENT_FS_GLOB, EVENT_FAMILY_FS, "fs_glob") \
    X(EVENT_FS_MKDIR, EVENT_FAMILY_FS, "fs_mkdir") \
    X(EVENT_FS_REMOVE, EVENT_FAMILY_FS, "fs_remove") \
    X(EVENT_FS_COPY, EVENT_FAMILY_FS, "fs_copy") \
    X(EVENT_FS_RENAME, EVENT_FAMILY_FS, "fs_rename") \
    X(EVENT_FS_CREATE_LINK, EVENT_FAMILY_FS, "fs_create_link") \
    X(EVENT_FS_CHMOD, EVENT_FAMILY_FS, "fs_chmod") \
    X(EVENT_FS_ARCHIVE_CREATE, EVENT_FAMILY_FS, "fs_archive_create") \
    X(EVENT_FS_ARCHIVE_EXTRACT, EVENT_FAMILY_FS, "fs_archive_extract") \
    X(EVENT_FS_TRANSFER_DOWNLOAD, EVENT_FAMILY_FS, "fs_transfer_download") \
    X(EVENT_FS_TRANSFER_UPLOAD, EVENT_FAMILY_FS, "fs_transfer_upload") \
    X(EVENT_PROC_EXEC_REQUEST, EVENT_FAMILY_PROC, "proc_exec_request") \
    X(EVENT_PROC_EXEC_RESULT, EVENT_FAMILY_PROC, "proc_exec_result") \
    X(EVENT_STRING_REPLACE, EVENT_FAMILY_STRING, "string_replace") \
    X(EVENT_STRING_CONFIGURE, EVENT_FAMILY_STRING, "string_configure") \
    X(EVENT_STRING_REGEX, EVENT_FAMILY_STRING, "string_regex") \
    X(EVENT_STRING_HASH, EVENT_FAMILY_STRING, "string_hash") \
    X(EVENT_STRING_TIMESTAMP, EVENT_FAMILY_STRING, "string_timestamp") \
    X(EVENT_LIST_APPEND, EVENT_FAMILY_LIST, "list_append") \
    X(EVENT_LIST_PREPEND, EVENT_FAMILY_LIST, "list_prepend") \
    X(EVENT_LIST_INSERT, EVENT_FAMILY_LIST, "list_insert") \
    X(EVENT_LIST_REMOVE, EVENT_FAMILY_LIST, "list_remove") \
    X(EVENT_LIST_TRANSFORM, EVENT_FAMILY_LIST, "list_transform") \
    X(EVENT_LIST_SORT, EVENT_FAMILY_LIST, "list_sort") \
    X(EVENT_MATH_EXPR, EVENT_FAMILY_MATH, "math_expr") \
    X(EVENT_PATH_NORMALIZE, EVENT_FAMILY_PATH, "path_normalize") \
    X(EVENT_PATH_COMPARE, EVENT_FAMILY_PATH, "path_compare") \
    X(EVENT_PATH_CONVERT, EVENT_FAMILY_PATH, "path_convert") \
    X(EVENT_TEST_ENABLE, EVENT_FAMILY_TEST, "test_enable") \
    X(EVENT_TEST_ADD, EVENT_FAMILY_TEST, "test_add") \
    X(EVENT_INSTALL_RULE_ADD, EVENT_FAMILY_INSTALL, "install_rule_add") \
    X(EVENT_CPACK_ADD_INSTALL_TYPE, EVENT_FAMILY_CPACK, "cpack_add_install_type") \
    X(EVENT_CPACK_ADD_COMPONENT_GROUP, EVENT_FAMILY_CPACK, "cpack_add_component_group") \
    X(EVENT_CPACK_ADD_COMPONENT, EVENT_FAMILY_CPACK, "cpack_add_component") \
    X(EVENT_PACKAGE_FIND_RESULT, EVENT_FAMILY_PACKAGE, "package_find_result") \
    X(EVENT_PROJECT_DECLARE, EVENT_FAMILY_PROJECT, "project_declare") \
    X(EVENT_PROJECT_MINIMUM_REQUIRED, EVENT_FAMILY_PROJECT, "project_minimum_required") \
    X(EVENT_TARGET_DECLARE, EVENT_FAMILY_TARGET, "target_declare") \
    X(EVENT_TARGET_ADD_SOURCE, EVENT_FAMILY_TARGET, "target_add_source") \
    X(EVENT_TARGET_ADD_DEPENDENCY, EVENT_FAMILY_TARGET, "target_add_dependency") \
    X(EVENT_TARGET_PROP_SET, EVENT_FAMILY_TARGET, "target_prop_set") \
    X(EVENT_TARGET_LINK_LIBRARIES, EVENT_FAMILY_TARGET, "target_link_libraries") \
    X(EVENT_TARGET_LINK_OPTIONS, EVENT_FAMILY_TARGET, "target_link_options") \
    X(EVENT_TARGET_LINK_DIRECTORIES, EVENT_FAMILY_TARGET, "target_link_directories") \
    X(EVENT_TARGET_INCLUDE_DIRECTORIES, EVENT_FAMILY_TARGET, "target_include_directories") \
    X(EVENT_TARGET_COMPILE_DEFINITIONS, EVENT_FAMILY_TARGET, "target_compile_definitions") \
    X(EVENT_TARGET_COMPILE_OPTIONS, EVENT_FAMILY_TARGET, "target_compile_options") \
    X(EVENT_INCLUDE_BEGIN, EVENT_FAMILY_META, "include_begin") \
    X(EVENT_INCLUDE_END, EVENT_FAMILY_META, "include_end") \
    X(EVENT_ADD_SUBDIRECTORY_BEGIN, EVENT_FAMILY_META, "add_subdirectory_begin") \
    X(EVENT_ADD_SUBDIRECTORY_END, EVENT_FAMILY_META, "add_subdirectory_end") \
    X(EVENT_DIR_PUSH, EVENT_FAMILY_META, "dir_push") \
    X(EVENT_DIR_POP, EVENT_FAMILY_META, "dir_pop") \
    X(EVENT_CMAKE_LANGUAGE_CALL, EVENT_FAMILY_META, "cmake_language_call") \
    X(EVENT_CMAKE_LANGUAGE_EVAL, EVENT_FAMILY_META, "cmake_language_eval") \
    X(EVENT_CMAKE_LANGUAGE_DEFER_QUEUE, EVENT_FAMILY_META, "cmake_language_defer_queue")

typedef enum {
#define DECLARE_EVENT_KIND(kind, family, label) kind,
    EVENT_KIND_LIST(DECLARE_EVENT_KIND)
#undef DECLARE_EVENT_KIND
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

// Transitional legacy enums still referenced by evaluator modules that have not
// yet been migrated off the frozen structural contract.
typedef enum {
    EV_PROP_SET = 0,
    EV_PROP_APPEND_LIST,
    EV_PROP_APPEND_STRING,
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
    Event_Diag_Severity severity;
    String_View component;
    String_View command;
    String_View code;
    String_View error_class;
    String_View cause;
    String_View hint;
} Event_Diag;

typedef struct {
    String_View key;
    String_View value;
    Event_Scope_Kind scope_kind;
    bool is_cache;
    bool is_env;
} Event_Var_Set;

typedef struct {
    String_View key;
    Event_Scope_Kind scope_kind;
    bool is_cache;
    bool is_env;
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
} Event_Test_Add;

typedef struct {
    Cmake_Install_Rule_Type rule_type;
    String_View item;
    String_View destination;
} Event_Install_Rule_Add;

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
    String_View package_name;
    String_View mode;
    String_View found_path;
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
    Cmake_Target_Type target_type;
    bool imported;
    bool alias;
    String_View alias_of;
} Event_Target_Declare;

typedef struct {
    String_View target_name;
    String_View path;
} Event_Target_Add_Source;

typedef struct {
    String_View target_name;
    String_View dependency_name;
} Event_Target_Add_Dependency;

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
    String_View source_dir;
    String_View binary_dir;
} Event_Dir_Push;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
} Event_Dir_Pop;

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
    Event_Family family;
    uint16_t kind;
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
        Event_Test_Add test_add;
        Event_Install_Rule_Add install_rule_add;
        Event_Cpack_Add_Install_Type cpack_add_install_type;
        Event_Cpack_Add_Component_Group cpack_add_component_group;
        Event_Cpack_Add_Component cpack_add_component;
        Event_Package_Find_Result package_find_result;
        Event_Project_Declare project_declare;
        Event_Project_Minimum_Required project_minimum_required;
        Event_Target_Declare target_declare;
        Event_Target_Add_Source target_add_source;
        Event_Target_Add_Dependency target_add_dependency;
        Event_Target_Prop_Set target_prop_set;
        Event_Target_Link_Libraries target_link_libraries;
        Event_Target_Link_Options target_link_options;
        Event_Target_Link_Directories target_link_directories;
        Event_Target_Include_Directories target_include_directories;
        Event_Target_Compile_Definitions target_compile_definitions;
        Event_Target_Compile_Options target_compile_options;
        Event_Include_Begin include_begin;
        Event_Include_End include_end;
        Event_Add_Subdirectory_Begin add_subdirectory_begin;
        Event_Add_Subdirectory_End add_subdirectory_end;
        Event_Dir_Push dir_push;
        Event_Dir_Pop dir_pop;
        Event_Cmake_Language_Call cmake_language_call;
        Event_Cmake_Language_Eval cmake_language_eval;
        Event_Cmake_Language_Defer_Queue cmake_language_defer_queue;
    } as;
} Event;

typedef struct {
    Event *items;
} Event_Stream;

typedef struct {
    const Event_Stream *stream;
    size_t index;
    const Event *current;
} Event_Stream_Iterator;

// Transitional compatibility aliases while evaluator modules migrate from the
// frozen structural IR to the semantic contract.
typedef Event_Origin Cmake_Event_Origin;
typedef Event_Stream Cmake_Event_Stream;
typedef Event_Stream_Iterator Cmake_Event_Stream_Iterator;
typedef Event_Diag_Severity Cmake_Diag_Severity;

#define EV_DIAG_NOTE EVENT_DIAG_SEVERITY_NOTE
#define EV_DIAG_WARNING EVENT_DIAG_SEVERITY_WARNING
#define EV_DIAG_ERROR EVENT_DIAG_SEVERITY_ERROR

Event_Stream *event_stream_create(Arena *arena);
bool event_stream_push(Arena *event_arena, Event_Stream *stream, Event ev);
Event_Stream_Iterator event_stream_iter(const Event_Stream *stream);
bool event_stream_next(Event_Stream_Iterator *it);

Event_Family event_kind_family(Event_Kind kind);
const char *event_family_name(Event_Family family);
const char *event_kind_name(Event_Kind kind);
void event_stream_dump(const Event_Stream *stream);

#endif // EVENT_IR_H_
