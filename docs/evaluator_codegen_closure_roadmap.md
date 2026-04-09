# Evaluator To Codegen Closure Roadmap

Status: Canonical active roadmap for the post-`P8` backend-closure program.
This document defines the multi-wave path from the current artifact-parity
baseline to a generated Nob backend that is product-complete enough to be
treated as the primary execution surface for the supported CMake 3.28 subset.

This roadmap does not replace subsystem contracts under `docs/evaluator/`,
`docs/transpiler/`, `docs/build_model/`, `docs/codegen/`, or `docs/tests/`.
It coordinates the cross-layer work required to turn the current
`evaluator -> Event IR -> build_model -> codegen` stack into a closure program
with explicit waves, stable contracts, and proof gates.

## 1. Status

Nobify already has:

- a broad evaluator audit closed against the current CMake 3.28 command matrix
- a canonical `Event_Stream` boundary
- a frozen/query-facing `build_model`
- aggregate-safe `codegen` coverage for build, install, export, and package
- explicit `artifact-parity` proof for the currently supported artifact subset
- an explicit `evaluator-codegen-diff` harness that inventories backend status

What it does not yet have is a closure program for the gap between:

- evaluator-visible implementation coverage
- downstream replayability in `build_model`
- generated-backend execution coverage
- product-level status reporting for families that are still outside the
  supported generated backend

That gap is too large and too cross-cutting to be treated as one more wave in
the artifact-parity roadmap. It needs its own active program.

## 2. Goal

The goal of this program is to make Nobify a product-complete transpiler for
its supported CMake 3.28 subset by ensuring that every implemented evaluator
surface is explicitly classified and that every surface with relevant
observable effects is either:

- supported through the canonical downstream pipeline
- rejected explicitly and diagnosably as a backend-owned gap
- or declared an explicit non-goal by justified variant, not by silent family
  drift

The preserved architecture remains:

`AST -> evaluator -> Event IR -> build_model -> codegen -> observable effects`

The closure program does not authorize:

- direct codegen consumption of evaluator-private state
- a generated `nob.c` that becomes a general evaluator replay engine
- bypassing the `build_model` to chase feature coverage quickly

## 3. Why This Is Not One Wave

This work is not one wave because it spans four independent concerns that must
advance together but do not land at the same rate:

- contract reset:
  the current suite and roadmap language still describe a backlog radar more
  than a product-closure program
- downstream modeling:
  replayable configure-time and host-effect actions need canonical build-model
  representation
- backend runtime:
  the generated Nob program needs explicit configure/test/runtime phase
  semantics and additional helpers
- repeated downstream queries:
  closure-scale codegen and diff workloads need a canonical memoized query
  surface so performance and cache ownership do not fragment across consumers
- proof hardening:
  each new supported family needs evaluator, pipeline, build-model, codegen,
  and explicit closure-harness evidence

Treating all of that as one wave would hide sequencing decisions and make it
unclear which contract is authoritative at each stage.

## 4. Frozen Decisions

The following decisions are frozen for this roadmap:

- the canonical product pipeline remains
  `AST -> evaluator -> Event IR -> build_model -> codegen`
- the `build_model` remains the only downstream semantic representation read by
  codegen
- no new Event IR role is introduced for this program; replayable downstream
  events use existing role masks and may carry both
  `EVENT_ROLE_RUNTIME_EFFECT` and `EVENT_ROLE_BUILD_SEMANTIC`
- families with practical observable effects are support targets by default;
  they do not remain `evaluator-only` merely because they are traditionally
  configure-time commands
- `explicit-non-goal` remains a legal classification, but it is chosen only by
  explicit variant and justification
- the generated `nob.c` must stay legible:
  helpers and phase functions are product surface, not incidental test output
- effective-query memoization is legal only as a query-time derived cache; it
  must not persist inferred semantics back into `Build_Model`

## 5. Closure Definition

This closure program is complete only when the following are simultaneously
true for the supported product subset:

- every `FULL` evaluator command has an explicit inventory entry in the closure
  harness
- every curated implemented subcommand family has explicit classification and
  phase ownership
- replayable downstream effects are represented canonically in `build_model`
  instead of surviving only as evaluator-local host execution
- generated backend execution covers the supported configure/build/test/
  install/export/package surfaces through explicit commands and stable runtime
  helpers
- unsupported variants fail through stable `backend-reject` or
  `explicit-non-goal` paths instead of implicit omission
- cross-layer proof exists at evaluator, pipeline, build-model, codegen, and
  closure-harness levels

