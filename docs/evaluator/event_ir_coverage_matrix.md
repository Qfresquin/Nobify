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
| `block` | `eval_flow.c` | complete | `local_only` | `SCOPE`, `FLOW`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Also interacts with return propagation. |
| `function` | `evaluator.c` | complete | `local_only` | `SCOPE`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Macro/function call trace should be explicit. |
| `macro` | `evaluator.c` | complete | `local_only` | `SCOPE`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Macro binding effects later map to `VAR`. |
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
