# Evaluator Variables and Scope (Rewrite Draft)

Status: Draft rewrite. This document describes the current variable model implemented by `src_v2/evaluator`, including scope visibility, mutation semantics, macro/function binding differences, and cache/environment interaction.

## 1. Scope

This document covers:
- variable storage and lookup precedence,
- scope-stack visibility and temporary parent/global views,
- `set()` / `unset()` / `option()` / `load_cache()` mutation behavior,
- function vs macro argument bindings (`ARGC`, `ARGV`, `ARGN`, `ARGV<n>`),
- `block(PROPAGATE ...)` and `return(PROPAGATE ...)` variable propagation,
- cache fallback behavior and process-environment interaction.

It does not try to restate all command execution flow details outside variable semantics.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/evaluator/evaluator_internal.h`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_vars.c`
- `src_v2/evaluator/eval_expr.c`
- `src_v2/evaluator/eval_flow.c`
- `src_v2/evaluator/eval_include.c`
- `src_v2/evaluator/eval_utils.c`

## 3. Variable Storage Model

### 3.1 Scope-Local Storage

Each scope is:

```c
typedef struct Var_Scope {
    Eval_Var_Entry *vars;
} Var_Scope;
```

`vars` is an `stb_ds` hash keyed by `char*` and storing `String_View` values.

### 3.2 Cache Storage

Cache variables live in `ctx->cache_entries` (`Eval_Cache_Entry*`) and are not tied to scope depth.

Each cache entry tracks:
- `data` (value),
- `type` (e.g. `BOOL`, `PATH`, `INTERNAL`),
- `doc` (docstring).

### 3.3 Lifetime and Ownership

Current behavior:
- keys and values written through evaluator variable helpers are copied into `event_arena`,
- scope hash tables themselves are heap-managed by `stb_ds`,
- `eval_scope_pop(...)` frees only the popped scope hash table,
- persistent string storage remains arena-owned until `event_arena` is destroyed.

## 4. Lookup and Definition Rules

### 4.1 Visible Lookup

Primary read helper:

```c
String_View eval_var_get_visible(Evaluator_Context *ctx, String_View key);
```

Lookup order:
1. visible scopes from innermost to outermost,
2. cache (`ctx->cache_entries`),
3. empty string if not found.

Practical consequence:
- normal scope bindings shadow cache entries with the same key,
- unsetting a normal binding can expose an older outer-scope or cache value.

### 4.2 Definition Checks

Helpers:
- `eval_var_defined_visible(...)`: true if found in any visible scope or cache,
- `eval_var_defined_current(...)`: true only in the current visible scope.

## 5. Scope Stack and Visibility Depth

The evaluator tracks:
- physical scope storage (`ctx->scopes`),
- visible depth (`ctx->visible_scope_depth`).

Invariants:
- a live context starts with one global scope at visible depth `1`,
- `eval_scope_pop(...)` will not pop below depth `1`.

### 5.1 Push/Pop

`eval_scope_push(...)`:
- increments visible depth,
- reuses an existing physical slot when possible,
- clears any old hash table in the reused slot.

`eval_scope_pop(...)`:
- frees the current scope hash table,
- decrements visible depth (if depth > 1).

### 5.2 Temporary Parent/Global Views

Helpers:
- `eval_scope_use_parent_view(...)`
- `eval_scope_use_global_view(...)`
- `eval_scope_restore_view(...)`

These temporarily change only visible depth; they do not destructively pop scopes.

## 6. Primitive Mutation Helpers

### 6.1 Current-Scope Set/Unset

Helpers:
- `eval_var_set_current(...)`
- `eval_var_unset_current(...)`

Current behavior:
- both require a non-stopped context and visible depth > 0,
- `set_current` writes only in current scope,
- `unset_current` deletes only current-scope binding.

### 6.2 Variable Watch Side Effects

