# Build Model Migration

## 1. Objective

Migrate the repository from the frozen legacy build model in
`src_obsolete/build_model/` to a canonical implementation under
`src_v2/build_model/` without letting the legacy API define the new design.

The migration is rewrite-first, not port-first:
- preserve good patterns
- preserve regression coverage
- reject the legacy public mutator surface and monolithic layout

## 2. Current State

As of March 8, 2026:
- canonical evaluator output is `Event_Stream`
- canonical build-model documentation lives in this directory
- operational pipeline tests still include legacy build-model headers
- `src_obsolete/build_model/` remains the functional baseline for behavior and
  regression comparison

## 3. Migration Batches

### Batch 0: Canonical Docs

Deliverables:
- add the new canonical docs in `docs/build_model/`
- mark `*_v2_spec.md` as historical/superseded
- keep historical benchmark notes for legacy-path measurement only

Acceptance:
- new docs are the default reference for implementation work
- historical docs are clearly labeled and linked from `README.md`

### Batch 1: Type System and Public Headers

Deliverables:
- create `src_v2/build_model/`
- add `build_model_types.h`
- add public headers for builder, validate, freeze, and query
- introduce typed IDs and `BM_Provenance`
- introduce the typed `Diag_Sink` dependency layer used by the build model

Acceptance:
- public headers compile without depending on legacy `build_model_core.h`
- no new public API depends on `Cmake_Event` aliases or `EV_*`

### Batch 2: Builder Core

Deliverables:
- implement `BM_Builder` lifecycle and dispatch
- implement directory/project/target domain handlers
- support the release-1 required event set for those domains

Acceptance:
- builder consumes only `EVENT_ROLE_BUILD_SEMANTIC`
- builder ignores trace/diagnostic events
- directory enter/leave semantics and target ownership are correct

### Batch 3: Remaining Release-1 Domains and Validation

Deliverables:
- implement tests/install/package/cpack handlers
- implement `bm_validate_draft(...)`
- add regression coverage for structural, resolution, cycle, and semantic
  failures

Acceptance:
- validation blocks freeze on errors
- explicit dependency resolution and CPack/install cross-links are checked

### Batch 4: Freeze and Query

Deliverables:
- implement `bm_freeze_draft(...)`
- implement the ID-based query layer
- add frozen-model indexes and string interning

Acceptance:
- frozen model is immutable
- query is sufficient for codegen-facing read access
- no codegen path requires direct struct-field access

### Batch 5: Compatibility Shims

Deliverables:
- add legacy-named wrappers only where tests or callers still require them
- route legacy names through the new implementation

Acceptance:
- old names are wrappers, not the primary contract
- no new feature work lands directly in `src_obsolete/build_model/`

### Batch 6: Pipeline Cutover

Deliverables:
- move pipeline tests to the canonical API
- remove direct build dependency on `src_obsolete/build_model/`
- keep `src_obsolete/build_model/` as archival reference until final deletion

Acceptance:
- the active test path no longer depends on legacy implementation files

## 4. Compatibility Rules

- During migration, legacy headers may forward to the new implementation.
- New implementation work must happen only in `src_v2/build_model/`.
- `src_obsolete/build_model/` may receive only migration-critical fixes until
  cutover is complete.
- The repository must never introduce a second-generation name like
  `build_model_v3`. The canonical implementation takes the plain `build_model`
  name in `src_v2`.

## 5. Regression Strategy

The legacy build model is the regression oracle, not the target architecture.

Required safeguards:
- keep focused pipeline tests for builder/freeze/query behavior
- add unit coverage for typed-ID lookups and provenance retention
- add side-by-side snapshot comparisons where the new and legacy models should
  agree for release-1 semantics

## 6. Done Condition

The migration is complete when:
- `src_v2/build_model/` provides builder, validate, freeze, and query
- the active pipeline uses the canonical API
- `src_obsolete/build_model/` is no longer required for normal builds or tests
- the documentation in this directory matches the implemented contract
