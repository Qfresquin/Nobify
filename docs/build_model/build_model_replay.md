# Build Model Replay Domain (Normative)

## 1. Role

The replay domain is the canonical downstream representation for actions that
must be replayable by the generated backend but do not fit purely inside the
existing target/build-step/install/export/package domains.

Its purpose is to preserve the architecture boundary:

`Event_Stream -> Builder -> Validate -> Freeze -> Query -> codegen`

Codegen must consume replay actions through query helpers over `Build_Model`.
It must not consume raw `Event_Stream` items directly.

## 1.1 Boundary

Replay actions are downstream build-model entities. They do not authorize
direct runtime replay from evaluator internals or direct codegen reads of raw
Event IR.

## 1.2 Data Flow

`downstream-consumable Event IR -> replay ingest -> replay freeze -> replay query -> codegen phase execution`

## 2. Scope

The replay domain covers actions that are:

- observable to the host or to later build phases
- ordered relative to configure/build/test/install/export/package execution
- not already fully represented by existing build-model domains

Typical examples include:

- configure-time filesystem materialization
- process-backed configure actions
- probe execution such as `try_compile()` and `try_run()`
- deterministic dependency materialization
- test execution plans that must be driven by the generated backend

The replay domain does not replace:

- target build steps
- install rules
- standalone export records
- package-plan records

Those domains stay first-class and keep their existing dedicated queries.

## 3. Public Type Additions

The canonical public headers must expose:

```c
typedef uint32_t BM_Replay_Action_Id;
```

```c
typedef enum {
    BM_REPLAY_PHASE_CONFIGURE = 0,
    BM_REPLAY_PHASE_BUILD,
    BM_REPLAY_PHASE_TEST,
    BM_REPLAY_PHASE_INSTALL,
    BM_REPLAY_PHASE_EXPORT,
    BM_REPLAY_PHASE_PACKAGE,
    BM_REPLAY_PHASE_HOST_ONLY,
} BM_Replay_Phase;
```

```c
typedef enum {
    BM_REPLAY_ACTION_FILESYSTEM = 0,
    BM_REPLAY_ACTION_PROCESS,
    BM_REPLAY_ACTION_PROBE,
    BM_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION,
    BM_REPLAY_ACTION_TEST_DRIVER,
    BM_REPLAY_ACTION_HOST_EFFECT,
} BM_Replay_Action_Kind;
```

The replay domain may grow by appending enum values only.

## 4. Canonical Replay Entity

Every replay action is a first-class frozen-model entity with:

- `id`
- `kind`
- `phase`
- `owner_directory_id`
- `provenance`
- `working_directory`
- declared environment overrides
- declared inputs
- declared outputs
- command argv or action-specific payload
- required external tools metadata when applicable

The ordering contract is append-only and source-derived:

- replay actions preserve the committed order of the upstream semantic events
- codegen must treat that order as canonical within the same phase
- phase ordering is explicit metadata, not inferred later from action kind

## 5. Phase Model

The canonical replay phases are:

- `configure`
  configure-time actions that must complete before build/test/install/export/
  package behavior can be considered valid
- `build`
  replay actions that belong to build-time execution but are not ordinary
  target build-step declarations
- `test`
  actions that drive test discovery or execution in the generated backend
- `install`
  phase-owned replay actions that are not already captured by install rules
- `export`
  phase-owned replay actions that are not already captured by export records
- `package`
  phase-owned replay actions that are not already captured by package-plan
  records
- `host-only`
  actions that remain observable and classified but are not part of the normal
  generated-backend phase flow

The replay domain must not guess phase membership at query time. Phase is part
of the frozen record.

## 6. Builder Rules

The builder consumes replay-domain inputs from canonical Event IR. It does not
invent replay actions from evaluator-private state.

Builder requirements:

- only events documented as downstream-consumable may create replay actions
- replay actions copy retained strings into builder-owned storage
- owner directory is captured from the active directory frame at ingest time
- argv, env, inputs, and outputs are stored as explicit spans
- the draft may keep unresolved symbolic references where necessary, but the
  frozen model must expose stable typed accessors only

The recommended implementation split is:

- `build_model_builder_replay.c`

That file owns replay-domain ingest and related helpers. The top-level builder
dispatch remains in `build_model_builder.c`.

## 7. Freeze And Query Rules

Freeze must produce compact replay-action arrays with stable indexing.

Query is the only public read surface for codegen and tooling. The canonical
query additions are:

- `bm_query_replay_action_count`
- `bm_query_replay_action_kind`
- `bm_query_replay_action_phase`
- `bm_query_replay_action_owner_directory`
- `bm_query_replay_action_working_directory`
- `bm_query_replay_action_inputs`
- `bm_query_replay_action_outputs`
- `bm_query_replay_action_argv`
- `bm_query_replay_action_environment`

Future query helpers may append action-kind-specific accessors, but the generic
surface above is the minimum stable contract.

## 8. Relationship To Existing Domains

The replay domain complements existing downstream domains.

It must not duplicate:

- build-step scheduling already represented by `EVENT_BUILD_STEP_*` and build
  model build-step queries
- install-tree copying already represented by install rules
- export-file generation already represented by export records
- package-plan generation already represented by package records

When an action is already canonical in another domain, the replay domain does
not mirror it.

The replay domain exists for everything that still needs backend execution but
would otherwise remain trapped as evaluator-local runtime behavior.

## 9. Codegen Contract

Codegen reads replay actions only through build-model queries.

Required consequences:

- generated-backend phase functions may orchestrate replay actions alongside
  existing domain execution
- helper generation may depend on replay-action kinds and required tools
- codegen must not inspect Event IR directly to recover replay behavior

This keeps the frozen model as the sole semantic representation consumed by the
generated backend.

## 10. Public Contract

The stable public contract of this document is:

- replay IDs, kinds, and phases
- ordering/provenance requirements
- minimum replay query surface consumed by codegen

## 11. Non-goals

- replacing canonical target/build-step/install/export/package records
- defining evaluator projection rules (owned by evaluator/Event IR contracts)
- making codegen a direct Event IR consumer

## 12. Evidence

Expected evidence includes:

- build-model tests for replay ingest/freeze/query behavior
- pipeline tests showing replay-domain propagation from Event IR
- codegen/closure-harness tests proving phase-aware replay behavior
