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
| `set` | `eval_vars.c` | complete | `legacy_structural` / `local_only` | `VAR`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_VAR_SET`, `EVENT_TRACE_COMMAND_END` | high | Includes normal, cache, and env forms. |
| `unset` | `eval_vars.c` | complete | `local_only` | `VAR`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_VAR_UNSET`, `EVENT_TRACE_COMMAND_END` | high | Includes normal and cache forms. |
| `if` | `eval_flow.c` | complete | `local_only` | `FLOW`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | high | Needs branch-evaluation events later. |
| `foreach` | `eval_flow.c` | complete | `local_only` | `FLOW`, `TRACE`, `SCOPE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Loop-body semantics still need dedicated flow events. |
| `while` | `eval_flow.c` | complete | `local_only` | `FLOW`, `TRACE`, `SCOPE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Same migration shape as `foreach`. |
| `return` | `eval_flow.c` | complete | `local_only` | `FLOW`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_FLOW_RETURN`, `EVENT_TRACE_COMMAND_END` | high | Preserve `PROPAGATE` data in payload. |
| `block` | `eval_flow.c` | complete | `local_only` | `SCOPE`, `FLOW`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Also interacts with return propagation. |
| `function` | `evaluator.c` | complete | `local_only` | `SCOPE`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Macro/function call trace should be explicit. |
| `macro` | `evaluator.c` | complete | `local_only` | `SCOPE`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Macro binding effects later map to `VAR`. |
| `include` | `eval_include.c` | complete | `partial` | `META`, `SCOPE` | `EVENT_INCLUDE_BEGIN`, `EVENT_DIR_PUSH`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_DIR_POP`, `EVENT_INCLUDE_END` | high | Include lifecycle is now semantic; richer include metadata can still expand later. |
| `add_subdirectory` | `eval_include.c` | partial | `partial` | `META`, `SCOPE` | `EVENT_ADD_SUBDIRECTORY_BEGIN`, `EVENT_DIR_PUSH`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_DIR_POP`, `EVENT_ADD_SUBDIRECTORY_END` | high | Begin/end and directory context are semantic now; dedicated subdir policy/context details can expand later. |
| `cmake_policy` | `eval_policy_engine.c` | complete | `local_only` | `POLICY`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_POLICY_PUSH`, `EVENT_POLICY_POP`, `EVENT_POLICY_SET`, `EVENT_TRACE_COMMAND_END` | high | Push/pop/set are part of initial nucleus. |
| `cmake_minimum_required` | `eval_project.c` | partial | `partial` | `PROJECT`, `POLICY` | `EVENT_PROJECT_MINIMUM_REQUIRED`, `EVENT_POLICY_SET` | high | Minimum-required semantics now emit a project event alongside policy effects. |
| `project` | `eval_project.c` | complete | `partial` | `PROJECT` | `EVENT_PROJECT_DECLARE` | high | Core project declaration is semantic now; additional project metadata can still expand later. |
| `add_executable` / `add_library` | `eval_project.c` | complete | `partial` | `TARGET` | `EVENT_TARGET_DECLARE`, `EVENT_TARGET_ADD_SOURCE` | high | Target declaration and source attachment now emit semantic target events. |
| `target_*` | `eval_target.c` | complete | `partial` | `TARGET` | `EVENT_TARGET_ADD_DEPENDENCY`, `EVENT_TARGET_PROP_SET`, `EVENT_TARGET_ADD_SOURCE`, `EVENT_TARGET_LINK_LIBRARIES`, `EVENT_TARGET_LINK_OPTIONS`, `EVENT_TARGET_LINK_DIRECTORIES`, `EVENT_TARGET_INCLUDE_DIRECTORIES`, `EVENT_TARGET_COMPILE_DEFINITIONS`, `EVENT_TARGET_COMPILE_OPTIONS` | high | Property/usage requirement handlers now emit normalized target events while keeping local store as source of truth. |
| `add_custom_command` / `add_custom_target` | `eval_custom.c` | complete | `partial` | `TARGET`, `FS`, `TRACE` | `EVENT_TARGET_DECLARE`, `EVENT_TARGET_PROP_SET`, `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | high | `add_custom_target()` now emits semantic target declaration; `add_custom_command()` remains trace until FS split lands. |
| `add_test` | `eval_test.c` | complete | `partial` | `TEST`, `TRACE` | `EVENT_TEST_ADD` | high | `EVENT_TEST_ENABLE` and `EVENT_TEST_ADD` now exist; richer test semantics can expand later. |
| `install` | `eval_install.c` | complete | `partial` | `INSTALL`, `TRACE` | `EVENT_INSTALL_RULE_ADD` | high | Rule emission is semantic now; command-level trace is no longer required for install rules. |
| `find_package` | `eval_package.c` | complete | `partial` | `PACKAGE`, `DIAG` | `EVENT_PACKAGE_FIND_RESULT`, `EVENT_DIAG` | high | Result semantics are emitted now; item-level/package-family expansion can come later. |
| `file(...)` | `eval_file.c` | complete | `local_only` | `FS`, `TRACE`, `DIAG` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_DIAG`, `EVENT_TRACE_COMMAND_END` | high | Full filesystem taxonomy will be introduced in a later family migration. |
| `execute_process` | `eval_host.c` | complete | `local_only` | `PROC`, `TRACE`, `DIAG` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_DIAG`, `EVENT_TRACE_COMMAND_END` | high | Request/result events are planned after FS family. |
| `cmake_language` | `eval_flow.c` | partial | `partial` | `META`, `FLOW` | `EVENT_CMAKE_LANGUAGE_CALL`, `EVENT_CMAKE_LANGUAGE_EVAL`, `EVENT_CMAKE_LANGUAGE_DEFER_QUEUE` | high | `CALL`, `EVAL`, and DEFER queueing now emit semantic meta events; richer flow payloads remain. |
| `string(...)` | `eval_string.c` | complete | `local_only` | `STRING`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | medium | Semantic string-operation events are planned later. |
| `list(...)` | `eval_list.c` | complete | `local_only` | `LIST`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | medium | Semantic list-operation events are planned later. |
| `math(...)` | `eval_math.c` | complete | `local_only` | `MATH`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | medium | `math(EXPR ...)` gets a dedicated event family later. |
| `cmake_path(...)` | `eval_cmake_path.c` | complete | `local_only` | `PATH`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | medium | Path normalization and conversion events come later. |
| `cpack_add_install_type` / `cpack_add_component_group` / `cpack_add_component` | `eval_cpack.c` | complete | `partial` | `CPACK`, `DIAG` | `EVENT_CPACK_ADD_INSTALL_TYPE`, `EVENT_CPACK_ADD_COMPONENT_GROUP`, `EVENT_CPACK_ADD_COMPONENT`, `EVENT_DIAG` | medium | Core CPACK payloads now exist; broader packaging semantics can expand later. |
