# Tests Structural Refactor Plan

## 1. Status

Status: Active plan. This document defines the structural refactor roadmap for
the v2 test architecture under `src_v2/build/nob_test.c` and `test_v2/`.

It does not redefine lexer, parser, evaluator, build-model, pipeline, or
codegen product contracts. It only defines how the test stack should evolve so
that those contracts are cheaper to verify and safer to extend.

## 2. Goal

The goal of this refactor is to make continued v2 test work land on an
architecture that is:

- modular
- reusable
- explicit about ownership boundaries
- cheaper to extend without copy-paste growth
- less dependent on white-box implementation details
- clear about suite taxonomy and aggregate execution policy

The refactor must preserve the current runner ergonomics and observability:

- `./build/nob_test` remains the official entrypoint
- workspace isolation remains mandatory
- sanitizer, coverage, and `clang-tidy` profiles remain runner-owned
- captured logs and preserved failed workspaces remain available for debugging

## 3. Current Structural Problems

### 3.1 Evaluator Tests Are Too Coupled To Internals

The normal evaluator test path still depends on implementation-private
machinery, including `evaluator_internal.h` and the macro-renaming harness in
`test_v2/evaluator/test_evaluator_v2_support.c`.

Why this hurts:
- routine internal refactors risk breaking the test harness shape, not only the
  behavior under test
- the aggregate evaluator suite is more white-box than the rest of the stack
- support code that should be reusable is trapped inside evaluator-specific
  implementation tricks

### 3.2 `build_model` Has No First-Class Test Module

`build_model` runtime sources are exercised through `pipeline` and `codegen`,
but the runner does not expose a standalone `build-model` suite as a
first-class module.

Why this hurts:
- build-model failures are harder to localize
- standalone build-model semantics compete with cross-layer assertions inside
  integration suites
- aggregate test ownership is unclear for one of the core v2 architectural
  stages

### 3.3 Semantic Pipeline Helpers Are Duplicated

`pipeline` and `codegen` both reconstruct their own variants of the semantic
path from script input through AST, evaluator execution, Event IR capture,
build-model builder/validate/freeze, and teardown.

Why this hurts:
- changes to the canonical semantic pipeline must be updated in more than one
  suite-specific helper stack
- ownership of arena lifetime, stream wrapping, and fixture shape is scattered
- duplicated bootstrap logic makes it harder to reason about whether failures
  come from the semantic pipeline or from test support drift

### 3.4 Snapshot And Golden Support Is Repeated Across Suites

`arena`, `lexer`, `parser`, `pipeline`, and `evaluator` all carry local copies
of generic helpers such as text loading, newline normalization, case-pack
parsing adapters, escaping helpers, and golden update/compare flow.

Why this hurts:
- low-level support behavior drifts across suites
- adding one more case-pack-based suite repeats already solved plumbing
- golden behavior becomes harder to audit as a single architectural surface

### 3.5 Generic Golden Suites Still Carry Hardcoded Case Counts

Several golden suites hardcode expected case counts in test code instead of
treating the parsed case pack and golden header as the source of truth.

Why this hurts:
- simple fixture edits can require redundant test-code edits
- the duplication adds maintenance friction without improving semantic
  confidence
- suite code is validating support metadata twice instead of validating one
  canonical baseline

### 3.6 Strengths To Preserve

This refactor is not a rewrite-from-scratch mandate. The current stack already
has strong operational qualities that the target architecture must preserve:

- one official runner controls command shape, profiles, locks, and logs
- workspaces isolate suite execution and per-test-case side effects
- incremental builds keep feedback fast enough for frequent use
- captured stdout/stderr and preserved failed workspaces make failures easy to
  inspect
- default aggregate execution already provides a practical smoke path

## 4. Target Architecture

The target test architecture is split into four layers with explicit ownership.

### 4.1 Official Runner Layer

Owned by `src_v2/build/nob_test.c`.

Responsibilities:
- command-line surface and aggregate selection
- module registry and module inclusion policy
- incremental build, object/dependency tracking, and build locks
- sanitizer, coverage, and `clang-tidy` profiles
- test-run workspace root creation
- captured stdout/stderr logs, preserved failed workspaces, and coverage output

