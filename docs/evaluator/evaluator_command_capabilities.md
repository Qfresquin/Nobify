# Evaluator Command Capabilities (Rewrite Draft)

Status: Draft rewrite. This document describes the current command-capability metadata contract exposed by evaluator APIs and backed by the registry in `src_v2/evaluator`.

## 1. Scope

This document covers:
- capability API surface,
- capability data model (`implemented_level`, `fallback_behavior`),
- registry-backed lookup behavior and normalization rules,
- relationship between capability metadata and runtime dispatch behavior,
- current limitations and non-goals.

It does not provide a command-by-command analytical coverage matrix. That belongs to `evaluator_coverage_matrix.md`.

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
- linear scan over the context-native registry,
- case-insensitive by command name (`eval_sv_eq_ci_lit`).

### 6.2 Return Value Semantics

`eval_command_caps_lookup(...)` returns:
- `true` when name is found in registry,
- `false` when name is not found, context is null, or when `out_capability == NULL`.

When not found and `out_capability` is valid, it still writes:
- `implemented_level = EVAL_CMD_IMPL_MISSING`
- `fallback_behavior = EVAL_FALLBACK_NOOP_WARN`

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

## 10. Native vs User-Defined Command Visibility

Capability lookup currently covers native commands from context registry (built-ins + externally registered natives).

It does not reflect:
- user-defined `function()`/`macro()` commands registered at runtime.

Practical consequence:
- `evaluator_get_command_capability(ctx, "my_func")` returns missing unless `my_func` is native, even if runtime dispatch could call a user command with that name.

## 11. Known-Command Predicate Relationship

`if(COMMAND <name>)` command existence uses:
- native known-command check (`eval_dispatcher_is_known_command(ctx, ...)`)
- or runtime user-command lookup (`eval_user_cmd_find`).

This predicate model is broader than capability API coverage because it includes user commands.

## 12. Performance Characteristics

Current lookup complexity:
- O(N) linear scan on each query.

There is no hash/index cache for capabilities in current implementation.

## 13. Current Limits and Non-Goals

Current limitations:
- capability lookup does not include user-defined commands,
- no dynamic capability downgrade/upgrade by policy/profile at runtime,
- no machine-readable reason field for why a command is `PARTIAL`,
- fallback metadata is not currently enforced by dispatcher as policy logic,
- no dedicated API to enumerate full registry with stable ordering/versioning guarantees.

## 14. Relationship to Other Docs

- `evaluator_v2_spec.md`
Top-level canonical contract.

- `evaluator_dispatch.md`
Runtime dispatch path and unknown-command behavior.

- `evaluator_compatibility_model.md`
Policies that actually shape runtime error/stop behavior.

- `evaluator_coverage_matrix.md`
Analytical document for command-by-command coverage tracking.
