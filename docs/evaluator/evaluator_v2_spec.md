# Evaluator v2 Specification (Canonical)

Status: Normative for evaluator v2 behavior currently implemented in `src_v2/evaluator`.

## 1. Scope and Boundary

The evaluator executes CMake-language semantics over AST input and emits an append-only `Cmake_Event_Stream`.

Boundary contract:
- Input: `Ast_Root` from parser.
- Output: `Cmake_Event_Stream` from Event IR.
- Prohibited dependency: evaluator does not depend on Build Model internals.

This document is canonical. Supporting annexes:
- `eval_dispatcher_v2_spec.md`
- `eval_expr_v2_spec.md`
- `event_ir_v2_spec.md`
- `evaluator_v2_compat_architecture.md`
- `evaluator_v2_coverage_status.md`

## 2. Source of Truth

Normative implementation files:
- Public API: `src_v2/evaluator/evaluator.h`
- Runtime/orchestration: `src_v2/evaluator/evaluator.c`
- Internal state: `src_v2/evaluator/evaluator_internal.h`
- Command routing: `src_v2/evaluator/eval_dispatcher.c`
- Expression engine: `src_v2/evaluator/eval_expr.c`
- Compatibility/reporting: `src_v2/evaluator/eval_compat.c`, `eval_diag_classify.c`, `eval_report.c`
- Event IR: `src_v2/transpiler/event_ir.h`, `src_v2/transpiler/event_ir.c`

## 3. Public API Contract

Public evaluator API is defined in `src_v2/evaluator/evaluator.h`.

### 3.1 Core API

- `Evaluator_Context *evaluator_create(const Evaluator_Init *init)`
- `void evaluator_destroy(Evaluator_Context *ctx)`
- `bool evaluator_run(Evaluator_Context *ctx, Ast_Root ast)`

### 3.2 Compatibility and Reporting API

- `bool evaluator_set_compat_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile)`
- `const Eval_Run_Report *evaluator_get_run_report(const Evaluator_Context *ctx)`
- `const Eval_Run_Report *evaluator_get_run_report_snapshot(const Evaluator_Context *ctx)`
- `bool evaluator_get_command_capability(String_View command_name, Command_Capability *out_capability)`

### 3.3 Public Types (Required in Docs)

- `Evaluator_Init`, `Evaluator_Context` (opaque in public header)
- `Eval_Compat_Profile`
- `Eval_Run_Report`
- `Command_Capability`
- `Eval_Command_Impl_Level`, `Eval_Command_Fallback`

## 4. Runtime Model

### 4.1 Memory Model

Evaluator uses two arenas:
- `arena`: temporary statement-level expansions and parsing helpers (rewound per node).
- `event_arena`: persistent storage for event payload and long-lived evaluator state.

### 4.2 Variable and Scope Model

- Global scope exists at depth 1.
- `function()` pushes variable scope.
- `macro()` uses macro-frame bindings in caller scope model.
- `block()` can push variable scope and/or policy scope depending on options.
- `set(... PARENT_SCOPE)` and `unset(... PARENT_SCOPE)` mutate parent scope when available.

### 4.3 Control Flow Execution

- `if/elseif/else`: only taken branch is evaluated.
- `foreach`: runtime iteration supports `RANGE`, `IN ITEMS`, `IN LISTS`, `IN ZIP_LISTS`.
- `while`: runtime loop with hard iteration guard (`10000`) to prevent infinite transpilation loops.
- `break`, `continue`, `return`, `return(PROPAGATE ...)` are handled by evaluator flow state.

## 5. Compatibility Runtime Controls

Runtime controls are read from evaluator variables:
- `CMAKE_NOBIFY_COMPAT_PROFILE` (`PERMISSIVE|STRICT|CI_STRICT`)
- `CMAKE_NOBIFY_UNSUPPORTED_POLICY` (`WARN|ERROR|NOOP_WARN`)
- `CMAKE_NOBIFY_ERROR_BUDGET` (size_t, `0` = unlimited in permissive)
- `CMAKE_NOBIFY_CONTINUE_ON_ERROR`

Profile behavior summary:
- `PERMISSIVE`: continue after recoverable errors until budget is exhausted.
- `STRICT` and `CI_STRICT`: warnings are promoted to errors; error handling tends to stop.

## 6. Diagnostics and Reporting Contract

Diagnostics are emitted as `EV_DIAGNOSTIC` events with structured metadata:
- severity
- component
- command
- `code`
- `error_class`
- cause
- hint

Classification is centralized in `eval_diag_classify.c` and recorded in `Eval_Run_Report`.

`Eval_Run_Report` tracks:
- warning/error counters
- error-class counters
- unsupported counter
- overall status (`EVAL_RUN_OK`, `..._WITH_WARNINGS`, `..._WITH_ERRORS`, `EVAL_RUN_FATAL`)

## 7. Event Emission Contract

Evaluator writes only Event IR entities from `src_v2/transpiler/event_ir.h`.

Key guarantees:
- Events are append-only during evaluation.
- Origin metadata (`file_path`, `line`, `col`) is attached per emitted event.
- Event payload strings are deep-copied into `event_arena` by `event_stream_push(...)`.

See `event_ir_v2_spec.md` for full schema contract.

## 8. Command Capability Contract

Capabilities are centralized in `src_v2/evaluator/eval_command_caps.c` and surfaced by `evaluator_get_command_capability(...)`.

Each known command has:
- implementation level: `FULL|PARTIAL|MISSING`
- fallback behavior: `NOOP_WARN|ERROR_CONTINUE|ERROR_STOP`

Coverage matrix details and subcommand deltas are tracked in `evaluator_v2_coverage_status.md`.

## 9. Explicit Divergences vs CMake (Implemented)

Implemented, documented divergences include:
- Permissive-first continuation model with configurable error budget.
- Unknown command behavior controlled by unsupported-command policy.
- Generator expressions (`$<...>`) are passed through as literals at evaluator stage.
- Host-filesystem predicates (`EXISTS`, `IS_DIRECTORY`, etc.) execute on host environment during transpilation.
- Several command families are intentionally partial (see coverage matrix).

No divergence should be implicit; all intentional deltas must be classified and documented.

## 10. Roadmap (Not Yet Implemented)

The following are roadmap targets, not current guarantees:
- Broader `cmake_path()` parity beyond current implemented subset.
- Broader `file()` parity for remote backends and platform-specific semantics.
- Property read-side CMake parity in all scopes (`get_property`, `get_target_property` style behavior).
- Additional policy-semantic modeling beyond current flow/block-focused coverage.

Roadmap items must remain separate from implemented normative behavior.
