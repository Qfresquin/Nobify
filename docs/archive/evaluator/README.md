# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Evaluator Docs

## Status

This directory is the canonical evaluator documentation map.

Evaluator boundary:

`Ast_Root + EvalSession + EvalExec_Request -> eval_session_run(...) -> EvalRunResult + optional Event_Stream`

The evaluator owns semantic execution and Event IR projection. It does not own
build-model reconstruction or generated runtime execution.

## Overview

Read in this order when onboarding:

1. [Top-level spec](./evaluator_v2_spec.md)
2. [Architecture target](./evaluator_architecture_target.md)
3. [Runtime model](./evaluator_runtime_model.md)
4. [Execution model](./evaluator_execution_model.md)

## Normative Contracts

- [Dispatch](./evaluator_dispatch.md)
- [Variables and scope](./evaluator_variables_and_scope.md)
- [Compatibility model](./evaluator_compatibility_model.md)
- [Event IR contract](./evaluator_event_ir_contract.md)
- [Diagnostics](./evaluator_diagnostics.md)
- [Command capabilities](./evaluator_command_capabilities.md)

Normative documents define target behavior and public boundary expectations.

## Audits

- [Coverage matrix](./evaluator_coverage_matrix.md)
- [Audit notes](./evaluator_audit_notes.md)
- [Expressions audit](./evaluator_expressions.md)
- [src_v2 code standardization](./evaluator_src_v2_code_standardization.md)

Audit documents describe implementation state and follow-up risks. They do not
override the normative contracts.

## History

- [Structural refactor closure record](../archive/evaluator/evaluator_structural_refactor_closure.md)

## Downstream Dependencies

- [Event IR docs](../transpiler/README.md)
- [Build model docs](../build_model/README.md)
