# Evaluator Execution Model (Rewrite Draft)

Status: Draft rewrite. This document is intended to describe how the evaluator executes AST nodes and structural flow.

## Rewrite Boundary

This file is a placeholder for the execution-model slice of the evaluator rewrite.

Current rule:
- use `docs/Evaluator/` as historical reference,
- describe execution order and node semantics here,
- keep this file subordinate to `evaluator_v2_spec.md`.

## Planned Sections

- Root AST traversal
- Command-node execution
- Structural control flow (`if`, `foreach`, `while`)
- User command execution (`function`, `macro`, `block`)
- Flow-control propagation (`return`, `break`, `continue`)
