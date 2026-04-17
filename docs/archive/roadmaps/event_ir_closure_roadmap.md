# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Event IR Incremental Roadmap

Status: Canonical active roadmap for the `evaluator -> Event_Stream` boundary.

This document owns only the Event IR contract and its evaluator-side
projection. It is intentionally separate from the broader
`evaluator -> Event IR -> build_model -> codegen` closure program.

## 1. Why This Exists

The closure roadmap is intentionally end-to-end. That is useful for product
status, but it is a poor ownership shape for the Event IR itself.

When Event IR changes are planned only through downstream closure waves, even a
small new semantic surface tends to force coordinated edits in:

- evaluator emitters
- `event_ir.h` / `event_ir.c`
- build-model ingest and freeze
- codegen validation/runtime
- cross-layer proof harnesses

That coupling is too wide for a boundary contract that should evolve in small,
append-only, producer-owned steps.

This roadmap exists to restore an incremental delivery model similar to the
evaluator program:

- freeze one narrow Event IR tranche at a time
- prove it at the producer/boundary layer first
- let downstream adoption happen later in separate roadmaps

## 2. Goal

The goal of this roadmap is to make Event IR a product-complete, append-only,
typed, independently evolvable boundary for the supported CMake 3.28 subset.

Completion here means:

- every supported emitted surface has a typed Event IR representation
- event kinds, metadata, payload shapes, and copy semantics are frozen
- role ownership is explicit per kind
- event projection can be tested without depending on build-model or codegen
- downstream consumers adopt only already-frozen Event IR tranches

This roadmap does not attempt to make build-model or codegen consume every
tranche immediately.

## 3. Non-Goals

This roadmap does not own:

- build-model ingest design
- codegen runtime behavior
- supported-subset product claims for generated backend execution
- direct evaluator-private replay in generated `nob.c`
- consumer-driven shortcuts that bypass Event IR contract freeze

Those concerns remain downstream programs.

## 4. Frozen Decisions

The following decisions are frozen for this roadmap:

- the preserved boundary remains
  `AST -> evaluator -> Event IR -> build_model -> codegen`
- Event IR waves modify only:
  - evaluator-side projection
  - `src_v2/transpiler/event_ir.h`
  - `src_v2/transpiler/event_ir.c`
  - Event IR documentation
  - Event IR / evaluator tests
- downstream adoption is not required for an Event IR tranche to freeze
- downstream consumers may only rely on already-frozen Event IR tranches
- `Event_Family`, `Event_Kind`, and typed replay opcode enums remain
  append-only
- role ownership is expressed through `Event_Role` metadata, not by creating
  parallel shadow streams
- when a runtime effect becomes downstream-consumable, the default Event IR
  mechanism is a dual-role kind or replay bridge payload, not a second ad hoc
  event taxonomy
- each wave should be family-sized or smaller; if a wave needs coordinated
  edits across multiple downstream layers to be meaningful, it is too large
  for this roadmap

## 5. Delivery Model

Each Event IR wave follows the same sequence:

1. freeze the producer-facing payload shape and metadata
2. land evaluator projection for only that tranche
3. add producer/boundary tests
4. update the normative Event IR spec
5. optionally run downstream compatibility checks, but do not make them the
   owner of the wave

The key rule is:

- Event IR freezes first
- build-model adoption comes later
- codegen/runtime adoption comes later still

That keeps the boundary legible and prevents “small feature, wide blast
radius” planning.

## 6. Wave Plan

### IR0 Contract And Stream Mechanics

Goal:
- freeze the stream mechanics and metadata helpers before expanding family
  coverage

Deliverables:
- canonical `Event_Stream` ownership and deep-copy rules
- `event_kind_meta(...)`, role-mask helpers, labels, and default versions
- append-only enum policy for family/kind growth
- stream ordering, `seq`, and version-default behavior frozen

Non-goals:
- no new semantic families
- no downstream replay ownership

Exit criteria:
- stream mechanics and metadata lookup are no longer debated per-family
- future waves can add kinds without reopening base contract questions

Evidence:
- dedicated stream/metadata tests
- evaluator smoke proving optional event projection still works

### IR1 Trace And Structural Context

Goal:
- freeze the control/context shell around semantic events

Deliverables:
- trace kinds such as command/include/subdirectory begin/end
- directory, scope, policy, and variable mutation context events
- origin/source-location expectations for projected events

Non-goals:
- no downstream build ownership claims yet
- no replay bridge yet

Exit criteria:
- consumers can reconstruct execution context and nesting from frozen Event IR
- evaluator no longer needs family-local trace ad hoc conventions

Evidence:
- evaluator command-trace tests
- Event IR dump/snapshot coverage

### IR2 Structural Build Semantics

Goal:
- freeze the declaration/update events for normal build semantics before
  effect replay enters the picture

Deliverables:
- project/build-graph/target/test/install/export/package declaration and
  mutation events
- typed payloads for target usage requirements, source attachment, ownership
  context, and test registration

