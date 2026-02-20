# Build Model Builder v2 (Normative)

## 1. Overview

The `build_model_builder.c` module implements the mutable state machine that constructs the Build Model. It consumes a linear stream of atomic events (`Cmake_Event`) and updates the internal structures (`Build_Model_Builder`) accordingly.

**Key Characteristics:**
1.  **Mutable:** The builder state changes with every event.
2.  **Incremental:** Events are processed one by one.
3.  **Scope-Aware (Directory Stack):** Tracks directory-level properties (e.g., `include_directories` that affect subsequent targets).
4.  **Transactional (Optional):** Supports rollback for failed events (e.g., if a target declaration fails validation).

## 2. Data Structures

### 2.1. Builder Context (`Build_Model_Builder`)
```c
typedef struct {
    Arena *arena;               // Memory arena for the builder lifetime
    Build_Model *model;         // The target model being built (mutable)
    
    // Directory Scope Stack (for inheritance)
    struct {
        String_List include_dirs;
        String_List link_dirs;
        String_List compile_defs;
        String_List compile_options;
    } current_scope;

    // Validation State
    bool has_fatal_error;
    Diag_Sink *diagnostics;
} Build_Model_Builder;
```

### 2.2. Event Handlers Map
The builder uses a switch/dispatch mechanism based on `Cmake_Event_Kind` to route events to specific update logic.

## 3. Event Handling Logic

### 3.1. Project & Targets
*   `EV_PROJECT_DECLARE`:
    *   Updates `model->project_name`, `model->project_version`, etc.
    *   Resets/Initializes global build flags if needed.
*   `EV_TARGET_DECLARE`:
    *   Creates a new `Build_Target` in `model->targets`.
    *   Sets `target->type` (EXECUTABLE, STATIC_LIBRARY, etc.).
    *   Initializes empty lists for sources, dependencies, etc.
    *   **Validation:** Checks for duplicate target names (error if exists).

### 3.2. Sources & Properties
*   `EV_TARGET_ADD_SOURCE`:
    *   Finds the target by name.
    *   Appends the source path to `target->sources`.
    *   **Deduplication:** Uses a hash set (or linear scan for small lists) to avoid duplicate sources.
*   `EV_TARGET_PROP_SET`:
    *   Finds the target.
    *   Updates the specific property (e.g., `OUTPUT_NAME`, `CXX_STANDARD`).
    *   Supports custom properties via a key-value map.

### 3.3. Dependencies & Linking
*   `EV_TARGET_LINK_LIBRARIES`:
    *   Finds the target.
    *   Parses the item (target name vs. file path vs. flag).
    *   Adds to `target->link_libraries` (and potentially `target->dependencies` if it's a known target).
    *   Handles `PUBLIC`, `PRIVATE`, `INTERFACE` visibility propagation.
*   `EV_TARGET_INCLUDE_DIRECTORIES`:
    *   Adds to `target->include_directories`.
    *   Handles visibility.

### 3.4. Directory Scope (Inheritance)
*   `EV_DIRECTORY_scope_ENTER`: Push a new scope frame (copy current state).
*   `EV_DIRECTORY_SCOPE_EXIT`: Pop the top scope frame.
*   `EV_DIRECTORY_INCLUDE_DIRECTORIES`: Adds to the *current* scope's include list.
    *   **Effect:** Any target declared *after* this event (within the same scope) inherits these includes.

## 4. Validation During Build

While `build_model_validate.c` does the heavy lifting post-build, the Builder performs immediate checks:
1.  **Existence:** References to non-existent targets in `LINK_LIBRARIES` (warn or error based on policy).
2.  **Type Safety:** Ensuring an `INTERFACE_LIBRARY` doesn't get sources (unless allowed by CMake policy CMP0019/etc - simplified here: usually error).
3.  **Duplicate Names:** Error on `add_executable(app)` if `app` already exists.

## 5. Interface

```c
// build_model_builder.h

// Creates a new builder instance
Build_Model_Builder* builder_create(Arena *arena, Diag_Sink *diags);

// Applies a single event to the builder state.
// Returns false on fatal error (e.g., out of memory).
bool builder_apply_event(Build_Model_Builder *builder, const Cmake_Event *ev);

// Finalizes the build process and returns the constructed model.
// The returned model is still mutable until frozen.
Build_Model* builder_finish(Build_Model_Builder *builder);
```

## 6. Memory Management Strategy

1.  **String Interning:** The builder ideally uses an interner or simply copies strings to its arena to avoid lifetime issues with the Event Stream.
    *   *Refinement D:* If the Event Stream persists, we can use `String_View` references. If not, we copy. The safest default for v2 is **Copy on Write** or just **Copy**.
2.  **Growth:** Arrays (targets, sources) grow dynamically using `arena_realloc` or linked lists (if performance dictates, though dynamic arrays are preferred for cache locality).
