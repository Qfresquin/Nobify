# Evaluator Event IR Contract

Status: active contract for the current evaluator output.

Project priority framing:
- this contract exists to preserve evaluator-reconstructed CMake 3.28 semantics
  as a stable boundary before build-model reconstruction and later Nob
  optimization,
- historical behavior is secondary unless it affects those observable
  semantics.

## 1. Scope

This document defines the evaluator as an `Event IR` producer:
- stream ownership expectations
- ordering and sequencing
- event classes emitted by the evaluator
- command trace guarantees
- directory/global semantic guarantees

It does not redefine the full schema in `src_v2/transpiler/event_ir.h`.

## 2. Boundary

The evaluator boundary remains:

`AST -> evaluator -> Event_Stream`

`Evaluator_Init` still receives:

```c
typedef struct {
    Arena *arena;
    Arena *event_arena;
    Event_Stream *stream;
    ...
} Evaluator_Init;
```

Boundary rules:
- evaluator requires `init->stream != NULL`
- evaluator appends into the caller-provided stream
- evaluator does not own stream destruction
- downstream builder/model concerns remain out of scope

## 3. Stream and Ordering

The evaluator emits into one append-only stream.

Ordering guarantees:
- events are appended in execution order
- nested evaluations append into the same stream
- includes, subdirectories, deferred calls, functions and macros naturally interleave in one linear order

Sequencing guarantees:
- `event_stream_push(...)` assigns `h.seq` when the emitter leaves it at `0`
- successful pushes therefore get monotonic stream-local sequence numbers
- `event_stream_push(...)` accepts only canonical `Event_Kind` values with registered metadata

## 4. Header Population

Evaluator-populated per-instance fields:
- `h.kind`
- `h.origin`
- `h.scope_depth`
- `h.policy_depth`
- optional explicit `h.version`

Stream-populated normalization:
- `h.version` defaults from kind metadata when left as `0`
- `h.seq` defaults from stream state when left as `0`

Derived data:
- family is derived from `event_kind_meta(h.kind)`
- role classification is derived from `event_kind_has_role(...)`

## 5. Payload Ownership

Ownership guarantee:
- payload data is made stable by `event_stream_push(...)`
- copied payload storage lives in the stream arena
- event payload remains valid while the stream arena lives

Practical note:
- some evaluator paths still prepare stable strings in `event_arena` before push for internal convenience
- consumers must still treat `event_stream_push(...)` as the only supported ownership contract
- legacy compatibility sentinels in `event_ir.h` are source-compatibility aliases only, not valid emitted kinds

## 6. Command Trace Guarantees

Every dispatched command now emits trace framing:
- `EVENT_COMMAND_BEGIN`
- `EVENT_COMMAND_END`

This includes:
- built-in commands
- user functions
- user macros
- unknown commands

`dispatch_kind` values:
- `builtin`
- `function`
- `macro`
- `unknown`

`status` values in `EVENT_COMMAND_END`:
- `success`
- `error`
- `unsupported`

Unknown-command contract:
- `EVENT_COMMAND_BEGIN(dispatch=unknown)`
- one diagnostic event with the normal unsupported-policy severity
- `EVENT_COMMAND_END(dispatch=unknown, status=unsupported)`

Sequencing contract:
- `EVENT_COMMAND_BEGIN` opens the dispatched-command frame
- diagnostics and semantic side-effect events produced by that dispatch occur before the matching `EVENT_COMMAND_END`
- `EVENT_COMMAND_END` closes the frame even on non-OOM error/unsupported paths when the allow-stopped emission path can still run
- the March 6, 2026 baseline also locks the success path for builtin/function/macro dispatch, so both success and non-OOM failure paths stay framed by matching begin/end events

## 7. Directory and Build-Semantic Guarantees

Directory context is first-class:
- `EVENT_DIRECTORY_ENTER`
- `EVENT_DIRECTORY_LEAVE`

Current include/subdirectory framing:
- include emits `EVENT_INCLUDE_BEGIN`, `EVENT_DIRECTORY_ENTER`, later `EVENT_DIRECTORY_LEAVE`, `EVENT_INCLUDE_END`
- add_subdirectory emits `EVENT_ADD_SUBDIRECTORY_BEGIN`, `EVENT_DIRECTORY_ENTER`, later `EVENT_DIRECTORY_LEAVE`, `EVENT_ADD_SUBDIRECTORY_END`

Directory/global build semantics are also first-class:
- `EVENT_DIRECTORY_PROPERTY_MUTATE`
- `EVENT_GLOBAL_PROPERTY_MUTATE`

This contract now covers evaluator output for directory-style commands such as:
- `add_compile_options`
- `add_compile_definitions`
- `add_definitions`
- `remove_definitions`
- `add_link_options`
- `include_directories`
- `link_directories`

It also covers `set_property(DIRECTORY ...)` and `set_property(GLOBAL ...)` through the same semantic event family.
The same directory mutators now keep `get_property(DIRECTORY PROPERTY ...)` and `get_directory_property(...)` aligned with the emitted semantic events; synthetic `NOBIFY_GLOBAL_*` variables remain evaluator-private compatibility state.

Current G0.4 baseline coverage also locks:
- metadata resolution for canonical kinds used by the evaluator baseline
- deep-copy ownership at `event_stream_push(...)` for manual stream producers
- include and `add_subdirectory` ordering as `BEGIN -> DIRECTORY_ENTER -> DIRECTORY_LEAVE -> END`
- directory/global property query visibility for the emitted semantic state

## 8. Role-Oriented Emission

Evaluator output now spans several roles in one stream.

Important emitted role groups:
- `TRACE`: command framing, include/subdirectory framing, `cmake_language(*)`
- `DIAGNOSTIC`: `EVENT_DIAG`
- `BUILD_SEMANTIC`: directory, project, target, test, install, cpack, package events
- `STATE`: variables, scopes, policies, directory/global property mutation
- `RUNTIME_EFFECT`: file/process/string/list/math/path operational events

Consumers should not infer “builder relevance” from family names alone. They should filter by `EVENT_ROLE_BUILD_SEMANTIC`.

## 9. Current Emission Notes

Stable emitted categories include:
- diagnostics
- command trace begin/end
- include/add_subdirectory trace
- directory enter/leave
- directory/global property mutation
- project/target/test/install/cpack/package events
- flow events used by functions, macros, blocks, loops and defer
- file/process/string/list/math/path events already modeled by evaluator helpers

Known gaps that remain semantic, not architectural:
- `SCOPE` and `POLICY` schema surface is broader than current emission coverage
- variable event coverage is still selective by handler
- environment variable writes are not yet universally mirrored as `EVENT_VAR_TARGET_ENV`

## 10. Failure Semantics

On stream push failure:
- evaluator marks OOM
- current command path aborts
- downstream consumers must tolerate truncated streams under allocation failure

Special case:
- `EVENT_COMMAND_END` uses the allow-stopped emission path when possible so command trace can remain paired after non-OOM command failures

## 11. Consumer Guidance

Consumers should:
- trust `h.kind`
- derive family and roles from kind metadata
- rely on stream order or `h.seq`
- tolerate missing events after OOM
- treat unknown future kinds as forward-compatible extensions