Non-goals:
- no runtime-effect replay yet
- no backend execution claims

Exit criteria:
- core structural semantics are represented directly in Event IR rather than
  inferred later from evaluator-private state

Evidence:
- evaluator semantic-event tests
- focused pipeline compatibility checks may run, but remain non-owning

### IR3 Deterministic Filesystem And Local Host Effects

Goal:
- freeze the deterministic effect-bearing families that are easiest to reason
  about locally

Deliverables:
- canonical `FS`-family payloads for deterministic text/file/directory effects
- typed local host-effect payloads where evaluator already resolves final
  paths and options deterministically
- explicit role metadata for effect-bearing kinds

Non-goals:
- no generic process replay
- no network/provider/VCS semantics

Exit criteria:
- deterministic local effects stop depending on informal evaluator behavior
- payload shapes are frozen before any broader downstream replay plan

Evidence:
- evaluator effect tests
- Event IR shape/dump coverage for the new kinds

### IR4 Replay Bridge Foundation

Goal:
- freeze the generic downstream replay bridge without yet coupling it to a
  broad backend-support program

Deliverables:
- canonical replay bridge kinds:
  `DECLARE`, `ADD_INPUT`, `ADD_OUTPUT`, `ADD_ARGV`, `ADD_ENV`
- typed replay opcode taxonomy owned by Event IR
- explicit replay phase metadata and working-directory ownership in declare
  payloads

Non-goals:
- no build-model execution semantics in this roadmap
- no codegen helper/runtime work in this roadmap

Exit criteria:
- Event IR can represent downstream-replayable actions canonically without
  inventing family-specific ad hoc handoff formats

Evidence:
- dedicated Event IR projection tests for replay actions
- spec freeze for opcode and payload invariants

### IR5 Probe, Process, And Test-Driver Ownership

Goal:
- make high-risk runtime-oriented families explicit in the Event IR contract
  even when positive backend support is deferred

Deliverables:
- typed ownership markers or replay opcodes for:
  - `try_compile`
  - `try_run`
  - local `ctest_*` driver flows
  - generic process families when they have observable owned effects
- stable reject-oriented payload ownership for variants that remain outside the
  supported backend

Non-goals:
- no promise that positive runtime support lands with the Event IR tranche
- no expansion to network/dashboard/script/process universes by default

Exit criteria:
- unsupported or deferred runtime families are still visible and typed in the
  boundary contract
- downstream programs no longer need to treat them as invisible backlog

Evidence:
- evaluator/Event IR tests for positive projection and reject ownership
- spec freeze for opcode naming and payload shape

### IR6 Dependency Materialization And Local Acquisition

Goal:
- freeze the Event IR ownership for narrow deterministic dependency
  materialization before downstream adoption

Deliverables:
- typed local dependency-materialization payloads
- local archive/source-dir ownership surfaces
- explicit boundaries for remote/provider/VCS/custom-command variants

Non-goals:
- no remote acquisition support
- no attempt to solve full dependency ecosystem semantics in one wave

Exit criteria:
- deterministic local dependency effects are first-class Event IR surfaces
- unsupported acquisition classes remain explicitly typed and documented

Evidence:
- evaluator/Event IR tests for local materialization projection
- spec updates for local-only boundary rules

### IR7 Hardening And Deprecation Control

Goal:
- keep the Event IR stable as the supported subset grows

Deliverables:
- compatibility checks for append-only growth
- stronger invariants around labels, versions, and debug dump behavior
- explicit deprecation policy for any legacy helper or compatibility shim

Non-goals:
- no new semantic family by default
- no downstream execution claims

Exit criteria:
- future Event IR growth no longer requires reopening ownership questions
- the spec, tests, and producer behavior stay aligned

Evidence:
- stream contract regression tests
- spec/implementation consistency checks

## 7. Relationship To Downstream Roadmaps

This roadmap freezes Event IR tranches.

It does not force immediate downstream adoption.

The intended handoff is:

- Event IR wave lands and freezes
- build-model roadmap decides whether and how to ingest that tranche
- codegen/closure roadmap decides whether and how to support that tranche in
  generated backend behavior

If a downstream roadmap needs a new Event IR surface, it should request a new
Event IR tranche instead of growing the boundary ad hoc inside a closure wave.

## 8. Evidence Gates

The minimum evidence gate for an Event IR wave is:

- evaluator tests proving projection behavior
- Event IR contract tests proving metadata, ordering, and ownership rules
- normative spec updates in `docs/transpiler/event_ir_v2_spec.md`

Optional compatibility checks:

- `test-pipeline`
- `test-build-model`

Those may run to catch accidental downstream breakage, but they are not the
owner of Event IR wave completion.

## 9. Canonical Documents

- Normative Event IR contract:
  [`event_ir_v2_spec.md`](./event_ir_v2_spec.md)
- Directory overview:
  [`README.md`](./README.md)
- Separate downstream closure program:
  [`../evaluator_codegen_closure_roadmap.md`](../evaluator_codegen_closure_roadmap.md)
