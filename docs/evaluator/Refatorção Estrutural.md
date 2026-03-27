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

Snapshot status (March 25, 2026):
- The public shim based on `Evaluator_Context` is gone from `src_v2/evaluator/evaluator.h`.
- `EvalNativeCommandDef` is now the only public native-command definition type.
- Native handlers now receive `EvalExecContext *`, and public native extension points are session/registry based.
- `EvalSession` now owns canonical persistent state directly through `EvalSessionState`; it no longer stores a hidden persisted execution context.
- `eval_session_create(...)` initializes canonical session state directly and no longer bootstraps it through a temporary `Event_Stream`.
- `eval_session_run(...)` instantiates a fresh per-run `EvalExecContext`, loads the current `EvalSessionState`, and commits only canonical persistent state back into `EvalSession` at the end of the run.
- Canonical artifact records and typed `ctest_*` step records now live inside `EvalSessionState`, so `ctest_submit` and `cmake_file_api` no longer need `NOBIFY_CTEST::*` variables as their source of truth.
- Flow control (`break` / `continue` / `return`) is modeled on execution frames instead of global run booleans.
- Registry mutation is blocked during `eval_session_run(...)`.
- `test_v2/evaluator`, `test_v2/pipeline`, `test_v2/codegen`, and `src_v2/app` no longer include evaluator internals and now use the public session/request API.
- `docs/evaluator/evaluator_coverage_matrix.md` reports `108` `FULL`, `27` `PARTIAL`, `0` `MISSING`, `0` native-tag divergences, and no remaining `artifact-critical` partial rows in this snapshot.
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

## 7. Post-Landed CTest Completion Waves

These waves do not reopen the landed architecture documented in
[evaluator_v2_spec.md](./evaluator_v2_spec.md) and
[evaluator_architecture_target.md](./evaluator_architecture_target.md).

Guardrails for all waves below:
- no public API changes beyond the landed `EvalSession` /
  `EvalExec_Request` / `EvalRunResult` boundary
- no reintroduction of `Evaluator_Context` as a public ownership boundary
- every implementation wave must follow the canonical pipeline:
  typed parse -> validation -> semantic resolution -> draft/mutation log ->
  commit -> variable/diagnostic/Event IR projection
- no `ctest_*` command may be upgraded to `FULL` in
  `evaluator_coverage_matrix.md` unless the delivered semantics match the
  documented CMake 3.28 surface actually implemented
- these waves extend internal evaluator implementation only; they do not
  authorize target-architecture changes by implication

Point-in-time `ctest_*` status for this follow-up plan:
- already `FULL`: `ctest_configure`, `ctest_empty_binary_directory`,
  `ctest_read_custom_files`, `ctest_run_script`, `ctest_sleep`,
  `ctest_start`, `ctest_submit`, `ctest_upload`, `ctest_build`,
  `ctest_coverage`, `ctest_memcheck`, `ctest_test`, `ctest_update`
- still `PARTIAL`: none

### Wave C0: Landed Foundation

Status:
- completed; prerequisite for all remaining `ctest_*` completion work

Delivered structural base:
- canonical artifact records now live in `EvalSessionState`
- typed `ctest_*` step records now live in `EvalSessionState`
- `ctest_submit` already consumes committed step/artifact state as its source
  of truth instead of relying on `NOBIFY_CTEST::*` variables

Implication for later waves:
- remaining work is semantic completion on top of the landed
  `CanonicalArtifactStore` / `CtestStepStore` model, not a second architecture
  migration

### Wave C1: Finish `ctest_coverage`

Status:
- completed (March 26, 2026)

Delivered semantics:
- `LABELS` now changes the staged `Coverage.xml` payload by filtering the
  committed coverage-source view against canonical source-property label state
- the evaluator-local coverage reporting path now preserves the coverage
  summary line for both normal and `QUIET` execution modes without changing
  staged artifacts or later submit resolution
- `evaluator_coverage_matrix.md` now upgrades `ctest_coverage` to `FULL`

Target:
- raise only `ctest_coverage` from `PARTIAL` to `FULL`

Why this wave is first:
- `ctest_coverage` already executes a real process-backed coverage step
- `Coverage.xml` and the coverage manifest are already staged as canonical
  committed artifacts
- `ctest_submit(PARTS Coverage)` already reuses those committed artifacts

