# Evaluator Compatibility Model (Rewrite Draft)

Status: Draft rewrite. This document describes the current compatibility controls implemented by `src_v2/evaluator`, including evaluator profiles, unsupported-command policy, budget/stop behavior, and the policy-engine axis (`cmake_policy`/`CMP*`).

## 1. Scope

This document covers:
- evaluator compatibility profiles (`PERMISSIVE`, `STRICT`, `CI_STRICT`),
- runtime compatibility variables (`CMAKE_NOBIFY_*`),
- unknown-command policy behavior,
- error-budget and continue-on-error stop decisions,
- the separate CMake policy engine axis (`cmake_policy`, `CMAKE_POLICY_VERSION`, `CMP*`),
- current divergences and limitations.

It does not restate detailed command semantics outside compatibility behavior.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/evaluator/evaluator.h`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_compat.c`
- `src_v2/evaluator/eval_compat.h`
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/eval_policy_engine.c`
- `src_v2/evaluator/eval_project.c`
- `src_v2/evaluator/eval_include.c`
- `src_v2/evaluator/eval_flow.c`
- `src_v2/evaluator/eval_report.c`

## 3. Compatibility Axes Overview

Current evaluator behavior is controlled by two main axes plus local knobs:

1. Evaluator profile axis
   - `EVAL_PROFILE_PERMISSIVE`
   - `EVAL_PROFILE_STRICT`
   - `EVAL_PROFILE_CI_STRICT`

2. CMake policy axis
   - policy stack + effective status resolution for `CMPxxxx`
   - commands such as `cmake_policy(...)` and `cmake_minimum_required(...)`

3. Local compatibility knobs
   - unsupported-command policy (`EVAL_UNSUPPORTED_*`)
   - error budget
   - continue-on-error
   - feature-specific strictness toggles (for example `CMAKE_NOBIFY_FILE_GLOB_STRICT`)

These controls are related but not identical in implementation path.

## 4. Bootstrap Defaults

At `evaluator_create(...)` time, the runtime starts with:
- `compat_profile = EVAL_PROFILE_PERMISSIVE` (unless init requests `STRICT` or `CI_STRICT`)
- `unsupported_policy = EVAL_UNSUPPORTED_WARN`
- `error_budget = 0`

And these compatibility variables are seeded:
- `CMAKE_NOBIFY_CONTINUE_ON_ERROR`
- `CMAKE_NOBIFY_COMPAT_PROFILE`
- `CMAKE_NOBIFY_ERROR_BUDGET`
- `CMAKE_NOBIFY_UNSUPPORTED_POLICY`
- `CMAKE_NOBIFY_FILE_GLOB_STRICT`

Policy bootstrap:
- `CMAKE_POLICY_VERSION` starts as empty
- `NOBIFY_POLICY_STACK_DEPTH` starts as `"1"`

Current budget meaning:
- `0` means unlimited errors in permissive budget logic.

## 5. Control Interfaces

### 5.1 Public API

Top-level setter:

```c
bool evaluator_set_compat_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile);
```

This delegates to `eval_compat_set_profile(...)`.

### 5.2 Runtime Variable Controls

Compatibility state can also be controlled by variables visible to evaluator logic:
- `CMAKE_NOBIFY_COMPAT_PROFILE`: `PERMISSIVE` / `STRICT` / `CI_STRICT`
- `CMAKE_NOBIFY_UNSUPPORTED_POLICY`: `WARN` / `ERROR` / `NOOP_WARN`
- `CMAKE_NOBIFY_ERROR_BUDGET`: unsigned decimal
- `CMAKE_NOBIFY_CONTINUE_ON_ERROR`: truthy/falsy

Current parsing behavior:
- profile and unsupported policy parsing are case-insensitive,
- unknown profile values fall back to `PERMISSIVE`,
- unknown unsupported-policy values fall back to `WARN`,
- invalid budget strings are ignored (previous `ctx->error_budget` is kept).

### 5.3 Refresh Timing

Compatibility variables are refreshed once per command evaluation cycle.

Current core refresh point:
- `eval_node(...)` calls `eval_refresh_runtime_compat(...)` before routing the node.

Current snapshot contents:
- `CMAKE_NOBIFY_COMPAT_PROFILE`
- `CMAKE_NOBIFY_UNSUPPORTED_POLICY`
- `CMAKE_NOBIFY_ERROR_BUDGET`
- `CMAKE_NOBIFY_CONTINUE_ON_ERROR`

Practical consequence:
- compatibility decisions made while executing command `N` read one stable snapshot,
- writes to those variables during command `N` become effective on command `N+1` by default,
- dispatcher unknown-command fallback and diagnostic handling do not perform their own ad hoc refresh.

## 6. Profile Semantics

### 6.1 Severity Shaping

`eval_compat_effective_severity(...)` applies profile shaping:
- permissive keeps warnings as warnings,
- strict and ci_strict promote warnings to errors.

This effective severity drives:
- the evaluator-side input severity passed to the shared diagnostics layer.

Then the shared diagnostics layer applies its own final strict-mode escalation through `diag_effective_severity(...)`.

That final severity drives:
- `EVENT_DIAG` severity,
- run-report warning/error counters,
- diagnostic stop decision in `eval_compat_decide_on_diag(...)`,
- severity rendered by `diag_log(...)`.

### 6.2 `STRICT` vs `CI_STRICT`

Current code path treats `EVAL_PROFILE_STRICT` and `EVAL_PROFILE_CI_STRICT` equivalently for:
- warning promotion,
- post-diagnostic stop decision branch.

No extra CI-only behavior is currently implemented in `eval_compat.c`.

### 6.3 API Setter Side Effects

`eval_compat_set_profile(...)` writes both:
- `CMAKE_NOBIFY_COMPAT_PROFILE`
- `CMAKE_NOBIFY_CONTINUE_ON_ERROR`

Current default coupling:
- permissive sets continue-on-error to truthy (`"1"`),
- strict/ci_strict set continue-on-error to falsy (`"0"`).

## 7. Unsupported Command Policy

Unknown-command path is implemented in dispatcher fallback.

Current enum:
- `EVAL_UNSUPPORTED_WARN`
- `EVAL_UNSUPPORTED_ERROR`
- `EVAL_UNSUPPORTED_NOOP_WARN`

Current behavior on unresolved command:
- emits one diagnostic with component `"dispatcher"` and cause `"Unknown command"`,
- severity is warning by default, error when policy is `EVAL_UNSUPPORTED_ERROR`,
- hint text differs for `NOOP_WARN` vs default warning path,
- command is behaviorally ignored (no implementation fallback execution).

Current event shape:
- unknown commands emit diagnostic events,
- they do not emit `EVENT_COMMAND_CALL`.

## 8. Error Budget Model

Budget source:
- `ctx->error_budget`, refreshed from `CMAKE_NOBIFY_ERROR_BUDGET` at command-cycle entry when parsing succeeds.

Decision path:
- `eval_compat_decide_on_diag(...)` runs after `eval_report_record_diag(...)`.
- It only acts on diagnostics whose effective severity is error.

Current permissive behavior:
- if budget is `0`, no budget stop is applied,
- if budget > `0`, evaluator requests stop when `run_report.error_count >= error_budget`.

Important ordering detail:
- the current diagnostic is already counted when the threshold check runs.

## 9. Continue-on-Error and Stop Requests

`eval_request_stop_on_error(...)` behavior:
- checks the current-cycle `continue_on_error_snapshot` through `eval_continue_on_error(...)`,
- if truthy, stop is not requested,
- otherwise `stop_requested` is set.

This helper is used in strict-profile diagnostic decisions and in several command handlers.

Important distinction:
- direct `eval_request_stop(...)` bypasses continue-on-error and always sets stop.

Practical consequence:
- a script can force continuation even under strict profile by toggling `CMAKE_NOBIFY_CONTINUE_ON_ERROR` truthy,
- but call sites that use direct `eval_request_stop(...)` still stop unconditionally.

## 10. CMake Policy Engine Axis

Compatibility also depends on policy-status resolution, separate from evaluator profile.

### 10.1 Policy Storage and Depth

Current storage model:
- stack of `Eval_Policy_Level` with slot states per known policy id,
- visible depth tracked by `visible_policy_depth`,
- `NOBIFY_POLICY_STACK_DEPTH` mirrors visible depth.

### 10.2 Known Policy Range

Current known id range:
- `CMP0000` through `CMP0155` (`0..155` in internal numeric form).

### 10.3 Effective Status Resolution

`eval_policy_get_effective(...)` resolves in this order:
1. explicit state from visible policy stack (including inherited slots),
2. `CMAKE_POLICY_DEFAULT_CMPxxxx`,
3. derived status from `CMAKE_POLICY_VERSION` vs policy introduction version,
4. unset/empty when no rule applies.

### 10.4 Command Integration

Current integration points:
- `cmake_policy(VERSION|SET|GET|PUSH|POP)` manipulates policy engine state directly,
- `cmake_minimum_required(VERSION ...)` sets `CMAKE_POLICY_VERSION` and applies defaults across known policies,
- `include()` pushes a policy scope unless `NO_POLICY_SCOPE` is requested,
- `block()` can push/pull policy scope depending on options.

### 10.5 Error Handling

Policy stack misuse (for example `POP` underflow) emits diagnostics and may request stop via the standard stop helpers.

## 11. Additional Compatibility-Like Knobs

Some strictness controls are feature-local instead of flowing through `eval_compat.c`.

Example:
- `CMAKE_NOBIFY_FILE_GLOB_STRICT` controls whether `file(GLOB)` directory-open failures emit warning or error.

This means compatibility behavior is partly centralized (`eval_compat.c`) and partly command-specific.

## 12. Relationship With Diagnostics and Reporting

Compatibility decisions affect diagnostics at multiple levels:
- severity shaping before event/log emission,
- stop decisions after run-report counter updates,
- unsupported policy in dispatcher fallback.

Shared logger interaction:
- evaluator-level compatibility still performs the first shaping stage,
- process-global diagnostics strict mode is the final severity authority consumed by evaluator event/report/gating paths as well as the external log line.

## 13. Current Divergences and Limits

Current intentional/visible limits:
- evaluator profiles are an evaluator-specific layer, not native CMake profile concepts.
- `STRICT` and `CI_STRICT` currently behave the same in compatibility core logic.
- unknown-command policy only affects unresolved command fallback; capability metadata is not dynamically enforced.
- compatibility decisions are intentionally snapshot-based at command-cycle granularity.
- continuation under strict can still be forced via `CMAKE_NOBIFY_CONTINUE_ON_ERROR`.

## 14. Relationship to Other Docs

- `evaluator_v2_spec.md`
Top-level canonical contract.

- `evaluator_runtime_model.md`
Lifecycle/ownership model for compatibility state storage.

- `evaluator_dispatch.md`
Unknown-command fallback and unsupported policy application.

- `evaluator_diagnostics.md`
Diagnostic emission pipeline, severity shaping, and stop behavior.

- `evaluator_execution_model.md`
Where compatibility outcomes influence traversal and control flow.

## 15. Command-Cycle Refresh Contract

Current contract:
- at command-cycle entry, evaluator refreshes the shared evaluator-core `CMAKE_NOBIFY_*` compatibility knobs exactly once,
- all compatibility decisions in that cycle (unknown-command policy, severity shaping, budget and stop checks) read that same refreshed snapshot,
- dispatcher/diagnostic internals do not perform extra refreshes for those shared knobs.

Determinism objective:
- equal command sequence + equal variable state at cycle boundaries yields equal compatibility decisions.

## 16. Inter-Command Mutation Contract

Mutation timing rule for compatibility variables:
- writes to `CMAKE_NOBIFY_*` that happen during command N become normative for command N+1 by default.

Fallback semantics:
- if a command mutates compatibility knobs after the cycle snapshot is taken, evaluator keeps current-cycle decisions stable and applies new values at the next cycle boundary,
- only commands that explicitly document immediate local refresh semantics may observe same-cycle effects, and that behavior must be declared in command-specific docs/tests.

## 17. Strictness Escalation Ownership

Ownership split is explicit:
- evaluator compatibility layer owns the first-stage shaping (`profile`, `unsupported_policy`, `continue_on_error`, `error_budget`),
- shared diagnostics layer owns the last-stage strict escalation rule,
- evaluator runtime then consumes that final severity for `EVENT_DIAG`, `Eval_Run_Report`, budget checks, stop decisions, and final result classification.

Boundary rule:
- shared/global strictness is no longer an external-only observability concern,
- evaluator-local run-state outcomes are intentionally aligned to the final severity returned by the shared diagnostics layer.
