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
- `build_model` now reconstructs and exposes install, standalone export,
  package-registry export, and CPack package-planning data through the
  frozen/query-facing model
- codegen now has explicit platform/backend policy selection, with
  `linux + posix` as the execution-proven baseline and
  `darwin + posix` / `windows + win32-msvc` as render-only baselines, but it
  still rejects several out-of-scope later-wave semantic surfaces
- the default `test-v2` smoke path is green again and is once more the trusted
  aggregate smoke baseline
- the repo now has an explicit-only artifact-parity harness under
  `test_v2/artifact_parity/`; it is currently Linux/POSIX-only for execution
  parity, and the fixture corpus is still intentionally narrow and does not
  yet prove later-wave export or packaging parity
- the repo now also has an explicit-only `evaluator-codegen-diff` harness
  under `test_v2/evaluator_codegen_diff/` that uses the evaluator corpus plus
  the coverage matrix as a canonical inventory for `parity-pass`,
  `backend-reject`, and `evaluator-only` backend status

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
- completed on April 4, 2026
- delivered:
  public `nobify` root flags `--source-root` and `--binary-root`, with legacy
  in-source defaults preserved when the flags are omitted
  explicit source/binary-root plumbing through the evaluator session, root
  execution request, wrapped directory events, and `Nob_Codegen_Options`
  backend path rules that place default final artifacts in the owner
  directory's binary dir and interpret relative output-directory properties
  from that same owner binary dir
  backend-private object staging rooted under `<binary_root>/.nob/obj/...`
  instead of hardcoded `build/obj`
  `clean` behavior narrowed to backend-private staging plus discovered emitted
  artifact paths instead of deleting an entire build tree
  evaluator/build-model fixes so `add_subdirectory()` default binary-dir
  rebasing now reaches downstream consumers in the correct event order and
  target-owner directory assignment
  aggregate-safe out-of-source smoke coverage in `test_v2/codegen/` and
  explicit real-CMake out-of-source parity cases in `test_v2/artifact_parity/`

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

Evidence delivered:
- `./build/nob_test test-build-model`
- `./build/nob_test test-codegen`
- `./build/nob_test test-artifact-parity`
- `./build/nob_test test-v2`

### P2 Generated-File And Build-Graph Semantics

Status:
- completed on April 4, 2026
- delivered:
  canonical Event IR build-graph families/events for output rules, custom
  targets, target hooks, tokenized command argv, and generated-source marks
  evaluator lowering that emits first-class build-step events for
  `add_custom_command(OUTPUT ...)`, `add_custom_target(...)`,
  `add_custom_command(TARGET ... PRE_BUILD|PRE_LINK|POST_BUILD ...)`, and
  both `GENERATED` source-property APIs
  frozen build-model/query support for build-step records, generated-source
  identity, producer-step linkage, and dependency classification across
  target, producer-step, and file edges
  backend scheduling that materializes generated outputs before compilation,
  attaches executable work to utility targets, preserves PRE_LINK/POST_BUILD
  ordering, and rejects `APPEND` steps explicitly instead of mis-executing
  them
  dedicated proof coverage across evaluator, pipeline, build-model, codegen,
  and explicit artifact-parity suites for generated sources, custom-target
  dependencies, and post-build sidecar outputs

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

Evidence delivered:
- `./build/nob_test test-evaluator`
- `CMK2NOB_UPDATE_GOLDEN=1 ./build/nob_test test-pipeline`
- `./build/nob_test test-build-model`
- `./build/nob_test test-codegen`
- `./build/nob_test test-artifact-parity`
- `./build/nob_test test-v2`

### Pre-P3 Hardening

Status:
- completed on April 4, 2026
- delivered:
  a private codegen step-emission layer that pulls build-step scheduling out of
  the monolithic backend file, standardizes per-command lexical scopes, and
  deduplicates rebuild inputs before emitting `nob_needs_rebuild(...)`
  embedded runtime tool resolution for `cmake`/`cpack`, with generated Nob
  precedence now fixed as `NOB_CMAKE_BIN` / `NOB_CPACK_BIN` -> embedded
  absolute path resolved at generation time -> bare `cmake` / `cpack`
  stricter frozen-model validation for duplicate effective producers, invalid
  step ownership contracts, invalid generated-source producer links, and
  execution-graph cycles across targets plus build steps
  focused build-model freeze/query coverage for producer matching, byproduct
  lookup, generated marks without producers, and unresolved file dependencies
  cleanup of the remaining `eval_expr.c` warning plus parity regressions that
  prove embedded CMake execution works without PATH injection crutches

