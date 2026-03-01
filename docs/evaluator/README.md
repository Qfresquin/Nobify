# Evaluator v2 Documentation Map

This folder is the evaluator documentation baseline for the v2 architecture.

## Canonical Precedence

1. `evaluator_v2_spec.md` is the canonical evaluator contract.
2. Annexes refine specific areas and must not contradict the canonical spec:
- `eval_dispatcher_v2_spec.md`
- `eval_expr_v2_spec.md`
- `event_ir_v2_spec.md`
- `evaluator_v2_compat_architecture.md`
- `evaluator_v2_coverage_status.md`

If any conflict exists, `evaluator_v2_spec.md` wins and annexes must be updated.

## File Roles

- `evaluator_v2_spec.md`
Core boundary, runtime model, public evaluator API contract, divergence policy, and roadmap separation.

- `eval_dispatcher_v2_spec.md`
Command routing model, unknown-command fallback behavior, and capability table contract.

- `eval_expr_v2_spec.md`
Variable expansion, truthiness, `if()/while()` expression semantics, and host predicate behavior.

- `event_ir_v2_spec.md`
Evaluator-to-downstream event schema and event stream memory contract.

- `evaluator_v2_compat_architecture.md`
Compatibility profiles, diagnostic classification, stop/continue decisions, and run reporting.

- `evaluator_v2_coverage_status.md`
Implementation coverage matrix (`FULL|PARTIAL|MISSING`) at command and subcommand granularity.

## Update Rules

- Treat `src_v2/evaluator/*.c`, `src_v2/evaluator/*.h`, and `src_v2/transpiler/event_ir.*` as source of truth.
- Keep implemented behavior and roadmap behavior in separate sections.
- For each `PARTIAL` entry in coverage, include at least one explicit delta vs CMake behavior.
- Keep links local to this folder when possible.
