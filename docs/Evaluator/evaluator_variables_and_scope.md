# Evaluator Variables and Scope (Rewrite Draft)

Status: Draft rewrite. This document is intended to describe evaluator variable visibility, scope rules, and mutation behavior.

## Rewrite Boundary

This file is a placeholder for the variables/scope slice of the evaluator rewrite.

Current rule:
- use `docs/Evaluator/` as historical reference,
- document variable semantics here rather than spreading them implicitly across runtime docs,
- keep this file subordinate to `evaluator_v2_spec.md`.

## Planned Sections

- Scope stack model
- Variable lookup rules
- Current vs parent vs global writes
- Macro bindings vs function scopes
- Cache and environment variable interaction
