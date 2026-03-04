# Event IR Coverage Matrix

This matrix tracks the migration from the frozen structural Event IR to the new
canonical semantic Event IR.

Status vocabulary:
- `legacy_structural`: emitted only through the obsolete structural contract
- `local_only`: handled in evaluator state/diag only
- `partial`: some semantic handling exists, but not on the new Event IR
- `planned`: target family and events are defined, migration not started

| Command | Module | Handler Status | Current Emission | Target Family | Target Events | Priority | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `set` | `eval_vars.c` | complete | `partial` | `VAR` | `EVENT_VAR_SET` | high | Normal and cache forms are semantic now; env/watch refinements can expand later. |
| `unset` | `eval_vars.c` | complete | `partial` | `VAR` | `EVENT_VAR_UNSET` | high | Normal and cache unsets are semantic now. |
| `if` | `eval_flow.c` | complete | `partial` | `FLOW` | `EVENT_FLOW_IF_EVAL`, `EVENT_FLOW_BRANCH_TAKEN` | high | Condition results and selected branch are now emitted semantically. |
| `foreach` | `eval_flow.c` | complete | `partial` | `FLOW`, `SCOPE` | `EVENT_FLOW_LOOP_BEGIN`, `EVENT_FLOW_LOOP_END`, `EVENT_FLOW_CONTINUE`, `EVENT_FLOW_BREAK` | high | Loop lifecycle is emitted semantically; per-iteration payloads can expand later. |
| `while` | `eval_flow.c` | complete | `partial` | `FLOW`, `SCOPE` | `EVENT_FLOW_IF_EVAL`, `EVENT_FLOW_LOOP_BEGIN`, `EVENT_FLOW_LOOP_END`, `EVENT_FLOW_CONTINUE`, `EVENT_FLOW_BREAK` | high | Condition and loop lifecycle are now observable semantically. |
| `return` | `eval_flow.c` | complete | `partial` | `FLOW` | `EVENT_FLOW_RETURN` | high | Preserve `PROPAGATE` data in payload. |
| `block` | `eval_flow.c` | complete | `partial` | `SCOPE`, `FLOW` | `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_FLOW_BLOCK_BEGIN`, `EVENT_FLOW_BLOCK_END`, `EVENT_FLOW_RETURN` | high | Block lifecycle is now explicit semantically; deeper per-branch/block metadata can still expand later. |
| `function` | `evaluator.c` | complete | `partial` | `SCOPE`, `FLOW` | `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_FLOW_FUNCTION_BEGIN`, `EVENT_FLOW_FUNCTION_END` | high | Function call lifecycle is now explicit semantically; argument/binding metadata can still expand later. |
| `macro` | `evaluator.c` | complete | `partial` | `SCOPE`, `FLOW` | `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_FLOW_MACRO_BEGIN`, `EVENT_FLOW_MACRO_END` | high | Macro call lifecycle is now explicit semantically; binding metadata can still expand later. |
| `include` | `eval_include.c` | complete | `partial` | `META`, `SCOPE` | `EVENT_INCLUDE_BEGIN`, `EVENT_DIR_PUSH`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_DIR_POP`, `EVENT_INCLUDE_END` | high | Include lifecycle is now semantic; richer include metadata can still expand later. |
| `add_subdirectory` | `eval_include.c` | partial | `partial` | `META`, `SCOPE` | `EVENT_ADD_SUBDIRECTORY_BEGIN`, `EVENT_DIR_PUSH`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_DIR_POP`, `EVENT_ADD_SUBDIRECTORY_END` | high | Begin/end and directory context are semantic now; dedicated subdir policy/context details can expand later. |
| `cmake_policy` | `eval_policy_engine.c` | complete | `partial` | `POLICY` | `EVENT_POLICY_PUSH`, `EVENT_POLICY_POP`, `EVENT_POLICY_SET` | high | Push/pop/set are part of the semantic nucleus now. |
| `cmake_minimum_required` | `eval_project.c` | partial | `partial` | `PROJECT`, `POLICY` | `EVENT_PROJECT_MINIMUM_REQUIRED`, `EVENT_POLICY_SET` | high | Minimum-required semantics now emit a project event alongside policy effects, without relying on `TRACE`. |
| `project` | `eval_project.c` | complete | `partial` | `PROJECT` | `EVENT_PROJECT_DECLARE` | high | Core project declaration is semantic now; additional project metadata can still expand later. |
| `add_executable` / `add_library` | `eval_project.c` | complete | `partial` | `TARGET` | `EVENT_TARGET_DECLARE`, `EVENT_TARGET_ADD_SOURCE` | high | Target declaration and source attachment now emit semantic target events. |
| `target_*` | `eval_target.c` | complete | `partial` | `TARGET` | `EVENT_TARGET_ADD_DEPENDENCY`, `EVENT_TARGET_PROP_SET`, `EVENT_TARGET_ADD_SOURCE`, `EVENT_TARGET_LINK_LIBRARIES`, `EVENT_TARGET_LINK_OPTIONS`, `EVENT_TARGET_LINK_DIRECTORIES`, `EVENT_TARGET_INCLUDE_DIRECTORIES`, `EVENT_TARGET_COMPILE_DEFINITIONS`, `EVENT_TARGET_COMPILE_OPTIONS` | high | Property/usage requirement handlers now emit normalized target events while keeping local store as source of truth. |
| `add_custom_command` / `add_custom_target` | `eval_custom.c` | complete | `partial` | `TARGET`, `FS` | `EVENT_TARGET_DECLARE`, `EVENT_TARGET_PROP_SET`, `EVENT_FS_WRITE_FILE`, `EVENT_FS_APPEND_FILE` | high | `TRACE` is no longer the primary fallback here; target-signature commands emit target semantics and output-signature commands emit FS output intents. |
| `add_test` | `eval_test.c` | complete | `partial` | `TEST` | `EVENT_TEST_ADD` | high | `EVENT_TEST_ENABLE` and `EVENT_TEST_ADD` now exist; command-level trace is no longer the primary signal. |
| `install` | `eval_install.c` | complete | `partial` | `INSTALL` | `EVENT_INSTALL_RULE_ADD` | high | Rule emission is semantic now; command-level trace is no longer required for install rules. |
| `find_package` | `eval_package.c` | complete | `partial` | `PACKAGE`, `DIAG` | `EVENT_PACKAGE_FIND_RESULT`, `EVENT_DIAG` | high | Result semantics are emitted now; item-level/package-family expansion can come later. |
| `file(...)` | `eval_file.c` | complete | `partial` | `FS`, `DIAG` | `EVENT_FS_GLOB`, `EVENT_FS_WRITE_FILE`, `EVENT_FS_APPEND_FILE`, `EVENT_FS_READ_FILE`, `EVENT_FS_MKDIR`, `EVENT_FS_REMOVE`, `EVENT_FS_RENAME`, `EVENT_FS_CREATE_LINK`, `EVENT_FS_CHMOD`, `EVENT_FS_ARCHIVE_CREATE`, `EVENT_FS_ARCHIVE_EXTRACT`, `EVENT_FS_TRANSFER_DOWNLOAD`, `EVENT_FS_TRANSFER_UPLOAD`, `EVENT_DIAG` | high | Core filesystem operations now emit semantic FS events; copy/install/extra subfamilies still need deeper coverage. |
| `execute_process` | `eval_flow.c` | complete | `partial` | `PROC`, `DIAG` | `EVENT_PROC_EXEC_REQUEST`, `EVENT_PROC_EXEC_RESULT`, `EVENT_DIAG` | high | Request/result semantics are emitted now; richer process metadata can expand later. |
| `cmake_language` | `eval_flow.c` | partial | `partial` | `META`, `FLOW` | `EVENT_CMAKE_LANGUAGE_CALL`, `EVENT_CMAKE_LANGUAGE_EVAL`, `EVENT_CMAKE_LANGUAGE_DEFER_QUEUE`, `EVENT_FLOW_DEFER_QUEUE`, `EVENT_FLOW_DEFER_FLUSH` | high | Meta calls are semantic now, and defer queue/flush also emit real `FLOW` events. |
| `string(...)` | `eval_string.c` | complete | `partial` | `STRING` | `EVENT_STRING_CONFIGURE`, `EVENT_STRING_TIMESTAMP`, `EVENT_STRING_HASH`, `EVENT_STRING_REPLACE`, `EVENT_STRING_REGEX` | medium | High-value implemented subcommands now emit semantic string events. |
| `list(...)` | `eval_list.c` | complete | `partial` | `LIST` | `EVENT_LIST_APPEND`, `EVENT_LIST_PREPEND`, `EVENT_LIST_INSERT`, `EVENT_LIST_REMOVE`, `EVENT_LIST_TRANSFORM`, `EVENT_LIST_SORT` | medium | Core mutating list operations now emit semantic list events. |
| `math(...)` | `eval_math.c` | complete | `partial` | `MATH` | `EVENT_MATH_EXPR` | medium | `math(EXPR ...)` is now represented semantically. |
| `cmake_path(...)` | `eval_cmake_path.c` | complete | `partial` | `PATH` | `EVENT_PATH_NORMALIZE`, `EVENT_PATH_COMPARE`, `EVENT_PATH_CONVERT` | medium | Normalization/compare/convert now emit semantic path events. |
| `cpack_add_install_type` / `cpack_add_component_group` / `cpack_add_component` | `eval_cpack.c` | complete | `partial` | `CPACK`, `DIAG` | `EVENT_CPACK_ADD_INSTALL_TYPE`, `EVENT_CPACK_ADD_COMPONENT_GROUP`, `EVENT_CPACK_ADD_COMPONENT`, `EVENT_DIAG` | medium | Core CPACK payloads now exist; broader packaging semantics can expand later. |
| `enable_testing` | `eval_test.c` | complete | `partial` | `TEST` | `EVENT_TEST_ENABLE` | high | Already emitted semantically; split out explicitly from `add_test` for full registry coverage. |
| `ctest_build` | `eval_ctest.c` | partial | `local_only` | `TEST` | `planned: EVENT_CTEST_BUILD` | medium | Currently modeled via metadata publication only; should emit a semantic CTest operation event. |
| `ctest_configure` | `eval_ctest.c` | partial | `local_only` | `TEST` | `planned: EVENT_CTEST_CONFIGURE` | medium | Currently modeled via metadata publication only. |
| `ctest_coverage` | `eval_ctest.c` | partial | `local_only` | `TEST` | `planned: EVENT_CTEST_COVERAGE` | medium | Coverage intent exists but is not yet represented on the Event IR. |
| `ctest_empty_binary_directory` | `eval_ctest.c` | partial | `local_only` | `TEST`, `FS`, `DIAG` | `planned: EVENT_CTEST_EMPTY_BINARY_DIRECTORY`, `planned: EVENT_FS_REMOVE`, `EVENT_DIAG` | medium | Semantically important because it mutates the build tree; should become explicit. |
| `ctest_memcheck` | `eval_ctest.c` | partial | `local_only` | `TEST` | `planned: EVENT_CTEST_MEMCHECK` | medium | Metadata-modeled today; no semantic event yet. |
| `ctest_read_custom_files` | `eval_ctest.c` | partial | `local_only` | `TEST`, `FS`, `DIAG` | `planned: EVENT_CTEST_READ_CUSTOM_FILES`, `EVENT_DIAG` | medium | Reads configuration-like metadata from directories; should be represented explicitly. |
| `ctest_run_script` | `eval_ctest.c` | partial | `local_only` | `TEST`, `META`, `DIAG` | `planned: EVENT_CTEST_RUN_SCRIPT`, `EVENT_DIAG` | medium | Script invocation intent exists but is not yet semantic. |
| `ctest_sleep` | `eval_ctest.c` | partial | `local_only` | `TEST` | `planned: EVENT_CTEST_SLEEP` | low | Operational intent only; still part of evaluator execution surface. |
| `ctest_start` | `eval_ctest.c` | partial | `local_only` | `TEST` | `planned: EVENT_CTEST_START` | medium | Start metadata should become explicit. |
| `ctest_submit` | `eval_ctest.c` | partial | `local_only` | `TEST`, `PROC` | `planned: EVENT_CTEST_SUBMIT` | medium | Submission intent should be modeled before any deeper transport detail. |
| `ctest_test` | `eval_ctest.c` | partial | `local_only` | `TEST` | `planned: EVENT_CTEST_TEST` | medium | Core CTest test-run intent is still missing from the Event IR. |
| `ctest_update` | `eval_ctest.c` | partial | `local_only` | `TEST`, `PROC` | `planned: EVENT_CTEST_UPDATE` | medium | Update intent exists only as metadata today. |
| `ctest_upload` | `eval_ctest.c` | partial | `local_only` | `TEST`, `PROC` | `planned: EVENT_CTEST_UPLOAD` | medium | Upload intent should become explicit once CTest coverage is expanded. |
| `add_compile_definitions` | `eval_directory.c` | complete | `local_only` | `TARGET`, `SCOPE` | `planned: EVENT_DIRECTORY_COMPILE_DEFINITIONS` or `EVENT_TARGET_COMPILE_DEFINITIONS` fanout | medium | Currently applied as evaluator state only; still outside semantic Event IR. |
| `add_compile_options` | `eval_directory.c` | complete | `local_only` | `TARGET`, `SCOPE` | `planned: EVENT_DIRECTORY_COMPILE_OPTIONS` or target fanout events | medium | Not yet represented semantically. |
| `add_definitions` | `eval_directory.c` | complete | `local_only` | `TARGET`, `SCOPE` | `planned: EVENT_DIRECTORY_COMPILE_DEFINITIONS` | medium | Legacy directory/global compile-definitions path still lacks a domain event. |
| `add_link_options` | `eval_directory.c` | complete | `local_only` | `TARGET`, `SCOPE` | `planned: EVENT_DIRECTORY_LINK_OPTIONS` or target fanout events | medium | Evaluator state only at the moment. |
| `include_directories` | `eval_directory.c` | complete | `local_only` | `TARGET`, `SCOPE` | `planned: EVENT_DIRECTORY_INCLUDE_DIRECTORIES` or target fanout events | medium | No semantic directory/global event yet. |
| `link_directories` | `eval_directory.c` | complete | `local_only` | `TARGET`, `SCOPE` | `planned: EVENT_DIRECTORY_LINK_DIRECTORIES` or target fanout events | medium | No semantic directory/global event yet. |
| `link_libraries` | `eval_directory.c` | complete | `local_only` | `TARGET`, `SCOPE` | `planned: EVENT_DIRECTORY_LINK_LIBRARIES` or target fanout events | medium | Global link state is not yet explicit in the Event IR. |
| `set_directory_properties` | `eval_directory.c` | complete | `local_only` | `SCOPE`, `META` | `planned: EVENT_DIRECTORY_PROPERTY_SET` | medium | Directory property mutation is still evaluator-local. |
| `get_directory_property` | `eval_directory.c` | partial | `local_only` | `SCOPE`, `META` | `planned: EVENT_DIRECTORY_PROPERTY_GET` | low | Query semantics are not yet represented. |
| `define_property` | `eval_vars.c` | complete | `local_only` | `META` | `planned: EVENT_PROPERTY_DEFINE` | medium | Property definition affects later semantics and should become explicit. |
| `set_property` | `eval_target.c` | complete | `partial` | `TARGET`, `META` | `EVENT_TARGET_PROP_SET`, `planned: EVENT_DIRECTORY_PROPERTY_SET`, `planned: EVENT_TEST_PROPERTY_SET` | medium | Target coverage exists; non-target scopes still need explicit events. |
| `get_property` | `eval_target.c` | partial | `local_only` | `TARGET`, `META` | `planned: EVENT_PROPERTY_GET` | low | Query path is still evaluator-local. |
| `set_target_properties` | `eval_target.c` | complete | `partial` | `TARGET` | `EVENT_TARGET_PROP_SET` | medium | Already covered semantically through property-set emission, but listed explicitly for registry completeness. |
| `get_target_property` | `eval_target.c` | partial | `local_only` | `TARGET` | `planned: EVENT_TARGET_PROPERTY_GET` | low | Query semantics not yet emitted. |
| `set_source_files_properties` | `eval_target.c` | complete | `local_only` | `META` | `planned: EVENT_SOURCE_FILE_PROPERTY_SET` | medium | Source-file property scope remains unmodeled. |
| `get_source_file_property` | `eval_target.c` | partial | `local_only` | `META` | `planned: EVENT_SOURCE_FILE_PROPERTY_GET` | low | Query semantics not yet explicit. |
| `set_tests_properties` | `eval_test.c` | complete | `local_only` | `TEST` | `planned: EVENT_TEST_PROPERTY_SET` | medium | Test property mutation is not yet represented semantically. |
| `get_test_property` | `eval_test.c` | partial | `local_only` | `TEST` | `planned: EVENT_TEST_PROPERTY_GET` | low | Query semantics not yet explicit. |
| `get_cmake_property` | `eval_meta.c` | partial | `local_only` | `META` | `planned: EVENT_CMAKE_PROPERTY_GET` | low | Query semantics only. |
| `find_file` / `find_library` / `find_path` / `find_program` | `eval_package.c` | complete | `local_only` | `PACKAGE`, `DIAG` | `planned: EVENT_PACKAGE_FIND_ITEM_RESULT`, `EVENT_DIAG` | high | These are core discovery commands and should join `find_package` in the semantic package family. |
| `configure_file` | `eval_file.c` | complete | `local_only` | `FS`, `META`, `DIAG` | `planned: EVENT_FS_CONFIGURE_FILE`, `EVENT_DIAG` | high | Important for transpilation/generation; still missing as an explicit event. |
| `write_file` | `eval_legacy.c` | partial | `local_only` | `FS`, `DIAG` | `planned: EVENT_FS_WRITE_FILE`, `EVENT_DIAG` | medium | Legacy spelling should still map to semantic FS output. |
| `make_directory` | `eval_legacy.c` | complete | `local_only` | `FS`, `DIAG` | `planned: EVENT_FS_MKDIR`, `EVENT_DIAG` | medium | Legacy wrapper still lacks explicit FS emission. |
| `get_filename_component` | `eval_legacy.c` | complete | `local_only` | `PATH` | `planned: EVENT_PATH_COMPONENT` | medium | Path decomposition semantics are not yet explicit. |
| `cmake_parse_arguments` | `eval_vars.c` | complete | `local_only` | `META`, `VAR` | `planned: EVENT_CMAKE_PARSE_ARGUMENTS` | medium | Affects argument semantics and generated variables, but is not yet represented. |
| `separate_arguments` | `eval_string.c` | partial | `local_only` | `STRING`, `VAR` | `planned: EVENT_STRING_SEPARATE_ARGUMENTS` | medium | Important because it transforms argument structure, not just text. |
| `message` | `eval_diag.c` | complete | `partial` | `DIAG` | `EVENT_DIAG` | medium | Already reaches the Event IR through diagnostics, but it is not tracked explicitly as a command row today. |
| `mark_as_advanced` | `eval_vars.c` | complete | `local_only` | `VAR`, `META` | `planned: EVENT_CACHE_MARK_ADVANCED` | low | Cache metadata only, but still part of evaluator semantics. |
| `site_name` | `eval_legacy.c` | complete | `local_only` | `VAR` | `planned: EVENT_VAR_SET` | low | Currently only affects variables. |
| `enable_language` | `eval_project.c` | complete | `local_only` | `PROJECT` | `planned: EVENT_PROJECT_ENABLE_LANGUAGE` | high | This is core project semantics and should become explicit. |
| `include_guard` | `eval_include.c` | complete | `local_only` | `META` | `planned: EVENT_INCLUDE_GUARD` | medium | Guard semantics affect include behavior and should be observable. |
| `include_regular_expression` | `eval_include.c` | complete | `local_only` | `META` | `planned: EVENT_INCLUDE_REGEX_SET` | low | Mostly metadata, but still part of include semantics. |
| `include_external_msproject` | `eval_meta.c` | partial | `partial` | `TARGET`, `META` | `EVENT_TARGET_DECLARE`, `planned: EVENT_INCLUDE_EXTERNAL_MSPROJECT` | medium | Target declaration exists; the include/meta aspect is still missing. |
| `cmake_file_api` | `eval_meta.c` | partial | `local_only` | `META`, `FS` | `planned: EVENT_CMAKE_FILE_API_QUERY` | medium | Query intent should be represented even if execution remains partial. |
| `export` / `export_library_dependencies` | `eval_meta.c` / `eval_legacy.c` | partial | `local_only` | `META`, `FS` | `planned: EVENT_EXPORT_REQUEST` | low | Export intent exists but is not yet semantic. |
| `try_compile` | `eval_try_compile.c` | partial | `local_only` | `PROC`, `FS`, `TARGET`, `PROJECT`, `DIAG` | `planned: EVENT_TRY_COMPILE_REQUEST`, `EVENT_DIAG` | high | Important for transpilation correctness; still mostly local/stubbed. |
| `try_run` | `eval_try_compile.c` | partial | `local_only` | `PROC`, `FS`, `DIAG` | `planned: EVENT_TRY_RUN_REQUEST`, `EVENT_DIAG` | high | Same rationale as `try_compile`; currently not semantically represented. |
| `build_command` / `build_name` | `eval_legacy.c` | partial | `local_only` | `PROJECT`, `VAR` | `planned: EVENT_BUILD_TOOL_QUERY` | low | Legacy metadata/query commands only. |
| `exec_program` | `eval_legacy.c` | partial | `local_only` | `PROC`, `DIAG` | `planned: EVENT_PROC_EXEC_REQUEST`, `planned: EVENT_PROC_EXEC_RESULT`, `EVENT_DIAG` | low | Legacy process command still lacks semantic PROC coverage. |
| `load_command` / `load_cache` | `eval_legacy.c` | partial | `local_only` | `META`, `VAR`, `DIAG` | `planned: EVENT_LEGACY_LOAD`, `EVENT_DIAG` | low | Legacy compatibility commands; still should have an explicit compatibility event if kept. |
| `output_required_files` | `eval_legacy.c` | partial | `local_only` | `FS`, `DIAG` | `planned: EVENT_FS_OUTPUT_REQUIRED_FILES`, `EVENT_DIAG` | low | Legacy file-generation metadata path. |
| `qt_wrap_cpp` / `qt_wrap_ui` / `fltk_wrap_ui` | `eval_legacy.c` | partial | `local_only` | `FS`, `TARGET`, `DIAG` | `planned: EVENT_WRAP_TOOL_REQUEST`, `EVENT_DIAG` | low | Tooling wrappers are still unmodeled semantically. |
| `remove` / `remove_definitions` | `eval_legacy.c` / `eval_directory.c` | partial | `local_only` | `VAR`, `TARGET`, `DIAG` | `planned: EVENT_REMOVE_REQUEST`, `EVENT_DIAG` | low | Legacy mutators with no semantic event yet. |
| `source_group` | `eval_target.c` | partial | `local_only` | `TARGET`, `META` | `planned: EVENT_SOURCE_GROUP_SET` | low | Source grouping is metadata but should still be explicit if preserved. |
| `subdir_depends` / `subdirs` | `eval_legacy.c` | partial | `local_only` | `META`, `SCOPE`, `DIAG` | `planned: EVENT_SUBDIR_REQUEST`, `EVENT_DIAG` | low | Legacy directory composition paths; currently not semantically modeled. |
| `target_compile_features` / `target_precompile_headers` | `eval_target.c` | partial | `local_only` | `TARGET` | `planned: EVENT_TARGET_COMPILE_FEATURES`, `planned: EVENT_TARGET_PRECOMPILE_HEADERS` | medium | Important target semantics still missing from the current target family. |
| `target_sources` | `eval_target.c` | partial | `partial` | `TARGET` | `EVENT_TARGET_ADD_SOURCE` | medium | The family exists, but registry status is still partial and should be tightened. |
| `use_mangled_mesa` / `utility_source` / `variable_requires` / `variable_watch` | `eval_legacy.c` / `eval_vars.c` | partial | `local_only` | `VAR`, `DIAG`, `META` | `planned: EVENT_LEGACY_COMPAT`, `EVENT_DIAG` | low | Compatibility/legacy helpers still need an explicit semantic or compat-layer event if they remain supported. |
