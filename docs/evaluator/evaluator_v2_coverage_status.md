# Evaluator v2 Coverage Status

Status snapshot sources:
- `src_v2/evaluator/eval_command_caps.c`
- command handlers in `src_v2/evaluator/*.c`
- CMake command reference `v3.28` (`https://cmake.org/cmake/help/v3.28/manual/cmake-commands.7.html`)
- CPack component command reference `v3.28` (`https://cmake.org/cmake/help/v3.28/module/CPackComponent.html`)
- Full audit companion: `evaluator_v2_full_audit.md`

## Coverage criteria used in this document

- Baseline semantics: CMake 3.28.
- Validation scope for this release: Linux + Windows (macOS intentionally out of scope).
- Only result-affecting divergences are considered.
- Architectural-only differences are ignored when they do not change observable result.
- Impact tag `Critical`: common valid CMake usage changes outcome.
- Impact tag `Medium`: valid but narrower usage changes outcome.
- Impact tag `Low`: edge-case valid usage changes outcome.
- Any known result-affecting divergence (`Low`/`Medium`/`Critical`) sets command level to `PARTIAL`.

## Legend

- `Capability Level`: effective documented coverage level after CMake 3.28 comparison.

## Scope note

- This matrix is authoritative for the `80` dispatcher-registered built-in commands exposed by `src_v2/evaluator/eval_command_caps.c`.
- It does not by itself represent the entire CMake command universe.
- The broader audit scope is tracked in `evaluator_v2_full_audit.md`: `131` scoped documented entry points (`128` from `cmake-commands(7)` + `3` `CPackComponent` module commands), of which `92` are currently implemented (`80` registry-backed + `12` structural parser/evaluator commands) and `39` remain missing.
- Structural language commands such as `if()`/`foreach()`/`while()`/`function()`/`macro()` are implemented outside the dispatcher and are audited in the full report, not in this registry-backed matrix.

## 1. Command-Level Matrix (Authoritative)

