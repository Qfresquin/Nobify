# Evaluator v2 Full Audit

Status: Documentation-driven audit snapshot against CMake 3.28.6 command and policy docs.

## 1. Audit scope and baseline

This audit expands beyond the dispatcher-only coverage matrix and accounts for the full command surface used for this evaluator release:

- `cmake-commands(7)` for CMake `3.28.6`: `128` documented core commands.
- `CPackComponent` module command surface for CMake `3.28.6`: `3` documented module commands (`cpack_add_component`, `cpack_add_component_group`, `cpack_add_install_type`).
- Effective scoped universe for this audit: `131` documented entry points.

Evaluator implementation state at the time of this audit:

- `45` dispatcher-registered built-in commands from `src_v2/evaluator/eval_command_caps.c`.
- `12` structural language commands implemented outside the dispatcher via parser AST nodes in `src_v2/parser/parser.h` and `src_v2/evaluator/evaluator.c`:
  - `if`, `elseif`, `else`, `endif`
  - `foreach`, `endforeach`
  - `while`, `endwhile`
  - `function`, `endfunction`
  - `macro`, `endmacro`
- Total implemented entry points within the scoped command universe: `57`.
- Missing from the scoped command universe: `74`.

Audit constraints used here:

- Baseline semantics: CMake `3.28.6`.
- Semantic validation target: Linux + Windows.
- Portability outside Linux + Windows is handled as static risk analysis only.
- No local `cmake` binary is available in this environment, so this is a docs-first audit and not a runtime differential test against an installed CMake.

Primary external references used:

- `https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html`
- `https://cmake.org/cmake/help/v3.28/manual/cmake-policies.7.html`
- `https://cmake.org/cmake/help/v3.28/module/CPackComponent.html`
- `https://cmake.org/cmake/help/v3.28/command/file.html`
- `https://cmake.org/cmake/help/v3.28/command/find_package.html`
- `https://cmake.org/cmake/help/v3.28/command/while.html`
- `https://cmake.org/cmake/help/v3.28/policy/CMP0074.html`
- `https://cmake.org/cmake/help/v3.28/policy/CMP0152.html`

## 2. Methodology and source hierarchy

Source-of-truth order used in this audit:

1. Official CMake `3.28.6` documentation.
2. Evaluator implementation in `src_v2/evaluator/*.c`, `src_v2/evaluator/*.h`, and `src_v2/parser/parser.h`.
3. Evaluator tests in `test_v2/evaluator/test_evaluator_v2_suite.c` and `test_v2/evaluator/golden/evaluator_all.cmake`.
4. Existing compatibility docs in `docs/evaluator/*.md`.

Concrete checks performed:

- Verified that `src_v2/evaluator/eval_command_caps.c` and `src_v2/evaluator/eval_dispatcher.c` expose the same `45` built-in command names with no drift in either direction.
- Verified that every dispatcher-registered command appears at least once in evaluator test sources or the golden evaluator script.
- Parsed the CMake `3.28.6` command manual to enumerate the `128` core commands.
- Parsed the CMake `3.28.6` policy manual and compared the grouped introduction versions against `src_v2/evaluator/eval_policy_engine.c`; the local `CMP0000..CMP0155` registry and introduction ranges matched exactly.
- Inspected policy-sensitive evaluator code paths and checked whether they consult the policy engine or hard-code one behavior.

What this audit intentionally does not claim:

- It does not prove byte-for-byte parity against a live CMake runtime.
- It does not prove exhaustive semantic parity for every structural language construct.
- It does not include code fixes; it records evidence and updates documentation accuracy only.

## 3. Full command inventory

### 3.1 Summary

| Bucket | Count | Notes |
|---|---:|---|
| CMake `3.28.6` core commands from `cmake-commands(7)` | `128` | Global command reference |
| `CPackComponent` module commands in scope | `3` | Not listed in `cmake-commands(7)` but explicitly in evaluator scope |
| Scoped command universe | `131` | Audit denominator |
| Dispatcher-registered built-ins | `45` | Capability-tracked in `eval_command_caps.c` |
| Structural language commands | `12` | Implemented through AST node handling, not capability-tracked |
| Implemented entry points in scope | `57` | `45 + 12` |
| Missing entry points in scope | `74` | Not implemented in dispatcher or structural evaluator flow |

