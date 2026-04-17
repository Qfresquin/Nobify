# Evaluator v2 Specification

## Status
Canonical transition contract for the evaluator.

## Role
The evaluator executes CMake input and freezes artifact-relevant semantics
before downstream stages build the Nob output.

## Product direction
The evaluator targets total parity with `CMake 3.28` as part of an end-to-end
pipeline that must reproduce the same observable artifacts.

## Current gap
The current implementation and supporting evidence still reflect a
subset-first closure program within the `CMake 3.28` surface. Some surfaces
still project artifact-relevant meaning through generic string payloads that
downstream code later reconstructs.

## Guarantees
- Evaluation order is preserved in the projected event stream.
- Unsupported behavior must fail or diagnose at the evaluator boundary rather
  than being silently reinterpreted downstream.
- Directory, target, test, install, package, and replay-relevant semantics must
  be emitted in a form that downstream stages can consume deterministically.

## Non-goals
- Treating string reconstruction in later stages as the desired architecture.
- Defining product success in terms of a permanent supported subset.

## Primary code
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/eval_target_usage.c`
- `src_v2/evaluator/eval_utils.c`

## Primary tests
- `test_v2/evaluator/`
- `test_v2/evaluator_diff/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/artifact_parity/`

## Boundary
Input:
- parsed CMake source plus host/toolchain context

Output:
- ordered Event IR and diagnostics that are sufficient for downstream artifact
  generation without re-executing CMake semantics