Deliverables:
- keep `P2` scheduling semantics while making the backend code shape explicit
  and easier to extend in `P3`
- harden the generated runtime so CMake-family helper steps do not depend on
  the caller's ambient `PATH`
- move impossible graph states from late backend failures into frozen-model
  validation errors with clear diagnostics
- lock the public freeze/query surface with smaller, targeted tests instead of
  relying only on broader integration scenarios

Non-goals:
- no expansion of positive backend support for `DEPFILE`, `IMPLICIT_DEPENDS`,
  `JOB_POOL`, `JOB_SERVER_AWARE`, `USES_TERMINAL`, or `CODEGEN`
- no new install/export/package semantics
- no widening of supported custom-command behavior beyond the `P2` contract

Exit criteria:
- `test-build-model`, `test-codegen`, `test-artifact-parity`, and `test-v2`
  all stay green after the scheduler refactor and stricter validation
- generated Nob no longer needs suite-local PATH mutation to execute
  `cmake -E` helper steps in parity fixtures
- impossible frozen-model execution-graph states fail during validation rather
  than surfacing later as backend-only corruption

Evidence delivered:
- `./build/nob_test test-build-model`
- `./build/nob_test test-codegen`
- `./build/nob_test test-artifact-parity`
- `./build/nob_test test-v2`

### P3 Usage-Requirement And Link Parity

Status:
- completed on April 4, 2026
- delivered:
  context-aware frozen-model query evaluation through `BM_Query_Eval_Context`,
  including canonical support for `BUILD_INTERFACE`, `INSTALL_INTERFACE`,
  `LINK_ONLY`, `CONFIG`, `COMPILE_LANGUAGE`, and `TARGET_PROPERTY`
  positive genex support for `NOT`, `AND`, `OR`, and `STREQUAL`, with
  unsupported operators still failing explicitly
  shared compile-feature catalog usage across evaluator validation,
  build-model query, and codegen so compile-feature floors now drive emitted
  `-std=` flags for supported C and C++ targets
  imported-target downstream support for `INTERFACE`, `STATIC`, `SHARED`, and
  `UNKNOWN` targets in usage/link contexts, including config-aware imported
  file and linker-file resolution plus imported link-language hinting
  generated Nob `--config <cfg>` support so config-sensitive genex and
  `debug`/`optimized` link items are deterministic at runtime
  per-source compile-side usage evaluation in codegen so mixed-language
  `COMPILE_LANGUAGE` conditions are honored on the current POSIX backend
  explicit backend rejection for still-out-of-scope concrete dependencies on
  `PRECOMPILE_HEADERS*`, imported executables as link inputs, and module
  libraries as link inputs

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

Evidence delivered:
- `./build/nob_test test-build-model`
- `./build/nob_test test-codegen`
- `./build/nob_test test-artifact-parity`
- `./build/nob_test test-v2`

### P4 Backend Abstraction And Platform Artifact Rules

Status:
- completed on April 5, 2026
- delivered:
  explicit generation-time platform/backend selection in `nobify` through
  `--platform host|linux|darwin|windows` and
  `--backend auto|posix|win32-msvc`
  typed `Nob_Codegen_Options` target selection with an explicit policy matrix
  that accepts only:
  `linux + posix`
  `darwin + posix`
  `windows + win32-msvc`
  policy-driven local artifact planning for runtime versus linker artifacts,
  including Windows DLL plus import-library splits and platform-aware
  `TARGET_FILE*` versus `TARGET_LINKER_FILE*` behavior
  backend-generated runtime helpers for directory creation, parent creation,
  recursive cleanup, file copy, and tool resolution, removing the remaining
  literal `mkdir -p` and `rm -rf` shell assumptions from generated output
  query/codegen platform threading so `$<PLATFORM_ID:...>` now evaluates from
  the chosen generation target instead of an implicit host/default context
  typed build-model/query accessors for `WIN32_EXECUTABLE` and
  `MACOSX_BUNDLE`, with explicit codegen rejection for both semantics until a
  future execution wave broadens support
  render-only proof for `darwin + posix` and `windows + win32-msvc`, while
  keeping `linux + posix` as the only execution-proven aggregate baseline
  Linux/POSIX-only artifact-parity execution, now driven explicitly through
  `nobify --platform linux --backend posix`

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

