# Documentation Archive

## Status
Canonical archive policy. Active support document.

## Role
This file explains how historical documentation is stored and how it should be
read after the `CMake 3.8 parity total -> Nob` reset.

## Product direction
The active documentation now describes Nobify as a `CMake 3.8` parity project.
Archive material exists to preserve design history and implementation context,
not to compete with that direction.

## Current gap
Many archived documents still reflect `CMake 3.28`, `supported subset`,
coverage-matrix, or roadmap-era framing. That is expected and intentionally
preserved as history.

## Guarantees
- Files under `docs/archive/` are historical or superseded.
- Archive material may explain why the code looks the way it does.
- Archive material is never the canonical product contract.

## Non-goals
- Keeping historical docs fully synchronized with the new active contract.
- Using archived roadmap language as the current project baseline.

## Primary code
- None. Archive policy is documentation-only.

## Primary tests
- None. Archive policy is validated by tree structure and active-doc coherence.

## Reading rule
Start in `docs/README.md`. Only enter `docs/archive/` when you need historical
context, migration rationale, or older terminology that still appears in the
code.