This program does not require replaying every evaluator-internal semantic step.
It requires that the backend-owning subset be explicit, understandable, and
provably correct.

## 6. Workstreams

The program is split into the following workstreams:

### 6.1 Closure Harness

- redefine `test-evaluator-codegen-diff` as the canonical closure harness
- version the inventory by command, subcommand, phase, and downstream domain
- keep case metadata aligned with the full `evaluator_diff` DSL
- turn status reporting into an operational product metric

### 6.2 Downstream Replay Domain

- add a first-class replay/action domain to `build_model`
- freeze phase-aware configure/build/test/install/export/package ownership
- expose typed query APIs so codegen never consumes raw Event IR directly

### 6.3 Query Memoization And Consumer Convergence

- add a canonical memoized derived-query surface for repeated effective-query
  and target-resolution access
- keep memoization query-time only; do not mutate frozen semantic ownership
- converge duplicated codegen-local caches onto the canonical query layer as
  memoized coverage lands

### 6.4 Generated Backend Runtime

- add explicit `configure` command to the generated `nob.c`
- define phase semantics, auto-configure rules, runtime helper vocabulary, and
  rejection policy
- keep generated code understandable and auditable

### 6.5 High-Surface Family Completion

- move deterministic configure-time and host-effect families from implicit
  evaluator execution into downstream replay where appropriate
- prioritize simple deterministic effects before process/probe/test families
- keep unsupported variants explicit and measurable

### 6.6 Corpus Hardening

- use real-project evidence to validate that the closure subset is meaningful
- turn recurring failures into new proof cases or explicit support boundaries

## 7. Wave Plan

### C0 Contract Reset And Inventory Baseline

Goal:
- reset the documentation and suite contracts so the program is explicitly
  multi-wave and no longer described as one backlog bucket

Deliverables:
- new closure roadmap under `docs/evaluator_codegen_closure_roadmap.md`
- rewritten `docs/tests/evaluator_codegen_diff.md`
- updated docs indexes and test-architecture wording
- formal state model:
  `parity-pass`, `backend-reject`, `evaluator-only`,
  `explicit-non-goal`, `skip-by-tool`
- explicit rule that `explicit-non-goal` is not the default outcome for the
  selected support-target families

Non-goals:
- no new backend behavior
- no new `build_model` storage yet
- no reclassification of large families without corresponding contract text

Exit criteria:
- the closure program has one canonical roadmap
- suite ownership and state taxonomy are decision-complete
- implementers no longer need to guess whether future work belongs to the
  artifact-parity roadmap or to the closure program

Evidence:
- updated docs only
- closure harness contract references the full `evaluator_diff` DSL

Dependencies:
- current `P0` through `P8` artifact-parity baseline remains the historical
  foundation

### C1 Downstream Replay Domain Foundation

Goal:
- create the canonical downstream domain for replayable actions without
  changing the architecture boundary

Deliverables:
- `build_model` replay domain with typed IDs, kinds, and phases
- builder ingest rules for downstream-replayable events
- frozen/query-facing accessors for replay actions
- pipeline snapshots proving replay-domain freezing and ordering

Non-goals:
- no broad family support claims yet
- no codegen execution of the new domain beyond scaffolding hooks

Exit criteria:
- replayable downstream actions are no longer only evaluator-local side
  effects
- codegen can query replay actions without touching Event IR directly

Evidence:
- `test-build-model`
- `test-pipeline`
- targeted evaluator and closure-harness inventory updates

Dependencies:
- `C0` contracts and inventory model

### C1M Query Memoization And Consumer Convergence

Goal:
- make repeated effective-query and target-resolution access cheap enough for
  closure-scale codegen and diff workloads without changing semantic ownership
  boundaries

Deliverables:
- canonical `build_model` query-session or equivalent memoized derived-query
  surface for effective queries
- memoization keys that cover all required caller context such as target,
  query family, usage mode, configuration, and compile language
- migration plan to collapse duplicated codegen-local caches onto the
  canonical memoized query layer
- focused workload counters or benchmarks for repeated-query closure runs

Non-goals:
- no semantic change to raw or effective query meaning
- no persistence of inferred values back into `Build_Model`
- no requirement for daemon-grade global incrementality or cross-process cache

Exit criteria:
- repeated effective queries are served through canonical memoized paths
- duplicate first-line caches for the same derived families are no longer the
  default ownership pattern in codegen
- memoization remains context-correct and invisible to public semantics

