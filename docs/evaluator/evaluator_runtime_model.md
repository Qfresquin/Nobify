# Evaluator Runtime Model (Rewrite Draft)

Status: Draft rewrite. This document is intended to describe the runtime lifecycle and state model for the new evaluator documentation set.

## Rewrite Boundary

This file is a placeholder for the runtime-model slice of the evaluator rewrite.

Current rule:
- use `docs/Evaluator/` as historical reference,
- define the runtime model here without inheriting the archived annex structure by default,
- keep this file subordinate to `evaluator_v2_spec.md`.

## Planned Sections

- Evaluator lifecycle
- Arena ownership and memory phases
- Context state and persistent runtime data
- Stop state and control flags
- Interaction between parser output and evaluator execution
