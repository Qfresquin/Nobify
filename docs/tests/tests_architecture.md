# Tests Architecture

## Status

This document defines the current baseline architecture and suite taxonomy for
the v2 test stack.

As of March 27, 2026:
- the official test entrypoint is `./build/nob_test`
- the runner owns module selection, build profiles, incremental compilation,
  workspace roots, captured logs, and aggregate execution
- the generic framework under `test_v2/` owns per-suite and per-case lifecycle
- generic support is only partially consolidated; some support is still
  duplicated or trapped inside suite-local helpers
- `build-model` is not yet a first-class runner module and remains the main gap
  in the target module set

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

- `evaluator`
  Focus: evaluator public execution/report/event behavior across command
  families. This is structurally a semantic integration suite even though parts
  of its support stack still rely on implementation-private helpers.
  Current aggregate status: included.

- `pipeline`
  Focus: evaluator-to-Event-Stream-to-build-model integration and frozen-model
  semantics.
  Current aggregate status: included.

### Host/Integration Suites

- `codegen`
  Focus: generated `nob` output, host compiler execution, and generated-binary
  behavior. This suite crosses semantic and host/toolchain boundaries.
  Current aggregate status: included.

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
- suite-local support files such as:
  - `test_v2/evaluator/test_evaluator_v2_support.*`
  - `test_v2/codegen/test_codegen_v2_support.*`

Current state:
- the shared-support boundary is incomplete
- some generic helpers are already centralized
- text IO, newline normalization, golden flow, snapshot formatting, host
  fixtures, and semantic pipeline setup are still spread across several suites

Target direction:
- generic support belongs here, not inside suite-local copies
- suite-local support should remain only where the logic is truly specific to
  one module

### Suite Layer

Owners:
- `test_v2/arena/`
- `test_v2/lexer/`
- `test_v2/parser/`
- `test_v2/evaluator/`
- `test_v2/pipeline/`
- `test_v2/codegen/`

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

Current gap:
- `build-model` does not yet exist as a standalone runner module

Target direction:
- `build-model` becomes a first-class module with its own suite ownership and
  planned aggregate participation
- `pipeline` stays focused on cross-layer integration instead of serving as the
  only direct home for standalone build-model coverage
- aggregate policy beyond the current baseline is intentionally deferred to the
  refactor plan's later aggregate-policy wave
