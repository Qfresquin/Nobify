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

## Canonical Precedence

In this folder:
- `evaluator_v2_spec.md` is the canonical evaluator contract.
- Slice documents are subordinate annexes.
- If an annex conflicts with the canonical spec, `evaluator_v2_spec.md` wins.

## Current Status

Substantive drafts already written:
- `evaluator_runtime_model.md`
- `evaluator_execution_model.md`
- `evaluator_variables_and_scope.md`
- `evaluator_dispatch.md`
- `evaluator_diagnostics.md`
- `evaluator_compatibility_model.md`
- `evaluator_expressions.md`
- `evaluator_event_ir_contract.md`
- `evaluator_command_capabilities.md`
- `evaluator_coverage_matrix.md`
- `evaluator_audit_notes.md`

Placeholder drafts still pending detailed content:
- none

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
