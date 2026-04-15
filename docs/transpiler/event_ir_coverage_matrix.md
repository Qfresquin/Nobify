# Event IR Coverage Matrix

Status: Implementation control matrix for the current `evaluator -> Event IR`
boundary.

This document tracks the audited command universe from the evaluator matrix,
but from the narrower perspective of Event IR ownership.
- one row per audited command or structural node
- no CMake command description column
- no downstream codegen/build-model status
- only the minimum fields needed to control what Event IR has or has not
  already frozen

## Overview

- Baseline audited universe: the same 136 rows used by
  [evaluator_coverage_matrix.md](../evaluator/evaluator_coverage_matrix.md)
- Event IR schema source:
  [`src_v2/transpiler/event_ir.h`](/home/pedro/Nobify/src_v2/transpiler/event_ir.h)
- Normative Event IR contract:
  [event_ir_v2_spec.md](./event_ir_v2_spec.md)
- Producer boundary contract:
  [../evaluator/evaluator_event_ir_contract.md](../evaluator/evaluator_event_ir_contract.md)

Current schema snapshot:

| Metric | Value |
|---|---:|
| Event families | 21 |
| Event kinds | 103 |
| Replay opcodes | 23 |
| Audited command/structural rows | 136 |

## Status Taxonomy

`Primary Event IR Surface`
- `flow_state`: typed flow/scope-like Event IR state or trace/state framing
- `trace.*`: typed trace framing such as command/include/subdirectory/language
- `diag`: typed diagnostic projection
- `var_state`: evaluator-visible state projected primarily through variable-like
  or query-result state
- `policy_state`: typed policy push/pop/set state
- `directory_property`: typed directory/global property mutation surface
- `target_semantic`: typed target/build-graph semantic surface
- `project_domain`: typed project/minimum-required surface
- `test_domain`: typed test registration/enabling surface
- `install_domain`: typed install-rule surface
- `cpack_domain`: typed CPack declaration surface
- `package_domain`: typed package/find-package semantic surface
- `export_domain`: typed export semantic surface
- `build_graph`: typed build-step/source-marking surface
- `proc_family`: typed process execution surface
- `fs_family`: typed filesystem/runtime-effect surface
- `replay_bridge.*`: typed replay bridge surface owned by replay action kinds

`Status`
- `typed`: the command currently has a frozen primary Event IR surface beyond
  generic command trace
- `replay`: the command is primarily represented through the replay bridge
- `trace_only`: the command is currently tracked only through generic
  command-trace and/or diagnostic framing for Event IR purposes

`Sampling`
- `direct`: focused evaluator/Event IR tests assert this command's Event IR
  surface directly
- `indirect`: the command participates in family-level or downstream sampling,
  but this matrix does not treat it as a command-row-focused Event IR sample
- `none`: no stable command-row Event IR sample is tracked here yet

Control rule:
- `typed` and `replay` count as implemented Event IR ownership
- `trace_only` is intentionally visible as remaining control debt when a later
  tranche wants a stronger typed Event IR surface

Primary-surface rule:
- rows name the primary Event IR owner only
- a command may touch multiple surfaces during evaluation
- the named surface is the one this matrix treats as the control point

## Audit Matrix

