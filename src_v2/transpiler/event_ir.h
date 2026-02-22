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

    // Global compile state
    EV_GLOBAL_COMPILE_DEFINITIONS,
    EV_GLOBAL_COMPILE_OPTIONS,

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
            String_View item;
        } global_compile_definitions;

        struct {
            String_View item;
        } global_compile_options;

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
