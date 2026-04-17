# Current Backend Closure Within The `CMake 3.28` Parity Goal

## Status
Canonical transition report for backend closure. This file describes the
current implementation shape, not the final product boundary.

## Role
This document records which backend areas are currently closed enough to use
and which ones remain open on the way to full parity.

## Product direction
The official product target is total parity with `CMake 3.28` and the same
observable artifacts. Any current subset language is transition-only.

## Current gap
The backend is still proven unevenly. Some families are usable today, but the
overall system should not be described as a final supported subset.

## Guarantees
- This document reports present closure without redefining the product goal.
- Open areas are stated as parity gaps, not as permanent exclusions.
- Evidence should come from build-model, codegen, diff, and artifact-parity
  suites.

## Non-goals
- Marketing the current closure as the final scope of Nobify.
- Treating unimplemented areas as out of scope forever.

## Primary code
- `src_v2/build_model/`
- `src_v2/codegen/`
- `src_v2/transpiler/event_ir.h`

## Primary tests
- `test_v2/codegen/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/artifact_parity/`

## Current closure snapshot
| Area | Current state | Gap to full parity |
| --- | --- | --- |
| Build graph emission | Core build/codegen suites exercise the main path | Still inherits subset-era rejection and missing edge typing |
| Target usage and dependencies | Partially reconstructed through build-model effective queries | Needs more typed upstream semantics and less late inference |
| Replay, install, export, package | Present as explicit domains and runtime surfaces | Evidence is thinner than the final parity target |
| Test and CTest-like behavior | Dedicated evaluator and codegen coverage exists | Harness and naming still carry subset-era debt |
| Real-project parity | Artifact-parity corpus exists | Corpus breadth is still smaller than the final claim |
