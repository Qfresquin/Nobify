# Build Model Query

## Status
Canonical transition contract for the read-only build-model surface.

## Role
The query layer exposes frozen build-model facts to codegen and test tools.

## Product direction
Queries should be simple readers over a graph that already carries enough
semantics for `CMake 3.28` artifact parity.

## Current gap
Some effective accessors still compensate for missing typed edges or for list
and generator-expression payloads that arrived too lossy from upstream. That is
temporary debt, especially around target-usage reconstruction.

## Guarantees
- Raw accessors return stored frozen facts.
- Effective accessors may compose frozen data, but they must not become a
  hidden evaluator.
- Query sessions may cache results for performance, not to invent semantics.

## Non-goals
- Name-based dependency inference as the final design.
- Reinterpreting arbitrary string payloads until something works.

## Primary code
- `src_v2/build_model/build_model_query.h`
- `src_v2/build_model/build_model_query.c`
- `src_v2/build_model/build_model_query_effective.c`
- `src_v2/build_model/build_model_query_session.c`

## Primary tests
- `test_v2/build_model/test_build_model_v2_suite.c`
- `test_v2/codegen/test_codegen_v2_suite_build.c`
- `test_v2/evaluator_codegen_diff/`

## Query rules
- Prefer explicit stored relations over derived text parsing.
- When an effective answer is needed, derive it from frozen typed facts first.
- Any query behavior that depends on lossy upstream payloads should be treated
  as a bug to remove, not as a feature to expand.
