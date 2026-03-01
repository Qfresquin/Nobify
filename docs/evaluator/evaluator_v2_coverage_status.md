# Evaluator v2 Coverage Status

Status snapshot source:
- `src_v2/evaluator/eval_command_caps.c`
- command-family handlers under `src_v2/evaluator/*.c`

Legend:
- `FULL`: implemented in evaluator runtime for declared command surface.
- `PARTIAL`: implemented with documented deltas/approximations.
- `MISSING`: not registered as evaluator built-in command.

## 1. Command-Level Matrix (Authoritative)

| Command | Level | Fallback | Notes |
|---|---|---|---|
| `add_compile_options` | `FULL` | `NOOP_WARN` | Global compile options events emitted. |
| `add_custom_command` | `PARTIAL` | `ERROR_CONTINUE` | Supports `TARGET`/`OUTPUT` signatures; not full CMake permutation parity. |
| `add_custom_target` | `FULL` | `NOOP_WARN` | Implemented with custom-command/target events. |
| `add_definitions` | `FULL` | `NOOP_WARN` | Treated as raw compile option flags. |
| `add_executable` | `FULL` | `NOOP_WARN` | Declaration + source events. |
| `add_library` | `FULL` | `NOOP_WARN` | Declaration + source events with type parsing. |
| `add_link_options` | `FULL` | `NOOP_WARN` | Global link options events emitted. |
| `add_subdirectory` | `FULL` | `NOOP_WARN` | Evaluates nested `CMakeLists.txt` with directory push/pop events. |
| `add_test` | `PARTIAL` | `ERROR_CONTINUE` | Main signatures implemented; unsupported extra args warned. |
| `block` | `FULL` | `NOOP_WARN` | Variable/policy scopes and `PROPAGATE` supported. |
| `break` | `FULL` | `NOOP_WARN` | Loop control implemented. |
| `cmake_minimum_required` | `FULL` | `NOOP_WARN` | Version parsing and policy-version update implemented. |
| `cmake_path` | `PARTIAL` | `ERROR_CONTINUE` | Subset of modes implemented (see subcommand matrix). |
| `cmake_policy` | `FULL` | `NOOP_WARN` | `VERSION/SET/GET/PUSH/POP` implemented. |
| `continue` | `FULL` | `NOOP_WARN` | Loop control implemented. |
| `cpack_add_component` | `FULL` | `NOOP_WARN` | CPack subset event supported. |
| `cpack_add_component_group` | `FULL` | `NOOP_WARN` | CPack subset event supported. |
| `cpack_add_install_type` | `FULL` | `NOOP_WARN` | CPack subset event supported. |
| `enable_testing` | `FULL` | `NOOP_WARN` | Testing enable event emitted. |
| `endblock` | `FULL` | `NOOP_WARN` | Block close and restore semantics implemented. |
| `file` | `PARTIAL` | `ERROR_CONTINUE` | Broad subcommand set with documented deltas (see matrix). |
| `find_package` | `PARTIAL` | `ERROR_CONTINUE` | Core resolver flow implemented; option/discovery parity not complete. |
| `include` | `PARTIAL` | `ERROR_CONTINUE` | `OPTIONAL` and `NO_POLICY_SCOPE` implemented; not full option parity. |
| `include_directories` | `FULL` | `NOOP_WARN` | Directory include events emitted with `SYSTEM/BEFORE/AFTER`. |
| `include_guard` | `FULL` | `NOOP_WARN` | `DIRECTORY` and `GLOBAL` modes implemented. |
| `install` | `FULL` | `NOOP_WARN` | `TARGETS/FILES/PROGRAMS/DIRECTORY` rule event emission. |
| `link_directories` | `FULL` | `NOOP_WARN` | Directory link directory events emitted. |
| `link_libraries` | `FULL` | `NOOP_WARN` | Global link library events emitted. |
| `list` | `FULL` | `NOOP_WARN` | Implemented subcommand set listed below. |
| `math` | `FULL` | `NOOP_WARN` | `EXPR` implemented, with overflow/invalid checks and output format support. |
| `message` | `FULL` | `NOOP_WARN` | Runtime stdout/stderr plus warning/error diagnostics. |
| `project` | `FULL` | `NOOP_WARN` | Project vars and declaration event implemented. |
| `return` | `FULL` | `NOOP_WARN` | Includes `PROPAGATE` behavior. |
| `set` | `FULL` | `NOOP_WARN` | Variable assignment with `CACHE` and `PARENT_SCOPE`. |
| `set_property` | `PARTIAL` | `ERROR_CONTINUE` | Scope coverage exists; parity for all CMake property semantics is not complete. |
| `set_target_properties` | `FULL` | `NOOP_WARN` | Property key/value pair emission implemented. |
| `string` | `FULL` | `NOOP_WARN` | Large subcommand surface implemented (see matrix). |
| `target_compile_definitions` | `FULL` | `NOOP_WARN` | Event emission by visibility. |
| `target_compile_options` | `FULL` | `NOOP_WARN` | Event emission by visibility. |
| `target_include_directories` | `FULL` | `NOOP_WARN` | Event emission with visibility and `SYSTEM/BEFORE`. |
| `target_link_directories` | `FULL` | `NOOP_WARN` | Event emission by visibility. |
| `target_link_libraries` | `FULL` | `NOOP_WARN` | Event emission by visibility. |
| `target_link_options` | `FULL` | `NOOP_WARN` | Event emission by visibility. |
| `try_compile` | `FULL` | `NOOP_WARN` | Implemented with simulated compile success/failure model. |
| `unset` | `FULL` | `NOOP_WARN` | Variable unsetting with `CACHE` and `PARENT_SCOPE`. |

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