| Command | Kind | Primary Event IR Surface | Status | Sampling |
|---|---|---|---|---|
| `FetchContent_Declare` | native | `trace.command` | `trace_only` | `indirect` |
| `FetchContent_MakeAvailable` | native | `replay_bridge.dependency_materialization` | `replay` | `indirect` |
| `cmake_host_system_information` | native | `var_state` | `typed` | `indirect` |
| `cmake_path` | native | `var_state` | `typed` | `indirect` |
| `export` | native | `export_domain` | `typed` | `indirect` |
| `file` | native | `fs_family/replay_bridge.filesystem` | `replay` | `indirect` |
| `get_cmake_property` | native | `var_state` | `typed` | `indirect` |
| `get_directory_property` | native | `var_state` | `typed` | `indirect` |
| `get_filename_component` | native | `var_state` | `typed` | `indirect` |
| `get_property` | native | `var_state` | `typed` | `indirect` |
| `get_target_property` | native | `var_state` | `typed` | `indirect` |
| `install` | native | `install_domain` | `typed` | `indirect` |
| `list` | native | `var_state` | `typed` | `indirect` |
| `string` | native | `var_state` | `typed` | `indirect` |
| `add_compile_options` | native | `directory_property` | `typed` | `direct` |
| `add_custom_command` | native | `build_graph` | `typed` | `direct` |
| `add_custom_target` | native | `build_graph` | `typed` | `direct` |
| `add_definitions` | native | `directory_property` | `typed` | `direct` |
| `add_dependencies` | native | `target_semantic` | `typed` | `indirect` |
| `add_executable` | native | `target_semantic` | `typed` | `indirect` |
| `add_library` | native | `target_semantic` | `typed` | `indirect` |
| `add_link_options` | native | `directory_property` | `typed` | `direct` |
| `add_subdirectory` | native | `trace.subdirectory` | `typed` | `indirect` |
| `add_test` | native | `test_domain` | `typed` | `direct` |
| `block` | native | `flow_state` | `typed` | `indirect` |
| `break` | native | `flow_state` | `typed` | `indirect` |
| `cmake_minimum_required` | native | `project_domain` | `typed` | `indirect` |
| `cmake_policy` | native | `policy_state` | `typed` | `indirect` |
| `configure_file` | native | `replay_bridge.filesystem` | `replay` | `indirect` |
| `continue` | native | `flow_state` | `typed` | `indirect` |
| `cpack_add_component` | native | `cpack_domain` | `typed` | `indirect` |
| `cpack_add_component_group` | native | `cpack_domain` | `typed` | `indirect` |
| `cpack_add_install_type` | native | `cpack_domain` | `typed` | `indirect` |
| `else` | structural | `flow_state` | `typed` | `indirect` |
| `elseif` | structural | `flow_state` | `typed` | `indirect` |
| `enable_testing` | native | `test_domain` | `typed` | `indirect` |
| `endblock` | native | `flow_state` | `typed` | `indirect` |
| `endforeach` | structural | `flow_state` | `typed` | `indirect` |
| `endfunction` | structural | `flow_state` | `typed` | `indirect` |
| `endif` | structural | `flow_state` | `typed` | `indirect` |
| `endmacro` | structural | `flow_state` | `typed` | `indirect` |
| `endwhile` | structural | `flow_state` | `typed` | `indirect` |
| `execute_process` | native | `proc_family` | `typed` | `indirect` |
| `find_file` | native | `var_state` | `typed` | `indirect` |
| `find_library` | native | `var_state` | `typed` | `indirect` |
| `find_package` | native | `package_domain` | `typed` | `indirect` |
| `find_path` | native | `var_state` | `typed` | `indirect` |
| `find_program` | native | `var_state` | `typed` | `indirect` |
| `foreach` | structural | `flow_state` | `typed` | `indirect` |
| `function` | structural | `flow_state` | `typed` | `indirect` |
| `if` | structural | `flow_state` | `typed` | `indirect` |
| `include` | native | `trace.include` | `typed` | `indirect` |
| `include_directories` | native | `directory_property` | `typed` | `indirect` |
| `include_guard` | native | `trace.command` | `trace_only` | `indirect` |
| `link_directories` | native | `directory_property` | `typed` | `indirect` |
| `link_libraries` | native | `target_semantic` | `typed` | `indirect` |
| `macro` | structural | `flow_state` | `typed` | `indirect` |
| `mark_as_advanced` | native | `trace.command` | `trace_only` | `none` |
| `math` | native | `var_state` | `typed` | `indirect` |
| `message` | native | `diag` | `typed` | `indirect` |
| `option` | native | `var_state` | `typed` | `indirect` |
| `project` | native | `project_domain` | `typed` | `indirect` |
| `return` | native | `flow_state` | `typed` | `indirect` |
| `set` | native | `var_state` | `typed` | `indirect` |
| `set_property` | native | `target_semantic/directory_property` | `typed` | `indirect` |
| `set_target_properties` | native | `target_semantic` | `typed` | `indirect` |
| `source_group` | native | `trace.command` | `trace_only` | `none` |
| `target_compile_definitions` | native | `target_semantic` | `typed` | `direct` |
| `target_compile_options` | native | `target_semantic` | `typed` | `direct` |
| `target_include_directories` | native | `target_semantic` | `typed` | `indirect` |
| `target_link_directories` | native | `target_semantic` | `typed` | `indirect` |
| `target_link_libraries` | native | `target_semantic` | `typed` | `direct` |
| `target_link_options` | native | `target_semantic` | `typed` | `indirect` |
| `try_compile` | native | `replay_bridge.probe` | `replay` | `indirect` |
| `unset` | native | `var_state` | `typed` | `indirect` |
| `while` | structural | `flow_state` | `typed` | `indirect` |
| `FetchContent_GetProperties` | native | `trace.command` | `trace_only` | `none` |
| `FetchContent_Populate` | native | `replay_bridge.dependency_materialization` | `replay` | `indirect` |
| `FetchContent_SetPopulated` | native | `trace.command` | `trace_only` | `none` |
| `add_compile_definitions` | native | `directory_property` | `typed` | `indirect` |
| `aux_source_directory` | native | `var_state` | `typed` | `indirect` |
| `build_command` | native | `var_state` | `typed` | `indirect` |
| `build_name` | native | `var_state` | `typed` | `indirect` |
| `cmake_file_api` | native | `trace.command` | `trace_only` | `none` |
| `cmake_language` | native | `trace.cmake_language` | `typed` | `indirect` |
| `cmake_parse_arguments` | native | `var_state` | `typed` | `indirect` |
| `create_test_sourcelist` | native | `trace.command` | `trace_only` | `none` |
| `ctest_build` | native | `replay_bridge.test_driver` | `replay` | `indirect` |
| `ctest_configure` | native | `replay_bridge.test_driver` | `replay` | `indirect` |
| `ctest_coverage` | native | `replay_bridge.test_driver` | `replay` | `direct` |
| `ctest_empty_binary_directory` | native | `replay_bridge.test_driver` | `replay` | `indirect` |
| `ctest_memcheck` | native | `replay_bridge.test_driver` | `replay` | `direct` |
| `ctest_read_custom_files` | native | `trace.command` | `trace_only` | `none` |
| `ctest_run_script` | native | `trace.command` | `trace_only` | `none` |
| `ctest_sleep` | native | `replay_bridge.test_driver` | `replay` | `indirect` |
| `ctest_start` | native | `replay_bridge.test_driver` | `replay` | `indirect` |
| `ctest_submit` | native | `trace.command` | `trace_only` | `none` |
| `ctest_test` | native | `replay_bridge.test_driver` | `replay` | `indirect` |
| `ctest_update` | native | `trace.command` | `trace_only` | `none` |
| `ctest_upload` | native | `trace.command` | `trace_only` | `none` |
| `define_property` | native | `trace.command` | `trace_only` | `none` |
| `enable_language` | native | `trace.command` | `trace_only` | `none` |
| `exec_program` | native | `proc_family` | `typed` | `indirect` |
| `export_library_dependencies` | native | `trace.command` | `trace_only` | `none` |
| `fltk_wrap_ui` | native | `trace.command` | `trace_only` | `none` |
| `get_source_file_property` | native | `var_state` | `typed` | `indirect` |
| `get_test_property` | native | `var_state` | `typed` | `indirect` |
| `include_external_msproject` | native | `target_semantic` | `typed` | `indirect` |
| `include_regular_expression` | native | `var_state` | `typed` | `indirect` |
| `install_files` | native | `install_domain` | `typed` | `indirect` |
| `install_programs` | native | `install_domain` | `typed` | `indirect` |
| `install_targets` | native | `install_domain` | `typed` | `indirect` |
| `load_cache` | native | `var_state` | `typed` | `indirect` |
| `load_command` | native | `trace.command` | `trace_only` | `none` |
| `make_directory` | native | `replay_bridge.filesystem` | `replay` | `indirect` |
| `output_required_files` | native | `trace.command` | `trace_only` | `none` |
| `qt_wrap_cpp` | native | `trace.command` | `trace_only` | `none` |
| `qt_wrap_ui` | native | `trace.command` | `trace_only` | `none` |
| `remove` | native | `var_state` | `typed` | `indirect` |
| `remove_definitions` | native | `directory_property` | `typed` | `indirect` |
| `separate_arguments` | native | `var_state` | `typed` | `indirect` |
| `set_directory_properties` | native | `directory_property` | `typed` | `indirect` |
| `set_source_files_properties` | native | `build_graph` | `typed` | `indirect` |
| `set_tests_properties` | native | `test_domain` | `typed` | `indirect` |
| `site_name` | native | `var_state` | `typed` | `indirect` |
| `subdir_depends` | native | `trace.command` | `trace_only` | `none` |
| `subdirs` | native | `trace.command` | `trace_only` | `none` |
| `target_compile_features` | native | `target_semantic` | `typed` | `indirect` |
| `target_precompile_headers` | native | `target_semantic` | `typed` | `indirect` |
| `target_sources` | native | `target_semantic` | `typed` | `indirect` |
| `try_run` | native | `replay_bridge.probe` | `replay` | `indirect` |
| `use_mangled_mesa` | native | `trace.command` | `trace_only` | `none` |
| `utility_source` | native | `trace.command` | `trace_only` | `none` |
| `variable_requires` | native | `trace.command` | `trace_only` | `none` |
| `variable_watch` | native | `var_state` | `typed` | `indirect` |
| `write_file` | native | `replay_bridge.filesystem` | `replay` | `indirect` |

## Notes

- This matrix is a control document for Event IR ownership only.
- It does not claim build-model ingest completeness or generated-backend
  runtime support.
- A `trace_only` row is not necessarily a bug; it is a visible control point
  for deciding whether a future Event IR tranche needs stronger typing.
- Rows stay aligned to the evaluator audited universe so that command-level
  discussion can move between evaluator and Event IR without changing the row
  set.

## Related Docs

- [Event IR v2 spec](./event_ir_v2_spec.md)
- [Event IR incremental roadmap](./event_ir_closure_roadmap.md)
- [Evaluator Event IR contract](../evaluator/evaluator_event_ir_contract.md)
- [Evaluator coverage matrix](../evaluator/evaluator_coverage_matrix.md)
