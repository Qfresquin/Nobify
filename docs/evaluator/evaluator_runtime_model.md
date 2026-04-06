# Evaluator Runtime Model

Status: Canonical Target. This document defines the target evaluator runtime
topology and ownership model.

## 1. Scope

This document covers:
- persistent vs transient evaluator state,
- session lifetime,
- execution-context stacking,
- arena and ownership boundaries,
- services and transaction ownership,
- multi-run behavior.

It does not restate detailed command semantics. Those belong in
[evaluator_execution_model.md](./evaluator_execution_model.md).

## 2. Runtime Topology

The target runtime is composed of four primary objects:
- `EvalSession`
- `EvalExecContext`
- `EvalRegistry`
- `EvalServices`

High-level relationship:

`EvalSession` owns durable semantic state.

`EvalExecContext` represents one active run or nested execution frame over that
session.

`EvalRegistry` owns native command definitions and capability metadata.

`EvalServices` exposes filesystem, process, environment, host, network, time,
and generator/toolchain capabilities.

## 3. Persistent Session State

`EvalSession` owns the semantic state that survives across runs.

Required persistent state includes:
- session compatibility settings,
- cache entries,
- policy baseline,
- directory graph,
- property engine,
- project / target / test / install / export / package models,
- user-defined function and macro definitions,
- session metadata and persistent reporting counters,
- references to registry and services.

The session is the canonical semantic source of truth.

Variables, emitted events, and diagnostic text are projections over this state,
not replacements for it.

## 4. Execution Contexts

An `EvalExecContext` is created for:
- top-level `eval_session_run(...)`,
- `include()`,
- `add_subdirectory()`,
- `function()`,
- `macro()`,
- `block()`,
- loop bodies,
- `try_compile()` children,
- `try_run()` children,
- deferred replay.

Each context carries:
- current source dir,
- current binary dir,
- current list file,
- frame-local variable overlays,
- flow-control state,
- origin stack,
- pending command transaction,
- frame-local diagnostics bookkeeping.

Child execution does not create a second semantic universe. It creates a new
context over the same session.

## 5. Arena and Ownership Model

The target runtime distinguishes persistent and scratch lifetimes.

`EvalSession_Config.persistent_arena` owns:
- session state,
- canonical models,
- persistent strings and identifiers,
- registry-owned command metadata when the registry is session-owned.

`EvalExec_Request.scratch_arena` owns:
- transient parsing results,
- temporary resolution data,
- short-lived request-local buffers,
- transaction-local scratch structures.

`Event_Stream` remains caller-owned. The evaluator deep-copies event payloads at
the stream boundary and never assumes ownership of the stream object itself.

The request model also owns execution gates that decide whether a committed
host effect:

- executes immediately through evaluator services
- is projected as a downstream-consumable replay action
- or does both where the documented compatibility mode requires immediate host
  behavior plus downstream visibility

These gates are request-level runtime configuration. They must not be inferred
later from arbitrary variables or from codegen-specific heuristics.

## 6. Transaction Ownership

Every command executes inside a transaction owned by the active execution
context.

The transaction owns:
- provisional semantic mutations,
- derived variable publications,
- provisional diagnostics,
- provisional event projections.

Only committed transactions may update canonical session state or emit final
projections.

This is the runtime mechanism that prevents half-applied semantic state after a
validation or execution failure.

## 7. Services Boundary

The runtime must not hide backend side effects inside arbitrary handlers.

All external effects flow through `EvalServices`, including:
- filesystem reads and writes,
- subprocess launch and capture,
- environment lookup and mutation overlays,
- host/system introspection,
- network access,
- time/clock reads,
- toolchain and generator capability queries.

Service access is session-scoped and visible to all child execution contexts.

When a family participates in the closure program, the runtime must support an
explicit request-level gate between:

- executing the effect on the host during evaluator execution
- projecting the effect as downstream replay input

This follows the same architectural pattern already used for gated export host
effects: the evaluator owns the immediate-service boundary, while downstream
replay ownership begins only after projection through canonical contracts.

## 8. Multi-Run Semantics

The session is expected to support multiple runs over time.

This means the runtime must keep separate:
- persistent semantic state from prior committed runs,
- the active execution stack for the current run,
- request-local scratch storage,
- deferred work that belongs to the current frame or directory,
- per-run report state returned in `EvalRunResult`.

`EvalRunResult.report` is per-run. Session state is not reset just because a
run boundary completed.

## 9. Relationship to Current Implementation

The current implementation may continue to centralize behavior in
`Evaluator_Context` during migration, but that is not the target runtime
topology.

Implementation-current descriptions belong in audit documents, not here.