### 3.2 Commands implemented outside the dispatcher

These commands are present in the CMake command manual, but in evaluator v2 they are modeled structurally instead of through `eval_dispatch_command(...)`:

- `if`, `elseif`, `else`, `endif`
- `foreach`, `endforeach`
- `while`, `endwhile`
- `function`, `endfunction`
- `macro`, `endmacro`

This is why the dispatcher-visible command count (`45`) is smaller than the total implemented command count (`57`).

### 3.3 Module commands implemented outside `cmake-commands(7)`

These evaluator commands are intentionally in scope even though they are documented under the `CPackComponent` module rather than `cmake-commands(7)`:

- `cpack_add_component`
- `cpack_add_component_group`
- `cpack_add_install_type`

## 4. Implemented command parity matrix

### 4.1 Dispatcher-backed built-ins

Current dispatcher-backed surface is documented in `evaluator_v2_coverage_status.md`.

Audit outcome for that surface:

- `43` dispatcher-backed commands remain consistent with their current documented `FULL` status in this pass.
- `2` dispatcher-backed commands have confirmed result-affecting divergences and must be `PARTIAL`:
  - `file`
  - `find_package`

Confirmed partials:

| Command | Audit result | Confirmed divergence | Why it is result-affecting |
|---|---|---|---|
| `file` | `PARTIAL` | `file(REAL_PATH)` does not consult `CMP0152`. The implementation in `src_v2/evaluator/eval_file_fsops.c` always follows its local path resolution path instead of honoring policy-driven `OLD` vs `NEW` behavior. | Valid policy-controlled `file(REAL_PATH)` inputs can resolve to different paths. |
| `find_package` | `PARTIAL` | `src_v2/evaluator/eval_package.c` always honors `<Pkg>_ROOT` / environment-root prefixes unless `NO_PACKAGE_ROOT_PATH` is set, but it does not consult `CMP0074`. | `cmake_policy(SET CMP0074 OLD)` cannot suppress package-root search paths, changing package resolution. |

Test coverage check:

- Every dispatcher-registered command name appears at least once in `test_v2/evaluator/test_evaluator_v2_suite.c` or `test_v2/evaluator/golden/evaluator_all.cmake`.
- This confirms coverage presence, not complete semantic exhaustiveness.

### 4.2 Structural language commands

These are implemented but not currently capability-tracked:

| Command family | Implementation path | Audit result | Notes |
|---|---|---|---|
| `if` / `elseif` / `else` / `endif` | `src_v2/evaluator/evaluator.c` (`eval_if`) | Implemented | Present as AST control-flow, not dispatcher metadata. |
| `foreach` / `endforeach` | `src_v2/evaluator/evaluator.c` (`eval_foreach`) | Implemented | Includes `CMP0124` handling for loop variable scoping. |
| `while` / `endwhile` | `src_v2/evaluator/evaluator.c` (`eval_while`) | `PARTIAL` | Hard iteration guard of `10000` iterations is evaluator-specific and can change valid CMake results for larger loops. |
| `function` / `endfunction` | `src_v2/evaluator/evaluator.c` (`eval_user_cmd_register`, `eval_user_cmd_invoke`) | Implemented | Supported through AST node registration and invocation. |
| `macro` / `endmacro` | `src_v2/evaluator/evaluator.c` (`eval_user_cmd_register`, `eval_user_cmd_invoke`) | Implemented | Supported through AST node registration and invocation. |

## 5. Missing command gap matrix

All missing commands currently fall through the unknown-command path in `src_v2/evaluator/eval_dispatcher.c`:

- user-defined function/macro resolution is attempted first
- otherwise an `Unknown command` diagnostic is emitted
- severity depends on `CMAKE_NOBIFY_UNSUPPORTED_POLICY`
- behavior remains a no-op after the diagnostic

### 5.1 High impact missing commands (`30`)

These are common build-model or configure-flow commands whose absence materially limits parity for modern CMake projects:

- `add_compile_definitions`, `add_dependencies`, `cmake_host_system_information`, `cmake_language`, `cmake_parse_arguments`, `configure_file`, `define_property`, `enable_language`, `execute_process`, `find_file`, `find_library`, `find_path`, `find_program`, `get_cmake_property`, `get_directory_property`, `get_filename_component`, `get_property`, `get_source_file_property`, `get_target_property`, `get_test_property`, `option`, `separate_arguments`, `set_directory_properties`, `set_source_files_properties`, `set_tests_properties`, `source_group`, `target_compile_features`, `target_precompile_headers`, `target_sources`, `try_run`

### 5.2 Medium impact missing commands (`26`)

These are valid and useful commands, but they are narrower, more tooling-oriented, or less central to the build graph than the high-impact set:

- `aux_source_directory`, `build_command`, `cmake_file_api`, `create_test_sourcelist`, `ctest_build`, `ctest_configure`, `ctest_coverage`, `ctest_empty_binary_directory`, `ctest_memcheck`, `ctest_read_custom_files`, `ctest_run_script`, `ctest_sleep`, `ctest_start`, `ctest_submit`, `ctest_test`, `ctest_update`, `ctest_upload`, `export`, `fltk_wrap_ui`, `include_external_msproject`, `include_regular_expression`, `load_cache`, `mark_as_advanced`, `remove_definitions`, `site_name`, `variable_watch`

### 5.3 Low impact missing commands (`18`)

These are predominantly deprecated, legacy, or niche utility commands:

- `build_name`, `exec_program`, `export_library_dependencies`, `install_files`, `install_programs`, `install_targets`, `load_command`, `make_directory`, `output_required_files`, `qt_wrap_cpp`, `qt_wrap_ui`, `remove`, `subdir_depends`, `subdirs`, `use_mangled_mesa`, `utility_source`, `variable_requires`, `write_file`

## 6. Policy audit

### 6.1 Policy engine registry correctness

Confirmed:

- `src_v2/evaluator/eval_policy_engine.c` declares a policy range of `CMP0000..CMP0155`.
- The local registry count is `156`.
- The introduction-version ranges in `POLICY_INTRO_RANGES` match the grouped `Policies Introduced by CMake X.Y` sections from `https://cmake.org/cmake/help/v3.28/manual/cmake-policies.7.html`.

This means the policy registry and introduction metadata are accurate for the CMake `3.28.6` baseline.

### 6.2 Policy-gated behavior that is actually modeled

Confirmed live policy hooks in evaluator behavior:

| Policy | Affected implementation | Audit result |
|---|---|---|
| `CMP0017` | `src_v2/evaluator/eval_include.c` | Modeled for `include()` search-order behavior |
| `CMP0048` | `src_v2/evaluator/eval_project.c` | Modeled for `project()` version-variable handling |
| `CMP0124` | `src_v2/evaluator/evaluator.c` | Modeled for `foreach()` loop variable scoping |
| `CMP0126` | `src_v2/evaluator/eval_vars.c` | Modeled for `set(CACHE)` interaction with normal variables |
| `CMP0140` | `src_v2/evaluator/eval_flow.c` | Modeled for `return()` argument handling |

### 6.3 Confirmed missing policy hooks on supported commands

Confirmed gaps:

| Policy | Affected command | Evidence | Impact |
|---|---|---|---|
| `CMP0074` | `find_package` | `src_v2/evaluator/eval_package.c` always consumes `<Pkg>_ROOT`-style roots but has no policy check. | Result-affecting, command must be `PARTIAL`. |
| `CMP0152` | `file(REAL_PATH)` | `src_v2/evaluator/eval_file_fsops.c` has no policy check around `REAL_PATH`. | Result-affecting, parent `file` command must be `PARTIAL`. |

Important clarification:

- `CMP0077` appears in tests and `cmake_policy()` usage examples, but there is no behavior hook for it in the supported command surface.
- This is not a direct defect in a supported command because the command it most directly affects (`option`) is itself missing.
- It does mean the policy engine should not be described as implying broad behavioral parity outside the commands that actually consult it.