Allowed implementation scope:
- finish the remaining command-local semantics without introducing a new public
  subsystem
- complete effective `LABELS` filtering so it affects the staged coverage
  result, not just metadata projection
- align `QUIET` behavior with the documented local evaluator/reporting path

Explicit non-goals:
- no generic new step runner for unrelated `ctest_*` commands in this wave
- no public API changes

Exit criteria:
- the documented coverage tool still executes through evaluator services
- `Coverage.xml` and the manifest remain canonical committed artifacts
- `LABELS` affects the staged coverage output rather than only projected
  metadata
- `ctest_submit(PARTS Coverage)` still resolves files from committed artifacts
- `evaluator_coverage_matrix.md` upgrades `ctest_coverage` to `FULL`

### Wave C2: Shared Internal CTest Step Runtime

Status:
- completed (March 26, 2026)

Delivered semantics:
- `ctest_build`, `ctest_test`, and `ctest_update` now use a shared internal
  CTest step runtime request/commit path instead of the old
  `ctest_handle_modeled_step(...)` shortcut
- `ctest_memcheck` now reuses the same internal runtime core for shared
  parse/context/commit behavior while keeping its command-local option
  validation
- default `ctest_submit()` part resolution continues to come from committed
  canonical step records rather than projected `NOBIFY_CTEST::*` metadata

Target:
- extract the shared internal runtime needed by the remaining operational
  `ctest_*` steps without upgrading any command to `FULL` by itself

Internal responsibilities to make explicit:
- `Ctest step core`: defaults, session/tag context, success/failure commit path
- `Ctest runner normalization`: normalize external execution into canonical step
  results
- `Ctest artifact staging`: stage step XML/manifests before commit
- `Ctest submit integration`: resolve `PARTS`/files from committed step records

Explicit prohibitions:
- no new public API surface
- do not move semantic source of truth back to `NOBIFY_CTEST::*` variables
- do not use Event IR as evaluator state storage

Exit criteria:
- `ctest_build`, `ctest_update`, `ctest_test`, and `ctest_memcheck` no longer
  depend only on `ctest_handle_modeled_step(...)`
- the shared runtime is documented and implemented as an internal evaluator
  subsystem over the landed canonical stores

### Wave C3: Full `ctest_update`

Status:
- completed (March 26, 2026)

Delivered semantics:
- `ctest_update` now resolves source/build session context against the landed
  canonical CTest state instead of stopping at a modeled-only step record
- the update step now resolves documented VCS/update command settings,
  executes through evaluator process services, and materializes the documented
  `RETURN_VALUE` / `CAPTURE_CMAKE_ERROR` outcomes
- `Update.xml` plus `UpdateManifest.txt` are now staged as canonical artifacts,
  so `ctest_submit(PARTS Update)` reuses committed update payloads rather than
  projected metadata alone

Target:
- raise `ctest_update` from `PARTIAL` to `FULL`

Dependency:
- requires Wave C2

Required semantic surface:
- resolve the documented source/update context against canonical session state
- execute the update step through the evaluator service/backend path
- materialize the documented `RETURN_VALUE` and `CAPTURE_CMAKE_ERROR` outcomes
- stage and commit `Update.xml` plus a manifest for
  `ctest_submit(PARTS Update)`

Explicit non-goals:
- no public-boundary changes

Exit criteria:
- `ctest_update` no longer reports only `MODELED`
- `ctest_submit(PARTS Update)` resolves committed update artifacts
- `evaluator_coverage_matrix.md` upgrades `ctest_update` to `FULL`

### Wave C4: Full `ctest_build`

Delivered semantics:
- `ctest_build` now resolves the canonical session build context plus the
  documented effective build command instead of stopping at modeled-only
  metadata
- the build step now executes through evaluator process services and
  materializes canonical `NUMBER_ERRORS`, `NUMBER_WARNINGS`, `RETURN_VALUE`,
  and `CAPTURE_CMAKE_ERROR` from the executed result
- `Build.xml` plus `BuildManifest.txt` are now committed canonical artifacts,
  so `ctest_submit(PARTS Build)` reuses staged build payloads rather than
  projected metadata alone

Target:
- raise `ctest_build` from `PARTIAL` to `FULL`

Dependency:
- requires Wave C2

