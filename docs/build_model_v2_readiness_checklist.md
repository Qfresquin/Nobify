# Build Model v2 Readiness Checklist

## 1. Purpose

Define objective criteria for declaring Build Model v2 "satisfactory".

If any blocker item is not satisfied, status remains "not ready".

## 2. Status Model

1. `PASS`: criterion fully met and validated
2. `FAIL`: criterion not met
3. `N/A`: only allowed with written rationale and approval

## 3. Contract Completeness

1. [ ] Canonical event family (`Cmake_Event_*`) declared in public v2 headers.
2. [ ] Event kind minimum set is implemented for Tier-1 (includes `VAR_SET`, `SCOPE_*`).
3. [ ] Required/optional/type constraints exist for implemented event kinds.
4. [ ] Unknown field policy is enforced for strict and compat modes.

## 4. API Completeness

1. [ ] `build_model_builder_create` exists and is covered by tests.
2. [ ] `build_model_apply_event` exists and is covered by tests.
3. [ ] `build_model_validate` exists and is covered by tests.
4. [ ] `build_model_freeze` exists and is covered by tests.
5. [ ] `build_model_snapshot_json` exists and is covered by tests.
6. [ ] read-only accessor family (`build_model_get_*`) exists for codegen consumers.
7. [ ] API Consumer Test (`test/test_build_model_api_consumer.c`) passes and validates ergonomics (Refinement D).

## 5. Validation and Integrity

1. [ ] Missing required fields trigger deterministic errors.
2. [ ] Invalid field types trigger deterministic errors.
3. [ ] Target-scoped operations on missing targets fail.
4. [ ] Conflicting redeclarations fail.
5. [ ] Dependency cycle detection is active.
6. [ ] Scope integrity (unbalanced scopes) is validated.
7. [ ] Validation does not mutate builder state.

## 6. Freeze and Determinism

1. [ ] Freeze output ordering is deterministic.
2. [ ] Post-freeze mutation is blocked.
3. [ ] Repeated freeze on same input produces equivalent frozen data.
4. [ ] Freeze behavior does not rely on non-deterministic container iteration.

## 7. Snapshot Contract

1. [ ] Snapshot JSON schema v1 is emitted.
2. [ ] Required top-level fields are always present.
3. [ ] Same input generates byte-identical snapshot output.
4. [ ] Schema version policy is enforced for incompatible changes.

## 8. Diagnostics and Origin Contract

1. [ ] Every strict error includes stable diagnostic code.
2. [ ] Every strict error includes origin.
3. [ ] Frozen model retains granular origin for list items (sources, flags) (Refinement C).
4. [ ] Event-kind context is present when applicable.
5. [ ] Diagnostic messages are deterministic enough for diff-based assertions.

## 9. Architecture Guards

1. [ ] semantic/evaluator v2 does not mutate Build Model directly.
2. [ ] planner consumes events only.
3. [ ] codegen consumes frozen model only.
4. [ ] pipeline gate (`events -> planner -> validate -> freeze -> codegen`) is test-covered.

## 10. Test and CI Gates

1. [ ] Build Model v2 contract suites are green in CI.
2. [ ] Determinism suites are green in CI.
3. [ ] Architecture guard suites are green in CI.
4. [ ] No blocker-level unresolved divergence in approved test corpus.

## 11. Release Decision Record

To mark readiness as PASS, attach:

1. commit/hash or release candidate identifier
2. checklist filled with evidence links
3. sign-off from Build Model owner and Transpiler owner

## 12. Gate Result

Final gate outcome (fill during release review):

1. Build Model v2 readiness: `PASS | FAIL`
2. Date:
3. Approved by:
4. Notes:
--- END OF FILE docs/build_model_v2_readiness_checklist.md ---