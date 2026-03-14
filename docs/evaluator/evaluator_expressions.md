# Evaluator Expressions

Status: Implementation Audit. This document describes the current expression
subsystem implemented in `src_v2/evaluator`.

Canonical target architecture remains defined in:
- [evaluator_v2_spec.md](./evaluator_v2_spec.md)
- [evaluator_architecture_target.md](./evaluator_architecture_target.md)

## 1. Scope

This audit covers the implementation-current expression subsystem, including:
- variable expansion,
- truthiness,
- `if()` and `while()` condition parsing,
- diagnostics and known current limitations.

It does not redefine the evaluator runtime or public API.

## 2. Implementation Sources

Primary implementation files for this slice:
- `src_v2/evaluator/eval_expr.c`
- `src_v2/evaluator/eval_expr.h`
- `src_v2/evaluator/evaluator.c`
- `src_v2/evaluator/evaluator_internal.h`
- `src_v2/evaluator/eval_utils.c`

## 3. Audit Purpose

This document exists to explain how expressions work today and where they still
diverge from the target model or CMake 3.28 expectations.

Use it for implementation review and follow-up planning, not as the canonical
architecture reference.
