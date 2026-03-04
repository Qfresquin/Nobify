# Evaluator Diagnostics (Rewrite Draft)

Status: Draft rewrite. This document describes the evaluator-side diagnostic pipeline currently implemented in `src_v2/evaluator`, while treating `docs/diagnostics/diagnostics_v2_spec.md` as the shared logger contract.

## 1. Scope

This document covers evaluator-specific diagnostic behavior:
- how evaluator code emits diagnostics,
- how severity is shaped by evaluator compatibility rules,
- how diagnostics become `EVENT_DIAG`,
- how diagnostic events update `Eval_Run_Report`,
- how diagnostics influence stop/continue behavior.

It does not redefine the global `diagnostics.h` logger API.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/evaluator/evaluator.c` (`eval_emit_diag`)
- `src_v2/evaluator/evaluator_internal.h` (diagnostic helper macros)
- `src_v2/evaluator/eval_diag_classify.c`
- `src_v2/evaluator/eval_report.c`
- `src_v2/evaluator/eval_compat.c`
- `src_v2/diagnostics/diagnostics.c` (shared logging sink)

## 3. Diagnostic Emission Entry Point

The central evaluator emission function is:

```c
bool eval_emit_diag(Evaluator_Context *ctx,
                    Event_Diag_Severity sev,
                    String_View component,
                    String_View command,
                    Event_Origin origin,
                    String_View cause,
                    String_View hint);
