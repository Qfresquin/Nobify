# Evaluator Runtime Model (Rewrite Draft)

Status: Draft rewrite. This document describes the runtime lifecycle and state topology currently implemented by `src_v2/evaluator`, with `evaluator_v2_spec.md` remaining the higher-level contract.

## 1. Scope

This document covers the evaluator runtime model for:
- `Evaluator_Context` creation and destruction,
- arena ownership and memory phases,
- persistent vs temporary runtime state,
- scope/policy visibility stacks,
- stop-state and OOM behavior,
- top-level run lifecycle and deferred flush boundaries.

It does not try to restate the detailed node-by-node execution rules. Those belong in `evaluator_execution_model.md`.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/evaluator/evaluator.h`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/evaluator_internal.h`
- `src_v2/evaluator/eval_compat.c`
- `src_v2/evaluator/eval_flow.c`

## 3. Runtime Object Model

The evaluator is modeled as one mutable runtime object:

```c
typedef struct Evaluator_Context Evaluator_Context;
```

High-level characteristics:
- one context owns the live evaluator state for a run sequence,
- the same context may execute multiple ASTs over time,
- nested file execution re-enters the same context (child-context isolation is out of scope in the current roadmap),
- the context is stateful and not designed as a pure/stateless executor.

Practical consequence:
- variable scopes, registered user commands, deferred work, and accumulated runtime metadata live on the context across command dispatch and nested execution.

## 4. Creation Contract

Primary constructor:

```c
Evaluator_Context *evaluator_create(const Evaluator_Init *init);
```

Current hard requirements:
- `init != NULL`
- `init->arena != NULL`
- `init->event_arena != NULL`
- `init->stream != NULL`

If any required input is missing:
- the function returns `NULL`
- no context is created

### 4.1 Initialization Inputs

`Evaluator_Init` currently supplies:
- the temporary arena (`arena`)
- the persistent arena (`event_arena`)
- the append-only event stream (`stream`)
- initial source/binary directories
- an optional `current_file`
- the initial compatibility profile

### 4.2 Initial Defaults

On successful allocation, the constructor sets:
- `return_context = EVAL_RETURN_CTX_TOPLEVEL`
- `return_requested = false`
- `return_propagate_vars = NULL`
- `compat_profile = EVAL_PROFILE_PERMISSIVE` unless `STRICT` or `CI_STRICT` was requested
- `unsupported_policy = EVAL_UNSUPPORTED_WARN`
- `error_budget = 0`

Current `error_budget` meaning:
- `0` means "unlimited" in the permissive profile path

### 4.3 Bootstrap Allocations

`evaluator_create(...)` allocates the context itself in `event_arena` using `arena_alloc_zero(...)`.

It also creates two sub-arenas:
- `known_targets_arena`
- `user_commands_arena`

Current lifecycle binding:
- both sub-arenas are registered as destroy callbacks on `event_arena`
- when `event_arena` is destroyed, the sub-arenas are destroyed automatically

### 4.4 Bootstrap Runtime State

On a successful create path, the evaluator also:
- copies `current_file` into `event_arena` when provided
- pushes one initial global variable scope
- sets `visible_scope_depth = 1`
- pushes one initial deferred-directory frame for `source_dir` / `binary_dir`
- resets `run_report`

The global scope is an invariant of the live runtime:
- a healthy created context starts with at least one scope visible

## 5. Memory Model and Ownership

The evaluator uses a split-memory model instead of one uniform heap.

### 5.1 Temporary Arena

Field:
- `ctx->arena`

Role:
- scratch storage for statement-local work
- temporary argument expansion
- short-lived `SV_List` materialization
- transient synthetic AST fragments for deferred execution

Operational pattern:
- code frequently marks and rewinds this arena
- `eval_node(...)` rewinds it after each statement

Practical consequence:
- data allocated only in the temp arena must not be retained as persistent evaluator state

### 5.2 Persistent Arena

Field:
- `ctx->event_arena`

Role:
- stores the `Evaluator_Context` itself
- stores persistent strings copied into evaluator-owned state
- stores event payload strings
- backs arena-array growth for many persistent runtime lists
- survives for the full evaluator/event-stream lifetime

Examples of data commonly copied here:
- diagnostic payload strings
- registered user-command names and cloned bodies
- deferred directory metadata
- `current_file`

### 5.3 Specialized Sub-Arenas

Additional persistent allocators:
- `ctx->known_targets_arena`
- `ctx->user_commands_arena`

