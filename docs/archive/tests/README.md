# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Tests Documentation Map

## Status

This directory is the canonical map for test architecture, suite ownership,
and active test-infrastructure roadmaps.

`nob front door -> daemon client/supervisor -> reactor daemon -> runner core -> suites`

## Overview

The test stack separates:

- aggregate-safe suites used as the default smoke baseline
- explicit-only host-sensitive suites used for heavy parity/closure proof

This documentation defines test ownership and execution policy. It does not
redefine evaluator, Event IR, build-model, or codegen product contracts.

## Normative Test Contracts

- [Tests architecture](./tests_architecture.md)
- [Evaluator to codegen diff contract](./evaluator_codegen_diff.md)

## Active Test Roadmap

- [Tests structural refactor plan](./tests_structural_refactor_plan.md)
- [Test daemon fast-track roadmap](./test_daemon_roadmap.md)

## Execution Policy Summary

Current supported human-facing baseline commands now run through `./build/nob test`:

- default smoke aggregate:
  `./build/nob test`
  `./build/nob test smoke`
- front-doored utility commands:
  `./build/nob test clean`
  `./build/nob test tidy all`
  `./build/nob test tidy <module>`
- watch mode:
  `./build/nob test watch <module>`
  `./build/nob test watch auto`
- daemon lifecycle:
  `./build/nob test daemon start`
  `./build/nob test daemon stop`
  `./build/nob test daemon status`
- explicit artifact parity suite:
  `./build/nob test artifact-parity`
- explicit real-project corpus suite:
  `./build/nob test artifact-parity-corpus`
- explicit closure harness suite:
  `./build/nob test evaluator-codegen-diff`

The current docs-only release gate for the generated-backend claim is:

- `./build/nob test evaluator-codegen-diff`
- `./build/nob test artifact-parity`
- `./build/nob test artifact-parity-corpus`

The active daemon roadmap now lands its T6 surface on:

- `./build/nob test`
- `./build/nob test smoke`
- `./build/nob test clean`
- `./build/nob test tidy all`
- `./build/nob test tidy <module>`
- `./build/nob test watch <module>`
- `./build/nob test watch auto`
- `./build/nob test daemon start|stop|status`
- `./build/nob test artifact-parity`
- `./build/nob test artifact-parity-corpus`
- `./build/nob test evaluator-codegen-diff`
- daemon front-door profile flags:
  `--verbose`, `--asan`, `--ubsan`, `--msan`, `--san`, `--cov`

That daemon target is intentionally Linux-first and now freezes watch routing,
cancellation, fast local feedback, and compact failure-first watch ergonomics
as part of the same program, not as separate later add-ons.

Current T6 limitation:
- watch sessions are foreground and attached to the active client; there is no
  persistent detached watch session surface yet.
- default watch mode is compact and failure-first; use `--verbose` to print
  roots, full routed path/module sets, and per-rerun fast-path detail.

`artifact-parity`, `artifact-parity-corpus`, and `evaluator-codegen-diff`
remain outside default smoke while they stay heavier and host-sensitive.

## Relationship To Product Contracts

- Closure roadmap:
  [`../evaluator_codegen_closure_roadmap.md`](../evaluator_codegen_closure_roadmap.md)
- Test daemon roadmap:
  [`./test_daemon_roadmap.md`](./test_daemon_roadmap.md)
- Generated runtime contract:
  [`../codegen/codegen_runtime_contract.md`](../codegen/codegen_runtime_contract.md)
- Generated backend supported subset:
  [`../codegen/generated_backend_supported_subset.md`](../codegen/generated_backend_supported_subset.md)
- Build-model replay contract:
  [`../build_model/build_model_replay.md`](../build_model/build_model_replay.md)
