# Evaluator Diagnostics (Rewrite Draft)

Status: Draft rewrite. This document is intended to describe evaluator-side diagnostic emission and runtime stop behavior.

## Rewrite Boundary

This file is a placeholder for the evaluator-diagnostics slice of the rewrite.

Current rule:
- use `docs/Evaluator/` as historical reference,
- treat `docs/diagnostics/` as the shared logger contract,
- document only evaluator-specific diagnostic behavior here,
- keep this file subordinate to `evaluator_v2_spec.md`.

## Planned Sections

- Evaluator diagnostic emission path
- Severity shaping and profile interaction
- Classification metadata
- Stop/continue decisions
- Run-report coupling
