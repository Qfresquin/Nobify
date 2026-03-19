# Evaluator Docs

## Status

This directory contains the canonical target documentation for the evaluator
refactor.

As of March 19, 2026:
- `evaluator_v2_spec.md` is the top-level evaluator contract
- `evaluator_architecture_target.md` is the canonical architecture companion
- the public evaluator boundary is the session/request model documented in the
  v2 specs
- `Evaluator_Context` is no longer part of the public API surface
- `eval_session_run(...)` now executes each run inside a fresh
  `EvalExecContext` linked back to a persistent `EvalSession`
- downstream contracts may depend on `Event_Stream`, but any evaluator API
  references must use the target `EvalSession` / `EvalExec_Request` /
  `EvalRunResult` boundary

The canonical evaluator boundary is:

`Ast_Root + EvalSession + EvalExec_Request -> eval_session_run(...) -> EvalRunResult + optional Event_Stream`

## Canonical Target Docs

- [Top-level spec](./evaluator_v2_spec.md)
- [Architecture target](./evaluator_architecture_target.md)
- [Runtime model](./evaluator_runtime_model.md)
- [Execution model](./evaluator_execution_model.md)
- [Dispatch](./evaluator_dispatch.md)
- [Variables and scope](./evaluator_variables_and_scope.md)
- [Compatibility model](./evaluator_compatibility_model.md)
- [Event IR contract](./evaluator_event_ir_contract.md)
- [Diagnostics](./evaluator_diagnostics.md)
- [Command capabilities](./evaluator_command_capabilities.md)

These documents define the active evaluator contract for new work. Remaining
implementation gaps are semantic-coverage gaps, not public-boundary drift back
toward the legacy create/run API.

## Implementation Audits

- [Structural refactor plan](./Refator%C3%A7%C3%A3o%20Estrutural.md)
- [Audit notes](./evaluator_audit_notes.md)
- [Coverage matrix](./evaluator_coverage_matrix.md)
- [Expressions audit](./evaluator_expressions.md)
- [src_v2 code standardization](./evaluator_src_v2_code_standardization.md)

These documents may describe the current `src_v2/evaluator` implementation, but
they do not redefine the target public API or runtime ownership model.

## Downstream Contracts

- [Event IR spec](../transpiler/event_ir_v2_spec.md)
- [Build model docs](../build_model/README.md)

Those downstream documents should treat `Event_Stream` as the stable
evaluator-output contract and should depend only on the session/request/runtime
surface, not evaluator internals.