| Command | Capability Level | Fallback | Functional Divergence vs CMake 3.28 | Impact |
|---|---|---|---|---|
| `add_compile_definitions` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented directory-scope behavior: definitions are normalized (leading `-D` removed, empty items ignored), applied to existing directory targets, and retained for future targets in the current directory scope. | - |
| `add_dependencies` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented CMake 3.28 signature (`add_dependencies(<target> <target-dependency>...)`), including required dependency arity and non-ALIAS target validation in covered flow. | - |
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
| `build_command` | `PARTIAL` | `NOOP_WARN` | The evaluator models the modern `cmake --build .` composition path rooted on `CMAKE_COMMAND`, including `TARGET`, `CONFIGURATION`, `PARALLEL_LEVEL`, and `CMP0061` (`OLD` may append legacy `-- -i` for makefile generators; `NEW` omits it). Legacy positionals are accepted only as a narrow compatibility parse path, and `PROJECT_NAME` is parsed but ignored. | `Medium` |
| `build_name` | `PARTIAL` | `NOOP_WARN` | `CMP0036` is modeled: `NEW` hard-errors because the command is disallowed, while `OLD` publishes a deterministic synthetic legacy-compatible value (`<CMAKE_HOST_SYSTEM_NAME>-<CMAKE_CXX_COMPILER_ID>`) instead of reproducing byte-for-byte historical formatting. | `Medium` |
| `cmake_host_system_information` | `PARTIAL` | `NOOP_WARN` | The evaluator implements a deterministic local subset (`NUMBER_OF_LOGICAL_CORES`, `HOSTNAME`, memory totals/available values, `IS_64BIT`, `OS_NAME`, `OS_RELEASE`, `OS_VERSION`, `OS_PLATFORM`, and `MSYSTEM_PREFIX` on Windows). Unsupported query keys emit explicit diagnostics and are returned as empty slots in the ordered result list. | `Medium` |
| `cmake_language` | `PARTIAL` | `ERROR_CONTINUE` | Core `CALL`, `EVAL CODE`, `GET_MESSAGE_LOG_LEVEL`, and documented `DEFER` queueing surface are implemented, but subcommands outside the baseline queue flow (notably dependency-provider integration) remain incomplete. | `Medium` |
| `cmake_minimum_required` | `FULL` | `NOOP_WARN` | Signature/validation parity implemented for `VERSION <min>[...<max>] [FATAL_ERROR]`; implicit policy-version application matches evaluator CMake 3.28 baseline model. | - |
| `cmake_parse_arguments` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented direct and `PARSE_ARGV` signatures, including keyword collision warnings, `UNPARSED_ARGUMENTS`, `KEYWORDS_MISSING_VALUES`, and pre-`CMP0174` empty-string handling appropriate for the 3.28 baseline. | - |
| `cmake_path` | `FULL` | `ERROR_CONTINUE` | No result-affecting divergence found for the documented CMake 3.28.6 surface implemented in evaluator v2, including mutating path transforms, `CONVERT`, `COMPARE`, and `HAS_*` / `IS_ABSOLUTE` predicates in validated Linux+Windows scope. | - |
| `cmake_policy` | `FULL` | `NOOP_WARN` | `VERSION SET GET PUSH POP` parity implemented with strict arity/known-policy validation and full CMake 3.28 policy registry (`CMP0000..CMP0155`). | - |
| `configure_file` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented configure/copy semantics in validated Linux+Windows scope, including `COPYONLY`, `@ONLY`, `ESCAPE_QUOTES`, `NEWLINE_STYLE`, file-permission options, output-directory resolution, and `#cmakedefine` / `#cmakedefine01` transforms. | - |
| `continue` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`continue()`). | - |
| `cpack_add_component` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set, including `ARCHIVE_FILE` and `PLIST`, with availability gated by `include(CPackComponent)`. | - |
| `cpack_add_component_group` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set, with availability gated by `include(CPackComponent)`. | - |
| `cpack_add_install_type` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented option set, with availability gated by `include(CPackComponent)`. | - |
| `define_property` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented scope definitions (`GLOBAL`, `DIRECTORY`, `TARGET`, `SOURCE`, `TEST`, `VARIABLE`, `CACHED_VARIABLE`), optional docs clauses, and `INITIALIZE_FROM_VARIABLE` behavior in the covered target-creation flow. | - |
| `enable_language` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for the documented CMake 3.28 baseline surface: known-language enabling, file-scope enforcement, and `OPTIONAL` rejection-as-documented placeholder behavior. The newer `CMP0165` pre-`project()` restriction is intentionally not modeled because it is outside the 3.28 policy baseline. | - |
| `enable_testing` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`enable_testing()`). | - |
| `endblock` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`endblock()`). | - |
| `execute_process` | `FULL` | `NOOP_WARN` | Documented CMake 3.28 surface is modeled in the evaluator for `COMMAND` pipelines, working-directory selection, timeout/result capture variables, output/error redirection, `COMMAND_ECHO`, `ECHO_*_VARIABLE`, and the baseline `COMMAND_ERROR_IS_FATAL` values (`ANY`/`LAST`). There is no dedicated `CMP` policy for this command in the 3.28 baseline, so behavior is fixed directly to the baseline surface and newer `NONE` mode is rejected. | - |
| `file` | `FULL` | `ERROR_CONTINUE` | No result-affecting divergence found in validated Linux+Windows scope for CMake 3.28 surface implemented by evaluator, including deferred `GENERATE`, transfer/archive backends, lock semantics, and `CMP0152`-aware `REAL_PATH` behavior in covered flow. | - |
| `find_file` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for the documented CMake 3.28 search surface used by evaluator: `NAMES`, legacy name positionals, `HINTS`, `PATHS`, `PATH_SUFFIXES`, `NO_*` path toggles, root-path modes, cached-result reuse, and `VALIDATOR` in covered flow. When executed from an active `find_package()` config/module context, the package-root stack is modeled with `CMP0074` and `CMP0144`. | - |
| `find_library` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for the documented CMake 3.28 search surface used by evaluator: library-name variant probing, `NAMES`, `HINTS`, `PATHS`, `PATH_SUFFIXES`, `NO_*` path toggles, root-path modes, cached-result reuse, and `VALIDATOR` in covered flow. When executed from an active `find_package()` config/module context, the package-root stack is modeled with `CMP0074` and `CMP0144`. | - |
| `find_package` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented evaluator subset (`AUTO MODULE CONFIG NO_MODULE`, `REQUIRED QUIET`, version/`EXACT`, components, `NAMES CONFIGS HINTS PATHS PATH_SUFFIXES`, `NO_*` path toggles, `CMAKE_FIND_PACKAGE_PREFER_CONFIG`, and package-root handling in covered flow). `CMP0074` and `CMP0144` are modeled both for direct package-prefix resolution and for the active package-root stack seen by nested `find_*` calls. | - |
| `find_path` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for the documented CMake 3.28 search surface used by evaluator: `NAMES`, `HINTS`, `PATHS`, `PATH_SUFFIXES`, `NO_*` path toggles, root-path modes, cached-result reuse, and `VALIDATOR` in covered flow. The evaluator returns the directory containing the matched entry, including directory-name matches, and models the active package-root stack with `CMP0074` and `CMP0144`. | - |
| `find_program` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for the documented CMake 3.28 search surface used by evaluator: executable-name probing, `NAMES`, `HINTS`, `PATHS`, `PATH_SUFFIXES`, `NO_*` path toggles, root-path modes, cached-result reuse, and `VALIDATOR` in covered flow. When executed from an active `find_package()` config/module context, the package-root stack is modeled with `CMP0074` and `CMP0144`. | - |
| `get_cmake_property` | `PARTIAL` | `NOOP_WARN` | Core legacy query surface is implemented for global-property fallback plus evaluator-owned list forms (`VARIABLES`, `CACHE_VARIABLES`, `MACROS`, `COMPONENTS`), but the broader historical CMake special-property surface is not exhaustively modeled. | `Medium` |
| `get_directory_property` | `PARTIAL` | `NOOP_WARN` | Property lookup and `DEFINITION` queries work in the covered current-directory flow, including inherited directory-property fallback through the evaluator property model, but explicit cross-directory variable-definition semantics remain narrower than full CMake. | `Medium` |
| `get_filename_component` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for the documented CMake 3.28 surface implemented in evaluator: `DIRECTORY`/`PATH`, `NAME`, `EXT`, `NAME_WE`, `LAST_EXT`, `NAME_WLE`, `ABSOLUTE` with `BASE_DIR`, `REALPATH`, `PROGRAM`, `PROGRAM_ARGS`, and optional `CACHE` result storage. There is no dedicated `CMP` policy for this command in the 3.28 baseline, so behavior is fixed directly to the baseline surface. | - |
| `get_property` | `PARTIAL` | `NOOP_WARN` | The evaluator now supports the documented scope dispatcher, `SET` / `DEFINED` / `BRIEF_DOCS` / `FULL_DOCS`, inherited-property fallback through the local property model, and `SOURCE` / `TEST` directory clauses in covered flow. Remaining divergence is concentrated in narrower legacy/built-in property-definition cases outside the evaluator-owned property store. | `Medium` |
| `get_source_file_property` | `PARTIAL` | `NOOP_WARN` | Wrapper parity is implemented for evaluator-owned source-file properties in the covered current-directory flow, but built-in source-file properties not represented in the evaluator property store remain outside this batch. | `Medium` |
| `get_target_property` | `PARTIAL` | `NOOP_WARN` | Wrapper parity is implemented for evaluator-owned target property events and target-property mutations, but CMake built-in target properties not yet projected into the evaluator property model remain incomplete. | `Medium` |
| `get_test_property` | `PARTIAL` | `NOOP_WARN` | Wrapper parity is implemented for evaluator-owned test properties in current-directory scope, including inherited-property lookup through the local property model, but unmodeled built-in test properties remain outside the covered surface. | `Medium` |
| `include` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`include(<file|module> [OPTIONAL] [RESULT_VARIABLE <var>] [NO_POLICY_SCOPE])`), including `CMAKE_MODULE_PATH`/`CMAKE_ROOT/Modules` lookup and `CMP0017` search-order behavior (`NEW` vs `OLD`). | - |
| `include_regular_expression` | `FULL` | `NOOP_WARN` | The documented one- and two-argument forms are modeled by updating `CMAKE_INCLUDE_REGULAR_EXPRESSION` and optional `CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN` in evaluator directory state, with no additional filesystem side effects. | - |
| `include_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling (`SYSTEM`, `BEFORE AFTER`, relative canonicalization). | - |
| `include_guard` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`include_guard([DIRECTORY|GLOBAL])`), including default variable-like scope (no-arg form), strict argument validation, and `DIRECTORY`/`GLOBAL` behavior. | - |
| `install` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented evaluator event-model install surface (core + advanced signatures emitted as install rules). | - |
| `link_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling (`BEFORE AFTER`, relative canonicalization). | - |
| `link_libraries` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented global link item handling, including `debug`, `optimized`, and `general` qualifiers emitted as deterministic per-item link payloads. | - |
| `list` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented subcommand surface, including `FILTER` and `TRANSFORM` actions/selectors (`GENEX_STRIP`, `OUTPUT_VARIABLE`). | - |
| `mark_as_advanced` | `FULL` | `NOOP_WARN` | Documented cache-entry marking is modeled by writing the cache `ADVANCED` property through the shared property backend, including `CLEAR`/`FORCE` and `CMP0102` behavior (`OLD` may synthesize an uninitialized cache entry; `NEW` ignores missing entries). | - |
| `math` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`math(EXPR <var> "<expr>" [OUTPUT_FORMAT ...])`). | - |
| `message` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented modes (`NOTICE/STATUS/VERBOSE/DEBUG/TRACE/WARNING/AUTHOR_WARNING/DEPRECATION/SEND_ERROR/FATAL_ERROR/CHECK_*/CONFIGURE_LOG`). | - |
| `option` | `FULL` | `NOOP_WARN` | The documented cache-BOOL command is modeled for the CMake 3.28 baseline, including default `OFF`, existing typed-cache no-op behavior, and `CMP0077` (`OLD` clears a visible normal binding so cache semantics win; `NEW` leaves an existing normal variable untouched). | - |
| `project` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature surface (`VERSION`, `DESCRIPTION`, `HOMEPAGE_URL`, short/long language forms including `LANGUAGES NONE`) and documented project-variable surface (`PROJECT_*`, `<PROJECT-NAME>_*`, top-level `CMAKE_PROJECT_*`, including `CMP0048`-driven no-`VERSION` behavior). | - |
| `remove_definitions` | `PARTIAL` | `NOOP_WARN` | Directory-state removal of `-D`/`/D` definitions is modeled against the same evaluator-managed compile-definition store used by `add_definitions()`, but already-emitted global-definition events are not retractable in the current event architecture. | `Medium` |
| `return` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented behavior (`return()`, `return(PROPAGATE ...)`, `CMP0140` argument handling, and macro-context rejection). | - |
| `separate_arguments` | `PARTIAL` | `NOOP_WARN` | The covered CMake 3.28 parsing surface includes one-argument list form plus `UNIX_COMMAND`, `WINDOWS_COMMAND`, and `NATIVE_COMMAND`. The `PROGRAM [SEPARATE_ARGS]` signature is not implemented in this batch and currently errors explicitly. | `Medium` |
| `set` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`set(<var> <value>... [PARENT_SCOPE])`, `set(<var> <value>... CACHE <type> <doc> [FORCE])`, `set(ENV{<var>} [<value>])`). | - |
| `set_directory_properties` | `FULL` | `NOOP_WARN` | The documented `PROPERTIES <k> <v>...` bulk directory-property wrapper is implemented by routing into the same directory-property backend already used by `set_property(DIRECTORY ...)`. | - |
| `set_property` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented scope/signature surface (`GLOBAL DIRECTORY TARGET SOURCE INSTALL TEST CACHE`, `APPEND APPEND_STRING`, `PROPERTY ...`), including zero-object scope handling and target/cache/test validations in covered flow. | - |
| `set_source_files_properties` | `FULL` | `NOOP_WARN` | The documented bulk source-file-property wrapper is implemented for current-file, `DIRECTORY`, and `TARGET_DIRECTORY` forms by routing into the shared source-property backend. | - |
| `set_target_properties` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature (`set_target_properties(<targets>... PROPERTIES <k> <v>...)`). | - |
| `set_tests_properties` | `FULL` | `NOOP_WARN` | The documented bulk test-property wrapper is implemented for current-directory and explicit `DIRECTORY` forms by routing into the shared test-property backend with existing scope validation. | - |
| `site_name` | `FULL` | `NOOP_WARN` | The documented single-variable signature is implemented with local hostname fallback and the legacy `HOSTNAME` command override path, trimming one trailing newline from command output in the covered flow. | - |
| `source_group` | `PARTIAL` | `NOOP_WARN` | The evaluator models documented `FILES`, `REGULAR_EXPRESSION`, shorthand regex, and `TREE ... PREFIX ... FILES` signatures by recording deterministic source-group metadata in evaluator variables/events, but it does not project those groups into downstream generator backends. | `Medium` |
| `string` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented string command surface, including full hash family (`MD5/SHA1/SHA224/SHA256/SHA384/SHA512/SHA3_*`), `REPEAT`, and `JSON` modes with `ERROR_VARIABLE`. | - |
| `target_compile_features` | `PARTIAL` | `NOOP_WARN` | The documented scope parsing (`PRIVATE`/`PUBLIC`/`INTERFACE`) and target validation are implemented, and requested features are persisted in the evaluator property model. The broader CMake compiler-feature validation and transitive resolution surface is not modeled in this batch. | `Medium` |
| `target_compile_definitions` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented normalization behavior (leading `-D` removal and empty-item ignore). | - |
| `target_compile_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature, including `[BEFORE]`. | - |
| `target_include_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling in covered signature surface. | - |
| `target_link_directories` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented path handling in covered signature surface. | - |
| `target_link_libraries` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented item qualifiers (`debug optimized general`). | - |
| `target_link_options` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signature, including `[BEFORE]`. | - |
| `target_precompile_headers` | `PARTIAL` | `NOOP_WARN` | The evaluator models documented `PRIVATE`/`PUBLIC`/`INTERFACE` usage requirements plus `REUSE_FROM` target linkage in the property/event layer, but it does not perform compiler-specific precompiled-header generation or backend integration. | `Medium` |
| `target_sources` | `PARTIAL` | `NOOP_WARN` | The evaluator models documented `PRIVATE`/`PUBLIC`/`INTERFACE` source attachment and interface-source publication, including target validation, but `FILE_SET` and the broader source-set/policy surface (such as `CMP0076` path semantics) are not implemented in this batch. | `Medium` |
| `try_compile` | `FULL` | `NOOP_WARN` | Native evaluator-side `SOURCE`/`PROJECT` execution now performs real toolchain probes, publishes actual compile output, and records configure-log entries without relying on a simulated existence-only shortcut. | - |
| `unset` | `FULL` | `NOOP_WARN` | No result-affecting divergence found for documented signatures (`unset(<var> [CACHE PARENT_SCOPE])`, `unset(ENV{<var>})`). | - |

