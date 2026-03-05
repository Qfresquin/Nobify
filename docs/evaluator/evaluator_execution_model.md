# Evaluator Execution Model (Rewrite Draft)

Status: Draft rewrite. This document describes how the evaluator currently executes AST nodes, propagates control flow, and enters nested execution contexts.

## 1. Scope

This document covers evaluator execution semantics for:
- top-level AST traversal,
- per-node execution,
- structural flow (`if`, `foreach`, `while`),
- user command registration and invocation (`function`, `macro`),
- flow-control propagation (`return`, `break`, `continue`),
- external file execution (`include`, `add_subdirectory` style nested evaluation).

It does not try to fully document the internals of every command handler.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/eval_flow.c`
- `src_v2/evaluator/eval_include.c`
- `src_v2/evaluator/evaluator_internal.h`

## 3. Top-Level Execution Entry Points

Primary public entry point:

```c
Eval_Result evaluator_run(Evaluator_Context *ctx, Ast_Root ast);
```

Current high-level behavior:
- Returns `EVAL_RESULT_FATAL` if `ctx == NULL`.
- Returns `EVAL_RESULT_FATAL` if execution is already in a stop state.
- Resets `Eval_Run_Report` at the start of the run.
- Executes the root AST through `eval_node_list(...)`.
- Flushes deferred directory work if execution remained healthy.
- Flushes deferred file-generation work if execution remained healthy.
- Clears return state before finishing.
- Finalizes the run report before returning.

Return condition:
- `EVAL_RESULT_OK` for clean runs.
- `EVAL_RESULT_SOFT_ERROR` when non-fatal errors were emitted.
- `EVAL_RESULT_FATAL` when stop-state was reached.

Secondary internal-style entry point:

```c
Eval_Result eval_run_ast_inline(Evaluator_Context *ctx, Ast_Root ast);
```

This executes a node list directly without the full top-level run-finalization path.

## 4. Root Traversal Model

The evaluator executes AST blocks through:

```c
static Eval_Result eval_node_list(Evaluator_Context *ctx, const Node_List *list);
```

Current block traversal contract:
- Nodes are executed in source order.
- Traversal stops immediately on:
  - hard execution failure,
  - `return_requested`,
  - `break_requested`,
  - `continue_requested`.

After each node:
- if `return_requested`, the evaluator unwinds active `block()` frames via `eval_unwind_blocks_for_return(...)` and returns to the caller,
- if `break_requested` or `continue_requested`, the current block returns control to the surrounding loop executor.

Practical consequence:
- flow-control flags are not consumed at the generic block level,
- they are consumed by the relevant structural executor (`foreach`, `while`) or by user-command cleanup logic.

## 5. Per-Node Execution

Each AST node is executed by:

```c
static Eval_Result eval_node(Evaluator_Context *ctx, const Node *node);
```

Current per-node sequence:

1. Reject null/stop-state execution.
2. Update `CMAKE_CURRENT_LIST_LINE` to the node's source line.
3. Mark the temp arena (`ctx->arena`).
4. Dispatch behavior by `node->kind`.
5. Rewind the temp arena mark after the node finishes.

This makes statement-local temporaries ephemeral by default.

### 5.1 Node Kind Dispatch

Current `Node_Kind` execution mapping:

- `NODE_COMMAND`
If the command name is non-empty, route to `eval_dispatch_command(...)`.

- `NODE_IF`
Execute through `eval_if(...)`.

- `NODE_FOREACH`
Execute through `eval_foreach(...)`.

- `NODE_WHILE`
Execute through `eval_while(...)`.

- `NODE_FUNCTION`
Register the user command through `eval_user_cmd_register(...)`.

- `NODE_MACRO`
Register the user command through `eval_user_cmd_register(...)`.

Important consequence:
- `function()` and `macro()` definitions are registration-time constructs at node execution time; their bodies are not executed when the definition node is encountered.

## 6. Command-Node Execution Path

For ordinary command nodes, the evaluator uses the dispatcher:

```c
eval_dispatch_command(ctx, node)
```

Current routing shape:
- Built-in commands are matched first.
- If no built-in matches, the evaluator looks for a user-defined function/macro.
- If a user command is found, the evaluator may emit a command-call event and invoke the stored body.
- If nothing matches, the unsupported-command policy produces a diagnostic and the command is behaviorally ignored.

This makes `NODE_COMMAND` the gateway for:
- built-in handlers,
- `block()` / `endblock()` / `return()` / `break()` / `continue()` handlers,
- user-defined command invocation,
- unknown-command fallback.

## 7. Structural Control Flow

### 7.1 `if`

`NODE_IF` is executed by `eval_if(...)`.

Current behavior:
- Evaluate the primary condition.
- Emit `FLOW_IF_EVAL`.
- If true:
  - emit branch-taken `"then"`
  - execute `then_block`
- Otherwise scan `elseif` clauses in order:
  - evaluate each clause condition
  - emit `FLOW_IF_EVAL` for each check
  - on first true clause:
    - emit branch-taken `"elseif"`
    - execute that clause block
- If no branch matched:
  - optionally emit branch-taken `"else"` when an else-block exists
  - execute `else_block`

Only one branch body is executed.

### 7.2 `foreach`

`NODE_FOREACH` is executed by `eval_foreach(...)`.

Current behavior:
- Resolve loop header args before iteration.
- Support the currently implemented forms:
  - plain item list
  - `RANGE`
  - `IN ITEMS`
  - `IN LISTS`
  - `IN ZIP_LISTS`
- Emit loop-begin before the first iteration.
- Increment `ctx->loop_depth`.
- For each item:
  - assign the loop variable in current scope
  - execute the loop body
  - react to flow flags:
    - `return_requested`: stop the loop immediately and bubble upward
    - `continue_requested`: clear the flag and advance to next iteration
    - `break_requested`: clear the flag and exit loop
- Emit loop-end with iteration count after normal/break exit.

Policy interaction:
- `CMP0124 NEW` restores or unsets the loop variable after the loop.

### 7.3 `while`

`NODE_WHILE` is executed by `eval_while(...)`.

Current behavior:
- Emit loop-begin before evaluation starts.
- Increment `ctx->loop_depth`.
- Re-evaluate the condition before each iteration.
- Emit `FLOW_IF_EVAL` for each condition check.
- On false condition:
  - emit loop-end
  - exit normally
- On true condition:
  - execute the body
  - react to flow flags similarly to `foreach`

Hard guard:
- iteration count is capped at `10000`
- exceeding that limit emits an evaluator error (`Iteration limit exceeded`) and returns at least `EVAL_RESULT_SOFT_ERROR` (or `EVAL_RESULT_FATAL` if stop-state is triggered)

This is an intentional evaluator safety divergence from unconstrained host execution.

## 8. Flow-Control Flags

The evaluator uses explicit flags on `Evaluator_Context`:
- `return_requested`
- `break_requested`
- `continue_requested`

### 8.1 `break` and `continue`

Handled by `eval_handle_break(...)` and `eval_handle_continue(...)`.

Current behavior:
- They are valid only inside an active loop (`ctx->loop_depth > 0`).
- Outside a loop they emit errors.
- On success they set the corresponding flag and emit flow events.

Consumption model:
- generic block traversal stops when it sees the flag,
- the surrounding loop executor clears and consumes the flag.

### 8.2 `return`

Handled by `eval_handle_return(...)`.

Current behavior:
- `return()` is rejected inside `macro()` execution context.
- Return state is cleared before parsing new return behavior.
- `CMP0140 NEW` enables the supported `PROPAGATE` form:
  - `return(PROPAGATE <var...>)`
- On success:
  - `return_requested` is set
  - `return_propagate_vars` may be populated

Block interaction:
- if propagation variables exist, active `block()` frames are marked `propagate_on_return`
- block unwinding then handles deferred propagation semantics

## 9. `block()` / `endblock()` in the Execution Model

Even though `block()` is parsed as an ordinary command node, it participates in structural execution via dispatcher handlers in `eval_flow.c`.

Current behavior:
- `block()` parses options and may push:
  - variable scope
  - policy scope
- It pushes a `Block_Frame`
- It emits `FLOW_BLOCK_BEGIN`

- `endblock()`:
  - requires a matching frame
  - pops the frame
  - emits `FLOW_BLOCK_END`

Return interaction:
- on `return_requested`, `eval_node_list(...)` calls `eval_unwind_blocks_for_return(...)`
- this drains pending block frames before the return bubbles further upward

Practical consequence:
- `block()` behaves like runtime structure even though it is not a distinct parser node kind.

## 10. User Command Lifecycle

### 10.1 Registration

When the evaluator encounters `NODE_FUNCTION` or `NODE_MACRO`, it calls:
- `eval_user_cmd_register(...)`

Current registration behavior:
- Copy the command name into `event_arena`
- Copy parameter names into persistent storage
- Deep-clone the function/macro body into persistent `event_arena` storage
- Append the resulting `User_Command` into `ctx->user_commands`

Lookup behavior:
- `eval_user_cmd_find(...)` scans from newest to oldest
- matching is case-insensitive

Practical consequence:
- later definitions shadow earlier ones

### 10.2 Invocation

User commands are executed by:

```c
bool eval_user_cmd_invoke(Evaluator_Context *ctx, String_View name, const SV_List *args, Cmake_Event_Origin origin);
```

Current behavior:
- Find the registered command
- Distinguish function vs macro semantics

For `function()`:
- push a new variable scope
- increment function depth
- set return context to function
- emit function-begin event

For `macro()`:
- push a macro frame (caller-scope style bindings)
- set return context to macro
- emit macro-begin event

Then the evaluator:
- binds declared parameters,
- populates implicit CMake-style arguments:
  - `ARGC`
  - `ARGV`
  - `ARGN`
  - `ARGV0`, `ARGV1`, ...
- executes the stored body through `eval_node_list(...)`

Cleanup behavior:
- captures whether a return happened
- applies `return(PROPAGATE ...)` to parent scope for functions when needed
- clears return state
- restores previous return context
- pops function scope or macro frame
- emits function-end / macro-end

Important consequence:
- `return()` inside a function is consumed as call control, not as a fatal execution error
- `return()` inside a macro is invalid and reported as an error

## 11. Argument Resolution as Execution Pre-Step

Command execution often begins by converting parser `Args` into evaluator-visible string lists.

Current resolution helpers:
- `eval_resolve_args(...)`
- `eval_resolve_args_literal(...)`

Current semantics:
- quoted args preserve semicolons and strip surrounding quotes
- bracket args preserve semicolons and strip bracket delimiters
- default unquoted args are expanded and then split on semicolons
- literal resolution (used for macro-like contexts) skips normal expansion/list splitting

This argument-resolution step is a major part of the evaluator execution model because handlers generally operate on already-resolved `SV_List` values, not raw parser tokens.

## 12. External File Execution

Nested file execution is handled by:

```c
Eval_Result eval_execute_file(Evaluator_Context *ctx,
                              String_View file_path,
                              bool is_add_subdirectory,
                              String_View explicit_bin_dir);
