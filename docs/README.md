# Nobify

## Status
Canonical overview. Transition contract for the `CMake 3.28 parity total -> Nob`
documentation reset.

## Role
This file states the product direction and points to the only active documents
that define the project today.

## Product direction
Nobify is documented as a transpiler from `CMake 3.28` to Nob with the same
observable artifacts. Internal simplification is allowed only when generated
build outputs, test behavior, install results, and package-visible artifacts
stay equivalent to the source project.

## Current gap
The codebase, tests, and older documents still contain some mistaken
`CMake 3.8` references and `supported subset` language. Those references
describe documentation drift and transition debt, not the official product
contract.

## Guarantees
- Active docs describe the `CMake 3.28` parity target first.
- Known implementation gaps are called out explicitly as short transition notes.
- `docs/archive/` preserves history but does not define current behavior.

## Non-goals
- Claiming that the current implementation already achieves full parity.
- Keeping legacy planning, historical analysis, and tracking-matrix material in
  the active reading path.

## Primary code
- `src_v2/evaluator/`
- `src_v2/transpiler/event_ir.h`
- `src_v2/build_model/`
- `src_v2/codegen/`

## Primary tests
- `test_v2/evaluator/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/build_model/`
- `test_v2/codegen/`
- `test_v2/artifact_parity/`

## Canonical docs
- [Project priorities](project_priorities.md)
- [Pipeline overview](architecture/pipeline_overview.md)
- [Evaluator v2 spec](evaluator/evaluator_v2_spec.md)
- [Evaluator to Event IR contract](evaluator/evaluator_event_ir_contract.md)
- [Build model architecture](build_model/build_model_architecture.md)
- [Build model query](build_model/build_model_query.md)
- [Build model replay](build_model/build_model_replay.md)
- [Codegen runtime contract](codegen/codegen_runtime_contract.md)
- [Current backend closure within the parity goal](codegen/generated_backend_supported_subset.md)
- [Tests architecture](tests/tests_architecture.md)
- [Evaluator to codegen diff](tests/evaluator_codegen_diff.md)
- [Archive policy](archive/README.md)