Evidence delivered:
- `./build/nob_test test-build-model`
- `./build/nob_test test-codegen`
- `./build/nob_test test-artifact-parity`
- `./build/nob_test test-v2`

Support matrix after `P4`:
- supported:
  `linux + posix`
- render-only:
  `darwin + posix`
  `windows + win32-msvc`
- rejected:
  any other platform/backend pair

### P5 Install Parity

Status:
- completed on April 5, 2026
- delivered:
  effective install/export component materialization in evaluator lowering,
  so omitted `COMPONENT` now freezes as the visible
  `CMAKE_INSTALL_DEFAULT_COMPONENT_NAME` or `Unspecified`
  generated Nob install CLI support for
  `install [--prefix <path>] [--component <name>]`
  prefix-aware and component-aware Linux/POSIX install execution for the
  supported baseline:
  `install(TARGETS)`, `install(FILES)`, `install(PROGRAMS)`,
  `install(DIRECTORY)`, `install(EXPORT)`, `PUBLIC_HEADER`, and
  artifact-specific destinations
  aggregate-safe evaluator/build-model/codegen proof for effective component
  naming, selective component installs, custom install prefixes,
  `PROGRAMS` executability preservation, `DIRECTORY` trailing-slash
  semantics, `PUBLIC_HEADER` installation, installed export emission, and
  `clean` preserving custom install prefixes
  explicit-only artifact-parity install fixtures with independent CMake/Nob
  prefix and component knobs, plus real-project corpus installs now driven
  through explicit generated-Nob `--prefix` execution

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

Evidence delivered:
- `./build/nob_test test-evaluator`
- `./build/nob_test test-build-model`
- `./build/nob_test test-codegen`
- `./build/nob_test test-artifact-parity`
- `./build/nob_test test-artifact-parity-corpus`
- `./build/nob_test test-v2`

### P6 Export Parity

Status:
- completed on April 5, 2026
- delivered:
  standalone export semantics now flow canonically through `Event IR`,
  `build_model`, query, and codegen instead of relying on evaluator-only host
  writes
  new downstream export events cover build-tree exports from
  `export(TARGETS ...)`, build-tree exports from `export(EXPORT ...)`, and
  package-registry exports from `export(PACKAGE ...)`
  evaluator host-effect gating is now explicit:
  `nobify` disables export host effects while evaluator-focused tests can keep
  them enabled to preserve contract coverage
  generated `nob.c` now has an explicit `export` command that emits build-tree
  export files and Linux/POSIX package-registry entries without implicitly
  building or installing targets
  aggregate-safe proof now covers standalone export lowering, frozen-model
  queries, pipeline snapshots, backend execution, and explicit rejection of
  unsupported `APPEND` / `CXX_MODULES_DIRECTORY` export forms
  explicit-only parity proof now includes build-tree export parity for
  `export(TARGETS ...)` and `export(EXPORT ...)`, plus package-registry parity
  for `export(PACKAGE ...)`, all with tiny downstream consumer projects

Deliverables:
- introduce a first-class downstream standalone-export path through
  `Event IR`, build-model, query, and codegen
- model the supported standalone export surface explicitly:
  `export(TARGETS ...)`
  `export(EXPORT ...)`
  `export(PACKAGE ...)`
- generate build-tree export files and package-registry entries from canonical
  downstream state
- prove build-tree export parity through file/content diffs and downstream
  consumers, and prove package-registry parity through isolated-home consumer
  flows

Non-goals:
- no attempt to solve package generators in the same wave
- no positive support for `APPEND` or `CXX_MODULES_DIRECTORY`
- no standalone C++ modules export parity yet
- no attempt to broaden this into install/package generator work that belongs
  to adjacent waves

Exit criteria:
- standalone export metadata is no longer only an evaluator-local side effect
- build-model/query can answer the standalone export questions the backend
  needs
- generated `nob.c` can execute explicit `export` runs without implicit build
  or install behavior
- explicit parity fixtures prove the supported standalone export signatures
  against real CMake outputs and downstream consumers

Evidence delivered:
- `./build/nob_test test-evaluator`
- `./build/nob_test test-build-model`
- `./build/nob_test test-codegen`
- `CMK2NOB_UPDATE_GOLDEN=1 ./build/nob_test test-pipeline`
- `./build/nob_test test-artifact-parity`
- `./build/nob_test test-artifact-parity-corpus`
- `./build/nob_test test-v2`

### P7 Packaging Parity

