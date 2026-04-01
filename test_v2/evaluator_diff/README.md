# Evaluator Differential Harness

This suite compares evaluator-visible behavior against a real `cmake 3.28.x`
binary.

## How to Run

- Default command: `./build/nob_test test-evaluator-diff`
- Optional override: set `CMK2NOB_TEST_CMAKE_BIN=/abs/path/to/cmake`
- Optional report: set `NOB_DIFF_STATUS_OUT=/abs/path/report.md`
- Resolution order:
  - `CMK2NOB_TEST_CMAKE_BIN`
  - `cmake` from `PATH`
- Version policy:
  - only `cmake 3.28.x` is accepted
  - if CMake is missing or incompatible, the suite skips instead of failing

## Current Scope

The harness now supports both `project-mode` and `script-mode`:

- `PROJECT` remains the default case mode
- `SCRIPT` uses `cmake -P` and runs with `source_dir == binary_dir == source`
- seed case packs for `target_*`, `list()`, `var_commands`, `property_query`,
  `cmake_path()`, `get_filename_component()`, `math()`, `add_executable()`/`add_library()`/`add_dependencies()`,
  `add_subdirectory()`, `string()`, `project()`/`cmake_minimum_required()`/`cmake_policy()`,
  `message()`, `configure_file()`, `directory_usage`, `property_setters`,
  `testing_meta`, `argument_parsing`, `find_pathlike`, `host_identity`,
  `cache_loading`, `legacy_generation`, direct property-wrapper coverage,
  plus script-first families for `include()`, `execute_process()`,
  `cmake_language(CALL/EVAL/GET_MESSAGE_LOG_LEVEL/DEFER)`,
  dependency-provider paths via `cmake_language(SET_DEPENDENCY_PROVIDER ...)`,
  script-snapshot `file()`, `cmake_policy()`, and script-mode
  `configure_file()`
- deterministic host-effect families for:
  - `install()`
  - `export()`
  - complex local `file()` effects
  - local deterministic `FetchContent_*`
- special-oracle families for:
  - `find_package()` resolution, redirects, registry, and provider interop
  - `try_compile()` and `try_run()`
  - `ctest_*`
  - meta/graph families including `cmake_file_api`, `add_custom_command()`,
    `add_custom_target()`, `source_group()`,
    `include_external_msproject()`, `output_required_files()`, `subdirs()`,
    and `subdir_depends()`
- structural/policy families for:
  - `flow_control_structural`
    - `if()`
    - `foreach()`
    - `while()`
    - `break()`
    - `continue()`
    - `block()`
  - `callable_scope_structural`
    - `function()`
    - `macro()`
    - `return()`
  - `structural_policy_compat`
    - `CMP0124`
    - `CMP0140`
    - structural `if(POLICY ...)`
    - policy-scope interactions through `include()`,
      `cmake_minimum_required()`, and `cmake_policy()`
- `find_pathlike` now also covers local `ENV`, `PATH`, and prefix-root driven
  search flows under isolated workspace control
- real CMake is the oracle
- success cases compare full normalized snapshots
- error cases compare normalized outcome plus any opt-in post-run observations

The suite is explicit-only and does not participate in the default `test-v2`
aggregate.

## Lane Families

The coverage matrix is the source of truth for exact `row -> lane -> owner`
classification. This README only summarizes the current lane families:

- `snapshot differential`
  - `target_usage`, `list`, `var_commands`, `property_query`,
    `property_wrappers`, `cmake_path`, `get_filename_component`, `math`,
    `add_targets`, `add_subdirectory`, `string`, `top_level_project`,
    `message`, `configure_file`, `directory_usage`, `property_setters`,
    `testing_meta`, `argument_parsing`, `host_identity`, `cache_loading`,
    `legacy_generation`, `include_script`, `execute_process_script`,
    `cmake_language_script`, `cmake_policy_script`,
    `configure_file_script`, `flow_control_structural`,
    `callable_scope_structural`, `structural_policy_compat`
- `host-effect differential`
  - `find_pathlike`, `install_host_effect`, `export_host_effect`,
    `file_host_effect`, `fetchcontent_host_effect`, `legacy_generation`
- `normalized failure differential`
  - documentary owners only in the matrix today:
    `legacy_cpack_failure`, `legacy_loader_failure`,
    `legacy_compat_failure`
- `special oracle lane`
  - `find_package_special`, `try_compile_special`, `try_run_special`,
    `ctest_special`, `file_api_meta_special`, `legacy_meta_special`
  - `enable_language_special` is currently a documentary owner in the matrix,
    not a separate pack yet

## CI Reporting

- GitHub Actions coverage lives in `.github/workflows/evaluator-diff.yml`
- the workflow pins `cmake 3.28.6`
- the suite can publish a per-family Markdown status report through
  `NOB_DIFF_STATUS_OUT`
- the CI job uploads that report as an artifact and copies it into the GitHub
  step summary
