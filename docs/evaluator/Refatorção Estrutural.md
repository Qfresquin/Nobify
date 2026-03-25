# Evaluator Structural Refactor Plan

Status: Landed. This document now serves as the closure record for the
repository migration onto the target architecture documented in
[evaluator_v2_spec.md](./evaluator_v2_spec.md) and
[evaluator_architecture_target.md](./evaluator_architecture_target.md).

## 1. Goal

The goal of this refactor is to make continued CMake 3.28 feature work land on
an architecture that is:
- state-correct,
- testable,
- transactional,
- coherent across directory/property/target/install/export features,
- explicit about public API ownership and backend boundaries.

This plan does not redefine the target architecture. It only describes how to
reach it from the current codebase.

## 2. Historical Starting Point

The migration started from an implementation centered on `Evaluator_Context`
and a handler-heavy execution model.

Known migration pressure points:
- semantic state and variable projections are too tightly coupled,
- cross-directory state is not modeled richly enough,
- property and capability logic are still partly scattered,
- nested execution boundaries are weaker than the target model requires,
- diagnostics and Event IR projection still reflect current implementation
  structure more than target transaction boundaries.

## 3. Migration Principles

The migration is allowed to make intentional architecture-breaking changes.

Explicitly allowed changes:
- replacing `Evaluator_Context` as the canonical public boundary
- introducing `EvalSession` and `EvalExecContext`
- introducing child execution contexts as first-class runtime objects
- moving from shared mutable handler state to typed canonical models
- changing extension APIs so registry ownership is explicit

The only stable downstream boundary that must remain intact throughout the
migration is:

`Event_Stream -> build_model`

## 4. Migration Phases

### Phase A: Canonical Docs and Target Naming

Deliverables:
- make `evaluator_v2_spec.md` the target contract
- add and stabilize `evaluator_architecture_target.md`
- reclassify implementation-current evaluator docs as audits where needed

Exit criteria:
- no evaluator architecture doc still treats `Evaluator_Context` as the target
  public boundary
- Event IR and build-model docs reference the new evaluator boundary

Implementation notes (March 14, 2026):
- `docs/evaluator/README.md` maps canonical target docs vs implementation
  audits
- Event IR and build-model docs now name the session/request boundary
  explicitly when they refer back to evaluator APIs

### Phase B: Public API Split

Deliverables:
- introduce `EvalSession`
- introduce `EvalSession_Config`
- introduce `EvalExec_Request`
- introduce `EvalRunResult`
- introduce `EvalRegistry` as the primary native extension boundary

Exit criteria:
- session/request APIs exist
- legacy create/run APIs are removed from the public boundary

### Phase C: Runtime Topology Split

Deliverables:
- separate persistent session state from transient execution state
- formalize child execution contexts
- move current ad hoc nested execution state into explicit frame objects

Exit criteria:
- includes, subdirectories, functions, macros, blocks, and deferred replay use
  explicit execution contexts

### Phase D: Canonical Semantic Models

Deliverables:
- introduce `DirectoryGraph`
- introduce unified `PropertyEngine`
- move targets/tests/install/export/package state into typed models

Exit criteria:
- cross-directory and property queries resolve against canonical models rather
  than incidental variable state

### Phase E: Service Boundary and Transactions

Deliverables:
- formalize `EvalServices`
- route filesystem/process/environment/host side effects through services
- add transaction-local mutation logs for commands

Exit criteria:
- command failures no longer leave half-committed semantic state
- Event IR and diagnostics projection are commit-based

### Phase F: Handler Migration and Coverage Work

Deliverables:
- move command families to typed request parsing and canonical mutations
- continue closing CMake 3.28 coverage gaps on top of the new model

Exit criteria:
- new feature work lands on the target pipeline by default
- remaining audits measure semantic coverage rather than architectural drift

Snapshot status (March 19, 2026):
- The public shim based on `Evaluator_Context` is gone from `src_v2/evaluator/evaluator.h`.
- `EvalNativeCommandDef` is now the only public native-command definition type.
- Native handlers now receive `EvalExecContext *`, and public native extension points are session/registry based.
- `EvalSession` now owns canonical persistent state directly through `EvalSessionState`; it no longer stores a hidden persisted execution context.
- `eval_session_create(...)` initializes canonical session state directly and no longer bootstraps it through a temporary `Event_Stream`.
- `eval_session_run(...)` instantiates a fresh per-run `EvalExecContext`, loads the current `EvalSessionState`, and commits only canonical persistent state back into `EvalSession` at the end of the run.
- Flow control (`break` / `continue` / `return`) is modeled on execution frames instead of global run booleans.
- Registry mutation is blocked during `eval_session_run(...)`.
- `test_v2/evaluator`, `test_v2/pipeline`, `test_v2/codegen`, and `src_v2/app` no longer include evaluator internals and now use the public session/request API.
- `docs/evaluator/evaluator_coverage_matrix.md` reports `107` `FULL`, `28` `PARTIAL`, `0` `MISSING`, `0` native-tag divergences, and no remaining `artifact-critical` partial rows in this snapshot.
- The remaining evaluator gaps are semantic coverage limits on typed request/canonical execution paths, not architectural drift from the target pipeline.

## 5. Temporary Compatibility Policy

During migration, temporary adapters are allowed:
- transitional data projection helpers
- compatibility wrappers around old handler entry points

Current status:
- public legacy API shims have been removed
- compatibility wrappers that remain are behavior-level CMake compatibility
  helpers, not architecture shims for `Evaluator_Context`

These adapters are acceptable only if:
- the target ownership model remains clear in docs,
- the code path does not redefine the target architecture,
- the adapter has a clear removal path.

## 6. Acceptance Criteria

The refactor is considered structurally successful when an implementor can add
new evaluator features by extending:
- typed request parsing,
- canonical semantic models,
- compatibility-aware validation,
- transaction commit logic,
- projection rules for variables, diagnostics, and Event IR

without having to reinterpret the architecture for each new command family.

This acceptance bar is satisfied in the current workspace snapshot.

Textual closure checks for this wave:
- no `persisted_exec`
- no `run_active`
- no `Evaluator_Native_Command_Def`
- no public `Evaluator_Context`