## 2. Subcommand Matrix: `file()`

| Subcommand | Status | Delta / Notes | Typical fallback/diag |
|---|---|---|---|
| `GLOB` | `FULL` | Host filesystem globbing with CMake-like path resolution. | Error/Warning diagnostic, continue by profile. |
| `GLOB_RECURSE` | `FULL` | Recursive glob implemented. | Error/Warning diagnostic, continue by profile. |
| `READ` | `FULL` | Offset/limit/hex handling implemented. | Error diagnostic on read failures. |
| `STRINGS` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic on invalid usage/IO failure. |
| `COPY` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic on invalid usage/IO failure. |
| `WRITE` | `FULL` | Writes content to host filesystem immediately. | Error on IO failure. |
| `APPEND` | `FULL` | Append semantics implemented. | Error on IO failure. |
| `MAKE_DIRECTORY` | `FULL` | Multi-directory creation implemented. | Error on failure. |
| `INSTALL` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic on invalid usage/IO failure. |
| `SIZE` | `FULL` | File size query implemented. | Error on stat failure. |
| `RENAME` | `FULL` | Rename with basic options implemented. | Error on failure. |
| `REMOVE` | `FULL` | Remove paths implemented. | Error on failure. |
| `REMOVE_RECURSE` | `FULL` | Recursive remove implemented. | Error on failure. |
| `READ_SYMLINK` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic on invalid usage/IO failure. |
| `CREATE_LINK` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic on invalid usage/IO failure. |
| `CHMOD` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic on invalid usage/IO failure. |
| `CHMOD_RECURSE` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic on invalid usage/IO failure. |
| `REAL_PATH` | `FULL` | Real-path resolution with options is implemented, including `CMP0152` (`OLD` vs `NEW`) behavior in the covered Linux+Windows flow. | Error on failure. |
| `RELATIVE_PATH` | `FULL` | Relative path computation implemented. | Error on invalid usage. |
| `TO_CMAKE_PATH` | `FULL` | Path conversion implemented. | Error on invalid usage. |
| `TO_NATIVE_PATH` | `FULL` | Path conversion implemented. | Error on invalid usage. |
| `<HASH>` (`MD5`, `SHA*`, `SHA3_*`) | `FULL` | File digest computation implemented over file bytes using same hash family as `string(<HASH> ...)`. | Error on invalid hash/IO failure. |
| `CONFIGURE` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic on invalid usage/IO failure. |
| `COPY_FILE` | `FULL` | Implemented for documented CMake 3.28 usage in validated Linux+Windows scope. | Error diagnostic or `RESULT` non-fatal code path. |
| `TOUCH` | `FULL` | Touch/create semantics implemented for one or more files. | Error diagnostic on IO failure. |
| `TOUCH_NOCREATE` | `FULL` | Touch semantics without creating missing files. | Error diagnostic on IO failure. |
| `DOWNLOAD` | `FULL` | Transfer options implemented for validated Linux+Windows scope, including probe-only mode, status/log variables, auth/header/netrc/tls controls, range, timeout, and expected-hash checks. | Error diagnostic + continue (`STATUS` path returns non-fatal status). |
| `UPLOAD` | `FULL` | Transfer options implemented for validated Linux+Windows scope, including status/log variables and auth/header/netrc/tls controls. | Error diagnostic + continue (`STATUS` path returns non-fatal status). |
| `TIMESTAMP` | `FULL` | Timestamp read/format behavior implemented. | Error on stat failure. |
| `GENERATE` | `FULL` | Implemented with deferred flush semantics and duplicate-output checks aligned to validated CMake 3.28 behavior scope. | Error diagnostic on invalid usage/IO failure. |
| `LOCK` | `FULL` | Implemented with `GUARD`, `TIMEOUT`, `DIRECTORY`, `RELEASE`, and duplicate-lock behavior in validated Linux+Windows scope. | Error diagnostic + continue (`RESULT_VARIABLE` path is non-fatal). |
| `ARCHIVE_CREATE` | `FULL` | Implemented with dedicated backend for validated Linux+Windows scope, with fallback execution path when optional library backend is unavailable. | Error diagnostic + continue. |
| `ARCHIVE_EXTRACT` | `FULL` | Implemented with dedicated backend for validated Linux+Windows scope, with fallback execution path when optional library backend is unavailable. | Error diagnostic + continue. |
| `GET_RUNTIME_DEPENDENCIES` | `FULL` | Implemented for validated Linux+Windows scope in this release surface. | Error diagnostic + continue. |

