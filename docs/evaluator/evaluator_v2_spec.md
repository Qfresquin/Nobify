# Evaluator v2 Specification (Rewrite Draft)

Status: Canonical rewrite draft for `docs/evaluator/`. This file defines the top-level evaluator contract; focused slice documents are subordinate.

## 1. Scope and Boundary

The evaluator consumes parser AST and emits semantic Event IR through an append-only stream.

Primary boundary:
- Input: `Ast_Root` from `src_v2/parser`.
- Output: `Event_Stream` from `src_v2/transpiler/event_ir.h`.
- Build-IR coupling (`build_model.h`) is out of scope in this roadmap (decision #2 rejected).

This document specifies:
- public API behavior and lifecycle contracts,
- runtime-state and stop-model guarantees,
- execution/dispatch boundaries,
- diagnostics/reporting boundary,
- compatibility-profile controls,
- evaluator-side Event IR guarantees.

Detailed semantics are delegated to annexes in `docs/evaluator/`, but those annexes cannot redefine this root contract.

## 2. Source of Truth

Primary implementation files:
- `src_v2/evaluator/evaluator.h`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/eval_exec_core.h`
- `src_v2/evaluator/eval_exec_core.c`
- `src_v2/evaluator/eval_user_command.c`
- `src_v2/evaluator/eval_nested_exec.c`
- `src_v2/evaluator/evaluator_internal.h`
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/eval_compat.c`
- `src_v2/evaluator/eval_report.c`
- `src_v2/evaluator/eval_diag_classify.c`
- `src_v2/evaluator/eval_string.c`
- `src_v2/evaluator/eval_string_internal.h`
- `src_v2/evaluator/eval_string_text.c`
- `src_v2/evaluator/eval_string_regex.c`
- `src_v2/evaluator/eval_string_json.c`
- `src_v2/evaluator/eval_string_misc.c`
- `src_v2/evaluator/eval_flow.c`
- `src_v2/evaluator/eval_file.c`
- `src_v2/evaluator/eval_file_internal.h`
- `src_v2/evaluator/eval_file_path.c`
- `src_v2/evaluator/eval_file_glob.c`
- `src_v2/evaluator/eval_file_rw.c`
- `src_v2/evaluator/eval_file_copy.c`
- `src_v2/evaluator/eval_file_extra.c`
- `src_v2/evaluator/eval_file_fsops.c`
- `src_v2/evaluator/eval_file_transfer.c`
- `src_v2/evaluator/eval_file_generate_lock_archive.c`
- `src_v2/evaluator/eval_vars.c`
- `src_v2/evaluator/eval_include.c`

## 3. Public API Contract

Public constructor/destructor:

```c
Evaluator_Context *evaluator_create(const Evaluator_Init *init);
void evaluator_destroy(Evaluator_Context *ctx);
```

Execution and reporting:

```c
Eval_Result evaluator_run(Evaluator_Context *ctx, Ast_Root ast);
const Eval_Run_Report *evaluator_get_run_report(const Evaluator_Context *ctx);
const Eval_Run_Report *evaluator_get_run_report_snapshot(const Evaluator_Context *ctx);
```

Compatibility and metadata:

```c
bool evaluator_set_compat_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile);
bool evaluator_register_native_command(Evaluator_Context *ctx, const Evaluator_Native_Command_Def *def);
bool evaluator_unregister_native_command(Evaluator_Context *ctx, String_View command_name);
bool evaluator_get_command_capability(Evaluator_Context *ctx, String_View command_name, Command_Capability *out_capability);
```

Current top-level API behavior:
- `evaluator_create(...)` returns `NULL` if required init pointers are missing or setup fails.
- `evaluator_run(...)` returns:
  - `EVAL_RESULT_OK` for clean execution,
  - `EVAL_RESULT_SOFT_ERROR` when non-fatal errors were emitted,
  - `EVAL_RESULT_FATAL` for stop-state paths (OOM / explicit stop / null or already-stopped context).
- run report is reset/finalized per top-level run.
- capability lookup is introspection metadata, not a direct execution call.

## 4. Lifecycle Model

### 4.1 Create Phase

`evaluator_create(...)` initializes one mutable `Evaluator_Context` with:
- runtime arenas,
- event stream sink,
- initial source/binary/list variables,
- one global scope,
- baseline compatibility defaults,
- baseline host/toolchain compatibility variables.

### 4.2 Run Phase

`evaluator_run(...)` performs:
1. run-report reset and file-depth entry,
2. AST traversal through the internal execution-core service,
3. deferred directory flush,
4. deferred file-generation flush,
5. return-state cleanup,
6. file-scope lock release and report finalization.

### 4.3 Destroy Phase

`evaluator_destroy(...)` releases runtime-managed heap tables (for example `stb_ds` maps and file locks), but the context memory itself is arena-owned.

Practical ownership rule:
- full memory release depends on destroying the owning arena(s), especially `event_arena`.

## 5. Runtime State and Ownership

The evaluator is intentionally stateful.

Core runtime ownership model:
- `ctx->arena`: temporary scratch allocations (frequently rewound).
- `ctx->event_arena`: persistent evaluator/event payload storage.
- `known_targets_arena` and `user_commands_arena`: sub-arenas tied to `event_arena` lifetime.

State includes:
- variable scopes and policy stacks,
- native-command registry,
- user-command registry,
- macro/block/deferred/file-lock stacks,
- diagnostics/report counters,
- stop-state flags (`oom`, `stop_requested`).

Threading model:
- current implementation is not thread-safe.

## 6. Execution and Dispatch Boundary

Execution is node-driven:
- structural nodes (`if`, `foreach`, `while`, `function`, `macro`) use dedicated evaluator paths,
- `NODE_COMMAND` uses dispatcher routing.

Dispatch routing order:
1. native command lookup in the context registry (built-ins are seeded at create),
2. user command lookup/invocation,
3. unknown-command diagnostic fallback.

Unknown-command behavior is policy-driven (`unsupported_policy`) and can be warning/error/no-op-warning semantics.

## 7. Compatibility Model (Top-Level)

Supported profiles:
- `EVAL_PROFILE_PERMISSIVE`
- `EVAL_PROFILE_STRICT`
- `EVAL_PROFILE_CI_STRICT`

Related runtime controls:
- unsupported command policy (`EVAL_UNSUPPORTED_*`)
- error budget
- continue-on-error variable path

Current behavior:
- compatibility state can be changed both through API (`evaluator_set_compat_profile`) and by runtime variable writes sampled through `eval_refresh_runtime_compat(...)`,
- runtime-variable changes become effective at the next command-cycle refresh boundary by default.

## 8. Refactor Direction

Strategic roadmap source:
- `docs/evaluator/Refatorção Estrutural.md`

Direction constraints for refactor waves:
- architecture remains context-centric (`Evaluator_Context` is still the integration boundary),
- subsystem extraction is incremental and service-oriented, not a one-shot object-graph rewrite,
- execution traversal, user-command lifecycle, and nested external execution may move into dedicated internal services while preserving the public API,
- evaluator boundary remains AST -> Event IR.

Declared non-goals for the current roadmap:
- no `BuildModelBuilder`/Build IR coupling in evaluator scope,
- no child evaluator context model for nested execution.

Public API stability rule:
- evaluator public API contracts remain unchanged during these refactor waves unless a separate RFC explicitly defines breaking API updates.

## 9. Diagnostics and Run Report Boundary

Evaluator diagnostics are emitted through `eval_emit_diag(...)`, which currently:
- classifies evaluator metadata (`code`, `error_class`),
- applies evaluator compatibility severity shaping,
- applies process-global diagnostics strict shaping as the final severity step,
- writes one external shared-log line through `diag_log(...)`,
- appends one `EVENT_DIAG`,
- updates `Eval_Run_Report`,
- applies compatibility stop/continue decision logic.

Important boundary:
- process-global diagnostics strict mode is the final severity authority for evaluator diagnostics.
- `EVENT_DIAG.severity`, `Eval_Run_Report`, error-budget checks, stop behavior, and final `Eval_Result` all follow that final global-effective severity.
- `Eval_Run_Report.warning_count` mirrors original warning inputs while `error_count` mirrors final effective errors, matching the shared diagnostics module.

## 10. Event IR Output Contract (Top-Level)

The evaluator appends semantic events to the provided stream and does not rewrite prior events.

Current event categories produced include:
- command-call events,
- diagnostic events,
- flow events,
- variable/cache-related events in command paths that emit them.

Ownership rule:
- event payload strings are copied into persistent evaluator-owned arena storage (`event_arena`-backed).

## 11. Stop-State Contract

Stop predicate:
- evaluator is considered stopped when `oom` or `stop_requested` is true.

Current propagation pattern:
- execution entry points return `EVAL_RESULT_FATAL` when stop is active,
- OOM (`ctx_oom`) marks both `oom` and `stop_requested`,
- stop is cooperative across traversal, dispatch, diagnostics, and nested file execution.

## 12. Current Known Divergences / Limits

Current intentionally visible limits:
- `while()` execution is guarded by `CMAKE_NOBIFY_WHILE_MAX_ITERATIONS`, defaulting to `10000`.
- the `while()` guard is read once at `while` node entry; mutations inside the loop affect only the next `while()` node.
- invalid `CMAKE_NOBIFY_WHILE_MAX_ITERATIONS` values emit a warning and fall back to `10000`.
- native dispatcher lookup is case-insensitive and index-backed through the runtime registry.
- capability lookup shares that same native registry lookup path for native-command introspection only.
- unknown-command fallback is generic and does not dynamically apply capability metadata.
- nested evaluation remains shared-context; child-context isolation is out of scope in the current roadmap.

## 13. Annex Map

Subordinate detailed docs:
- `Refatorção Estrutural.md`
- `evaluator_runtime_model.md`
- `evaluator_execution_model.md`
- `evaluator_variables_and_scope.md`
- `evaluator_dispatch.md`
- `evaluator_diagnostics.md`
- `evaluator_compatibility_model.md`
- `evaluator_expressions.md`
- `evaluator_event_ir_contract.md`
- `evaluator_command_capabilities.md`
- `evaluator_src_v2_code_standardization.md`
- `evaluator_coverage_matrix.md`
- `evaluator_audit_notes.md`

Planned/pending detailed docs:
- none
