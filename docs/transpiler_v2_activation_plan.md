# Transpiler v2 Activation Plan (Post Build Model Readiness)

## 1. Activation Precondition

This plan starts only after `docs/build_model_v2_readiness_checklist.md` is fully PASS.

Before that, transpiler v2 remains implementation-frozen for planner/codegen logic.

## 2. Objective

Enable functional transpiler v2 in controlled phases with measurable regression gates.

## 3. Activation Sequence

### T0 - Enable planner skeleton with real model API calls

Scope:

1. wire `build_model_v2_builder_create`
2. wire `build_model_v2_apply_event`
3. wire `build_model_v2_validate`
4. wire `build_model_v2_freeze`

Rules:

1. fallback switch remains enabled
2. no feature expansion outside Tier-1 command families

Exit criteria:

1. pipeline executes end-to-end behind gate
2. architecture tests remain green

### T1 - Tier-1 functional parity

Scope:

1. `project`
2. `add_executable` / `add_library`
3. primary `target_*` operations

Rules:

1. each command family requires snapshot and diff coverage
2. unapproved divergences block merge

Exit criteria:

1. Tier-1 differential suite green
2. no blocker diagnostics without origin

### T2 - Tier-2 expansion

Scope:

1. `include`, `find_*`, `find_package`
2. `install`, `add_test`, `enable_testing`
3. common checks/probes

Exit criteria:

1. Tier-2 differential suite green
2. accepted divergence list updated and approved

### T3 - Tier-3 expansion

Scope:

1. `custom_command`
2. `cpack`
3. lower-frequency advanced semantics

Exit criteria:

1. Tier-3 suite green
2. no unresolved blocker divergence

### T4 - Fallback drawdown

Scope:

1. disable fallback by default
2. keep emergency guard compile-available for one release cycle
3. collect telemetry on fallback usage

Exit criteria:

1. fallback usage equals zero in official corpus
2. rollback plan documented

## 4. Feature Flags Recommendation

Recommended flags:

1. `NOBIFY_TV2_ENABLE_PIPELINE`
2. `NOBIFY_TV2_ENABLE_TIER1`
3. `NOBIFY_TV2_ENABLE_TIER2`
4. `NOBIFY_TV2_ENABLE_TIER3`
5. `NOBIFY_TV2_FORCE_LEGACY_FALLBACK`

Use flags to isolate regressions quickly and preserve release safety.

## 5. Test Strategy During Activation

1. keep `test/test_transpiler_v2_diff.c` mandatory on all activation PRs
2. add tier-specific diff/snapshot suites incrementally
3. block merge on non-deterministic snapshot output
4. block merge on architecture guard violations

## 6. Rollback Policy

1. rollback trigger: blocker regressions in official corpus
2. immediate action: enable fallback-first mode
3. follow-up: open incident with root cause and corrective milestone

## 7. Completion Criteria

Activation plan is complete when:

1. v2 path is default and stable
2. fallback is disabled by default
3. differential gates are green with no blocker divergence
4. one release cycle passes without emergency fallback usage

## 8. Related Documents

1. `docs/build_model_v2_contract.md`
2. `docs/build_model_v2_roadmap.md`
3. `docs/build_model_v2_readiness_checklist.md`
4. `docs/transpiler_v2_spec.md`
