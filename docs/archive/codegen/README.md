# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Codegen Docs

## Status

This directory is the canonical documentation map for the generated backend.

Codegen boundary:

`Build_Model (query only) -> generated nob.c runtime`

The code generator does not consume evaluator internals and does not consume
raw Event IR directly.

## Overview

Codegen owns generated runtime shape, phase command behavior, helper emission,
tool-resolution policy, and explicit rejection behavior for unsupported
variants.

## Normative Contract

- [Runtime contract](./codegen_runtime_contract.md)
- [Supported subset](./generated_backend_supported_subset.md)

## Dependencies

- [Build model contracts](../build_model/README.md)
- [Closure roadmap](../evaluator_codegen_closure_roadmap.md)
- [Closure harness contract](../tests/evaluator_codegen_diff.md)
