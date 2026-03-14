# Evaluator Execution Model

Status: Canonical Target. This document defines the target command and AST
execution pipeline.

## 1. Scope

This document covers:
- top-level run entry,
- per-node execution,
- command pipeline stages,
- flow-control propagation,
- nested execution,
- transaction commit and failure semantics.

It complements the runtime topology in
[evaluator_runtime_model.md](./evaluator_runtime_model.md).

## 2. Top-Level Run

`eval_session_run(...)` performs the following high-level sequence:

1. validate the session and request
2. create a root `EvalExecContext`
3. seed frame-local projections required for the requested directory/list-file
4. traverse the AST
5. flush run-end deferred work that belongs to the root context
6. finalize diagnostics and event projection counts
7. return `EvalRunResult`

The run result summarizes the execution even when no `Event_Stream` is
provided.

## 3. Node Categories

The evaluator recognizes two broad node classes:
- structural nodes
- command nodes

Structural nodes are handled by dedicated control-flow executors:
- conditionals,
- loops,
- function and macro definitions,
- block-like scoping constructs.

Command nodes enter the registry/session command pipeline described below.

## 4. Canonical Command Pipeline

Every command must execute through the same pipeline:

1. resolve command name
2. parse raw arguments into a typed request
3. validate request shape and execution-mode legality
4. resolve semantic references against session state and services
5. apply mutations into a transaction-local mutation log
6. commit the mutation log into canonical session state
7. project variables, diagnostics, and Event IR from committed state

Architectural consequences:
- handlers are not the public boundary,
- parse and validation are separate from mutation,
- projected variables are derived outputs,
- failed validation cannot partially mutate the session,
- failed application cannot emit semantic events as if the command succeeded.

## 5. Flow-Control Semantics

Flow control belongs to execution contexts, not to the session as a whole.

Each execution context may carry:
- normal execution,
- `return`,
- `break`,
- `continue`,
- fatal stop.

Propagation rules:
- `break` and `continue` are consumed by the nearest valid loop frame,
- `return` exits the current function or the current file-mode boundary where
  CMake permits it,
- fatal stop aborts the remaining active pipeline for the run.

Flow-control effects are not encoded as hidden global booleans in the target
architecture.

## 6. Nested Execution

Nested execution creates child contexts over the same session.

Required child-context use cases:
- `include()`
- `add_subdirectory()`
- user-defined `function()`
- `macro()` argument overlays
- `block()`
- deferred replay
- `try_compile()` and `try_run()` child execution boundaries

Nested execution must preserve:
- access to the same canonical session state,
- well-defined scope overlays,
- correct directory and origin tracking,
- isolated transaction ownership per active frame.

## 7. Deferred Work

Deferred work is scheduled against explicit runtime owners:
- frame-local defers,
- directory-local defers,
- run-end defers when the root context owns them.

Deferred work re-enters the execution model through a child execution context.
It does not mutate session state out of band.

## 8. Stop Model

The target execution model recognizes three broad result classes:
- clean success,
- soft-error completion,
- fatal stop.

Typical fatal-stop causes include:
- out-of-memory,
- unrecoverable service failure,
- explicit fatal evaluator stop,
- invariant violation that invalidates continued execution.

Soft errors are still surfaced in diagnostics and run reporting, but they do
not implicitly convert the session into a permanently stopped object.

## 9. Relationship to Command Coverage Work

Feature implementation should extend this pipeline, not bypass it.

New command work is expected to land as:
- typed request parsing,
- validation rules,
- model mutations,
- projection rules,
- tests against the committed result.