Evidence:
- `test-build-model`
- `test-codegen`
- targeted repeated-query workload measurements or closure-harness runs

Dependencies:
- `C1` replay/query baseline

### C2 Configure Backend And Deterministic Effects

Goal:
- add explicit configure-phase backend behavior and close deterministic simple
  effects first

Deliverables:
- generated `nob.c` `configure` command
- generated `nob.c` `build [targets...]` command
- default CLI rule: no subcommand means `configure + build`
- auto-configure before later phases when required
- backend support for deterministic replay families such as:
  simple filesystem effects
  deterministic `file(...)` host effects
  explicit configure-time output materialization that already has canonical
  downstream representation

Non-goals:
- no broad process/probe matrix yet
- no generated `test` command yet
- no full `FetchContent_*` or `ctest_*` completion in this wave

Exit criteria:
- configure-time replay has a stable generated-backend contract
- deterministic replayable effects are no longer forced into
  `backend-reject` merely because they happen before build

Evidence:
- `test-codegen`
- `test-evaluator-codegen-diff`
- explicit focused parity cases

Dependencies:
- `C1` replay domain and query surface
- `C1M` memoized derived-query foundation

### C3 Process, Probes, Dependency Materialization, And Test Execution

Goal:
- extend the downstream/backend contract to the heavier configure-time and
  host-process families

Deliverables:
- canonical replay support for process-backed actions where the product chooses
  support
- typed downstream representation for probe families such as `try_compile` and
  `try_run`
- deterministic dependency-materialization flows for the supported
  `FetchContent_*` subset
- generated `nob.c` `test` command and baseline `ctest_*` downstream support

Non-goals:
- no blanket claim that every historical provider/network variant is supported
- no silent cross-compiling or emulator support expansion without dedicated
  proof

Exit criteria:
- process/probe/test families have explicit downstream ownership
- supported variants move to `parity-pass`
- unsupported variants stay explicit by signature

Evidence:
- evaluator tests for projection and gating
- build-model and pipeline tests for replay-domain freezing
- codegen execution tests
- closure-harness parity or stable-reject cases

Dependencies:
- `C2` phase runtime contract

### C4 Real-Project Hardening And Supported-Subset Freeze

Goal:
- harden the closure subset against real projects and freeze the supported
  generated-backend scope as a product claim

Deliverables:
- real-project corpus runs against the closure harness
- severity/frequency classification of remaining rejects
- explicit supported-subset documentation derived from evidence
- release-gating policy informed by corpus results

Non-goals:
- no infinite support claim for the full CMake 3.28 universe
- no silent backlog burial

Exit criteria:
- the supported subset is documented and evidence-backed
- recurring corpus failures are converted into proof cases, rejects, or product
  boundaries

Evidence:
- `test-evaluator-codegen-diff`
- `test-artifact-parity`
- real-project corpus runs

Dependencies:
- `C0` through `C3`
- `C1M`

## 8. Evidence Gates

The minimum evidence gate for each newly supported family is:

- evaluator projection or semantic tests
- pipeline/build-model proof for downstream representation
- codegen render and execution proof
- closure-harness classification with at least one focused case

The minimum evidence gate for each newly declared explicit non-goal is:

- variant-level inventory entry
- documented justification
- stable reject or explicit non-goal coverage in the closure harness

No family or variant may change status through code changes alone.
The suite and the documentation must change with it.

## 9. Relationship To Other Docs

- [`cmake_artifact_parity_roadmap.md`](./cmake_artifact_parity_roadmap.md)
  Historical and delivered `P0` through `P8` path to the current parity
  baseline. Future closure work is coordinated here instead.

- [`tests/evaluator_codegen_diff.md`](./tests/evaluator_codegen_diff.md)
  Canonical closure-harness contract, state taxonomy, and inventory rules.

- [`build_model/build_model_replay.md`](./build_model/build_model_replay.md)
  Canonical downstream replay-domain contract.

- [`build_model/build_model_query.md`](./build_model/build_model_query.md)
  Canonical query surface and query-time derivation rules that memoization must
  preserve.

- [`codegen/codegen_runtime_contract.md`](./codegen/codegen_runtime_contract.md)
  Canonical generated-backend CLI and runtime contract.

- [`transpiler/event_ir_v2_spec.md`](./transpiler/event_ir_v2_spec.md)
  Canonical Event IR schema and role taxonomy.

- [`evaluator/evaluator_event_ir_contract.md`](./evaluator/evaluator_event_ir_contract.md)
  Canonical evaluator projection contract for downstream-consumable events.
