# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Build Model Builder (Normative)

## 1. Role

`BM_Builder` incrementally reconstructs `Build_Model_Draft` from the canonical
`Event_Stream`.

It is the only writer of draft state.

Its job is semantic reconstruction first:
- preserve the evaluator's validated CMake 3.28-visible semantics,
- keep historical behavior only when that is required for the same observable
  baseline,
- avoid backend-specific optimization or semantic guessing during ingest.

## 2. Public API

The canonical builder API is:

```c
BM_Builder *bm_builder_create(Arena *arena, Diag_Sink *sink);
bool bm_builder_apply_event(BM_Builder *builder, const Event *ev);
bool bm_builder_apply_stream(BM_Builder *builder, const Event_Stream *stream);
const Build_Model_Draft *bm_builder_finalize(BM_Builder *builder);
```

Compatibility wrappers such as `builder_create(...)` and `builder_finish(...)`
may exist during migration, but they are shims only.

## 3. Input Contract

The builder consumes the canonical `Event` type from
`src_v2/transpiler/event_ir.h`.

The upstream producer is the evaluator run entry
`eval_session_run(EvalSession *, const EvalExec_Request *, Ast_Root)` when
`EvalExec_Request.stream != NULL`.

Required behavior:
- If `event_kind_has_role(ev->h.kind, EVENT_ROLE_BUILD_SEMANTIC)` is false, the
  event is ignored and the function returns `true`.
- If the event is build-semantic and supported in release 1, the builder must
  apply it.
- If the event is build-semantic and unsupported, the builder emits an error,
  enters fatal state, and returns `false`.

The release-1 supported build-semantic set is:
- `EVENT_PROJECT_DECLARE`
- `EVENT_PROJECT_MINIMUM_REQUIRED`
- `EVENT_DIRECTORY_ENTER`
- `EVENT_DIRECTORY_LEAVE`
- `EVENT_DIRECTORY_PROPERTY_MUTATE`
- `EVENT_GLOBAL_PROPERTY_MUTATE`
- `EVENT_TARGET_DECLARE`
- `EVENT_TARGET_ADD_SOURCE`
- `EVENT_TARGET_ADD_DEPENDENCY`
- `EVENT_TARGET_PROP_SET`
- `EVENT_TARGET_LINK_LIBRARIES`
- `EVENT_TARGET_LINK_OPTIONS`
- `EVENT_TARGET_LINK_DIRECTORIES`
- `EVENT_TARGET_INCLUDE_DIRECTORIES`
- `EVENT_TARGET_COMPILE_DEFINITIONS`
- `EVENT_TARGET_COMPILE_OPTIONS`
- `EVENT_TARGET_COMPILE_FEATURES`
- `EVENT_TEST_ENABLE`
- `EVENT_TEST_ADD`
- `EVENT_INSTALL_RULE_ADD`
- `EVENT_CPACK_ADD_INSTALL_TYPE`
- `EVENT_CPACK_ADD_COMPONENT_GROUP`
- `EVENT_CPACK_ADD_COMPONENT`
- `EVENT_PACKAGE_FIND_RESULT`

The closure program extends that baseline with a replay-domain ingest path for
events that remain configure/build/test/install/export/package-relevant after
evaluation. Those events are still consumed only through the canonical Event IR
schema and only when the Event IR contract marks them as downstream-consumable.

## 4. Builder State

`BM_Builder` owns:
- the destination arena for all draft allocations
- a fatal-error flag
- the current directory stack
- transient lookup indexes by name and by ID
- the mutable `Build_Model_Draft`

It does not own or mutate the source `Event_Stream`.

## 5. Domain Rules

### Project

- `EVENT_PROJECT_DECLARE` initializes or replaces the effective project record.
- `languages` is split using CMake-list semantics and stored as normalized items.
- `EVENT_PROJECT_MINIMUM_REQUIRED` updates dedicated minimum-required state on
  the draft; it does not mutate evaluator policy state.

### Directory

- `EVENT_DIRECTORY_ENTER` creates a directory record and pushes it as the active
  frame.
- The root directory is the first entered directory.
- `EVENT_DIRECTORY_LEAVE` must match the active frame. A mismatched pop is
  fatal.
- `EVENT_DIRECTORY_PROPERTY_MUTATE` updates the active directory record.
- `EVENT_GLOBAL_PROPERTY_MUTATE` updates global raw property state on the draft.

Release-1 typed directory/global properties are:
- `INCLUDE_DIRECTORIES`
- `LINK_DIRECTORIES`
- `COMPILE_DEFINITIONS`
- `COMPILE_OPTIONS`
- `LINK_OPTIONS`
- `LINK_LIBRARIES`

