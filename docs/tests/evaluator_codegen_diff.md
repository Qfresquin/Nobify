# Evaluator To Codegen Diff

## Status
Canonical transition contract for the evaluator-to-codegen bridge suite.

## Role
This suite checks whether evaluator output, Event IR, build-model projection,
and generated code stay aligned enough to preserve artifact-relevant behavior.

## Product direction
The suite exists to measure progress toward `CMake 3.8` artifact parity, not to
freeze an older supported-subset target.

## Current gap
The current inventory and naming still come from the `3.28` closure era, and
some cases prove downstream consistency without yet proving complete artifact
equivalence on their own.

## Guarantees
- Cases should fail when downstream stages invent or drop semantics relative to
  evaluator output.
- Inventory growth should track artifact-relevant capability, not only internal
  implementation milestones.
- Promotion into stronger parity evidence should happen as real-project and
  end-to-end coverage improves.

## Non-goals
- Treating this suite as a full replacement for artifact-parity coverage.
- Treating current inventory limits as the final product boundary.

## Primary code
- `test_v2/evaluator_codegen_diff/`
- `src_v2/transpiler/event_ir.h`
- `src_v2/build_model/build_model_query.c`
- `src_v2/build_model/build_model_query_effective.c`
- `src_v2/codegen/`

## Primary tests
- `test_v2/evaluator_codegen_diff/`
- `test_v2/build_model/`
- `test_v2/codegen/`
- `test_v2/artifact_parity/`

## Evidence rule
Use this suite to catch semantic drift between evaluator and generated-backend
consumers early, then confirm artifact parity with stronger end-to-end suites.
