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
- seed case packs for `target_*`, `list()`, `var_commands`, `property_query`,
  `cmake_path()`, `get_filename_component()`, `math()`, `add_executable()`/`add_library()`,
  `add_subdirectory()`, `string()`, `project()`/`cmake_minimum_required()`/`cmake_policy()`,
  `message()`, `configure_file()`, and direct property-wrapper coverage
- real CMake is the oracle
- success cases compare full normalized snapshots
- error cases compare normalized outcome plus any opt-in post-run observations

The suite is explicit-only and does not participate in the default `test-v2`
aggregate.

## Case-Pack DSL

Cases live under `test_v2/evaluator_diff/cases/*.cmake`.

Current case packs:

- `target_usage_seed_cases.cmake`
- `list_seed_cases.cmake`
- `var_commands_seed_cases.cmake`
- `property_query_seed_cases.cmake`
- `cmake_path_seed_cases.cmake`
- `get_filename_component_seed_cases.cmake`
- `math_seed_cases.cmake`
- `add_targets_seed_cases.cmake`
- `add_subdirectory_seed_cases.cmake`
- `string_seed_cases.cmake`
- `top_level_project_seed_cases.cmake`
- `message_seed_cases.cmake`
- `configure_file_seed_cases.cmake`
- `property_wrappers_seed_cases.cmake`

Supported directives:

- `#@@CASE <name>`
- `#@@OUTCOME SUCCESS|ERROR`
- `#@@PROJECT_LAYOUT BODY_ONLY_PROJECT|RAW_CMAKELISTS`
- `#@@FILE <relpath>`
- `#@@DIR <relpath>`
- `#@@FILE_TEXT <relpath>` ... `#@@END_FILE_TEXT`
- `#@@ENV <NAME>=<value>`
- `#@@ENV_UNSET <NAME>`
- `#@@CACHE_INIT <NAME>:<TYPE>=<value>`
- `#@@QUERY VAR <name>`
- `#@@QUERY CACHE_DEFINED <name>`
- `#@@QUERY TARGET_EXISTS <target>`
- `#@@QUERY TARGET_PROP <target> <property>`
- `#@@QUERY FILE_EXISTS <path>`
- `#@@QUERY STDOUT`
- `#@@QUERY STDERR`
- `#@@QUERY FILE_TEXT <path>`
- `#@@QUERY CMAKE_PROP <property>`
- `#@@QUERY GLOBAL_PROP <property>`
- `#@@QUERY DIR_PROP <dir> <property>`

By default, the case body is treated as a body-only project script. The harness
generates a complete `CMakeLists.txt` with:

- `cmake_minimum_required(VERSION 3.28)`
- `project(DiffCase LANGUAGES C CXX)`
- the case body
- an automatically generated probe block

If `#@@PROJECT_LAYOUT RAW_CMAKELISTS` is used, the body becomes the full
`CMakeLists.txt` and the harness appends only the probe block.

Fixture path scope:

- `source/<rel>` writes under the case source tree
- `build/<rel>` writes under both evaluator and CMake build trees
- bare fixture paths default to the source tree for compatibility

## Snapshot Format

Success cases normalize and compare `diff_snapshot.txt` with stable ordered
lines:

- `OUTCOME=<SUCCESS|ERROR>`
- `VAR:<name>=<value|__UNDEFINED__>`
- `CACHE_DEFINED:<name>=0|1`
- `TARGET_EXISTS:<target>=0|1`
- `TARGET_PROP:<target>:<prop>=<value|__UNSET__|__MISSING_TARGET__>`
- `FILE_EXISTS:<path>=0|1`
- `CMAKE_PROP:<prop>=<value|__UNSET__>`
- `GLOBAL_PROP:<prop>=<value|__UNSET__>`
- `DIR_PROP:<dir>:<prop>=<value|__UNSET__|__MISSING_DIR__>`
- `STDOUT_B64=<base64>`
- `STDERR_B64=<base64>`
- `FILE_TEXT_B64:<path>=<base64|__MISSING_FILE__>`

## Failure Artifacts

On mismatch, the harness preserves:

- generated `CMakeLists.txt`
- evaluator snapshot
- CMake snapshot
- evaluator stdout/stderr
- evaluator nob log
- CMake stdout/stderr
- case summary

Artifacts are copied under the preserved run workspace in `__diff_failures/`.
