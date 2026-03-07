# Evaluator Coverage Matrix (Rewrite Draft)

Status: Analytical draft. This document tracks evaluator implementation coverage using the current command-capability registry and related runtime surfaces. It is not a canonical behavior contract.

## 1. Scope

This matrix tracks:
- built-in command coverage from the dispatcher registry,
- implementation-level distribution (`FULL` vs `PARTIAL`),
- fallback metadata distribution,
- domain-level concentration of `PARTIAL` commands,
- non-registry structural language coverage status.

It does not redefine evaluator guarantees. Canonical behavior remains in `evaluator_v2_spec.md` and subordinate semantic docs.

## 2. Source of Truth

Primary files for this matrix:
- `src_v2/evaluator/eval_command_registry.h`
- `src_v2/evaluator/eval_command_caps.c`
- `src_v2/evaluator/evaluator.h`
- `src_v2/evaluator/eval_dispatcher.c`
- `src_v2/evaluator/evaluator.c` (structural nodes outside dispatcher)

Historical reference set (archived):
- no evaluator coverage archive is currently kept in-tree; previous `docs/Evaluatorr/...` references were stale path leftovers.

## 3. Method and Legend

Snapshot method:
- registry-driven counts from `EVAL_COMMAND_REGISTRY(X)`,
- analytical grouping by command domain,
- no external CMake scraping in this rewrite draft snapshot.

Legend:
- `FULL`: command marked fully implemented in registry metadata.
- `PARTIAL`: command exists but has reduced scope or known semantic gaps.
- `MISSING`: command absent from built-in registry.

Important boundary:
- these labels are static capability metadata and do not guarantee success for every invocation shape.

## 4. Registry Coverage Snapshot

Snapshot date (workspace): March 6, 2026.

### 4.1 Built-in Command Counts

| Metric | Value |
|---|---:|
| Registry built-ins | 119 |
| `FULL` | 67 |
| `PARTIAL` | 52 |
| `MISSING` (inside registry scope) | 0 |

### 4.2 Coverage Ratios

| Level | Count | Ratio |
|---|---:|---:|
| `FULL` | 67 | 56.3% |
| `PARTIAL` | 52 | 43.7% |
| `MISSING` | 0 | 0.0% |

## 5. Fallback Metadata Snapshot

Registry fallback distribution:

| Fallback | Count | Ratio |
|---|---:|---:|
| `EVAL_FALLBACK_NOOP_WARN` | 116 | 97.5% |
| `EVAL_FALLBACK_ERROR_CONTINUE` | 3 | 2.5% |
| `EVAL_FALLBACK_ERROR_STOP` | 0 | 0.0% |

Commands currently tagged `ERROR_CONTINUE`:
- `cmake_language`
- `cmake_path`
- `file`

Current `ERROR_STOP` registry entries:
- none

## 6. Structural Language Coverage (Outside Registry)

The following are implemented outside dispatcher registry metadata and should be tracked separately:
- `if`
- `foreach`
- `while`
- `function`
- `macro`

Current status in code paths:
- implemented in evaluator structural execution (`evaluator.c` + flow helpers),
- not represented as rows in built-in command capability table.

## 7. `PARTIAL` Concentration by Domain

`PARTIAL` commands are concentrated in specific domains, not evenly distributed.

| Domain | Partial Count | Notes |
|---|---:|---|
| `ctest_*` workflow | 13 | Largest cluster; mostly metadata/compat surfaces vs full native tool behavior. |
| Legacy compatibility wrappers | 19 | Deprecated/compat commands intentionally narrower than modern CMake behavior. |
| Property/query and introspection | 7 | Read/query paths still less complete than mutation/write paths. |
| Meta/runtime integrations | 3 | `cmake_file_api`, `cmake_host_system_information`, `include_external_msproject`. |
| Modern but still incomplete semantics | 10 | Includes `cmake_language`, `target_*` advanced surfaces, `try_run`, `export`, `load_cache`, `variable_watch`, `separate_arguments`. |

## 8. Explicit `PARTIAL` Command List

Current `PARTIAL` set (52 commands):

```text
build_command
build_name
cmake_file_api
cmake_host_system_information
cmake_language
ctest_build
ctest_configure
ctest_coverage
ctest_empty_binary_directory
ctest_memcheck
ctest_read_custom_files
ctest_run_script
ctest_sleep
ctest_start
ctest_submit
ctest_test
ctest_update
ctest_upload
exec_program
export
export_library_dependencies
fltk_wrap_ui
get_cmake_property
get_directory_property
get_property
get_source_file_property
get_target_property
get_test_property
include_external_msproject
install_files
install_programs
install_targets
load_cache
load_command
output_required_files
qt_wrap_cpp
qt_wrap_ui
remove
remove_definitions
separate_arguments
source_group
subdir_depends
subdirs
target_compile_features
target_precompile_headers
target_sources
try_run
use_mangled_mesa
utility_source
variable_requires
variable_watch
write_file
```

