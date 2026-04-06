# Historical: Build Model Builder v2

> Historical note (March 8, 2026): this document is superseded by
> `docs/build_model/build_model_builder.md`.
> It is retained only as migration context while the canonical implementation
> moves to `src_v2/build_model/`.

## 1. Status

There is no active `src_v2` implementation of the build-model builder at the moment.

This document is a consumer contract for a future builder that will read the current `Event IR`. It is not a statement that `build_model_builder.c` already exists in the active pipeline.

## 2. Upstream Contract

The future builder consumes:
- one canonical `Event_Stream`
- only the subset of kinds marked with `EVENT_ROLE_BUILD_SEMANTIC`

Historical boundary note:
- the upstream evaluator architecture has moved to a target session/request
  design under `docs/evaluator/`
- this historical document must be read as a stream-consumer note only, not as
  a statement about evaluator public API shape

The builder must not depend on:
- evaluator-private variables such as `NOBIFY_GLOBAL_*`
- trace-only breadcrumbs
- diagnostic events for semantic reconstruction

## 3. Required Event Subset

Minimum build-semantic events the future builder is expected to consume:
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
- `EVENT_TEST_ENABLE`
- `EVENT_TEST_ADD`
- `EVENT_INSTALL_RULE_ADD`
- `EVENT_CPACK_ADD_INSTALL_TYPE`
- `EVENT_CPACK_ADD_COMPONENT_GROUP`
- `EVENT_CPACK_ADD_COMPONENT`
- `EVENT_PACKAGE_FIND_RESULT`

## 4. Directory Semantics

The builder must model directory state from first-class directory events, not from evaluator internals.

Required behavior:
- `EVENT_DIRECTORY_ENTER` pushes directory context
- `EVENT_DIRECTORY_LEAVE` pops directory context
- `EVENT_DIRECTORY_PROPERTY_MUTATE` updates the active directory frame
- targets declared after a directory mutation inherit the effective directory state according to builder policy

The builder should treat these properties as canonical inputs when present:
- `INCLUDE_DIRECTORIES`
- `LINK_DIRECTORIES`
- `COMPILE_DEFINITIONS`
- `COMPILE_OPTIONS`
- `LINK_OPTIONS`

The mutation payload provides:
- `property_name`
- `op`
- `modifier_flags`
- `items[]`

## 5. Trace and Diagnostics Policy

The builder may ignore all non-build-semantic events by default.

In particular, it should not require:
- `EVENT_COMMAND_BEGIN`
- `EVENT_COMMAND_END`
- `EVENT_INCLUDE_BEGIN`
- `EVENT_INCLUDE_END`
- `EVENT_ADD_SUBDIRECTORY_BEGIN`
- `EVENT_ADD_SUBDIRECTORY_END`
- `EVENT_DIAG`

Those events are useful for tooling, replay and debugging, but they are not the semantic source of truth for build reconstruction.

## 6. Planned Builder Shape

When implemented, the builder is expected to remain:
- incremental
- append-driven
- directory-scope aware
- the only writer of `Build_Model`

Expected interface shape:

```c
Build_Model_Builder *builder_create(Arena *arena, Diag_Sink *diags);
bool builder_apply_event(Build_Model_Builder *builder, const Event *ev);
Build_Model *builder_finish(Build_Model_Builder *builder);
```

The exact file layout and implementation details remain open until the builder returns to active scope.
