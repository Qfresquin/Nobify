# Transpiler v2 Spec (Dependent on Build Model v2)

## 1. Document Role

Status: `normative for transpiler v2`, subordinate to Build Model v2 contract.

Precedence:

1. `docs/build_model_v2_contract.md` (highest)
2. `docs/transpiler_v2_spec.md`
3. `docs/build_model_v2_transition_recommendations.md`

## 2. Goal

Define transpiler v2 behavior and release gates while preserving strict architectural dependency on Build Model v2.

Official pipeline target:

`AST normalized -> Event IR -> Builder -> Validate -> Freeze -> Codegen -> Snapshot`.

## 3. Scope and Non-Scope

In scope:

1. semantic-to-event boundary contract
2. planner integration with Build Model v2 APIs
3. codegen consumption of frozen model only
4. compatibility profile and validation policy handshake
5. fallback policy and activation gates

Out of scope during Build Model readiness period:

1. functional planner implementation in transpiler v2
2. functional v2 codegen implementation
3. feature parity acceleration before model contract is certified

## 4. Hard Dependency Policy (Mandatory)

Until `Build Model v2 = satisfactory`:

1. functional implementation in transpiler v2 planner is blocked
2. functional implementation in transpiler v2 codegen is blocked
3. transpiler v2 runtime path must remain fallback-first to legacy

Allowed work in this period:

1. type/interface definitions
2. architecture guard tests
3. diagnostic plumbing and telemetry
4. documentation and migration scaffolding

Forbidden work in this period:

1. event-to-model mutation logic inside transpiler v2
2. new functional codegen behavior tied to incomplete model APIs
3. removing fallback-first behavior before readiness gate

## 5. Canonical Types and Boundary

Canonical event types (target):

1. `Cmake_Event_Kind`
2. `Cmake_Event_Value_Type`
3. `Cmake_Event_Field`
4. `Cmake_Event_Origin`
5. `Cmake_Event`
6. `Cmake_Event_Stream`

Boundary rules:

1. semantic/evaluator emits canonical events only
2. planner consumes events only
3. planner does not read AST directly
4. every model-related error must carry event origin

## 6. Entrypoints

Current v2 entrypoint:

1. `transpile_datree_v2(Ast_Root, String_Builder*, const Transpiler_Run_Options*, const Transpiler_Compat_Profile*)`

Required behavior by readiness state:

1. pre-readiness: fallback-first execution path is required
2. post-readiness: phased activation of native v2 planner/codegen is allowed

## 7. Build Model API Dependency (Required)

Transpiler v2 may rely functionally on Build Model only after readiness gate, and only through:

1. `build_model_v2_builder_create`
2. `build_model_v2_apply_event`
3. `build_model_v2_validate`
4. `build_model_v2_freeze`
5. `build_model_v2_snapshot_json`
6. frozen read APIs (`build_model_v2_get_*`)

## 8. Compatibility and Validation Handshake

1. `Transpiler_Compat_Profile` defines accepted semantic behavior.
2. `Build_Model_v2_Validation_Mode` defines strictness severity.
3. profile vs validation conflicts must emit explicit diagnostics.

Defaults:

1. profile default: `CMAKE_3_X`
2. validation default: `BM_V2_VALIDATION_STRICT`

## 9. Diagnostics Contract for Transpiler-BM Integration

Every propagated diagnostic must include:

1. stable `code`
2. `severity`
3. `message`
4. `origin`
5. `event_kind` when available

Required outcome:

1. deterministic diffability across repeated runs
2. stable triage for legacy vs v2 divergences

## 10. Snapshot and Diff Requirements

1. snapshot must be generated from frozen model only
2. same input must produce identical snapshot output
3. differential tests compare fallback/legacy outputs against v2 outputs

Primary usage:

1. regression gates
2. divergence analysis
3. release readiness evidence

## 11. Activation Gates (Before Functional Transpiler Work)

All items below are blockers and must be green:

1. `docs/build_model_v2_readiness_checklist.md` fully passed
2. Build Model v2 contract tests green
3. deterministic freeze/snapshot tests green
4. architecture guard tests green

Only after this gate can transpiler v2 functional implementation start.

## 12. Post-Gate Activation Order

After readiness gate is green:

1. activate planner for Tier-1 commands while keeping fallback switch
2. activate freeze + read-only codegen path for Tier-1
3. expand by tier with diff-based approvals
4. disable fallback by default only after accepted divergence budget is zero

Detailed execution lives in `docs/transpiler_v2_activation_plan.md`.

## 13. Required Test Gates

1. architecture gate: no semantic direct mutation
2. pipeline gate: `events -> planner -> validate -> freeze -> codegen`
3. diff gate: no unapproved divergence
4. deterministic gate: stable diagnostics and snapshots

## 14. References

1. `docs/build_model_v2_contract.md`
2. `docs/build_model_v2_transition_recommendations.md`
3. `docs/build_model_v2_roadmap.md`
4. `docs/build_model_v2_readiness_checklist.md`
5. `docs/transpiler_v2_activation_plan.md`