## 9. Coverage Interpretation Notes

Current high-level interpretation:
- core modern command surface has broad `FULL` coverage,
- residual risk is concentrated in `ctest_*`, legacy wrappers, and query/introspection paths,
- `try_run` remains correctly tagged `PARTIAL`: native source-file and `PROJECT` signatures now execute, but the cross-compiling answer-file workflow still resolves to a deterministic compile-only skip instead of full target execution,
- fallback metadata remains overwhelmingly `NOOP_WARN`, so runtime strictness is mostly shaped by command handlers and compat policy rather than fallback enum diversity.

## 10. Priority Promotion Candidates

Highest-leverage candidates to reduce `PARTIAL` footprint:
1. Residual external `ctest_*` execution surface.
   Progress note for the March 6, 2026 workspace batches: the evaluator-side/local-orchestration slice of the family is now complete. The cluster shares a lightweight CTest session model (`MODEL`, `TRACK`, `SOURCE`, `BUILD`) seeded by `ctest_start(...)`; that start step also stages `Testing/TAG` plus session `TAG` / `TAG_FILE` / `TAG_DIR` / `TESTING_DIR`. The configure/build/test/coverage/memcheck/update commands resolve omitted source/build context from that session, `ctest_submit` / `ctest_upload` stage local manifest files under `Testing/<tag>/` with resolved file lists, and `ctest_run_script(NEW_PROCESS ...)` executes in an isolated evaluator-side child scope. The remaining `PARTIAL` footprint is the intentionally deferred external dashboard/tool-execution layer.
2. `target_compile_features` / `target_precompile_headers` / `target_sources`.
   Progress note for the March 6, 2026 workspace batch: these advanced target commands now feed the persistent target property store as well as the Event IR, so `get_target_property(...)` can observe `SOURCES`, `INTERFACE_SOURCES`, compile-feature properties, precompile-header properties, and the `TYPE HEADERS` `FILE_SET` surface (`HEADER_SET*` / `HEADER_DIRS*`). Residual partial coverage is now concentrated in other file-set types such as `CXX_MODULES`.
3. `try_run` cross-compiling answer-file workflow.
   Progress note for the March 6, 2026 workspace batch: `try_run(PROJECT ...)` now executes through the shared `try_compile(PROJECT ...)` machinery. The remaining partial behavior is the non-native answer-file/emulator path, which currently returns a deterministic compile-only skip.
4. `get_property` and related query wrappers.
   Progress note for the March 6, 2026 workspace batch: the evaluator now keeps property query state in a persistent internal store across scope pops, and inherited `TARGET` plus `SOURCE TARGET_DIRECTORY` queries follow the target declaration directory instead of the caller-local directory.
5. G4 residual runtime/meta integrations.
   Progress note for the March 6, 2026 workspace batch: `cmake_file_api(QUERY API_VERSION 1 ...)` now stages evaluator-side query/reply/index artifacts under `.cmake/api/v1` and publishes stable `NOBIFY_CMAKE_FILE_API::*` path variables, `cmake_host_system_information()` now covers the `FQDN` query key instead of treating it as unimplemented, and `cmake_language(SET_DEPENDENCY_PROVIDER ...)` now covers both the file-scope `FIND_PACKAGE` provider subset and the `FETCHCONTENT_MAKEAVAILABLE_SERIAL` subset. The new `FetchContent` batch keeps evaluator-side declaration/population state, supports local `SOURCE_DIR` / `BINARY_DIR` flows plus `FETCHCONTENT_SOURCE_DIR_<UPPER>` bypass, and honors same-dependency recursive fallback without re-entering the provider. Remaining partial behavior is concentrated in the rest of the advanced `cmake_language` surface and richer file-api/meta-runtime semantics beyond local staging.

## 11. Relationship to Other Docs

- `evaluator_v2_spec.md`
Canonical contract and precedence.

- `evaluator_command_capabilities.md`
API/data-model contract that provides the static capability labels used here.

- `evaluator_dispatch.md`
Runtime routing behavior that is separate from analytical coverage labeling.

- `evaluator_event_ir_contract.md`
Event output coverage/invariants, complementary to command capability coverage.
