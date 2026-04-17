# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Build Model Coverage Matrix

Status: Implementation control matrix for the current
`Event_Stream -> Build_Model -> codegen` artifact-preservation boundary.

This document tracks the same audited command universe used by
[evaluator_coverage_matrix.md](../evaluator/evaluator_coverage_matrix.md), but
answers a narrower downstream question:

> Does the frozen `Build_Model` preserve enough command-relevant semantics for a
> generated `nob` to reproduce the same observable artifacts as real CMake?

This document is a command-row audit only. It is **not** the right completion
checklist for "after this, only minor work should remain."

For that stronger question, use
[build_model_closure_matrix.md](./build_model_closure_matrix.md).

## Overview

- Baseline audited universe: the same 136 rows used by
  [evaluator_coverage_matrix.md](../evaluator/evaluator_coverage_matrix.md)
- Build-model architecture source:
  [build_model_architecture.md](./build_model_architecture.md)
- Canonical downstream read surface:
  [build_model_query.md](./build_model_query.md)
- Replay-domain contract:
  [build_model_replay.md](./build_model_replay.md)
- Current supported-backend claim:
  [../codegen/generated_backend_supported_subset.md](../codegen/generated_backend_supported_subset.md)

## Taxonomy

`Artifact Relevant`
- `yes`: this row has artifact-relevant semantics that the build model must
  preserve or explicitly boundary for parity control
- `no`: evaluator-side execution may use this row, but the frozen build model
  is not expected to carry it as a first-class artifact-generation surface

`Primary Build-Model Domain`
- `project`
- `directory`
- `target`
- `build_step`
- `test`
- `install`
- `export`
- `package`
- `cpack`
- `replay`
- `none`

`Build-Model Coverage`
- `canonical`: the frozen model has a canonical downstream domain/query surface
  for the artifact-relevant subset of this row
- `subset`: only a narrower signature/property subset is canonical downstream
- `ownership_only`: the model carries enough downstream ownership to classify
  or reject the row explicitly, but not enough to claim positive parity
- `none`: the build model is not the downstream artifact carrier for this row

`Parity Readiness`
- `yes`: the current supported generated-backend claim includes this row's
  artifact-relevant subset
- `subset`: only a narrower subset of the family is inside the current claim
- `boundary`: explicit downstream boundary or reject remains
- `n/a`: this row is not a downstream artifact carrier for parity purposes

`Proof`
- `direct`: focused build-model/pipeline/codegen/artifact-parity evidence
  exists for this row
- `indirect`: the row is covered through family-level or downstream subset
  evidence
- `none`: no stable command-row proof is tracked here yet

Control rules:
- An artifact-relevant row is downstream-sufficient only when
  `Build-Model Coverage` is `canonical` or `subset` and `Parity Readiness` is
  `yes` or `subset`.
- `ownership_only` is intentionally visible: it means the row has explicit
  downstream ownership, but the current product claim still stops at
  boundary/reject behavior.
- `subset` is used for command families such as `file`, `FetchContent_*`,
  `ctest_*`, and generic property-setting commands where only part of the full
  command family is inside the current supported claim.

## Audit Matrix