These exist to isolate growth for specific long-lived registries while still remaining tied to the `event_arena` lifecycle.

### 5.4 Non-Arena Heap State

Not all evaluator memory is arena-backed.

Current explicit heap-backed state includes:
- `cache_entries` hash storage
- per-scope `vars` hash storage

These use `stb_ds` allocation internally and are explicitly released by `evaluator_destroy(...)`.

## 6. Bootstrapped Runtime Environment

Creation does more than allocate structs. It seeds the evaluator with a baseline runtime environment.

### 6.1 Canonical Directory and List Variables

The constructor seeds variables such as:
- `CMAKE_SOURCE_DIR`
- `CMAKE_BINARY_DIR`
- `CMAKE_CURRENT_SOURCE_DIR`
- `CMAKE_CURRENT_BINARY_DIR`
- `CMAKE_CURRENT_LIST_DIR`
- `CMAKE_CURRENT_LIST_FILE`
- `CMAKE_CURRENT_LIST_LINE`

This establishes the initial file/list context before any AST executes.

### 6.2 Compatibility and Nobify Control Variables

The constructor also initializes:
- `NOBIFY_POLICY_STACK_DEPTH`
- `CMAKE_POLICY_VERSION`
- `CMAKE_NOBIFY_CONTINUE_ON_ERROR`
- `CMAKE_NOBIFY_COMPAT_PROFILE`
- `CMAKE_NOBIFY_ERROR_BUDGET`
- `CMAKE_NOBIFY_UNSUPPORTED_POLICY`
- `CMAKE_NOBIFY_FILE_GLOB_STRICT`

Important runtime property:
- these variables are not only informational,
- `eval_refresh_runtime_compat(...)` re-reads the evaluator-core compatibility subset at command-cycle entry,
- command-specific knobs such as `CMAKE_NOBIFY_FILE_GLOB_STRICT` remain owned by their local consumers.

### 6.3 Host / Toolchain Compatibility Built-Ins

The current implementation seeds compatibility variables commonly used by scripts, including:
- `WIN32`
- `UNIX`
- `APPLE`
- `MSVC`
- `MINGW`
- `CMAKE_VERSION` and version fragments
- `CMAKE_SYSTEM_*`
- `CMAKE_HOST_SYSTEM_*`
- `CMAKE_COMMAND`
- `PROJECT_NAME`
- `PROJECT_VERSION`
- `CMAKE_C_COMPILER`
- `CMAKE_CXX_COMPILER`
- `CMAKE_C_COMPILER_ID`
- `CMAKE_CXX_COMPILER_ID`

This means a newly created evaluator is not an empty shell. It starts with a compatibility-oriented baseline environment.

## 7. State Topology Inside `Evaluator_Context`

The runtime state is concentrated in `Evaluator_Context`.

### 7.1 Configuration and Location State

Core location/config fields:
- `source_dir`
- `binary_dir`
- `current_file`
- `stream`

These anchor:
- event origin fallback,
- current directory semantics,
- the append-only semantic output sink.

### 7.2 Variable and Policy Stacks

Mutable stack-like state:
- `scopes`
- `visible_scope_depth`
- `policy_levels`
- `visible_policy_depth`

The implementation tracks both:
- the physical stack length
- the currently visible depth

This allows temporary view changes without physically popping underlying data.

### 7.3 Persistent Registries and Side Tables

Longer-lived runtime collections include:
- `known_targets`
- `alias_targets`
- `user_commands`
- `property_definitions`
- `cache_entries`
- `active_find_packages`
- `watched_variables`
- `watched_variable_commands`

These are part of the evaluator's persistent semantic memory rather than statement-local scratch state.

### 7.4 Active Execution Stacks

Execution-shape stacks include:
- `macro_frames`
- `block_frames`
- `deferred_dirs`
- `file_locks`
- `file_generate_jobs`
- `message_check_stack`

These fields track currently open runtime constructs and deferred operational work.

### 7.5 Counters and Depth Indicators

Current counters/depth fields include:
- `next_deferred_call_id`
- `file_eval_depth`
- `function_eval_depth`
- `loop_depth`

These are used to:
- detect whether flow control is valid in the current context,
- scope file-lock ownership,
- tag nested execution state,
- manage deferred-call ordering.

### 7.6 Flow, Compatibility, and Report State

Execution control and reporting fields:
- `break_requested`
- `continue_requested`
- `return_requested`
- `return_context`
- `return_propagate_vars`
- `compat_profile`
- `unsupported_policy`
- `error_budget`
- `run_report`

