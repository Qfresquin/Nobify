# Evaluator Coverage Matrix (CMake 3.28 Audit)

Status: Implementation Audit. This document tracks evaluator parity against the
CMake 3.28 command/language surface implemented in the current workspace. It is
intentionally broader than raw registry metadata.

Important boundary note:
- this matrix audits the current implementation
- it is not the source of truth for the target evaluator architecture
- canonical target behavior is defined in
  [evaluator_v2_spec.md](./evaluator_v2_spec.md) and
  [evaluator_architecture_target.md](./evaluator_architecture_target.md)

Project priority framing:
- this audit serves the primary project goal defined in
  [`../project_priorities.md`](../project_priorities.md): **CMake 3.28 semantic
  compatibility first**,
- historical CMake behavior is secondary here unless it changes the observable
  CMake 3.28 outcome that real projects depend on,
- Nob backend optimization is downstream of semantic parity and is therefore
  outside the scoring scope of this document.

## 1. Scope

This audit measures evaluator responsibility only:
- structural execution (`if`/`foreach`/`while`/`function`/`macro`),
- argument resolution, variables, policies, flow, property/query state,
- runtime/meta effects the evaluator already models,
- semantic `Event_Stream` emission.

It does not score gaps in `build_model`, freeze/query, codegen, or the `nob` backend.

## 2. Audit Method

Baseline:
- official comparison target is **CMake 3.28**, matching the evaluator baseline enforced by `cmake_minimum_required()` and `cmake_policy(VERSION ...)` in `src_v2/evaluator/eval_project.c`.

Primary sources of truth:
- local code and docs: `src_v2/evaluator/eval_command_registry.h`, structural execution paths, and focused evaluator docs;
- official docs:
  - [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html)
  - [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html)
  - command/module pages for every audited `PARTIAL` row.

Universe audited in this snapshot:
- 123 registry-backed native commands.
- 12 structural language nodes outside the registry.
- 0 additional `missing-official` commands in the current workspace after filtering.

Relevance buckets:
- `first_party_runtime`: commands exercised by `test_v2/evaluator/**`, `test_v2/pipeline/**`, `provider_root/**`, and first-party runtime fixtures.
- `external_corpus`: commands exercised by the vendored `curl-8.18.0/**` corpus.
- `both`: appears in both buckets.
- `none`: not exercised by the audited runtime corpus in this workspace.

Classification rules:
- `FULL`: the evaluator covers the CMake 3.28 semantics that matter for the audited corpus at evaluator layer.
- `PARTIAL`: implementation exists, but valid documented CMake 3.28 behavior still falls outside the supported evaluator subset.
- `MISSING`: an official CMake 3.28 command relevant to the audited workspace has no evaluator handler or structural path.

Important distinction:
- `Registry Tag` is the static metadata exported by the native-command registry.
- `Audit Status` is this document's parity verdict against actual CMake 3.28 behavior for the audited corpus.
- the two intentionally diverge when code/docs show a real evaluator gap despite a registry `FULL` tag.

Priority interpretation:
- rows and gaps are prioritized first by how directly they block the primary
  CMake 3.28 parity goal,
- historical-compatibility refinements matter when they change that baseline's
  observable behavior,
- optimization opportunities are not used to upgrade or downgrade audit status
  in this document.

Excluded from `MISSING`:
- non-built-in command names introduced by project or module code at runtime;
- module-provided helpers/macros outside the command/language manual inventory, such as `check_symbol_exists`, `check_function_exists`, `check_type_size`, `find_dependency`, `cmake_push_check_state`, `ExternalProject_Add`, and `pkg_check_modules`.

## 3. Snapshot Summary

Snapshot date: March 28, 2026.

| Metric | Value |
|---|---:|
| Registry built-ins | 123 |
| Structural nodes outside registry | 12 |
| Audited universe | 135 |
| Audit `FULL` | 135 |
| Audit `PARTIAL` | 0 |
| Audit `MISSING` | 0 |
| Relevant to `first_party_runtime` | 55 |
| Relevant to `external_corpus` | 60 |
| Relevant to `both` | 39 |
| Native rows where `Audit Status != Registry Tag` | 0 |

Current native-tag divergences:
- none in this snapshot

Architecture note for this snapshot:
- the evaluator now exposes the `EvalSession` / `EvalExec_Request` /
  `EvalRunResult` public boundary directly; the current snapshot has no
  remaining `PARTIAL` audit rows or residual public/runtime drift toward
  `Evaluator_Context`
- canonical persistent evaluator state now lives in `EvalSessionState`, while
  each `eval_session_run(...)` uses a fresh transient `EvalExecContext`

## 4. Differential Harness Program

Differential coverage is tracked separately from the audit verdict above.

Important distinction:
- `Audit Status = FULL` means the current evaluator implementation appears
  semantically complete for the audited CMake 3.28 corpus
- it does not mean the command already has a real-CMake differential lane
- the differential program below is the operational backlog for converting that
  parity claim into explicit oracle-backed evidence

Current v1 status:
- runner entry point: `./build/nob_test test-evaluator-diff`
- suite location: `test_v2/evaluator_diff/`
- oracle policy: resolve `cmake` from `CMK2NOB_TEST_CMAKE_BIN`, then `PATH`
- version gate: only `cmake 3.28.x` participates; otherwise the suite skips
- current mode: `project-mode` only
- current family coverage: `target_*`, `list()`, `var_commands`, and `property_query` seed cases
- current comparison model:
  - `SUCCESS`: compare normalized `OUTCOME` plus `diff_snapshot.txt`
  - `ERROR`: compare normalized `OUTCOME` only

