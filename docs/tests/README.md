# Tests Documentation Map

## Status

This directory is the canonical map for test architecture, suite ownership,
and active test-infrastructure roadmaps.

Current baseline architecture boundary:

`nob_test runner -> test framework -> shared support -> suites`

Active daemon-program target boundary:

`nob front door -> daemon client -> runner core -> suites`

## Overview

The test stack separates:

- aggregate-safe suites (`test-v2`) used as the default smoke baseline
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

Current baseline commands still run through `./build/nob_test` today:

- default smoke aggregate:
  `./build/nob_test test-v2`
- explicit artifact parity suite:
  `./build/nob_test test-artifact-parity`
- explicit closure harness suite:
  `./build/nob_test test-evaluator-codegen-diff`

The active daemon roadmap treats those commands as transitional and targets:

- `./build/nob test test-v2`
- `./build/nob test artifact-parity`
- `./build/nob test evaluator-codegen-diff`
- `./build/nob test watch <module>`
- `./build/nob test daemon start|stop|status`

`artifact-parity` and `evaluator-codegen-diff` remain outside default smoke
while they stay heavier and host-sensitive.

## Relationship To Product Contracts

- Closure roadmap:
  [`../evaluator_codegen_closure_roadmap.md`](../evaluator_codegen_closure_roadmap.md)
- Test daemon roadmap:
  [`./test_daemon_roadmap.md`](./test_daemon_roadmap.md)
- Generated runtime contract:
  [`../codegen/codegen_runtime_contract.md`](../codegen/codegen_runtime_contract.md)
- Build-model replay contract:
  [`../build_model/build_model_replay.md`](../build_model/build_model_replay.md)
