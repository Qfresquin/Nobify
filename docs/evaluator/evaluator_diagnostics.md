# Evaluator Diagnostics

Status: code-first contract implemented in `src_v2/evaluator`.

## 1. Scope

This document describes the evaluator-local diagnostic model:
- emission entry points and helper families,
- code/class/severity metadata,
- `EVENT_DIAG` payload rules,
- `Eval_Run_Report` coupling,
- stop/continue interaction with compatibility policy.

It does not redefine the shared process logger in `docs/diagnostics/diagnostics_v2_spec.md`.

## 2. Source of Truth

Primary implementation files:
- `src_v2/evaluator/eval_diag_registry.h`
- `src_v2/evaluator/eval_diag_classify.c`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_report.c`
- `src_v2/evaluator/evaluator_internal.h`
- `src_v2/evaluator/eval_compat.c`

The diagnostic registry is the single source of truth for evaluator diagnostic metadata.

Each `Eval_Diag_Code` entry defines:
- public `EVENT_DIAG.code` string,
- default evaluator severity,
- `Eval_Error_Class`,
- whether it contributes to `run_report.unsupported_count`,
- the intended hint contract for call sites.

There is no heuristic classifier based on `component` or `cause` text anymore.

## 3. Emission API

Canonical evaluator entry points:

```c
Eval_Result eval_emit_diag(Evaluator_Context *ctx,
                           Eval_Diag_Code code,
                           String_View component,
                           String_View command,
                           Cmake_Event_Origin origin,
                           String_View cause,
                           String_View hint);

Eval_Result eval_emit_diag_with_severity(Evaluator_Context *ctx,
                                         Cmake_Diag_Severity severity,
                                         Eval_Diag_Code code,
                                         String_View component,
                                         String_View command,
                                         Cmake_Event_Origin origin,
                                         String_View cause,
                                         String_View hint);
```

Contract:
- `code` is mandatory at the call site.
- `eval_emit_diag(...)` uses the registry default severity for that code.
- `eval_emit_diag_with_severity(...)` is the explicit override path for runtime-shaped cases.
- `cause` and `hint` are payload strings only; they do not influence code/class selection.

The severity override path is reserved for cases whose evaluator severity legitimately depends on runtime policy or script intent, notably:
- unknown-command fallback,
- `message()` diagnostics.

## 4. Helper Families

`evaluator_internal.h` exposes three macro families:
- `*_RESULT`: returns `Eval_Result`
- `*_BOOL`: returns `bool`
- `*_EMIT`: emit-and-ignore convenience form

Examples:
- `EVAL_NODE_ORIGIN_DIAG_RESULT_SEV(...)`
- `EVAL_NODE_DIAG_BOOL(...)`
- `EVAL_DIAG_EMIT(...)`

The old ambiguous helper names are no longer part of the contract.

## 5. Pipeline

Each successful evaluator diagnostic emission follows this flow:

1. reject `NULL` / stop-state / OOM-invalid paths,
2. refresh runtime compatibility state,
3. read registry metadata for `code`,
4. derive base severity from either registry default or explicit override,
5. apply evaluator compatibility shaping,
6. send one shared log line through `diag_log(...)`,
7. build and append one `EVENT_DIAG`,
8. update `Eval_Run_Report`,
9. run compatibility stop/continue decision logic.

Evaluator severity shaping still happens before the shared logger sees the diagnostic.

## 6. Severity Model

Evaluator call sites work with `EV_DIAG_WARNING` and `EV_DIAG_ERROR`.

Severity sources:
- default path: registry default severity for the selected code,
- override path: explicit runtime severity supplied by the caller.

Then evaluator compatibility may still reshape that severity before the event/report/log are recorded.

This means:
- `EVENT_DIAG.severity`,
- `Eval_Run_Report.warning_count/error_count`,
- compatibility stop decisions

all use the evaluator-effective severity after compatibility shaping.

## 7. `EVENT_DIAG` Contract

For each successful evaluator diagnostic, one `EVENT_DIAG` is emitted with:
- `severity`
- `component`
- `command`
- `code`
- `error_class`
- `cause`
- `hint`

Important public contract changes:
- `EVENT_DIAG.code` now exposes explicit stable code strings from the registry,
- `EVENT_DIAG.error_class` remains the coarser taxonomy,
- `component` is observational context only and does not drive classification.

All payload strings are copied into `event_arena`.

## 8. Run Report Coupling

`eval_report_record_diag(...)` now counts from code metadata, not from comparisons against generic code buckets.

Current rules:
- `warning_count` / `error_count` are driven by evaluator-effective severity,
- class counters come from the registry `Eval_Error_Class`,
- `unsupported_count` increments only for codes whose registry entry marks them as unsupported/engine-limitation aggregates.

This keeps `Eval_Run_Report` stable even when different commands share the same coarse class.

## 9. Representative Code Paths

Current important code-first paths:
- unknown command: `EVAL_DIAG_UNKNOWN_COMMAND`
- policy stack / CMP conflicts: `EVAL_DIAG_POLICY_CONFLICT`
- script-authored warning: `EVAL_DIAG_SCRIPT_WARNING`
- script-authored error: `EVAL_DIAG_SCRIPT_ERROR`
- not implemented yet: `EVAL_DIAG_NOT_IMPLEMENTED`
- filesystem/process I/O failures: `EVAL_DIAG_IO_FAILURE`

Command-specific argument/semantic failures now choose explicit input-side codes at the call site such as:
- `EVAL_DIAG_MISSING_REQUIRED`
- `EVAL_DIAG_UNEXPECTED_ARGUMENT`
- `EVAL_DIAG_DUPLICATE_ARGUMENT`
- `EVAL_DIAG_INVALID_VALUE`
- `EVAL_DIAG_INVALID_CONTEXT`
- `EVAL_DIAG_OUT_OF_RANGE`
- `EVAL_DIAG_CONFLICTING_OPTIONS`
- `EVAL_DIAG_NOT_FOUND`

## 10. Unsupported Command and `message()`

Unknown-command fallback remains policy-shaped:
- code is always `EVAL_DIAG_UNKNOWN_COMMAND`,
- severity depends on `ctx->unsupported_policy`,
- class stays `ENGINE_LIMITATION`,
- `unsupported_count` increments through registry metadata.

`message()` remains a script-authored diagnostic producer:
- warning-like modes emit `EVAL_DIAG_SCRIPT_WARNING`,
- error-like modes emit `EVAL_DIAG_SCRIPT_ERROR`,
- runtime control variables may still affect the effective severity for deprecation flows.

## 11. Limitations

Current non-goals / limitations:
- the shared external log still uses top-level component `"evaluator"` even when `EVENT_DIAG.component` is more specific,
- evaluator and global logger severities can still diverge if the global logger runs with its own strict policy,
- registry codes are stable within evaluator scope, but they are not intended to replace the higher-level error-class taxonomy.

## 12. Related Docs

- `docs/evaluator/evaluator_compatibility_model.md`
- `docs/evaluator/evaluator_dispatch.md`
- `docs/evaluator/evaluator_event_ir_contract.md`
- `docs/diagnostics/diagnostics_v2_spec.md`
