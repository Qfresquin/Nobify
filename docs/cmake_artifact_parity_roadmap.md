# CMake Artifact Parity Roadmap

Status: Historical parity-program summary and handoff.

## Role

This document is the root summary for the delivered artifact-parity program.
It keeps parity scope and evidence expectations visible without duplicating the
full historical wave log in the active documentation path.

## Boundary

The parity program preserved this architecture:

`AST -> evaluator -> Event IR -> build_model -> codegen/backend -> artifacts`

The document does not redefine subsystem contracts under `docs/evaluator/`,
`docs/transpiler/`, `docs/build_model/`, `docs/codegen/`, or `docs/tests/`.

## Data Flow

Historical parity proof layered evidence across:

1. subsystem behavior tests (`evaluator`, `pipeline`, `build-model`, `codegen`)
2. explicit CMake-vs-generated-Nob artifact diffs (`artifact-parity`)
3. real-project hardening and support-boundary classification

## Public Contract

- CMake 3.28 remains the primary compatibility baseline.
- Unsupported behavior must fail explicitly and diagnosably.
- Future closure work for evaluator-to-codegen backend completion is owned by:
  [`evaluator_codegen_closure_roadmap.md`](./evaluator_codegen_closure_roadmap.md)
- Full delivered wave detail (`P0`..`P8`) is archived at:
  [`archive/roadmaps/cmake_artifact_parity_p0_p8_history.md`](./archive/roadmaps/cmake_artifact_parity_p0_p8_history.md)

## Non-goals

- This summary is not the owner of new backend-closure waves.
- This summary is not a replacement for subsystem normative contracts.
- This summary does not duplicate the full historical backlog narrative.

## Evidence

Current parity and closure evidence is tracked by:

- [`tests/tests_architecture.md`](./tests/tests_architecture.md)
- [`tests/evaluator_codegen_diff.md`](./tests/evaluator_codegen_diff.md)
- explicit host suites (`test-artifact-parity`, `test-evaluator-codegen-diff`)
