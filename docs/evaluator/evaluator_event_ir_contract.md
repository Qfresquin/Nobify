# Evaluator Event IR Contract (Rewrite Draft)

Status: Draft rewrite. This document defines the evaluator-side output contract toward Event IR as currently implemented.

## 1. Scope

This document covers:
- evaluator boundary as Event IR producer,
- stream append/ordering guarantees,
- event header normalization rules,
- payload ownership/lifetime,
- currently emitted event families/kinds,
- known gaps between Event IR schema and evaluator emission coverage.

It does not redefine the full `event_ir.h` schema itself.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/transpiler/event_ir.h`
- `src_v2/transpiler/event_ir.c`
- `src_v2/evaluator/evaluator.h`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_utils.c`
- `src_v2/evaluator/evaluator_internal.h`
- evaluator command modules that call `eval_emit_*` helpers

## 3. Producer Boundary

Evaluator API receives an output stream pointer at creation:

```c
typedef struct {
    Arena *arena;
    Arena *event_arena;
    Event_Stream *stream;
    ...
} Evaluator_Init;
```

Current boundary rules:
- evaluator requires `init->stream != NULL` to create context,
- evaluator does not allocate, reset, or destroy the stream object,
- evaluator appends events into the caller-provided stream.

The evaluator is a producer only; downstream transformation/consumption is out of scope here.

## 4. Stream Model and Ordering

The stream is append-only:
- evaluator uses `event_stream_push(...)`,
- there is no evaluator-side mutation/removal of prior events.

Ordering contract:
- events are appended in execution order,
- nested evaluations (`include`, `add_subdirectory`, deferred calls) append into the same stream and interleave naturally with caller events.

Current sequencing note:
- `Event_Header.seq` exists in schema but is not assigned by current evaluator/event_stream code paths (it remains default `0` unless set externally before push).

## 5. Event Header Normalization

For each pushed event, `event_stream_push(...)` enforces:
- `h.family = event_kind_family(h.kind)` (derived from kind),
- `h.version = 1` when emitter left it as `0`.

Evaluator-specific header population:
- most events go through `eval_emit_event(...)`, which sets:
  - `h.scope_depth = eval_scope_visible_depth(ctx)`
  - `h.policy_depth = eval_policy_visible_depth(ctx)`
- diagnostics (`eval_emit_diag`) set the same depth fields directly before push.

Current version usage:
- generic default: version `1`,
- `EVENT_VAR_SET` and `EVENT_VAR_UNSET` emitted via helper wrappers set `h.version = 2`.

Current flags note:
- `h.flags` is currently left at default `0` by evaluator paths.

## 6. Origin Metadata Rules

Most command-driven events use origin from:

```c
Event_Origin eval_origin_from_node(const Evaluator_Context *ctx, const Node *node);
```

Current behavior:
- `origin.file_path` falls back to `"<input>"` when `ctx->current_file` is absent,
- `origin.line`/`origin.col` come from AST node coordinates when available.

Some synthetic/internal emissions use custom or zeroed origin:
- for example certain expansion diagnostics pass `(Event_Origin){0}`,
- deferred-flush meta events may use partially filled origin data.

Practical consequence:
- consumers must tolerate events with missing/zero origin fields.

## 7. Payload Ownership and Lifetime

Ownership guarantee for pushed events:
- payload `String_View` data is deep-copied into `event_arena` by `event_stream_push(...)` (`event_deep_copy_payload`),
- `Event_Stream.items` storage also lives in `event_arena`.

Current implementation detail:
- many evaluator helpers already copy strings to `event_arena` before push, then `event_stream_push` deep-copies again.
- this duplicates payload bytes but still preserves the same final ownership guarantee.

Lifetime contract:
- event payload remains valid while the owning `event_arena` is alive.

## 8. Emission Failure Semantics

