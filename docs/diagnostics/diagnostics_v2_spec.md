# Diagnostics v2 Specification

Status: Canonical diagnostics contract for `src_v2/diagnostics/diagnostics.c` and `src_v2/diagnostics/diagnostics.h`.

## 1. Role

The diagnostics module is the process-global sink for:
- warning/error counting,
- strict-mode warning escalation,
- structured `nob_log(...)` emission,
- unsupported-command telemetry aggregation.

It is intentionally simple and stateful.

It is not:
- an arena-owned subsystem,
- a per-context diagnostic buffer,
- a structured in-memory event log,
- thread-safe.

## 2. Global State Model

The module stores all state in a single process-global static struct.

Tracked state:
- `strict_mode`
- `warning_count`
- `error_count`
- unsupported-command telemetry:
  - `names`
  - `counts`
  - `count`
  - `capacity`
  - `total`

Consequences:
- All callers in the same process share the same counters and telemetry.
- The API is not thread-safe.
- There is no snapshot/restore API.

## 3. Public API

### 3.1 Severity

`Diag_Severity` values:

- `DIAG_SEV_WARNING`
- `DIAG_SEV_ERROR`

### 3.2 Severity Mapping

- `int diag_to_nob_level(Diag_Severity sev)`
- `Diag_Severity diag_effective_severity(Diag_Severity sev)`

Current mapping:
- `DIAG_SEV_ERROR` => `NOB_ERROR`
- everything else => `NOB_WARNING`

Because the enum currently has only two values, this effectively means warning maps to `NOB_WARNING`.

### 3.3 Safe String Helper

- `const char *diag_safe_str(const char *s)`

Behavior:
- returns `s` if `s != NULL` and `s[0] != '\0'`
- otherwise returns `"-"`

This is used to keep log output structurally complete even when a field is missing.

## 4. Core Diagnostic Counters

### 4.1 Reset

- `void diag_reset(void)`

Resets only:
- `warning_count`
- `error_count`

It does not reset:
- `strict_mode`
- telemetry state

### 4.2 Strict Mode

- `void diag_set_strict(bool strict_mode)`
- `bool diag_is_strict(void)`
- `Diag_Severity diag_effective_severity(Diag_Severity sev)`

Strict mode changes only the effective severity used for logging and error counting:
- a warning logged in strict mode is emitted as an error-level diagnostic
- an explicit error remains an error

Strict mode does not rewrite the caller's requested severity everywhere. This matters for counter semantics (see below).

### 4.3 Count Queries

- `size_t diag_warning_count(void)`
- `size_t diag_error_count(void)`
- `bool diag_has_warnings(void)`
- `bool diag_has_errors(void)`

`diag_has_warnings()` and `diag_has_errors()` are simple `> 0` checks over the stored counters.

## 5. `diag_log()` Contract

### 5.1 Signature

```c
void diag_log(
    Diag_Severity sev,
    const char *component,
    const char *source,
    size_t line,
    size_t col,
    const char *command,
    const char *cause,
    const char *hint
);
```

### 5.2 Effective Severity

The function computes:

- `effective = sev`
- if strict mode is enabled and `sev == DIAG_SEV_WARNING`, then `effective = DIAG_SEV_ERROR`

`diag_effective_severity(...)` exposes that exact rule as a reusable helper, and `diag_log(...)` applies the same helper internally.

This `effective` severity controls:
- the output log level passed to `nob_log(...)`
- the rendered `"ERROR"` vs `"WARNING"` tag
- whether `error_count` increments

### 5.3 Counting Semantics

Current counting behavior is asymmetric:

- If the original input severity is `DIAG_SEV_WARNING`, `warning_count` increments.
- If the effective severity is `DIAG_SEV_ERROR`, `error_count` increments.

Important consequence:
- In strict mode, a warning increments both `warning_count` and `error_count`.
- Therefore `diag_warning_count()` reflects originally requested warnings, not only warnings that remained warnings after escalation.

### 5.4 Output Format

`diag_log()` emits exactly one structured log line through `nob_log(...)` with this shape:

```text
DIAG|<ERROR-or-WARNING>|component=<...>|source=<...>|line=<...>|col=<...>|command=<...>|cause=<...>|hint=<...>
```

Field normalization:
- `component`, `source`, `command`, `cause`, and `hint` are passed through `diag_safe_str()`
- missing or empty string fields become `"-"`
- `line` and `col` are emitted verbatim as decimal integers

