# Evaluator Audit Notes (Rewrite Draft)

Status: Analytical draft. This document captures implementation-audit findings, risk prioritization, and remediation backlog for the evaluator rewrite set in `docs/evaluator/`.

## 1. Scope

This document records:
- cross-cutting implementation findings that do not fit a single semantic slice,
- behavior divergences and operational risks,
- maintainability/performance hotspots,
- prioritized follow-up actions.

It does not redefine evaluator contracts. Canonical behavior remains in `evaluator_v2_spec.md`.

## 2. Source of Truth

Primary implementation sources:
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_exec_core.c`
- `src_v2/evaluator/eval_user_command.c`
- `src_v2/evaluator/eval_nested_exec.c`
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/eval_command_caps.c`
- `src_v2/evaluator/eval_command_registry.h`
- `src_v2/evaluator/eval_file.c`
- `src_v2/evaluator/eval_file_internal.h`
- `src_v2/evaluator/eval_file_path.c`
- `src_v2/evaluator/eval_file_glob.c`
- `src_v2/evaluator/eval_file_rw.c`
- `src_v2/evaluator/eval_file_copy.c`
- `src_v2/evaluator/eval_compat.c`
- `src_v2/evaluator/eval_report.c`

Companion docs used for audit context:
- `docs/evaluator/evaluator_v2_spec.md`
- `docs/evaluator/evaluator_compatibility_model.md`
- `docs/evaluator/evaluator_command_capabilities.md`
- `docs/evaluator/evaluator_coverage_matrix.md`
- `docs/Evaluatorr/evaluator_v2_full_audit.md` (legacy archive reference)

## 3. Snapshot Baseline

Snapshot date (workspace): March 6, 2026.

Current quantitative baseline:
- Built-in registry commands: `119`
- Capability labels: `67 FULL` / `52 PARTIAL` / `0 MISSING`
- Fallback labels: `116 NOOP_WARN` / `3 ERROR_CONTINUE` / `0 ERROR_STOP`
- Largest implementation files by size:
  - `eval_target.c` (`2239` lines)
  - `eval_package.c` (`1823` lines)
  - `eval_flow.c` (`1766` lines)
  - `eval_try_compile.c` (`1500` lines)
  - `eval_file_extra.c` (`1295` lines)
- `evaluator.c` after Phase E1 execution-service extraction: `977` lines
- `eval_file.c` after Phase D1 dispatcher split: `57` lines
- `eval_file_{path,glob,rw,copy}.c`: `409` / `422` / `612` / `637` lines
- `eval_string.c` after Phase D2 dispatcher split: `58` lines
- `eval_string_{text,regex,json,misc}.c`: `545` / `185` / `874` / `541` lines

## 4. Positive Findings

Current strengths worth preserving:
- Dispatcher and capability metadata are generated from one registry macro (`EVAL_COMMAND_REGISTRY`), reducing drift risk.
- Native dispatch, native known-command checks, and capability lookup now share one case-insensitive runtime index, reducing namespace drift.
- Stop-state handling is coherent: OOM transitions to `stop_requested` and short-circuits most execution paths.
- Diagnostic emission is consistent and dual-sink: one external log line plus one `EVENT_DIAG` with run-report updates.
- Unknown-command behavior is explicit and policy-driven instead of silently ignored.
- Compatibility refresh timing is centralized at command-cycle entry and covered by evaluator tests.
- Phase E1 reduced `evaluator.c` by extracting execution traversal, user-command lifecycle, and nested file execution without changing public API or golden output.
- Phase D1 reduced `eval_file.c` to a thin dispatcher/orchestrator and moved path/glob/rw/copy families into explicit internal modules without changing public API or golden output.
- Phase D2 reduced `eval_string.c` to a thin dispatcher and moved text/regex/json/misc families into explicit internal modules without changing public API or golden output.

## 5. Prioritized Findings

| ID | Severity | Category | Finding (short) |
|---|---|---|---|
| F-06 | Low | Maintainability | Large evaluator translation units concentrate many concerns and raise refactor risk. |
| F-07 | Low | Coverage debt | `PARTIAL` footprint remains high (`43.7%`), concentrated in `ctest_*` and legacy wrappers. |

## 6. Detailed Findings

### F-06: File-size concentration

Evidence:
- Multiple core `.c` files still exceed ~1500 lines (`eval_target.c`, `eval_package.c`, `eval_flow.c`, `eval_try_compile.c`), even after the execution-service split reduced `evaluator.c` to `977` lines, Phase D1 reduced `eval_file.c` to `57` lines, and Phase D2 reduced `eval_string.c` to `58` lines.

