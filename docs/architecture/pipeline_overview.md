# Pipeline Overview

## Status

Canonical high-level architecture map for the active Nobify product pipeline.

## Role

Explain how semantic recovery and backend generation connect end-to-end for
implementers and reviewers.

## Boundary

Nobify preserves this boundary:

`AST -> evaluator -> Event IR -> build_model -> codegen -> generated nob.c runtime`

No layer may bypass the next contract boundary for feature delivery.

## Data Flow

1. `lexer` and `parser` convert CMake input into AST.
2. `evaluator` executes AST semantics against session state and optionally
   projects `Event_Stream`.
3. `build_model` ingests build-semantic events, validates draft state, freezes
   immutable model state, and exposes query APIs.
4. `codegen` consumes query APIs and emits readable `nob.c` with phase-aware
   helpers.
5. generated backend executes phase commands (`configure/build/test/install/export/package`)
   and emits observable artifacts.

## Inputs

- `CMakeLists.txt` and included CMake scripts
- CLI generation options (`source_root`, `binary_root`, platform/backend policy)
- host tool availability used by explicit parity suites and generated runtime

## Outputs

- `Event_Stream` (optional per evaluator run)
- frozen `Build_Model`
- generated `nob.c`
- runtime build/install/export/package artifacts
- diagnostics and structured test evidence

## Who Consumes

- Feature implementers across evaluator/build-model/codegen
- Test suite maintainers (`pipeline`, `build-model`, `codegen`,
  `artifact-parity`, `evaluator-codegen-diff`)
- Reviewers validating boundary ownership

## Key Source Regions

- `src_v2/app/nobify.c` (orchestrates parse -> evaluate -> model -> codegen)
- `src_v2/parser/*`
- `src_v2/evaluator/*`
- `src_v2/transpiler/event_ir.*`
- `src_v2/build_model/*`
- `src_v2/codegen/*`
- `test_v2/*` and `src_v2/build/nob_test.c`

## Failure Ownership

- Parse/token errors: lexer/parser boundary
- Semantic command or compatibility errors: evaluator boundary
- Unsupported build-semantic ingest or integrity failures: build-model
  builder/validate/freeze
- Unsupported generated backend behavior: codegen validation/runtime rejection
- Cross-layer parity drift: explicit parity and closure suites
