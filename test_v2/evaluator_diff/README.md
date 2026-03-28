# Evaluator Differential Harness

This suite compares evaluator-visible behavior against a real `cmake 3.28.x`
binary.

## How to Run

- Default command: `./build/nob_test test-evaluator-diff`
- Optional override: set `CMK2NOB_TEST_CMAKE_BIN=/abs/path/to/cmake`
- Resolution order:
  - `CMK2NOB_TEST_CMAKE_BIN`
  - `cmake` from `PATH`
- Version policy:
  - only `cmake 3.28.x` is accepted
  - if CMake is missing or incompatible, the suite skips instead of failing

## Current Scope

V1 is intentionally narrow:

- `project-mode` only
- seed case packs for `target_*`, `list()`, `var_commands`, and `property_query`
- real CMake is the oracle
- success cases compare full normalized snapshots
- error cases compare normalized outcome only

The suite is explicit-only and does not participate in the default `test-v2`
aggregate.

## Case-Pack DSL

Cases live under `test_v2/evaluator_diff/cases/*.cmake`.

Current case packs:

- `target_usage_seed_cases.cmake`
- `list_seed_cases.cmake`
- `var_commands_seed_cases.cmake`
- `property_query_seed_cases.cmake`

Supported directives:

- `#@@CASE <name>`
- `#@@OUTCOME SUCCESS|ERROR`
- `#@@FILE <relpath>`
- `#@@DIR <relpath>`
- `#@@QUERY VAR <name>`
- `#@@QUERY CACHE_DEFINED <name>`
- `#@@QUERY TARGET_EXISTS <target>`
- `#@@QUERY TARGET_PROP <target> <property>`
- `#@@QUERY FILE_EXISTS <path>`

The case body is treated as a body-only project script. The harness generates a
complete `CMakeLists.txt` with:

- `cmake_minimum_required(VERSION 3.28)`
- `project(DiffCase LANGUAGES C CXX)`
- the case body
- an automatically generated probe block

## Snapshot Format

Success cases normalize and compare `diff_snapshot.txt` with stable ordered
lines:

- `OUTCOME=<SUCCESS|ERROR>`
- `VAR:<name>=<value|__UNDEFINED__>`
- `CACHE_DEFINED:<name>=0|1`
- `TARGET_EXISTS:<target>=0|1`
- `TARGET_PROP:<target>:<prop>=<value|__UNSET__|__MISSING_TARGET__>`
- `FILE_EXISTS:<path>=0|1`

## Failure Artifacts

On mismatch, the harness preserves:

- generated `CMakeLists.txt`
- evaluator snapshot
- CMake snapshot
- CMake stdout/stderr
- case summary

Artifacts are copied under the preserved run workspace in `__diff_failures/`.
