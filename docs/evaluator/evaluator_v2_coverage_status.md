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
| `add_custom_command` | `PARTIAL` | `ERROR_CONTINUE` | Supports `TARGET`/`OUTPUT` signatures; not full CMake permutation parity. | `Medium` |
| `add_custom_target` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set, including scheduler options (`JOB_POOL`, `JOB_SERVER_AWARE`). | - |
| `add_definitions` | `PARTIAL` | `NOOP_WARN` | Treated as raw compile options only; CMake's compile-definition-oriented behavior and legacy conversion nuances are not fully mirrored. | `Low` |
| `add_executable` | `PARTIAL` | `NOOP_WARN` | Valid signatures `IMPORTED` and `ALIAS` are not implemented; option/property semantics are reduced. | `Critical` |
| `add_library` | `PARTIAL` | `NOOP_WARN` | Valid signatures/behaviors (`IMPORTED`, `ALIAS`, and broader type/property surface) are not fully implemented. | `Critical` |
| `add_link_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option handling (`SHELL:`/`LINKER:` expansion + de-duplication). | - |
| `add_subdirectory` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature subset (`source_dir [binary_dir] [EXCLUDE_FROM_ALL] [SYSTEM]`). | - |
| `add_test` | `PARTIAL` | `ERROR_CONTINUE` | Main signatures implemented; unsupported extra args/options are warning+ignore fallback rather than full CMake option-surface parity. | `Low` |
| `block` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set (`SCOPE_FOR`, `PROPAGATE`). | - |
| `break` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`break()`). | - |
| `cmake_minimum_required` | `FULL` | `NOOP_WARN` | Signature/validation parity implemented for `VERSION <min>[...<max>] [FATAL_ERROR]`; implicit policy-version application matches evaluator CMake 3.28 baseline model. | - |
| `cmake_path` | `PARTIAL` | `ERROR_CONTINUE` | Subset of modes implemented (see subcommand matrix). | `Medium` |
| `cmake_policy` | `FULL` | `NOOP_WARN` | `VERSION|SET|GET|PUSH|POP` parity implemented with strict arity/known-policy validation and full CMake 3.28 policy registry (`CMP0000..CMP0155`). | - |
| `continue` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`continue()`). | - |
| `cpack_add_component` | `PARTIAL` | `NOOP_WARN` | Valid options `ARCHIVE_FILE` and `PLIST` are missing from implementation. | `Medium` |
| `cpack_add_component_group` | `PARTIAL` | `NOOP_WARN` | In CMake these commands are provided by `CPackComponent` module; evaluator exposes them as always-available built-ins. | `Medium` |
| `cpack_add_install_type` | `PARTIAL` | `NOOP_WARN` | In CMake these commands are provided by `CPackComponent` module; evaluator exposes them as always-available built-ins. | `Medium` |
| `enable_testing` | `PARTIAL` | `NOOP_WARN` | Evaluator additionally sets `BUILD_TESTING=1`, which can alter script-visible variable behavior compared to native CMake command semantics. | `Low` |
| `endblock` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`endblock()`). | - |
| `file` | `PARTIAL` | `ERROR_CONTINUE` | Broad subcommand set with documented deltas (see matrix). | `Medium` |
| `find_package` | `PARTIAL` | `ERROR_CONTINUE` | Core resolver flow implemented; option/discovery parity not complete. | `Medium` |
| `include` | `PARTIAL` | `ERROR_CONTINUE` | `OPTIONAL` and `NO_POLICY_SCOPE` implemented; not full option parity. | `Medium` |
| `include_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling (`SYSTEM`, `BEFORE|AFTER`, relative canonicalization). | - |
| `include_guard` | `PARTIAL` | `NOOP_WARN` | Default no-arg scope in CMake matches variable scope, while evaluator defaults to directory-style guarding; unsupported modes are downgraded to warning+fallback. | `Medium` |
| `install` | `PARTIAL` | `NOOP_WARN` | CMake 3.28 install command surface is much broader; implementation only supports a reduced subset of signatures/options. | `Critical` |
| `link_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling (`BEFORE|AFTER`, relative canonicalization). | - |
| `link_libraries` | `PARTIAL` | `NOOP_WARN` | CMake item qualifiers (`debug`, `optimized`, `general`) and broader semantic handling are not implemented. | `Medium` |
| `list` | `PARTIAL` | `NOOP_WARN` | Subcommand/action coverage is incomplete relative to CMake 3.28 list command behavior. | `Medium` |
| `math` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`math(EXPR <var> "<expr>" [OUTPUT_FORMAT ...])`). | - |
| `message` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented modes (`NOTICE/STATUS/VERBOSE/DEBUG/TRACE/WARNING/AUTHOR_WARNING/DEPRECATION/SEND_ERROR/FATAL_ERROR/CHECK_*/CONFIGURE_LOG`). | - |
| `project` | `PARTIAL` | `NOOP_WARN` | Reduced signature support (e.g. missing `HOMEPAGE_URL`, reduced language-signature handling and variable surface). | `Medium` |
| `return` | `PARTIAL` | `NOOP_WARN` | CMake states `macro()` cannot handle `return()`; evaluator allows it (warning only), changing control-flow outcome in macro contexts. | `Medium` |
| `set` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`set(<var> <value>... [PARENT_SCOPE])`, `set(<var> <value>... CACHE <type> <doc> [FORCE])`, `set(ENV{<var>} [<value>])`). | - |
| `set_property` | `PARTIAL` | `ERROR_CONTINUE` | Scope coverage exists; parity for all CMake property semantics is not complete. | `Medium` |
| `set_target_properties` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`set_target_properties(<targets>... PROPERTIES <k> <v>...)`). | - |
| `string` | `PARTIAL` | `NOOP_WARN` | CMake 3.28 string command surface is broader (hash family, `REPEAT`, JSON modes/options) than current implementation. | `Medium` |
| `target_compile_definitions` | `PARTIAL` | `NOOP_WARN` | CMake normalization rules (e.g. `-D` handling nuances and related semantics) are not fully mirrored. | `Low` |
| `target_compile_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature, including `[BEFORE]`. | - |
| `target_include_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling in covered signature surface. | - |
| `target_link_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling in covered signature surface. | - |
| `target_link_libraries` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented item qualifiers (`debug|optimized|general`). | - |
| `target_link_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature, including `[BEFORE]`. | - |
| `try_compile` | `PARTIAL` | `NOOP_WARN` | Evaluator uses simulated compile success/failure logic instead of native compile pipeline semantics. | `Critical` |
| `unset` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`unset(<var> [CACHE|PARENT_SCOPE])`, `unset(ENV{<var>})`). | - |

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
| `DOWNLOAD` | `PARTIAL` | Local backend only; remote URL unsupported without external backend. | Error diagnostic + continue. |
| `UPLOAD` | `PARTIAL` | Local backend only; remote URL unsupported without external backend. | Error diagnostic + continue. |
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
| `UUID` | Supported | Supported (`MD5|SHA1`) | None in supported mode |
| `JSON` | `GET/TYPE/MEMBER/LENGTH/REMOVE/SET/EQUAL` (+ `ERROR_VARIABLE`) | `GET/TYPE/LENGTH` subset, no `ERROR_VARIABLE` handling | `Medium` |

## 4. Coverage details: `list()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Core list mutation/query (`APPEND/PREPEND/INSERT/REMOVE_*/LENGTH/GET/FIND/JOIN/SUBLIST/POP_*/REVERSE/SORT`) | Supported | Supported | None for covered forms |
| `FILTER` | Full command semantics include mode constraints and regex behavior | Implemented with `INCLUDE|EXCLUDE REGEX` subset | `Low` |
| `TRANSFORM` | Broad action set | Action/selector subset only | `Medium` |

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

