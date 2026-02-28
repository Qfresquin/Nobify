# Evaluator v2 CMake 3.28 Compatibility Architecture (Permissive-First)

Status: Normative architecture and implementation contract for distributed usage.
Baseline: CMake 3.28.x language semantics.
Primary goal: maximize accepted input surface without collapsing project-wide evaluation.

## 1. Scope and Intent

This document defines the compatibility runtime architecture for `src_v2/evaluator`.

It applies to:
- Parser integration and AST recovery behavior.
- Evaluator semantic execution and flow flattening.
- Policy governance and compatibility defaults.
- Diagnostic classification and fail/continue decisions.
- Event stream resiliency for distributed scenarios.

It does not redefine Build Model semantics; it defines evaluator-side behavior and contracts.

## 2. Compatibility Principles

1. Input-first resilience.
- Invalid local inputs should not fail the whole project by default.

2. CMake-point fidelity.
- For every command/subcommand, behavior must be labeled FULL, PARTIAL, or MISSING against CMake 3.28.

3. Explicit divergence.
- Any deviation from CMake semantics must emit a classified diagnostic and a deterministic fallback.

4. Fatal boundary clarity.
- Engine integrity failures (OOM, broken invariants) are always hard-stop.

5. Deterministic output.
- Same input + profile + environment must produce the same diagnostic/report profile.

## 3. Runtime Profiles

Public enum: `Eval_Compat_Profile`
- `EVAL_PROFILE_PERMISSIVE` (default)
- `EVAL_PROFILE_STRICT`
- `EVAL_PROFILE_CI_STRICT`

Control variable:
- `CMAKE_NOBIFY_COMPAT_PROFILE` = `PERMISSIVE|STRICT|CI_STRICT`

Profile semantics:
- `PERMISSIVE`:
  - Keep evaluating after non-fatal semantic/input errors.
  - Honor `CMAKE_NOBIFY_ERROR_BUDGET`.
- `STRICT`:
  - Promote warnings to errors.
  - Stop on evaluator error unless explicit continue policy overrides.
- `CI_STRICT`:
  - Same as strict, intended for reproducible CI gates.

## 4. Diagnostic Model

Public enums:
- `Eval_Diag_Code`
  - `EVAL_ERR_PARSE`
  - `EVAL_ERR_SEMANTIC`
  - `EVAL_ERR_UNSUPPORTED`
  - `EVAL_WARN_LEGACY`
- `Eval_Error_Class`
  - `INPUT_ERROR`
  - `ENGINE_LIMITATION`
  - `IO_ENV_ERROR`
  - `POLICY_CONFLICT`

Every `EV_DIAGNOSTIC` event must carry:
- severity
- component
- command
- `code`
- `error_class`
- cause
- hint

Classification point:
- Single point in evaluator (`eval_emit_diag`) classifies and records.
- Classification table implementation lives in `src_v2/evaluator/eval_diag_classify.c`.

## 5. Failure Decision Matrix

### 5.1 Classes

- `INPUT_ERROR`:
  - malformed command usage, invalid options, local semantic misuse.
- `ENGINE_LIMITATION`:
  - unsupported command/subcommand or unimplemented behavior.
- `IO_ENV_ERROR`:
  - file/network/environment constraints not matching command expectation.
- `POLICY_CONFLICT`:
  - policy-sensitive behavior conflicts or ambiguous policy state.

### 5.2 Actions by Profile

- `PERMISSIVE`:
  - `INPUT_ERROR`: `ERROR_CONTINUE`
  - `ENGINE_LIMITATION`: `ERROR_CONTINUE` (or `NOOP_WARN` when safe)
  - `IO_ENV_ERROR`: `ERROR_CONTINUE`
  - `POLICY_CONFLICT`: `ERROR_CONTINUE`
  - Stop only when:
    - OOM/integrity failure, or
    - `error_count >= CMAKE_NOBIFY_ERROR_BUDGET` (if budget > 0)

- `STRICT` / `CI_STRICT`:
  - warnings promoted to errors
  - default `ERROR_STOP` for evaluator errors
  - explicit continue policy may relax only if configured

### 5.3 Always Fatal

- arena allocation failure/OOM
- internal invariant corruption
- unrecoverable state desynchronization

## 6. Unsupported Command Policy

Public enum: `Eval_Unsupported_Policy`
- `EVAL_UNSUPPORTED_WARN`
- `EVAL_UNSUPPORTED_ERROR`
- `EVAL_UNSUPPORTED_NOOP_WARN`

Control variable:
- `CMAKE_NOBIFY_UNSUPPORTED_POLICY` = `WARN|ERROR|NOOP_WARN`

Behavior:
- Unknown commands route through dispatcher fallback.
- Severity and fallback are selected by unsupported policy.

## 7. Error Budget and Continuation Controls

Variables:
- `CMAKE_NOBIFY_ERROR_BUDGET`
  - `0` means unlimited budget in permissive mode.
- `CMAKE_NOBIFY_CONTINUE_ON_ERROR`
  - compatibility control for legacy behavior.

Default bootstrap:
- profile `PERMISSIVE`
- unsupported policy `WARN`
- error budget `0`

## 8. Run Report Contract

Public struct: `Eval_Run_Report`
- warning count
- error count
- class counters (`input`, `engine`, `io_env`, `policy`)
- unsupported counter
- overall status:
  - `EVAL_RUN_OK`
  - `EVAL_RUN_OK_WITH_WARNINGS`
  - `EVAL_RUN_OK_WITH_ERRORS`
  - `EVAL_RUN_FATAL`

