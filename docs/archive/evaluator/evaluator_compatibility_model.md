# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Evaluator Compatibility Model

Status: Canonical Target. This document defines the target compatibility and
policy architecture for evaluator execution.

## 1. Scope

This document covers:
- compatibility profile ownership,
- policy state,
- execution-mode compatibility,
- unsupported-command behavior,
- error-budget and continue-on-error rules,
- script-visible compatibility projections.

## 2. Ownership Model

Compatibility is session-scoped.

The session owns:
- the active compatibility profile,
- policy baseline and overrides,
- unsupported-command policy defaults,
- error-budget configuration,
- continue-on-error behavior,
- compatibility metadata that needs to survive across runs.

Execution contexts may hold request-local or frame-local views of that state,
but they do not redefine the owning compatibility source of truth.

## 3. Compatibility Profiles

Target profiles remain the main coarse-grained control:
- permissive
- strict
- CI-strict

The session profile shapes:
- default diagnostic severities,
- unknown-command behavior,
- unsupported feature handling,
- tolerance for recoverable divergences.

Changing the profile is a session operation, not an incidental consequence of
mutating arbitrary runtime fields.

## 4. Policy Engine

The CMake policy engine is a separate but related axis.

The target architecture requires:
- session ownership of the base policy state,
- frame-visible policy views,
- explicit policy snapshots at directory and execution boundaries,
- deterministic policy lookup during validation and semantic application.

Policy state must not depend on hidden refresh passes over unrelated variable
storage.

## 5. Execution Modes

Compatibility decisions depend on more than the profile alone.

Required execution-mode inputs include:
- project mode vs script mode,
- try-compile and try-run child execution,
- platform/generator/toolchain characteristics,
- whether a command is evaluated in a nested directory or file context.

These mode flags are part of the runtime model and must be available during
validation.

## 6. Script-Visible Compatibility Variables

Compatibility variables exposed to CMake code are projections over typed
compatibility state.

Target rule:
- evaluator semantics are not controlled by rereading arbitrary variable maps
  at unpredictable times

If script-level variables are allowed to influence compatibility, the runtime
must translate them into typed compatibility updates at explicit pipeline
boundaries.

## 7. Unsupported Commands and Soft Divergences

Unknown or unsupported behavior is handled through compatibility policy and
stable diagnostics.

The target architecture requires:
- explicit unsupported-command policy,
- stable diagnostic codes for unsupported features,
- deterministic run-report accounting,
- no hidden severity changes outside compatibility or diagnostics services.

## 8. Relationship to Current Implementation

The current implementation may still refresh some compatibility settings from
variables or context state. That behavior is implementation-current and must be
tracked in audit documents.

The target contract is session-owned compatibility state with explicit
pipeline-boundary updates.
