# Build Model Validate (Normative)

## 1. Role

Validation is the semantic gate between draft reconstruction and freeze.

It is read-only over `Build_Model_Draft`.

Validation exists to protect correctness of the reconstructed CMake 3.28
semantic baseline before freeze and any downstream Nob optimization.

## 2. Public API

```c
bool bm_validate_draft(const Build_Model_Draft *draft,
                       Arena *scratch,
                       Diag_Sink *sink);
```

Return rules:
- `true`: no error-level issues were found
- `false`: one or more error-level issues were found

Warnings never change the return value.

## 3. Validation Passes

Validation runs in this fixed order:

1. `structural`
2. `resolution`
3. `cycles`
4. `semantic`

All passes may emit diagnostics. None may mutate the draft.

## 4. Structural Pass

The structural pass checks invariants that must hold regardless of semantic
interpretation.

Required checks:
- root directory exists if any build-semantic entity exists
- every directory parent chain is valid
- every entity stores valid `owner_directory_id`
- every target has a non-empty name and valid `BM_Target_Kind`
- install/package/CPack/test records have required fields populated
- no duplicate IDs or duplicate target names remain in the finalized draft

## 5. Resolution Pass

The resolution pass verifies that every symbolic reference can be resolved.

Required checks:
- target dependency references resolve to declared targets
- alias targets reference declared targets
- `BM_INSTALL_RULE_TARGET` items resolve to declared targets
- CPack component `group` references resolve to component groups when non-empty
- CPack component dependency references resolve to declared components
- CPack component install-type references resolve to declared install types

This pass validates resolvability only. It does not write resolved IDs back into
the draft.

## 6. Cycle Pass

Cycle detection operates only on explicit dependency edges.

The cycle graph uses:
- target explicit dependencies from `EVENT_TARGET_ADD_DEPENDENCY`
- target relations explicitly materialized by future promoted dependency events

The cycle graph does not infer edges from:
- raw `link_libraries` string payload
- generator expressions embedded inside string payload
- directory/global property text

The algorithm is DFS with 3-color marking.

## 7. Semantic Pass

The semantic pass enforces model-level rules for release 1.

Required checks:
- `BM_TARGET_INTERFACE_LIBRARY` may not own concrete source files
- interface libraries may not carry private link-library entries
- alias targets may not own sources, dependency edges, or typed build payload
- duplicate source paths inside the same non-interface target emit warnings
- imported targets may not declare local build output properties

Warnings are allowed for suspicious but non-fatal conditions. Errors block
freeze.

## 8. Diagnostics Policy

Validation uses `Diag_Sink` only.

Each diagnostic should include, when available:
- entity name
- entity kind
- provenance file/line/column
- failing symbolic reference
- concise fix-oriented message

The validator does not call `diag_log(...)` directly.

## 9. Failure Contract

Callers must stop before freeze if `bm_validate_draft(...)` returns `false`.

The validator should continue reporting independent issues after the first
error when doing so does not depend on mutated state.
