# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Parser v2 Documentation Map

This folder documents the v2 parser that builds the arena-owned AST from lexer tokens.

## Canonical Precedence

1. `parser_v2_spec.md` is the canonical parser contract.
2. `parser_v2_grammar.md` is a focused annex for grammar shape and recovery notes. It must not contradict the canonical spec.

If implementation and docs diverge, `src_v2/parser/parser.c` and `src_v2/parser/parser.h` are the source of truth until the docs are updated.

## File Roles

- `parser_v2_spec.md`
Public API, AST shape, argument grouping, control-flow node contracts, diagnostics, and recovery behavior.

- `parser_v2_grammar.md`
Condensed grammar view, command shape rules, and quick-reference recovery notes.

## Update Rules

- Keep the docs aligned with shipped behavior, including recovery quirks and permissive fallback paths.
- Treat `test_v2/parser/test_parser_v2_suite.c` and `test_v2/parser/golden/parser_all.txt` as the executable baseline for edge cases.
- Keep lexer responsibilities separate from parser responsibilities; tokenization details belong in `docs/lexer/`.
- Document node-drop behavior explicitly whenever a syntax error causes a statement to be discarded from the final AST.
