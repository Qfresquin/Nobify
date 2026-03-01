# Evaluator v2 Coverage Status

Status snapshot sources:
- `src_v2/evaluator/eval_command_caps.c`
- command handlers in `src_v2/evaluator/*.c`
- CMake command reference `v3.28` (`https://cmake.org/cmake/help/v3.28/command/`)
- CPack component command reference `v3.28` (`https://cmake.org/cmake/help/v3.28/module/CPackComponent.html`)

## Coverage criteria used in this document

- Baseline semantics: CMake 3.28.
- Only result-affecting divergences are considered.
- Architectural-only differences are ignored when they do not change observable result.
- Impact tag `Critical`: common valid CMake usage changes outcome.
- Impact tag `Medium`: valid but narrower usage changes outcome.
- Impact tag `Low`: edge-case valid usage changes outcome.
- Any known result-affecting divergence (`Low`/`Medium`/`Critical`) sets command level to `PARTIAL`.

## Legend

- `Capability Level`: effective documented coverage level after CMake 3.28 comparison.

## 1. Command-Level Matrix (Authoritative)

| Command | Capability Level | Fallback | Functional Divergence vs CMake 3.28 | Impact |
|---|---|---|---|---|
| `add_compile_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option handling (`SHELL:` expansion + de-duplication). | - |
| `add_custom_command` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented evaluator subset of CMake 3.28 signatures: strict `TARGET`/`OUTPUT` signature validation, stage/target checks for `TARGET`, and conflict validation in `OUTPUT` (`IMPLICIT_DEPENDS` pairs, `DEPFILE` conflict, `JOB_POOL` vs `USES_TERMINAL`). | - |
| `add_custom_target` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set, including scheduler options (`JOB_POOL`, `JOB_SERVER_AWARE`). | - |
| `add_definitions` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented legacy behavior: `-D`/`/D` items are routed as compile definitions and non-definition flags remain compile options. | - |
| `add_executable` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`normal`, `IMPORTED [GLOBAL]`, `ALIAS`) and option handling (`WIN32`, `MACOSX_BUNDLE`, `EXCLUDE_FROM_ALL`). | - |
| `add_library` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`normal`, `OBJECT`, `INTERFACE`, `IMPORTED [GLOBAL]`, `ALIAS`) and default-type behavior (`BUILD_SHARED_LIBS`) in covered surface. | - |
| `add_link_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option handling (`SHELL:`/`LINKER:` expansion + de-duplication). | - |
| `add_subdirectory` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature subset (`source_dir [binary_dir] [EXCLUDE_FROM_ALL] [SYSTEM]`). | - |
| `add_test` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`add_test(NAME ... COMMAND ... [CONFIGURATIONS ...] [WORKING_DIRECTORY ...] [COMMAND_EXPAND_LISTS])` and legacy `add_test(<name> <command> [<arg>...])`) with strict unexpected-argument validation in NAME form. | - |
| `block` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set (`SCOPE_FOR`, `PROPAGATE`). | - |
| `break` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`break()`). | - |
| `cmake_minimum_required` | `FULL` | `NOOP_WARN` | Signature/validation parity implemented for `VERSION <min>[...<max>] [FATAL_ERROR]`; implicit policy-version application matches evaluator CMake 3.28 baseline model. | - |
| `cmake_path` | `PARTIAL` | `ERROR_CONTINUE` | Subset of modes implemented (see subcommand matrix). | `Medium` |
| `cmake_policy` | `FULL` | `NOOP_WARN` | `VERSION SET GET PUSH POP` parity implemented with strict arity/known-policy validation and full CMake 3.28 policy registry (`CMP0000..CMP0155`). | - |
| `continue` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`continue()`). | - |
| `cpack_add_component` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set, including `ARCHIVE_FILE` and `PLIST`, with availability gated by `include(CPackComponent)`. | - |
| `cpack_add_component_group` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set, with availability gated by `include(CPackComponent)`. | - |
| `cpack_add_install_type` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set, with availability gated by `include(CPackComponent)`. | - |
| `enable_testing` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`enable_testing()`). | - |
| `endblock` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`endblock()`). | - |
| `file` | `PARTIAL` | `ERROR_CONTINUE` | Broad subcommand set with documented deltas (see matrix). | `Medium` |
| `find_package` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented evaluator subset (`AUTO MODULE CONFIG NO_MODULE`, `REQUIRED QUIET`, version/`EXACT`, components, `NAMES CONFIGS HINTS PATHS PATH_SUFFIXES`, `NO_*` path toggles, and `CMAKE_FIND_PACKAGE_PREFER_CONFIG`). | - |
| `include` | `PARTIAL` | `ERROR_CONTINUE` | `OPTIONAL` and `NO_POLICY_SCOPE` implemented; not full option parity. | `Medium` |
| `include_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling (`SYSTEM`, `BEFORE AFTER`, relative canonicalization). | - |
| `include_guard` | `PARTIAL` | `NOOP_WARN` | Default no-arg scope in CMake matches variable scope, while evaluator defaults to directory-style guarding; unsupported modes are downgraded to warning+fallback. | `Medium` |
| `install` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented evaluator event-model install surface (core + advanced signatures emitted as install rules). | - |
| `link_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling (`BEFORE AFTER`, relative canonicalization). | - |
| `link_libraries` | `PARTIAL` | `NOOP_WARN` | CMake item qualifiers (`debug`, `optimized`, `general`) and broader semantic handling are not implemented. | `Medium` |
| `list` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented subcommand surface, including `FILTER` and `TRANSFORM` actions/selectors (`GENEX_STRIP`, `OUTPUT_VARIABLE`). | - |
| `math` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`math(EXPR <var> "<expr>" [OUTPUT_FORMAT ...])`). | - |
| `message` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented modes (`NOTICE/STATUS/VERBOSE/DEBUG/TRACE/WARNING/AUTHOR_WARNING/DEPRECATION/SEND_ERROR/FATAL_ERROR/CHECK_*/CONFIGURE_LOG`). | - |
| `project` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature surface (`VERSION`, `DESCRIPTION`, `HOMEPAGE_URL`, short/long language forms including `LANGUAGES NONE`) and documented project-variable surface (`PROJECT_*`, `<PROJECT-NAME>_*`, top-level `CMAKE_PROJECT_*`, including `CMP0048`-driven no-`VERSION` behavior). | - |
| `return` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented behavior (`return()`, `return(PROPAGATE ...)`, `CMP0140` argument handling, and macro-context rejection). | - |
| `set` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`set(<var> <value>... [PARENT_SCOPE])`, `set(<var> <value>... CACHE <type> <doc> [FORCE])`, `set(ENV{<var>} [<value>])`). | - |
| `set_property` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented scope/signature surface (`GLOBAL DIRECTORY TARGET SOURCE INSTALL TEST CACHE`, `APPEND APPEND_STRING`, `PROPERTY ...`), including zero-object scope handling and target/cache/test validations in covered flow. | - |
| `set_target_properties` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`set_target_properties(<targets>... PROPERTIES <k> <v>...)`). | - |
| `string` | `PARTIAL` | `NOOP_WARN` | CMake 3.28 string command surface is broader (hash family, `REPEAT`, JSON modes/options) than current implementation. | `Medium` |
| `target_compile_definitions` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented normalization behavior (leading `-D` removal and empty-item ignore). | - |
| `target_compile_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature, including `[BEFORE]`. | - |
| `target_include_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling in covered signature surface. | - |
| `target_link_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling in covered signature surface. | - |
| `target_link_libraries` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented item qualifiers (`debug optimized general`). | - |
| `target_link_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature, including `[BEFORE]`. | - |
| `try_compile` | `PARTIAL` | `NOOP_WARN` | Evaluator uses simulated compile success/failure logic instead of native compile pipeline semantics. | `Critical` |
| `unset` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`unset(<var> [CACHE PARENT_SCOPE])`, `unset(ENV{<var>})`). | - |