Status:
- completed on April 5, 2026
- delivered:
  CPack package-planning semantics now flow canonically through `Event IR`,
  `build_model`, query, and codegen instead of remaining a loose backend-side
  reconstruction
  `include(CPack)` now materializes a downstream package snapshot with
  effective generators, package name/version, file name, output directory,
  include-top-level flag, archive-component-install flag, and preserved
  component metadata
  generated `nob.c` now has an explicit Linux/POSIX-only packaging backend for
  archive-style generators `TGZ`, `TXZ`, and `ZIP`, with
  `package [--generator <name>] [--output-dir <path>]`
  package generation is now full-package-only and reuses the install backend to
  stage payloads before creating archives and normalized package metadata
  aggregate-safe proof now covers package snapshot lowering, frozen-model
  queries, pipeline snapshots, generated package execution, custom output
  directories, include-top-level on/off behavior, and explicit rejection of
  unsupported component packaging
  explicit-only parity proof now covers real-CMake package parity for `TGZ`,
  `TXZ`, and `ZIP` through structural archive metadata plus normalized
  extracted-tree diffs

Deliverables:
- consume canonical CPack package-planning data in the backend instead of
  reconstructing package behavior from loose install/export state
- support archive-style package generators first:
  `TGZ`
  `TXZ`
  `ZIP`
- add structural package manifest and metadata diff support to the parity
  harness
- define the positive baseline clearly:
  Linux/POSIX only
  internal backend only
  full-package-only execution

Non-goals:
- no installer-generator support in this wave
- no positive component-packaging support in this wave
- no generator explosion beyond `TGZ`, `TXZ`, and `ZIP`
- no platform-installer claims before archive-style parity is stable

Exit criteria:
- archive-style package fixtures match CMake outputs for supported generators
- the generated backend owns archive creation directly instead of delegating to
  `cpack`
- full-package-only execution is explicit and diagnosable
- package outputs and metadata are compared structurally, not only by file
  existence

Evidence delivered:
- `./build/nob_test test-evaluator`
- `./build/nob_test test-build-model`
- `CMK2NOB_UPDATE_GOLDEN=1 ./build/nob_test test-pipeline`
- `./build/nob_test test-pipeline`
- `./build/nob_test test-codegen`
- `./build/nob_test test-artifact-parity`
- `./build/nob_test test-artifact-parity-corpus`
- `./build/nob_test test-v2`

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

## 9. Handoff To The Closure Program

This roadmap remains the historical and delivered `P0` through `P8` path to the
current parity baseline.

It is no longer the canonical home for detailed planning of:

- generated-backend `configure` execution
- `execute_process`
- `try_compile`
- `try_run`
- `FetchContent_*`
- `ctest_*`
- the broader closure of evaluator-implemented surfaces into explicit
  downstream/backend status

That future work is now coordinated by:

- [`evaluator_codegen_closure_roadmap.md`](./evaluator_codegen_closure_roadmap.md)

The project goal does not change. Future work remains subordinate to the same
CMake 3.28 artifact-parity objective. What changes is ownership: closure of the
remaining evaluator-to-codegen gap is now treated as its own multi-wave program
instead of being folded into the already-delivered artifact-parity wave list.

## 10. Public Contract Impact

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

## 11. Evidence Expectations

The minimum evidence vocabulary for this roadmap is:

- subsystem tests for new evaluator/Event IR/build-model/query behavior
- `test-v2` smoke coverage for the supported path
- explicit evaluator-corpus-backed inventory coverage that keeps backend-owned
  unsupported surfaces classified instead of implicit
- artifact manifest diffs between CMake and generated Nob builds
- install tree diffs
- export file/content diffs
- package content/metadata diffs
- real-project corpus parity runs in later waves

No wave is complete with "it seems to work" evidence alone.

## 12. Relationship To Other Docs

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

- [`tests/evaluator_codegen_diff.md`](./tests/evaluator_codegen_diff.md)
  Canonical contract for the explicit closure harness that classifies
  implemented evaluator surfaces into explicit downstream/backend states.

- [`evaluator_codegen_closure_roadmap.md`](./evaluator_codegen_closure_roadmap.md)
  Canonical post-`P8` multi-wave closure program for the remaining
  evaluator-to-codegen gap.

- [`tests/tests_structural_refactor_plan.md`](./tests/tests_structural_refactor_plan.md)
  Active test-architecture roadmap that should absorb parity harness ownership
  once the harness becomes a stable part of the default proof stack.
