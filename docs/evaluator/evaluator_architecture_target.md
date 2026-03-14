# Evaluator Architecture Target

Status: Canonical Target. This document is the source of truth for the
post-refactor evaluator architecture, independent of the current
`src_v2/evaluator` implementation.

## 1. Purpose

The evaluator target architecture exists to make continued CMake 3.28 feature
implementation predictable, composable, and state-correct.

The intended boundary remains:

`Ast_Root -> evaluator -> Event_Stream`

But the evaluator must no longer be modeled as one context-centric mutable
object with handlers directly mutating shared internals. The target design is a
semantic runtime composed of:
- one persistent session,
- stacked execution contexts,
- typed semantic state models,
- injected external services,
- a command registry,
- transactional command application,
- Event IR and report projection after committed state changes.

## 2. Primary Runtime Objects

### 2.1 `EvalSession`

`EvalSession` is the public persistent runtime owner.

It owns:
- evaluator-wide compatibility settings,
- the canonical cache model,
- the directory graph,
- typed project / target / test / install / export / package models,
- user-defined function and macro definitions,
- the runtime command namespace,
- the property engine and synthetic property providers,
- persistent diagnostics/reporting counters and session metadata,
- references to injected services.

It does not represent one active call frame or one AST traversal.

### 2.2 `EvalExecContext`

`EvalExecContext` is the transient execution object created for a run or nested
evaluation boundary.

Each context owns:
- the current source/binary/list-file view,
- frame-local variable overlays,
- frame-local policy visibility,
- flow-control state,
- origin tracking,
- pending deferred work for that frame,
- the active command transaction.

Contexts are stacked for:
- top-level execution,
- `include()`,
- `add_subdirectory()`,
- `function()`,
- `macro()`,
- `block()`,
- loops,
- `try_compile()` children,
- `try_run()` children,
- deferred replay.

Child execution uses new contexts over the same session. It does not create a
second semantic universe.

### 2.3 `EvalRegistry`

`EvalRegistry` owns native command metadata and handlers.

It is responsible for:
- built-in command registration,
- externally registered native commands,
- capability metadata,
- normalized lookup,
- runtime command dispatch entries for native commands.

Scripted commands are session state, not registry state.

### 2.4 `EvalServices`

`EvalServices` is the explicit backend boundary. It groups:
- filesystem access,
- process execution,
- environment access,
- host/system introspection,
- network access,
- time/clock access,
- toolchain and generator capabilities.

No command handler may open-code backend behavior outside these service
interfaces.

## 3. Canonical State Models

### 3.1 `DirectoryGraph`

Directory semantics are first-class and persistent.

Each processed directory node stores:
- normalized source and binary paths,
- parent directory linkage,
- the directory snapshot visible at entry,
- objects declared in that directory,
- directory property mutations,
- effective policy baseline,
- deferred queues and directory-local metadata.

Cross-directory queries must resolve against this graph, not against the
current frame's temporary view.

### 3.2 `PropertyEngine`

The property layer is unified and typed.

Every property query resolves through exactly one engine that combines:
- stored properties,
- inherited properties,
- synthetic properties,
- scope validation and object lookup.

Synthetic properties such as `COMMANDS`, `CACHE_VARIABLES`, `COMPONENTS`,
`IN_TRY_COMPILE`, `CMAKE_ROLE`, and generator state are providers inside this
engine, not scattered special cases in unrelated handlers.

### 3.3 Typed Object Models

Targets, tests, installs, exports, packages, and related runtime metadata are
modeled as typed session state, not as incidental variable strings.

Required canonical models:
- `ProjectModel`
- `DirectoryGraph`
- `TargetModel`
- `TestModel`
- `InstallModel`
- `ExportModel`
- `PackageModel`
- `FetchContentModel`

Variables remain important, but they are projections over these models.

## 4. Variable and Scope Rules

Variables are no longer the primary source of truth for evaluator semantics.

Target design:
- normal variables live in execution-frame overlays,
- cache variables live in the session cache store,
- environment access is service-backed,
- CMake compatibility variables are projections from typed session state,
- `PROJECT_*`, `CMAKE_*`, target-derived metadata, and synthetic query results
  are published from canonical models instead of hand-maintained shadow state.

Function and macro behavior must still differ:
- functions create a new variable frame,
- macros create a textual/argument overlay without a new normal variable scope.

## 5. Command Pipeline

Every command must execute through the same architectural pipeline:

1. resolve command name through the registry/session namespace
2. parse raw AST arguments into a typed request
3. validate request shape and execution-mode legality
4. resolve semantic references against session state and services
5. apply mutations into a transaction-local mutation log
6. commit mutations into canonical session state
7. project variable updates, diagnostics, and Event IR from committed changes

Consequences:
- handlers stop mutating arbitrary session internals directly,
- Event IR is emitted from committed state changes,
- failed commands do not leave half-applied semantic state behind,
- feature work can extend parsers and models independently.

## 6. Diagnostics and Event Projection

Diagnostics are produced through a diagnostics service that consumes:
- stable evaluator diagnostic codes,
- compatibility policy,
- execution origin,
- command context,
- post-validation / post-apply failures.

Event IR is projected from committed semantic mutations and explicit trace
boundaries. Event emission is optional per run, but `Event_Stream` remains the
canonical downstream contract when a sink is provided.

`EvalRunResult.report` is the canonical execution summary. It is not derived
later by querying hidden mutable context fields.

## 7. Ownership and Lifetime

The target ownership model is:
- session-owned persistent arena/state for canonical semantic data,
- request/run scratch arena for temporary parsing and resolution work,
- caller-owned `Event_Stream` when event capture is requested,
- service-owned external side effects.

The session must support multiple runs over time without confusing:
- persistent semantic state,
- current execution frame state,
- transaction-local mutations.

## 8. Public API Consequences

The public architecture is intentionally not centered on `Evaluator_Context`.

The target public surface is session/request based:
- `EvalSession`
- `EvalSession_Config`
- `EvalExec_Request`
- `EvalRunResult`
- `EvalServices`
- `EvalRegistry`

The current `Evaluator_Context` API is not the target contract and may be
deleted once the implementation migration reaches parity with this document.

## 9. Non-Goals

The target architecture still does not move build-model reconstruction into the
evaluator. `Event_Stream` remains the downstream contract consumed by
`docs/build_model/`.

The target architecture also does not permit ad hoc fallback to evaluator-local
string bags as a substitute for typed state when a real semantic model is
required.

## 10. Relationship to Other Docs

- `evaluator_v2_spec.md`
  Top-level API and boundary contract.

- `evaluator_runtime_model.md`
  Session/context lifetime and state ownership.

- `evaluator_execution_model.md`
  Execution pipeline and frame behavior.

- `evaluator_variables_and_scope.md`
  Variable projection and scope overlay rules.

- `Refatorção Estrutural.md`
  Migration plan from the current implementation to this target architecture.
