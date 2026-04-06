# Codegen Docs

## Status

This directory contains the canonical generated-backend documentation for
Nobify.

As of April 6, 2026:

- `build_model` remains the only downstream semantic representation consumed by
  codegen
- generated `nob.c` behavior is no longer treated as an implementation detail
  once it becomes part of the product closure program
- the runtime CLI, phase rules, helper vocabulary, and rejection policy need a
  dedicated canonical home instead of being inferred from tests and roadmap
  notes

## Canonical Docs

- [Runtime contract](./codegen_runtime_contract.md)

## Relationship To Other Docs

- [`../build_model/README.md`](../build_model/README.md)
  Canonical build-model query and replay-domain contracts consumed by codegen.

- [`../evaluator_codegen_closure_roadmap.md`](../evaluator_codegen_closure_roadmap.md)
  Canonical multi-wave closure roadmap for the evaluator-to-codegen gap.

- [`../tests/evaluator_codegen_diff.md`](../tests/evaluator_codegen_diff.md)
  Canonical closure-harness contract that proves generated-backend status.
