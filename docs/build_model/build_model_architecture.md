# Build Model Architecture

## Status
Canonical transition contract for the frozen build graph.

## Role
The build model turns Event IR into a validated, frozen, queryable graph that
codegen can use to reproduce source-project artifacts.

## Product direction
Within the `CMake 3.28 -> Nob` pipeline, the build model exists to preserve
artifact-relevant semantics in explicit data structures rather than in late
heuristics.

## Current gap
The supported `target`/`directory`/`install`/`export`/`replay` genex path now
resolves downstream through query context and frozen data. Remaining debt is
mostly limited to intentional raw-property boundaries and adjacent rows such as
broader toolchain, dependency-materialization, and custom-command breadth.

## Guarantees
- `builder` records upstream facts and domain entities.
- `validate` rejects structurally broken or semantically impossible graphs.
- `freeze` produces an immutable model for downstream use.
- `query` exposes stored or pre-frozen semantics without becoming a second
  semantic engine.

## Non-goals
- Inventing missing semantics after freeze.
- Hiding evaluator or Event IR lossiness behind increasingly clever queries.

## Primary code
- `src_v2/build_model/build_model_builder.c`
- `src_v2/build_model/build_model_validate.c`
- `src_v2/build_model/build_model_freeze.c`
- `src_v2/build_model/build_model_query.c`
- `src_v2/build_model/build_model_query_effective.c`

## Primary tests
- `test_v2/build_model/`
- `test_v2/pipeline/`
- `test_v2/codegen/test_codegen_v2_suite_build.c`
- `test_v2/artifact_parity/`

## Phase boundary
Event IR in:
- ordered evaluator facts

Frozen model out:
- explicit project, directory, target, replay, install, package, and test data
  ready for deterministic queries
