# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Diagnostics v2 Documentation Map

This folder documents the shared diagnostics subsystem used by the v2 pipeline.

## Canonical Precedence

1. `diagnostics_v2_spec.md` is the canonical contract for the global diagnostics module in `src_v2/diagnostics/`.

If implementation and docs diverge, `src_v2/diagnostics/diagnostics.c` and `src_v2/diagnostics/diagnostics.h` are the source of truth until the docs are updated.

## File Roles

- `diagnostics_v2_spec.md`
Global counters, strict-mode behavior, structured logging format, unsupported-command telemetry, and lifecycle rules.

## Update Rules

- Keep the spec aligned with shipped behavior, including global-state quirks.
- Document which reset functions do and do not clear each piece of state.
- Call out strict-mode counting semantics explicitly, because warnings can be counted as both warnings and errors.
- Keep evaluator-specific diagnostic classification (`src_v2/evaluator/eval_diag_*`) separate unless this folder later gains an annex for it.