Core emission path:
- `eval_emit_event(...)` calls `event_stream_push(...)`.
- on push failure, evaluator marks OOM via `ctx_oom(...)` (`oom=true`, `stop_requested=true`) and returns `false`.

Call-site behavior:
- many emission sites propagate failure and abort the current command path,
- some telemetry-style sites intentionally ignore return value (`(void)eval_emit_*`), especially in parts of file/package paths.

Contract implication:
- Event IR is best-effort under allocation pressure; missing events are possible when OOM/stop occurs.

## 9. Currently Emitted Event Kinds

Evaluator currently emits this subset of Event IR kinds.

### 9.1 Diagnostics and Meta

- `EVENT_DIAG`
- `EVENT_COMMAND_CALL`
- `EVENT_CMAKE_LANGUAGE_CALL`
- `EVENT_CMAKE_LANGUAGE_EVAL`
- `EVENT_CMAKE_LANGUAGE_DEFER_QUEUE`
- `EVENT_INCLUDE_BEGIN`
- `EVENT_INCLUDE_END`
- `EVENT_ADD_SUBDIRECTORY_BEGIN`
- `EVENT_ADD_SUBDIRECTORY_END`
- `EVENT_DIR_PUSH`
- `EVENT_DIR_POP`

### 9.2 Flow

- `EVENT_FLOW_IF_EVAL`
- `EVENT_FLOW_BRANCH_TAKEN`
- `EVENT_FLOW_LOOP_BEGIN`
- `EVENT_FLOW_LOOP_END`
- `EVENT_FLOW_BREAK`
- `EVENT_FLOW_CONTINUE`
- `EVENT_FLOW_DEFER_QUEUE`
- `EVENT_FLOW_DEFER_FLUSH`
- `EVENT_FLOW_BLOCK_BEGIN`
- `EVENT_FLOW_BLOCK_END`
- `EVENT_FLOW_FUNCTION_BEGIN`
- `EVENT_FLOW_FUNCTION_END`
- `EVENT_FLOW_MACRO_BEGIN`
- `EVENT_FLOW_MACRO_END`

### 9.3 Variables and Properties

- `EVENT_VAR_SET`
- `EVENT_VAR_UNSET`
- `EVENT_TARGET_PROP_SET`

### 9.4 FS / Proc / String / List / Math / Path

- `EVENT_FS_WRITE_FILE`
- `EVENT_FS_APPEND_FILE`
- `EVENT_FS_READ_FILE`
- `EVENT_FS_GLOB`
- `EVENT_FS_MKDIR`
- `EVENT_FS_REMOVE`
- `EVENT_FS_COPY`
- `EVENT_FS_RENAME`
- `EVENT_FS_CREATE_LINK`
- `EVENT_FS_CHMOD`
- `EVENT_FS_ARCHIVE_CREATE`
- `EVENT_FS_ARCHIVE_EXTRACT`
- `EVENT_FS_TRANSFER_DOWNLOAD`
- `EVENT_FS_TRANSFER_UPLOAD`
- `EVENT_PROC_EXEC_REQUEST`
- `EVENT_PROC_EXEC_RESULT`
- `EVENT_STRING_REPLACE`
- `EVENT_STRING_CONFIGURE`
- `EVENT_STRING_REGEX`
- `EVENT_STRING_HASH`
- `EVENT_STRING_TIMESTAMP`
- `EVENT_LIST_APPEND`
- `EVENT_LIST_PREPEND`
- `EVENT_LIST_INSERT`
- `EVENT_LIST_REMOVE`
- `EVENT_LIST_TRANSFORM`
- `EVENT_LIST_SORT`
- `EVENT_MATH_EXPR`
- `EVENT_PATH_NORMALIZE`
- `EVENT_PATH_COMPARE`
- `EVENT_PATH_CONVERT`

### 9.5 Project / Target / Test / Install / CPack / Package

