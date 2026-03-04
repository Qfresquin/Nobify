# Event IR v2 Contract (Annex)

Status: Normative annex for evaluator output contract.

Source of truth:
- `src_v2/transpiler/event_ir.h`
- `src_v2/transpiler/event_ir.c`

## 1. Purpose

Event IR is the evaluator output boundary consumed by downstream build-model/codegen stages.
It is an append-only stream of typed `Event` records with origin metadata and deep-copied payloads.

## 2. Public Enums and Structural Types

Core enums used in payloads:
- `Event_Kind`
- `Event_Family`
- `Event_Scope_Kind`
- `Event_Var_Target_Kind`
- `Cmake_Visibility`
- `Cmake_Target_Property_Op`
- `Cmake_Target_Type`
- `Event_Diag_Severity`
- `Cmake_Install_Rule_Type`

Core structs:
- `Event_Origin`
- `Event`
- `Event_Stream`
- `Event_Stream_Iterator`

Auxiliary enum values currently defined:
- `Event_Var_Target_Kind`: `EVENT_VAR_TARGET_CURRENT`, `EVENT_VAR_TARGET_CACHE`, `EVENT_VAR_TARGET_ENV`
- `Cmake_Visibility`: `EV_VISIBILITY_UNSPECIFIED`, `EV_VISIBILITY_PRIVATE`, `EV_VISIBILITY_PUBLIC`, `EV_VISIBILITY_INTERFACE`
- `Cmake_Target_Property_Op`: `EV_PROP_SET`, `EV_PROP_APPEND_LIST`, `EV_PROP_APPEND_STRING`
- `Cmake_Target_Type`: `EV_TARGET_EXECUTABLE`, `EV_TARGET_LIBRARY_STATIC`, `EV_TARGET_LIBRARY_SHARED`, `EV_TARGET_LIBRARY_MODULE`, `EV_TARGET_LIBRARY_INTERFACE`, `EV_TARGET_LIBRARY_OBJECT`, `EV_TARGET_LIBRARY_UNKNOWN`
- `Event_Diag_Severity`: `EVENT_DIAG_SEVERITY_NOTE`, `EVENT_DIAG_SEVERITY_WARNING`, `EVENT_DIAG_SEVERITY_ERROR`
- `Cmake_Install_Rule_Type`: `EV_INSTALL_RULE_TARGET`, `EV_INSTALL_RULE_FILE`, `EV_INSTALL_RULE_PROGRAM`, `EV_INSTALL_RULE_DIRECTORY`

## 3. Event Naming Contract

Current code uses the semantic `Event_*` naming family from `src_v2/transpiler/event_ir.h`.

Examples of canonical kinds:
- `EVENT_DIAG`
- `EVENT_VAR_SET`
- `EVENT_VAR_UNSET`
- `EVENT_SCOPE_PUSH`
- `EVENT_SCOPE_POP`
- `EVENT_POLICY_SET`
- `EVENT_FLOW_RETURN`
- `EVENT_COMMAND_CALL`
- `EVENT_PROJECT_DECLARE`
- `EVENT_TARGET_DECLARE`
- `EVENT_TEST_ADD`
- `EVENT_INSTALL_RULE_ADD`
- `EVENT_PACKAGE_FIND_RESULT`

The complete set is defined only by `EVENT_KIND_LIST(...)` in `src_v2/transpiler/event_ir.h`.
This annex should not duplicate a frozen copy of that macro list.

## 4. Payload Mapping by Event Kind

All events include:
- `kind`
- `origin.file_path`
- `origin.line`
- `origin.col`

Payload mapping (`Event.as.*`):

| Event Kind | Payload Struct | Key Fields |
|---|---|---|
| `EVENT_DIAG` | `diag` | `severity`, `component`, `command`, `code`, `error_class`, `cause`, `hint` |
| `EVENT_PROJECT_DECLARE` | `project_declare` | `name`, `version`, `description`, `homepage_url`, `languages` |
| `EVENT_VAR_SET` | `var_set` | `key`, `value`, `target_kind` |
| `EVENT_VAR_UNSET` | `var_unset` | `key`, `target_kind` |
| `EVENT_SCOPE_PUSH` | `scope_push` | `scope_kind`, `depth_before`, `depth_after` |
| `EVENT_SCOPE_POP` | `scope_pop` | `scope_kind`, `depth_before`, `depth_after` |
| `EVENT_TARGET_DECLARE` | `target_declare` | `name`, `type`, `alias_of`, `imported` |
| `EVENT_TARGET_ADD_SOURCE` | `target_add_source` | `target_name`, `path` |
| `EVENT_TARGET_PROP_SET` | `target_prop_set` | `target_name`, `key`, `value`, `op` |
| `EVENT_TARGET_INCLUDE_DIRECTORIES` | `target_include_directories` | `target_name`, `visibility`, `path`, `is_system`, `is_before` |
| `EVENT_TARGET_COMPILE_DEFINITIONS` | `target_compile_definitions` | `target_name`, `visibility`, `item` |
| `EVENT_TARGET_COMPILE_OPTIONS` | `target_compile_options` | `target_name`, `visibility`, `item`, `is_before` |
| `EVENT_TARGET_LINK_LIBRARIES` | `target_link_libraries` | `target_name`, `visibility`, `item` |
| `EVENT_TARGET_LINK_OPTIONS` | `target_link_options` | `target_name`, `visibility`, `item`, `is_before` |
| `EVENT_TARGET_LINK_DIRECTORIES` | `target_link_directories` | `target_name`, `visibility`, `path`, `is_before` |
| `EVENT_DIR_PUSH` | `dir_push` | `source_dir`, `binary_dir` |
| `EVENT_DIR_POP` | `dir_pop` | `source_dir`, `binary_dir` |
| `EVENT_COMMAND_CALL` | `command_call` | `command_name` |
| `EVENT_TEST_ENABLE` | `test_enable` | `enabled` |
| `EVENT_TEST_ADD` | `test_add` | `name`, `command`, `working_dir`, `command_expand_lists` |
| `EVENT_INSTALL_RULE_ADD` | `install_rule_add` | `rule_type`, `item`, `destination` |
| `EVENT_CPACK_ADD_INSTALL_TYPE` | `cpack_add_install_type` | `name`, `display_name` |
| `EVENT_CPACK_ADD_COMPONENT_GROUP` | `cpack_add_component_group` | `name`, `display_name`, `description`, `parent_group`, `expanded`, `bold_title` |
| `EVENT_CPACK_ADD_COMPONENT` | `cpack_add_component` | `name`, `display_name`, `description`, `group`, `depends`, `install_types`, `archive_file`, `plist`, `required`, `hidden`, `disabled`, `downloaded` |
| `EVENT_PACKAGE_FIND_RESULT` | `package_find_result` | `package_name`, `mode`, `required`, `found`, `found_path` |

`commands*` indicates dynamic `String_View` arrays plus `command_count`.

`EVENT_COMMAND_CALL` is emitted for every dispatched command before any handler-specific semantic events.

## 5. Stream and API Contract

Public API:

```c
Event_Stream *event_stream_create(Arena *arena);
bool event_stream_push(Arena *event_arena, Event_Stream *stream, Event ev);
Event_Stream_Iterator event_stream_iter(const Event_Stream *stream);
bool event_stream_next(Event_Stream_Iterator *it);
void event_stream_dump(const Event_Stream *stream);
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
- emit variable mutation targets explicitly through `target_kind` rather than inferring from boolean flags

## 8. Roadmap (Not Yet Implemented)

Roadmap, not current behavior:
- optional external serialization/replay layer above current in-memory Event IR API.
