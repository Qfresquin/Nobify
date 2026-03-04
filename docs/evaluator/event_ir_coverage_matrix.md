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
| `include` | `eval_include.c` | complete | `local_only` | `META`, `SCOPE`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Needs dedicated include begin/end events later. |
| `add_subdirectory` | `eval_directory.c` | partial | `local_only` | `META`, `SCOPE`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_SCOPE_PUSH`, `EVENT_SCOPE_POP`, `EVENT_TRACE_COMMAND_END` | high | Directory context switching needs its own meta event family. |
| `cmake_policy` | `eval_policy_engine.c` | complete | `local_only` | `POLICY`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_POLICY_PUSH`, `EVENT_POLICY_POP`, `EVENT_POLICY_SET`, `EVENT_TRACE_COMMAND_END` | high | Push/pop/set are part of initial nucleus. |
| `cmake_minimum_required` | `eval_project.c` | partial | `local_only` | `PROJECT`, `POLICY`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_POLICY_SET`, `EVENT_TRACE_COMMAND_END` | high | Needs project-level semantic payload later. |
| `project` | `eval_project.c` | complete | `legacy_structural` | `PROJECT`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | high | Structural project payload will be reintroduced semantically. |
| `add_executable` / `add_library` | `eval_target.c` | complete | `legacy_structural` | `TARGET`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | high | Target-declare semantic payload comes after core scope/var migration. |
| `target_*` | `eval_target.c` | complete | `legacy_structural` | `TARGET`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | high | Properties/usage requirements migrate as normalized target events. |
| `add_custom_command` | `eval_target.c` | complete | `legacy_structural` | `TARGET`, `FS`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | high | Later split between target semantics and filesystem/process semantics. |
| `add_test` | `eval_test.c` | complete | `partial` | `TEST`, `TRACE` | `EVENT_TEST_ADD` | high | `EVENT_TEST_ENABLE` and `EVENT_TEST_ADD` now exist; richer test semantics can expand later. |
| `install` | `eval_install.c` | complete | `partial` | `INSTALL`, `TRACE` | `EVENT_INSTALL_RULE_ADD` | high | Rule emission is semantic now; command-level trace is no longer required for install rules. |
| `find_package` | `eval_package.c` | complete | `partial` | `PACKAGE`, `DIAG` | `EVENT_PACKAGE_FIND_RESULT`, `EVENT_DIAG` | high | Result semantics are emitted now; item-level/package-family expansion can come later. |
| `file(...)` | `eval_file.c` | complete | `local_only` | `FS`, `TRACE`, `DIAG` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_DIAG`, `EVENT_TRACE_COMMAND_END` | high | Full filesystem taxonomy will be introduced in a later family migration. |
| `execute_process` | `eval_host.c` | complete | `local_only` | `PROC`, `TRACE`, `DIAG` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_DIAG`, `EVENT_TRACE_COMMAND_END` | high | Request/result events are planned after FS family. |
| `cmake_language` | `eval_flow.c` | partial | `local_only` | `META`, `FLOW`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | high | `CALL`, `EVAL`, and `DEFER` need dedicated meta/flow payloads. |
| `string(...)` | `eval_string.c` | complete | `local_only` | `STRING`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | medium | Semantic string-operation events are planned later. |
| `list(...)` | `eval_list.c` | complete | `local_only` | `LIST`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | medium | Semantic list-operation events are planned later. |
| `math(...)` | `eval_math.c` | complete | `local_only` | `MATH`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | medium | `math(EXPR ...)` gets a dedicated event family later. |
| `cmake_path(...)` | `eval_cmake_path.c` | complete | `local_only` | `PATH`, `TRACE` | `EVENT_TRACE_COMMAND_BEGIN`, `EVENT_TRACE_COMMAND_END` | medium | Path normalization and conversion events come later. |
| `cpack_add_install_type` / `cpack_add_component_group` / `cpack_add_component` | `eval_cpack.c` | complete | `partial` | `CPACK`, `DIAG` | `EVENT_CPACK_ADD_INSTALL_TYPE`, `EVENT_CPACK_ADD_COMPONENT_GROUP`, `EVENT_CPACK_ADD_COMPONENT`, `EVENT_DIAG` | medium | Core CPACK payloads now exist; broader packaging semantics can expand later. |
