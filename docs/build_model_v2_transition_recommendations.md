# Build Model v2 Transition Recommendations (Operational)

## 1. Purpose

Provide an execution path from current code to the Build Model v2 contract without functional regression.

This document is operational. Normative authority is `docs/build_model_v2_contract.md`.

## 2. Current Baseline (Repository Reality)

Observed in codebase today:

1. `src/build_model/build_model_types.h` still exposes a single mutable `Build_Model` shape.
2. `src/build_model/build_model.c` implements early APIs (`build_model_create`, `build_model_set_project`, `build_model_add_target`).
3. `src/transpiler/transpiler.c` is scaffold-first and fallback-first to legacy bridge.

Implication: contract-level types and planner boundary are not yet complete.

## 3. Mandatory Strategy

Order is fixed:

1. complete Build Model v2 contract implementation first
2. certify Build Model v2 as "satisfactory"
3. only then start functional implementation in transpiler v2 planner/codegen

Any inverse order creates rework risk because transpiler semantics depend on finalized model invariants.

## 4. Transition Phases

### Phase 0 - Contract and type freeze

Deliverables:

1. canonical event type family declared in `src/transpiler/transpiler_types.h`
2. builder/frozen split declared in `src/build_model/build_model_types.h`
3. diagnostic code map reserved (`BM_E*`, `BM_W*`)
4. snapshot schema v1 fields frozen in docs

Exit criteria:

1. headers compile
2. no unresolved naming conflicts with current public APIs
3. contract docs approved

### Phase 1 - Builder kernel

Deliverables:

1. `build_model_builder_create`
2. `build_model_apply_event` for Tier-1 minimum events
3. local transactional apply using `arena_mark`/`arena_rewind`
4. deterministic error reporting with origin propagation

Exit criteria:

1. contract tests for missing field/type mismatch/unknown field are green
2. no direct AST dependency inside planner layer

### Phase 2 - Validation and integrity

Deliverables:

1. `build_model_validate`
2. strict checks for target existence/redeclaration conflicts/invalid visibility
3. cycle detection for target graph (at validate or freeze boundary)

Exit criteria:

1. integrity test matrix is green
2. diagnostics carry stable code + origin for all strict failures

### Phase 3 - Freeze and read-only API

Deliverables:

1. `build_model_freeze`
2. deterministic ordering for exported collections
3. `build_model_get_*` read APIs for codegen consumers
4. mutation lock after freeze

Exit criteria:

1. repeatability tests confirm deterministic freeze output
2. no mutation path remains reachable after freeze

### Phase 4 - Snapshot contract

Deliverables:

1. `build_model_snapshot_json`
2. schema v1 emitter with required top-level fields
3. diagnostics summary in snapshot metadata

Exit criteria:

1. same input generates byte-identical snapshot output
2. schema version and compatibility policy checks are green

### Phase 5 - Build Model readiness certification

Deliverables:

1. readiness checklist fully green
2. architecture guard tests green
3. approved divergence list documented

Exit criteria:

1. Build Model v2 marked "satisfactory"
2. transpiler v2 implementation freeze can be lifted

## 5. API Migration Map (Current -> Target)

Current API family to preserve short-term compatibility:

1. `build_model_create`
2. `build_model_set_project`
3. `build_model_add_target`
4. `build_model_register_find_package_result`

Target API family for contract compliance:

1. `build_model_builder_create`
2. `build_model_apply_event`
3. `build_model_validate`
4. `build_model_freeze`
5. `build_model_snapshot_json`

Recommendation:

1. keep compatibility wrappers temporarily where needed
2. route new behavior only through builder/event pipeline
3. remove wrappers only after transpiler v2 activation phase stabilizes

## 6. Test Recommendations

Minimum tests per phase:

1. event contract tests (required fields, type mismatch, unknown field policy)
2. model integrity tests (missing target references, redeclaration conflicts)
3. dependency cycle tests
4. freeze determinism tests (repeat N times, identical output)
5. snapshot schema tests (required fields and version policy)

Suggested files:

1. `test/test_build_model_contract.c`
2. `test/test_build_model_validate.c`
3. `test/test_build_model_freeze.c`
4. `test/test_build_model_snapshot.c`

## 7. CI Gates Recommendation

Merge should be blocked when any condition below fails:

1. Build Model v2 unit/contract suites
2. architecture guard suite (`semantic -> events -> planner -> validate -> freeze -> codegen`)
3. deterministic snapshot check
4. no new undocumented diagnostic codes

## 8. Risk Register and Mitigation

1. Risk: event taxonomy grows without governance.
Mitigation: central event table with required/optional/type constraints in one source file.

2. Risk: freeze output ordering drifts over time.
Mitigation: stable sorting strategy + repeatability tests in CI.

3. Risk: transpiler work starts before model contract is stable.
Mitigation: hard policy in `docs/transpiler_v2_spec.md` + readiness checklist gate.

4. Risk: legacy fallback becomes permanent.
Mitigation: fallback usage telemetry and explicit removal milestone.

## 9. Practical Team Workflow

1. Keep PR scope single-phase when possible.
2. Require docs update in same PR for any contract change.
3. Require at least one contract-level test per new event kind.
4. Record accepted divergence with rationale and expiry milestone.

## 10. Related Documents

1. `docs/build_model_v2_contract.md`
2. `docs/build_model_v2_roadmap.md`
3. `docs/build_model_v2_readiness_checklist.md`
4. `docs/transpiler_v2_spec.md`
5. `docs/transpiler_v2_activation_plan.md`
