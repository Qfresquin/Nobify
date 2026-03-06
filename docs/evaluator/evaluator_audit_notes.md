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
- `src_v2/evaluator/eval_package_find_item.c`
- `src_v2/evaluator/eval_file.c`
- `src_v2/evaluator/eval_file_internal.h`
- `src_v2/evaluator/eval_file_path.c`
- `src_v2/evaluator/eval_file_glob.c`
- `src_v2/evaluator/eval_file_rw.c`
- `src_v2/evaluator/eval_file_copy.c`
- `src_v2/evaluator/eval_try_compile.c`
- `src_v2/evaluator/eval_try_compile_parse.c`
- `src_v2/evaluator/eval_try_compile_exec.c`
- `src_v2/evaluator/eval_try_run.c`
- `src_v2/evaluator/eval_compat.c`
- `src_v2/evaluator/eval_report.c`
- `src_v2/build/nob_test.c` (verification runner source)

Companion docs used for audit context:
- `docs/evaluator/evaluator_v2_spec.md`
- `docs/evaluator/evaluator_compatibility_model.md`
- `docs/evaluator/evaluator_command_capabilities.md`
- `docs/evaluator/evaluator_coverage_matrix.md`
- `docs/evaluator/Refatorção Estrutural.md`

Historical archive note:
- no legacy evaluator audit archive is currently present in-tree; previous references to `docs/Evaluatorr/...` were stale path leftovers.

## 3. Snapshot Baseline

Snapshot date (workspace): March 6, 2026.

Verification baseline:
- `./build/nob_v2_test test-evaluator` passes with `passed=105 failed=0` on the March 6, 2026 workspace snapshot.
- local `build/nob_test` is not a source of truth for this audit because `/build` is ignored and stale binaries can predate the post-refactor unit split even when `src_v2/build/nob_test.c` is already current.

Current quantitative baseline:
- Built-in registry commands: `119`
- Capability labels: `67 FULL` / `52 PARTIAL` / `0 MISSING`
- Fallback labels: `116 NOOP_WARN` / `3 ERROR_CONTINUE` / `0 ERROR_STOP`
- Largest implementation files by size:
  - `eval_package.c` (`1078` lines)
  - `eval_file_generate_lock_archive.c` (`984` lines)
  - `eval_project.c` (`942` lines)
  - `eval_file_transfer.c` (`919` lines)
  - `eval_file_fsops.c` (`911` lines)
- `evaluator.c` after Phase E1 execution-service extraction: `987` lines
- `eval_file.c` after Phase D1 dispatcher split: `57` lines
- `eval_file_{path,glob,rw,copy}.c`: `409` / `422` / `612` / `637` lines
- `eval_string.c` after Phase D2 dispatcher split: `58` lines
- `eval_string_{text,regex,json,misc}.c`: `545` / `185` / `874` / `541` lines
- `eval_target` after Phase D3 split: `1440` / `481` / `227` lines (`core` / `usage` / `source_group`)
- `eval_package` after Phase D4 split: `1078` / `733` lines (`core` / `find_item`)
- `eval_flow` after Phase D5 split: `269` / `251` / `459` / `784` lines (`core` / `block` / `cmake_language` / `process`)
- `eval_try_compile` after Phase D6 split: `243` / `414` / `568` / `121` lines (`core` / `parse` / `exec` / `try_run`)

## 4. Positive Findings

Current strengths worth preserving:
- Dispatcher and capability metadata are generated from one registry macro (`EVAL_COMMAND_REGISTRY`), reducing drift risk.
- Native dispatch, native known-command checks, and capability lookup now share one case-insensitive runtime index, reducing namespace drift.
- Stop-state handling is coherent: OOM transitions to `stop_requested` and short-circuits most execution paths.
- Diagnostic emission is consistent and dual-sink: one external log line plus one `EVENT_DIAG` with run-report updates.
- Unknown-command behavior is explicit and policy-driven instead of silently ignored.
- Compatibility refresh timing is centralized at command-cycle entry and covered by evaluator tests.
- Phase E1 reduced `evaluator.c` by extracting execution traversal, user-command lifecycle, and nested file execution while preserving the public API.
- Phase D1 reduced `eval_file.c` to a thin dispatcher/orchestrator and moved path/glob/rw/copy families into explicit internal modules while preserving the public API.
- Phase D2 reduced `eval_string.c` to a thin dispatcher and moved text/regex/json/misc families into explicit internal modules while preserving the public API.
- Phase D3 split `eval_target` into core/property handling, usage/linkage handlers, and `source_group()` helpers while preserving the public API.
- Phase D4 split `eval_package` into `find_package()` core flow and shared `find_*` item resolution while preserving the public API.
- Phase D5 split `eval_flow` into shared helpers plus dedicated block, `cmake_language()`, and process-execution modules while preserving the public API.
- Phase D6 split `eval_try_compile` into shared helpers plus parse/exec/`try_run` modules while preserving the public API.
- Phase F completed the bulk structural campaign by decomposing the post-D hotspot tier, normalizing the new internal boundaries, and returning the evaluator to a green baseline without reopening dispatcher or context-ownership decisions.
- The current evaluator suite gives a stable behavioral post-refactor baseline when run through `build/nob_v2_test`.

