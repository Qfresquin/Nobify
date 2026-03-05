# Evaluator `src_v2` Code Standardization

Status: normative for evaluator runtime code in `src_v2/evaluator`.

## 1. Scope

This standard applies to evaluator execution/runtime code in `src_v2/evaluator`.

It is mandatory for:
- AST execution (`evaluator_run`, `eval_node`, `eval_node_list`, `eval_execute_file`, `eval_run_ast_inline`),
- command dispatch (`eval_dispatch_command`),
- native command handlers (`eval_handle_*`),
- runtime nested execution paths (`include`, `add_subdirectory`, `cmake_language(EVAL/CALL)`, deferred calls).

## 2. Result Type Contract

Execution functions must use the tri-state result type:
- `EVAL_RESULT_OK`
- `EVAL_RESULT_SOFT_ERROR`
- `EVAL_RESULT_FATAL`

Mandatory helpers from `evaluator_internal.h`:
- constructors: `eval_result_ok`, `eval_result_soft_error`, `eval_result_fatal`
- merge: `eval_result_merge` with precedence `FATAL > SOFT_ERROR > OK`
- fatal query: `eval_result_is_fatal`
- context conversion: `eval_result_from_ctx`

## 3. Return Matrix

Return type by function category:
- execution path functions: `Eval_Result` (mandatory)
- native handlers (`Eval_Native_Command_Handler`): `Eval_Result` (mandatory)
- dispatch entry (`eval_dispatch_command`): `Eval_Result` (mandatory)
- predicates/query helpers (`is_*`, parser helpers, option matchers): `bool` (allowed)
- pure data transforms/formatters: `bool` or domain-specific scalar (allowed)

## 4. Propagation Rules

Rules are mandatory:
1. Fatal short-circuit: if a callee returns `EVAL_RESULT_FATAL`, return fatal immediately.
2. Soft accumulation: aggregate non-fatal outcomes with `eval_result_merge`.
3. End-of-scope merge: execution orchestrators should merge local aggregate with current runtime state (`eval_result_ok_if_running` or `eval_result_from_ctx` per call site intent).
4. Never encode execution-state propagation as raw bool return checks.

## 5. Diagnostic Mapping

Diagnostic emission must map to result states:
- note/warning diagnostics -> `EVAL_RESULT_OK`
- non-fatal error diagnostics -> `EVAL_RESULT_SOFT_ERROR`
- OOM/stop-state diagnostics paths -> `EVAL_RESULT_FATAL`

`eval_emit_diag` is the canonical boundary for this mapping.

## 6. Runtime Stop-State Ownership

Direct writes to runtime fatal flags are restricted:
- allowed writer file: `src_v2/evaluator/evaluator.c`
- forbidden elsewhere: direct assignment to `ctx->oom` / `ctx->stop_requested`

Other files must use public/internal helpers only.

## 7. Arena and Ownership Conventions

- temporary statement-scoped strings/arrays: `ctx->arena`
- persistent event payload strings: `ctx->event_arena`
- native command registry storage: `ctx->native_commands_arena`
- user command registry storage: `ctx->user_commands_arena`

Do not return pointers to memory that will be invalidated by immediate temp-arena rewinds.

## 8. Naming Conventions

- `eval_handle_*`: command handlers (execution, `Eval_Result`)
- `eval_*_emit_*` or `eval_emit_*`: event/diagnostic emission helpers
- `*_parse_*`, `*_is_*`, `*_match_*`: predicate/parser helpers (`bool` allowed)
- `*_from_ctx` helpers: context-to-result conversion boundaries

## 9. Forbidden Patterns

The following are forbidden in `src_v2/evaluator`:
- `return !eval_should_stop(ctx);`
- `bool eval_handle_...` signatures
- legacy native handler signature: `typedef bool (*Eval_Native_Command_Handler)(...)`
- direct writes to `ctx->oom` / `ctx->stop_requested` outside `evaluator.c`

## 10. Review Checklist

Each evaluator execution PR must verify:
- execution functions use `Eval_Result`
- fatal paths short-circuit explicitly
- soft paths are merged, not dropped
- diagnostics map to result-kind contract
- no forbidden patterns are introduced
- nested execution (`include`, inline AST, deferred) preserves tri-state semantics

## 11. CI Enforcement

Automated check script:
- `test_v2/evaluator/check_result_type_conventions.sh`

Current CI workflow integration:
- `.github/workflows/evaluator-file-parity.yml`
- runs before evaluator suite on Linux and Windows jobs
