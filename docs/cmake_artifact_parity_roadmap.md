# CMake Artifact Parity Roadmap

Status: Canonical active roadmap. This document defines the incremental path
from the current v2 semantic pipeline to generated Nob build programs that
produce artifacts equivalent to CMake 3.28.

This roadmap does not replace subsystem contracts under `docs/evaluator/`,
`docs/build_model/`, `docs/transpiler/`, or `docs/tests/`. It coordinates the
cross-layer work required to turn those contracts into trustworthy final
artifact parity.

## 1. Status

Nobify is past the point where evaluator-only progress is the main bottleneck.
The core semantic recovery stack is now far enough along that the remaining
risk is concentrated in downstream reconstruction, backend behavior, and proof
quality.

This roadmap is active because the project goal is not "semantic recovery in
isolation". The goal is generated Nob code that reproduces the observable
artifacts of a CMake 3.28 build.

## 2. Goal

The goal is to generate a `nob.c` program from a CMake project such that the
resulting Nob-driven build reproduces the CMake 3.28 artifact surface that
matters to real projects:

- local build outputs
- generated files and build-order edges
- install tree outputs
- export metadata/files
- final package outputs

This work must preserve the current architectural boundary:

`AST -> evaluator -> Event IR -> build_model -> codegen/backend -> artifacts`

## 3. Success Definition

This roadmap is complete only when Nobify can prove artifact equivalence, not
just semantic plausibility.

Success means:

- the generated Nob backend produces the same relevant build artifacts as
  CMake 3.28 for supported scenarios
- those artifacts appear in equivalent output layouts and with equivalent
  dependency ordering where the build graph makes that observable
- install, export, and packaging flows produce equivalent outputs for the
  supported generators/modes
- unsupported cases fail in a deliberate, diagnosable way instead of silently
  producing divergent artifacts
- the proof loop is stable enough that regressions are caught by normal test
  execution and by a growing real-project corpus

## 4. Current State

The roadmap starts from the current repository reality:

- evaluator coverage is effectively closed for the audited evaluator scope, but
  that audit explicitly does not prove downstream build-model, codegen, or
  backend parity
- `build_model` already reconstructs and exposes install, package, and CPack
  data through the frozen/query-facing model
- export behavior is not yet a first-class downstream semantic domain in the
  same way as targets, install rules, packages, or CPack objects
- codegen is still a minimal POSIX-oriented backend and rejects several
  semantically important downstream cases
- the default `test-v2` smoke path is green again and is once more the trusted
  aggregate smoke baseline
- the repo now has an explicit-only artifact-parity harness under
  `test_v2/artifact_parity/`, but the fixture corpus is still intentionally
  narrow and does not yet prove later-wave export or packaging parity

## 5. Frozen Definition Of Artifact Equivalence

For this roadmap, "artifact equivalence" means equivalence of observable output
and observable build behavior, not byte-for-byte identity of every intermediate
implementation detail.

The required equivalence surface is:

- build outputs:
  executables, static libraries, shared libraries, module libraries, output
  names, prefixes/suffixes, and output-directory placement
- generated files and build-order edges:
  custom command outputs, byproducts, generated sources, and the dependency
  relationships required to build them correctly
- install tree outputs:
  installed files, destinations, component layout, and install-type/group
  behavior where modeled
- export files and metadata:
  exported target metadata, namespaces, referenced targets, and build-visible
  information needed by downstream consumers
- final package outputs:
  produced package files, manifest/content layout, and package metadata for the
  supported generators
- observable failure behavior:
  unsupported or rejected cases must fail explicitly with diagnostics instead
  of silently generating incorrect artifacts

Non-goals for this definition:

- byte-identical generated Nob source is not required
- internal evaluator/build-model representation identity is not required
- backend command-line identity is not required when the artifact result and
  observable behavior are equivalent

## 6. Guardrails

The following constraints are frozen for all waves:

- CMake 3.28 remains the primary compatibility baseline
- no backend shortcut may bypass `Event IR` or `build_model` semantics in order
  to "just make artifacts work"
- backend optimization remains downstream of semantic parity and may not become
  a competing priority
- out-of-source parity is required; in-source-only behavior is not an
  acceptable final target
- every wave must end with explicit proof artifacts, not only implementation
  claims
- newly supported behavior must land with subsystem-level assertions and at
  least one cross-layer parity check
- unsupported surfaces may remain temporarily, but they must stay explicit,
  diagnosable, and tracked as roadmap debt

## 7. Proof Layers

This roadmap requires three distinct proof layers. A wave is not complete until
it defines which layer it advances and what evidence it contributes.

### 7.1 Synthetic Fixture Parity

Small, focused fixtures prove:

- Event IR behavior
- build-model reconstruction and query behavior
- backend rendering/build behavior for one semantic feature at a time
- manifest-style diffs between CMake outputs and generated Nob outputs

### 7.2 Backend And Platform Parity

Backend/platform proof demonstrates:

- POSIX/Linux baseline parity first
- correct output naming/layout rules for each platform/backend mode in scope
- host-aware install/package behavior for the supported generators

### 7.3 Real-Project Regression Parity