## 7. Code health audit

### 7.1 Reimplementation / duplication

Confirmed duplication hotspots:

- `string_hash_compute_temp(...)` exists as an unused static helper in both `src_v2/evaluator/eval_hash.c` and `src_v2/evaluator/eval_string.c`.
- Semver parsing and comparison logic is duplicated across `src_v2/evaluator/eval_policy_engine.c` and `src_v2/evaluator/eval_project.c`.
- Small `emit_event(...)` wrappers are repeated in multiple handler files (`eval_cpack.c`, `eval_directory.c`, `eval_include.c`, `eval_install.c`, `eval_package.c`, `eval_project.c`, `eval_target.c`, `eval_test.c`, `eval_vars.c`), increasing maintenance surface for identical push-and-OOM glue.

Recommended direction:

- Remove or centralize the duplicated hash helper.
- Extract shared semver helpers into a single internal utility.
- Replace repeated `emit_event(...)` wrappers with one common helper or a thin macro if signatures stay identical.

### 7.2 Dead code / stale code

Confirmed dead or stale code indicators:

- `src_v2/evaluator/eval_hash.c`: unused static helper marked `__attribute__((unused))`.
- `src_v2/evaluator/eval_string.c`: duplicate unused static helper marked `__attribute__((unused))`.

These are strong candidates for deletion unless they are intentionally retained for an imminent refactor.

### 7.3 OOM propagation

Most evaluator-owned allocations use `EVAL_OOM_RETURN_IF_NULL(...)`, `arena_da_try_append(...)`, or `ctx_oom(...)` correctly.

Confirmed exception:

- `src_v2/evaluator/eval_file_generate_lock_archive.c` builds fallback `tar` / `zip` / `unzip` command lines using `nob_da_append(...)`.
- `nob_da_append(...)` in `vendor/nob.h` grows its backing storage with `NOB_REALLOC(...)` and enforces success with `NOB_ASSERT(...)`.
- If allocation fails, this path aborts the process instead of marking evaluator OOM through `ctx_oom(...)`.

This is not just stylistic inconsistency; it bypasses the evaluator's documented fatal-OOM contract and can terminate the process outside normal diagnostic handling.

### 7.4 Portability

Confirmed portability risks:

- POSIX-only subprocess backends in `src_v2/evaluator/eval_file_extra.c` and `src_v2/evaluator/eval_file_transfer.c` rely on `fork`, `execvp`, `dprintf`, and `unistd.h`.
- `src_v2/evaluator/eval_file_generate_lock_archive.c` shells out to host `tar`, `zip`, and `unzip` when the archive backend is unavailable.
- `src_v2/evaluator/eval_file_fsops.c` uses `PATH_MAX` and `realpath`, which are not uniformly portable across all non-Linux hosts.
- `__attribute__((unused))` in `src_v2/evaluator/eval_hash.c` and `src_v2/evaluator/eval_string.c` is compiler-specific.

Classification:

- Linux + Windows baseline: mostly acceptable because most high-risk paths are guarded with `_WIN32` branches.
- Non-baseline hosts: elevated risk, especially for POSIX subprocess assumptions and path-size assumptions.
- One baseline-specific asymmetry remains noteworthy: `file(REAL_PATH)` uses different underlying host APIs on Windows vs POSIX, which already contributes to the policy gap around `CMP0152`.

### 7.5 Simplification / code reduction

High-value simplification targets by current file size and complexity:

- `src_v2/evaluator/eval_string.c`: split JSON, regex, hash, and text-transform subdomains.
- `src_v2/evaluator/eval_file.c`: extract path safety, globbing, and copy semantics into tighter internal modules.
- `src_v2/evaluator/eval_try_compile.c`: separate parse/normalize, host-toolchain execution, and child-evaluator execution.
- `src_v2/evaluator/evaluator.c`: separate structural command execution from shared context/state utilities.
- `src_v2/evaluator/eval_list.c`: extract regex-heavy transforms and selectors.
- `src_v2/evaluator/eval_project.c`: separate command handlers from shared project/policy/semver helpers.

These are refactor candidates, not current correctness defects.