These fields connect the runtime model directly to:
- flow control,
- diagnostics,
- compatibility behavior,
- final run status.

### 7.7 Stop-State Flags

Global stop flags:
- `oom`
- `stop_requested`

These are sticky runtime failure signals. Once set, most evaluator entry points stop doing useful work.

## 8. Visibility Model for Scopes and Policies

The evaluator distinguishes between:
- stored stack depth
- visible stack depth

This is a runtime design choice, not an incidental implementation detail.

### 8.1 Scope Visibility

Helpers:
- `eval_scope_visible_depth(...)`
- `eval_scope_use_parent_view(...)`
- `eval_scope_use_global_view(...)`
- `eval_scope_restore_view(...)`

Current behavior:
- parent/global lookups can be modeled by temporarily lowering `visible_scope_depth`
- the underlying `scopes` array remains intact

Practical consequence:
- some commands can evaluate against parent/global scope views without mutating the real scope stack shape

### 8.2 Policy Visibility

The same model exists for policy state:
- `policy_levels` stores the stack
- `visible_policy_depth` controls what is currently in effect

This keeps policy evaluation aligned with block/scope-style visibility rather than requiring destructive pops for every temporary view shift.

## 9. Stop, OOM, and Failure Propagation

The evaluator uses a cooperative stop protocol shared across command handlers, diagnostics, and nested execution.

### 9.1 Global Stop Predicate

Central predicate:

```c
bool eval_should_stop(Evaluator_Context *ctx);
```

Current behavior:
- returns `true` for `NULL` contexts
- returns `true` when `ctx->oom` is set
- returns `true` when `ctx->stop_requested` is set

### 9.2 OOM Path

Central OOM helper:

```c
bool ctx_oom(Evaluator_Context *ctx);
```

Current behavior:
- sets `ctx->oom = true`
- sets `ctx->stop_requested = true`
- returns `false`

Practical consequence:
- allocation helpers can use `ctx_oom(...)` as both a side effect and a failure return path
- OOM is terminal for the current context unless the caller abandons it

### 9.3 Explicit Stop Requests

Helpers:
- `eval_request_stop(...)`
- `eval_request_stop_on_error(...)`

Current behavior:
- `eval_request_stop(...)` unconditionally sets `stop_requested`
- `eval_request_stop_on_error(...)` respects the current-cycle `continue_on_error_snapshot`; if that snapshot is truthy, it does not stop immediately

This makes stop behavior partly data-driven through command-cycle compatibility snapshots, not only through fixed constructor settings.

## 10. Runtime Compatibility as Live State

The compatibility profile is not frozen strictly at construction time.

Public API:

```c
bool evaluator_set_compat_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile);
```

Internal refresh path:
- `eval_refresh_runtime_compat(...)`, called once from `eval_node(...)` at command-cycle entry

Current behavior:
- `evaluator_set_compat_profile(...)` updates `ctx->compat_profile`
- it also updates the visible variables `CMAKE_NOBIFY_COMPAT_PROFILE` and `CMAKE_NOBIFY_CONTINUE_ON_ERROR`
- at the next command-cycle boundary, `eval_refresh_runtime_compat(...)` re-reads:
  - `CMAKE_NOBIFY_COMPAT_PROFILE`
  - `CMAKE_NOBIFY_UNSUPPORTED_POLICY`
  - `CMAKE_NOBIFY_ERROR_BUDGET`
  - `CMAKE_NOBIFY_CONTINUE_ON_ERROR`

Important consequence:
- compatibility behavior is a live part of runtime state
- scripts can influence future evaluator behavior by mutating these variables,
- those mutations become effective on the next command cycle unless a command documents a local immediate-read rule.

## 11. Run Lifecycle

Primary execution entry point:

```c
Eval_Result evaluator_run(Evaluator_Context *ctx, Ast_Root ast);
```

### 11.1 Entry Behavior

At the start of `evaluator_run(...)`, the evaluator:
- rejects `NULL` or already-stopped contexts
- increments `file_eval_depth`
- resets `run_report`

This means each top-level run gets a fresh report, but it does not get a brand-new context.

### 11.2 Main Execution Phase

The main body of the run:
- executes the AST through `eval_node_list(...)`

The runtime state mutated during that traversal remains on the same context unless explicitly unwound by control-flow handlers.

### 11.3 Post-Traversal Flush Phase