Non-responsibilities:
- suite-local semantic assertions
- golden rendering logic
- subsystem-specific white-box fixtures

### 4.2 Framework Layer

Owned by the generic v2 test framework under `test_v2/`.

Responsibilities:
- `TEST`, `ASSERT`, `TEST_SKIP`, `TEST_PASS`, and deferred cleanup behavior
- suite lifecycle helpers
- per-case isolation
- generic workspace entry/leave primitives

Non-responsibilities:
- subsystem-specific semantic pipeline setup
- duplicated snapshot helpers
- direct runner profile logic

### 4.3 Shared Support Layer

Owned by reusable support modules under `test_v2/` and shared by suites.

Responsibilities:
- case-pack parsing adapters
- text IO helpers
- newline normalization
- golden compare/update policy
- generic snapshot escaping and formatting helpers
- host-fixture helpers for filesystem/process/environment setup
- one canonical semantic pipeline fixture for:
  `script -> AST -> EvalSession -> Event_Stream -> Build_Model draft/validate/freeze`

Non-responsibilities:
- module CLI ownership
- suite-specific assertions
- implementation-private subsystem access by default

### 4.4 Suite Layer

Owned by each logical test module:
- `arena`
- `lexer`
- `parser`
- `build-model`
- `evaluator`
- `pipeline`
- `codegen`
- `artifact-parity`

Responsibilities:
- semantic assertions specific to that suite
- fixture inputs and golden data owned by that suite
- explicit classification as unit-like, semantic integration, or host/integration

Non-responsibilities:
- reimplementing generic support behavior already owned by the shared layer
- importing implementation-private headers in aggregate-path suites

## 5. Guardrails

The following constraints are frozen for all waves in this plan:

- keep the `./build/nob_test` command surface stable for existing commands
- keep sanitizer, coverage, and `clang-tidy` behavior owned by the runner
- no aggregate-path test may include subsystem `*_internal.h`
- no suite may own duplicated generic case-pack, golden, text IO, or newline
  helpers once shared support exists
- `pipeline` remains an integration suite, not the home of standalone
  `build_model` coverage
- `codegen` consumes frozen/query-facing build-model behavior, not a duplicated
  custom semantic pipeline
- any future internal-only white-box suite must be explicit, non-aggregate, and
  justified in documentation
- suite taxonomy must remain explicit in docs and runner policy
- the refactor may change internal support structure aggressively, but it may
  not regress the runner ergonomics already relied on by the workspace

## 6. Required Waves

### Wave T0: Baseline And Taxonomy

Status:
- completed (March 27, 2026)

Delivered artifacts:
- `docs/tests/tests_architecture.md` now records the current test-stack
  baseline, preserved runner behavior, ownership boundaries, and suite taxonomy
- `docs/tests/README.md` now distinguishes the canonical baseline from the
  multi-wave roadmap

Deliverables:
- classify current modules into unit-like, semantic integration, and
  host/integration categories
- document preserved runner behavior for workspace setup, build profiles, log
  capture, and aggregate execution
- define the target ownership boundaries between runner, framework, shared
  support, and suites
- establish the planned first-class module set, including `build-model`

Non-goals:
- no support extraction yet
- no suite rewrites in this wave
- no runner CLI expansion beyond documentation of the planned target state

Exit criteria:
- suite taxonomy is explicit
- ownership boundaries are explicit
- no later wave needs to reinterpret where runner, framework, support, or
  suite responsibilities belong

Closure checks:
- every current suite has a documented category
- the target module list includes `build-model`
- preserved runner behavior is written down as a hard constraint

### Wave T1: Shared Snapshot And Case-Pack Support

Status:
- completed (March 27, 2026)

Delivered artifacts:
- `test_v2/test_snapshot_support.h` and `test_v2/test_snapshot_support.c`
  now define the shared snapshot/case-pack helper surface for text IO,
  newline normalization, case-pack parse, escaped snapshot rendering, and
  golden compare/update flow
- `arena`, `lexer`, `parser`, `pipeline`, and `evaluator` now consume the
  shared helper layer instead of carrying their own generic plumbing copies
- hardcoded generic case-count checks have been removed from the shared golden
  suites so parsed case-pack content and golden snapshots remain the canonical
  baseline