Risk:
- Review complexity and regression probability increase, especially for cross-cutting edits.

Recommendation:
- Continue refactoring by domain boundaries, with `eval_target` now the clearest remaining hotspot, followed by `eval_package`, `eval_flow`, and `eval_try_compile`.

### F-07: Coverage debt concentration

Evidence:
- Coverage matrix snapshot: `52` of `119` built-ins remain `PARTIAL` (`43.7%`).
- Concentration is mostly `ctest_*`, legacy compatibility commands, and query/introspection surfaces.

Risk:
- Behavioral confidence remains uneven despite broad command-name availability.

Recommendation:
- Keep promotion backlog focused on clusters (not one-off commands), starting with `ctest_*` + `target_* advanced` + `try_run`.

## 7. Remediation Backlog

Priority tiers for next engineering/doc pass:

1. P0
- Start decomposition of largest evaluator files with stable internal interfaces (F-06).
- Keep coverage-promotion roadmap in sync with `evaluator_coverage_matrix.md` (F-07).

## 8. Closed / Documented

### F-01: `while()` guard configurability

Current state:
- `eval_while(...)` now reads `CMAKE_NOBIFY_WHILE_MAX_ITERATIONS` once at `while()` entry,
- default is `10000`,
- invalid or non-positive values emit a warning and fall back to `10000`,
- mutations inside a running loop do not affect that active loop and only apply to the next `while()` node,
- evaluator tests cover low-limit failure, invalid-value fallback, and snapshot-at-loop-entry semantics.

Disposition:
- closed as implemented-and-documented for the current baseline.

### F-02: Capability metadata contract

Current state:
- capability lookup remains native-command introspection only,
- dispatcher and unknown-command fallback do not branch on capability metadata,
- `if(COMMAND ...)` continues to be broader than capability lookup because it also sees user-defined `function()` / `macro()` commands,
- evaluator tests now cover that user-command/runtime visibility split explicitly.

Disposition:
- closed as implemented-and-documented for the current baseline.

### F-03: Global diagnostics severity authority

Current state:
- evaluator compatibility still performs the first severity-shaping stage,
- shared diagnostics strict mode now provides the final severity authority through `diag_effective_severity(...)`,
- `EVENT_DIAG.severity`, `Eval_Run_Report`, error-budget checks, stop behavior, and final `Eval_Result` now all consume that final severity,
- evaluator tests cover a warning path escalated by global strict mode into fatal budget stop.

Disposition:
- closed as implemented-and-documented for the current baseline.

### F-04: Compatibility refresh timing

Current state:
- `eval_refresh_runtime_compat(...)` is called once at `eval_node(...)` command entry,
- evaluator tests cover next-command activation for `CMAKE_NOBIFY_COMPAT_PROFILE`, `CMAKE_NOBIFY_UNSUPPORTED_POLICY`, and `CMAKE_NOBIFY_CONTINUE_ON_ERROR`,
- normative docs now treat command-cycle snapshot timing as the official contract.

Disposition:
- closed as implemented-and-documented for the current baseline.

### F-05: Native lookup scalability

Current state:
- `eval_dispatch_command(...)`, `eval_dispatcher_is_known_command(...)`, and `eval_command_caps_lookup(...)` all reuse `eval_native_cmd_find_const(...)`,
- that helper uses the runtime `native_command_index` hash table over the native registry,
- register/unregister rebuild the same index used by dispatcher and capability introspection.

Disposition:
- closed as implemented-and-documented for the current baseline.

## 9. Verification Checklist for Next Audit Pass

- Re-run registry stats and fallback distribution from `eval_command_registry.h`.
- Confirm any new `PARTIAL -> FULL` promotions are reflected in both coverage matrix and capability docs.
- Recompute top evaluator file-size hotspots after any refactor wave.

## 10. Open Questions

- Should `CI_STRICT` remain behaviorally equivalent to `STRICT`, or gain CI-specific stop/report semantics?
- Should unknown-command and known-command fallback policy converge to one unified policy mechanism?
- Should the shared diagnostics module eventually expose richer metadata than warning/error counts for evaluator-specific CI dashboards?

## 11. Relationship to Other Docs

- `evaluator_v2_spec.md`
Canonical contract; this file is analytical only.

- `evaluator_coverage_matrix.md`
Quantitative command-coverage snapshot used by this audit.

- `evaluator_command_capabilities.md`
Capability API/data contract referenced by the closed capability-contract note.

- `evaluator_compatibility_model.md`
Profile/policy behavior referenced by the closed severity-authority note and the closed compatibility-timing note.
