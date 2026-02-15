# Build Model v2 Contract (Normative)

## 1. Status and Authority

Status: `normative`.

This document defines the mandatory contract for Build Model v2. In case of conflict:

1. `docs/build_model_v2_contract.md` prevails over any other v2 doc.
2. `docs/build_model_v2_transition_recommendations.md` is operational guidance, not authority.
3. `docs/transpiler_v2_spec.md` must conform to this contract.

## 2. Objective

Define a deterministic, testable, and decoupled pipeline:

`AST normalized -> Event IR -> Builder -> Validate -> Freeze -> Codegen -> Snapshot`.

Main reason: avoid v1 coupling where semantic/evaluator knows internals of the build model.

## 3. Non-Negotiable Principles

1. Single semantic boundary: planner consumes only canonical events.
2. Mutation is allowed only in `Build_Model_v2_Builder`.
3. Read-only access is allowed only on frozen `Build_Model_v2`.
4. Validation is strict by default.
5. Snapshot JSON is an official contract artifact.
6. Diagnostics must be deterministic: stable code + stable origin + stable message class.

## 4. Scope v2

Initial schema coverage (`schema_version = 1`):

1. `project`
2. `targets` (sources, includes, definitions, link libs, deps, properties, conditionals)
3. `find` / `find_package`
4. `install`
5. `tests`
6. `cpack`
7. `custom_commands`

Out of scope in this phase:

1. Full historical parity for all CMake quirks.
2. Bit-for-bit parity of legacy messages/side effects.
3. Full emulation of all CMake generators.

## 5. Canonical Semantic Boundary

Canonical event family (to be hosted in `src/transpiler_v2/transpiler_v2_types.h`):

1. `Cmake_Event_Kind`
2. `Cmake_Event_Value_Type` (`BOOL`, `INT`, `STRING`, `STRING_LIST`)
3. `Cmake_Event_Field`
4. `Cmake_Event_Origin` (`command_name`, `node_ref`, `source_path`, `line`, `column`)
5. `Cmake_Event`
6. `Cmake_Event_Stream`

Boundary rules:

1. semantic/evaluator v2 must not call `build_model_v2_*` mutation APIs directly.
2. planner must not depend on AST nodes; only on `Cmake_Event`.
3. every planner validation error must include `origin` from the source event.

## 6. Minimum Event Set and Field Contract

Minimum event kinds:

1. `EVENT_PROJECT_SET`
2. `EVENT_TARGET_DECLARE`
3. `EVENT_TARGET_ADD_SOURCE`
4. `EVENT_TARGET_SET_PROPERTY`
5. `EVENT_TARGET_LINK_LIB`
6. `EVENT_TARGET_LINK_TARGET`
7. `EVENT_FIND_PACKAGE_RESULT`
8. `EVENT_INSTALL_RULE_ADD`
9. `EVENT_TEST_ADD`
10. `EVENT_CPACK_COMPONENT_SET`
11. `EVENT_CUSTOM_COMMAND_ADD`

Each `Event_Kind` must define:

1. required fields
2. optional fields
3. expected type for each field
4. semantic constraints (enum, uniqueness, cardinality)

Unknown fields policy:

1. `STRICT`: error
2. `COMPAT`: warning (with migration path to future error)

## 7. Builder/Frozen Data Model

Target public model split in `src/build_model_v2/build_model_v2_types.h`:

1. `Build_Model_v2_Builder` (mutable, incremental)
2. `Build_Model_v2` (frozen, immutable)

Target API contract:

1. `Build_Model_v2_Builder *build_model_v2_builder_create(Arena *arena, Build_Model_v2_Options opts);`
2. `bool build_model_v2_apply_event(Build_Model_v2_Builder *b, const Cmake_Event *ev, Diag_Sink *diag);`
3. `bool build_model_v2_validate(const Build_Model_v2_Builder *b, Diag_Sink *diag);`
4. `const Build_Model_v2 *build_model_v2_freeze(Build_Model_v2_Builder *b, Arena *out_arena, Diag_Sink *diag);`
5. `String_View build_model_v2_snapshot_json(const Build_Model_v2 *m, Arena *arena);`
6. `build_model_v2_get_*` read-only accessors for codegen.

