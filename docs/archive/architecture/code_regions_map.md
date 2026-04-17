# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Code Regions Map

## Status

Canonical communication map for major `src_v2/*` regions in the active
pipeline.

## Role

Show ownership boundaries by code region so contributors know where behavior
belongs.

## Boundary

Region responsibilities map to the same product boundary:

`AST -> evaluator -> Event IR -> build_model -> codegen`

## Data Flow

- `src_v2/app` composes the pipeline and forwards options.
- `src_v2/lexer` and `src_v2/parser` emit AST.
- `src_v2/evaluator` executes semantics and emits Event IR.
- `src_v2/transpiler/event_ir.*` defines stream schema, metadata, and ownership.
- `src_v2/build_model` reconstructs, validates, freezes, and serves queries.
- `src_v2/codegen` emits generated runtime code from query-only access.
- `src_v2/build` + `test_v2` run and classify proof suites.

## Inputs

- parsed AST nodes
- evaluator session/request state
- Event IR items with role metadata
- frozen query calls and codegen options

## Outputs

- committed evaluator semantic state and optional Event IR stream
- immutable `Build_Model` plus typed query responses
- generated `nob.c` and runtime helper graph
- per-suite parity and closure evidence

## Who Consumes

- Maintainers deciding where to land new feature logic
- Engineers triaging regressions by layer ownership
- Reviewers checking boundary violations

## Key Source Regions

- `src_v2/app/nobify.c`
- `src_v2/lexer/`
- `src_v2/parser/`
- `src_v2/evaluator/`
- `src_v2/transpiler/`
- `src_v2/build_model/`
- `src_v2/codegen/`
- `src_v2/build/nob.c`
- `src_v2/build/nob_testd.c`
- `src_v2/build/test_runner_*`
- `test_v2/`

## Failure Ownership

- Wrong AST shape or tokenization: `lexer`/`parser`
- Wrong semantic behavior or projection gates: `evaluator`
- Event-kind taxonomy mismatch: `transpiler/event_ir`
- Missing/rejected downstream representation: `build_model`
- Runtime command or phase mismatch in generated backend: `codegen`
- Classification drift between implemented semantics and backend status:
  `evaluator-codegen-diff` suite
