# Build Model

## Status

This directory is the canonical build-model documentation map.

Pipeline boundary:

`Event_Stream -> Builder -> Validate -> Freeze -> Query`

Codegen consumes only query-facing `Build_Model` semantics. It does not consume
raw Event IR directly.

## Overview

The build model is the downstream semantic representation between evaluator
projection and generated backend execution. It owns reconstruction stability,
typed IDs, validation, freeze-time integrity, and query APIs.

## Normative Contracts

- [Architecture](./build_model_architecture.md)
- [Types](./build_model_types.md)
- [Builder](./build_model_builder.md)
- [Validate](./build_model_validate.md)
- [Freeze](./build_model_freeze.md)
- [Query](./build_model_query.md)
- [Replay domain](./build_model_replay.md)

These contracts define active implementation behavior for `src_v2/build_model`.

## History

Historical migration and superseded v2 specs are preserved in archive:

- [Migration record](../archive/build_model/build_model_migration.md)
- [Historical architecture notes](../archive/build_model/build_model_architecture_v2.md)
- [Historical builder spec](../archive/build_model/build_model_builder_v2_spec.md)
- [Historical validation spec](../archive/build_model/build_model_validate_v2_spec.md)
- [Historical freeze spec](../archive/build_model/build_model_freeze_v2_spec.md)
- [Historical query spec](../archive/build_model/build_model_query_v2_spec.md)
- [Historical benchmark notes](../archive/build_model/build_model_v2_benchmark_notes.md)

## Dependencies

- Upstream: [Evaluator docs](../evaluator/README.md),
  [Event IR spec](../transpiler/event_ir_v2_spec.md)
- Downstream: [Codegen docs](../codegen/README.md)
- Shared terms: [Glossary](../glossary.md)
