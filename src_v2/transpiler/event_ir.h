// Canonical semantic Event IR.
#ifndef EVENT_IR_H_
#define EVENT_IR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nob.h"
#include "arena.h"

#define EVENT_FAMILY_LIST(X) \
    X(EVENT_FAMILY_TRACE, "trace") \
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
    X(EVENT_TRACE_COMMAND_BEGIN, EVENT_FAMILY_TRACE, "trace_command_begin") \
    X(EVENT_TRACE_COMMAND_END, EVENT_FAMILY_TRACE, "trace_command_end") \
    X(EVENT_DIAG, EVENT_FAMILY_DIAG, "diag") \
    X(EVENT_VAR_SET, EVENT_FAMILY_VAR, "var_set") \
    X(EVENT_VAR_UNSET, EVENT_FAMILY_VAR, "var_unset") \
    X(EVENT_SCOPE_PUSH, EVENT_FAMILY_SCOPE, "scope_push") \
    X(EVENT_SCOPE_POP, EVENT_FAMILY_SCOPE, "scope_pop") \
    X(EVENT_POLICY_PUSH, EVENT_FAMILY_POLICY, "policy_push") \
    X(EVENT_POLICY_POP, EVENT_FAMILY_POLICY, "policy_pop") \
    X(EVENT_POLICY_SET, EVENT_FAMILY_POLICY, "policy_set") \
    X(EVENT_FLOW_RETURN, EVENT_FAMILY_FLOW, "flow_return") \
    X(EVENT_TEST_ENABLE, EVENT_FAMILY_TEST, "test_enable") \
    X(EVENT_TEST_ADD, EVENT_FAMILY_TEST, "test_add") \
    X(EVENT_INSTALL_RULE_ADD, EVENT_FAMILY_INSTALL, "install_rule_add") \
    X(EVENT_CPACK_ADD_INSTALL_TYPE, EVENT_FAMILY_CPACK, "cpack_add_install_type") \
    X(EVENT_CPACK_ADD_COMPONENT_GROUP, EVENT_FAMILY_CPACK, "cpack_add_component_group") \
    X(EVENT_CPACK_ADD_COMPONENT, EVENT_FAMILY_CPACK, "cpack_add_component")

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
    String_View command_name;
    String_View *resolved_args;
    size_t resolved_arg_count;
} Event_Trace_Command_Begin;

typedef struct {
    String_View command_name;
    bool completed;
    bool failed;
} Event_Trace_Command_End;

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
        Event_Trace_Command_Begin trace_command_begin;
        Event_Trace_Command_End trace_command_end;
        Event_Diag diag;
        Event_Var_Set var_set;
        Event_Var_Unset var_unset;
        Event_Scope_Push scope_push;
        Event_Scope_Pop scope_pop;
        Event_Policy_Push policy_push;
        Event_Policy_Pop policy_pop;
        Event_Policy_Set policy_set;
        Event_Flow_Return flow_return;
        Event_Test_Enable test_enable;
        Event_Test_Add test_add;
        Event_Install_Rule_Add install_rule_add;
        Event_Cpack_Add_Install_Type cpack_add_install_type;
        Event_Cpack_Add_Component_Group cpack_add_component_group;
        Event_Cpack_Add_Component cpack_add_component;
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
