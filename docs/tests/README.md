# Tests Documentation Map

## Status

This directory contains the canonical baseline and active structural roadmap for
the v2 test stack.

The current test architecture boundary is:

`nob_test runner -> test framework -> shared support -> suites`

This documentation is about **test architecture** only. It does not redefine
lexer, parser, evaluator, build-model, pipeline, or codegen product semantics.

## Preserved Strengths

The current stack already provides important operational behavior that future
refactors must preserve:

- the official `./build/nob_test` runner as the command entrypoint
- isolated suite and per-case workspaces
- sanitizer, coverage, and `clang-tidy` profiles owned by the runner
- incremental test builds and captured stdout/stderr logs
- preserved failed workspaces for debugging

## Artifact Parity Harness

The explicit `P0` artifact-parity harness lives under `test_v2/artifact_parity/`.

- ownership:
  runner-owned tool/env setup in `src_v2/build/nob_test.c`
  suite-owned parity fixtures and manifest assertions in `test_v2/artifact_parity/`
- `P1` proof split:
  `test_v2/codegen/` owns aggregate-safe out-of-source smoke coverage
  `test_v2/artifact_parity/` stays explicit-only and owns real-CMake
  end-to-end parity checks
- required external tools:
  real `cmake 3.28.x`
  sibling `cpack` only for package-phase cases
  runner-provided `CMK2NOB_TEST_NOBIFY_BIN` so the suite uses a freshly built
  `nobify` from the current workspace sources
- execution policy:
  explicit-only via `./build/nob_test test-artifact-parity`
  not part of the default `./build/nob_test test-v2` smoke aggregate

## Canonical Documents

- [Tests architecture](./tests_architecture.md)
- [Tests structural refactor plan](./tests_structural_refactor_plan.md)

- `tests_architecture.md` is the canonical baseline for ownership boundaries,
  suite taxonomy, aggregate/CI policy, and preserved runner behavior.
- `tests_structural_refactor_plan.md` is the active roadmap for changing that
  architecture in waves without reopening product-level semantic contracts.