Public API:
- `evaluator_get_run_report()`
- `evaluator_get_run_report_snapshot()`
- `evaluator_set_compat_profile(...)`

Intended usage:
- CI/fleet collectors consume report summary without parsing raw logs.

## 9. Command Capability Contract

Public model:
- `Eval_Command_Impl_Level` = `FULL|PARTIAL|MISSING`
- `Eval_Command_Fallback` = `NOOP_WARN|ERROR_CONTINUE|ERROR_STOP`
- `Command_Capability`

Public API:
- `evaluator_get_command_capability()`

Rules:
- All known commands return capability.
- Missing commands return `MISSING` + policy-driven fallback.
- PARTIAL commands must document deltas in coverage docs.

## 10. Block Definitions Compliance (Mandatory)

### 10.1 function/endfunction

Contract:
- Creates variable scope.
- Binds `ARGC`, `ARGV`, `ARGN`, `ARGV<n>`.
- Supports `return()` and `return(PROPAGATE ...)`.
- Propagation copies selected variables to parent function caller scope.

Policy behavior:
- Policy scope behavior must be explicit in implementation notes.
- Any non-CMake-equivalent edge case emits classified diagnostic.

Permissive behavior:
- malformed function-local behavior emits `INPUT_ERROR` and continues unless fatal.

### 10.2 macro/endmacro

Contract:
- Executes in caller variable scope semantics (textual-style substitution model).
- Macro argument bindings are maintained per macro frame.

Legacy compatibility:
- `return()` in macro is accepted for compatibility and emits `EVAL_WARN_LEGACY`.

### 10.3 end* validation

Commands:
- `endif(...)`, `endforeach(...)`, `endwhile(...)`, `endfunction(...)`, `endmacro(...)`

Rules:
- Signature mismatch emits diagnostic.
- Profile decides severity impact (warning promotion in strict profiles).
- Recovery in permissive mode continues evaluation.

### 10.4 Fallback taxonomy for block definitions

- `NOOP_WARN`: safe ignored closure/argument extras.
- `ERROR_CONTINUE`: recoverable semantic mismatch.
- `ERROR_STOP`: only when parser/evaluator integrity is threatened.

## 11. CMake Policy Governance

Baseline policy source:
- CMake 3.28.

Governance rules:
- Policy stack must be deterministic and scope-aware.
- Unset policy defaults must be documented per policy.
- Policy-dependent flow behavior must be explicitly listed.

Initial required policies for flow/block governance:
- `CMP0124` (foreach loop variable behavior).
- Additional flow/block-relevant policies to be tracked in compatibility matrix.

## 12. Layered Execution Architecture

Layer A: Parser/AST recovery
- preserve origin metadata
- recover from local syntax where feasible

Layer B: Evaluator semantic runtime
- flow, scope, policy stack, command handlers

Layer C: Diagnostics and telemetry
- classify every diagnostic by code/class
- aggregate run report and unsupported telemetry

Layer D: Resilient event stream
- append valid events even when local command failures occur

Current module split (implemented):
- Runtime/orchestration core: `src_v2/evaluator/evaluator.c`
- Compatibility profile and stop/continue decisions: `src_v2/evaluator/eval_compat.c`
- Diagnostic classification table: `src_v2/evaluator/eval_diag_classify.c`
- Policy engine stack/default resolution: `src_v2/evaluator/eval_policy_engine.c`
- Command capability registry: `src_v2/evaluator/eval_command_caps.c`
- Run report aggregation/finalization: `src_v2/evaluator/eval_report.c`

## 13. Compliance Matrix Requirements

Coverage artifact must include for each command/subcommand:
- implemented level (`FULL|PARTIAL|MISSING`)
- current evaluator behavior
- expected CMake 3.28 behavior
- divergence summary
- fallback behavior
- diagnostic code/class used on divergence

For PARTIAL entries, at least one explicit delta example is required.

## 14. Testing Requirements

1. Semantic conformance
- valid CMake 3.28 scenarios for function/macro/return/end*.

2. Permissive robustness
- invalid command-level inputs continue evaluation.

3. Strict behavior
- same invalid input fails per strict profile policy.

4. Diagnostics integrity
- every emitted diagnostic carries non-empty `code` and `error_class`.

5. Report integrity
- `Eval_Run_Report` counters match emitted diagnostics.

6. Regression
- existing evaluator suite remains green with profile defaults.

## 15. Rollout Plan

Phase 1: Documentation and type contracts
- publish this architecture
- expose profile/report/capability public types and APIs

Phase 2: Centralized diagnostic classification
- enforce code/class assignment in one emission path

Phase 3: Profile enforcement and budgeted continuation
- permissive default, strict variants

Phase 4: Capability matrix and command fallback normalization
- central dispatcher capability mapping

Phase 5: Block definitions hardening
- close remaining gaps and policy-specific edge cases

Phase 6: Coverage and telemetry maturation
- finalize FULL/PARTIAL/MISSING + fallback mapping for evaluator command set

## 16. Acceptance Criteria

1. Architecture document exists and is normative.
2. Default runtime profile is permissive and deterministic.
3. Every diagnostic event includes code/class metadata.
4. Unsupported command handling is policy-driven.
5. Run report is available programmatically.
6. Capability query API returns deterministic command compatibility metadata.
7. Block definitions behavior and fallback rules are explicitly documented and testable.
