# Arena v2 Documentation Map

This folder documents the arena allocator used across `src_v2`.

## Canonical Precedence

1. `arena_v2_spec.md` is the canonical contract for the allocator and `arena_dyn.h` helpers.

If implementation and docs diverge, `src_v2/arena/arena.c`, `src_v2/arena/arena.h`, and `src_v2/arena/arena_dyn.h` are the source of truth until the docs are updated.

## File Roles

- `arena_v2_spec.md`
Public API contract, memory model, cleanup lifecycle, rewind semantics, and dynamic-array helper behavior.

## Update Rules

- Keep the spec aligned with the shipped behavior, not with planned allocator features.
- Separate implemented behavior from roadmap ideas when adding future notes.
- Treat `test_v2/arena/test_arena_v2_suite.c` as the executable behavioral baseline for edge cases.
- Document invalid-input behavior explicitly, because the API intentionally returns safe no-op/`NULL` results in many cases.