Required semantic surface:
- resolve the documented build context and build command
- execute the build step rather than publishing metadata only
- materialize canonical `NUMBER_ERRORS`, `NUMBER_WARNINGS`, `RETURN_VALUE`, and
  `CAPTURE_CMAKE_ERROR` from the executed result
- stage and commit `Build.xml` plus a manifest for
  `ctest_submit(PARTS Build)`

Exit criteria:
- `ctest_build` produces a committed canonical build artifact
- published counters come from executed build results
- `evaluator_coverage_matrix.md` upgrades `ctest_build` to `FULL`

### Wave C5: Full `ctest_test`

Delivered semantics:
- `ctest_test()` now resolves its runnable test plan from the canonical
  evaluator/session test model instead of committing a modeled-only step
- the step executes planned tests through evaluator process services and
  materializes canonical pass/fail counts plus `RETURN_VALUE` /
  `CAPTURE_CMAKE_ERROR`
- `Test.xml` and `TestManifest.txt` are staged under `Testing/<tag>` and
  committed for later submit reuse
- `OUTPUT_JUNIT` is preserved as a side output of the committed step without
  replacing the canonical `Test.xml` artifact
- `ctest_submit(PARTS Test)` now reuses the committed `Test.xml` artifact set
  instead of projected metadata
- focused evaluator SAN coverage now validates execution, JUnit side output,
  and submit reuse
Target:
- raise `ctest_test` from `PARTIAL` to `FULL`

Dependency:
- requires Wave C2

Required semantic surface:
- resolve the test plan against the canonical session test model
- execute the test step with the documented filters/options already accepted by
  the evaluator surface
- commit the canonical step result instead of a metadata-only record
- stage and commit `Test.xml`
- preserve `OUTPUT_JUNIT` as an output of the committed test step without
  treating it as the source of truth
- integrate the committed test artifact with `ctest_submit(PARTS Test)`

Exit criteria:
- `ctest_test` is no longer a modeled-only step
- `Test.xml` and test-step metadata are committed canonical artifacts
- `evaluator_coverage_matrix.md` upgrades `ctest_test` to `FULL`

### Wave C6: Full `ctest_memcheck`

Status:
- completed (March 27, 2026)

Delivered semantics:
- `ctest_memcheck()` now resolves backend type, command, options, sanitizer
  options, suppressions, and the canonical session/build test-plan context
  instead of stopping at modeled step metadata
- the step executes selected tests through the configured memcheck backend on
  top of the shared `ctest_test()` runtime path and extracts real defect counts
  from executed output
- `DEFECT_COUNT`, `RETURN_VALUE`, and `CAPTURE_CMAKE_ERROR` now come from the
  executed memcheck results rather than placeholders
- `MemCheck.xml` plus `MemCheckManifest.txt` are committed canonical artifacts,
  so `ctest_submit(PARTS MemCheck)` reuses staged memcheck payloads and
  `OUTPUT_JUNIT` remains only a side output

Target:
- raise `ctest_memcheck` from `PARTIAL` to `FULL`

Dependencies:
- requires Waves C2 and C5

Required semantic surface:
- resolve the memcheck backend and runtime configuration
- execute memcheck on top of the canonical `ctest_test` plan/runtime path
- extract real defect counts from executed results
- materialize `DEFECT_COUNT`, `RETURN_VALUE`, and `CAPTURE_CMAKE_ERROR` from
  execution, not placeholders
- stage and commit `MemCheck.xml` plus a manifest for
  `ctest_submit(PARTS MemCheck)`

Explicit requirement:
- `ctest_memcheck` must not be upgraded to `FULL` from parse/validation/metadata
  work alone

Exit criteria:
- `MemCheck.xml` exists as a committed canonical artifact
- `DEFECT_COUNT` reflects real memcheck execution results
- `evaluator_coverage_matrix.md` upgrades `ctest_memcheck` to `FULL`

### Wave Closure

Completion rule for the `ctest_*` family:
- the family is not fully closed until the five remaining `PARTIAL` rows above
  are removed from `evaluator_coverage_matrix.md`

Architecture escalation rule:
- if any later wave discovers a real blocker in the public target architecture,
  the implementer must update `evaluator_architecture_target.md` and
  `evaluator_v2_spec.md` first; this document does not authorize such a change
  on its own