Other directory/global properties are stored in a raw future-facing property
bag. They are not dropped silently.

### Target

- `EVENT_TARGET_DECLARE` creates a new target record with unique name.
- Duplicate target names are fatal.
- `owner_directory_id` is the current directory frame at declaration time.
- `alias`, `imported`, and `alias_of` are captured directly from the event.
- `EVENT_TARGET_ADD_SOURCE` appends a raw source path without hidden path
  normalization.
- `EVENT_TARGET_ADD_DEPENDENCY` appends an explicit dependency name reference.
- visibility-aware target events and `EVENT_TARGET_COMPILE_FEATURES` are stored
  as typed raw item entries without flattening transitive semantics
- `EVENT_TARGET_PROP_SET` updates typed scalar properties for the promoted
  scalar keys below and also promotes supported usage-requirement setters into
  the same typed item families used by the direct target events
- promoted `SET` operations replace only the matching logical subfamily inside a
  shared typed array; for example `INTERFACE_INCLUDE_DIRECTORIES` does not wipe
  `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES`
- unsupported target properties, custom metadata, and `APPEND_STRING` payloads
  remain in the raw property bag

Release-1 promoted scalar target property keys are:
- `OUTPUT_NAME`
- `PREFIX`
- `SUFFIX`
- `ARCHIVE_OUTPUT_DIRECTORY`
- `LIBRARY_OUTPUT_DIRECTORY`
- `RUNTIME_OUTPUT_DIRECTORY`
- `FOLDER`

Release-1 promoted usage-requirement target property keys are:
- `INCLUDE_DIRECTORIES`
- `INTERFACE_INCLUDE_DIRECTORIES`
- `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES`
- `COMPILE_DEFINITIONS`
- `INTERFACE_COMPILE_DEFINITIONS`
- `COMPILE_OPTIONS`
- `INTERFACE_COMPILE_OPTIONS`
- `COMPILE_FEATURES`
- `INTERFACE_COMPILE_FEATURES`
- `LINK_LIBRARIES`
- `INTERFACE_LINK_LIBRARIES`
- `LINK_OPTIONS`
- `INTERFACE_LINK_OPTIONS`
- `LINK_DIRECTORIES`
- `INTERFACE_LINK_DIRECTORIES`

Visibility-aware target events are stored as typed raw entries. The builder does
not flatten visibility or transitive effects during ingest.

### Tests

- `EVENT_TEST_ENABLE` updates the draft testing-enabled flag.
- `EVENT_TEST_ADD` creates a test record owned by the current directory frame.

### Replay Domain

- downstream-replayable events create replay-action draft records owned by the
  current directory frame
- replay actions store explicit phase, working-directory, argv, env, inputs,
  outputs, and provenance metadata
- the builder preserves committed event order and does not infer replay order
  later during freeze or query
- replay actions complement the existing build/install/export/package domains;
  they do not duplicate records that are already canonical elsewhere

### Install

- `EVENT_INSTALL_RULE_ADD` creates an install-rule record owned by the current
  directory frame.
- For target install rules, the raw `item` is stored as a symbolic target name
  to be resolved after validation.

### Package and CPack

- `EVENT_PACKAGE_FIND_RESULT` creates a package-result record owned by the
  current directory frame.
- `EVENT_CPACK_ADD_INSTALL_TYPE`, `EVENT_CPACK_ADD_COMPONENT_GROUP`, and
  `EVENT_CPACK_ADD_COMPONENT` append first-class CPack records.
- `depends` and `install_types` from component events are split using CMake-list
  semantics during ingest.

## 6. Storage Policy

- The builder copies any retained strings into its own arena.
- The builder stores typed raw entries for modifier-bearing properties
  (`visibility`, `is_before`, `is_system`) so that freeze/query can preserve
  semantics exactly.
- The builder never computes effective inherited or transitive results.

## 7. Diagnostics and Fatal Conditions

The builder emits diagnostics through `Diag_Sink`.

Fatal conditions include:
- unsupported build-semantic event kind
- duplicate target name
- malformed directory enter/leave stack
- allocation failure
- structurally impossible payload for a supported event

After a fatal condition:
- all subsequent apply calls return `false`
- `bm_builder_finalize(...)` returns `NULL`

## 8. Internal File Split

The implementation must keep domain handlers split:
- `build_model_builder_directory.c`
- `build_model_builder_project.c`
- `build_model_builder_target.c`
- `build_model_builder_test.c`
- `build_model_builder_replay.c`
- `build_model_builder_install.c`
- `build_model_builder_package.c`

The top-level `build_model_builder.c` is reserved for lifecycle, dispatch, and
shared helper glue only.