Current local set/unset operations trigger variable-watch notifications when the key is watched:
- writes `NOBIFY_VARIABLE_WATCH_LAST_*`,
- emits a warning diagnostic,
- recursion is guarded by `ctx->in_variable_watch_notification`.

### 6.3 Event Emission Boundary

The primitive helpers do not automatically emit `EVENT_VAR_SET/UNSET` for current-scope mutations.

Current behavior:
- cache-target variable events are emitted by cache-oriented command paths (for example `set(... CACHE ...)`, `unset(... CACHE)`, `option()` write path),
- current-target variable events are emitted only where handlers explicitly call `eval_emit_var_set_current(...)` / `eval_emit_var_unset_current(...)`.

## 7. Command-Level Variable Semantics

### 7.1 `set(...)`

Implemented by `eval_handle_set(...)`.

Supported modes:
- `set(ENV{<name>} [value])`
  - writes process environment (`setenv` / `_putenv_s`),
  - extra args after first value are ignored with warning.
- `set(<var> <value>... CACHE <type> <doc> [FORCE])`
  - writes/updates cache entry,
  - supports `BOOL`, `FILEPATH`, `PATH`, `STRING`, `INTERNAL`,
  - `INTERNAL` implies `FORCE`.
- `set(<var>)`
  - unsets current-scope normal binding.
- `set(<var> <value>... [PARENT_SCOPE])`
  - writes current scope by default,
  - with `PARENT_SCOPE`, temporarily switches to parent view and writes there.

Policy-sensitive behavior:
- `CMP0126` affects removal of local normal binding when writing `CACHE` entries.

### 7.2 `unset(...)`

Implemented by `eval_handle_unset(...)`.

Supported modes:
- `unset(ENV{<name>})` (no extra options),
- `unset(<var>)` (current scope),
- `unset(<var> PARENT_SCOPE)` (parent visible scope),
- `unset(<var> CACHE)` (cache removal + cache-target unset event).

Invalid `PARENT_SCOPE` usage without a parent depth emits an error diagnostic.

### 7.3 Other Variable-Oriented Commands

- `option(...)`
  - writes typed `BOOL` cache entry when appropriate,
  - `CMP0077` controls interaction with pre-existing normal bindings.
- `mark_as_advanced(...)`
  - updates cache advanced metadata; `CMP0102` controls implicit cache creation.
- `load_cache(...)`
  - supports `READ_WITH_PREFIX` into normal variables,
  - otherwise imports selected entries into cache store.
- `cmake_parse_arguments(...)`
  - writes/unsets prefix result variables in current scope,
  - `PARSE_ARGV` signature is function-scope only,
  - `CMP0174` affects whether empty single-value keyword args are treated as defined.
- `separate_arguments(...)`
  - reads and rewrites an output variable in current scope.

## 8. Function vs Macro Variable Semantics

### 8.1 `function()` Calls

Current behavior in `eval_user_cmd_invoke(...)`:
- pushes a new variable scope,
- binds declared parameters as normal variables,
- sets `ARGC`, `ARGV`, `ARGN`, `ARGV0..ARGVn` as normal variables in that function scope,
- pops scope on exit.

### 8.2 `macro()` Calls

Current behavior:
- does not push a variable scope,
- pushes a macro frame (`ctx->macro_frames`),
- stores parameter and implicit argument bindings in macro-frame bindings.

### 8.3 Precedence During Reads

For expansion/truthiness paths that consult macro bindings:
- macro binding lookup is attempted first,
- visible normal/cache variable lookup is fallback.

This is used by:
- `eval_expand_vars(...)` for `${...}`,
- `eval_truthy(...)` for bare-token truthiness.

## 9. Expansion and `if()` Variable Semantics

### 9.1 Expansion

`eval_expand_vars(...)` supports:
- `${VAR}`,
- nested `${${VAR}}`,
- `$ENV{VAR}`,
- escaped `\${...}`.

