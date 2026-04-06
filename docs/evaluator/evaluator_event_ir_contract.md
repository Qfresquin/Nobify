# Evaluator Event IR Contract

Status: Canonical Target. This document defines the target evaluator-to-Event-IR
boundary.

## 1. Scope

This document covers:
- when Event IR is projected,
- event ordering guarantees,
- ownership expectations,
- command tracing guarantees,
- semantic vs diagnostic projection rules.

The shared Event IR schema itself is defined in
[`../transpiler/event_ir_v2_spec.md`](../transpiler/event_ir_v2_spec.md).

## 2. Boundary

The canonical evaluator boundary is:

`Ast_Root -> execution pipeline -> semantic state mutation -> Event IR/result projection`

Target API consequence:
- the canonical producer entry point is
  `eval_session_run(EvalSession *, const EvalExec_Request *, Ast_Root)`
- `Event_Stream` is supplied per run through `EvalExec_Request`
- `EvalExec_Request.stream` enables or disables Event IR projection for that
  run
- `Event_Stream` is optional
- `EvalRunResult` is always returned, regardless of whether a stream is present

The evaluator does not require `stream != NULL` in order to execute.

## 2.1 Data Flow

Canonical projection flow:

`Ast_Root -> execution transaction -> committed semantic state -> Event IR projection`

This flow is commit-driven. Event IR is derived from committed mutations and
committed effect records.

## 3. Projection Timing

Event IR is projected from committed semantic mutations.

This means:
- validation failures do not emit success-shaped semantic events,
- failed command application does not leave partially projected semantic state,
- diagnostics and trace events may still be produced for failed commands when a
  stream is present,
- semantic events must reflect canonical session state after commit.

## 4. Ordering Guarantees

When a stream is present, the evaluator guarantees:
- append-only event ordering,
- stable command tracing order for dispatched commands,
- deterministic sequencing for committed semantic events within one command
  transaction,
- explicit directory/file nesting order derived from execution contexts.

The evaluator must not reorder committed semantic events across command
boundaries.

## 5. Event Categories

The evaluator may project several categories of events, including:
- command tracing
- diagnostics
- runtime-effect events
- build-semantic events
- evaluator-semantic property/query events when modeled by the Event IR schema

The build model consumes only the relevant semantic subset. The evaluator still
owns the full projection rules for the stream it emits.

Closure-program rule:
- some effects that historically existed only as evaluator-local runtime
  behavior may become downstream-consumable
- when that happens, the evaluator still projects them from committed state or
  committed effect records
- the transition is expressed by the Event IR contract and downstream
  build-model ingestion rules, not by exposing evaluator-private state to
  codegen
- downstream replayability does not change the command-trace contract or the
  requirement that failed commands not leave success-shaped semantic events

## 6. Ownership

The caller owns `Event_Stream`.

The Event IR layer owns event payload storage after `event_stream_push(...)`
deep-copies the event payload.

The evaluator must not:
- retain borrowed pointers into caller-owned event buffers,
- assume the stream exists for every run,
- require downstream consumers to query evaluator-private state in order to
  interpret emitted events.

## 7. Command Tracing

When a stream is present, dispatched commands must emit consistent command
framing for:
- successful execution,
- soft-error execution,
- unknown-command paths,
- fatal diagnostic paths where the run still has a visible trace boundary.

Trace projection is a run output concern. It is not the canonical semantic
source of truth.

## 8. Downstream Contract

Downstream consumers, including the build model, must depend only on:
- the Event IR schema,
- event kind metadata,
- event ordering guarantees,
- documented semantic roles.

If a downstream document needs to name the evaluator boundary, it should refer
to:
- `EvalSession`
- `EvalExec_Request`
- `EvalRunResult`

They must not depend on:
- `Evaluator_Context`,
- `Evaluator_Init`,
- hidden evaluator variable state,
- legacy create/run API details.

For the closure program this also means:
- configure-time and host-effect families may become backend-owned through
  canonical Event IR plus build-model contracts
- no downstream consumer may read raw evaluator service state or hidden command
  transactions to recover that behavior

## 9. Public Contract

The stable public contract of this document is:

- when Event IR projection is enabled (`EvalExec_Request.stream`)
- how ordering and commit timing behave
- how downstream consumers must interpret projected events without evaluator
  internals

## 10. Non-goals

- defining the full Event IR schema (owned by
  `../transpiler/event_ir_v2_spec.md`)
- defining build-model ingest behavior (owned by `../build_model/*`)
- replacing evaluator runtime-model ownership rules

## 11. Evidence

Contract evidence is expected from:

- evaluator tests covering projection timing and command trace boundaries
- pipeline/build-model tests that consume projected build-semantic events
- closure-harness classification when runtime effects become downstream-owned