## 2. Subcommand Matrix: `file()`

| Subcommand | Status | Delta / Notes | Typical fallback/diag |
|---|---|---|---|
| `GLOB` | `FULL` | Host filesystem globbing with project-scoped safety handling. | Error/Warning diagnostic, continue by profile. |
| `GLOB_RECURSE` | `FULL` | Recursive glob implemented. | Error/Warning diagnostic, continue by profile. |
| `READ` | `FULL` | Offset/limit/hex handling implemented. | Error diagnostic on read failures. |
| `STRINGS` | `PARTIAL` | Core behavior implemented; some options are warned as unsupported. | Warning + continue. |
| `COPY` | `PARTIAL` | Implemented with filters/permissions subset; some permission modes not implemented. | Warning/error + continue. |
| `WRITE` | `FULL` | Writes content to host filesystem immediately. | Error on IO failure. |
| `APPEND` | `FULL` | Append semantics implemented. | Error on IO failure. |
| `MAKE_DIRECTORY` | `FULL` | Multi-directory creation implemented. | Error on failure. |
| `INSTALL` | `PARTIAL` | Pragmatic mapping to copy-like flow; not full CMake install parity. | Warning/error + continue. |
| `SIZE` | `FULL` | File size query implemented. | Error on stat failure. |
| `RENAME` | `FULL` | Rename with basic options implemented. | Error on failure. |
| `REMOVE` | `FULL` | Remove paths implemented. | Error on failure. |
| `REMOVE_RECURSE` | `FULL` | Recursive remove implemented. | Error on failure. |
| `READ_SYMLINK` | `PARTIAL` | Not implemented on Windows backend. | Warning/error + continue. |
| `CREATE_LINK` | `PARTIAL` | Platform-specific behavior; `COPY_ON_ERROR` path approximated. | Warning/error + continue. |
| `CHMOD` | `PARTIAL` | Permission-token handling implemented; platform caveats remain. | Warning/error + continue. |
| `CHMOD_RECURSE` | `PARTIAL` | Recursive chmod with same caveats as `CHMOD`. | Warning/error + continue. |
| `REAL_PATH` | `FULL` | Real-path resolution with options implemented. | Error on failure. |
| `RELATIVE_PATH` | `FULL` | Relative path computation implemented. | Error on invalid usage. |
| `TO_CMAKE_PATH` | `FULL` | Path conversion implemented. | Error on invalid usage. |
| `TO_NATIVE_PATH` | `FULL` | Path conversion implemented. | Error on invalid usage. |
| `DOWNLOAD` | `PARTIAL` | Local + remote transfer supported (remote via `curl` backend); unsupported option surface remains (`EXPECTED_HASH`/`EXPECTED_MD5` and broader CMake transport options). | Error diagnostic + continue (`STATUS` path returns non-fatal status). |
| `UPLOAD` | `PARTIAL` | Local + remote transfer supported (remote via `curl` backend); full CMake option/transport parity is not implemented. | Error diagnostic + continue (`STATUS` path returns non-fatal status). |
| `TIMESTAMP` | `FULL` | Timestamp read/format behavior implemented. | Error on stat failure. |
| `GENERATE` | `PARTIAL` | Core generation implemented with option constraints. | Warning/error + continue. |
| `LOCK` | `PARTIAL` | Advisory/local lock semantics; backend/platform approximations. | Warning/error + continue. |
| `ARCHIVE_CREATE` | `PARTIAL` | Pragmatic tar backend subset; format/compression limits. | Error diagnostic + continue. |
| `ARCHIVE_EXTRACT` | `PARTIAL` | Pragmatic tar backend subset. | Error diagnostic + continue. |
| Other `file()` subcommands | `MISSING` | Not currently routed by evaluator `file()` handler chain. | Unsupported subcommand warning. |

