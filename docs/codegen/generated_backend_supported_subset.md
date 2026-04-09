# Generated Backend Supported Subset

## Status

This document is the canonical `C4` product claim for the generated backend.

It is evidence-backed by:

- `./build/nob test evaluator-codegen-diff`
- `./build/nob test artifact-parity`
- `./build/nob test artifact-parity-corpus`

Anything not listed here remains outside the supported claim even if the
evaluator implements it.

## Proven Phases And Families

The current supported generated-backend subset is limited to the proven
downstream path:

- configure replay for the deterministic filesystem and local host-effect
  subset landed in `C2`
- build graph execution for the supported target/build-step subset
- local-only `test` / baseline `ctest_*` / local `FetchContent_*` ownership
  landed in `C3`
- install, export, and archive package execution already proven by the
  explicit parity harness

This claim is intentionally phase-and-family based. It is not a blanket claim
for the full CMake 3.28 universe.

## Supported Real Projects

The pinned real-project corpus currently supports these projects through
build/install/consumer proof:

- `fmt`
  Imported target expectation: `fmt::fmt`
- `pugixml`
  Imported target expectation: `pugixml::pugixml`
- `nlohmann_json`
  Imported target expectation: `nlohmann_json::nlohmann_json`
- `cjson`
  Imported target expectation: `cjson`

These projects are the current real-project scope of the supported claim.
Unlisted projects are outside the `C4` product boundary by default.

## Explicit Boundaries

The supported subset does not currently claim:

- generic process replay such as `execute_process()` and `exec_program()`
- positive generated-backend probe execution for `try_compile()` and `try_run()`
- remote/provider/VCS/custom-command `FetchContent_*`
- dashboard/script/network `ctest_*` variants outside the local-only subset
- installer generators and unsupported component packaging variants
- the full CMake 3.28 command universe beyond the explicit closure inventory

Those surfaces must stay explicit in the closure harness as `backend-reject`
or explicit product boundaries until new proof lands.
