# Evaluator v2 Compatibility Architecture (Permissive-First)

Status: Normative compatibility architecture for current evaluator v2 runtime.

## 1. Intent

This document defines how evaluator v2 handles:
- compatibility profiles
- diagnostic classification and severity shaping
- unsupported-command policy
- stop/continue decisions
- run reporting and command capability exposure

It does not redefine Build Model semantics.

## 2. Principles

- Deterministic behavior for same input/profile/environment.
- Explicit compatibility controls via evaluator variables.
- Diagnostics as structured data (`EV_DIAGNOSTIC`) rather than log-only behavior.
- Permissive-first default with strict profiles available.
- Fatal stop only for unrecoverable runtime conditions or profile/policy-triggered stop decisions.

## 3. Runtime Controls

Runtime variables currently consumed by evaluator:
- `CMAKE_NOBIFY_COMPAT_PROFILE` (`PERMISSIVE|STRICT|CI_STRICT`)
- `CMAKE_NOBIFY_UNSUPPORTED_POLICY` (`WARN|ERROR|NOOP_WARN`)
- `CMAKE_NOBIFY_ERROR_BUDGET` (size_t; `0` means unlimited under permissive profile)
- `CMAKE_NOBIFY_CONTINUE_ON_ERROR`

Default bootstrap in evaluator creation:
- profile: `PERMISSIVE`
- unsupported policy: `WARN`
- error budget: `0`

## 4. Profile Model

Public enum: `Eval_Compat_Profile`
- `EVAL_PROFILE_PERMISSIVE`
- `EVAL_PROFILE_STRICT`
- `EVAL_PROFILE_CI_STRICT`

Effective severity behavior:
- In `STRICT` and `CI_STRICT`, warnings are promoted to errors.
- In `PERMISSIVE`, warning/error severities are preserved.

Stop decision behavior:
- `PERMISSIVE`: continue unless explicit stop is requested or error budget threshold is reached.
- `STRICT`/`CI_STRICT`: evaluator typically stops on error via compatibility decision path.

## 5. Unsupported Command Policy

Public enum: `Eval_Unsupported_Policy`
- `EVAL_UNSUPPORTED_WARN`
- `EVAL_UNSUPPORTED_ERROR`
- `EVAL_UNSUPPORTED_NOOP_WARN`

Unknown-command fallback behavior:
- policy determines diagnostic severity/hint.
- unresolved command is ignored as no-op behaviorally.
- user-defined function/macro resolution is attempted before unsupported fallback.

## 6. Diagnostic Model

Diagnostics are emitted through a centralized path (`eval_emit_diag`).

Each diagnostic event carries:
- severity
- component
- command
- classification fields: `code`, `error_class`
- cause and hint

Public classification enums:
- `Eval_Diag_Code`: `EVAL_ERR_PARSE`, `EVAL_ERR_SEMANTIC`, `EVAL_ERR_UNSUPPORTED`, `EVAL_WARN_LEGACY`, `EVAL_ERR_NONE`
- `Eval_Error_Class`: `INPUT_ERROR`, `ENGINE_LIMITATION`, `IO_ENV_ERROR`, `POLICY_CONFLICT`, `NONE`

Classification implementation is centralized in `eval_diag_classify.c`.

## 7. Run Report Contract

Public struct: `Eval_Run_Report`
- warning/error counts
- class counters (`input`, `engine`, `io_env`, `policy`)
- unsupported counter
- overall status:
- `EVAL_RUN_OK`
- `EVAL_RUN_OK_WITH_WARNINGS`
- `EVAL_RUN_OK_WITH_ERRORS`
- `EVAL_RUN_FATAL`

Public APIs:
- `evaluator_get_run_report(...)`
- `evaluator_get_run_report_snapshot(...)`

Report counters are updated on diagnostic emission path.

## 8. Command Capability Contract

Capability metadata is centralized in `eval_command_caps.c`.

Public fields:
- `command_name`
- `implemented_level` (`FULL|PARTIAL|MISSING`)
- `fallback_behavior` (`NOOP_WARN|ERROR_CONTINUE|ERROR_STOP`)

Public API:
- `evaluator_get_command_capability(...)`

Coverage status document must remain aligned with this registry.

## 9. Policy Governance (Current)

Current policy engine foundation covers CMake 3.28 command-level policy mechanics:
- complete known-policy registry (`CMP0000..CMP0155`) with intro-version metadata
- `cmake_policy(VERSION|SET|GET|PUSH|POP)` with strict arity and known-policy validation
- internal policy stack state (independent from variable-scope stack) with level shadowing support (`UNSET` at upper level blocks fallback)
- `cmake_minimum_required(VERSION ...)` implicit policy-version application aligned with the same policy core

Compatibility note:
- command-level policy framework is complete for baseline 3.28, but behavior changes gated by each policy in other commands remain covered by those commands' own compatibility entries.

## 10. Fatal vs Recoverable Boundaries

Always-fatal style conditions include:
- allocation failures/OOM
- unrecoverable stop state

Recoverable semantic/input incompatibilities normally emit diagnostics and follow profile/policy stop rules.

## 11. Testing Expectations for This Architecture

At minimum, evaluator tests should validate:
- profile severity shaping
- unsupported-command policy behavior
- non-empty diagnostic classification fields
- report counters/status coherence with emitted diagnostics
- command capability API determinism

## 12. Roadmap (Not Yet Implemented)

Roadmap, not current guarantees:
- broader semantic policy modeling beyond current focus.
- deeper command-level fallback customization by error class.
- expanded compatibility telemetry dimensions.
