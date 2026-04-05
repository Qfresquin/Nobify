# Tests Documentation Map

## Status

This directory contains the canonical baseline and active structural roadmap for
the v2 test stack.

The current test architecture boundary is:

`nob_test runner -> test framework -> shared support -> suites`

This documentation is about **test architecture** only. It does not redefine
lexer, parser, evaluator, build-model, pipeline, or codegen product semantics.

## Preserved Strengths

The current stack already provides important operational behavior that future
refactors must preserve:

- the official `./build/nob_test` runner as the command entrypoint
- isolated suite and per-case workspaces
- sanitizer, coverage, and `clang-tidy` profiles owned by the runner
- incremental test builds and captured stdout/stderr logs
- preserved failed workspaces for debugging

## Artifact Parity Harness

The explicit `P0` artifact-parity harness lives under `test_v2/artifact_parity/`.

- ownership:
  runner-owned tool/env setup in `src_v2/build/nob_test.c`
  suite-owned parity fixtures and manifest assertions in `test_v2/artifact_parity/`
- `P1` proof split:
  `test_v2/codegen/` owns aggregate-safe out-of-source smoke coverage
  `test_v2/artifact_parity/` stays explicit-only and owns real-CMake
  end-to-end parity checks
- `P2` proof split:
  `test_v2/pipeline/` snapshots build-graph golden coverage
  `test_v2/build_model/` locks frozen build-step/query semantics
  `test_v2/codegen/` owns aggregate-safe generated-file and hook scheduling
  smoke
  `test_v2/artifact_parity/` stays explicit-only and owns real-CMake
  parity checks for generated sources, custom-target dependencies, and
  post-build sidecar outputs
- `P3` proof split:
  `test_v2/build_model/` locks context-aware query evaluation, imported-target
  resolution, and shared compile-feature floors
  `test_v2/codegen/` owns aggregate-safe smoke for mixed-language usage
  propagation, imported prebuilt libraries, config-sensitive link selection,
  and `TARGET_FILE*` build-step argv expansion
  `test_v2/artifact_parity/` stays explicit-only and owns real-CMake parity
  checks for imported prebuilt libraries, `--config`-driven link selection,
  and usage propagation through `BUILD_INTERFACE`, `LINK_ONLY`, and
  `TARGET_PROPERTY`
- `P4` proof split:
  `test_v2/build_model/` locks platform-aware query context behavior,
  including `$<PLATFORM_ID:...>` and typed `WIN32_EXECUTABLE` /
  `MACOSX_BUNDLE` accessors
  `test_v2/codegen/` owns aggregate-safe Linux/POSIX execution plus
  render-only policy coverage for explicit generation targets
  `linux + posix`, `darwin + posix`, and `windows + win32-msvc`
  `test_v2/artifact_parity/` stays explicit-only and remains Linux/POSIX-only
  for execution parity, now invoking `nobify` explicitly with
  `--platform linux --backend posix`
- `P5` proof split:
  `test_v2/build_model/` locks effective install/export components and typed
  install metadata for artifact-specific destinations, export association, and
  default-component fallback
  `test_v2/codegen/` owns aggregate-safe Linux/POSIX install smoke for custom
  prefixes, component-selective installs, `PROGRAMS` executability,
  `DIRECTORY` slash semantics, `PUBLIC_HEADER`, installed export emission, and
  `clean` preserving custom install prefixes
  `test_v2/artifact_parity/` stays explicit-only and owns real-CMake install
  parity with per-case CMake/Nob `--prefix` and `--component` control
  `test_v2/artifact_parity/real_projects/` stays explicit-only and now runs
  full-install parity plus consumer-against-installed-prefix checks with
  explicit generated-Nob install prefixes for the pinned corpus projects
- `P6` proof split:
  `test_v2/evaluator/` locks standalone export lowering and the
  `enable_export_host_effects` gate
  `test_v2/build_model/` locks generalized export-domain queries for
  build-tree and package-registry exports
  `test_v2/pipeline/` snapshots standalone export events and resolved export
  membership in the evaluator-to-build-model path
  `test_v2/codegen/` owns aggregate-safe smoke for explicit generated-Nob
  `export` execution, including build-tree export files, package-registry
  writes, and rejection of unsupported `APPEND` /
  `CXX_MODULES_DIRECTORY` forms
  `test_v2/artifact_parity/` stays explicit-only and owns real-CMake parity
  plus downstream-consumer proof for `export(TARGETS ...)`,
  `export(EXPORT ...)`, and `export(PACKAGE ...)`
- required external tools:
  real `cmake 3.28.x`
  sibling `cpack` only for package-phase cases
  runner-provided `CMK2NOB_TEST_NOBIFY_BIN` so the suite uses a freshly built
  `nobify` from the current workspace sources
  generated Nob runtime tool precedence is
  `NOB_CMAKE_BIN` / `NOB_CPACK_BIN` -> embedded absolute path from `nobify` ->
  bare `cmake` / `cpack`
  generated Nob install execution is now explicit through
  `install [--prefix <path>] [--component <name>]`; omitting `--component`
  means full install
  generated Nob standalone export execution is now explicit through `export`,
  which honors the same global `--config <cfg>` selection used by `build`
  generated Nob config selection is explicit through `--config <cfg>`; the
  empty config remains the default when the flag is omitted
  generation-time platform/backend selection is explicit through
  `nobify --platform ... --backend ...`; support tiers are:
  `linux + posix` supported
  `darwin + posix` render-only
  `windows + win32-msvc` render-only
- execution policy:
  explicit-only via `./build/nob_test test-artifact-parity`
  not part of the default `./build/nob_test test-v2` smoke aggregate
  the real-project corpus stores pinned upstream inputs as local archives under
  `test_v2/artifact_parity/real_projects/archives/` and extracts them only
  inside the isolated `Temp_tests` workspace during execution, so third-party
  source trees do not stay exploded in the main repo layout

## Canonical Documents

- [Tests architecture](./tests_architecture.md)
- [Tests structural refactor plan](./tests_structural_refactor_plan.md)

- `tests_architecture.md` is the canonical baseline for ownership boundaries,
  suite taxonomy, aggregate/CI policy, and preserved runner behavior.
- `tests_structural_refactor_plan.md` is the active roadmap for changing that
  architecture in waves without reopening product-level semantic contracts.
