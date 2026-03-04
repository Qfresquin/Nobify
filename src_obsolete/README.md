This directory contains code that is intentionally obsolete.

The active architecture no longer treats the legacy structural Event IR or the
legacy build model as canonical contracts.

Current policy:
- `src_obsolete/transpiler/` keeps the frozen structural Event IR contract.
- `src_obsolete/build_model/` keeps the frozen build-model pipeline that was
  coupled to the structural Event IR.
- `src_v2/transpiler/event_ir.h` and `src_v2/transpiler/event_ir.c` now define
  the new canonical semantic Event IR.

Nothing under `src_obsolete/` should be extended as part of the active design.