The function does not:
- allocate memory,
- store diagnostics for later replay,
- return a success/failure result.

## 6. Telemetry: Unsupported Commands

The module also tracks unsupported command names for summary/reporting.

This telemetry is independent from warning/error counters.

### 6.1 Reset

- `void diag_telemetry_reset(void)`

Behavior:
- frees all stored command-name copies
- frees the backing `names` and `counts` arrays
- resets telemetry counters and capacity to zero

It does not affect:
- `strict_mode`
- warning/error counters

### 6.2 Recording

- `void diag_telemetry_record_unsupported_sv(String_View cmd_name)`

Behavior:
- ignores empty or null `String_View`
- increments `unsupported.total` for every accepted call
- performs exact string matching against previously recorded names
- increments an existing per-name counter if found
- otherwise appends a new owned copy of the command name

Matching details:
- comparison is exact and case-sensitive (`nob_sv_eq`)
- names are not normalized

Memory model:
- names are duplicated with heap `malloc`, not arena allocation
- the telemetry store owns those copies until `diag_telemetry_reset()`

Allocation-failure behavior:
- if capacity growth allocation fails, the function returns early after `total` was already incremented
- if name duplication fails, the function returns early after `total` was already incremented

Practical consequence:
- `unsupported.total` can be greater than the sum of per-name counts if recording fails during allocation pressure

### 6.3 Queries

- `size_t diag_telemetry_unsupported_total(void)`
- `size_t diag_telemetry_unsupported_unique(void)`
- `size_t diag_telemetry_unsupported_count_for(const char *cmd_name)`

Query behavior:
- `unsupported_total` returns total accepted record attempts
- `unsupported_unique` returns the number of distinct stored names
- `unsupported_count_for(NULL)` returns `0`
- unknown names return `0`

### 6.4 Summary Emission

- `void diag_telemetry_emit_summary(void)`

Behavior:
- emits nothing if `unsupported.total == 0`
- otherwise writes one aggregate warning line plus one per-command warning line

Current line shapes:

```text
TELEMETRY|unsupported_commands|total=<N>|unique=<M>
TELEMETRY|unsupported_command|name=<cmd>|count=<K>
```

All summary lines are emitted with `NOB_WARNING`.

### 6.5 Report File

- `bool diag_telemetry_write_report(const char *path, const char *source_label)`

Behavior:
- returns `false` if `path == NULL` or empty
- opens `path` in append mode (`"a"`)
- returns `false` if the file cannot be opened
- appends one run header plus one indented line per stored command
- returns `true` on successful write

Current file format:

```text
run_ts=<epoch-seconds> source=<label-or-> total=<N> unique=<M>
  cmd=<name> count=<K>
```

Formatting notes:
- `run_ts` uses `time(NULL)` converted to a signed long long for printing
- `source_label == NULL` or empty becomes `"-"`
- the file is append-only; the function does not truncate prior runs

## 7. Lifecycle Patterns

Typical CLI usage in the current app:

1. `diag_reset()`
2. `diag_set_strict(...)`
3. `diag_telemetry_reset()`
4. run lexer/parser/evaluator
5. optionally emit telemetry summary/report
6. inspect `diag_has_errors()` / counts

This ordering matters because:
- `diag_reset()` alone does not clear telemetry
- `diag_telemetry_reset()` alone does not clear warning/error counters

## 8. Integration Boundaries

The diagnostics module is the shared sink used by:
- the CLI app (`src_v2/app/nobify.c`)
- parser error reporting
- evaluator logging

Evaluator-specific diagnostic classification (`src_v2/evaluator/eval_diag_classify.c`) is a higher-level layer that decides metadata before calling into this shared logger. It is not part of the contract of `diagnostics.h`.

## 9. Current Non-Goals / Limitations

Current intentional limitations:

- No per-thread or per-context isolation.
- No structured retrieval API for previously emitted diagnostics.
- No callback sink abstraction.
- No severity levels beyond warning/error.
- No deduplication of repeated diagnostics.
- Telemetry storage can partially lose per-command detail on allocation failure while still increasing total count.

## 10. Example

```c
diag_reset();
diag_set_strict(true);
diag_telemetry_reset();

diag_log(DIAG_SEV_WARNING,
         "parser",
         "CMakeLists.txt",
         12, 3,
         "if",
         "deprecated form",
         "rewrite the condition");

// In strict mode:
// - warning_count == 1
// - error_count == 1
// - the emitted line is tagged as ERROR
```
