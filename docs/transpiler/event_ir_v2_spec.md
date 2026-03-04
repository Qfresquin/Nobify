# Event IR Implementation v2 (Normative)

## 1. Overview

The `event_ir.c` module implements the **canonical data structure** for the communication between the Evaluator and the Build Model Builder. It defines the `Event` types, the stream container, and memory management for event payloads.

**Role:** Pure Data Container. No logic.

## 2. Data Structures

### 2.1. Event Kinds (`Event_Kind`)
Defined in `src_v2/transpiler/event_ir.h` through `EVENT_KIND_LIST(...)`.
*   **Structure:** `EVENT_PROJECT_DECLARE`, `EVENT_TARGET_DECLARE`, `EVENT_TARGET_ADD_SOURCE`, `EVENT_TARGET_LINK_LIBRARIES`, `EVENT_TARGET_INCLUDE_DIRECTORIES`, `EVENT_TARGET_COMPILE_DEFINITIONS`, `EVENT_TARGET_COMPILE_OPTIONS`, `EVENT_TARGET_PROP_SET`.
*   **Scope/Flow:** `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_DIR_PUSH`, `EVENT_DIR_POP`, `EVENT_FLOW_*`.
*   **Variables:** `EVENT_VAR_SET`, `EVENT_VAR_UNSET`.
*   **Meta bridge:** `EVENT_COMMAND_CALL`, `EVENT_CMAKE_LANGUAGE_*`.
*   **Diagnostics:** `EVENT_DIAG`.

### 2.2. Event Payload (`Event`)
A tagged union structure optimized for size and clarity.

```c
typedef struct {
    Event_Header h;

    union {
        Event_Diag diag;
        Event_Var_Set var_set;
        Event_Var_Unset var_unset;
        Event_Scope_Push scope_push;
        Event_Scope_Pop scope_pop;
        Event_Project_Declare project_declare;
        Event_Target_Declare target_declare;
        Event_Target_Add_Source target_add_source;
        Event_Target_Link_Libraries target_link_libraries;
        Event_Target_Include_Directories target_include_directories;
        Event_Target_Compile_Definitions target_compile_definitions;
        Event_Target_Compile_Options target_compile_options;
        Event_Target_Prop_Set target_prop_set;
        Event_Dir_Push dir_push;
        Event_Dir_Pop dir_pop;
        Event_Command_Call command_call;
        ...
    } as;
} Event;
```

For variable mutation payloads, the canonical semantic contract is:

```c
typedef struct {
    String_View key;
    String_View value;
    Event_Var_Target_Kind target_kind; // current, cache, env
} Event_Var_Set;
```

`Event_Command_Call` is the universal command-level breadcrumb emitted for every dispatched command.

### 2.3. Event Stream (`Event_Stream`)
A simple dynamic array.
```c
typedef struct {
    Event *items;
} Event_Stream;
```

## 3. Memory Management (Refinement D)

### 3.1. Event Arena
The `Event_Stream` is backed by a dedicated `Arena`.
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
Event_Stream* event_stream_create(Arena *arena);

// Appends an event to the stream. Deep copies string payloads.
bool event_stream_push(Arena *event_arena, Event_Stream *stream, Event event);

// Iterator for consuming the stream.
typedef struct {
    const Event *current;
    size_t index;
    const Event_Stream *stream;
} Event_Stream_Iterator;

Event_Stream_Iterator event_stream_iter(const Event_Stream *stream);
bool event_stream_next(Event_Stream_Iterator *it);

// Debug helper: Dumps the stream to stdout in human-readable format.
void event_stream_dump(const Event_Stream *stream);
```

## 5. Serialization (Optional Future)
The simple struct layout allows for easy binary serialization (e.g., dumping events to a file for replay/debugging) if needed later. For v2, in-memory is sufficient.
