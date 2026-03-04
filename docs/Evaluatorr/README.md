# Evaluator v2 Documentation Map (Archived Baseline)

This folder preserves the previous evaluator documentation set as the archived baseline during the evaluator-doc rewrite.

The active rewrite now lives in `docs/evaluator/`.

## Canonical Precedence

Within this archived documentation set:

1. `evaluator_v2_spec.md` is the canonical evaluator contract.
2. Every other file in this folder is an annex to that canonical spec.

Normative runtime annexes:
- `eval_dispatcher_v2_spec.md`
- `eval_expr_v2_spec.md`
- `event_ir_v2_spec.md`

Normative compatibility and diagnostic-shaping annexes:
- `evaluator_v2_compat_architecture.md`

Analytical annexes:
- `evaluator_v2_coverage_status.md`
- `evaluator_v2_full_audit.md`
- `event_ir_coverage_matrix.md`

If any conflict exists inside this archived set, `evaluator_v2_spec.md` wins.
- Normative annexes must be updated to match the canonical spec.
- Analytical annexes must be reconciled with the canonical spec and current implementation.

## File Roles

- `evaluator_v2_spec.md`
Canonical root document: core boundary, runtime model, public evaluator API contract, divergence policy, and roadmap separation.

### Runtime Annexes

- `eval_dispatcher_v2_spec.md`
Command routing model, unknown-command fallback behavior, and capability table contract.

- `eval_expr_v2_spec.md`
Variable expansion, truthiness, `if()/while()` expression semantics, and host predicate behavior.

- `event_ir_v2_spec.md`
Evaluator-to-downstream event schema and event stream memory contract.

### Compatibility and Diagnostics Annexes

- `evaluator_v2_compat_architecture.md`
Compatibility profiles, evaluator-side diagnostic shaping/classification, stop/continue decisions, and run reporting.

### Analytical Annexes

- `evaluator_v2_coverage_status.md`
Registry-backed implementation coverage matrix (`FULL|PARTIAL|MISSING`) for dispatcher-visible built-ins plus selected subcommand detail.

- `evaluator_v2_full_audit.md`
Full command-universe audit against the scoped CMake `3.28.6` surface, including missing-command inventory, policy findings, and code-health findings.

- `event_ir_coverage_matrix.md`
Migration-planning matrix for the semantic Event IR rollout, tracking which evaluator command families already emit domain events versus planned coverage.

## Update Rules

- Treat `src_v2/evaluator/*.c`, `src_v2/evaluator/*.h`, and `src_v2/transpiler/event_ir.*` as source of truth.
- If the archived set must be corrected for reference purposes, update `evaluator_v2_spec.md` first and then adjust affected annexes.
- Keep implemented behavior and roadmap behavior in separate sections.
- For each `PARTIAL` entry in coverage, include at least one explicit delta vs CMake behavior.
- Keep `evaluator_v2_coverage_status.md` and `evaluator_v2_full_audit.md` aligned on command counts and confirmed divergences.
- Keep links local to this folder when possible.