## 3. Coverage details: `string()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Text manipulation core (`APPEND/PREPEND/CONCAT/JOIN/LENGTH/STRIP/FIND/REPLACE/TOUPPER/TOLOWER/SUBSTRING`) | Supported | Supported | None for these forms |
| Regex (`MATCH/MATCHALL/REPLACE`) | Supported | Supported | None for these forms |
| Hash family | Generic `<HASH>` family (`MD5`, `SHA1`, `SHA224`, `SHA256`, `SHA384`, `SHA512`, `SHA3_224`, `SHA3_256`, `SHA3_384`, `SHA3_512`) | Supported | None |
| `REPEAT` | Supported | Supported | None |
| `UUID` | Supported | Supported (`MD5 SHA1`) | None in supported mode |
| `JSON` | `GET/TYPE/MEMBER/LENGTH/REMOVE/SET/EQUAL` (+ `ERROR_VARIABLE`) | Supported (`GET/TYPE/MEMBER/LENGTH/REMOVE/SET/EQUAL` + `ERROR_VARIABLE`) | None |

## 4. Coverage details: `list()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Core list mutation/query (`APPEND/PREPEND/INSERT/REMOVE_*/LENGTH/GET/FIND/JOIN/SUBLIST/POP_*/REVERSE/SORT`) | Supported | Supported | None for covered forms |
| `FILTER` | `INCLUDE|EXCLUDE REGEX` | Supported | None |
| `TRANSFORM` | Actions/selectors + `OUTPUT_VARIABLE` | Supported (`APPEND`, `PREPEND`, `TOLOWER`, `TOUPPER`, `STRIP`, `GENEX_STRIP`, `REPLACE`; selectors `AT FOR REGEX`; optional `OUTPUT_VARIABLE`) | None |

