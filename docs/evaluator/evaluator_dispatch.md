# Evaluator Dispatch (Rewrite Draft)

Status: Draft rewrite. This document describes how `NODE_COMMAND` nodes are routed to handlers in the current evaluator implementation.

## 1. Scope

This document covers:
- built-in command routing and table construction,
- handler invocation contract and stop-state behavior,
- user-defined function/macro fallback dispatch,
- unknown-command policy behavior,
- command capability metadata lookup integration.

It does not attempt to restate per-command semantic behavior.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/evaluator/eval_dispatcher.h`
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/eval_command_registry.h`
- `src_v2/evaluator/eval_command_caps.h`
- `src_v2/evaluator/eval_command_caps.c`
- `src_v2/evaluator/evaluator_internal.h`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_expr.c`

## 3. Dispatch Entry Points

Public/internal entry points:

```c
Eval_Result eval_dispatch_command(Evaluator_Context *ctx, const Node *node);
bool eval_dispatcher_is_known_command(const Evaluator_Context *ctx, String_View name);
bool eval_dispatcher_get_command_capability(const Evaluator_Context *ctx, String_View name, Command_Capability *out_capability);
bool eval_dispatcher_seed_builtin_commands(Evaluator_Context *ctx);
```

Top-level API surface:

```c
bool evaluator_register_native_command(Evaluator_Context *ctx, const Evaluator_Native_Command_Def *def);
bool evaluator_unregister_native_command(Evaluator_Context *ctx, String_View command_name);
bool evaluator_get_command_capability(Evaluator_Context *ctx, String_View command_name, Command_Capability *out_capability);
```

`evaluator_get_command_capability(...)` delegates directly to dispatcher capability lookup.

## 4. Native Registry Runtime Model

### 4.1 Registry as Single Source

Built-ins are defined in one macro registry:
- `EVAL_COMMAND_REGISTRY(X)` in `eval_command_registry.h`.

Each entry carries:
- command name,
- handler symbol,
- implementation level metadata,
- fallback metadata.

### 4.2 Runtime Registry Construction

`evaluator_create(...)` calls `eval_dispatcher_seed_builtin_commands(...)`, which expands
`EVAL_COMMAND_REGISTRY(X)` and registers each built-in into the context registry.

Current routing mechanics:
- linear scan over `ctx->native_commands`,
- case-insensitive name comparison.

Current implication:
- dispatch and capability lookup cost are O(N) in registered command count.

## 5. Command Dispatch Pipeline

### 5.1 Guard Conditions

`eval_dispatch_command(...)` returns `EVAL_RESULT_FATAL` immediately when:
- `ctx == NULL`,
- evaluator is in stop state,
- `node == NULL`,
- `node->kind != NODE_COMMAND`.

### 5.2 Native Match Path

On first native registry name match:
1. emit `EVENT_COMMAND_CALL`,
2. invoke handler,
3. return handler `Eval_Result` merged with runtime stop state.

Current event helper:
- `eval_emit_command_call(...)` copies command name to `event_arena`.

### 5.3 User Command Fallback Path

If no native command matches:
1. refresh runtime compatibility knobs (`eval_refresh_runtime_compat(...)`),
2. lookup user command (`eval_user_cmd_find(...)`),
3. if found, resolve args with `eval_resolve_args_literal(...)` for macros or `eval_resolve_args(...)` for functions,
4. emit `EVENT_COMMAND_CALL`,
5. invoke via `eval_user_cmd_invoke(...)`.

Return behavior on user command invoke:
- if invoke finishes without stop-state, dispatcher keeps execution in non-fatal state,
- if invoke enters stop-state, dispatcher returns `EVAL_RESULT_FATAL`.

Practical consequence:
- non-stop execution failures in user invocation are treated as recoverable at dispatcher return boundary.

### 5.4 Unknown Command Path

If native and user lookup both miss:
- emit diagnostic component `dispatcher`, cause `"Unknown command"`,
- diagnostic severity depends on `ctx->unsupported_policy`,
- dispatcher returns `EVAL_RESULT_SOFT_ERROR` for non-fatal unknown-command diagnostics, or `EVAL_RESULT_FATAL` if policy/stop state escalates.

Current hint text:
- `EVAL_UNSUPPORTED_NOOP_WARN`: `"No-op with warning by policy"`,
- otherwise: `"Ignored during evaluation"`.

Important event detail:
- unknown commands currently emit diagnostic events, but no `EVENT_COMMAND_CALL`.

## 6. Handler Contract

Handler type:

```c
typedef Eval_Result (*Cmd_Handler)(Evaluator_Context *ctx, const Node *node);
```

Current contract shape:
- `EVAL_RESULT_OK` means clean execution,
- `EVAL_RESULT_SOFT_ERROR` means non-fatal diagnostics happened,
- `EVAL_RESULT_FATAL` means hard stop path (commonly OOM/stop),
- dispatcher merges handler result with runtime stop-state.

## 7. Stop and Error Propagation

Dispatch is stop-aware, not exception-based.

Current behavior:
- dispatch will not start when stop is already set,
- any path that sets stop (`oom` or explicit request) makes dispatch return `false`,
- diagnostic escalation policy can indirectly cause stop (through compat/report pipeline), affecting dispatch result.

## 8. Unknown-Command Policy Integration

Policy source:
- `ctx->unsupported_policy`.

Current enum:
- `EVAL_UNSUPPORTED_WARN`
- `EVAL_UNSUPPORTED_ERROR`
- `EVAL_UNSUPPORTED_NOOP_WARN`

Runtime refresh:
- `eval_refresh_runtime_compat(...)` can update `unsupported_policy` from variable `CMAKE_NOBIFY_UNSUPPORTED_POLICY`.

Current severity mapping in dispatcher unknown path:
- default warning,
- error when policy is `EVAL_UNSUPPORTED_ERROR`.

## 9. Capability Metadata Integration

### 9.1 Data Model

Capability type:

```c
typedef struct {
    String_View command_name;
    Eval_Command_Impl_Level implemented_level;
    Eval_Command_Fallback fallback_behavior;
} Command_Capability;
```

Implementation level enum:
- `EVAL_CMD_IMPL_FULL`
- `EVAL_CMD_IMPL_PARTIAL`
- `EVAL_CMD_IMPL_MISSING`

Fallback enum:
- `EVAL_FALLBACK_NOOP_WARN`
- `EVAL_FALLBACK_ERROR_CONTINUE`
- `EVAL_FALLBACK_ERROR_STOP`

### 9.2 Lookup Behavior

`eval_command_caps_lookup(...)`:
- scans registry-derived capability table by case-insensitive name,
- writes matching metadata and returns `true` on hit,
- on miss writes default `implemented_level = EVAL_CMD_IMPL_MISSING`, `fallback_behavior = EVAL_FALLBACK_NOOP_WARN`, and returns `false`.

### 9.3 Current Boundary vs Runtime Routing

Current implementation uses capability metadata for introspection/API only.

Current dispatch routing does not dynamically branch on:
- `implemented_level`,
- `fallback_behavior`.

Those fields are documentation/reporting metadata in current code paths.

## 10. Relationship With Expression Semantics

`if(COMMAND <name>)` currently resolves command existence by:
- native registry presence (`eval_dispatcher_is_known_command(ctx, ...)`), or
- user command registration (`eval_user_cmd_find(...)`).

This means `COMMAND` predicates observe the same command namespace model used by dispatcher routing.

## 11. Current Limits and Non-Goals

Current limitations visible in implementation:
- native lookup is linear (no hash/indexed dispatcher).
- unknown-command handling is generic; per-command fallback metadata is not applied at runtime.
- there is no `EVENT_COMMAND_CALL` emission for unknown-command attempts.
- dispatcher contract is `NODE_COMMAND`-only; structural nodes (`if`, `while`, etc.) are routed elsewhere.

## 12. Relationship to Other Docs

- `evaluator_v2_spec.md`
  - canonical evaluator contract.
- `evaluator_execution_model.md`
  - where `NODE_COMMAND` dispatch is invoked from node execution.
- `evaluator_variables_and_scope.md`
  - variable and argument resolution behavior used before user-command invocation.
- `evaluator_command_capabilities.md`
  - should detail the capability matrix and intended semantics of implementation/fallback metadata.
- `evaluator_diagnostics.md`
  - diagnostic emission and stop escalation behavior used by unknown-command handling.
