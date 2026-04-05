# Tests Architecture

## Status

This document defines the current baseline architecture and suite taxonomy for
the v2 test stack.

As of April 5, 2026:
- the official test entrypoint is `./build/nob_test`
- the runner owns module selection, build profiles, incremental compilation,
  workspace roots, captured logs, and aggregate execution
- the generic framework under `test_v2/` owns per-suite and per-case lifecycle
- generic snapshot/case-pack and semantic pipeline support are now centralized
  under shared helpers in `test_v2/`
- generic host-fixture support for env/symlink/git/tar helpers is now
  centralized under shared helpers in `test_v2/`
- `build-model` is now a first-class runner module and aggregate participant
- `artifact-parity` now exists as an explicit-only host/integration module for
  real-CMake versus generated-Nob artifact comparison
- generated-file/build-graph proof is now split across `pipeline`,
  `build-model`, `codegen`, and explicit-only `artifact-parity` coverage
- platform/backend proof is now split between aggregate-safe `codegen`
  render/execution coverage and Linux/POSIX-only explicit `artifact-parity`
  execution coverage
- install parity proof is now split between aggregate-safe `evaluator`,
  `build-model`, and `codegen` coverage, while `artifact-parity` and the
  pinned real-project corpus remain explicit-only for real-CMake install
  comparison
- standalone export parity proof is now split between aggregate-safe
  `evaluator`, `build-model`, `pipeline`, and `codegen` coverage, while
  `artifact-parity` remains explicit-only for real-CMake export parity plus
  downstream consumer proof

This file is the canonical baseline for test architecture, ownership, and suite
taxonomy. The multi-wave change roadmap lives in
[`tests_structural_refactor_plan.md`](./tests_structural_refactor_plan.md).

## Canonical Boundary

The current and target architectural boundary is:

`nob_test runner -> test framework -> shared support -> suites`

The current implementation already has the runner and framework boundaries in
place. The shared-support boundary exists only partially and is the main area
of structural follow-up covered by the refactor plan.

This documentation is about test architecture only. It does not redefine lexer,
parser, evaluator, build-model, pipeline, or codegen product semantics.

## Preserved Runner Behavior

The runner-owned behavior that future waves must preserve is:

- stable `./build/nob_test` command entry for existing commands
- module registry and aggregate selection
- incremental object and binary rebuild behavior
- build locking under `Temp_tests/locks`
- sanitizer, coverage, and `clang-tidy` profiles
- top-level run workspaces under `Temp_tests/runs`
- captured stdout/stderr logs and preserved failed workspaces
- preflight ownership for result-type convention checks and runner validation

These behaviors belong to the runner layer and must not drift into suite-local
code.

## Current Module Taxonomy

### Unit-Like Suites

- `arena`
  Focus: allocator behavior, cleanup semantics, and case-pack/golden coverage.
  Current aggregate status: included.

- `lexer`
  Focus: tokenization behavior and golden snapshots.
  Current aggregate status: included.

- `parser`
  Focus: AST construction, diagnostics, and golden snapshots.
  Current aggregate status: included.

### Semantic Integration Suites

- `build-model`
  Focus: Event IR to draft/validate/freeze semantics and frozen-model query
  behavior.
  Current aggregate status: included.

- `evaluator`
  Focus: evaluator public execution/report/event behavior across command
  families. This is structurally a semantic integration suite and the aggregate
  path now exercises it through public execution/report/query surfaces by
  default.
  Current aggregate status: included.

- `pipeline`
  Focus: evaluator-to-Event-Stream-to-build-model integration and frozen-model
  semantics.
  Current aggregate status: included.

### Host/Integration Suites

- `codegen`
  Focus: generated `nob` output, host compiler execution, and generated-binary
  behavior. This suite crosses semantic and host/toolchain boundaries and now
  owns the aggregate-safe out-of-source `P1` smoke coverage for local build
  artifact placement plus aggregate-safe `P2` smoke coverage for generated
  sources, custom-target scheduling, hook ordering, and explicit `APPEND`
  rejection. `P3` extends that aggregate-safe ownership again with mixed
  language `COMPILE_LANGUAGE` cases, imported prebuilt library coverage,
  config-sensitive `debug`/`optimized` link selection, `TARGET_FILE*`
  build-step argv coverage, and explicit rejection paths for unsupported genex
  operators plus concrete `PRECOMPILE_HEADERS*` dependencies. `P4` keeps
  Linux/POSIX as the only execution-proven aggregate baseline while adding
  render-only policy coverage for explicit generation targets
  `linux + posix`, `darwin + posix`, and `windows + win32-msvc`. Platform and
  backend are now explicit generation-time inputs instead of host-derived
  implicit defaults. `P5` extends the aggregate-safe ownership again with
  Linux/POSIX install smoke for custom prefixes, component-selective installs,
  `PROGRAMS` executability preservation, `DIRECTORY` trailing-slash semantics,
  `PUBLIC_HEADER`, installed export emission, and verification that `clean`
  does not delete custom install prefixes. `P6` extends it again with
  standalone export smoke for explicit generated-Nob `export` execution,
  build-tree export file emission from `export(TARGETS ...)` and
  `export(EXPORT ...)`, Linux/POSIX package-registry writes from
  `export(PACKAGE ...)`, and explicit rejection of unsupported
  `APPEND` / `CXX_MODULES_DIRECTORY` export forms.
  Current aggregate status: included.

