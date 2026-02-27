# Evaluator Dispatcher v2 (Normative)

## 1. Overview

The `eval_dispatcher.c` module is the command routing layer of the Evaluator. It acts as the bridge between the raw AST command nodes and the structured Event Stream.

**Responsibilities:**
1.  **Command Lookup:** Find the handler for a given command name (e.g., `add_executable`).
2.  **Argument Parsing:** Validate the number and type of arguments.
3.  **Variable Resolution:** Call `eval_expand_vars` on arguments before processing.
4.  **Event Emission:** Construct and push `Cmake_Event` structs to the output stream.

## 2. Dispatch Mechanism

### 2.1. The Dispatch Table
A static constant array of `Command_Entry` structures maps string keys to function pointers.
*   **Key:** Command name (case-insensitive).
*   **Value:** Handler function.
*   **Lookup:** Binary search or hash map (implementation detail, currently binary search for simplicity/performance).

### 2.2. Handler Signature
All handlers must conform to:
```c
typedef void (*Cmd_Handler)(Evaluator_Context *ctx, const Node *node);
```

### 2.3. Fallback
If a command is not found in the table:
1.  Check if it is a user-defined function/macro (via `ctx->scopes`).
2.  If yes, invoke the function/macro evaluator.
3.  If no, emit `EV_DIAGNOSTIC` (Warning: "Unknown command").

## 3. Standard Handlers Implementation

### 3.1. Project & Targets
*   `project(NAME ...)` -> `EV_PROJECT_DECLARE`
    *   Parses `VERSION`, `LANGUAGES`, `DESCRIPTION`.
*   `add_executable(name [WIN32] [MACOSX_BUNDLE] [EXCLUDE_FROM_ALL] source1 ...)` ->
    1.  `EV_TARGET_DECLARE { name="name", type=TARGET_EXECUTABLE }`
    2.  `EV_TARGET_PROP_SET { target="name", key="WIN32_EXECUTABLE", val="ON" }` (if `WIN32` present)
    3.  `EV_TARGET_ADD_SOURCE` for each source file.
*   `add_library(name [STATIC|SHARED|MODULE] source1 ...)` ->
    1.  `EV_TARGET_DECLARE { name="name", type=TARGET_... }`
    2.  `EV_TARGET_ADD_SOURCE` for each source file.

### 3.2. Properties & Flags
*   `set_target_properties(target PROPERTIES prop val ...)` ->
    *   Iterates pairs of `prop` and `val`.
    *   Emits `EV_TARGET_PROP_SET` for each pair.
    *   Limitation: does not update an evaluator-side property store; only emits events.
*   `set_property(TARGET ... PROPERTY key value...)` ->
    *   Emits `EV_TARGET_PROP_SET` with operation semantics (`SET`, `APPEND`, `APPEND_STRING`) encoded in the event payload.
    *   Limitation: same as above, no evaluator-side materialized property state.
*   `target_include_directories(target [SYSTEM] [BEFORE] <INTERFACE|PUBLIC|PRIVATE> items...)` ->
    *   Parses visibility scope.
    *   Emits `EV_TARGET_INCLUDE_DIRECTORIES` for each item with the correct visibility.
*   `target_compile_definitions` -> `EV_TARGET_COMPILE_DEFINITIONS`
*   `target_compile_options` -> `EV_TARGET_COMPILE_OPTIONS`
*   `target_link_libraries` -> `EV_TARGET_LINK_LIBRARIES`
    *   **Special Handling:** Distinguishes between linking a *target* (e.g., `ZLIB::ZLIB`) and a *file path* (e.g., `/usr/lib/libz.so`) based on heuristics or explicit flags (though ideally passed raw to Build Model which handles the distinction).

### 3.3. Variables & Flow Control (Evaluator-Internal)
*   `set(VAR val)`:
    *   **Action:** Updates `ctx->scopes` directly.
    *   **Event:** Emits `EV_VAR_SET` **ONLY IF** the variable is marked as CACHE or intended for export (policy configurable). By default, local variables do not generate events.
*   `unset(VAR)`:
    *   **Action:** Removes from `ctx->scopes`.
*   `message([STATUS|WARNING|FATAL_ERROR] "msg")`:
    *   **Action:** Prints to stdout/stderr immediately during transpilation.
    *   **Event:** Emits `EV_DIAGNOSTIC` for `WARNING` and `FATAL_ERROR` levels to ensure the build model captures the intent.

### 3.4. File System & Globbing
*   `file(GLOB var patterns...)`:
    *   **Action:** Evaluator performs the globbing on the *host* filesystem immediately.
    *   **Result:** Sets variable `var` to the list of found files.
    *   **Event:** None directly (the variable set is internal).
*   `file(WRITE ...)`:
    *   **Action:** Writes the file on the host immediately.
    *   **Constraint:** Since transpilation happens before build, this side-effect is immediate.

### 3.5. CMake Policy Directives
*   `cmake_minimum_required(VERSION ...)`:
    *   Parsed by evaluator and stored in variables (`CMAKE_MINIMUM_REQUIRED_VERSION`, `CMAKE_POLICY_VERSION`).
*   `cmake_policy(...)`:
    *   V2 supports basic `VERSION`, `SET`, and `GET` state handling.
    *   `PUSH`/`POP` are accepted with warning and currently no-op.
    *   Policy-dependent behavioral changes (for example, subtle `if()` argument semantics changes tied to specific CMP policies) are not fully modeled in evaluator v2.

## 4. Helper Functions

```c
// eval_dispatcher.h

// Main entry point for command dispatch
void eval_dispatch_command(Evaluator_Context *ctx, const Node *node);

// Resolves arguments, expanding variables and handling quoting
Args eval_resolve_args(Evaluator_Context *ctx, const Args *raw_args);

// Parses visibility keywords (PUBLIC, PRIVATE, INTERFACE) from an argument list
// Returns the index where the keyword was found or -1.
int eval_parse_visibility(const Args *args, int start_index, Visibility *out_vis);
```

## 5. Error Handling in Dispatcher

*   **Argument Count Mismatch:** Emit `EV_DIAGNOSTIC` (Error: "Function called with incorrect number of arguments").
*   **Invalid Argument Type:** Emit `EV_DIAGNOSTIC` (Error: "Invalid argument for command").
*   **Missing Required Argument:** Emit `EV_DIAGNOSTIC` (Error: "Missing required argument").

## 6. Generator Expressions Note

As per the general Evaluator spec, arguments containing `$<...>` are passed through as literal strings to the event payload. The Dispatcher does **not** attempt to evaluate them.

## 7. Compatibility Limitation: Property Read Commands

Because the Dispatcher/Evaluator do not own a target-property state table, read-side commands such as `get_target_property(...)` and `get_property(TARGET ...)` are not currently representable with full CMake semantics. Implementing them requires either:
1.  A dedicated evaluator-side property state cache (new layer), or
2.  Deferring these commands to a model-aware phase with explicit policy.
