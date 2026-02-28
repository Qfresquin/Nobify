// Event IR v2
// Boundary contract between evaluator and downstream builder/codegen stages.
#ifndef EVENT_IR_H_
#define EVENT_IR_H_

#include <stddef.h>
#include <stdbool.h>

#include "nob.h"
#include "arena.h"

typedef enum {
    EV_DIAGNOSTIC = 0,

    // Project
    EV_PROJECT_DECLARE,

    // Variables / cache
    EV_VAR_SET, // informational (e.g. CACHE, or debug)
    EV_SET_CACHE_ENTRY,

    // Targets
    EV_TARGET_DECLARE,
    EV_TARGET_ADD_SOURCE,
    EV_TARGET_PROP_SET,
    EV_TARGET_INCLUDE_DIRECTORIES,
    EV_TARGET_COMPILE_DEFINITIONS,
    EV_TARGET_COMPILE_OPTIONS,
    EV_TARGET_LINK_LIBRARIES,
    EV_TARGET_LINK_OPTIONS,
    EV_TARGET_LINK_DIRECTORIES,
    EV_CUSTOM_COMMAND_TARGET,
    EV_CUSTOM_COMMAND_OUTPUT,

    // Directory-level state
    EV_DIR_PUSH,
    EV_DIR_POP,
    EV_DIRECTORY_INCLUDE_DIRECTORIES,
    EV_DIRECTORY_LINK_DIRECTORIES,

    // Global compile state
    EV_GLOBAL_COMPILE_DEFINITIONS,
    EV_GLOBAL_COMPILE_OPTIONS,
    EV_GLOBAL_LINK_OPTIONS,
    EV_GLOBAL_LINK_LIBRARIES,

    // Testing / install
    EV_TESTING_ENABLE,
    EV_TEST_ADD,
    EV_INSTALL_ADD_RULE,

    // CPack subset
    EV_CPACK_ADD_INSTALL_TYPE,
    EV_CPACK_ADD_COMPONENT_GROUP,
    EV_CPACK_ADD_COMPONENT,

    // Package discovery
    EV_FIND_PACKAGE,
} Cmake_Event_Kind;

typedef enum {
    EV_VISIBILITY_UNSPECIFIED = 0,
    EV_VISIBILITY_PRIVATE,
    EV_VISIBILITY_PUBLIC,
    EV_VISIBILITY_INTERFACE,
} Cmake_Visibility;

typedef enum {
    EV_PROP_SET = 0,
    EV_PROP_APPEND_LIST,
    EV_PROP_APPEND_STRING,
} Cmake_Target_Property_Op;

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
    EV_DIAG_WARNING = 0,
    EV_DIAG_ERROR,
} Cmake_Diag_Severity;

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
} Cmake_Event_Origin;

typedef struct {
    Cmake_Event_Kind kind;
    Cmake_Event_Origin origin;

    union {
        struct {
            Cmake_Diag_Severity severity;
            String_View component;
            String_View command;
            String_View code;
            String_View error_class;
            String_View cause;
            String_View hint;
        } diag;

        struct {
            String_View name;
            String_View version;
            String_View description;
            String_View languages; // semi-separated list
        } project_declare;

        struct {
            String_View key;
            String_View value;
        } var_set;

        struct {
            String_View key;
            String_View value;
        } cache_entry;

        struct {
            String_View name;
            Cmake_Target_Type type;
        } target_declare;

        struct {
            String_View target_name;
            String_View path;
        } target_add_source;

        struct {
            String_View target_name;
            String_View key;
            String_View value;
            Cmake_Target_Property_Op op;
        } target_prop_set;

        struct {
            String_View target_name;
            Cmake_Visibility visibility;
            String_View path;
            bool is_system;
            bool is_before;
        } target_include_directories;

        struct {
            String_View target_name;
            Cmake_Visibility visibility;
            String_View item;
        } target_compile_definitions;

        struct {
            String_View target_name;
            Cmake_Visibility visibility;
            String_View item;
        } target_compile_options;

        struct {
            String_View target_name;
            Cmake_Visibility visibility;
            String_View item;
        } target_link_libraries;

        struct {
            String_View target_name;
            Cmake_Visibility visibility;
            String_View item;
        } target_link_options;

        struct {
            String_View target_name;
            Cmake_Visibility visibility;
            String_View path;
        } target_link_directories;

        struct {
            String_View target_name;
            bool pre_build;
            String_View *commands;
            size_t command_count;
            String_View working_dir;
            String_View comment;
            String_View outputs; // semi-separated list
            String_View byproducts; // semi-separated list
            String_View depends; // semi-separated list
            String_View main_dependency;
            String_View depfile;
            bool append;
            bool verbatim;
            bool uses_terminal;
            bool command_expand_lists;
            bool depends_explicit_only;
            bool codegen;
        } custom_command_target;

        struct {
            String_View *commands;
            size_t command_count;
            String_View working_dir;
            String_View comment;
            String_View outputs; // semi-separated list
            String_View byproducts; // semi-separated list
            String_View depends; // semi-separated list
            String_View main_dependency;
            String_View depfile;
            bool append;
            bool verbatim;
            bool uses_terminal;
            bool command_expand_lists;
            bool depends_explicit_only;
            bool codegen;
        } custom_command_output;

        struct {
            String_View source_dir;
            String_View binary_dir;
        } dir_push;

        struct {
            String_View path;
            bool is_system;
            bool is_before;
        } directory_include_directories;

        struct {
            String_View path;
            bool is_before;
        } directory_link_directories;

        struct {
            String_View item;
        } global_compile_definitions;

        struct {
            String_View item;
        } global_compile_options;

        struct {
            String_View item;
        } global_link_options;

        struct {
            String_View item;
        } global_link_libraries;

        struct {
            bool enabled;
        } testing_enable;

        struct {
            String_View name;
            String_View command;
            String_View working_dir;
            bool command_expand_lists;
        } test_add;

        struct {
            Cmake_Install_Rule_Type rule_type;
            String_View item;
            String_View destination;
        } install_add_rule;

        struct {
            String_View name;
            String_View display_name;
        } cpack_add_install_type;

        struct {
            String_View name;
            String_View display_name;
            String_View description;
            String_View parent_group;
            bool expanded;
            bool bold_title;
        } cpack_add_component_group;

        struct {
            String_View name;
            String_View display_name;
            String_View description;
            String_View group;
            String_View depends; // semi-separated list
            String_View install_types; // semi-separated list
            bool required;
            bool hidden;
            bool disabled;
            bool downloaded;
        } cpack_add_component;

        struct {
            String_View package_name;
            String_View mode; // MODULE / CONFIG / AUTO
            bool required;
            bool found;
            String_View location;
        } find_package;
    } as;
} Cmake_Event;

typedef struct {
    Cmake_Event *items;
    size_t count;
    size_t capacity;
} Cmake_Event_Stream;

typedef struct {
    const Cmake_Event *current;
    size_t index;
    const Cmake_Event_Stream *stream;
} Event_Stream_Iterator;

Cmake_Event_Stream *event_stream_create(Arena *arena);
bool event_stream_push(Arena *event_arena, Cmake_Event_Stream *stream, Cmake_Event ev);
Event_Stream_Iterator event_stream_iter(const Cmake_Event_Stream *stream);
bool event_stream_next(Event_Stream_Iterator *it);
void event_stream_dump(const Cmake_Event_Stream *stream);

#endif // EVENT_IR_H_
