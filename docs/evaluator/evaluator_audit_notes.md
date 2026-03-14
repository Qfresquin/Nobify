# Evaluator Audit Notes

Status: Implementation Audit. This document records implementation-current
findings, risks, and follow-up priorities for `src_v2/evaluator`.

Canonical target behavior is defined in:
- [evaluator_v2_spec.md](./evaluator_v2_spec.md)
- [evaluator_architecture_target.md](./evaluator_architecture_target.md)

## 1. Scope

This audit captures:
- current implementation divergences from the target architecture,
- semantic gaps that slow CMake 3.28 parity work,
- maintainability and testability hotspots,
- prioritization notes for future refactor or feature batches.

It does not redefine the target contract.

## 2. Audit Sources

Primary implementation evidence comes from:
- `src_v2/evaluator/`
- evaluator tests under `test_v2/evaluator/`
- command-level audits recorded in
  [evaluator_coverage_matrix.md](./evaluator_coverage_matrix.md)

Primary target references are:
- [evaluator_v2_spec.md](./evaluator_v2_spec.md)
- [evaluator_architecture_target.md](./evaluator_architecture_target.md)

## 3. Current Risk Buckets

### 3.1 Architectural Drift

Common drift patterns to watch:
- semantic truth stored only in variable maps
- command handlers mixing parse, validation, mutation, and projection
- cross-directory behavior depending on caller-local state
- backend side effects open-coded inside unrelated handlers

### 3.2 Coverage Pressure

Feature families that usually expose architectural weakness:
- cross-directory property and definition queries
- install/export/package interactions
- nested execution and deferred replay
- try-compile / try-run / environment-sensitive behavior

### 3.3 Maintainability Pressure

Signals that typically deserve follow-up:
- duplicated parsing logic,
- duplicated property synthesis logic,
- command-specific storage that should be part of canonical models,
- tests that only validate variable echoes and not committed semantic state.

## 4. Recommended Audit Usage

Use this document to decide:
- what to fix next in the implementation,
- which gaps are architectural vs command-local,
- where a new feature should land in the target model.

Do not use this document as the architecture source of truth.
