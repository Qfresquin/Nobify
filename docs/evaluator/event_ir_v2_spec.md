# Event IR v2 Contract (Annex)

Status: Normative annex for evaluator output contract.

Source of truth:
- `src_v2/transpiler/event_ir.h`
- `src_v2/transpiler/event_ir.c`

## 1. Purpose

Event IR is the evaluator output boundary consumed by downstream build-model/codegen stages.
It is an append-only stream of typed events with origin metadata and deep-copied payloads.

## 2. Public Enums and Structural Types

Core enums used in payloads:
- `Cmake_Event_Kind`
- `Cmake_Visibility`
- `Cmake_Target_Property_Op`
- `Cmake_Target_Type`
- `Cmake_Diag_Severity`
- `Cmake_Install_Rule_Type`

Core structs:
- `Cmake_Event_Origin`
- `Cmake_Event`
- `Cmake_Event_Stream`
- `Event_Stream_Iterator`

Auxiliary enum values currently defined:
- `Cmake_Visibility`: `EV_VISIBILITY_UNSPECIFIED`, `EV_VISIBILITY_PRIVATE`, `EV_VISIBILITY_PUBLIC`, `EV_VISIBILITY_INTERFACE`
- `Cmake_Target_Property_Op`: `EV_PROP_SET`, `EV_PROP_APPEND_LIST`, `EV_PROP_APPEND_STRING`
- `Cmake_Target_Type`: `EV_TARGET_EXECUTABLE`, `EV_TARGET_LIBRARY_STATIC`, `EV_TARGET_LIBRARY_SHARED`, `EV_TARGET_LIBRARY_MODULE`, `EV_TARGET_LIBRARY_INTERFACE`, `EV_TARGET_LIBRARY_OBJECT`, `EV_TARGET_LIBRARY_UNKNOWN`
- `Cmake_Diag_Severity`: `EV_DIAG_WARNING`, `EV_DIAG_ERROR`
- `Cmake_Install_Rule_Type`: `EV_INSTALL_RULE_TARGET`, `EV_INSTALL_RULE_FILE`, `EV_INSTALL_RULE_PROGRAM`, `EV_INSTALL_RULE_DIRECTORY`

## 3. Complete Event Kind Set

Current `Cmake_Event_Kind` values:
- `EV_DIAGNOSTIC`
- `EV_PROJECT_DECLARE`
- `EV_VAR_SET`
- `EV_SET_CACHE_ENTRY`
- `EV_TARGET_DECLARE`
- `EV_TARGET_ADD_SOURCE`
- `EV_TARGET_PROP_SET`
- `EV_TARGET_INCLUDE_DIRECTORIES`
- `EV_TARGET_COMPILE_DEFINITIONS`
- `EV_TARGET_COMPILE_OPTIONS`
- `EV_TARGET_LINK_LIBRARIES`
- `EV_TARGET_LINK_OPTIONS`
- `EV_TARGET_LINK_DIRECTORIES`
- `EV_CUSTOM_COMMAND_TARGET`
- `EV_CUSTOM_COMMAND_OUTPUT`
- `EV_DIR_PUSH`
- `EV_DIR_POP`
- `EV_DIRECTORY_INCLUDE_DIRECTORIES`
- `EV_DIRECTORY_LINK_DIRECTORIES`
- `EV_GLOBAL_COMPILE_DEFINITIONS`
- `EV_GLOBAL_COMPILE_OPTIONS`
- `EV_GLOBAL_LINK_OPTIONS`
- `EV_GLOBAL_LINK_LIBRARIES`
- `EV_TESTING_ENABLE`
- `EV_TEST_ADD`
- `EV_INSTALL_ADD_RULE`
- `EV_CPACK_ADD_INSTALL_TYPE`
- `EV_CPACK_ADD_COMPONENT_GROUP`
- `EV_CPACK_ADD_COMPONENT`
- `EV_FIND_PACKAGE`

## 4. Payload Mapping by Event Kind

All events include:
- `kind`
- `origin.file_path`
- `origin.line`
- `origin.col`

Payload mapping (`Cmake_Event.as.*`):