## 3. Coverage details: `string()`

CMake 3.28 supports a broader `string()` surface than current evaluator implementation.

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Text manipulation core (`APPEND/PREPEND/CONCAT/JOIN/LENGTH/STRIP/FIND/REPLACE/TOUPPER/TOLOWER/SUBSTRING`) | Supported | Supported | None for these forms |
| Regex (`MATCH/MATCHALL/REPLACE`) | Supported | Supported | None for these forms |
| Hash family | Generic `<HASH>` family (`MD5`, `SHA1`, `SHA224`, `SHA256`, `SHA384`, `SHA512`, etc.) | `MD5`, `SHA1`, `SHA256` only | `Medium` |
| `REPEAT` | Supported | Missing | `Low` |
| `UUID` | Supported | Supported (`MD5 SHA1`) | None in supported mode |
| `JSON` | `GET/TYPE/MEMBER/LENGTH/REMOVE/SET/EQUAL` (+ `ERROR_VARIABLE`) | `GET/TYPE/LENGTH` subset, no `ERROR_VARIABLE` handling | `Medium` |

## 4. Coverage details: `list()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Core list mutation/query (`APPEND/PREPEND/INSERT/REMOVE_*/LENGTH/GET/FIND/JOIN/SUBLIST/POP_*/REVERSE/SORT`) | Supported | Supported | None for covered forms |
| `FILTER` | `INCLUDE|EXCLUDE REGEX` | Supported | None |
| `TRANSFORM` | Actions/selectors + `OUTPUT_VARIABLE` | Supported (`APPEND`, `PREPEND`, `TOLOWER`, `TOUPPER`, `STRIP`, `GENEX_STRIP`, `REPLACE`; selectors `AT FOR REGEX`; optional `OUTPUT_VARIABLE`) | None |