```

Current execution sequence:

1. Enter file-evaluation context.
2. Mark the temp arena.
3. Read the external source file.
4. Lex it into tokens.
5. Parse it into a new AST.
6. Push temporary file/list-dir context.
7. For `add_subdirectory`, optionally push a new variable scope and directory context.
8. Execute the nested AST through `eval_node_list(...)`.
9. Flush deferred work for the nested directory when needed.
10. Clear return state.
11. Restore previous context.
12. Rewind the temp arena.

Important consequence:
- the evaluator can recursively re-enter its own lex/parse/execute pipeline for external files,
- but it still uses the same evaluator context and event stream.

## 13. Deferred Work at Execution Boundaries

At top-level `evaluator_run(...)`, the evaluator performs additional execution-phase flushes after node traversal:
- `eval_defer_flush_current_directory(...)`
- `eval_file_generate_flush(...)`

This means some operational effects are intentionally deferred until after immediate AST traversal succeeds.

Practical consequence:
- “execution” in the evaluator is not only node-by-node dispatch,
- it also includes post-pass flushing of queued work.

## 14. Stop-State Interaction

The evaluator uses:

```c
bool eval_should_stop(Evaluator_Context *ctx);
```

Current stop state becomes true when:
- `ctx->oom` is set
- `ctx->stop_requested` is set

Most execution entry points bail out early when stop is active.

This makes execution cooperative:
- command handlers, structural executors, diagnostics, and nested evaluation all participate in the same stop-state protocol.

## 15. Current Non-Goals / Limits

Current limits visible in the execution model:

- `while()` is bounded by a hard iteration guard.
- `function()` and `macro()` definitions are stored eagerly rather than lazily reparsed on invocation.
- `block()` is modeled through command dispatch rather than a dedicated parser node kind.
- External-file execution reuses the same evaluator context rather than spawning isolated child contexts.
- Flow control is flag-based, not exception-based.

## 16. Relationship to Other Docs

- `evaluator_v2_spec.md`
Defines the broader canonical evaluator contract; this file is the execution-focused slice.

- `evaluator_runtime_model.md`
Should describe the persistent state and lifecycle that this execution model mutates.

- `evaluator_dispatch.md`
Should describe the command-routing layer used by command-node execution.

- `evaluator_variables_and_scope.md`
Should describe the scope and binding rules that are exercised by loops, blocks, functions, and macros.