## 4.1 Coverage details: `cmake_path()`

| Area | CMake 3.28.6 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Mutation / transform surface (`SET`, `APPEND`, `APPEND_STRING`, `REMOVE_*`, `REPLACE_*`, `NORMAL_PATH`) | Supported | Supported, with in-place mutation or `OUTPUT_VARIABLE` handling on the documented mutating subcommands | None |
| Query surface (`GET`, `HAS_*`, `IS_ABSOLUTE`) | Supported | Supported for documented components (`ROOT_NAME`, `ROOT_DIRECTORY`, `ROOT_PATH`, `FILENAME`, `EXTENSION`, `STEM`, `RELATIVE_PART`, `PARENT_PATH`) including `LAST_ONLY` on `EXTENSION` / `STEM` | None |
| Path conversion / comparison (`RELATIVE_PATH`, `ABSOLUTE_PATH`, `NATIVE_PATH`, `CONVERT`, `COMPARE`) | Supported | Supported with strict argument validation, `EQUAL` / `NOT_EQUAL` compare parity, and Linux+Windows separator semantics including drive / UNC-aware handling in covered path decomposition | None |
| Invalid arity / option combinations | Hard command error | Hard command error | None |

## 4.2 Coverage details: `find_package()`

| Area | CMake 3.28 | Evaluator v2 | Divergence impact |
|---|---|---|---|
| Mode selection (`AUTO`, `MODULE`, `CONFIG`, `NO_MODULE`) | Supported | Supported, including `CMAKE_FIND_PACKAGE_PREFER_CONFIG` priority in `AUTO` mode | None for covered forms |
| Name/config selection (`NAMES`, `CONFIGS`) | Supported | Supported | None for covered forms |
| Config search shaping (`HINTS`, `PATHS`, `PATH_SUFFIXES`, `NO_*` path toggles) | Supported | Supported in local filesystem resolver | None for covered forms |
| Version checks (`[version]`, `EXACT`, config-version file evaluation) | Supported | Supported | None for covered forms |
| Find-context variables used by package scripts | Supported | Supported for covered subset (including `<Pkg>_FIND_REGISTRY_VIEW` when requested) | None for covered forms |
| Package-root policy (`CMP0074`, `CMP0144`) | Supported | Supported, including `OLD` vs `NEW` behavior for mixed-case and upper-case package-root variables / environment-root handling in the covered flow, plus the active package-root stack exposed to nested `find_*` calls | None |

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
| Full compile pipeline semantics | Actual configure/compile checks | Actual evaluator-side compile pipeline using host toolchain (`SOURCE`) plus isolated child-evaluator configure/build flow (`PROJECT`) | None in covered flow |
| Result publication | `CACHE` by default, `NO_CACHE` for local scope, output/configure-log populated from real probe | Matches covered flow, including cache/local scope split, `OUTPUT_VARIABLE`, `COPY_FILE`, and `LOG_DESCRIPTION` publication | None in covered flow |
| Signature parsing | Broad signature support | Covered `SOURCE` and `PROJECT` forms are normalized into the internal probe request model before execution | None in covered flow |

