# Build Model Replay Domain

## Status
Canonical transition contract for deferred and replayable build-model actions.

## Role
The replay domain captures artifact-relevant work that the generated Nob side
must reproduce later without re-evaluating the original CMake project.

## Product direction
Replay data must be explicit enough that generated Nob can materialize the same
observable artifacts expected from `CMake 3.28` input.

## Current gap
Replay coverage is still shaped by backend-closure work and by tests written
around subset-closure framing. Some surfaces still need better typing and
stronger evidence.

## Guarantees
- Replay entities are frozen with the rest of the build model.
- Codegen consumes replay data without reaching back into evaluator internals.
- Artifact-relevant side effects should be represented explicitly, not hidden in
  opaque command strings whenever structured data is available.

## Non-goals
- Treating replay as a catch-all bucket for semantics that were lost upstream.
- Making codegen infer undeclared inputs, tools, or outputs.

## Primary code
- `src_v2/build_model/build_model_builder_replay.c`
- `src_v2/build_model/build_model.h`
- `src_v2/codegen/nob_codegen_replay.c`

## Primary tests
- `test_v2/codegen/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/artifact_parity/`

## Boundary
Replay stores deferred actions for later generated execution. It is part of the
frozen model, not a second chance to run evaluator logic.