- this differential workflow stays separate from the default `test-v2` smoke
  aggregate

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
- `directory_usage_seed_cases.cmake`
- `property_setters_seed_cases.cmake`
- `testing_meta_seed_cases.cmake`
- `argument_parsing_seed_cases.cmake`
- `find_pathlike_seed_cases.cmake`
- `host_identity_seed_cases.cmake`
- `cache_loading_seed_cases.cmake`
- `legacy_generation_seed_cases.cmake`
- `include_seed_cases.cmake`
- `execute_process_seed_cases.cmake`
- `cmake_language_seed_cases.cmake`
- `dependency_provider_seed_cases.cmake`
- `file_script_seed_cases.cmake`
- `cmake_policy_script_seed_cases.cmake`
- `configure_file_script_seed_cases.cmake`
- `install_host_effect_seed_cases.cmake`
- `export_host_effect_seed_cases.cmake`
- `file_host_effect_seed_cases.cmake`
- `fetchcontent_host_effect_seed_cases.cmake`
- `find_package_special_seed_cases.cmake`
- `try_compile_special_seed_cases.cmake`
- `try_run_special_seed_cases.cmake`
- `ctest_special_seed_cases.cmake`
- `file_api_meta_special_seed_cases.cmake`
- `legacy_meta_special_seed_cases.cmake`
- `flow_control_structural_seed_cases.cmake`
- `callable_scope_structural_seed_cases.cmake`
- `structural_policy_compat_seed_cases.cmake`

Supported directives:

- `#@@CASE <name>`
- `#@@MODE PROJECT|SCRIPT`
- `#@@OUTCOME SUCCESS|ERROR`
- `#@@PROJECT_LAYOUT BODY_ONLY_PROJECT|RAW_CMAKELISTS`
- `#@@FILE <relpath>`
- `#@@DIR <relpath>`
- `#@@FILE_TEXT <relpath>` ... `#@@END_FILE_TEXT`
- `#@@ENV <NAME>=<value>`
- `#@@ENV_UNSET <NAME>`
- `#@@ENV_PATH <NAME> <scoped-path>`
- `#@@CACHE_INIT <NAME>:<TYPE>=<value>`
- `#@@QUERY VAR <name>`
- `#@@QUERY CACHE_DEFINED <name>`
- `#@@QUERY TARGET_EXISTS <target>`
- `#@@QUERY TARGET_PROP <target> <property>`
- `#@@QUERY FILE_EXISTS <path>`
- `#@@QUERY STDOUT`
- `#@@QUERY STDERR`
- `#@@QUERY FILE_TEXT <path>`
- `#@@QUERY FILE_SHA256 <path>`
- `#@@QUERY TREE <path>`
- `#@@QUERY CMAKE_PROP <property>`
- `#@@QUERY GLOBAL_PROP <property>`
- `#@@QUERY DIR_PROP <dir> <property>`

`PROJECT` mode:

- by default, the case body is treated as a body-only project script
- the harness generates a complete `CMakeLists.txt` with:
  - `cmake_minimum_required(VERSION 3.28)`
  - `project(DiffCase LANGUAGES C CXX)`
  - the case body
  - an automatically generated probe block

If `#@@PROJECT_LAYOUT RAW_CMAKELISTS` is used, the body becomes the full
`CMakeLists.txt` and the harness appends only the probe block.

`SCRIPT` mode:

- the harness generates `source/diff_script.cmake`
- it does not inject `cmake_minimum_required()` or `project()`
- it appends the same probe block to the script body
- the evaluator runs with `EVAL_EXEC_MODE_SCRIPT`
- real CMake runs as `cmake -P <abs path>/diff_script.cmake`
- the harness temporarily uses the case source directory as cwd on both sides

Fixture path scope:

- `source/<rel>` writes under the case source tree
- `build/<rel>` writes under both evaluator and CMake build trees
- bare fixture paths default to the source tree for compatibility
- `build/<rel>` is rejected in `SCRIPT` mode to avoid ambiguous
  `CMAKE_BINARY_DIR` semantics
- `#@@ENV_PATH` resolves `source/<rel>` or `build/<rel>` to an absolute path
  before each side runs
- `build/<rel>` is also rejected for `#@@ENV_PATH` in `SCRIPT` mode

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
- `FILE_SHA256:<path>=<hex|__MISSING_FILE__|__IS_DIR__>`
- `TREE_B64:<path>=<base64 manifest|__MISSING_PATH__>`

## Special Oracles

Most packs still use the generic normalized snapshot lane. The Phase 4 packs
also use lane-specific oracle reports written under `build/__oracle/`:

- `find_package_special` writes a canonical `find_package_report.txt`
- `try_compile_special` and `try_run_special` write `try_compile_report.txt`
  and `try_run_report.txt`
- `ctest_special` writes `ctest_report.txt` and captures the loopback submit
  server tree under `build/__ctest_server`
- `file_api_meta_special` writes `file_api_meta_report.txt`
- `legacy_meta_special` writes `legacy_meta_report.txt`

These special lanes still reuse the same case-pack DSL and harness execution
model; only the final artifact normalization differs.

## Failure Artifacts

On mismatch, the harness preserves:

- generated `CMakeLists.txt`
- evaluator snapshot
- CMake snapshot
- lane-specific oracle reports under `build/__oracle/`
- evaluator stdout/stderr
- evaluator nob log
- CMake stdout/stderr
- case summary

Artifacts are copied under the preserved run workspace in `__diff_failures/`.
