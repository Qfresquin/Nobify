# Tests Architecture

## Status
Canonical transition contract for the test stack.

## Role
The test system provides evidence that Nobify is converging toward `CMake 3.28`
artifact parity and makes ownership of regressions explicit.

## Product direction
Tests are part of the product contract: they should demonstrate that the
generated Nob output produces the same observable artifacts as the source
project.

## Current gap
Many fixtures, helpers, and suite names still frame evidence around a
supported-subset closure program. The real `cmake 3.28.x` oracle remains
aligned with the official baseline, but the surrounding narratives still need
migration toward total-parity wording.

## Guarantees
- Suites are organized by pipeline stage and evidence type.
- Regression ownership should be attributable to evaluator, Event IR,
  build-model, codegen, or end-to-end artifact behavior.
- Real-project and diff-based suites are the main evidence for parity claims.

## Non-goals
- Treating subset-style suite framing as the final product boundary.
- Using unit coverage alone as proof of artifact parity.

## Primary code
- `test_v2/test_semantic_pipeline.c`
- `test_v2/test_workspace.c`
- `test_v2/test_snapshot_support.c`
- `test_v2/test_manifest_support.c`

## Primary tests
- `test_v2/evaluator/`
- `test_v2/evaluator_diff/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/build_model/`
- `test_v2/codegen/`
- `test_v2/artifact_parity/`
- `test_v2/pipeline/`

## Evidence lanes
- Core suites: prove local invariants and data-shape rules.
- Diff suites: compare evaluator or downstream behavior against a real CMake
  oracle.
- Build-model and codegen suites: prove frozen-model and generated-backend
  correctness.
- Artifact-parity suites: prove end-to-end behavior on curated projects.

## Migration fronts
- Remove the stale `3.8` typo and subset-closure wording from suite purpose
  statements.
- Promote parity-oriented cases over subset-closure narratives.
- Expand artifact-parity coverage where diff suites still stand in as the only
  proof.
