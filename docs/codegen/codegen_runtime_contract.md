# Codegen Runtime Contract

## Status
Canonical transition contract for generated Nob output.

## Role
Codegen turns the frozen build model into a Nob program and helper runtime that
reproduce source-project artifacts.

## Product direction
The generated backend exists to achieve `CMake 3.8` artifact parity. Internal
implementation differences are acceptable only when observable outputs remain
equivalent.

## Current gap
Backend evidence is still partial, and some rejection rules and test language
come from the earlier `supported subset` era rather than from a final parity
contract.

## Guarantees
- Codegen reads the frozen build model instead of re-running CMake semantics.
- Generated Nob steps should be deterministic and inspectable.
- Differences are allowed in implementation strategy, not in observable
  artifacts.

## Non-goals
- Preserving CMake internals for their own sake.
- Defining the product as a permanently limited backend subset.

## Primary code
- `src_v2/codegen/nob_codegen.c`
- `src_v2/codegen/nob_codegen_runtime.c`
- `src_v2/codegen/nob_codegen_steps.c`
- `src_v2/codegen/nob_codegen_resolve.c`
- `src_v2/codegen/nob_codegen_replay.c`

## Primary tests
- `test_v2/codegen/`
- `test_v2/artifact_parity/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/pipeline/`

## Boundary
Input:
- frozen build model plus replay data

Output:
- generated Nob program, runtime helpers, and build/test/install/package steps
  that aim for the same source-project artifacts