- `src_v2/build/nob_test.c` now compiles the shared support module into the
  suites that depend on it

Deliverables:
- extract one canonical shared support surface for:
  - text file load
  - newline normalization
  - case-pack parse
  - golden compare/update
  - snapshot escaping utilities
- move generic golden support out of suite-local copies
- replace hardcoded generic case-count assertions with one canonical source of
  truth derived from parsed case-pack content and/or golden header validation

Non-goals:
- no semantic pipeline fixture yet
- no evaluator public/private boundary work yet
- no suite-specific assertion consolidation beyond shared support extraction

Exit criteria:
- `arena`, `lexer`, `parser`, `pipeline`, and `evaluator` no longer own local
  copies of generic snapshot/golden plumbing
- common golden behavior is defined once
- generic case-count duplication is removed from suite code

Closure checks:
- one shared helper surface owns generic case-pack and golden flow
- suite code keeps only suite-specific rendering/assertion logic
- no aggregate suite carries a magic case-count constant for generic golden
  support bookkeeping

### Wave T2: Shared Semantic Pipeline Fixture

Status:
- completed (March 27, 2026)

Delivered artifacts:
- `test_v2/test_semantic_pipeline.h` and `test_v2/test_semantic_pipeline.c`
  now own the canonical semantic fixture for `script -> AST -> EvalSession ->
  Event_Stream -> Build_Model draft/validate/freeze`
- `pipeline` golden snapshots and stream-to-model integration assertions now
  consume the shared semantic fixture instead of local bootstrap helpers
- `codegen` now builds its semantic test model through the same shared fixture
  path used by `pipeline`
- arena ownership, root stream wrapping, and build-model freeze behavior are
  now defined once in shared support instead of drifting between suites

Deliverables:
- introduce one canonical shared fixture for:
  `script -> AST -> EvalSession -> Event_Stream -> Build_Model draft/validate/freeze`
- define one owner for arena lifetime and teardown in that fixture
- migrate `pipeline` and `codegen` onto that shared fixture
- keep suite-specific assertions separate from fixture construction

Non-goals:
- no standalone `build-model` suite split yet
- no evaluator decoupling work beyond using public semantic pipeline surfaces
- no runner behavior changes beyond consuming the same suite binaries

Exit criteria:
- no duplicated semantic pipeline bootstrap logic remains across `pipeline` and
  `codegen`
- arena ownership, stream wrapping, and teardown policy are defined once
- semantic pipeline support is reusable for future `build-model` suite work

Closure checks:
- `pipeline` and `codegen` call the same semantic fixture path
- stream wrapping and model freeze helpers no longer drift independently
- suite files contain assertions, not duplicated infrastructure

### Wave T3: First-Class `build-model` Module

Status:
- completed (March 27, 2026)

Delivered artifacts:
- `test_v2/build_model/` now exists as a standalone first-class suite with its
  own runner entrypoint and suite ownership
- the runner now exposes `test-build-model` and includes `build-model` in the
  default aggregate module set
- standalone `Event_Stream -> Build_Model` semantics moved out of `pipeline`
  and into the dedicated `build-model` suite
- `pipeline` now remains focused on script-driven cross-layer integration
  snapshots

Deliverables:
- add a standalone `build-model` test module to the target architecture and
  planned runner surface
- move standalone build-model semantic coverage out of `pipeline` where that
  coverage is not actually cross-layer
- define target aggregate behavior with `build-model` included in the default
  aggregate suite
- keep `pipeline` focused on Event IR to build-model integration and other
  cross-layer behavior

Non-goals:
- no collapse of `pipeline` into `build-model`
- no codegen ownership changes beyond consuming build-model query/frozen state
- no evaluator-specific white-box additions

Exit criteria:
- `build_model` has direct suite ownership
- build-model failures are isolatable without routing through `pipeline` or
  `codegen`
- aggregate policy explicitly includes `build-model`

Closure checks:
- runner target architecture lists `build-model` as a module
- `pipeline` retains only integration assertions that cross subsystem
  boundaries
- standalone build-model semantics have a direct test home

### Wave T4: Evaluator Test Decoupling

Status:
- completed (March 27, 2026)