## 5. Prioritized Findings

| ID | Severity | Category | Finding (short) |
|---|---|---|---|
| F-10 | Low | Verification workflow | A stale local `build/nob_test` binary can report false refactor regressions even though the current runner source and `build/nob_v2_test` are aligned. |
| F-07 | Low | Coverage debt | `PARTIAL` footprint remains high (`43.7%`), concentrated in `ctest_*` and legacy wrappers. |

## 6. Detailed Findings

### F-10: Verification workflow drift can come from stale local runner binaries

Evidence:
- In the March 6, 2026 workspace, `./build/nob_test test-evaluator` can fail at link time against missing symbols from the extracted evaluator units.
- The current source of `src_v2/build/nob_test.c` already includes the split units, and `./build/nob_v2_test test-evaluator` passes `105/105`.
- `/build` is ignored, so a locally cached runner binary can lag behind source without showing up as a tracked diff.

Risk:
- Engineers can misclassify a stale local artifact as a refactor regression in evaluator semantics or link topology.

Recommendation:
- Treat `build/nob_v2_test` or a freshly rebuilt runner as the audit baseline and document the stale-artefact risk explicitly in evaluator verification notes.

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
- Keep coverage-promotion roadmap in sync with `evaluator_coverage_matrix.md` (F-07).

2. P1
- Document or remove ambiguity around stale local runner binaries vs current runner source (`build/nob_test` vs `build/nob_v2_test`) in evaluator verification workflow (F-10).

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

### F-06: File-size concentration

Current state:
- Phase F completed the planned post-Phase-D hotspot decomposition and follow-up cleanup.
- The largest remaining evaluator translation unit is now `eval_package.c` at `1078` lines, with the rest of the top tier below `1k`.
- The temporary low-level helper leaks introduced during the bulk move were normalized before closing the campaign.

Disposition:
- closed as structurally resolved for the March 6, 2026 baseline; further work should focus on semantic promotion, not more file-splitting for its own sake.

### F-08: `find_*(... ENV ...)` malformed validation path

Current state:
- malformed `ENV` clauses in `find_file()` / `find_path()` / `find_library()` / `find_program()` now emit `EVAL_DIAG_MISSING_REQUIRED` with the explicit cause `find_*(ENV) requires an environment variable name`,
- the `find_*` handlers now return `EVAL_RESULT_SOFT_ERROR` for those non-fatal parse failures instead of collapsing them into `EVAL_RESULT_OK`,
- malformed `ENV` input no longer routes through `ctx_oom()` or sets `stop_requested`,
- evaluator tests cover both `ENV` at end-of-clause and `ENV` immediately followed by another keyword.

Disposition:
- closed in the March 6, 2026 evaluator fix pass.

### F-09: Empty `try_compile` / `try_run` capture-file noise

Current state:
- `try_compile_append_file_to_log(...)` now skips zero-byte capture files before calling `nob_read_entire_file(...)`,
- successful `try_compile` / `try_run` paths no longer emit the previous `Could not read file ...: File exists` noise for empty `.out` / `.err` captures,
- evaluator tests now cover the empty-capture helper path directly and the full evaluator suite passes without that specific capture noise.

Disposition:
- closed in the March 6, 2026 evaluator fix pass.

### F-11: Documentation drift after the refactor wave

Current state:
- this audit pass no longer claims that Phases D1-D6 preserved golden output across the full March 6, 2026 snapshot; the wording is now limited to public-API preservation,
- the verification baseline is explicitly tied to `./build/nob_v2_test test-evaluator`,
- stale `docs/Evaluatorr/...` references were removed and replaced by an explicit note that no legacy evaluator audit archive is present in-tree.

Disposition:
- closed in the March 6, 2026 documentation pass.

## 9. Verification Checklist for Next Audit Pass

- Re-run `./build/nob_v2_test test-evaluator` and record both the pass/fail summary and any unexpected evaluator-side log noise.
- Rebuild or ignore stale `build/nob_test` binaries before classifying workflow failures as evaluator regressions.
- Re-run registry stats and fallback distribution from `eval_command_registry.h`.
- Confirm any new `PARTIAL -> FULL` promotions are reflected in both coverage matrix and capability docs.
- Keep `try_run` limitations aligned between `evaluator_v2_spec.md`, `evaluator_coverage_matrix.md`, and this audit file.
- Keep malformed `find_*(... ENV ...)` forms covered by regression tests as the option parser evolves.
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
