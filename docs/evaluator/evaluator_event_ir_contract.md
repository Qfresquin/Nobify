# Evaluator Event IR Contract

## Status
Canonical transition contract between evaluation and downstream model building.

## Role
This document defines what the evaluator must preserve when projecting its
results into Event IR.

## Product direction
Event IR must carry enough typed, ordered semantics to support `CMake 3.28`
artifact parity without forcing the build model or codegen to rediscover
meaning from raw strings.

## Current gap
Some target usage and configuration-sensitive information still degrade into
textual payloads or generator-expression strings, which leaves the build-model
query layer doing late inference.

## Guarantees
- Event ordering is stable and consumer-visible.
- Artifact-relevant semantics are expected to survive projection.
- Consumers do not re-run control flow or variable evaluation.
- Diagnostics belong upstream; Event IR is not a silent repair channel.

## Non-goals
- Using Event IR as a lossy debug log.
- Treating downstream semantic reconstruction as the intended steady state.

## Primary code
- `src_v2/transpiler/event_ir.h`
- `src_v2/transpiler/event_ir.c`
- `src_v2/evaluator/eval_target_usage.c`

## Primary tests
- `test_v2/evaluator_codegen_diff/`
- `test_v2/build_model/`
- `test_v2/artifact_parity/`

## Required shape
- Events must preserve ordering.
- Payloads that influence dependencies, usage requirements, replay behavior, or
  generated steps should be typed enough that downstream consumers mostly read
  and compose, not reinterpret.
