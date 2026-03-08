# Build Model Freeze (Normative)

## 1. Role

Freeze converts a validated `Build_Model_Draft` into an immutable
`Build_Model`.

The frozen model is compact, indexable, and independent from builder state.

## 2. Public API

```c
const Build_Model *bm_freeze_draft(const Build_Model_Draft *draft,
                                   Arena *out_arena,
                                   Diag_Sink *sink);
```

Return rules:
- returns non-`NULL` on success
- returns `NULL` on OOM or invariant failure

## 3. Preconditions

`bm_freeze_draft(...)` requires:
- a non-`NULL` draft returned by `bm_builder_finalize(...)`
- a successful `bm_validate_draft(...)` result
- an output arena dedicated to the frozen model lifetime

Freeze is allowed to re-check critical invariants defensively, but callers are
required to run validation first.

## 4. Freeze Responsibilities

Freeze performs these operations:

1. Copy all retained strings and records into `out_arena`.
2. Intern repeated strings.
3. Convert symbolic references into typed ID arrays.
4. Convert append-oriented draft collections into compact flat arrays.
5. Build lookup indexes required by query.

## 5. Resolution Policy

The frozen model stores typed resolved relations, not symbolic strings, for:
- target explicit dependencies
- target alias references
- target-owned install rules
- CPack component group references
- CPack component dependency references
- CPack component install-type references

If a required resolution is missing during freeze, freeze emits an error through
`Diag_Sink` and returns `NULL`.

## 6. String and Property Policy

- Strings are interned opportunistically during freeze.
- Typed raw property entries are preserved.
- Raw future-facing property bags are preserved for tooling and future
  promotions, but they remain read-only in the frozen model.
- Freeze does not evaluate generator expressions or synthesize inherited state.

## 7. Required Indexes

Release-1 frozen indexes include:
- target name -> `BM_Target_Id`
- test name -> `BM_Test_Id`
- package name -> `BM_Package_Id`
- directory ID validation metadata needed by query traversal

Additional indexes may be added if they are append-only extensions of the query
contract.

## 8. Frozen Model Guarantees

The frozen model guarantees:
- stable IDs for the lifetime of the model
- no pointers back into builder-owned memory
- no mutable append buffers
- safe concurrent read access by multiple query callers

## 9. Non-Responsibilities

Freeze does not:
- mutate evaluator or builder state
- perform semantic validation policy
- infer new dependency edges
- flatten effective inherited/transitive properties for every target eagerly

That work remains in validation and query.