## 4.1 Coverage details: `find_package()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Mode selection (`AUTO`, `MODULE`, `CONFIG`, `NO_MODULE`) | Supported | Supported, including `CMAKE_FIND_PACKAGE_PREFER_CONFIG` priority in `AUTO` mode | None for covered forms |
| Name/config selection (`NAMES`, `CONFIGS`) | Supported | Supported | None for covered forms |
| Config search shaping (`HINTS`, `PATHS`, `PATH_SUFFIXES`, `NO_*` path toggles) | Supported | Supported in local filesystem resolver | None for covered forms |
| Version checks (`[version]`, `EXACT`, config-version file evaluation) | Supported | Supported | None for covered forms |
| Find-context variables used by package scripts | Supported | Supported for covered subset (including `<Pkg>_FIND_REGISTRY_VIEW` when requested) | None for covered forms |

## 5. Coverage details: `math()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| `math(EXPR <var> "<expr>" [OUTPUT_FORMAT ...])` | Supported | Supported | None for main signature |
| Output format | `DECIMAL`/`HEXADECIMAL` | Supported (including legacy trailing form) | None for normal usage |
| Empty/invalid invocation edges | Hard command error on invalid arity | Hard command error on invalid arity | None |

## 6. Coverage details: `set()` / `unset()`

### `set()`

| Signature | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| `set(<var> <value>... [PARENT_SCOPE])` | Supported | Supported | None for this signature |
| `set(<var> <value>... CACHE <type> <doc> [FORCE])` | Supported | Supported (including `INTERNAL` implies `FORCE`, untyped-cache type promotion, and `CMP0126` OLD/NEW local-binding behavior) | None for this signature |
| `set(ENV{<var>} [<value>])` | Supported | Supported | None for this signature |

### `unset()`

| Signature | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| `unset(<var> [CACHE|PARENT_SCOPE])` | Supported | Supported | None for this signature |
| `unset(ENV{<var>})` | Supported | Supported | None for this signature |

## 7. Coverage details: `install()`

Evaluator `install()` handler covers core and advanced signature families in the current event-model contract.

| Signature family | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| `install(TARGETS FILES PROGRAMS DIRECTORY ... DESTINATION TYPE ...)` | Supported | Supported | None in covered event-model contract |
| Advanced signatures (`SCRIPT`, `CODE`, `EXPORT`, `EXPORT_ANDROID_MK`, `RUNTIME_DEPENDENCY_SET`, `IMPORTED_RUNTIME_ARTIFACTS`) | Supported | Supported | None in covered event-model contract |
| Option-rich forms (artifact groups, destination clauses, component/config/perms families) | Supported | Parsed and represented through install-rule emission in evaluator event model | None in covered event-model contract |

