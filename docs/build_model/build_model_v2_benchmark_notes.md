# Build Model v2 Benchmark Notes

## Goal
Track hot-path cost for:
- `builder_apply_stream(...)`
- `build_model_validate(...)`

## Current Baseline Process
Use the existing `test-v2` pipeline module as a repeatable workload:
1. Run `./build/nob_v2_test.exe test-pipeline` 5x.
2. Record wall-clock mean/p95 externally.
3. Compare before/after refactors.

## Why this document exists
The v2 code path now avoids heuristic dependency inference in validation and adds directory scope events.
This note defines a stable measurement procedure without blocking functional delivery.
