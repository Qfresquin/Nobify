# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Nobify Glossary

## Event Role

Role metadata attached to each `Event_Kind` in Event IR. Roles define intended
consumers (`TRACE`, `DIAGNOSTIC`, `RUNTIME_EFFECT`, `BUILD_SEMANTIC`) without
creating multiple streams.

## Event Stream

Append-only ordered stream emitted by evaluator runs when `EvalExec_Request`
provides an event sink.

## Replay Action

Build-model downstream entity that records replayable actions not fully covered
by target/build-step/install/export/package domains.

## Replay Phase

Phase ownership marker for replay actions:
`configure`, `build`, `test`, `install`, `export`, `package`, `host-only`.

## Build_Model_Draft

Mutable reconstruction state written by the builder while ingesting Event IR.

## Build_Model

Frozen immutable model consumed by query helpers and codegen.

## Frozen Model

The immutable post-freeze representation of reconstructed semantics. This is
the only semantic layer codegen reads.

## Backend-Reject

Closure-harness status for a backend-owned variant that is currently expected
to fail explicitly with stable diagnostics.

## Evaluator-Only

Closure-harness status for behavior that is semantic-only and does not require
downstream replay to preserve product behavior.

## Explicit-Non-Goal

Closure-harness status for variants intentionally outside supported product
scope by explicit and justified decision.

## Skip-By-Tool

Closure-harness status used when a case is valid but the host lacks required
external tooling for execution.

## Artifact Parity

Proof objective where generated Nob output is compared against CMake 3.28
through observable artifacts and structured diffs.
