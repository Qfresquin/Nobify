# Tests Documentation Map

## Status

This directory is the canonical map for test architecture and suite ownership.

Architecture boundary:

`nob_test runner -> test framework -> shared support -> suites`

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

## Execution Policy Summary

- default smoke aggregate:
  `./build/nob_test test-v2`
- explicit artifact parity suite:
  `./build/nob_test test-artifact-parity`
- explicit closure harness suite:
  `./build/nob_test test-evaluator-codegen-diff`

`artifact-parity` and `evaluator-codegen-diff` remain outside default smoke
while they stay heavier and host-sensitive.

## Relationship To Product Contracts

- Closure roadmap:
  [`../evaluator_codegen_closure_roadmap.md`](../evaluator_codegen_closure_roadmap.md)
- Generated runtime contract:
  [`../codegen/codegen_runtime_contract.md`](../codegen/codegen_runtime_contract.md)
- Build-model replay contract:
  [`../build_model/build_model_replay.md`](../build_model/build_model_replay.md)