## 8. Prioritized findings and recommended remediation

### Finding 1

- Severity: `High`
- Type: `Documentation`, `Compatibility`
- Evidence: `docs/evaluator/evaluator_v2_coverage_status.md`, `src_v2/evaluator/eval_command_caps.c`, `src_v2/parser/parser.h`, `src_v2/evaluator/evaluator.c`, `https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html`
- Finding: the existing coverage document was authoritative only for dispatcher-visible built-ins, but it did not disclose the broader scoped command universe or the missing-command count.
- Impact: readers could misread the document as broad command-surface parity.
- Recommended action: keep the coverage document scoped to the registry-backed surface and add a separate full audit document plus explicit scope note.

### Finding 2

- Severity: `High`
- Type: `Policy`, `Compatibility`
- Evidence: `src_v2/evaluator/eval_package.c`, `https://cmake.org/cmake/help/v3.28/policy/CMP0074.html`, `https://cmake.org/cmake/help/v3.28/command/find_package.html`
- Finding: `find_package` does not model `CMP0074`.
- Impact: valid policy-controlled package searches can resolve differently from CMake.
- Recommended action: document as `PARTIAL` now; later add explicit policy check around package-root lookup.

### Finding 3

- Severity: `Medium`
- Type: `Policy`, `Compatibility`
- Evidence: `src_v2/evaluator/eval_file_fsops.c`, `https://cmake.org/cmake/help/v3.28/policy/CMP0152.html`, `https://cmake.org/cmake/help/v3.28/command/file.html`
- Finding: `file(REAL_PATH)` does not model `CMP0152`.
- Impact: valid policy-controlled path resolution can diverge from CMake.
- Recommended action: document `file` as `PARTIAL`; later thread policy-aware behavior into `handle_file_real_path(...)`.

### Finding 4

- Severity: `High`
- Type: `OOM`
- Evidence: `src_v2/evaluator/eval_file_generate_lock_archive.c`, `vendor/nob.h`
- Finding: fallback archive command assembly uses `nob_da_append(...)`, which can abort through `NOB_ASSERT(...)` instead of propagating evaluator OOM.
- Impact: crash / abort risk under allocation failure.
- Recommended action: replace `nob_da_append(...)` in evaluator runtime code with checked append helpers that call `ctx_oom(...)`.

### Finding 5

- Severity: `Low`
- Type: `Dead code`, `Duplication`
- Evidence: `src_v2/evaluator/eval_hash.c`, `src_v2/evaluator/eval_string.c`
- Finding: duplicated unused hash helper exists in two translation units.
- Impact: maintainability risk only.
- Recommended action: delete it or move to one shared helper once a caller exists.

### Finding 6

- Severity: `Low`
- Type: `Compatibility`
- Evidence: `src_v2/evaluator/evaluator.c`, `https://cmake.org/cmake/help/v3.28/command/while.html`
- Finding: `while()` has a hard evaluator-specific iteration cap (`10000`).
- Impact: valid long-running loops can terminate differently from CMake.
- Recommended action: keep documented as a structural divergence until the loop guard is made configurable or semantics are revisited.

## 9. Confidence and unresolved items

Confidence level for this document: medium-high.

High-confidence statements in this audit:

- Full scoped command counts (`131` total, `57` implemented, `74` missing).
- Registry alignment between `eval_command_caps.c` and `eval_dispatcher.c`.
- Policy registry alignment for `CMP0000..CMP0155`.
- Confirmed policy gaps for `CMP0074` and `CMP0152`.
- Confirmed OOM propagation hazard around `nob_da_append(...)`.

Known limits:

- No installed `cmake` binary was available for runtime side-by-side execution.
- Structural language commands were verified for presence and obvious divergences, but not exhaustively re-matrixed against every edge case in the official manuals.
- Additional policy-sensitive gaps may exist beyond the ones confirmed here; this audit records the confirmed set, not a proof of absence.

Next follow-up after this document:

- keep `evaluator_v2_coverage_status.md` aligned with the confirmed partials and the clarified command-surface scope
- use this audit as the backlog input for a later code-fix pass