```

High-level contract:
- Returns `false` if `ctx == NULL`.
- Returns `false` immediately if evaluator execution should already stop.
- Returns `false` on event-stream push failure (which is treated as OOM).
- Otherwise emits one evaluator diagnostic and returns `true`.

The function is the canonical evaluator-side path. Most evaluator code should not call the shared `diag_log(...)` directly.

## 4. Emission Helpers Used by Call Sites

Current helper macros:

- `EVAL_NODE_ORIGIN_DIAG(...)`
Calls `eval_emit_diag(...)` using:
- an explicit `Event_Origin`
- `node->as.cmd.name` as the command field

- `EVAL_NODE_DIAG(...)`
Calls `EVAL_NODE_ORIGIN_DIAG(...)` after deriving origin from `eval_origin_from_node(...)`.

- `EVAL_DIAG(...)`
Direct wrapper over `eval_emit_diag(...)` for sites that already have all fields.

Practical consequence:
- most command handlers emit diagnostics with the current command name and node origin automatically,
- the diagnostic call sites remain compact,
- handler code follows a mostly uniform error-reporting shape.

## 5. Diagnostic Pipeline

Each successful `eval_emit_diag(...)` call follows this sequence:

1. Check stop state (`eval_should_stop`).
2. Refresh runtime compatibility settings (`eval_refresh_runtime_compat`).
3. Classify the diagnostic into evaluator metadata (`code`, `error_class`).
4. Compute evaluator-effective severity with compatibility rules.
5. Send one external log line to the shared diagnostics module via `diag_log(...)`.
6. Build one `EVENT_DIAG` event.
7. Deep-copy diagnostic payload strings into `event_arena`.
8. Append the event to the stream.
9. Update `Eval_Run_Report`.
10. Run compatibility stop/continue decision logic.

This makes evaluator diagnostics both:
- externally visible as process logs,
- and internally preserved as semantic event data.

## 6. Severity Model

### 6.1 Input Severity

Evaluator call sites emit:
- `EV_DIAG_WARNING`
- `EV_DIAG_ERROR`

These are evaluator/event severities, not the global `Diag_Severity` enum from `diagnostics.h`.

### 6.2 Evaluator-Level Severity Shaping

Evaluator severity shaping is controlled by:
- `eval_compat_effective_severity(...)`

Current behavior:
- In `EVAL_PROFILE_PERMISSIVE`, warning/error are preserved.
- In `EVAL_PROFILE_STRICT` and `EVAL_PROFILE_CI_STRICT`, warnings are promoted to errors.

This evaluator-effective severity drives:
- the `EVENT_DIAG` event severity,
- `Eval_Run_Report` warning/error counters,
- compatibility stop/continue decisions,
- the severity passed into the shared logger.

### 6.3 Interaction with Shared Logger Strict Mode

After evaluator shaping, `eval_emit_diag(...)` calls:

```c
diag_log(effective_sev == EV_DIAG_ERROR ? DIAG_SEV_ERROR : DIAG_SEV_WARNING, ...)
```

Important consequence:
- evaluator compatibility shaping happens before the shared logger sees the diagnostic.
- If the process-global diagnostics module is also in strict mode (`diag_set_strict(true)`), it can still escalate a warning log line even when evaluator compatibility kept it as a warning.

This creates a possible split:
- `EVENT_DIAG` severity and `Eval_Run_Report` can remain warning,
- while the external `diag_log(...)` sink can log the same issue as an error and increment global error counters.

That split is possible when:
- evaluator compatibility remains permissive,
- but the global diagnostics logger is running in strict mode.

## 7. Classification Metadata

Evaluator diagnostic metadata is assigned by:
- `eval_diag_classify(...)`

Outputs:
- `Eval_Diag_Code`
- `Eval_Error_Class`

Current baseline defaults:
- code: `EVAL_ERR_SEMANTIC`
- class: `EVAL_ERR_CLASS_INPUT_ERROR`

Current heuristic overrides:
- component `parser` or `lexer` => `EVAL_ERR_PARSE` + `INPUT_ERROR`
- cause containing `unsupported` or `unknown command` => `EVAL_ERR_UNSUPPORTED` + `ENGINE_LIMITATION`
- cause containing `policy` => `POLICY_CONFLICT`
- component `eval_file`, or causes mentioning read/security/remote-url failures => `IO_ENV_ERROR`
- warning causes containing `legacy`, `ignored`, or `deprecated` => `EVAL_WARN_LEGACY`

These rules are heuristic and text-driven, not a registry of exact command-specific error IDs.

## 8. `EVENT_DIAG` Emission Contract

For each successful evaluator diagnostic, one `EVENT_DIAG` is appended.

Current payload fields populated by `eval_emit_diag(...)`:
- `severity`
- `component`
- `command`
- `code`
- `error_class`
- `cause`
- `hint`

Header fields also carry:
- `origin`
- `scope_depth`
- `policy_depth`

Ownership contract:
- all string payload fields are copied into `event_arena`,
- the event remains valid for the lifetime of the event stream.

If the event cannot be pushed:
- the evaluator transitions into OOM handling,
- the diagnostic is not considered successfully emitted.

## 9. External Logging Contract

Even though evaluator diagnostics are modeled as events, the evaluator also emits one shared log line for each diagnostic through `diag_log(...)`.

Current `diag_log(...)` call shape:
- component is always logged as `"evaluator"`
- source is `ctx->current_file` or `"<input>"`
- line/col come from `Event_Origin`
- command is formatted from the evaluator command `String_View`
- cause/hint are passed through as strings

Practical consequence:
- the external log stream identifies the producer as the evaluator,
- while the richer internal `component` field is preserved inside `EVENT_DIAG`.

This is intentional: parser/dispatcher/file/etc. remain subcomponents inside the event payload, while the shared log sink sees one top-level producer.

## 10. Run Report Coupling

Successful evaluator diagnostics update `Eval_Run_Report` through:
- `eval_report_record_diag(...)`

Current report behavior:
- warning severity increments `warning_count`
- error severity increments `error_count`
- `INPUT_ERROR`, `ENGINE_LIMITATION`, `IO_ENV_ERROR`, and `POLICY_CONFLICT` increment their respective class counters
- `EVAL_ERR_UNSUPPORTED` increments `unsupported_count`

Then `eval_report_finalize(...)` recomputes overall status:
- `EVAL_RUN_FATAL` if `ctx->oom` or `ctx->stop_requested`
- otherwise `EVAL_RUN_OK_WITH_ERRORS` if any error exists
- otherwise `EVAL_RUN_OK_WITH_WARNINGS` if any warning exists
- otherwise `EVAL_RUN_OK`

Important distinction from the shared diagnostics module:
- `Eval_Run_Report` counts the evaluator-effective severity,
- not whatever the global diagnostics logger may do later under its own strict mode.

## 11. Stop / Continue Semantics

After recording the event and report data, `eval_emit_diag(...)` calls:
- `eval_compat_decide_on_diag(...)`

Current behavior:
- non-error diagnostics do not trigger stop decisions
- for errors in `PERMISSIVE`, execution continues until `error_budget` is exceeded (if non-zero)
- for errors in `STRICT` / `CI_STRICT`, evaluator requests stop through the compatibility path

The stop decision happens after:
- external logging
- `EVENT_DIAG` emission
- run-report update

So a diagnostic that causes the evaluator to stop is still recorded as data before the stop request takes effect.

## 12. Unsupported Command Path

One high-value diagnostic path is unknown-command handling in the dispatcher.

Current behavior:
- unresolved commands are classified under component `"dispatcher"`
- cause is `"Unknown command"`
- severity is selected from `ctx->unsupported_policy`
- hint changes depending on policy (`Ignored during evaluation` vs `No-op with warning by policy`)

This path is one of the main producers of:
- `EVAL_ERR_UNSUPPORTED`
- `EVAL_ERR_CLASS_ENGINE_LIMITATION`
- `run_report.unsupported_count`

## 13. Message Command as Diagnostic Producer

`message()` is also a first-class diagnostic source.

Current behavior includes:
- `SEND_ERROR` / `FATAL_ERROR` paths emitting evaluator errors
- `WARNING`, `AUTHOR_WARNING`, and deprecation flows emitting warnings or escalated errors depending on runtime conditions
- invalid `CHECK_PASS` / `CHECK_FAIL` usage emitting evaluator errors

This means not all evaluator diagnostics represent failures in command implementation; some are script-authored diagnostic intents.

## 14. Current Non-Goals / Limitations

Current limitations of evaluator diagnostics:

- Classification is heuristic and largely substring-based.
- The shared external log stream and `EVENT_DIAG` stream can diverge in effective severity when global diagnostics strict mode is enabled.
- There is no dedicated in-memory evaluator diagnostic queue apart from the event stream and run report.
- `eval_emit_diag(...)` stops doing work entirely once the evaluator is already in stop state.
- The shared logger still uses a top-level `"evaluator"` component string, so subcomponent granularity is preserved only in event payloads.

## 15. Relationship to Other Docs

- `evaluator_v2_spec.md`
Defines the top-level evaluator contract. This file is a focused slice of that broader spec.

- `evaluator_compatibility_model.md`
Should describe the broader compatibility controls that feed into diagnostic severity shaping and stop decisions.

- `docs/diagnostics/diagnostics_v2_spec.md`
Defines the shared process-global logger and telemetry sink used by the evaluator.
