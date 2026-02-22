#ifndef EVALUATOR_H_
#define EVALUATOR_H_

// Evaluator v2
// - Input:  Ast_Root (parser.h)
// - Output: Cmake_Event_Stream (append-only)
// - Strict boundary: does NOT include or depend on build_model.h

#include <stddef.h>
#include <stdbool.h>

#include "nob.h"          // String_View
#include "arena.h"        // Arena
#include "parser.h"       // Ast_Root, Node, Args

// -----------------------------------------------------------------------------
// Event IR boundary (Evaluator -> Build Model)
// -----------------------------------------------------------------------------

typedef enum {
    EV_DIAGNOSTIC = 0,

    // Project
    EV_PROJECT_DECLARE,

    // Variables / cache
    EV_VAR_SET,           // informational (e.g. CACHE, or debug)
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

bool event_stream_push(Arena *event_arena, Cmake_Event_Stream *stream, Cmake_Event ev);

// -----------------------------------------------------------------------------
// Evaluator API
// -----------------------------------------------------------------------------

typedef struct Evaluator_Context Evaluator_Context;

typedef struct {
    Arena *arena;        // temporary expansions (rewound frequently)
    Arena *event_arena;  // persistent strings + event stream storage
    Cmake_Event_Stream *stream;

    String_View source_dir; // CMAKE_SOURCE_DIR
    String_View binary_dir; // CMAKE_BINARY_DIR

    const char *current_file; // for origin fallback
} Evaluator_Init;

Evaluator_Context *evaluator_create(const Evaluator_Init *init);
void evaluator_destroy(Evaluator_Context *ctx);

bool evaluator_run(Evaluator_Context *ctx, Ast_Root ast);

#endif // EVALUATOR_H_
