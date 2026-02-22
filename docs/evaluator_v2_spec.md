# Evaluator v2 Specification (Normative)

## 1. Overview and Responsibility

The **Evaluator** acts as the "CPU" of the transpilation process. It is responsible for executing the logic within `CMakeLists.txt`, resolving variables, flattening control flow, and emitting a linear stream of atomic events.

**Strict Boundary:**
*   **Input:** `Ast_Root` (Read-only).
*   **Output:** `Cmake_Event_Stream` (Append-only).
*   **Forbidden:** The Evaluator **must not** depend on or include `build_model.h`. It does not know what a "Target" struct looks like; it only knows how to emit an `EV_TARGET_DECLARE` event.

## 2. Architecture & Memory Model

### 2.1. File Structure
*   `src/evaluator/evaluator.c`: Core loop, scope management, control flow (`if`, `foreach`).
*   `src/evaluator/eval_expr.c`: Boolean expression evaluation and variable expansion (`${VAR}`).
*   `src/evaluator/eval_dispatcher.c`: Command dispatch table mapping strings to event emitters.
*   `src/evaluator/eval_context.h`: Definition of the execution state.

### 2.2. Context Definition (`eval_context.h`)

The context holds the mutable state of the execution *before* it becomes a build model artifact.

```c
typedef struct {
    // Memory Management
    Arena *arena;           // Arena for temporary expansion & scope strings
    Arena *event_arena;     // Persistent arena for the Event Stream (Refinement D)

    // State
    Symbol_Table *scopes;   // Stack of variable scopes (Global -> Function -> Block)
    size_t scope_depth;
    
    // Output
    Cmake_Event_Stream *stream; // The linear output log
    
    // Configuration
    String_View source_dir; // CMAKE_SOURCE_DIR
    String_View binary_dir; // CMAKE_BINARY_DIR
    
    // Origin Tracking (Refinement C)
    const char *current_file;
    size_t current_line;
} Evaluator_Context;
```

## 3. Core Logic: The Evaluation Loop

The entry point `evaluator_run(ctx, ast)` iterates over the AST nodes. Unlike v1, this is a **flattening** process.

### 3.1. Variable Resolution (Conflict Resolution A)
The Evaluator is the sole owner of variable state.
*   When `set(MY_SRC "main.c")` is encountered: The Evaluator updates its `Symbol_Table`. **No event is emitted** for local variables (unless exported to Cache).
*   When `add_executable(app ${MY_SRC})` is encountered:
    1.  The Evaluator expands `${MY_SRC}` -> `"main.c"`.
    2.  The Evaluator resolves arguments.
    3.  The Evaluator emits `EV_TARGET_ADD_SOURCE { name="app", path="main.c" }`.

**Result:** The Build Model receives data that is already resolved. It never sees `${...}`.

### 3.4. Target Property State (Current Limitation)
The Evaluator does **not** maintain a materialized in-memory map of target properties.
*   Commands like `set_target_properties(...)` and `set_property(TARGET ...)` are treated as **event producers** only.
*   Property semantics (overwrite/append resolution, effective value by config, genex expansion) are delegated to the consumer side (Build Model / later phases).
*   Consequence: `get_target_property(...)` and `get_property(TARGET ...)` cannot be implemented with CMake-compatible behavior in the Evaluator alone without introducing a property state layer.

This is intentional for v2 boundary purity (Evaluator -> Event Stream), but it must be considered a compatibility gap.

### 3.2. Control Flow Flattening
*   **IF/ELSE:** The Evaluator evaluates the condition. Only the nodes inside the *taken* branch are processed and produce events. The `if` itself produces no event.
*   **FOREACH:** The Evaluator unrolls the loop. If a loop runs 5 times adding sources, 5 distinct `EV_TARGET_ADD_SOURCE` events are appended to the stream.

### 3.3. Command Dispatching
The `eval_dispatcher.c` maps command names to handlers.

```c
// Example Handler Signature
void handle_target_sources(Evaluator_Context *ctx, const Node *node) {
    // 1. Resolve Arguments (Expand Variables)
    Args resolved = eval_expand_args(ctx, node->args);

    // 2. Extract Origin (Refinement C)
    Cmake_Event_Origin origin = {
        .file = ctx->current_file,
        .line = node->token.line
    };

    // 3. Emit Events
    String_View target_name = resolved.items[0];
    for (size_t i = 1; i < resolved.count; i++) {
        Cmake_Event ev = {
            .kind = EV_TARGET_ADD_SOURCE,
            .origin = origin,
            .target_name = target_name,
            .path = resolved.items[i] // "main.c", not "${SRC}"
        };
        event_stream_push(ctx->stream, ev);
    }
}
```

## 4. Origin Traceability (Refinement C Implementation)

To support the `--trace-origin` feature documented in `docs/implementation_granular_origin.md`, the Evaluator must attach origin metadata to **every** event that defines data.

### 4.1. The `Cmake_Event` Structure
Defined in `src/transpiler/event_ir_types.h` (but populated here):

```c
typedef struct {
    // ... payload ...
    struct {
        String_View file_path;
        size_t line;
        size_t col;
    } origin;
} Cmake_Event;
```

### 4.2. Propagation Rule
1.  **Lexer/Parser:** Tags `Node` with file/line.
2.  **Evaluator:** Reads `Node.origin`.
3.  **Evaluator:** Copies `Node.origin` into `Cmake_Event.origin` during emission.
4.  **Result:** Even after flattening loops and resolving variables, the final event knows exactly where the data came from.

## 5. Generator Expressions Handling

While the Evaluator resolves standard variables (`${VAR}`), it **must pass through** Generator Expressions (`$<CONFIG:Debug>`) as literal strings.
*   **Reason:** GenEx evaluation often depends on the final build configuration (Debug/Release), which is a runtime property of `nob.c` or the compiler, not the transpilation time.
*   **Behavior:** `"$<TARGET_FILE:app>"` is treated as a string literal.

## 6. Error Handling

The Evaluator uses "Error as Data" via the Event Stream.
*   **Fatal Error (Syntax):** Returns `NULL` / stops execution.
*   **Semantic Error (Missing file):** Emits `EV_DIAGNOSTIC` with level `ERROR`.
*   **Warning:** Emits `EV_DIAGNOSTIC` with level `WARNING`.

This allows the Build Model validator to decide later if an error is fatal or if it can be suppressed by a compatibility policy.

## 7. Memory Management Strategy

1.  **Temporary Strings:** Created in `ctx->arena`. Cleared/Reset at the end of each statement or block to keep memory footprint low during recursive evaluation.
2.  **Persistent Strings:** Strings that must survive into the Event Stream (like target names, source paths) are copied to `ctx->event_arena`.
3.  **Lifecycle:** `ctx->event_arena` is handed off to the Builder, while `ctx->arena` is destroyed immediately after evaluation finishes.
