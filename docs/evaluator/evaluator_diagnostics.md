# Evaluator Diagnostics

Status: Canonical Target. This document defines the target evaluator
diagnostics and run-report model.

## 1. Scope

This document covers:
- diagnostic code ownership,
- severity shaping,
- diagnostics projection,
- run-report ownership,
- relationship between diagnostics and compatibility.

## 2. Diagnostics Service

The target architecture uses a diagnostics service instead of ad hoc emission
from arbitrary stateful helpers.

The diagnostics service consumes:
- stable evaluator diagnostic codes,
- default severity metadata,
- compatibility and policy state,
- command origin,
- execution context metadata,
- validation and apply failures.

This service is the canonical owner of evaluator diagnostic shaping.

## 3. Diagnostic Codes

Diagnostic codes remain explicit and stable.

Each diagnostic code defines:
- stable machine-readable identity,
- default severity,
- evaluator error class,
- whether it contributes to run-report counters,
- expected hint and remediation category.

Call sites must emit diagnostics by code, not by formatting arbitrary text and
then trying to infer severity later.

## 4. Severity Shaping

Final severity is shaped by:
- the diagnostic code metadata,
- session compatibility profile,
- policy state,
- execution mode,
- explicit command-local fatality rules where CMake requires them.

Severity shaping is deterministic and centralized.

## 5. Projection Rules

Diagnostics may be projected to:
- `EvalRunResult.report`
- optional `Event_Stream` diagnostic events
- optional user-facing sinks built on top of the same diagnostic service

Projection targets are outputs. The canonical diagnostic fact is the evaluated
diagnostic record inside the run.

## 6. Run Report

`EvalRunResult.report` is the canonical run summary.

It includes at least:
- overall result classification,
- error and warning accounting,
- unsupported-command or unsupported-feature accounting,
- execution-summary counters needed by tooling and tests.

The run report is returned directly from execution. It is not meant to be
reconstructed by probing hidden evaluator state after the fact.

## 7. Failure Model

The target diagnostics model distinguishes:
- soft errors that still permit continued execution,
- fatal diagnostics that stop the active run,
- service/invariant failures that immediately produce fatal stop.

These outcomes are reflected both in `EvalRunResult.result` and in the report
contents.

## 8. Relationship to Current Implementation

The current implementation may still expose helper families tied to
`Evaluator_Context` and a later report query API. Those are implementation
details or temporary shims during migration.

The target contract is diagnostics-service plus `EvalRunResult.report`.
