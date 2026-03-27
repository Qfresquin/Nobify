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

## Canonical Documents

- [Tests architecture](./tests_architecture.md)
- [Tests structural refactor plan](./tests_structural_refactor_plan.md)

- `tests_architecture.md` is the canonical baseline for ownership boundaries,
  suite taxonomy, aggregate/CI policy, and preserved runner behavior.
- `tests_structural_refactor_plan.md` is the active roadmap for changing that
  architecture in waves without reopening product-level semantic contracts.
