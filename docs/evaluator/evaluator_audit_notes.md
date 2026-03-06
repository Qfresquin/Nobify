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
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/eval_command_caps.c`
- `src_v2/evaluator/eval_command_registry.h`
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
  - `eval_string.c` (`2513` lines)
  - `eval_target.c` (`2239` lines)
  - `eval_file.c` (`2129` lines)
  - `eval_package.c` (`1823` lines)
  - `eval_flow.c` (`1766` lines)

## 4. Positive Findings

Current strengths worth preserving:
- Dispatcher and capability metadata are generated from one registry macro (`EVAL_COMMAND_REGISTRY`), reducing drift risk.
- Native dispatch, native known-command checks, and capability lookup now share one case-insensitive runtime index, reducing namespace drift.
- Stop-state handling is coherent: OOM transitions to `stop_requested` and short-circuits most execution paths.
- Diagnostic emission is consistent and dual-sink: one external log line plus one `EVENT_DIAG` with run-report updates.
- Unknown-command behavior is explicit and policy-driven instead of silently ignored.
- Compatibility refresh timing is centralized at command-cycle entry and covered by evaluator tests.

## 5. Prioritized Findings

| ID | Severity | Category | Finding (short) |
|---|---|---|---|
| F-01 | Medium | Behavioral divergence | `while()` has hard iteration cap (`10000`) that can change valid script outcomes. |
| F-02 | Medium | Contract coherence | Capability metadata (`implemented_level`/`fallback`) is not enforced by dispatch runtime. |
| F-03 | Medium | Observability | Evaluator severity and process-global diagnostics severity can diverge under strict modes. |
| F-06 | Low | Maintainability | Large evaluator translation units concentrate many concerns and raise refactor risk. |
| F-07 | Low | Coverage debt | `PARTIAL` footprint remains high (`43.7%`), concentrated in `ctest_*` and legacy wrappers. |

## 6. Detailed Findings

### F-01: `while()` hard cap

Evidence:
- `src_v2/evaluator/evaluator.c` (`eval_while`) sets `const size_t kMaxIter = 10000` and emits `"Iteration limit exceeded"` when exhausted.

Risk:
- Long but valid loops can terminate with evaluator error even when original CMake flow would continue.

Recommendation:
- Keep guard, but make it configurable (`env` or evaluator variable) and document default explicitly in canonical spec.

### F-02: Capability metadata is informational only

Evidence:
- Capability lookup returns static metadata from `eval_command_caps.c`.
- Dispatch path in `eval_dispatcher.c` routes directly to handler by name and does not branch on capability/fallback metadata.

Risk:
- Tooling can over-interpret capability fields as runtime policy.
- Metadata/runtime drift can persist unnoticed.

Recommendation:
- Choose one explicit contract:
  - keep metadata informational-only and state this in all related docs/tests, or
  - add runtime checks/hook points that consume fallback metadata for known commands.

### F-03: Severity split between evaluator and global diagnostics

Evidence:
- `eval_emit_diag(...)` computes evaluator-effective severity and emits both `EVENT_DIAG` and `diag_log(...)`.
- Shared diagnostics strict mode can escalate warnings independently from evaluator report/event severity.

Risk:
- CI gates based on global diagnostics counters can disagree with evaluator run report.

Recommendation:
- Define one authority for severity escalation (evaluator vs global diagnostics) and codify a single gating recommendation.

### F-06: File-size concentration

Evidence:
- Multiple core `.c` files exceed ~1800 lines (`eval_string.c`, `eval_target.c`, `eval_file.c`, `eval_flow.c`, `eval_package.c`).

Risk:
- Review complexity and regression probability increase, especially for cross-cutting edits.

Recommendation:
- Refactor by domain boundaries (for example: split `eval_file` by subcommand families and `eval_string` by mode families).

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
- Clarify severity authority and CI gating rule (F-03).
- Decide and document capability-metadata contract (F-02).

2. P1
- Add configurable `while()` guard limit and document default semantics (F-01).

3. P2
- Start decomposition of largest evaluator files with stable internal interfaces (F-06).
- Keep coverage-promotion roadmap in sync with `evaluator_coverage_matrix.md` (F-07).

## 8. Closed / Documented in B+C

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
- Re-check strict-mode severity behavior with one controlled warning scenario.
- Re-check `while()` behavior and guard configurability if implemented.
- Recompute top evaluator file-size hotspots after any refactor wave.

## 10. Open Questions

- Should `CI_STRICT` remain behaviorally equivalent to `STRICT`, or gain CI-specific stop/report semantics?
- Should unknown-command and known-command fallback policy converge to one unified policy mechanism?
- Which metric is canonical for build gating: evaluator run report, global diagnostics counters, or both?

## 11. Relationship to Other Docs

- `evaluator_v2_spec.md`
Canonical contract; this file is analytical only.

- `evaluator_coverage_matrix.md`
Quantitative command-coverage snapshot used by this audit.

- `evaluator_command_capabilities.md`
Capability API/data contract referenced by finding F-02.

- `evaluator_compatibility_model.md`
Profile/policy behavior referenced by finding F-03 and the closed compatibility-timing note.
