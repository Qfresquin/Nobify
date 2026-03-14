# Event IR v2 (Normative)

## 1. Overview

`src_v2/transpiler/event_ir.h` and `src_v2/transpiler/event_ir.c` define the
canonical in-memory contract emitted by the evaluator.

This is no longer a builder-shaped IR. It is the evaluator output contract. A
future build-model builder will consume a formal subset of these events.

Upstream producer note:
- the evaluator target architecture is defined in
  [`../evaluator/evaluator_v2_spec.md`](../evaluator/evaluator_v2_spec.md)
  and
  [`../evaluator/evaluator_architecture_target.md`](../evaluator/evaluator_architecture_target.md)
- the canonical producer entry is
  `eval_session_run(EvalSession *, const EvalExec_Request *, Ast_Root)`
- `EvalExec_Request.stream` controls whether a run projects `Event_Stream`
- Event IR is projected from the evaluator's session/request execution model
  after committed semantic mutations
- downstream consumers must not depend on the legacy evaluator API shape

Project priority framing:
- this IR exists to preserve evaluator-reconstructed CMake 3.28 semantics at a
  stable boundary before build-model reconstruction,
- historical behavior is secondary unless it changes those observable
  semantics,
- Nob backend optimization is downstream and should consume later semantic
  layers built on top of this contract.

Core properties:
- one append-only `Event_Stream`
- per-kind static metadata
- semantic separation by `Event_Role`, not by multiple streams
- deep-copy ownership at `event_stream_push(...)`
- frozen G0.1 base taxonomy: 19 canonical families, 86 canonical kinds

The existence of one canonical `Event_Stream` does not imply that the evaluator
must always be called with an event sink. Event projection is optional per run;
the stream contract becomes active when a sink is provided.

## 2. Kind Metadata

Every `Event_Kind` has static metadata exposed by:

```c
const Event_Kind_Meta *event_kind_meta(Event_Kind kind);
bool event_kind_has_role(Event_Kind kind, Event_Role role);
uint32_t event_kind_role_mask(Event_Kind kind);
```

`Event_Kind_Meta` carries:
- `kind`
- `family`
- `label`
- `role_mask`
- `default_version`

The current role taxonomy is:
- `EVENT_ROLE_TRACE`
- `EVENT_ROLE_DIAGNOSTIC`
- `EVENT_ROLE_RUNTIME_EFFECT`
- `EVENT_ROLE_STATE`
- `EVENT_ROLE_BUILD_SEMANTIC`

Consumers must filter by role, not by assumptions about evaluator internals.

## 3. Families and Key Kinds

Current families:
- `TRACE`
- `DIAG`
- `DIRECTORY`
- `FLOW`
- `SCOPE`
- `POLICY`
- `VAR`
- `FS`
- `PROC`
- `STRING`
- `LIST`
- `MATH`
- `PATH`
- `PROJECT`
- `TARGET`
- `TEST`
- `INSTALL`
- `CPACK`
- `PACKAGE`

Base-contract freeze rules:
- canonical `Event_Family` and `Event_Kind` values are append-only
- new canonical entries append at the tail of the enum lists
- legacy compatibility sentinels are not valid payload kinds for `event_stream_push(...)`

Important trace kinds:
- `EVENT_COMMAND_BEGIN`
- `EVENT_COMMAND_END`
- `EVENT_INCLUDE_BEGIN`
- `EVENT_INCLUDE_END`
- `EVENT_ADD_SUBDIRECTORY_BEGIN`
- `EVENT_ADD_SUBDIRECTORY_END`
- `EVENT_CMAKE_LANGUAGE_*`

Important directory/build-semantic kinds:
- `EVENT_DIRECTORY_ENTER`
- `EVENT_DIRECTORY_LEAVE`
- `EVENT_DIRECTORY_PROPERTY_MUTATE`
- `EVENT_GLOBAL_PROPERTY_MUTATE`

Important target/build-semantic kinds remain typed:
- `EVENT_TARGET_DECLARE`
- `EVENT_TARGET_ADD_SOURCE`
- `EVENT_TARGET_ADD_DEPENDENCY`
- `EVENT_TARGET_PROP_SET`
- `EVENT_TARGET_LINK_LIBRARIES`
- `EVENT_TARGET_LINK_OPTIONS`
- `EVENT_TARGET_LINK_DIRECTORIES`
- `EVENT_TARGET_INCLUDE_DIRECTORIES`
- `EVENT_TARGET_COMPILE_DEFINITIONS`
- `EVENT_TARGET_COMPILE_OPTIONS`

## 4. Event Header

`Event_Header` stores only per-instance data:

```c
typedef struct {
    Event_Kind kind;
    uint16_t version;
    uint32_t flags;
    uint64_t seq;
    uint32_t scope_depth;
    uint32_t policy_depth;
    Event_Origin origin;
} Event_Header;
```

Notes:
- `family` is not stored in the header anymore; it is derived from `event_kind_meta(kind)`.
- `seq` is assigned by `event_stream_push(...)` when the caller leaves it as `0`.
- `version` defaults from kind metadata when the caller leaves it as `0`.

## 5. Stream Contract

```c
Event_Stream *event_stream_create(Arena *arena);
bool event_stream_push(Event_Stream *stream, const Event *ev);
Event_Stream_Iterator event_stream_iter(const Event_Stream *stream);
bool event_stream_next(Event_Stream_Iterator *it);
void event_stream_dump(const Event_Stream *stream);
```

`Event_Stream` owns:
- the backing arena reference
- the copied payload storage
- the appended `items`
- the monotonic `next_seq`

Ordering guarantees:
- append-only
- stable stream order equals execution order
- nested evaluations append into the same stream
- `event_stream_push(...)` rejects kinds that do not have canonical metadata
- the March 6, 2026 G0.4 baseline keeps manual stream contract tests for metadata resolution, deep-copy ownership, and canonical stream ordering/version defaults

## 6. Ownership and Copy Rules

`event_stream_push(...)` is the payload ownership boundary.

Rules:
- `String_View` payloads are deep-copied into the stream arena
- `String_View[]` payload arrays are deep-copied into the stream arena
- stream items remain valid while the stream arena lives

This applies in particular to:
- `EVENT_FLOW_RETURN.propagate_vars`
- `EVENT_DIRECTORY_PROPERTY_MUTATE.items`
- `EVENT_GLOBAL_PROPERTY_MUTATE.items`

## 7. Command Trace Contract

Dispatched commands are represented by:
- `EVENT_COMMAND_BEGIN`
- `EVENT_COMMAND_END`

Payload fields:
- `command_name`
- `dispatch_kind`: `builtin`, `function`, `macro`, `unknown`
- `argc`
- end-only `status`: `success`, `error`, `unsupported`

`EVENT_COMMAND_BEGIN/END` are trace events. They are not a substitute for diagnostic events or semantic build events.

Sequencing rule:
- dispatched commands open with `EVENT_COMMAND_BEGIN`
- any diagnostic and semantic events caused by that dispatch stay inside the frame
- `EVENT_COMMAND_END` closes the frame with `success`, `error`, or `unsupported`
- unknown commands follow the same rule: begin, unsupported diagnostic, end

## 8. Directory Property Mutation Contract

Directory/global build-semantics that used to be hidden behind evaluator-private variables must now be represented by first-class events.

`EVENT_DIRECTORY_PROPERTY_MUTATE` and `EVENT_GLOBAL_PROPERTY_MUTATE` carry:
- `property_name`
- `op`
- `modifier_flags`
- `items[]`

Current mutation ops:
- `SET`
- `APPEND_LIST`
- `APPEND_STRING`
- `PREPEND_LIST`

Current modifier flags:
- `BEFORE`
- `SYSTEM`

These events are the canonical way for future consumers to observe directory/global build semantics.

Current directory/global state coverage includes:
- `add_compile_options`
- `add_compile_definitions`
- `add_definitions`
- `remove_definitions`
- `add_link_options`
- `include_directories`
- `link_directories`
- `set_property(DIRECTORY ...)`
- `set_property(GLOBAL ...)`
- `set_directory_properties(...)`

For the directory commands above, the evaluator now keeps property queries and semantic events aligned. `remove_definitions()` publishes the resulting `COMPILE_DEFINITIONS` state through `EVENT_DIRECTORY_PROPERTY_MUTATE(op=SET)` because the contract does not model a dedicated remove op.

The March 6, 2026 G0.4 baseline also locks:
- success and non-OOM error command framing for builtin/function/macro dispatch
- unknown-command framing as `BEGIN -> DIAG -> END`
- include and `add_subdirectory` ordering as `BEGIN -> DIRECTORY_ENTER -> DIRECTORY_LEAVE -> END`
- core directory/global semantic visibility through both events and property queries

## 9. Consumer Guidance

Consumers should:
- derive family from `event_kind_meta(...)`
- filter by `Event_Role`
- rely on stream order or `h.seq`, not evaluator internals
- treat unknown kinds as forward-compatible additions

Future builder guidance:
- consume only `EVENT_ROLE_BUILD_SEMANTIC`
- ignore trace/diagnostic events unless explicitly needed for tooling
