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

```c
typedef enum {
    BM_REPLAY_OPCODE_NONE = 0,
    BM_REPLAY_OPCODE_FS_MKDIR,
    BM_REPLAY_OPCODE_FS_WRITE_TEXT,
    BM_REPLAY_OPCODE_FS_APPEND_TEXT,
    BM_REPLAY_OPCODE_FS_COPY_FILE,
    BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL,
    BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR,
    BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR,
    BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE,
    BM_REPLAY_OPCODE_HOST_LOCK_RELEASE,
    BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE,
    BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT,
    BM_REPLAY_OPCODE_PROBE_TRY_RUN,
    BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR,
    BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP,
} BM_Replay_Opcode;
```

The replay domain may grow by appending enum values only.

## 4. Canonical Replay Entity

Every replay action is a first-class frozen-model entity with:

- `id`
- `kind`
- `opcode`
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
- canonical replay ingest uses dedicated Event IR kinds:
  `EVENT_REPLAY_ACTION_DECLARE`, `EVENT_REPLAY_ACTION_ADD_INPUT`,
  `EVENT_REPLAY_ACTION_ADD_OUTPUT`, `EVENT_REPLAY_ACTION_ADD_ARGV`,
  `EVENT_REPLAY_ACTION_ADD_ENV`
- replay actions copy retained strings into builder-owned storage
- owner directory is captured from the active directory frame at ingest time
- argv, env, inputs, and outputs are stored as explicit spans
- environment entries freeze as normalized `KEY=VALUE` strings
- opcode-specific payload layout is frozen over the generic spans
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
- `bm_query_replay_action_opcode`
- `bm_query_replay_action_phase`
- `bm_query_replay_action_owner_directory`
- `bm_query_replay_action_working_directory`
- `bm_query_replay_action_inputs`
- `bm_query_replay_action_outputs`
- `bm_query_replay_action_argv`
- `bm_query_replay_action_environment`

Future query helpers may append action-kind-specific accessors, but the generic
surface above plus the typed opcode is the minimum stable contract.

Current C2 configure replay opcodes freeze payloads as follows:

- `FS_MKDIR`
  `outputs[] = ordered directories`
- `FS_WRITE_TEXT`
  `outputs[0] = destination`, `argv[0] = final text`, `argv[1] = octal mode or ""`
- `FS_APPEND_TEXT`
  `outputs[0] = destination`, `argv[0] = appended text`
- `FS_COPY_FILE`
  `inputs[0] = source`, `outputs[0] = destination`, `argv[0] = octal mode or ""`
- `HOST_DOWNLOAD_LOCAL`
  `inputs[0] = normalized local source`, `outputs[0] = destination`,
  `argv[0] = hash algorithm or ""`, `argv[1] = digest or ""`
- `HOST_ARCHIVE_CREATE_PAXR`
  `inputs[] = ordered source paths`, `outputs[0] = archive path`,
  `argv[0] = mtime epoch`
- `HOST_ARCHIVE_EXTRACT_TAR`
  `inputs[0] = archive`, `outputs[0] = destination`
- `HOST_LOCK_ACQUIRE` and `HOST_LOCK_RELEASE`
  `outputs[0] = resolved lock file path`

Current `C3` additions freeze payloads as follows:

- `PROBE_TRY_COMPILE_SOURCE`
  `inputs[] = ordered source files`, `outputs[0] = binary dir`,
  `argv[] = typed probe payload`
- `PROBE_TRY_COMPILE_PROJECT`
  `inputs[0] = source dir`, `outputs[0] = binary dir`,
  `argv[] = typed probe payload`
- `PROBE_TRY_RUN`
  `outputs[0] = binary dir`, `argv[] = typed probe payload`
- `DEPS_FETCHCONTENT_SOURCE_DIR`
  `outputs[0] = effective source dir`, `outputs[1] = effective binary dir`,
  `argv[0] = dependency name`
- `DEPS_FETCHCONTENT_LOCAL_ARCHIVE`
  `inputs[0] = local archive`, `outputs[0] = effective source dir`,
  `outputs[1] = effective binary dir`, `argv[] = archive/hash payload`
- `TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY`
  `outputs[0] = target build dir`
- `TEST_DRIVER_CTEST_START_LOCAL`
  `outputs[0] = source dir`, `outputs[1] = build dir`,
  `argv[0] = model`, `argv[1] = track`, `argv[2] = append flag`
- `TEST_DRIVER_CTEST_CONFIGURE_SELF`
  `outputs[0] = source dir`, `outputs[1] = build dir`
- `TEST_DRIVER_CTEST_BUILD_SELF`
  `outputs[0] = build dir`, `argv[0] = config`, `argv[1] = target`
- `TEST_DRIVER_CTEST_TEST`
  `outputs[0] = build dir`, `argv[0] = output junit or ""`,
  `argv[1] = random-schedule flag`
- `TEST_DRIVER_CTEST_SLEEP`
  `argv[0] = duration text`

In strict local-only `C3`:

- dependency-materialization positive support is limited to saved
  `SOURCE_DIR` and local archive `URL` plus `URL_HASH`
- test-driver positive support is limited to local
  `ctest_empty_binary_directory`, `ctest_start`, `ctest_configure`,
  `ctest_build`, `ctest_test`, and `ctest_sleep`
- probe opcodes are typed downstream ownership but remain explicit backend
  rejects

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