- `artifact-parity`
  Focus: real `cmake 3.28.x` versus `nobify -> generated Nob` artifact
  comparison through the explicit harness under `test_v2/artifact_parity/`.
  The current `P0` fixture set proves build outputs, generated files, install
  trees, and empty export/package manifest domains through structured
  `TREE`/`FILE_TEXT`/`FILE_SHA256` capture. `P1` extends that harness with
  explicit out-of-source build parity fixtures while keeping it outside the
  default aggregate. `P2` extends it again with real-CMake parity fixtures for
  generated-source consumers, custom-target dependency edges, and post-build
  sidecar outputs. `P3` adds explicit-only real-CMake parity fixtures for
  imported prebuilt libraries, config-sensitive link selection, and
  usage-requirement propagation through `BUILD_INTERFACE`, `LINK_ONLY`, and
  `TARGET_PROPERTY`. The suite requires a real `cmake 3.28.x` plus the
  runner-provided `CMK2NOB_TEST_NOBIFY_BIN` tool path. Generated Nob runtime
  resolution for CMake-family tools now follows this precedence:
  `NOB_CMAKE_BIN` / `NOB_CPACK_BIN` -> embedded absolute path captured by
  `nobify` at generation time -> bare `cmake` / `cpack`. The harness no
  longer relies on PATH injection to make `cmake -E` steps work. Generated Nob
  runtime config selection is now explicit through `--config <cfg>`; the empty
  config remains the default when the flag is omitted. `P4` keeps this suite
  Linux/POSIX-only for execution parity and now drives `nobify` explicitly as
  `--platform linux --backend posix`, so the parity baseline no longer depends
  on implicit host selection. The explicit real-project corpus now keeps pinned
  upstream inputs as compressed local archives and only extracts them under the
  isolated `Temp_tests` workspace, which keeps third-party trees out of the
  day-to-day repo layout. `P5` extends the install harness again with explicit
  per-case `--prefix` / `--component` control for CMake and generated Nob, and
  the real-project corpus now runs generated-Nob installs through explicit
  custom prefixes before the consumer-against-installed-prefix checks. `P6`
  extends the explicit harness again with an explicit `export` phase and
  downstream-consumer proof for `export(TARGETS ...)`, `export(EXPORT ...)`,
  and isolated-home `export(PACKAGE ...)`.
  Current aggregate status: excluded from the default aggregate path.

- `evaluator-integration`
  Focus: heavier evaluator scenarios that depend on host process, environment,
  filesystem, or tool interactions.
  Current aggregate status: excluded from the default aggregate path.

## Current Ownership Boundaries

### Runner Layer

Owner:
- `src_v2/build/nob_test.c`

Responsibilities:
- module registry
- aggregate inclusion policy
- test profiles and instrumentation flags
- binary build and link flow
- run workspace creation and cleanup
- failure log capture and coverage artifact generation

Non-responsibilities:
- suite-specific semantic assertions
- duplicated golden or snapshot helpers
- subsystem-private fixture logic

### Framework Layer

Owners:
- `test_v2/test_v2_assert.h`
- `test_v2/test_v2_assert.c`
- `test_v2/test_v2_suite.h`
- `test_v2/test_workspace.h`
- `test_v2/test_workspace.c`

Responsibilities:
- official-runner enforcement
- test pass/fail/skip signaling
- deferred cleanup stack behavior
- suite workspace setup and teardown
- per-case isolation through `test_ws_case_enter(...)`

Non-responsibilities:
- subsystem-specific semantic bootstrap
- duplicated suite-local support behavior that should be shared

### Shared Support Layer

Current owners:
- `test_v2/test_case_pack.h`
- `test_v2/test_fs.h`
- `test_v2/test_snapshot_support.*`
- `test_v2/test_semantic_pipeline.*`
- `test_v2/test_host_fixture_support.*`
- suite-local support files such as:
  - `test_v2/evaluator/test_evaluator_v2_support.*`
  - `test_v2/codegen/test_codegen_v2_support.*`

