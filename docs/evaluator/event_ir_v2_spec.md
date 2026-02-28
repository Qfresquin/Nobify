# Event IR Implementation v2 (Normative)

## 1. Overview

The `event_ir.c` module implements the **canonical data structure** for the communication between the Evaluator and the Build Model Builder. It defines the `Cmake_Event` types, the stream container, and memory management for event payloads.

**Role:** Pure Data Container. No logic.

## 2. Data Structures

### 2.1. Event Kinds (`Cmake_Event_Kind`)
Defined in `event_ir_types.h`.
*   **Structure:** `EV_PROJECT_DECLARE`, `EV_TARGET_DECLARE`, `EV_TARGET_ADD_SOURCE`, `EV_TARGET_LINK_LIBRARIES`, `EV_TARGET_INCLUDE_DIRECTORIES`, `EV_TARGET_COMPILE_DEFINITIONS`, `EV_TARGET_COMPILE_OPTIONS`, `EV_TARGET_PROP_SET`.
*   **Scope:** `EV_DIRECTORY_SCOPE_ENTER`, `EV_DIRECTORY_SCOPE_EXIT`, `EV_DIRECTORY_INCLUDE_DIRECTORIES`, `EV_DIRECTORY_LINK_DIRECTORIES`, `EV_DIRECTORY_COMPILE_DEFINITIONS`, `EV_DIRECTORY_COMPILE_OPTIONS`.
*   **Variables:** `EV_VAR_SET` (Only for CACHE/Exported vars).
*   **Diagnostics:** `EV_DIAGNOSTIC`.

### 2.2. Event Payload (`Cmake_Event`)
A tagged union structure optimized for size and clarity.

```c
typedef struct {
    Cmake_Event_Kind kind;
    Cmake_Event_Origin origin; // File, Line, Col

    union {
        struct { String_View name; String_View version; } project;
        struct { String_View name; Target_Type type; } target_decl;
        struct { String_View target; String_View path; } target_source;
        struct { String_View target; String_View item; Visibility vis; } target_link;
        struct { String_View target; String_View dir; Visibility vis; } target_include;
        struct { String_View target; String_View def; Visibility vis; } target_compile_def;
        struct { String_View target; String_View opt; Visibility vis; } target_compile_opt;
        struct { String_View target; String_View key; String_View val; } target_prop;
        
        // Scope events (Directory level properties)
        struct { String_View dir; } dir_include;
        struct { String_View opt; } dir_compile_opt;
        struct { String_View def; } dir_compile_def;

        struct { String_View key; String_View val; String_View doc; } var_set;
        struct { Diagnostic_Level level; String_View msg; } diagnostic;
    } as;
} Cmake_Event;
```

### 2.3. Event Stream (`Cmake_Event_Stream`)
A simple dynamic array.
```c
typedef struct {
    Cmake_Event *items;
    size_t count;
    size_t capacity;
    Arena *arena; // Owns the array memory
} Cmake_Event_Stream;
```

## 3. Memory Management (Refinement D)

### 3.1. Event Arena
The `Cmake_Event_Stream` is backed by a dedicated `Arena`.
*   **Persistence:** This arena must survive until the Builder finishes processing all events.
*   **Ownership:** The Evaluator writes to it. The Builder reads from it.
*   **Cleanup:** After the Build Model is frozen (Deep Copy), this arena is destroyed to reclaim memory.

### 3.2. String Copying
When the Evaluator emits an event, strings (e.g., paths expanded from variables) are often in temporary memory.
**Rule:** The `event_stream_push` function **must deep copy** `String_View` payloads into the stream's arena to ensure validity.

## 4. Interface

```c
// event_ir.h

// Creates a new stream using the provided arena.
Cmake_Event_Stream* event_stream_create(Arena *arena);

// Appends an event to the stream. Deep copies string payloads.
void event_stream_push(Cmake_Event_Stream *stream, Cmake_Event event);

// Iterator for consuming the stream.
typedef struct {
    const Cmake_Event *current;
    size_t index;
    const Cmake_Event_Stream *stream;
} Event_Stream_Iterator;

Event_Stream_Iterator event_stream_iter(const Cmake_Event_Stream *stream);
bool event_stream_next(Event_Stream_Iterator *it);

// Debug helper: Dumps the stream to stdout in human-readable format.
void event_stream_dump(const Cmake_Event_Stream *stream);
```

## 5. Serialization (Optional Future)
The simple struct layout allows for easy binary serialization (e.g., dumping events to a file for replay/debugging) if needed later. For v2, in-memory is sufficient.
