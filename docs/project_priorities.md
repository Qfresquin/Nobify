# Project Priorities

## Status
Canonical product direction. Target state with an explicit transition backlog.

## Role
This file defines what success means for Nobify and how conflicting local goals
must be resolved.

## Product direction
The official goal is total parity with `CMake 3.28` when transpiling to Nob.
Success is measured by the same observable artifacts, not by approximate
semantic similarity or by preserving CMake internals one-for-one.

## Current gap
Current implementation evidence still clusters around a supported-subset
framing, and some documents still carry the mistaken `CMake 3.8` baseline.
That language remains useful only as a description of the gap between the
target product and the code that exists today.

## Guarantees
- Artifact equivalence is the top-level definition of correctness.
- Evaluator, Event IR, build model, and codegen are judged by whether they
  preserve artifact-relevant semantics without downstream guesswork.
- Internal simplifications are acceptable when they do not change observable
  outputs.

## Non-goals
- Treating `supported subset` as the final product identity.
- Preserving the mistaken `3.8` baseline in docs, contracts, or tests.

## Primary code
- `src_v2/evaluator/`
- `src_v2/transpiler/event_ir.h`
- `src_v2/build_model/`
- `src_v2/codegen/`

## Primary tests
- `test_v2/artifact_parity/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/build_model/`
- `test_v2/codegen/`

## Priority order
1. Reach `CMake 3.28` artifact parity for the generated Nob output.
2. Freeze enough typed semantics upstream to avoid downstream reconstruction by
   string heuristics.
3. Keep the generated Nob side deterministic, debuggable, and faithful to the
   frozen model.
4. Demote subset-era wording and stale `3.8` references to transition debt
   only.

## Alignment backlog
- Remove the stale `3.8` baseline typo from active docs and parity-oriented
  test narratives.
- Reclassify subset-style suites as progress indicators toward full parity,
  not as the final product boundary.
- Tighten the evaluator to Event IR contract wherever artifact-relevant
  semantics still collapse into plain strings.
- Tighten the build-model query layer wherever effective accessors still infer
  dependencies or usage meaning late.
- Reframe backend closure docs and rejection behavior around transition status
  inside a full-parity goal.