Current state:
- the shared-support boundary is still incomplete overall
- snapshot/golden plumbing and semantic pipeline bootstrap are now centralized
- host fixtures are now centralized
- evaluator-specific and codegen-specific support remains suite-local only where
  the logic is truly module-specific
- generated build-step execution stays suite-local to `codegen`, but its proof
  now includes focused scheduler coverage plus runtime tool-resolution
  regressions instead of depending on ad hoc PATH setup

Target direction:
- generic support belongs here, not inside suite-local copies
- suite-local support should remain only where the logic is truly specific to
  one module

### Suite Layer

Owners:
- `test_v2/arena/`
- `test_v2/lexer/`
- `test_v2/parser/`
- `test_v2/build_model/`
- `test_v2/evaluator/`
- `test_v2/pipeline/`
- `test_v2/codegen/`
- `test_v2/artifact_parity/`

Responsibilities:
- semantic assertions
- suite-local fixtures and golden inputs
- category-specific coverage for the module they own

Non-responsibilities:
- re-owning generic support already assigned to runner, framework, or shared
  support

## Target First-Class Module Set

The target first-class test module set is:

- `arena`
- `lexer`
- `parser`
- `build-model`
- `evaluator`
- `evaluator-integration`
- `pipeline`
- `codegen`
- `artifact-parity`

Current gap:
- none in the planned first-class module set

Target direction:
- `build-model` owns standalone frozen-model semantics directly
- `pipeline` stays focused on cross-layer integration instead of serving as the
  direct home for standalone build-model coverage

## Default Aggregate Policy

The default aggregate command is `./build/nob_test test-v2`.

The runner also defaults to `test-v2` when no command is provided. Aggregate
membership is owned by `src_v2/build/nob_test.c` through each module's
`include_in_aggregate` flag, and the same membership is reused by
`clang-tidy-v2`.

Current default aggregate membership and intent:

- `arena`
  Included because allocator behavior is foundational and cheap enough for every
  smoke pass.

- `lexer`
  Included because tokenization regressions invalidate every higher semantic
  layer.

- `parser`
  Included because AST and diagnostics are a required baseline for all semantic
  suites.

- `build-model`
  Included because frozen-model semantics are now first-class and should fail as
  a direct architectural boundary.

- `evaluator`
  Included because evaluator public execution/report/event behavior is part of
  the core semantic contract.

- `pipeline`
  Included because the evaluator-to-Event-Stream-to-build-model path is the
  canonical cross-layer smoke boundary.

- `codegen`
  Included because generated output and generated-binary execution are part of
  the default end-to-end confidence story.

- `artifact-parity`
  Excluded from `test-v2` because it depends on a real `cmake 3.28.x` binary
  and is intentionally kept as an explicit proof harness outside the default
  smoke tier while P0 parity coverage is still narrow.

- `evaluator-integration`
  Excluded from `test-v2` because it is heavier and more host-sensitive than the
  default smoke tier. It remains a first-class module with explicit commands,
  but it is not part of the default aggregate path.

Guardrails:

- important architectural suites are not silently dropped from `test-v2`
- explicit-only status requires a concrete host-sensitivity or runtime-cost
  reason
- aggregate membership is runner-owned, not suite-owned

## Smoke And CI Shape

The intended execution tiers are:

- default smoke:
  `./build/nob_test`
  `./build/nob_test test-v2`

- aggregate profile variants:
  `./build/nob_test test-v2-san`
  `./build/nob_test test-v2-asan`
  `./build/nob_test test-v2-ubsan`
  `./build/nob_test test-v2-msan`
  `./build/nob_test test-v2-cov`

- explicit heavier module path:
  `./build/nob_test test-evaluator-integration`
  plus the same profile suffix variants when a targeted host-sensitive run is
  needed

Current versioned CI baseline:

- `.github/workflows/evaluator-file-parity.yml` runs result-type-convention
  preflight plus `test-v2` on Linux and Windows
- that workflow therefore validates the default aggregate smoke contract, not
  every explicit-only suite

Runner-owned ergonomics that apply equally to aggregate and explicit commands:

- per-module build orchestration
- isolated run workspaces under `Temp_tests/runs`
- captured stdout/stderr logs
- preserved failed workspaces for debugging
- profile-specific coverage and sanitizer handling

This is the canonical T5 baseline: `test-v2` is the required smoke tier,
explicit-only suites remain first-class but opt-in, and workflow expectations
match that split.