Current behavior:
- undefined `${VAR}` resolves to empty string,
- expansion is iterative with cycle detection,
- recursion limit defaults to `100` and can be tuned (bounded by a hard cap) via:
  - `CMAKE_NOBIFY_EXPAND_MAX_RECURSION`,
  - `NOBIFY_EVAL_EXPAND_MAX_RECURSION` (environment).

### 9.2 `if(DEFINED ...)`

Current behavior:
- `DEFINED ENV{X}` checks process environment (`eval_has_env(...)`),
- `DEFINED X` checks visible normal+cache definitions (`eval_var_defined_visible(...)`).

## 10. Parent, Block, Return, and Global Writes

### 10.1 Parent-Scope Writes

`set(... PARENT_SCOPE)` and `unset(... PARENT_SCOPE)`:
- temporarily lower visible depth with `eval_scope_use_parent_view(...)`,
- perform current-scope operation in that parent view,
- restore prior visibility.

### 10.2 `block(PROPAGATE ...)`

`block()` may push variable/policy scopes. On `endblock()`:
- propagated vars are copied from current block scope to parent scope when configured.

On return-unwind (`eval_unwind_blocks_for_return(...)`):
- block frames are popped and optionally propagate according to `propagate_on_return`.

### 10.3 `return(PROPAGATE ...)`

Under `CMP0140 NEW`, `return(PROPAGATE <vars...>)` stores requested variable list.

Current function cleanup then:
- copies currently defined requested vars from function scope into parent scope,
- clears return state,
- pops function scope.

`return()` inside macro context is rejected.

### 10.4 Global View Writes

There is no public `set(... GLOBAL_SCOPE)` form.

Current code paths needing global-only writes (for example include-guard internals) use:
- `eval_scope_use_global_view(...)` + normal current-scope set/unset + restore.

## 11. Scope Boundaries Across Constructs

Current scope boundaries:
- `function()` and `block(SCOPE_FOR VARIABLES ...)` push variable scope,
- `add_subdirectory` nested file execution pushes variable scope,
- `include()` nested execution does not push variable scope by itself,
- `macro()` does not push scope (uses macro frame only).

Loop note:
- `foreach()` mutates loop variable in current scope each iteration,
- `CMP0124 NEW` restores/unsets loop variable after loop completion.

## 12. Cache Interaction Model

Cache behavior is fallback-based:
- normal scope lookup has priority,
- cache answers only when no visible normal binding exists.

Additional current behavior:
- `eval_var_defined_visible(...)` treats cache entries as defined,
- `set(... CACHE PATH|FILEPATH ...)` may normalize previously untyped relative cache values to absolute paths,
- cache entries persist across scope push/pop within the same evaluator context.

## 13. Environment Interaction Model

Environment variables are process-backed, not scope-backed.

Read paths:
- `$ENV{X}` expansion,
- `if(DEFINED ENV{X})`,
- utility calls via `eval_getenv_temp(...)`.

Write paths:
- `set(ENV{X} ...)`,
- `unset(ENV{X})`.

Platform note:
- Windows environment reads/checks in these helpers normalize names to uppercase before lookup.

## 14. Current Limits and Non-Goals

Current limitations visible in implementation:
- `DEFINED <name>` does not consult macro-frame bindings.
- There is no dedicated public API for global-scope writes; internal paths use temporary visibility switching.
- Current-scope variable mutations are not universally mirrored into var-set/var-unset events unless handlers emit them.
- Cache storage is global to context, not scoped by block/function depth.

## 15. Relationship to Other Docs

- `evaluator_v2_spec.md`
  - canonical top-level evaluator contract.
- `evaluator_runtime_model.md`
  - context lifecycle and state ownership that this variable model uses.
- `evaluator_execution_model.md`
  - traversal/control-flow behavior that triggers these scope transitions.
- `evaluator_expressions.md`
  - should hold the full expression grammar/semantics that consume these lookup rules.
- `evaluator_event_ir_contract.md`
  - should define event-level guarantees for variable/cache mutation observability.