## 8. Coverage details: target command family

### `set_property()`

| Signature area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Scope/signature surface (`GLOBAL DIRECTORY TARGET SOURCE INSTALL TEST CACHE`, `APPEND APPEND_STRING`, `PROPERTY`) | Supported | Supported | None for covered signature surface |
| Zero-object semantics | Supported for applicable scopes | Supported (no-op behavior) | None |
| Target/test/cache validation in covered flow | Existing target/test/cache entry requirements apply by scope | Enforced with error diagnostics before property application | None |

### `set_target_properties()`

| Signature | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| `set_target_properties(<targets>... PROPERTIES <k> <v>...)` | Supported | Supported in evaluator event model | None for nominal signature |
| Alias-target restriction | Alias targets do not allow setting properties | Restriction enforced (error diagnostic, no property event for alias target) | None |

### Target usage-requirement commands

| Command | Key CMake 3.28 semantic | Evaluator gap | Impact |
|---|---|---|---|
| `target_compile_definitions` | Scope-based items with normalization behavior | No known result-affecting gap in covered normalization behavior | - |
| `target_compile_options` | Supports `[BEFORE]` and scope grouping | No known result-affecting gap in covered signature surface | - |
| `target_include_directories` | Scope, `SYSTEM`, order, path semantics | No known result-affecting gap in covered signature surface | - |
| `target_link_directories` | Scope, order, path semantics | No known result-affecting gap in covered signature surface | - |
| `target_link_libraries` | Rich item classification (`debug /optimized/general`, etc.) | No known result-affecting gap for qualifier handling | - |
| `target_link_options` | Supports `[BEFORE]` and scope grouping | No known result-affecting gap in covered signature surface | - |

## 9. Coverage details: `project()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Signature forms (`project(<name> [<lang>...])` and keyword form with `VERSION`, `DESCRIPTION`, `HOMEPAGE_URL`, `LANGUAGES`) | Supported | Supported | None for covered forms |
| Language handling (`LANGUAGES NONE`, omitted `LANGUAGES` defaults, explicit language lists) | Supported | Supported | None for covered forms |
| Variable surface (`PROJECT_*`, `<PROJECT-NAME>_*`, top-level `CMAKE_PROJECT_*`) | Supported | Supported | None for covered forms |
| `CMP0048` no-`VERSION` behavior | Supported | Supported (`NEW` clears version vars when omitted; `OLD` preserves prior values when omitted) | None for covered forms |

## 10. Coverage details: `try_compile()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Full compile pipeline semantics | Actual configure/compile checks | Simulated result based on source/CMakeLists existence and simplified flow | `Critical` |
| Signature parsing | Broad signature support | Broad parsing exists, but execution model is simulated | `Critical` |

## 11. Commands currently `FULL`

`add_compile_options`, `add_custom_command`, `add_custom_target`, `add_definitions`, `add_executable`, `add_library`, `add_link_options`, `add_subdirectory`, `add_test`, `block`, `break`, `cmake_minimum_required`, `cmake_policy`, `continue`, `cpack_add_component`, `cpack_add_component_group`, `cpack_add_install_type`, `enable_testing`, `endblock`, `find_package`, `include_directories`, `install`, `link_directories`, `list`, `math`, `message`, `project`, `return`, `set`, `set_property`, `set_target_properties`, `target_compile_definitions`, `target_compile_options`, `target_include_directories`, `target_link_directories`, `target_link_libraries`, `target_link_options`, `unset`.

All other commands in the matrix are currently documented as `PARTIAL`.

## 12. Consistency checks required for future updates

- Keep command list and fallback values aligned with current evaluator runtime (`eval_command_caps.c` + handlers).
- Keep `Capability Level` aligned with documented CMake 3.28 behavior comparison.
- If any new result-affecting divergence is identified (even `Low`), command must be `PARTIAL`.
- For complex commands, maintain subcommand/signature coverage tables as behavior evolves.
