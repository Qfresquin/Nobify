# Evaluator Command Capabilities (Rewrite Draft)

Status: Draft rewrite. This document describes the current command-capability
metadata contract exposed by evaluator APIs and backed by the registry in
`src_v2/evaluator`.

Project priority framing:
- capability metadata is interpreted under the canonical project direction in
  [`../project_priorities.md`](../project_priorities.md),
- the primary goal remains CMake 3.28 semantic parity,
- historical compatibility wrappers are secondary and should not be read as
  outranking remaining CMake 3.28 gaps,
- Nob backend optimization is out of scope for this metadata contract.

## 1. Scope

This document covers:
- capability API surface,
- capability data model (`implemented_level`, `fallback_behavior`),
- registry-backed lookup behavior and normalization rules,
- relationship between capability metadata and runtime dispatch behavior,
- current limitations and non-goals.

It does not provide a command-by-command analytical coverage matrix. That belongs to `evaluator_coverage_matrix.md`, which is now an audit against CMake 3.28 and may intentionally diverge from raw registry metadata when audited behavior is narrower or broader than the static tag.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/evaluator/evaluator.h`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_dispatcher.h`
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/eval_command_registry.h`
- `src_v2/evaluator/eval_command_caps.h`
- `src_v2/evaluator/eval_command_caps.c`

## 3. Capability Data Model

Public model in `evaluator.h`:

```c
typedef enum {
    EVAL_CMD_IMPL_FULL = 0,
    EVAL_CMD_IMPL_PARTIAL,
    EVAL_CMD_IMPL_MISSING,
} Eval_Command_Impl_Level;

typedef enum {
    EVAL_FALLBACK_NOOP_WARN = 0,
    EVAL_FALLBACK_ERROR_CONTINUE,
    EVAL_FALLBACK_ERROR_STOP,
} Eval_Command_Fallback;

