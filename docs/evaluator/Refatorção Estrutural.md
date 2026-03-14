# Evaluator Structural Refactor Plan

Status: Transition Plan. This document defines how the repository migrates from
the current evaluator implementation to the target architecture documented in
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

## 2. Starting Point

The current implementation is still centered on `Evaluator_Context` and a
handler-heavy execution model.

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

### Phase B: Public API Split

Deliverables:
- introduce `EvalSession`
- introduce `EvalSession_Config`
- introduce `EvalExec_Request`
- introduce `EvalRunResult`
- introduce `EvalRegistry` as the primary native extension boundary

Exit criteria:
- session/request APIs exist
- legacy create/run APIs are either removed or documented as compatibility
  shims

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

## 5. Temporary Compatibility Policy

During migration, temporary adapters are allowed:
- legacy API shims
- transitional data projection helpers
- compatibility wrappers around old handler entry points

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