## 11. Commands currently `FULL`

`add_compile_definitions`, `add_dependencies`, `add_compile_options`, `add_custom_command`, `add_custom_target`, `add_definitions`, `add_executable`, `add_library`, `add_link_options`, `add_subdirectory`, `add_test`, `block`, `break`, `cmake_minimum_required`, `cmake_parse_arguments`, `cmake_path`, `cmake_policy`, `configure_file`, `continue`, `cpack_add_component`, `cpack_add_component_group`, `cpack_add_install_type`, `define_property`, `enable_language`, `enable_testing`, `endblock`, `execute_process`, `file`, `find_file`, `find_library`, `find_package`, `find_path`, `find_program`, `get_filename_component`, `include`, `include_regular_expression`, `include_directories`, `include_guard`, `install`, `link_directories`, `link_libraries`, `list`, `mark_as_advanced`, `math`, `message`, `option`, `project`, `return`, `set`, `set_directory_properties`, `set_property`, `set_source_files_properties`, `set_target_properties`, `set_tests_properties`, `string`, `target_compile_definitions`, `target_compile_options`, `target_include_directories`, `target_link_directories`, `target_link_libraries`, `target_link_options`, `try_compile`, `unset`.

Commands currently documented as `PARTIAL`: `cmake_language`, `get_cmake_property`, `get_directory_property`, `get_property`, `get_source_file_property`, `get_target_property`, `get_test_property`, `remove_definitions`, `separate_arguments`.

## 11.1 Full-scope summary

- Dispatcher-backed built-ins tracked by this document: `72`.
- Additional structural commands implemented outside the dispatcher: `12` (`if`, `elseif`, `else`, `endif`, `foreach`, `endforeach`, `while`, `endwhile`, `function`, `endfunction`, `macro`, `endmacro`).
- Full scoped command universe audited in `evaluator_v2_full_audit.md`: `131`.
- Missing commands from that broader scope: `47`.

## 12. Consistency checks required for future updates

- Keep command list and fallback values aligned with current evaluator runtime (`eval_command_caps.c` + handlers).
- Keep the broader command-count summary aligned with `evaluator_v2_full_audit.md` when the implemented surface changes.
- Keep `Capability Level` aligned with documented CMake 3.28 behavior comparison.
- If any new result-affecting divergence is identified (even `Low`), command must be `PARTIAL`.
- For complex commands, maintain subcommand/signature coverage tables as behavior evolves.