Real-project proof demonstrates:

- the parity harness scales beyond synthetic fixtures
- unsupported surfaces are shrinking in real repos, not only in isolated cases
- regressions in end-to-end artifact behavior are visible at the project level

## 8. Required Waves

### P0 Baseline And Proof Harness

Status:
- completed on April 4, 2026
- delivered:
  green `test-v2` smoke after fixing the immediate `pipeline` and `codegen`
  blockers
  explicit-only `artifact-parity` harness under `test_v2/artifact_parity/`
  that runs real `cmake` versus `nobify -> generated nob.c -> generated Nob`
  flows
  table-driven fixture execution with explicit `configure`, `build`, and
  `install` phase ownership plus reserved `package` orchestration
  structured manifest capture for `TREE`, `FILE_TEXT`, and `FILE_SHA256`
  across build, generated-file, install, export, package-file, and
  package-metadata domains
  runner-owned `CMK2NOB_TEST_NOBIFY_BIN` delivery so the parity harness uses a
  freshly built `nobify` from the current workspace sources
  generated `nob.c` CLI support for reserved `clean`, `install`, and `package`
  commands, with minimal install execution for focused `P0` fixtures

Deliverables:
- restore trust in the default `test-v2` smoke path by fixing the current smoke
  blockers and keeping the aggregate green
- define one canonical artifact-parity harness that runs:
  `CMake configure/build/install/package -> capture manifests`
  versus
  `nobify -> generated Nob build/install/package -> capture manifests`
- add manifest capture/diff support for:
  build outputs
  generated files
  install trees
  export files
  package files and metadata
- document the parity harness location, ownership, and aggregate execution
  policy in the test architecture docs when it becomes part of the default
  proof stack

Non-goals:
- broad new semantic support is not the target of this wave
- no backend-generalization push beyond what is needed to stabilize the proof
  loop

Exit criteria:
- `test-v2` is green again and remains the trusted smoke baseline
- at least one canonical parity suite compares CMake and generated Nob
  artifacts for a focused fixture set
- the harness can report structured diffs instead of only pass/fail text

Evidence delivered:
- `./build/nob_test test-v2`
- `./build/nob_test test-codegen`
- `./build/nob_test test-artifact-parity`

### P1 Local Build Artifact Parity

Status:
- planned

Deliverables:
- close the local build-output gaps for executables, static libraries, shared
  libraries, and module libraries on the POSIX/Linux single-config baseline
- make source root and binary root explicit backend inputs so out-of-source
  builds become first-class
- reconstruct output naming and output-directory behavior from the frozen model
  instead of relying on minimal backend defaults
- extend codegen/backend logic so supported target kinds build with artifact
  layout equivalent to CMake for this baseline

Non-goals:
- no install/export/package execution yet
- no Windows/macOS artifact rules yet

Exit criteria:
- parity fixtures cover the supported target kinds and output layout rules
- `test-v2` includes stable smoke coverage for out-of-source build cases
- artifact manifests match CMake for the supported POSIX/Linux build fixtures

### P2 Generated-File And Build-Graph Semantics

Status:
- planned

Deliverables:
- promote `add_custom_command(...)` and `add_custom_target(...)` build-relevant
  semantics into canonical downstream data instead of leaving them as local
  metadata or runtime-effect-only traces
- add first-class handling for byproducts, generated-source identity, and
  source-file build semantics that affect downstream artifact generation
- make the build-model/query surface capable of expressing generated-file
  producers, consumers, and ordering requirements
- teach the backend to materialize generated outputs and required build edges
  before compiling dependent targets

Non-goals:
- no attempt to support every custom-command backend feature in one wave
- no packaging/install/export parity yet

Exit criteria:
- synthetic parity fixtures prove generated files and byproducts appear in the
  correct locations and order
- build-model/query tests prove the canonical graph data needed by codegen
- the backend no longer relies on ad hoc local knowledge for generated sources

### P3 Usage-Requirement And Link Parity

Status:
- planned

Deliverables:
- support the build-relevant subset of generator expressions required for the
  supported artifact scenarios
- promote or canonicalize the target/source metadata that is still only visible
  as raw properties when that metadata changes emitted artifacts
- add downstream support for imported targets and richer link-library items
  where they affect link lines, transitive usage, or artifact selection
- expand effective query coverage so the backend consumes canonical usage
  semantics instead of special-casing raw strings

Non-goals:
- no "support every genex" mandate in one wave
- no platform-specific installer/package work yet

Exit criteria:
- parity fixtures prove matching link inputs and output artifacts for imported
  targets, supported genex cases, and richer usage propagation
- subsystem tests lock the new Event IR/build-model/query behavior
- unsupported downstream link cases are materially reduced and explicitly
  tracked

### P4 Backend Abstraction And Platform Artifact Rules

Status:
- planned

Deliverables:
- replace the current minimal POSIX-only assumptions with explicit backend and
  platform abstraction points
- keep Linux/POSIX as the first fully proven backend baseline
- add artifact naming, directory, and toolchain rules for Windows and macOS as
  staged follow-up deliverables under the same abstraction