Current v1 DSL:
- `#@@CASE <name>`
- `#@@OUTCOME SUCCESS|ERROR`
- `#@@FILE <relpath>`
- `#@@DIR <relpath>`
- `#@@QUERY VAR <name>`
- `#@@QUERY CACHE_DEFINED <name>`
- `#@@QUERY TARGET_EXISTS <target>`
- `#@@QUERY TARGET_PROP <target> <property>`
- `#@@QUERY FILE_EXISTS <path>`

Current v1 snapshot lines:
- `OUTCOME=<SUCCESS|ERROR>`
- `VAR:<name>=<value|__UNDEFINED__>`
- `CACHE_DEFINED:<name>=0|1`
- `TARGET_EXISTS:<target>=0|1`
- `TARGET_PROP:<target>:<prop>=<value|__UNSET__|__MISSING_TARGET__>`
- `FILE_EXISTS:<path>=0|1`

Operational backlog rule:
- every registry command and every structural node must eventually be assigned
  to exactly one differential mode family
- no command should remain “unowned” by the differential program

Differential mode families:
- `snapshot differential`
  - compare stable semantic snapshots such as variables, properties, target
    existence, cache visibility, and evaluator-visible state
- `host-effect differential`
  - compare normalized filesystem, process, or host-side effects rather than
    only final in-memory properties
- `normalized failure differential`
  - compare success/failure class and normalized observable failure shape
    without requiring exact diagnostic text parity
- `special oracle lane`
  - use a family-specific harness for commands whose oracle is not well modeled
    by the generic project/script snapshot path

Rules for advancing coverage:
- a family is only considered differentially covered when it has at least one
  success case and one relevant failure case
- every critical differential behavior should also keep a local non-differential
  regression in `test_v2/evaluator/`
- the coverage matrix should be treated as the family-level backlog, not as a
  file-level checklist

Roadmap:
- **Phase 1, close configurable `project-mode` basics**
  - expand from `target_*` into `add_*`, `project`, `get_*`, `set_*`,
    `option`, `math`, `list`, `string`, `cmake_path`, directory properties,
    and target/query surfaces that fit stable snapshots without external
    toolchain dependence
- **Phase 2, add `script-mode` to the same harness**
  - introduce `#@@MODE PROJECT|SCRIPT`
  - cover `file()`, `configure_file`, `execute_process`, `include()`,
    `cmake_language(EVAL/CALL/DEFER)`, policy commands, and script-first
    control/data commands whose primary semantics do not require a project
    configure
- **Phase 3, model host and filesystem effects**
  - extend snapshots into manifests for files, normalized contents, process
    results, and staged side effects
  - target `file(DOWNLOAD|ARCHIVE_*)`, `find_*`, parts of `FetchContent`,
    `install`, and `export`
- **Phase 4, create special-oracle lanes**
  - add dedicated harnesses for families whose oracles are not well represented
    by the generic snapshot lane
  - target `find_package`, providers, redirects, package registry/export,
    `try_compile`, `try_run`, `ctest_*`, and `custom command/target`
- **Phase 5, cover structural nodes and compat/policy quirks**
  - differentialize `if`, `foreach`, `while`, `function`, `macro`, `block`,
    `return`, `break`, `continue`, and policy/compat interactions that change
    observable behavior
- **Phase 6, close backlog ownership**
  - classify every row in this matrix as `snapshot differential`,
    `host-effect differential`, `normalized failure differential`, or
    `special oracle lane`
  - no row should remain outside one of those buckets
- **Phase 7, operational hardening**
  - add explicit CI coverage with pinned `cmake 3.28.x`
  - publish per-family differential status
  - treat differential status as evidence in parity discussions without merging
    it into the default smoke aggregate

## 5. Audit Matrix