Delivered artifacts:
- aggregate-path evaluator support no longer includes `evaluator_internal.h`
- canonical artifact and ctest-step inspection now flow through narrow public
  `eval_session_*` accessors instead of test-side session-struct peeking
- generic env, symlink, tar, git, and mock-host-command helpers now live in
  shared `test_v2/test_host_fixture_support.*`
- evaluator-specific support now wraps shared host-fixture helpers instead of
  owning their implementation
- aggregate evaluator assertions no longer rely on direct `Eval_Test_Runtime`
  field reads for current-file inspection

Deliverables:
- remove the architectural dependence on `evaluator_internal.h` from normal
  evaluator tests
- move generic OS/process/git/tar/symlink/environment helpers into shared
  host-fixture support where they remain reusable and explicitly bounded
- require evaluator assertions to use public session/request/report/stream
  surfaces by default
- where white-box visibility is still required, prefer narrow stable inspection
  helpers over internal-header inclusion

Non-goals:
- no reintroduction of broad evaluator-private API exposure
- no collapse of all evaluator tests into pure black-box command-line tests
- no runner-owned host helper duplication

Exit criteria:
- aggregate evaluator suites are public-surface driven
- macro-renaming and internal-header harness tricks are no longer the default
  architecture
- host-fixture helpers that remain are shared and explicitly owned

Closure checks:
- no aggregate evaluator suite includes `evaluator_internal.h`
- evaluator support layers are organized around public execution/report surfaces
- canonical artifact and ctest-step checks go through public query helpers
- any remaining white-box path is explicit, isolated, and non-aggregate

### Wave T5: Aggregate Policy And CI Shape

Status:
- completed (March 27, 2026)

Delivered artifacts:
- default aggregate membership is now documented per module in
  `docs/tests/tests_architecture.md`
- the runner module registry explicitly documents that aggregate membership is
  owned by `include_in_aggregate`
- the architecture baseline now defines the smoke path as `./build/nob_test`
  or `./build/nob_test test-v2`, with profile variants kept on the same module
  set
- explicit-only status for `evaluator-integration` is documented with a concrete
  host-sensitivity/runtime-cost reason
- the versioned CI baseline is now tied to `.github/workflows/evaluator-file-parity.yml`
  and documented as validating the aggregate smoke contract

Deliverables:
- define which suites belong in default aggregate execution and why
- preserve current smoke workflow and captured-failure ergonomics
- document which suites may remain explicit/non-aggregate because they are
  heavier or more host-sensitive
- align module ownership, aggregate policy, and CI expectations

Non-goals:
- no silent demotion of important architectural suites from aggregate execution
- no runner ergonomics regression in the name of CI simplification
- no undocumented host-sensitive exceptions

Exit criteria:
- aggregate policy is intentional and documented
- module ownership and CI behavior are aligned
- heavy or host-sensitive exceptions are explicit

Closure checks:
- default aggregate membership is documented per module
- non-aggregate suites have a stated reason
- CI expectations match the module taxonomy and aggregate policy

## 7. Acceptance And Verification

This structural refactor is successful when the test stack reaches a state where
new coverage can be added by extending:

- runner policy for module/profile execution
- framework lifecycle behavior
- shared support fixtures
- suite-local semantic assertions

without reintroducing duplicated infrastructure or implementation-private
coupling as the default path.

The completed architecture must satisfy these closure checks:

- no aggregate suite includes subsystem `*_internal.h`
- no suite-local duplicate generic golden, case-pack, text IO, or newline
  helper stacks remain
- `pipeline` and `codegen` share one semantic pipeline support path
- `build-model` exists as a first-class module and aggregate participant
- runner CLI stability is preserved for existing commands
- workspace isolation, sanitizer profiles, coverage flow, and captured logs
  remain runner-owned

## 8. Current Baseline

Current workspace snapshot: March 27, 2026.

Operational baseline observed in the current workspace:
- the present runner shape is operational
- `./build/nob_test test-lexer` passes
- `./build/nob_test test-parser` passes
- `./build/nob_test test-pipeline` passes

These smoke checks do not mean the target architecture is landed. They only
confirm that the current operational baseline is healthy enough to support a
multi-wave structural refactor.