| Event Kind | Payload Struct | Key Fields |
|---|---|---|
| `EV_DIAGNOSTIC` | `diag` | `severity`, `component`, `command`, `code`, `error_class`, `cause`, `hint` |
| `EV_PROJECT_DECLARE` | `project_declare` | `name`, `version`, `description`, `languages` |
| `EV_VAR_SET` | `var_set` | `key`, `value` |
| `EV_SET_CACHE_ENTRY` | `cache_entry` | `key`, `value` |
| `EV_TARGET_DECLARE` | `target_declare` | `name`, `type` |
| `EV_TARGET_ADD_SOURCE` | `target_add_source` | `target_name`, `path` |
| `EV_TARGET_PROP_SET` | `target_prop_set` | `target_name`, `key`, `value`, `op` |
| `EV_TARGET_INCLUDE_DIRECTORIES` | `target_include_directories` | `target_name`, `visibility`, `path`, `is_system`, `is_before` |
| `EV_TARGET_COMPILE_DEFINITIONS` | `target_compile_definitions` | `target_name`, `visibility`, `item` |
| `EV_TARGET_COMPILE_OPTIONS` | `target_compile_options` | `target_name`, `visibility`, `item` |
| `EV_TARGET_LINK_LIBRARIES` | `target_link_libraries` | `target_name`, `visibility`, `item` |
| `EV_TARGET_LINK_OPTIONS` | `target_link_options` | `target_name`, `visibility`, `item` |
| `EV_TARGET_LINK_DIRECTORIES` | `target_link_directories` | `target_name`, `visibility`, `path` |
| `EV_CUSTOM_COMMAND_TARGET` | `custom_command_target` | `target_name`, `pre_build`, `commands*`, `working_dir`, `comment`, `outputs`, `byproducts`, `depends`, `main_dependency`, `depfile`, flags |
| `EV_CUSTOM_COMMAND_OUTPUT` | `custom_command_output` | `commands*`, `working_dir`, `comment`, `outputs`, `byproducts`, `depends`, `main_dependency`, `depfile`, flags |
| `EV_DIR_PUSH` | `dir_push` | `source_dir`, `binary_dir` |
| `EV_DIR_POP` | none | no extra payload fields |
| `EV_DIRECTORY_INCLUDE_DIRECTORIES` | `directory_include_directories` | `path`, `is_system`, `is_before` |
| `EV_DIRECTORY_LINK_DIRECTORIES` | `directory_link_directories` | `path`, `is_before` |
| `EV_GLOBAL_COMPILE_DEFINITIONS` | `global_compile_definitions` | `item` |
| `EV_GLOBAL_COMPILE_OPTIONS` | `global_compile_options` | `item` |
| `EV_GLOBAL_LINK_OPTIONS` | `global_link_options` | `item` |
| `EV_GLOBAL_LINK_LIBRARIES` | `global_link_libraries` | `item` |
| `EV_TESTING_ENABLE` | `testing_enable` | `enabled` |
| `EV_TEST_ADD` | `test_add` | `name`, `command`, `working_dir`, `command_expand_lists` |
| `EV_INSTALL_ADD_RULE` | `install_add_rule` | `rule_type`, `item`, `destination` |
| `EV_CPACK_ADD_INSTALL_TYPE` | `cpack_add_install_type` | `name`, `display_name` |
| `EV_CPACK_ADD_COMPONENT_GROUP` | `cpack_add_component_group` | `name`, `display_name`, `description`, `parent_group`, `expanded`, `bold_title` |
| `EV_CPACK_ADD_COMPONENT` | `cpack_add_component` | `name`, `display_name`, `description`, `group`, `depends`, `install_types`, `archive_file`, `plist`, `required`, `hidden`, `disabled`, `downloaded` |
| `EV_FIND_PACKAGE` | `find_package` | `package_name`, `mode`, `required`, `found`, `location` |

`commands*` indicates dynamic `String_View` arrays plus `command_count`.

## 5. Stream and API Contract

Public API:

```c
Cmake_Event_Stream *event_stream_create(Arena *arena);
bool event_stream_push(Arena *event_arena, Cmake_Event_Stream *stream, Cmake_Event ev);
Event_Stream_Iterator event_stream_iter(const Cmake_Event_Stream *stream);
bool event_stream_next(Event_Stream_Iterator *it);
void event_stream_dump(const Cmake_Event_Stream *stream);
```

`event_stream_push` returns `false` on allocation/copy failure.

## 6. Deep-Copy and Lifetime Semantics

`event_stream_push` deep-copies into event arena:
- `origin.file_path`
- all payload `String_View` fields
- custom-command `String_View` arrays

This guarantees payload validity after temp-arena rewinds in evaluator runtime.

## 7. Evaluator Obligations

Evaluator must:
- provide correct origin metadata
- avoid passing temp-memory references in event payload
- preserve deterministic event ordering for deterministic input/profile/environment

## 8. Roadmap (Not Yet Implemented)

Roadmap, not current behavior:
- optional external serialization/replay layer above current in-memory Event IR API.
