# Evaluator To Codegen Diff

## Status

As of April 6, 2026, `test-evaluator-codegen-diff` is the canonical
explicit-only closure harness for the remaining
`evaluator -> Event IR -> build_model -> codegen` gap.

It is intentionally heavier than the default smoke aggregate, remains outside the default smoke
aggregate, and is organized around explicit multi-wave closure work rather than
one undifferentiated backend backlog.

## Goal

This suite exists to answer one product question for every implemented
evaluator command and curated subcommand surface:

- is the surface already supported by the generated backend with proof
- is it still a backend-owned reject with stable diagnostics
- is it genuinely evaluator-only because no downstream replay is required
- or is it an explicit non-goal by justified variant

The suite does not turn generated `nob.c` into a general evaluator replay
engine. It preserves the product boundary:

`AST -> evaluator -> Event IR -> build_model -> codegen -> observable effects`

## Product Boundary

The closure harness makes backend ownership explicit.

Product rules:

- configure-time or host-effect families do not remain `evaluator-only` by
  default merely because they happen before build
- a surface is `evaluator-only` only when its relevant behavior is already
  frozen downstream and no backend replay is needed
- a surface may be `explicit-non-goal` only by explicit variant and written
  justification
- codegen proof still flows through `build_model`; the suite does not authorize
  direct codegen consumption of raw Event IR
- query memoization proof is legal only as query-time derived-cache evidence;
  it does not create a new semantic state and does not authorize persistence of
  inferred semantics back into `Build_Model`

## State Model

Each case and each inventoried command/subcommand carries one declared semantic
classification from the following set:

- `parity-pass`
  The case is replayed through real `cmake` and through
  `nobify -> generated nob.c -> generated Nob`, and the suite diffs the
  observable outputs.
- `backend-reject`
  The case is backend-owned, but the current backend is still expected to fail
  explicitly with a stable diagnostic.
- `evaluator-only`
  The case is semantic-only because the relevant observable behavior is already
  frozen downstream and does not require replay in the generated backend.
- `explicit-non-goal`
  The case is outside the intended generated-backend product surface by
  explicit variant-level decision and written justification.

Runtime reporting may additionally emit:

- `skip-by-tool`
  The case is otherwise valid for proof, but the host is missing a required
  external tool such as `cmake`, `cpack`, `gzip`, `xz`, or a later-wave helper
  dependency.

`skip-by-tool` is not a host-dependent inventory classification. It is a
runtime execution/reporting result layered on top of the declared semantic
state.

`explicit-non-goal` is a legal state but not the default destination for the
families chosen as closure-program support targets. It must never be used as a
bulk family escape hatch.

## Inventory Rules

The suite cross-checks evaluator implementation inventory against:

- `docs/evaluator/evaluator_coverage_matrix.md`
- the evaluator command registry/capability tables compiled into the current
  binary

The suite must fail if:

- any `FULL` command in the coverage matrix has no explicit inventory entry
- any curated implemented subcommand in the tracked families has no explicit
  classification
- any closure-program support-target family has variants drifting without
  phase/domain ownership

The inventory is versioned and explicit. It records at minimum:

- command family
- subcommand or signature when needed
- source case-pack
- phase ownership
- downstream domain ownership
- classification state
- reason text
- backlog key or explicit non-goal justification key when applicable

Curated subcommand families include at least:

- `file()`
- `string()`
- `list()`
- `math()`
- `cmake_language()`
- `cmake_path()`
- `ctest_*`

The closure program may add more curated families as backend ownership expands.

## Phase Model

The suite classifies proof ownership by required phase set:

- `configure`
- `build`
- `test`
- `install`
- `export`
- `package`
- `host-only`

Phase membership is case metadata, not an implementation guess.

The suite exists to prove that configure-time and later-phase behavior can be
carried through canonical downstream domains without breaking the product
boundary.

## Case Metadata

The canonical corpus is:

