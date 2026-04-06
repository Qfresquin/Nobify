# Phase Runtime Map

## Status

Canonical map of runtime phase semantics across build-model replay and generated
backend execution.

## Role

Explain which phase owns which behavior so new support work lands in the right
domain and tests.

## Boundary

Phase behavior is driven by frozen build-model records and query APIs, not by
re-reading evaluator internals or raw Event IR at runtime.

## Data Flow

1. Evaluator commits semantic effects and projects downstream-consumable events.
2. Build model freezes phase-owned domains (`build`, `install`, `export`,
   `package`) plus replay actions with explicit `BM_Replay_Phase`.
3. Codegen emits phase entrypoints and helpers.
4. Generated runtime executes phases with explicit ownership and rejection
   policy.

## Inputs

- Build-model install/export/package records
- Build-model replay actions (`configure/build/test/install/export/package/host-only`)
- Runtime CLI phase invocation and options

## Outputs

- phase-specific side effects (configure materialization, build outputs, test
  runs, install trees, export artifacts, package archives)
- stable reject diagnostics for unsupported variants

## Who Consumes

- Codegen implementers adding new replay-action support
- Closure-harness maintainers classifying phase ownership
- Release maintainers validating supported subset boundaries

## Key Source Regions

- `docs/build_model/build_model_replay.md` (contract docs)
- `src_v2/build_model/build_model_query.*`
- `src_v2/codegen/nob_codegen_runtime.c`
- `src_v2/codegen/nob_codegen_install.c`
- `src_v2/codegen/nob_codegen_export.c`
- `src_v2/codegen/nob_codegen_package.c`
- `test_v2/codegen/`
- `test_v2/evaluator_codegen_diff/`
- `test_v2/artifact_parity/`

## Failure Ownership

- Invalid phase-domain reconstruction: build-model ingest/freeze validation
- Missing phase command behavior in generated backend: codegen/runtime contract
- Tooling absence on host: suite-level `skip-by-tool`
- Unsupported phase variant inside supported families: explicit
  `backend-reject` or justified `explicit-non-goal`
