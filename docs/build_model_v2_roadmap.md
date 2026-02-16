# Build Model v2 Roadmap

## 1. Roadmap Objective

Provide an execution roadmap for Build Model v2 until "satisfactory" status, with explicit dependency control for transpiler v2.

Reference date: 2026-02-15.

## 2. Rule Zero

No functional implementation in transpiler v2 planner/codegen starts before Build Model v2 readiness gate is green.

## 3. Milestones

### M0 - Contract Freeze (Week 0)

Goals:

1. contract docs approved
2. event taxonomy and schema v1 frozen at documentation level
3. diagnostics namespace reserved

Exit criteria:

1. doc review completed
2. no unresolved contract conflicts

### M1 - Type Foundation (Week 1)

Goals:

1. introduce canonical `Cmake_Event_*` family in headers
2. split mutable builder and frozen model types
3. compile-safe API declarations for builder/apply/validate/freeze/snapshot

Exit criteria:

1. headers compile without shim hacks
2. public API shape agreed and versioned

### M2 - Event Apply Core & Scope (Week 2)

Goals:

1. implement `build_model_apply_event` for Tier-1 core events
2. implement `EVENT_VAR_SET` and `EVENT_SCOPE_*` logic in Builder (Refinement A)
3. guarantee local transaction semantics (`arena_mark` + rollback on failure)
4. propagate event origin in all strict failures

Exit criteria:

1. event contract tests green
2. scope stack logic verified
3. deterministic diagnostics for event validation failures

### M3 - Validation Engine (Week 3)

Goals:

1. implement `build_model_validate`
2. enforce structural checks (missing target, conflicting redeclare, invalid visibility)
3. enforce dependency integrity checks

Exit criteria:

1. integrity test suite green
2. no missing diagnostic origin in strict mode

### M4 - Freeze + Read APIs (Week 4)

Goals:

1. implement deterministic `build_model_freeze`
2. expose read-only model APIs for codegen
3. block mutation after freeze
4. validate Read API ergonomics via `test_build_model_api_consumer.c` (Refinement D)

Exit criteria:

1. repeated freeze output is stable
2. post-freeze mutation attempts fail predictably
3. consumer API tests pass without accessing internal structs

### M5 - Snapshot v1 (Week 5)

Goals:

1. implement `build_model_snapshot_json`
2. emit required top-level schema fields
3. include diagnostics summary metadata

Exit criteria:

1. schema conformance tests green
2. deterministic snapshot tests green

### M6 - Readiness Certification (Week 6)

Goals:

1. pass all checks in `docs/build_model_v2_readiness_checklist.md`
2. close blocker-level gaps
3. publish readiness decision record

Exit criteria:

1. Build Model v2 marked "satisfactory"
2. transpiler v2 activation work is unblocked

## 4. Deliverable Matrix

| Milestone | Mandatory Artifact | Validation Source |
| --- | --- | --- |
| M0 | contract docs | docs review |
| M1 | type/API headers | compile checks |
| M2 | apply_event core + scope | contract unit tests |
| M3 | validation engine | integrity tests |
| M4 | freeze + read APIs | determinism + api consumer tests |
| M5 | snapshot v1 | schema + determinism tests |
| M6 | readiness record | checklist sign-off |

## 5. Ownership and Review Model

1. every milestone has one technical owner
2. every contract change requires one reviewer from transpiler side
3. every new event kind requires docs + tests in same PR

## 6. Blockers and Escalation

Blockers:

1. unresolved contract ambiguity
2. non-deterministic snapshot or freeze
3. missing origin in strict diagnostics
4. bypass of event boundary

Escalation policy:

1. blocker stops milestone closure
2. blocker must include root cause and mitigation PR plan

## 7. Definition of Done (Roadmap)

Roadmap is complete when:

1. M0-M6 are closed
2. readiness checklist is fully green
3. transpiler activation plan is approved for execution

## 8. Related Documents

1. `docs/build_model_v2_contract.md`
2. `docs/build_model_v2_transition_recommendations.md`
3. `docs/build_model_v2_readiness_checklist.md`
4. `docs/transpiler_v2_activation_plan.md`
