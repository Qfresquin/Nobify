# Pipeline Overview

## Status
Canonical transition contract for the end-to-end `CMake 3.28 -> Nob` pipeline.

## Role
This document defines the product pipeline and the ownership boundary between
its major stages.

## Product direction
The pipeline exists to preserve enough `CMake 3.28` semantics that the
generated
Nob program produces the same observable artifacts as the input project.

## Current gap
Some downstream stages still compensate for incomplete upstream typing, and
parts of the evidence stack are still named and scoped around subset-closure
work instead of total `3.28` parity.

## Guarantees
- `evaluator` executes and freezes artifact-relevant semantics before codegen.
- `Event IR` carries ordered, consumer-safe information out of evaluation.
- `build_model` converts that stream into a frozen graph that codegen can query
  without re-running CMake logic.
- `codegen` emits Nob using only the frozen model and replay data.

## Non-goals
- Treating late string inference as a permanent design feature.
- Using the generated backend as a second evaluator.

## Primary code
- `src_v2/evaluator/evaluator.c`
- `src_v2/transpiler/event_ir.h`
- `src_v2/build_model/build_model_builder.c`
- `src_v2/build_model/build_model_freeze.c`
- `src_v2/codegen/nob_codegen.c`

## Primary tests
- `test_v2/pipeline/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/build_model/`
- `test_v2/codegen/`
- `test_v2/artifact_parity/`

## Flow
1. Parse and evaluate the input project.
2. Project ordered evaluation facts into Event IR.
3. Build, validate, and freeze the build model.
4. Generate Nob code and runtime helpers from the frozen model.
5. Compare generated behavior and artifacts against the source project.