If execution is still healthy, the evaluator then performs:
- `eval_defer_flush_current_directory(...)`
- `eval_file_generate_flush(...)`

This is an explicit runtime phase boundary:
- some operational side effects are deferred until after immediate AST traversal

### 11.4 Exit Cleanup

Before returning, `evaluator_run(...)` currently:
- clears return state
- releases file locks owned by the entered file scope
- decrements `file_eval_depth`
- finalizes the run report

Return condition:
- `EVAL_RESULT_OK` for clean runs,
- `EVAL_RESULT_SOFT_ERROR` when non-fatal errors were emitted,
- `EVAL_RESULT_FATAL` when stop-state was reached.

### 11.5 Inline Execution Path

Secondary execution entry point:

```c
Eval_Result eval_run_ast_inline(Evaluator_Context *ctx, Ast_Root ast);
```

This is a lighter-weight runtime path:
- it rejects `NULL` / stopped contexts
- it executes `eval_node_list(...)`
- it does not perform the full top-level report/flush/finalization sequence

This path is used for re-entrant execution such as deferred synthetic calls and similar nested runtime work.

## 12. Deferred Directory Runtime Frames

The evaluator keeps deferred execution state per active directory frame.

Key helpers:
- `eval_defer_push_directory(...)`
- `eval_defer_pop_directory(...)`
- `eval_defer_flush_current_directory(...)`

Current model:
- each frame stores `source_dir`, `binary_dir`, and queued deferred calls
- the frame is persistent runtime state stored under `deferred_dirs`
- flushing the current frame drains queued calls in FIFO order

Important runtime behavior:
- deferred calls are converted into synthetic `NODE_COMMAND` nodes
- those nodes execute through `eval_run_ast_inline(...)`
- return state is cleared after each deferred call

This means deferred work is not merely logged metadata. It re-enters the normal evaluator execution pipeline inside the same context.

## 13. Destruction and Cleanup Boundaries

Primary destructor:

```c
void evaluator_destroy(Evaluator_Context *ctx);
```

Current behavior:
- no-op on `NULL`
- cleans up file locks through `eval_file_lock_cleanup(...)`
- frees `cache_entries` hash storage
- frees each scope's `vars` hash storage

Important ownership boundary:
- `evaluator_destroy(...)` does not free the `Evaluator_Context` allocation itself
- it does not destroy `event_arena`
- it does not destroy the sub-arenas directly

Why:
- the context and most persistent arrays are arena-owned
- their actual storage is released when the caller destroys `event_arena`

Practical consequence:
- correct full teardown requires both:
  - `evaluator_destroy(ctx)`
  - later destruction of the owning arena(s), especially `event_arena`

## 14. Current Non-Goals / Limits

Current runtime-model limitations visible in the implementation:

- The context is stateful and not thread-safe by design.
- A run reset clears `run_report`, not the entire evaluator environment.
- Nested execution reuses one context; child-context isolation remains an explicit out-of-scope decision in this roadmap.
- Most persistent data is append/grow oriented; there is no general-purpose runtime snapshot/rollback API.
- OOM is treated as a sticky fatal state for the context.

## 15. Relationship to Other Docs

- `evaluator_v2_spec.md`
Defines the broader evaluator contract. This file is the runtime-state slice.

- `evaluator_execution_model.md`
Describes how the runtime state is consumed during AST traversal and command execution.

- `evaluator_variables_and_scope.md`
Should document variable lookup, scope mutation, and macro/function binding rules in more detail.

- `evaluator_diagnostics.md`
Describes how runtime failures and warnings are emitted, recorded, and turned into events.

## 16. Incremental Subsystemization Boundary

Refactor direction for runtime architecture is incremental extraction over shared context state.

Current target boundary:
- scope, policy, flow, diagnostics, dispatcher, file execution, and deferred queues evolve as internal services,
- those services continue to operate over the same `Evaluator_Context` integration boundary.

Explicit non-target in this roadmap:
- no child evaluator runtime context model for nested execution (`include`, subdirectory, inline/deferred execution).

## 17. Compatibility Refresh Timing Contract

Current behavior contract:
- evaluator-core compatibility knobs are refreshed once at a canonical command-cycle boundary before policy/diagnostic decisions,
- command-level decisions in that cycle use the same refreshed snapshot,
- mutations of `CMAKE_NOBIFY_*` variables become effective on the next command cycle unless a command explicitly documents immediate local refresh semantics.