## 3. Subcommand Matrix: `string()`

| Subcommand / Family | Status | Delta / Notes |
|---|---|---|
| `APPEND`, `PREPEND` | `FULL` | Implemented. |
| `CONCAT`, `JOIN` | `FULL` | Implemented. |
| `LENGTH`, `STRIP`, `FIND`, `COMPARE` | `FULL` | Implemented. |
| `ASCII`, `HEX` | `FULL` | Implemented. |
| `CONFIGURE` | `FULL` | Supports `@ONLY` and `ESCAPE_QUOTES`. |
| `MAKE_C_IDENTIFIER` | `FULL` | Implemented. |
| `GENEX_STRIP` | `FULL` | Implemented. |
| `RANDOM` | `FULL` | Supports `LENGTH`, `ALPHABET`, `RANDOM_SEED`. |
| `TIMESTAMP` | `FULL` | Implemented with format/UTC handling. |
| `UUID` | `FULL` | Name-based `TYPE MD5|SHA1` support. |
| `MD5`, `SHA1`, `SHA256` | `FULL` | Direct hash modes implemented. |
| `JSON GET|TYPE|LENGTH` | `PARTIAL` | Implemented subset only. |
| `REPLACE` | `FULL` | Implemented. |
| `TOUPPER`, `TOLOWER`, `SUBSTRING` | `FULL` | Implemented. |
| `REGEX MATCH|REPLACE|MATCHALL` | `FULL` | Implemented. |
| `JSON MEMBER|REMOVE|SET|EQUAL` | `MISSING` | Not implemented in current handler. |
| `SHA224`, `SHA384`, `SHA512`, generic `HASH` mode | `MISSING` | Not implemented in current handler surface. |

## 4. Subcommand Matrix: `list()`

| Subcommand / Family | Status | Delta / Notes |
|---|---|---|
| `APPEND`, `PREPEND`, `INSERT` | `FULL` | Implemented. |
| `REMOVE_ITEM`, `REMOVE_AT`, `REMOVE_DUPLICATES` | `FULL` | Implemented. |
| `LENGTH`, `GET`, `FIND`, `JOIN`, `SUBLIST` | `FULL` | Implemented. |
| `POP_BACK`, `POP_FRONT` | `FULL` | Implemented. |
| `FILTER` | `PARTIAL` | Supports `INCLUDE|EXCLUDE REGEX` mode only. |
| `REVERSE` | `FULL` | Implemented. |
| `SORT` | `FULL` | Supports compare/case/order options. |
| `TRANSFORM` | `PARTIAL` | Implemented action/selector subset; not complete CMake transform universe. |

## 5. Subcommand Matrix: `cmake_path()`

| Mode | Status | Delta / Notes |
|---|---|---|
| `SET` | `FULL` | Includes optional normalization behavior. |
| `GET` | `PARTIAL` | Component subset implemented (`ROOT_*`, `FILENAME`, `STEM`, `EXTENSION`, `RELATIVE_PART`, `PARENT_PATH`). |
| `APPEND` | `FULL` | Includes `OUTPUT_VARIABLE` and `NORMALIZE` handling. |
| `NORMAL_PATH` | `FULL` | Implemented. |
| `RELATIVE_PATH` | `FULL` | Implemented with base/output options. |
| `COMPARE` | `FULL` | `EQUAL/NOT_EQUAL/LESS/LESS_EQUAL/GREATER/GREATER_EQUAL` implemented. |
| `HAS_*` | `PARTIAL` | Relies on current `GET` component support. |
| `IS_*` | `PARTIAL` | `IS_ABSOLUTE` and `IS_RELATIVE` implemented subset. |
| Other `cmake_path()` modes | `MISSING` | Emit not-implemented warning. |

## 6. Coverage Notes: `try_compile()`

`try_compile` is marked `FULL` in capability registry for evaluator-targeted behavior, with these explicit semantics:
- Supports project-signature parsing (`PROJECT`, `SOURCE_DIR`, `BINARY_DIR`, `TARGET`, `OUTPUT_VARIABLE`, `LOG_DESCRIPTION`, `NO_CACHE`, `CMAKE_FLAGS`).
- Supports classic signature with source inputs and source generation options.
- Compile result is evaluator-simulated based on source/CMakeLists presence checks, not an external compiler invocation pipeline.

This is an intentional evaluator compatibility model, not a direct full CMake backend execution.

## 7. Known Missing Built-ins (High-Impact)

Not registered in `eval_command_caps.c` (examples):
- `target_sources`
- `target_compile_features`
- `target_precompile_headers`
- `get_target_property`
- `get_property`
- `find_library`, `find_path`, `find_file`, `find_program`
- `configure_file`

These are outside current built-in command set.

## 8. Consistency Rules for This File

- Command-level rows must stay synchronized with `eval_command_caps.c`.
- Every `PARTIAL` command row must include at least one explicit delta.
- Subcommand coverage must match currently routed handler branches.