Execution rules:

1. `apply_event` never performs freeze.
2. `validate` is side-effect free on builder state.
3. `freeze` must be deterministic and independent from non-deterministic iteration order.
4. codegen accepts only `const Build_Model_v2 *`.

## 8. Memory and Ownership Policy

1. Single arena per run for model lifetime.
2. `String_View` at boundaries; persistent ownership must copy to arena.
3. any pointer from `Builder` becomes invalid for use after `freeze`.
4. snapshot memory belongs to the export arena provided to snapshot API.

## 9. Validation Policy

Validation modes:

1. `BM_V2_VALIDATION_STRICT` (default)
2. `BM_V2_VALIDATION_COMPAT`

Minimum strict checks:

1. target-scoped operation on missing target -> error
2. invalid target redeclaration/type conflict -> error
3. invalid visibility/config -> error
4. missing required event field -> error
5. invalid field type -> error
6. structural conflicts in install/test/cpack/custom command domains -> error
7. dependency cycle -> error (detected no later than `freeze`)

## 10. Diagnostics Contract

Each diagnostic must include:

1. stable code (`BMV2_EXXXX` for error, `BMV2_WXXXX` for warning)
2. severity
3. message
4. origin
5. `event_kind` when applicable

Diagnostic code ranges (reserved):

1. `BMV2_E1000-BMV2_E1999`: event contract violations
2. `BMV2_E2000-BMV2_E2999`: target/model integrity
3. `BMV2_E3000-BMV2_E3999`: freeze/order/dependency integrity
4. `BMV2_E4000-BMV2_E4999`: snapshot/export errors
5. `BMV2_W1000-BMV2_W1999`: compat/deprecation warnings

## 11. Frozen Invariants

Frozen `Build_Model_v2` must guarantee:

1. unique identifiers in each relevant domain (for example target name)
2. resolved internal references
3. deterministic ordering of exported collections
4. no unresolved dependency cycles
5. no partial/pending records

## 12. Official Snapshot Contract

API:

1. `String_View build_model_v2_snapshot_json(const Build_Model_v2 *m, Arena *arena);`

Schema v1 top-level fields:

1. `meta` (`schema_version`, `compat_profile`, `validation_mode`, `generated_at`)
2. `project`
3. `targets`
4. `find_packages`
5. `install_rules`
6. `tests`
7. `cpack`
8. `custom_commands`
9. `diagnostics_summary`

Rules:

1. deterministic serialization for same input
2. all required schema fields always present in v1
3. incompatible schema changes require `schema_version++`
4. compatible additions must be optional and documented

## 13. Mandatory Gates for Build Model v2 "Satisfactory" Status

A Build Model v2 is considered satisfactory only when all gates below are green:

1. API gate: all builder/validate/freeze/snapshot APIs exist and are tested.
2. Event contract gate: required/optional/type checks are enforced deterministically.
3. Model integrity gate: redeclaration, missing target references, and cycles are validated.
4. Freeze gate: deterministic output ordering proven by repeatability tests.
5. Snapshot gate: schema v1 emitted and validated; deterministic across repeated runs.
6. Architecture gate: no direct semantic mutation of model internals.

Detailed checklist lives in `docs/build_model_v2_readiness_checklist.md`.

## 14. Hard Dependency Policy for Transpiler v2

Until Build Model v2 reaches "satisfactory" status:

1. no functional implementation in transpiler v2 planner/codegen is allowed.
2. transpiler v2 can only maintain scaffolding, interfaces, and guard tests.
3. legacy fallback remains the execution path.

Reason: transpiler v2 semantics depend on final Build Model v2 contract and invariants.

## 15. References

1. `docs/build_model_v2_transition_recommendations.md`
2. `docs/build_model_v2_roadmap.md`
3. `docs/build_model_v2_readiness_checklist.md`
4. `docs/transpiler_v2_spec.md`