- `EVENT_PROJECT_DECLARE`
- `EVENT_PROJECT_MINIMUM_REQUIRED`
- `EVENT_TARGET_DECLARE`
- `EVENT_TARGET_ADD_SOURCE`
- `EVENT_TARGET_ADD_DEPENDENCY`
- `EVENT_TARGET_LINK_LIBRARIES`
- `EVENT_TARGET_LINK_OPTIONS`
- `EVENT_TARGET_LINK_DIRECTORIES`
- `EVENT_TARGET_INCLUDE_DIRECTORIES`
- `EVENT_TARGET_COMPILE_DEFINITIONS`
- `EVENT_TARGET_COMPILE_OPTIONS`
- `EVENT_TEST_ENABLE`
- `EVENT_TEST_ADD`
- `EVENT_INSTALL_RULE_ADD`
- `EVENT_CPACK_ADD_INSTALL_TYPE`
- `EVENT_CPACK_ADD_COMPONENT_GROUP`
- `EVENT_CPACK_ADD_COMPONENT`
- `EVENT_PACKAGE_FIND_RESULT`

## 10. Key Evaluator-Side Invariants

### 10.1 Command Call Visibility

`EVENT_COMMAND_CALL` is emitted for:
- built-in command matches,
- user function/macro invocations.

Current exception:
- unknown-command fallback emits diagnostic but does not emit `EVENT_COMMAND_CALL`.

### 10.2 Diagnostic Event Semantics

Each successful `eval_emit_diag(...)` push produces one `EVENT_DIAG` carrying:
- effective severity after evaluator compatibility shaping and the final shared-diagnostics strict step,
- classified code/error_class,
- component/command/cause/hint payload.

### 10.3 Directory / Include / Subdirectory Framing

Current patterns:
- include path emits `EVENT_INCLUDE_BEGIN` then `EVENT_DIR_PUSH`, later `EVENT_DIR_POP` and `EVENT_INCLUDE_END(success=...)`.
- add_subdirectory emits `EVENT_ADD_SUBDIRECTORY_BEGIN`, `EVENT_DIR_PUSH`, later `EVENT_DIR_POP`, then `EVENT_ADD_SUBDIRECTORY_END(success=...)`.

### 10.4 Deferred Call Observability

Deferred APIs emit:
- queue-time event (`EVENT_FLOW_DEFER_QUEUE`),
- flush-time batch marker (`EVENT_FLOW_DEFER_FLUSH`).

Queued deferred calls are later executed and can emit their own normal command/flow/diag events.

### 10.5 Variable Event Coverage

Current variable event coverage is selective:
- cache/current var events are emitted only where handlers call `eval_emit_var_*` helpers.
- environment variable writes do not currently emit `EVENT_VAR_TARGET_ENV` events.

## 11. Schema vs Emission Gaps (Current)

Event kinds present in schema but not emitted by current evaluator paths include:
- `EVENT_SCOPE_PUSH`
- `EVENT_SCOPE_POP`
- `EVENT_POLICY_PUSH`
- `EVENT_POLICY_POP`
- `EVENT_POLICY_SET`
- `EVENT_FLOW_RETURN`

Contract implication:
- consumers must not assume full schema coverage from evaluator output today.

## 12. Consumer Guidance

Consumers of evaluator Event IR should:
- trust `h.kind` and normalized `h.family`,
- tolerate missing origin metadata in some events,
- not rely on `h.seq` ordering field (use stream index/order instead),
- tolerate missing events under OOM/stop conditions,
- treat unknown/new event kinds as forward-compatible extensions.

## 13. Relationship to Other Docs

- `evaluator_v2_spec.md`
Top-level canonical evaluator contract.

- `evaluator_runtime_model.md`
Ownership/lifecycle context for stream and arena lifetime.

- `evaluator_execution_model.md`
Execution order that drives event append order.

- `evaluator_diagnostics.md`
Detailed contract for `EVENT_DIAG` production.
