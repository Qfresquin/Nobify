# Build Model Architecture (Canonical)

## 1. Status

This document supersedes `build_model_architecture_v2.md`.

It defines the canonical architecture for the active build-model
implementation under `src_v2/build_model/`.

Project priority framing:
- the build model serves the canonical project direction in
  [`../project_priorities.md`](../project_priorities.md),
- its first job is to preserve reconstructed CMake 3.28 semantics faithfully,
- historical behavior is secondary unless it is required to preserve that
  observable baseline,
- downstream Nob optimization should consume this stable semantic model rather
  than bypass it.

## 2. System Boundary

The build model is not part of the evaluator.

Its role in the pipeline is semantic preservation first, optimization substrate
second.

Boundary rules:
- The evaluator emits `Event_Stream` only.
- The build model is intentionally decoupled from evaluator public API shape.
  It consumes the stream produced by `eval_session_run(...)` over
  `EvalSession` / `EvalExec_Request`, whether the producer is currently reached
  through legacy shims or the new session/request API.
- The build model consumes the canonical event stream from
  `src_v2/transpiler/event_ir.h`.
- The builder consumes only events whose kind has
  `EVENT_ROLE_BUILD_SEMANTIC`.
- Trace, diagnostic, runtime-effect, and evaluator-private state are not
  semantic inputs for reconstruction.

The canonical data flow is:

`Event_Stream -> BM_Builder -> Build_Model_Draft -> Validate -> Freeze -> Query`

## 3. Phase Responsibilities

### Builder

- Incremental and append-driven.
- The only writer of `Build_Model_Draft`.
- Owns transient indexes, directory stack state, and domain-specific append
  helpers.
- Rejects unsupported build-semantic events instead of silently dropping them.

### Validate

- Read-only over `Build_Model_Draft`.
- Performs ordered semantic passes:
  `structural -> resolution -> cycles -> semantic`.
- Emits diagnostics but does not mutate draft state.

### Freeze

- Consumes a validated draft.
- Produces an immutable `Build_Model`.
- Converts unresolved symbolic references into typed IDs.
- Builds compact arrays and lookup indexes.

### Query

- The only public read surface for codegen and tooling.
- Exposes raw and effective views without exposing internal storage layout.
- Operates on `Build_Model` only, never on builder-owned draft state.

## 4. Release-1 Domain Coverage

The first canonical implementation must cover:
- project metadata
- directory hierarchy and directory properties
- targets and central target properties
- tests
- install rules
- package results
- CPack install types, groups, and components

Out-of-scope semantics are not silently discarded. They are either:
- represented by typed future-facing raw property bags, or
- rejected with an explicit builder diagnostic if the event is build-semantic
  and no lossless representation exists yet

## 5. Directory Model

Directory semantics are first-class.

Required rules:
- `EVENT_DIRECTORY_ENTER` pushes a new active directory frame.
- `EVENT_DIRECTORY_LEAVE` pops the active directory frame.
- `EVENT_DIRECTORY_PROPERTY_MUTATE` mutates the active directory record.
- `EVENT_GLOBAL_PROPERTY_MUTATE` mutates global build-model state.
- Every target, test, install rule, package result, and CPack record stores
  `owner_directory_id` pointing at the active directory when it was declared.

The builder records raw directory mutations. Effective inherited directory
state is resolved later by query helpers, not by hidden evaluator state.

## 6. Ownership and Lifetime

Ownership rules are strict:
- `Event_Stream` owns its payload storage.
- `BM_Builder` owns all draft allocations in the builder arena.
- `Build_Model_Draft` remains valid while the builder arena lives.
- `Build_Model` owns its own frozen arena and never points back into the draft
  or input stream.

Callers may destroy the stream and builder arenas after a successful freeze.

## 7. Diagnostics and Failure Model

- The build model depends on a typed `Diag_Sink` interface. The legacy
  `void *diagnostics` contract is forbidden in the canonical architecture.
- `bm_builder_apply_event(...)` and `bm_builder_apply_stream(...)` return
  `false` once the builder enters fatal state.
- `bm_builder_finalize(...)` returns `NULL` after any fatal builder error.
- `bm_validate_draft(...)` returns `false` if any error-level issue is found.
- `bm_freeze_draft(...)` returns `NULL` on OOM or invariant failure.

Warnings do not block freeze. Errors do.

## 8. Rejected Legacy Patterns

The new architecture explicitly rejects:
- monolithic source files in the style of `build_model.c` or
  `build_model_builder.c`
- a public low-level mutator surface like `build_model_core.h`
- a single mutable `Build_Model` serving both draft and final roles
- direct dependence on transitional `Cmake_Event` aliases or `EV_*` names
- implicit dependency inference from string payloads

## 9. Planned Module Layout

The implementation should be split by responsibility under `src_v2/build_model/`:

- `build_model_types.h`
- `build_model_builder.h`
- `build_model_builder.c`
- `build_model_builder_directory.c`
- `build_model_builder_project.c`
- `build_model_builder_target.c`
- `build_model_builder_test.c`
- `build_model_builder_install.c`
- `build_model_builder_package.c`
- `build_model_validate.h`
- `build_model_validate.c`
- `build_model_validate_cycles.c`
- `build_model_freeze.h`
- `build_model_freeze.c`
- `build_model_query.h`
- `build_model_query.c`

This split is part of the contract, not an implementation detail left open.