- `test_v2/evaluator/golden/evaluator_default.cmake`
- `test_v2/evaluator/golden/evaluator_all.cmake`
- `test_v2/evaluator/golden/evaluator_integration.cmake`
- `test_v2/evaluator_diff/cases/*.cmake`
- focused seed case-packs under `test_v2/evaluator_codegen_diff/cases/` when a
  backend-owned surface has no suitable existing case

The closure harness adopts the full case-pack DSL from `evaluator_diff` rather
than a suite-local subset. Each closure case declares the metadata required to
drive the proof path, including:

- case mode
- required phase set
- required external tools
- observed roots/files
- diff mode per observed output
- expected result kind
- downstream domain ownership
- repeated-query workload tag when the case also participates in memoization
  proof
- backlog or explicit non-goal key when applicable

When existing evaluator case-packs are not enough, the suite adds local seeds
under `test_v2/evaluator_codegen_diff/cases/` instead of synthesizing ad hoc C
tests.

## Promotion Rules

A case or variant may change state only when the suite and its documentation
change with it.

Legal state transitions include:

- `backend-reject -> parity-pass`
- `backend-reject -> explicit-non-goal`
- `evaluator-only -> parity-pass`
  when the product deliberately promotes a surface into backend replay

Illegal transition pattern:

- changing product status only through implementation changes without matching
  suite classification, metadata, and evidence updates

Promotion into `parity-pass` requires:

- downstream representation through canonical `build_model` or existing
  supported domains
- generated-backend proof
- explicit closure-harness evidence

## Execution Policy

`test-evaluator-codegen-diff` is explicit-only by design.

Reasons:

- it depends on real host tools for `parity-pass` cases
- it may exercise configure/build/test/install/export/package in one run
- it is intended to be the operational closure harness for backend completion,
  not the default smoke tier

It therefore does not participate in the current baseline
`./build/nob test` / `./build/nob test smoke` smoke aggregate.

`./build/nob test evaluator-codegen-diff`

Like the other explicit-only host suites, it inherits runner-owned workspace,
locking, logging, and tool-resolution behavior from `src_v2/build/test_runner_*`.

## Reporting

The closure harness reports status by command family, subcommand family, and
classification state, while reporting `skip-by-tool` separately as a runtime
execution result.

Minimum reporting vocabulary:

- `parity-pass`
- `backend-reject`
- `evaluator-only`
- `explicit-non-goal`
- `skip-by-tool`

The suite compares observable outputs, not internal implementation details.

Canonical diff primitives are:

- `TREE`
- `FILE_TEXT`
- `FILE_SHA256`
- structural manifests built from those primitives

Logs are not a primary diff surface for `parity-pass` cases. They are a
diagnostic surface for rejects and for preserving actionable failure context.

## Derived Query Performance Evidence

The closure harness remains correctness-first, but it may also carry explicit
supporting evidence for the query-memoization workstream.

Legal memoization evidence includes:

- targeted repeated-query workloads over closure-harness cases that stress
  effective-query and target-resolution access
- before/after measurements tied to the same case set and tool availability
- `BM_Query_Session` hit/miss counters gathered while those workloads run
- proof that canonical memoized query paths replace duplicated first-line
  consumer caches without changing case classification or observable outputs

Memoization evidence does not replace the normal closure gate:

- correctness classification stays primary
- `parity-pass` and `backend-reject` decisions still come from observable
  behavior and stable diagnostics
- performance data is supporting wave evidence, not a new product state

Unless the roadmap later freezes a numeric gate, memoization measurements are
comparative and diagnostic rather than release-blocking by absolute threshold.
Session counters are therefore valid supporting evidence even when a host is
not suitable for stable wall-clock benchmarking.

## Relationship To Other Suites

- `evaluator`
  Owns semantic execution/report/event correctness for command families.
- `pipeline`
  Owns evaluator-to-build-model integration snapshots.
- `codegen`
  Owns aggregate-safe generated-backend smoke.
- `artifact-parity`
  Owns explicit real-CMake parity for the currently supported artifact surface.
- `evaluator-codegen-diff`
  Owns closure-program status classification and explicit proof for the
  remaining evaluator-to-codegen gap.
