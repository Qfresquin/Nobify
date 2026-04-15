# Transpiler Docs

## Status

This directory contains the canonical Event IR contract used as the boundary
between evaluator execution and downstream reconstruction.

Boundary:

`evaluator committed semantics -> Event_Stream -> build_model ingest`

## Overview

Event IR is a stable, typed, append-only stream contract. It is not a
build-model-specific private format and it is not a codegen input surface.

Downstream consumers must respect role metadata and ordering guarantees defined
by the normative Event IR spec.

## Normative Contract

- [Event IR v2 spec](./event_ir_v2_spec.md)

## Control Matrix

- [Event IR coverage matrix](./event_ir_coverage_matrix.md)

## Active Change Roadmap

- [Event IR incremental roadmap](./event_ir_closure_roadmap.md)

## Dependencies

- Upstream producer contract:
  [`../evaluator/evaluator_event_ir_contract.md`](../evaluator/evaluator_event_ir_contract.md)
- Downstream consumer contract:
  [`../build_model/README.md`](../build_model/README.md)
- Separate downstream closure program:
  [`../evaluator_codegen_closure_roadmap.md`](../evaluator_codegen_closure_roadmap.md)