| Command | Kind | Registry Tag | Audit Status | Repo Relevance | CMake 3.28 Expectation | Current Evaluator Behavior | Primary Evidence | Official Reference |
|---|---|---|---|---|---|---|---|---|
| `FetchContent_Declare` | native | FULL | FULL | external_corpus | Cover the documented FetchContent module workflow used to declare, populate and query dependencies. | The evaluator now parses and stores the documented CMake 3.28 declaration surface exercised by the parity suite, including URL/Git/SVN/Hg/CVS/custom-download transports, download/update/patch options, and the declaration-time snapshots that drive later provider and `find_package()` behavior. | `src_v2/evaluator/eval_fetchcontent.c` | [FetchContent](https://cmake.org/cmake/help/v3.28/module/FetchContent.html) |
| `FetchContent_MakeAvailable` | native | FULL | FULL | external_corpus | Cover the documented FetchContent module workflow used to declare, populate and query dependencies. | The evaluator now covers the `MakeAvailable` workflows exercised by the audited corpus, including provider fulfillment, recursive bypass/idempotence, `FETCHCONTENT_TRY_FIND_PACKAGE_MODE`, local `SOURCE_DIR` overrides, URL archives with redirect staging, and saved `GIT` declarations validated by focused evaluator tests. | `src_v2/evaluator/eval_fetchcontent.c` | [FetchContent](https://cmake.org/cmake/help/v3.28/module/FetchContent.html) |
| `cmake_host_system_information` | native | FULL | FULL | external_corpus | Expose the documented host `QUERY` keys needed by CMake 3.28 scripts. | Parses `RESULT`/`QUERY` clauses into typed requests, resolves the supported host, memory, CPU, processor, registry, and distro keys through the canonical host query path, and honors both `QUERY WINDOWS_REGISTRY ...` and fallback-script-backed `DISTRIB_*` lookups validated by focused evaluator tests. | `src_v2/evaluator/eval_host.c` | [`cmake_host_system_information`](https://cmake.org/cmake/help/v3.28/command/cmake_host_system_information.html) |
| `cmake_path` | native | FULL | FULL | both | Cover the documented `cmake_path()` subcommand family used to compute script-visible path values. | The evaluator now covers the documented `SET`/`GET`/append/transform/query subcommands exercised by the current corpus, including the `GET` component surface and the `CONVERT`/`COMPARE`/`HAS_*`/`IS_*` branches validated by focused evaluator tests. | `src_v2/evaluator/eval_cmake_path.c` | [`cmake_path`](https://cmake.org/cmake/help/v3.28/command/cmake_path.html) |
| `export` | native | FULL | FULL | external_corpus | Cover the documented `export()` signatures that publish build/export metadata. | The evaluator now covers the documented `TARGETS`, `EXPORT`, and `PACKAGE` forms needed for the audited CMake 3.28 corpus, including evaluator-side `CXX_MODULES_DIRECTORY` sidecars, the implicit `FILE` for `export(EXPORT ...)`, and local package-registry publication consumed by `find_package()` under the documented gate variables and policies. | `src_v2/evaluator/eval_meta.c` | [`export`](https://cmake.org/cmake/help/v3.28/command/export.html) |
| `file` | native | FULL | FULL | both | Cover the documented `file()` subcommands that mutate evaluator-visible state or runtime effects. | The evaluator now covers the `file()` surface exercised by the audited corpus, including `READ`/`WRITE`/`STRINGS`, `GLOB`, transfer/hash/configure helpers, fsops, generate/lock/archive flows, and the curl-style `STRINGS ... REGEX` plus hashed certificate `GLOB` queries validated by focused evaluator tests on top of the existing golden coverage. | `src_v2/evaluator/eval_file.c` + file submodules | [`file`](https://cmake.org/cmake/help/v3.28/command/file.html) |
| `get_cmake_property` | native | FULL | FULL | external_corpus | Expose property/query results with the same observable semantics CMake scripts depend on. | Uses the shared property-query/provider path to cover the historical `VARIABLES`/`MACROS` forms, synthetic `CACHE_VARIABLES`/`COMMANDS`/`COMPONENTS`, synthesized globals such as `CMAKE_ROLE`, `IN_TRY_COMPILE`, `GENERATOR_IS_MULTI_CONFIG`, `PACKAGES_FOUND`, and `PACKAGES_NOT_FOUND`, plus the command-specific literal `NOTFOUND` fallback. | `src_v2/evaluator/eval_target_property_query.c` | [`get_cmake_property`](https://cmake.org/cmake/help/v3.28/command/get_cmake_property.html) |
| `get_directory_property` | native | FULL | FULL | external_corpus | Expose property/query results with the same observable semantics CMake scripts depend on. | Uses typed request parsing and the shared property/provider path to cover selected-directory `DEFINITION` snapshots, graph-derived directory properties such as `SOURCE_DIR`, `BINARY_DIR`, `PARENT_DIRECTORY`, `SUBDIRECTORIES`, `BUILDSYSTEM_TARGETS`, `IMPORTED_TARGETS`, `TESTS`, `VARIABLES`, `MACROS`, and `LISTFILE_STACK`, while preserving the documented known-directory checks and empty-string materialization for unset directory properties. | `src_v2/evaluator/eval_target_property_query.c` | [`get_directory_property`](https://cmake.org/cmake/help/v3.28/command/get_directory_property.html) |
| `get_filename_component` | native | FULL | FULL | external_corpus | Support the documented component/query modes exposed by `get_filename_component()`. | Parses the mode-specific option surface into a typed request, then executes the canonical directory/path/program resolution flow while preserving the observable `PROGRAM`/`CACHE` quirks around raw `PROGRAM_ARGS`, cached-result short-circuiting, and path-with-spaces program resolution. | `src_v2/evaluator/eval_directory.c` | [`get_filename_component`](https://cmake.org/cmake/help/v3.28/command/get_filename_component.html) |
| `get_property` | native | FULL | FULL | external_corpus | Expose property/query results with the same observable semantics CMake scripts depend on. | The shared property-query path now covers the documented generic `get_property()` scope and query-mode surface used by the audited corpus, including empty-string materialization for unset values, literal `NOTFOUND` for undefined property docs, declaration-directory-aware `SOURCE ... TARGET_DIRECTORY <target>` lookups, and known binary-directory qualifiers for `DIRECTORY`, `SOURCE DIRECTORY`, and `TEST DIRECTORY` lookups. | `src_v2/evaluator/eval_target_property_query.c` | [`get_property`](https://cmake.org/cmake/help/v3.28/command/get_property.html) |
| `get_target_property` | native | FULL | FULL | external_corpus | Expose property/query results with the same observable semantics CMake scripts depend on. | Uses the shared property/provider path to expose stored target properties plus the read-only target metadata CMake scripts rely on, including `TYPE`, `IMPORTED`, `IMPORTED_GLOBAL`, `ALIASED_TARGET`, `ALIAS_GLOBAL`, `SOURCE_DIR`, and `BINARY_DIR`, while preserving declaration-directory-aware lookup for inherited property resolution. | `src_v2/evaluator/eval_target_property_query.c` | [`get_target_property`](https://cmake.org/cmake/help/v3.28/command/get_target_property.html) |
| `install` | native | FULL | FULL | both | Parse the documented install signatures and emit install-relevant evaluator semantics. | The evaluator covers the install signature surface exercised by the audited corpus, including `TARGETS`/`FILES`/`PROGRAMS`/`DIRECTORY`, `SCRIPT`/`CODE`, `EXPORT`/`EXPORT_ANDROID_MK`, `IMPORTED_RUNTIME_ARTIFACTS`, `RUNTIME_DEPENDENCY_SET`, TYPE-derived destinations, and component inventory emission validated by focused evaluator tests. | `src_v2/evaluator/eval_install.c` | [`install`](https://cmake.org/cmake/help/v3.28/command/install.html) |
| `list` | native | FULL | FULL | both | Cover the documented `list()` mutation and query subcommands used in script control flow. | The evaluator covers the documented mutation/query surface used by the current corpus, including `SORT` compare/case/order modes and `TRANSFORM` selector combinations validated by focused evaluator tests. | `src_v2/evaluator/eval_list.c` | [`list`](https://cmake.org/cmake/help/v3.28/command/list.html) |
| `string` | native | FULL | FULL | both | Cover the documented `string()` subcommand family used in script-visible computation. | The evaluator now covers the documented string computation surface exercised by the audited corpus, including the `FIND`/`COMPARE`/`CONFIGURE`/`RANDOM`/`TIMESTAMP`/`UUID` option branches validated by focused evaluator tests on top of the existing text/regex/JSON/hash coverage. | `src_v2/evaluator/eval_string.c` + string submodules | [`string`](https://cmake.org/cmake/help/v3.28/command/string.html) |
| `add_compile_options` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses raw option tokens into a typed request, expands supported `SHELL:` fragments once, de-duplicates the resulting items, and commits the canonical directory `COMPILE_OPTIONS` mutation through a dedicated execution helper. | `src_v2/evaluator/eval_directory.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_custom_command` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_custom_target` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_definitions` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses legacy `-D`/`/D` flags into typed definition/option buckets, then applies canonical directory compile-definition and compile-option mutations while preserving target-side emission for the supported current-file targets. | `src_v2/evaluator/eval_directory.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_dependencies` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_executable` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_library` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_link_options` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses raw link-option tokens into a typed request, expands supported `SHELL:`/`LINKER:` forms once, de-duplicates the resulting items, and commits the canonical directory `LINK_OPTIONS` mutation through a dedicated execution helper. | `src_v2/evaluator/eval_directory.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_subdirectory` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `add_test` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `block` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `break` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `cmake_minimum_required` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `cmake_policy` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `configure_file` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `continue` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `cpack_add_component` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the component name and supported keyword surface into a typed request, then emits the canonical CPack component event while preserving the current module gate and legacy extra-argument warnings. | `src_v2/evaluator/eval_cpack.c` | [CPackComponent](https://cmake.org/cmake/help/v3.28/module/CPackComponent.html) |
| `cpack_add_component_group` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the group name plus supported metadata flags into a typed request, then emits the canonical CPack group event while preserving the current module gate and unexpected-argument warnings. | `src_v2/evaluator/eval_cpack.c` | [CPackComponent](https://cmake.org/cmake/help/v3.28/module/CPackComponent.html) |
| `cpack_add_install_type` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the install-type name and `DISPLAY_NAME` option into a typed request, then emits the canonical CPack install-type event while preserving the current module gate and unexpected-argument warnings. | `src_v2/evaluator/eval_cpack.c` | [CPackComponent](https://cmake.org/cmake/help/v3.28/module/CPackComponent.html) |
| `else` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `elseif` | structural | n/a | FULL | external_corpus | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `enable_testing` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `endblock` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `endforeach` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `endfunction` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `endif` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `endmacro` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `endwhile` | structural | n/a | FULL | first_party_runtime | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `execute_process` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses raw process options into a typed request, emits request/result trace events, and applies output/file side effects through shared canonical process helpers. | `src_v2/evaluator/eval_flow_process.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `find_file` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `find_library` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `find_package` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `find_path` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `find_program` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `foreach` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `function` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `if` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `include` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `include_directories` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses `SYSTEM`/`BEFORE`/`AFTER` modifiers and resolved paths into a typed request, then applies the canonical directory `INCLUDE_DIRECTORIES` mutation with the corresponding modifier flags through a dedicated execution helper. | `src_v2/evaluator/eval_directory.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `include_guard` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `link_directories` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses `BEFORE`/`AFTER` modifiers and resolved paths into a typed request, then applies the canonical directory `LINK_DIRECTORIES` mutation through a dedicated execution helper. | `src_v2/evaluator/eval_directory.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `link_libraries` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses legacy `debug`/`optimized`/`general` qualifiers into a typed request, preserving the existing config-genex lowering and dangling-qualifier diagnostics behind a dedicated execution boundary. | `src_v2/evaluator/eval_directory.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `macro` | structural | n/a | FULL | both | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `mark_as_advanced` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the optional `CLEAR`/`FORCE` prefix and variable batch into a typed request, then applies the canonical cache `ADVANCED` property mutations while preserving CMP0102 behavior for missing entries. | `src_v2/evaluator/eval_vars.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `math` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `message` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `option` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses `<variable> <help_text> [value]` into a typed request, then applies the canonical CMP0077-aware normal-binding/cache-write flow without disturbing existing typed cache entries. | `src_v2/evaluator/eval_vars.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `project` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `return` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `set` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses environment, normal, and `CACHE` forms into typed requests, then executes the canonical environment mutation, local/parent-scope binding, and CMP0126-aware cache update paths while preserving the legacy `ENV{}` extra-argument warning. | `src_v2/evaluator/eval_vars.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `set_property` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `set_target_properties` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `source_group` | native | FULL | FULL | external_corpus | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `target_compile_definitions` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `target_compile_options` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `target_include_directories` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `target_link_directories` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `target_link_libraries` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `target_link_options` | native | FULL | FULL | first_party_runtime | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `try_compile` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `unset` | native | FULL | FULL | both | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses environment, current-scope, `CACHE`, and `PARENT_SCOPE` forms into a typed request, then applies the canonical removal path with the documented parent-scope validation and cache/environment side effects. | `src_v2/evaluator/eval_vars.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `while` | structural | n/a | FULL | first_party_runtime | Execute structural control flow and definition semantics before semantic Event IR emission. | Handled outside the registry in structural evaluator execution; this audit found no project-relevant gap in the exercised surface. | `src_v2/evaluator/evaluator.c` + flow helpers | [cmake-language(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-language.7.html) |
| `FetchContent_GetProperties` | native | FULL | FULL | none | Cover the documented FetchContent module workflow used to declare, populate and query dependencies. | The shared FetchContent state query path now reports the documented population flags and configured directories for saved declarations, provider fulfillment, `find_package()` resolution, empty-dir provider handoff, and the direct-populate non-persistence rules validated by focused tests. | `src_v2/evaluator/eval_fetchcontent.c` | [FetchContent](https://cmake.org/cmake/help/v3.28/module/FetchContent.html) |
| `FetchContent_Populate` | native | FULL | FULL | none | Cover the documented FetchContent module workflow used to declare, populate and query dependencies. | The evaluator now covers both saved-detail and direct-option population semantics exercised by the CMake 3.28 parity suite, including multiple URLs, custom download/update/patch commands, disconnected behavior, Git defaults/update rules, and SVN/Hg/CVS smoke coverage without leaking direct-populate state into the global FetchContent registry. | `src_v2/evaluator/eval_fetchcontent.c` | [FetchContent](https://cmake.org/cmake/help/v3.28/module/FetchContent.html) |
| `FetchContent_SetPopulated` | native | FULL | FULL | none | Cover the documented FetchContent module workflow used to declare, populate and query dependencies. | Provider-context population updates now preserve the documented CMake 3.28 state model, including empty `SOURCE_DIR`/`BINARY_DIR` fulfillment, canonical population bookkeeping, and the published-variable behavior consumed by `MakeAvailable()` and `GetProperties()`. | `src_v2/evaluator/eval_fetchcontent.c` | [FetchContent](https://cmake.org/cmake/help/v3.28/module/FetchContent.html) |
| `add_compile_definitions` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses normalized compile-definition items into a typed request, de-duplicates them, and commits the canonical directory `COMPILE_DEFINITIONS` mutation while preserving target-side emission for supported current-file targets. | `src_v2/evaluator/eval_directory.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `aux_source_directory` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `build_command` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses legacy positional and keyword options into a typed request, then emits the compatibility build-command string through a dedicated execution helper that still honors the current policy/generator gates. | `src_v2/evaluator/eval_host.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `build_name` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the single-output-variable signature into a typed request and materializes the legacy host/compiler identifier through a dedicated execution path with the existing CMP0036 gate preserved. | `src_v2/evaluator/eval_host.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `cmake_file_api` | native | FULL | FULL | none | Model the documented `cmake_file_api()` query and reply workflow at evaluator layer. | Parses the supported `QUERY API_VERSION 1` form into typed object requests, keeps command queries in evaluator state instead of materializing `query.json`, merges them with on-disk client queries, and emits reply/index payloads for the supported `codemodel`, `cache`, `cmakeFiles`, and `toolchains` kinds under `.cmake/api/v1/reply`. | `src_v2/evaluator/eval_meta.c` | [`cmake_file_api`](https://cmake.org/cmake/help/v3.28/command/cmake_file_api.html) |
| `cmake_language` | native | FULL | FULL | none | Support the documented `CALL`, `EVAL`, `DEFER`, provider and related subcommand surface. | Parses the supported top-level subcommands into typed requests and routes `CALL`, `EVAL CODE`, `DEFER`, `GET_MESSAGE_LOG_LEVEL`, and the supported dependency-provider subset through shared execution helpers, including the documented duplicate-id, `GET_CALL`, and cancel-by-id `DEFER` behavior, plus the first-`project()`/`CMAKE_PROJECT_TOP_LEVEL_INCLUDES` restriction on `SET_DEPENDENCY_PROVIDER`. | `src_v2/evaluator/eval_flow_cmake_language.c` | [`cmake_language`](https://cmake.org/cmake/help/v3.28/command/cmake_language.html) |
| `cmake_parse_arguments` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses either the direct or `PARSE_ARGV` signature into a typed request carrying prefix, keyword specs, and source arguments, then runs the canonical keyword-consumption pass that preserves duplicate-keyword warnings, CMP0174 empty-value handling, and `_UNPARSED_ARGUMENTS` / `_KEYWORDS_MISSING_VALUES` publication. | `src_v2/evaluator/eval_vars_parse.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `create_test_sourcelist` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `ctest_build` | native | FULL | FULL | none | Match the documented CTest build-step semantics, including context resolution, build-command execution, result counters, XML staging, and submission reuse. | Parses the documented build keyword surface into a typed request, resolves session-local build context and the effective build command, executes the build step through evaluator process services, materializes canonical `NUMBER_ERRORS` / `NUMBER_WARNINGS` / `RETURN_VALUE` / `CAPTURE_CMAKE_ERROR`, stages committed `Build.xml` plus `BuildManifest.txt`, and lets `ctest_submit(PARTS Build)` reuse the committed build artifacts instead of projected metadata. | `src_v2/evaluator/eval_ctest.c` | [`ctest_build`](https://cmake.org/cmake/help/v3.28/command/ctest_build.html) |
| `ctest_configure` | native | FULL | FULL | none | Match the documented CTest configure-step semantics, including `BUILD`/`SOURCE` defaults, command resolution, XML staging, and `LabelsForSubprojects` propagation. | Parses the documented configure option surface into a typed request, resolves omitted `BUILD`/`SOURCE` through `CTEST_*` script settings or the corresponding `PROJECT_*` module values before falling back to evaluator roots, resolves `CTEST_CONFIGURE_COMMAND` or the `CMAKE_COMMAND` fallback, executes the configure step through evaluator process services, stages `Configure.xml` and manifests for later `ctest_submit(PARTS Configure)` reuse, and propagates `CTEST_LABELS_FOR_SUBPROJECTS` into the generated configure submission payload. | `src_v2/evaluator/eval_ctest.c` | [`ctest_configure`](https://cmake.org/cmake/help/v3.28/command/ctest_configure.html) |
| `ctest_coverage` | native | FULL | FULL | none | Match the documented CTest coverage-step semantics, including `BUILD` fallback, coverage-tool invocation, label-filtered staging, local summary reporting, result variables, and submission staging. | Parses the documented coverage option surface into a typed request, resolves `BUILD` plus `CTEST_COVERAGE_COMMAND` / `COVERAGE_COMMAND` and extra flags, executes the configured coverage tool in the build directory, materializes the staged `Coverage.xml` against canonical source-label state so `LABELS` changes the committed coverage payload instead of only projected metadata, preserves the evaluator-local coverage summary line in both normal and `QUIET` execution modes, stages the coverage manifest for later `ctest_submit(PARTS Coverage)` reuse, and publishes the documented `RETURN_VALUE` / `CAPTURE_CMAKE_ERROR` outcomes. | `src_v2/evaluator/eval_ctest.c` | [`ctest_coverage`](https://cmake.org/cmake/help/v3.28/command/ctest_coverage.html) |
| `ctest_empty_binary_directory` | native | FULL | FULL | none | Empty the requested binary directory after the documented safety checks. | Parses the one-directory signature into a typed request, rejects paths outside `CMAKE_BINARY_DIR`, empties existing directories, recreates missing targets as empty directories, and rejects non-directory paths before publishing the documented result metadata. | `src_v2/evaluator/eval_ctest.c` | [`ctest_empty_binary_directory`](https://cmake.org/cmake/help/v3.28/command/ctest_empty_binary_directory.html) |
| `ctest_memcheck` | native | FULL | FULL | none | Match the documented CTest memcheck-step semantics, including backend resolution, runtime execution, defect counting, result variables, XML staging, and submission reuse. | Parses the documented memcheck surface into a typed request, resolves `BUILD`, backend command/type/options/suppressions, and the canonical test-plan/runtime context, executes the selected tests through the configured dynamic-analysis tool, extracts real defect counts into `DEFECT_COUNT` / `RETURN_VALUE` / `CAPTURE_CMAKE_ERROR`, stages committed `MemCheck.xml` plus `MemCheckManifest.txt`, preserves `OUTPUT_JUNIT` as a side output, and lets `ctest_submit(PARTS MemCheck)` reuse the committed memcheck artifacts instead of projected metadata. | `src_v2/evaluator/eval_ctest.c` | [`ctest_memcheck`](https://cmake.org/cmake/help/v3.28/command/ctest_memcheck.html) |
| `ctest_read_custom_files` | native | FULL | FULL | none | Read and apply the documented per-directory CTest custom files. | Parses the directory list into a typed request, resolves and executes both documented `CTestCustom.ctest` and `CTestCustom.cmake` files when present, and publishes the loaded-file inventory through the evaluator metadata path. | `src_v2/evaluator/eval_ctest.c` | [`ctest_read_custom_files`](https://cmake.org/cmake/help/v3.28/command/ctest_read_custom_files.html) |
| `ctest_run_script` | native | FULL | FULL | none | Run one or more CTest scripts, including `NEW_PROCESS` isolation and `RETURN_VALUE` of the last script run. | Parses execution mode, return sink, and script list into a typed request, resolves omitted scripts to the current list file, executes scripts sequentially with evaluator-backed `NEW_PROCESS` scope isolation, and publishes the documented return value from the last script run. | `src_v2/evaluator/eval_ctest.c` | [`ctest_run_script`](https://cmake.org/cmake/help/v3.28/command/ctest_run_script.html) |
| `ctest_sleep` | native | FULL | FULL | none | Sleep for the documented one-arg or three-arg duration forms inside a CTest script. | Parses the documented one-arg and three-arg forms into a typed duration request, computes the CTest-compatible derived duration, performs the actual sleep through the host runtime, and publishes the resulting duration metadata canonically. | `src_v2/evaluator/eval_ctest.c` | [`ctest_sleep`](https://cmake.org/cmake/help/v3.28/command/ctest_sleep.html) |
| `ctest_start` | native | FULL | FULL | none | Match the documented CTest command semantics, including dashboard-side execution behavior. | Parses the documented model/source/binary forms plus `GROUP`/deprecated `TRACK`, `APPEND`, and `QUIET`; defaults source/binary from `CTEST_SOURCE_DIRECTORY` and `CTEST_BINARY_DIRECTORY`; preserves `TAG` metadata with model/group for later `APPEND`; warns when appended model/group diverge from the existing `TAG`; and runs `CTEST_CHECKOUT_COMMAND`/`CTEST_CVS_CHECKOUT` through the evaluator process path from the parent of the source directory. | `src_v2/evaluator/eval_ctest.c` | [`ctest_start`](https://cmake.org/cmake/help/v3.28/command/ctest_start.html) |
| `ctest_submit` | native | FULL | FULL | none | Match the documented CTest command semantics, including dashboard-side execution behavior. | Parses the documented default and `CDASH_UPLOAD` signatures into typed requests, resolves explicit and implicit `Notes`/`ExtraFiles`/`Upload` file sets, honors direct and legacy submit-URL configuration, performs process-backed remote submissions with retry/inactivity handling, extracts `BUILD_ID` results, preserves enriched manifests under `Testing/<tag>`, and models the documented two-phase `CDASH_UPLOAD` flow. | `src_v2/evaluator/eval_ctest.c` | [`ctest_submit`](https://cmake.org/cmake/help/v3.28/command/ctest_submit.html) |
| `ctest_test` | native | FULL | FULL | none | Match the documented CTest command semantics, including dashboard-side execution behavior. | Parses the supported test-step surface into a typed request, resolves the canonical test plan from the evaluator event/session model, executes the planned tests through evaluator process services, stages committed `Test.xml` plus `TestManifest.txt`, preserves `OUTPUT_JUNIT` as a side output instead of submit truth, and lets `ctest_submit(PARTS Test)` reuse the committed test artifact set. | `src_v2/evaluator/eval_ctest.c` | [`ctest_test`](https://cmake.org/cmake/help/v3.28/command/ctest_test.html) |
| `ctest_update` | native | FULL | FULL | none | Match the documented CTest update-step semantics, including source-context resolution, update-command selection, result variables, and submission staging. | Parses the documented update option surface into a typed request, resolves source/build context against the canonical CTest session, detects or honors the documented VCS/update command settings, executes the update command through evaluator process services, materializes the documented `RETURN_VALUE` / `CAPTURE_CMAKE_ERROR` outcomes, and stages `Update.xml` plus a manifest for later `ctest_submit(PARTS Update)` reuse. | `src_v2/evaluator/eval_ctest.c` | [`ctest_update`](https://cmake.org/cmake/help/v3.28/command/ctest_update.html) |
| `ctest_upload` | native | FULL | FULL | none | Match the documented CTest command semantics, including dashboard-side execution behavior. | Parses the documented `FILES`/`QUIET`/`CAPTURE_CMAKE_ERROR` surface into a typed request, resolves local files, stages both `Upload.xml` and evaluator manifests under `Testing/<tag>`, and integrates with `ctest_submit(PARTS Upload)` so the prepared upload payloads participate in remote dashboard submission. | `src_v2/evaluator/eval_ctest.c` | [`ctest_upload`](https://cmake.org/cmake/help/v3.28/command/ctest_upload.html) |
| `define_property` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `enable_language` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `exec_program` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the legacy compatibility signature into a typed request and lowers it onto the same canonical process execution/output path used by `execute_process()`. | `src_v2/evaluator/eval_flow_process.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `export_library_dependencies` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `fltk_wrap_ui` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the legacy target/output shape into a typed request, then materializes the canonical generated `fluid_*.cxx` / `fluid_*.h` list into `<target>_FLTK_UI_SRCS`. | `src_v2/evaluator/eval_legacy.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `get_source_file_property` | native | FULL | FULL | none | Expose property/query results with the same observable semantics CMake scripts depend on. | Uses the shared property/provider path to cover the documented `DIRECTORY <dir>` and `TARGET_DIRECTORY <target>` overrides, known binary-directory resolution, synthetic `LOCATION`, globally visible `GENERATED`, and the command-specific literal `NOTFOUND` fallback. | `src_v2/evaluator/eval_target_property_query.c` | [`get_source_file_property`](https://cmake.org/cmake/help/v3.28/command/get_source_file_property.html) |
| `get_test_property` | native | FULL | FULL | none | Expose property/query results with the same observable semantics CMake scripts depend on. | Parses the documented CMake 3.28 argument order, supports `DIRECTORY <dir>` overrides including known binary directories, exposes the default `WORKING_DIRECTORY` recorded by `add_test()` alongside later `set_tests_properties()` overrides through the shared property/provider path, and preserves the documented literal `NOTFOUND` fallback. | `src_v2/evaluator/eval_target_property_query.c` | [`get_test_property`](https://cmake.org/cmake/help/v3.28/command/get_test_property.html) |
| `include_external_msproject` | native | FULL | FULL | none | Model external MS project inclusion closely enough for evaluator-side control flow and metadata. | Parses the supported arguments into typed metadata, registers an imported external target, records location/type/GUID/platform metadata through the property engine, and emits dependency edges that keep the evaluator-visible graph aligned with downstream `add_dependencies()` usage. | `src_v2/evaluator/eval_meta.c` | [`include_external_msproject`](https://cmake.org/cmake/help/v3.28/command/include_external_msproject.html) |
| `include_regular_expression` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the one- or two-regex signature into a typed request and applies the legacy include-regex variable projections through a dedicated execution helper. | `src_v2/evaluator/eval_directory.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `install_files` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `install_programs` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `install_targets` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `load_cache` | native | FULL | FULL | none | Import cache entries with the documented modes and visibility rules of CMake 3.28. | The handler now covers the documented legacy single-build-directory form and `READ_WITH_PREFIX` form, including prefixed variable publication without local cache creation, legacy internal-cache imports with `EXCLUDE`/`INCLUDE_INTERNALS`, the documented single-path legacy shape, the empty-value unset quirk, and the CMake 3.28 `cmake -P` script-mode allowance for `READ_WITH_PREFIX`. | `src_v2/evaluator/eval_vars.c` | [`load_cache`](https://cmake.org/cmake/help/v3.28/command/load_cache.html) |
| `load_command` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `make_directory` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the directory batch into a typed request, then resolves each path against the current binary dir and applies the canonical mkdir path one item at a time. | `src_v2/evaluator/eval_legacy.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `output_required_files` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `qt_wrap_cpp` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the legacy library/output/header form into a typed request, then emits the canonical `moc_<stem>.cxx` list into the requested output variable. | `src_v2/evaluator/eval_legacy.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `qt_wrap_ui` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the legacy library/header-var/source-var/ui-file form into a typed request, then publishes the canonical `ui_*.h` and `ui_*.cxx` outputs through a dedicated execution path. | `src_v2/evaluator/eval_legacy.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `remove` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the variable name and removal set into a typed request, then applies the canonical list-filtering pass against the current semicolon list binding. | `src_v2/evaluator/eval_legacy.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `remove_definitions` | native | FULL | FULL | none | Remove directory-level compile definitions with legacy CMake-compatible normalization. | Removes valid CMake-style `-D`/`/D` definitions from directory compile-definition state, removes legacy non-definition flags previously added through `add_definitions()`, and now applies the `CMP0005`-aware matching/normalization path validated by focused evaluator tests for escaped legacy values. | `src_v2/evaluator/eval_directory.c` | [`remove_definitions`](https://cmake.org/cmake/help/v3.28/command/remove_definitions.html) |
| `separate_arguments` | native | FULL | FULL | none | Support all documented parsing modes, including `PROGRAM` handling. | Parses the legacy, token-splitting, and `PROGRAM` request shapes into explicit execution modes, then applies the canonical command-line splitting and program-resolution paths for plain output, `PROGRAM`, and `PROGRAM SEPARATE_ARGS`. | `src_v2/evaluator/eval_vars_parse.c` | [`separate_arguments`](https://cmake.org/cmake/help/v3.28/command/separate_arguments.html) |
| `set_directory_properties` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `set_source_files_properties` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `set_tests_properties` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `site_name` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the output sink into a typed request and routes helper-command capture or host fallback resolution through a dedicated execution helper, preserving the existing warning behavior for empty results. | `src_v2/evaluator/eval_host.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `subdir_depends` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `subdirs` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `target_compile_features` | native | FULL | FULL | none | Record compile-feature requirements with CMake-compatible feature semantics. | Validates requested features against evaluator-published `CMAKE_<LANG>_KNOWN_FEATURES` / `CMAKE_<LANG>_COMPILE_FEATURES`, enforces imported-target restrictions, records canonical compile-feature properties, and resolves meta-features into target standard metadata visible to later evaluator stages. | `src_v2/evaluator/eval_target_usage.c` | [`target_compile_features`](https://cmake.org/cmake/help/v3.28/command/target_compile_features.html) |
| `target_precompile_headers` | native | FULL | FULL | none | Model precompiled-header declarations and reuse semantics visible to scripts and later stages. | Validates imported-target restrictions, enforces the exclusivity between direct headers and `REUSE_FROM`, normalizes header tokens according to the documented source-dir and generator-expression rules, and records the resulting property/dependency state for downstream evaluator consumers. | `src_v2/evaluator/eval_target_usage.c` | [`target_precompile_headers`](https://cmake.org/cmake/help/v3.28/command/target_precompile_headers.html) |
| `target_sources` | native | FULL | FULL | none | Model regular sources and documented `FILE_SET` families visible to scripts and downstream build semantics. | The evaluator covers the regular-source and `FILE_SET` surface exercised by the audited corpus, including `HEADERS` and `CXX_MODULES` default/named sets, imported `INTERFACE` module sets, and the export/install-facing property materialization validated by the existing target-usage and export tests. | `src_v2/evaluator/eval_target_usage.c` | [`target_sources`](https://cmake.org/cmake/help/v3.28/command/target_sources.html) |
| `try_run` | native | FULL | FULL | none | Compile and execute test programs with native and cross-compiling behavior matching CMake 3.28. | Parses into a typed request and executes through a dedicated compile/run path, covering native source/project execution, cache-vs-`NO_CACHE` result publication, `FAILED_TO_RUN`, cross-compiling emulator dispatch, placeholder staging, and reuse of prefilled cross-compiling cache answers. | `src_v2/evaluator/eval_try_run.c` | [`try_run`](https://cmake.org/cmake/help/v3.28/command/try_run.html) |
| `use_mangled_mesa` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `utility_source` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `variable_requires` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Native handler present; this audit found no project-relevant gap in the exercised CMake 3.28 surface. | `src_v2/evaluator/eval_command_registry.h` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `variable_watch` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses the watched variable and optional command into a typed request, then updates or appends the evaluator-side watcher registration through a dedicated execution path. | `src_v2/evaluator/eval_legacy.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |
| `write_file` | native | FULL | FULL | none | Implement the documented CMake 3.28 semantics needed before build-model projection. | Parses path, concatenated content payload, and optional `APPEND` into a typed request, then routes the write through the canonical text-file helper against the current binary dir. | `src_v2/evaluator/eval_legacy.c` | [cmake-commands(7)](https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html) |

No `missing-official` rows were found in the current workspace after restricting the audit to official built-ins/structural language entries and excluding module-provided helper macros/functions.

## 6. `PARTIAL` Detail Matrix

This snapshot has no remaining `Audit Status = PARTIAL` rows.

The previous evaluator-semantic gaps for `cmake_host_system_information`,
`cmake_file_api`, `export`, `include_external_msproject`,
`remove_definitions`, `target_compile_features`, and
`target_precompile_headers` are now covered by focused evaluator tests and the
main audit matrix above.
## 7. Interpretation Notes

- This document is intentionally stricter than the static registry metadata.
- A registry `FULL` tag can still audit as `PARTIAL` when the current code rejects a documented CMake 3.28 behavior that matters to the audited corpus.
- This snapshot now has no registry/audit divergences and no remaining `PARTIAL` rows.
- `parser` / `lexer` goldens were used only as syntax signals and did not raise implementation priority by themselves.
- The matrix should be read in project-priority order: CMake 3.28 parity first,
  historical behavior second, backend optimization later and outside this audit.

## 7. Relationship to Other Docs

- `../project_priorities.md`
  Canonical project-level priority order used to interpret this audit.

- `evaluator_v2_spec.md`
  Canonical top-level evaluator contract.

- `evaluator_command_capabilities.md`
  Defines the registry metadata (`implemented_level`, `fallback_behavior`) that now feeds, but does not fully determine, this audit.

- `evaluator_event_ir_contract.md`
  Complements this command audit with output/event invariants.