typedef struct {
    String_View command_name;
    Eval_Command_Impl_Level implemented_level;
    Eval_Command_Fallback fallback_behavior;
} Command_Capability;
```

Contract intent:
- `implemented_level` describes implementation completeness classification,
- `fallback_behavior` describes intended fallback policy metadata for tooling/docs,
- `command_name` echoes lookup subject (see section 6.3 nuance).

## 4. API Surface

Public API:

```c
bool evaluator_register_native_command(Evaluator_Context *ctx, const Evaluator_Native_Command_Def *def);
bool evaluator_unregister_native_command(Evaluator_Context *ctx, String_View command_name);
bool evaluator_get_command_capability(Evaluator_Context *ctx, String_View command_name, Command_Capability *out_capability);
```

Internal delegated path:

```c
bool eval_dispatcher_get_command_capability(const Evaluator_Context *ctx, String_View name, Command_Capability *out_capability);
bool eval_command_caps_lookup(const Evaluator_Context *ctx, String_View name, Command_Capability *out_capability);
```

Current call chain:
- `evaluator_get_command_capability(...)` delegates to dispatcher,
- dispatcher delegates to `eval_command_caps_lookup(...)`.

Lookup uses evaluator runtime context (`ctx`) and reflects native commands registered in that context.

## 5. Registry Model

Capabilities are registry-driven from one macro list:
- `EVAL_COMMAND_REGISTRY(X)` in `eval_command_registry.h`.

Each registry entry currently carries:
- command name string,
- handler symbol,
- implementation level,
- fallback behavior.

Built-ins are seeded into each context-native registry in `evaluator_create(...)` by expanding this same macro list.

Practical consequence:
- dispatcher command set and capability metadata stay structurally in sync at runtime per context.

## 6. Lookup Contract

### 6.1 Matching

Lookup is:
- delegated to `eval_native_cmd_find_const(...)` over the context-native runtime registry,
- case-insensitive through the same normalized lookup key used by dispatcher routing.

### 6.2 Return Value Semantics

`eval_command_caps_lookup(...)` returns:
- `true` when name is found in registry,
- `false` when name is not found, context is null, or when `out_capability == NULL`.

When `out_capability` is valid and lookup misses, it still writes:
- `implemented_level = EVAL_CMD_IMPL_MISSING`
- `fallback_behavior = EVAL_FALLBACK_NOOP_WARN`

That miss contract also applies when `ctx == NULL`.

### 6.3 `command_name` Field Nuance

On both hit and miss, capability object is built from the input `name` argument, not normalized registry spelling.

Current implication:
- callers should treat `command_name` as echo of query token,
- do not assume canonical lowercase normalization from this field.

## 7. Interpretation of Levels

Current intended meanings:
- `EVAL_CMD_IMPL_FULL`: command path considered substantially implemented in evaluator.
- `EVAL_CMD_IMPL_PARTIAL`: command path exists but has known behavior gaps or reduced feature scope.
- `EVAL_CMD_IMPL_MISSING`: command not represented in the context-native registry.

These are static metadata flags, not runtime proof of success for any specific invocation.

Current evaluator policy for legacy compatibility wrappers:
- a documented evaluator-side subset can still be tagged `FULL` when that reduced subset is the intended contract,
- policy-gated commands such as `build_name()` and `exec_program()` therefore remain `FULL` in capability metadata even though valid execution still depends on `CMP0036` / `CMP0153`,
- reduced-scope metadata wrappers can also be `FULL` when the evaluator intentionally models inspection/state-publication behavior rather than historical native side effects.

## 8. Interpretation of Fallback

Current enum intent:
- `EVAL_FALLBACK_NOOP_WARN`
- `EVAL_FALLBACK_ERROR_CONTINUE`
- `EVAL_FALLBACK_ERROR_STOP`

Current registry usage:
- `NOOP_WARN` is dominant,
- `ERROR_CONTINUE` is used by a subset (for example `cmake_language`, `cmake_path`, `file`),
- `ERROR_STOP` currently has no built-in registry entries.

## 9. Capability Metadata vs Runtime Dispatch

Important boundary in current implementation:
- dispatch routing does not branch on `implemented_level` or `fallback_behavior`.
- runtime behavior is driven by actual handler logic plus evaluator compat/unsupported policies.

Examples:
- unknown command fallback severity is controlled by `ctx->unsupported_policy`, not by missing capability fallback metadata.
- a command labeled `FULL` may still emit runtime diagnostics/errors for invalid usage.
- a command labeled `PARTIAL` may still succeed for many valid subsets.

Capability metadata is therefore introspection/reporting data, not an execution policy engine.

This is the canonical contract:
- `evaluator_get_command_capability(...)` is native-command introspection only,
- `if(COMMAND ...)` is a broader runtime predicate than capability lookup and may succeed for non-native names introduced during evaluation,
- unknown-command fallback continues to follow `CMAKE_NOBIFY_UNSUPPORTED_POLICY`, not capability metadata.

## 10. Native vs Non-Native Command Visibility

Capability lookup currently covers native commands from context registry (built-ins + externally registered natives).

It does not reflect:
- non-native command names introduced during evaluation.

Practical consequence:
- `evaluator_get_command_capability(ctx, "my_func")` returns missing unless `my_func` is native, even if runtime dispatch could call a user command with that name.

## 11. Known-Command Predicate Relationship

`if(COMMAND <name>)` command existence uses:
- native known-command check (`eval_dispatcher_is_known_command(ctx, ...)`)
- or runtime user-command lookup (`eval_user_cmd_find`).

This predicate model is broader than capability API coverage because it includes non-native runtime command names.

## 12. Performance Characteristics

Current lookup complexity:
- native capability lookup reuses the runtime hash-backed native-command index,
- index maintenance happens when the native registry is rebuilt after register/unregister.

## 13. Current Limits and Non-Goals

Current limitations:
- capability lookup does not include non-native runtime command names,
- no dynamic capability downgrade/upgrade by policy/profile at runtime,
- no machine-readable reason field for why a command is `PARTIAL`,
- fallback metadata is intentionally not enforced by dispatcher as policy logic,
- no dedicated API to enumerate full registry with stable ordering/versioning guarantees,
- no parallel capability table exists; metadata is intentionally derived from the same native registry dispatcher uses.

## 14. Relationship to Other Docs

- `evaluator_v2_spec.md`
Top-level canonical contract.

- `evaluator_dispatch.md`
Runtime dispatch path and unknown-command behavior.

- `evaluator_compatibility_model.md`
Policies that actually shape runtime error/stop behavior.

- `evaluator_coverage_matrix.md`
Analytical document for command-by-command coverage tracking.
