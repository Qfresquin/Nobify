# Evaluator Documentation Rewrite

This folder is the active workspace for the new evaluator documentation rewrite.

## Intent

- `docs/Evaluator/` keeps the previous evaluator documentation set preserved as the legacy baseline.
- `docs/evaluator/` is where the replacement documentation should be written.
- When the rewrite is complete and validated, `docs/Evaluator/` can be removed.

## Current Migration Rule

During the rewrite:
- treat `docs/Evaluator/` as reference/archive material,
- treat this folder as the source for the next documentation structure,
- do not assume file names or annex structure must match the archived version.

## First Active Draft

- `evaluator_v2_spec.md`
Initial placeholder for the new canonical evaluator document.

## Planned Draft Set

- `evaluator_v2_spec.md`
Canonical root document for the rewritten evaluator docs.

- `evaluator_runtime_model.md`
Draft for evaluator lifecycle, memory model, and runtime state.

- `evaluator_execution_model.md`
Draft for AST execution flow and structural command behavior.

- `evaluator_variables_and_scope.md`
Draft for variables, scopes, bindings, and mutation rules.

- `evaluator_dispatch.md`
Draft for command routing and unknown-command fallback behavior.

- `evaluator_expressions.md`
Draft for variable expansion, truthiness, and condition evaluation.

- `evaluator_diagnostics.md`
Draft for evaluator-side diagnostics, severity shaping, and stop behavior.

- `evaluator_event_ir_contract.md`
Draft for evaluator-to-Event-IR output guarantees.

- `evaluator_compatibility_model.md`
Draft for compat profiles, unsupported policy, and error-budget controls.

- `evaluator_command_capabilities.md`
Draft for documented command capability metadata and fallback semantics.

- `evaluator_coverage_matrix.md`
Draft analytical matrix for implementation coverage tracking.

- `evaluator_audit_notes.md`
Draft analytical notes for findings, risks, and documentation-driven review.