| Command | Kind | Artifact Relevant | Primary Build-Model Domain | Build-Model Coverage | Parity Readiness | Proof |
|---|---|---|---|---|---|---|
| `FetchContent_Declare` | native | `no` | `none` | `none` | `n/a` | `none` |
| `FetchContent_MakeAvailable` | native | `yes` | `replay` | `subset` | `subset` | `direct` |
| `cmake_host_system_information` | native | `no` | `none` | `none` | `n/a` | `none` |
| `cmake_path` | native | `no` | `none` | `none` | `n/a` | `none` |
| `export` | native | `yes` | `export` | `canonical` | `yes` | `direct` |
| `file` | native | `yes` | `replay` | `subset` | `subset` | `direct` |
| `get_cmake_property` | native | `no` | `none` | `none` | `n/a` | `none` |
| `get_directory_property` | native | `no` | `none` | `none` | `n/a` | `none` |
| `get_filename_component` | native | `no` | `none` | `none` | `n/a` | `none` |
| `get_property` | native | `no` | `none` | `none` | `n/a` | `none` |
| `get_target_property` | native | `no` | `none` | `none` | `n/a` | `none` |
| `install` | native | `yes` | `install` | `canonical` | `yes` | `direct` |
| `list` | native | `no` | `none` | `none` | `n/a` | `none` |
| `string` | native | `no` | `none` | `none` | `n/a` | `none` |
| `add_compile_options` | native | `yes` | `directory` | `canonical` | `yes` | `direct` |
| `add_custom_command` | native | `yes` | `build_step` | `canonical` | `yes` | `direct` |
| `add_custom_target` | native | `yes` | `build_step` | `canonical` | `yes` | `direct` |
| `add_definitions` | native | `yes` | `directory` | `canonical` | `yes` | `direct` |
| `add_dependencies` | native | `yes` | `target` | `canonical` | `yes` | `indirect` |
| `add_executable` | native | `yes` | `target` | `canonical` | `yes` | `direct` |
| `add_library` | native | `yes` | `target` | `canonical` | `yes` | `direct` |
| `add_link_options` | native | `yes` | `directory` | `canonical` | `yes` | `direct` |
| `add_subdirectory` | native | `yes` | `directory` | `canonical` | `yes` | `indirect` |
| `add_test` | native | `yes` | `test` | `canonical` | `yes` | `direct` |
| `block` | native | `no` | `none` | `none` | `n/a` | `none` |
| `break` | native | `no` | `none` | `none` | `n/a` | `none` |
| `cmake_minimum_required` | native | `no` | `none` | `none` | `n/a` | `none` |
| `cmake_policy` | native | `no` | `none` | `none` | `n/a` | `none` |
| `configure_file` | native | `yes` | `replay` | `canonical` | `yes` | `direct` |
| `continue` | native | `no` | `none` | `none` | `n/a` | `none` |
| `cpack_add_component` | native | `yes` | `cpack` | `canonical` | `yes` | `indirect` |
| `cpack_add_component_group` | native | `yes` | `cpack` | `canonical` | `yes` | `indirect` |
| `cpack_add_install_type` | native | `yes` | `cpack` | `canonical` | `yes` | `indirect` |
| `else` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `elseif` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `enable_testing` | native | `yes` | `test` | `canonical` | `yes` | `indirect` |
| `endblock` | native | `no` | `none` | `none` | `n/a` | `none` |
| `endforeach` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `endfunction` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `endif` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `endmacro` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `endwhile` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `execute_process` | native | `yes` | `replay` | `ownership_only` | `boundary` | `indirect` |
| `find_file` | native | `no` | `none` | `none` | `n/a` | `none` |
| `find_library` | native | `no` | `none` | `none` | `n/a` | `none` |
| `find_package` | native | `yes` | `package` | `canonical` | `yes` | `direct` |
| `find_path` | native | `no` | `none` | `none` | `n/a` | `none` |
| `find_program` | native | `no` | `none` | `none` | `n/a` | `none` |
| `foreach` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `function` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `if` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `include` | native | `no` | `none` | `none` | `n/a` | `none` |
| `include_directories` | native | `yes` | `directory` | `canonical` | `yes` | `indirect` |
| `include_guard` | native | `no` | `none` | `none` | `n/a` | `none` |
| `link_directories` | native | `yes` | `directory` | `canonical` | `yes` | `indirect` |
| `link_libraries` | native | `yes` | `target/directory` | `subset` | `subset` | `direct` |
| `macro` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `mark_as_advanced` | native | `no` | `none` | `none` | `n/a` | `none` |
| `math` | native | `no` | `none` | `none` | `n/a` | `none` |
| `message` | native | `no` | `none` | `none` | `n/a` | `none` |
| `option` | native | `no` | `none` | `none` | `n/a` | `none` |
| `project` | native | `yes` | `project` | `canonical` | `yes` | `indirect` |
| `return` | native | `no` | `none` | `none` | `n/a` | `none` |
| `set` | native | `no` | `none` | `none` | `n/a` | `none` |
| `set_property` | native | `yes` | `target/directory/test` | `subset` | `subset` | `direct` |
| `set_target_properties` | native | `yes` | `target` | `subset` | `subset` | `direct` |
| `source_group` | native | `no` | `none` | `none` | `n/a` | `none` |
| `target_compile_definitions` | native | `yes` | `target` | `canonical` | `yes` | `direct` |
| `target_compile_options` | native | `yes` | `target` | `canonical` | `yes` | `direct` |
| `target_include_directories` | native | `yes` | `target` | `canonical` | `yes` | `indirect` |
| `target_link_directories` | native | `yes` | `target` | `canonical` | `yes` | `indirect` |
| `target_link_libraries` | native | `yes` | `target` | `canonical` | `yes` | `direct` |
| `target_link_options` | native | `yes` | `target` | `canonical` | `yes` | `indirect` |
| `try_compile` | native | `yes` | `replay` | `ownership_only` | `boundary` | `indirect` |
| `unset` | native | `no` | `none` | `none` | `n/a` | `none` |
| `while` | structural | `no` | `none` | `none` | `n/a` | `none` |
| `FetchContent_GetProperties` | native | `no` | `none` | `none` | `n/a` | `none` |
| `FetchContent_Populate` | native | `yes` | `replay` | `subset` | `subset` | `direct` |
| `FetchContent_SetPopulated` | native | `no` | `none` | `none` | `n/a` | `none` |
| `add_compile_definitions` | native | `yes` | `directory` | `canonical` | `yes` | `indirect` |
| `aux_source_directory` | native | `no` | `none` | `none` | `n/a` | `none` |
| `build_command` | native | `no` | `none` | `none` | `n/a` | `none` |
| `build_name` | native | `no` | `none` | `none` | `n/a` | `none` |
| `cmake_file_api` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `cmake_language` | native | `no` | `none` | `none` | `n/a` | `none` |
| `cmake_parse_arguments` | native | `no` | `none` | `none` | `n/a` | `none` |
| `create_test_sourcelist` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `ctest_build` | native | `yes` | `replay` | `subset` | `subset` | `indirect` |
| `ctest_configure` | native | `yes` | `replay` | `subset` | `subset` | `indirect` |
| `ctest_coverage` | native | `yes` | `replay` | `canonical` | `yes` | `direct` |
| `ctest_empty_binary_directory` | native | `yes` | `replay` | `canonical` | `yes` | `indirect` |
| `ctest_memcheck` | native | `yes` | `replay` | `canonical` | `yes` | `direct` |
| `ctest_read_custom_files` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `ctest_run_script` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `ctest_sleep` | native | `yes` | `replay` | `canonical` | `yes` | `indirect` |
| `ctest_start` | native | `yes` | `replay` | `subset` | `subset` | `indirect` |
| `ctest_submit` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `ctest_test` | native | `yes` | `replay` | `canonical` | `yes` | `indirect` |
| `ctest_update` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `ctest_upload` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `define_property` | native | `no` | `none` | `none` | `n/a` | `none` |
| `enable_language` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `exec_program` | native | `yes` | `replay` | `ownership_only` | `boundary` | `indirect` |
| `export_library_dependencies` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `fltk_wrap_ui` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `get_source_file_property` | native | `no` | `none` | `none` | `n/a` | `none` |
| `get_test_property` | native | `no` | `none` | `none` | `n/a` | `none` |
| `include_external_msproject` | native | `yes` | `target` | `ownership_only` | `boundary` | `indirect` |
| `include_regular_expression` | native | `no` | `none` | `none` | `n/a` | `none` |
| `install_files` | native | `yes` | `install` | `canonical` | `yes` | `indirect` |
| `install_programs` | native | `yes` | `install` | `canonical` | `yes` | `indirect` |
| `install_targets` | native | `yes` | `install` | `canonical` | `yes` | `indirect` |
| `load_cache` | native | `no` | `none` | `none` | `n/a` | `none` |
| `load_command` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `make_directory` | native | `yes` | `replay` | `canonical` | `yes` | `direct` |
| `output_required_files` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `qt_wrap_cpp` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `qt_wrap_ui` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `remove` | native | `no` | `none` | `none` | `n/a` | `none` |
| `remove_definitions` | native | `yes` | `directory` | `canonical` | `yes` | `indirect` |
| `separate_arguments` | native | `no` | `none` | `none` | `n/a` | `none` |
| `set_directory_properties` | native | `yes` | `directory` | `subset` | `subset` | `indirect` |
| `set_source_files_properties` | native | `yes` | `target/build_step` | `canonical` | `yes` | `direct` |
| `set_tests_properties` | native | `yes` | `test` | `subset` | `subset` | `indirect` |
| `site_name` | native | `no` | `none` | `none` | `n/a` | `none` |
| `subdir_depends` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `subdirs` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `target_compile_features` | native | `yes` | `target` | `canonical` | `yes` | `direct` |
| `target_precompile_headers` | native | `yes` | `target` | `ownership_only` | `boundary` | `indirect` |
| `target_sources` | native | `yes` | `target` | `canonical` | `yes` | `direct` |
| `try_run` | native | `yes` | `replay` | `ownership_only` | `boundary` | `indirect` |
| `use_mangled_mesa` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `utility_source` | native | `yes` | `none` | `none` | `boundary` | `none` |
| `variable_requires` | native | `no` | `none` | `none` | `n/a` | `none` |
| `variable_watch` | native | `no` | `none` | `none` | `n/a` | `none` |
| `write_file` | native | `yes` | `replay` | `canonical` | `yes` | `direct` |

## Notes

- This matrix is the downstream control document for artifact-preserving
  `Build_Model` ownership.
- It is stricter than the Event IR matrix: a row can be well-typed in Event IR
  and still remain `ownership_only`, `subset`, or `boundary` here.
- `Artifact Relevant = no` does not mean the evaluator command is unimportant.
  It means the command's contribution is expected to be collapsed upstream
  before `Build_Model` freeze.
- Family rows such as `file`, `FetchContent_*`, `ctest_*`, `set_property`, and
  `set_*_properties` intentionally summarize the current supported subset rather
  than claim full command-family closure.

## Related Docs

- [Build model closure matrix](./build_model_closure_matrix.md)
- [Build model architecture](./build_model_architecture.md)
- [Build model query](./build_model_query.md)
- [Build model replay domain](./build_model_replay.md)
- [Generated backend supported subset](../codegen/generated_backend_supported_subset.md)
- [Event IR coverage matrix](../transpiler/event_ir_coverage_matrix.md)
- [Evaluator coverage matrix](../evaluator/evaluator_coverage_matrix.md)
