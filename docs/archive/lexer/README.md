# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Lexer v2 Documentation Map

This folder documents the lexer used by the v2 parser/evaluator pipeline.

## Canonical Precedence

1. `lexer_v2_spec.md` is the canonical lexer contract.

If implementation and docs diverge, `src_v2/lexer/lexer.c` and `src_v2/lexer/lexer.h` are the source of truth until the docs are updated.

## File Roles

- `lexer_v2_spec.md`
Token surface, skip rules, tokenization semantics, position tracking, and intentional divergences from stricter CMake lexing.

## Update Rules

- Keep the spec aligned with shipped behavior, including permissive recovery behavior.
- Treat `test_v2/lexer/test_lexer_v2_suite.c` and `test_v2/lexer/golden/lexer_all.txt` as the executable baseline for edge cases.
- Document token text slices exactly as emitted today; the lexer preserves delimiters in several token kinds.
- Call out parser-owned validation separately from lexer tokenization, because the lexer intentionally accepts broader identifier shapes than command names.