Current evaluator `install()` handler supports only a reduced subset.

| Signature family | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| `install(TARGETS|FILES|PROGRAMS|DIRECTORY ... DESTINATION ...)` | Supported | Supported (reduced options) | `Medium` |
| Advanced signatures (`SCRIPT`, `CODE`, `EXPORT`, `RUNTIME_DEPENDENCY_SET`, `IMPORTED_RUNTIME_ARTIFACTS`, etc.) | Supported | Missing | `Critical` |
| Per-artifact option semantics (`CONFIGURATIONS`, `PERMISSIONS`, `COMPONENT`, etc.) | Supported | Not fully modeled | `Medium` |

## 8. Coverage details: target command family

### `set_target_properties()`

| Signature | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| `set_target_properties(<targets>... PROPERTIES <k> <v>...)` | Supported | Supported in evaluator event model | None for nominal signature |
| Alias-target restriction | Alias targets do not allow setting properties | Restriction enforced (error diagnostic, no property event for alias target) | None |

### Target usage-requirement commands

| Command | Key CMake 3.28 semantic | Evaluator gap | Impact |
|---|---|---|---|
| `target_compile_definitions` | Scope-based items with normalization behavior | Normalization nuances not fully mirrored | `Low` |
| `target_compile_options` | Supports `[BEFORE]` and scope grouping | No known result-affecting gap in covered signature surface | - |
| `target_include_directories` | Scope, `SYSTEM`, order, path semantics | No known result-affecting gap in covered signature surface | - |
| `target_link_directories` | Scope, order, path semantics | No known result-affecting gap in covered signature surface | - |
| `target_link_libraries` | Rich item classification (`debug/optimized/general`, etc.) | No known result-affecting gap for qualifier handling | - |
| `target_link_options` | Supports `[BEFORE]` and scope grouping | No known result-affecting gap in covered signature surface | - |

## 9. Coverage details: `project()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Core name/version/description/languages | Supported | Supported subset | None for covered subset |
| Additional signature surface (`HOMEPAGE_URL`, full language forms, related variable surface) | Supported | Reduced | `Medium` |

## 10. Coverage details: `try_compile()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Full compile pipeline semantics | Actual configure/compile checks | Simulated result based on source/CMakeLists existence and simplified flow | `Critical` |
| Signature parsing | Broad signature support | Broad parsing exists, but execution model is simulated | `Critical` |

## 11. Commands currently `FULL`

`add_compile_options`, `add_custom_target`, `add_link_options`, `add_subdirectory`, `block`, `break`, `cmake_minimum_required`, `cmake_policy`, `continue`, `endblock`, `include_directories`, `link_directories`, `math`, `message`, `set`, `set_target_properties`, `target_compile_options`, `target_include_directories`, `target_link_directories`, `target_link_libraries`, `target_link_options`, `unset`.

All other commands in the matrix are currently documented as `PARTIAL`.

## 12. Consistency checks required for future updates

- Keep command list and fallback values aligned with current evaluator runtime (`eval_command_caps.c` + handlers).
- Keep `Capability Level` aligned with documented CMake 3.28 behavior comparison.
- If any new result-affecting divergence is identified (even `Low`), command must be `PARTIAL`.
- For complex commands, maintain subcommand/signature coverage tables as behavior evolves.