- ensure backend differences are driven by canonical model/query inputs rather
  than hidden codegen heuristics

Non-goals:
- no premature multi-generator optimization layer
- no platform work that bypasses missing semantic data in earlier waves

Exit criteria:
- backend-facing interfaces clearly separate platform policy from semantic
  reconstruction
- Linux/POSIX parity remains green while Windows/macOS baseline fixtures are
  added incrementally
- platform-specific artifact naming/layout behavior is proven with parity
  fixtures before it is considered supported

### P5 Install Parity

Status:
- planned

Deliverables:
- consume install rules from the frozen model in the backend instead of
  stopping at build outputs
- implement install-tree generation with destinations, components, and install
  metadata driven by canonical build-model/query data
- add parity harness support for install tree manifests and metadata diffs
- connect install proof into normal aggregate execution once the surface is
  stable enough

Non-goals:
- no export or package-generator parity yet beyond what install execution needs
- no attempt to support every historical install quirk before the CMake 3.28
  baseline is covered

Exit criteria:
- install parity fixtures match CMake install trees for the supported install
  rule kinds
- component and install-type/group behavior is proven where the model already
  carries it
- install regressions are visible in the aggregate proof stack

### P6 Export Parity

Status:
- planned

Deliverables:
- introduce a first-class downstream export semantic path through Event IR and
  build-model where needed, instead of relying on evaluator-local metadata
  writing alone
- model and query export-relevant data needed for generated export files and
  downstream target references
- generate export files/metadata from canonical downstream state
- add parity fixtures that compare export file content and referenced metadata
  against CMake

Non-goals:
- no attempt to solve package generators in the same wave
- no promise that every export signature lands before a minimal proven subset

Exit criteria:
- export metadata is no longer only an evaluator-local side effect
- build-model/query can answer the export questions the backend needs
- export fixtures prove file/content parity for the supported signatures

### P7 Packaging Parity

Status:
- planned

Deliverables:
- consume package and CPack model data in the backend or packaging driver path
- stage archive-style package generators first, then installer-style generators
  after the archive baseline is proven
- add package manifest and metadata diff support to the parity harness
- define which package generators are supported, experimental, or intentionally
  out of scope at each milestone

Non-goals:
- no generator explosion without proof ownership
- no platform-installer claims before archive-style parity is stable

Exit criteria:
- archive-style package fixtures match CMake outputs for supported generators
- installer-style work only advances once archive parity is stable
- package outputs and metadata are compared structurally, not only by file
  existence

### P8 Real-Project Hardening

Status:
- planned

Deliverables:
- run the parity harness over a growing real-project corpus instead of only
  synthetic fixtures
- classify remaining unsupported surfaces by severity, frequency, and roadmap
  ownership
- turn recurring real-project failures into tracked release gates or explicit
  unsupported diagnostics
- use the corpus to validate that the supported subset is meaningful for real
  projects, not only for laboratory fixtures

Non-goals:
- no indefinite support claim for projects outside the proven subset
- no silent broadening of support without corresponding proof updates

Exit criteria:
- the project has a documented real-project corpus with repeatable parity runs
- release gating is informed by corpus evidence, not only by synthetic success
- remaining unsupported cases are explicit roadmap items, not hidden drift

## 9. Public Contract Impact

This roadmap is expected to change public or documented contracts in these
areas:

- Event IR families and event kinds, especially where generated-file, export,
  and downstream build-graph semantics need first-class representation
- build-model query surface, including new typed accessors needed by install,
  export, package, and backend execution paths
- `nobify` CLI/backend configuration so source root, binary root, install
  execution, export generation, and package execution become explicit workflow
  inputs
- test architecture and aggregate parity suites, including the canonical
  artifact-diff harness and the policy for when parity suites become part of
  default smoke coverage

## 10. Evidence Expectations

The minimum evidence vocabulary for this roadmap is:

- subsystem tests for new evaluator/Event IR/build-model/query behavior
- `test-v2` smoke coverage for the supported path
- artifact manifest diffs between CMake and generated Nob builds
- install tree diffs
- export file/content diffs
- package content/metadata diffs
- real-project corpus parity runs in later waves

No wave is complete with "it seems to work" evidence alone.

## 11. Relationship To Other Docs

- [`project_priorities.md`](./project_priorities.md)
  Canonical priority order: CMake 3.28 parity first, backend optimization
  later.

- [`evaluator/evaluator_event_ir_contract.md`](./evaluator/evaluator_event_ir_contract.md)
  Canonical evaluator-to-Event-IR boundary.

- [`transpiler/event_ir_v2_spec.md`](./transpiler/event_ir_v2_spec.md)
  Canonical Event IR schema and role taxonomy.

- [`build_model/README.md`](./build_model/README.md)
  Build-model documentation map and canonical downstream reconstruction docs.

- [`tests/tests_architecture.md`](./tests/tests_architecture.md)
  Current test-stack baseline and aggregate ownership rules.

- [`tests/tests_structural_refactor_plan.md`](./tests/tests_structural_refactor_plan.md)
  Active test-architecture roadmap that should absorb parity harness ownership
  once the harness becomes a stable part of the default proof stack.
