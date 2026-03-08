# Build Model

## Status

This directory contains the canonical documentation for the next build-model
implementation.

As of March 8, 2026:
- the canonical upstream contract is `src_v2/transpiler/event_ir.h`
- the canonical implementation target is `src_v2/build_model/`
- the operational build-model code still lives in `src_obsolete/build_model/`
- the `*_v2_spec.md` files in this directory are retained only as historical
  migration context

## Goals

- Rebuild the build model from scratch around the canonical `Event_Stream`.
- Preserve the good boundaries from the legacy implementation:
  `builder -> validate -> freeze -> query`.
- Replace monolithic mutable structs with a strict split between
  `Build_Model_Draft` and frozen `Build_Model`.
- Make the frozen model the only codegen-facing semantic representation.

## Non-Goals

- Achieving full parity with every legacy field in
  `src_obsolete/build_model/build_model_types.h`.
- Recreating evaluator-private compatibility state inside the build model.
- Inferring target dependency edges from textual `link_libraries(...)` payload.
- Adding a new "v3" naming layer. This documentation is the canonical baseline.

## Canonical Pipeline

The build-model pipeline is:

`Event_Stream -> Builder -> Validate -> Freeze -> Query`

- `Event_Stream`: emitted by the evaluator and owned by the transpiler layer.
- `Builder`: consumes build-semantic events and writes `Build_Model_Draft`.
- `Validate`: read-only semantic checks over the draft.
- `Freeze`: produces an immutable `Build_Model`.
- `Query`: the only read surface consumed by codegen and tooling.

## Canonical Documents

- [Architecture](./build_model_architecture.md)
- [Types](./build_model_types.md)
- [Builder](./build_model_builder.md)
- [Validate](./build_model_validate.md)
- [Freeze](./build_model_freeze.md)
- [Query](./build_model_query.md)
- [Migration](./build_model_migration.md)

## Historical Documents

These files are no longer the active contract:

- [Historical architecture notes](./build_model_architecture_v2.md)
- [Historical builder spec](./build_model_builder_v2_spec.md)
- [Historical validation spec](./build_model_validate_v2_spec.md)
- [Historical freeze spec](./build_model_freeze_v2_spec.md)
- [Historical query spec](./build_model_query_v2_spec.md)
- [Historical benchmark notes](./build_model_v2_benchmark_notes.md)

## Glossary

- `Build_Model_Draft`: mutable builder-owned semantic reconstruction state.
- `Build_Model`: immutable frozen model consumed by query/codegen.
- `BM_Builder`: the only writer of `Build_Model_Draft`.
- `Raw state`: exactly what the builder reconstructed from supported semantic
  events, plus opaque future-facing property bags where needed.
- `Effective state`: inherited or transitive views computed by query helpers
  over the frozen model.
