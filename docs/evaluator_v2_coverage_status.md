# Evaluator v2 Coverage Status

Status snapshot based on current source in `src_v2/evaluator` and parser integration in `src_v2/parser`.

## Legend

- Implemented: available in evaluator runtime.
- Partial: exists but with reduced semantics and/or ignored options.
- Missing: not implemented in evaluator runtime.

## 1) Utility Mode (`cmake -E`)

- Missing:
  - `chdir`, `copy`, `copy_directory`, `copy_if_different`, `make_directory`, `remove`, `remove_directory`, `rename`, `touch`, `touch_nocreate`
  - `create_symlink`, `delete_regv`, `write_regv`
  - `cat`, `compare_files`, `md5sum`, `sha256sum`, `sha512sum`
  - `echo`, `echo_append`, `env`, `environment`, `sleep`, `time`
  - `tar`, `tar cfz`, `tar xfz`

Note: evaluator v2 currently executes CMake language commands, not `cmake -E` utility subcommands.

## 2) CMake Script Language

### Flow control

- Implemented:
  - `if / elseif / else / endif`
  - `foreach / endforeach`
  - `while / endwhile`
  - `block / endblock` (supports `SCOPE_FOR` and `PROPAGATE`)
  - `break`, `continue`, `return`

### Block definitions

- Implemented:
  - `function / endfunction`
  - `macro / endmacro`

### Variables

- Implemented:
  - `set(...)` including cache and `PARENT_SCOPE` handling
- Partial:
  - `set(<var>)` unsets variable (works as unset behavior for this form)
- Missing:
  - `unset(...)` command entrypoint

### `list()`

- Implemented:
  - `APPEND`, `REMOVE_ITEM`, `LENGTH`, `GET`, `FIND`
- Missing:
  - `JOIN`, `SUBLIST`
  - `FILTER`
  - `INSERT`, `POP_BACK`, `POP_FRONT`, `PREPEND`, `REMOVE_AT`, `REMOVE_DUPLICATES`
  - `TRANSFORM`
  - `REVERSE`, `SORT`

### `string()`

- Implemented:
  - `REPLACE`
  - `TOUPPER`, `TOLOWER`
  - `SUBSTRING`
  - `REGEX MATCH`, `REGEX REPLACE`
- Missing:
  - `APPEND`, `PREPEND`, `CONCAT`, `JOIN`
  - `LENGTH`, `STRIP`, `FIND`
  - `REGEX MATCHALL`
  - `COMPARE`
  - `HASH`
  - `ASCII`, `HEX`
  - `CONFIGURE`
  - `MAKE_C_IDENTIFIER`
  - `GENEX_STRIP`
  - `JSON`
  - `RANDOM`
  - `TIMESTAMP`
  - `UUID`

### `math()`

- Implemented:
  - `math(EXPR ...)`
  - operators currently exercised in evaluator: unary `+ - ~`, `* / %`, `+ -`, `<< >>`, `& ^ |`, parentheses
  - overflow/invalid checks added for literals and arithmetic edge cases
- Missing:
  - other `math()` subcommands (if any outside `EXPR`)

## 3) `file()` commands

- Implemented:
  - `file(READ ...)`
  - `file(STRINGS ...)` (with several options, including `REGEX`, limits, `NEWLINE_CONSUME`, `ENCODING`)
  - `file(WRITE ...)`
  - `file(MAKE_DIRECTORY ...)`
  - `file(COPY ... DESTINATION ...)`
  - `file(GLOB ...)`
  - `file(GLOB_RECURSE ...)`
- Partial:
  - `file(COPY ...)`: some options still warned/ignored (for example source-permission mode toggles)
  - `file(STRINGS ...)`: unsupported options still produce warning when encountered
- Missing:
  - `APPEND`, `RENAME`, `REMOVE`, `REMOVE_RECURSE`
  - `INSTALL`
  - `SIZE`
  - `READ_SYMLINK`
  - `CREATE_LINK`
  - `CHMOD`, `CHMOD_RECURSE`
  - `REAL_PATH`, `RELATIVE_PATH`
  - `TO_CMAKE_PATH`, `TO_NATIVE_PATH`
  - `DOWNLOAD`, `UPLOAD`
  - `TIMESTAMP`
  - `GENERATE`
  - `LOCK`
  - `ARCHIVE_CREATE`, `ARCHIVE_EXTRACT`

## 4) Project/Target commands

### Project and targets

- Implemented:
  - `project(...)`
  - `add_executable(...)`
  - `add_library(...)`
  - `add_custom_target(...)`
  - `add_custom_command(...)`
- Partial:
  - `add_custom_command(...)`: supported signatures exist, but not all CMake permutations are covered

### Target properties and relationships

- Implemented:
  - `target_include_directories(...)`
  - `target_compile_definitions(...)`
  - `target_compile_options(...)`
  - `target_link_libraries(...)`
  - `target_link_directories(...)`
  - `target_link_options(...)`
  - `set_target_properties(...)`
  - `set_property(...)` with target and non-target scopes (partial behavior by design)
- Missing:
  - `target_compile_features(...)`
  - `target_sources(...)`
  - `target_precompile_headers(...)`
  - `get_target_property(...)`
  - `get_property(...)`

### Directories/includes

- Implemented:
  - `add_subdirectory(...)`
  - `include_directories(...)`
  - `link_directories(...)`
- Missing:
  - `source_group(...)`

### Package/search

- Implemented:
  - `find_package(...)`
- Partial:
  - `find_package(...)` options and discovery behavior are not 100% complete (`REGISTRY_VIEW` etc.)
- Missing:
  - `find_library(...)`
  - `find_path(...)`
  - `find_file(...)`
  - `find_program(...)`

## 5) Install and tests

- Implemented:
  - `install(TARGETS|FILES|PROGRAMS|DIRECTORY ... DESTINATION ...)`
  - `add_test(...)`
  - `enable_testing()`
- Partial:
  - `add_test(NAME ... COMMAND ...)`: extra args can be warned/ignored
- Missing:
  - `set_tests_properties(...)`
  - `install(SCRIPT ...)`
  - `install(CODE ...)`
  - `install(EXPORT ...)`
  - `install(RUNTIME_DEPENDENCY_SET ...)`

## 6) Configuration and include helpers

- Implemented:
  - `include(<file|module> [OPTIONAL] [NO_POLICY_SCOPE])`
  - `include_guard([DIRECTORY|GLOBAL])`
  - `cmake_minimum_required(...)`
  - `cmake_policy(...)`
  - `cmake_path(...)`
- Partial:
  - `include(...)`: `RESULT_VAR` is not implemented
  - `include_guard(...)`: unsupported modes are warned
  - `cmake_minimum_required(...)`: unknown extra args warned/ignored
  - `cmake_path(...)`: many subcommands/components are still missing
- Missing:
  - `configure_file(...)`
  - `aux_source_directory(...)`

## 7) CTest, CPack, legacy, platform-specific

- CTest:
  - Missing: `ctest_build`, `ctest_configure`, `ctest_coverage`, `ctest_empty_binary_directory`, `ctest_memcheck`, `ctest_read_custom_files`, `ctest_run_script`, `ctest_sleep`, `ctest_start`, `ctest_submit`, `ctest_test`, `ctest_upload`
- CPack:
  - Implemented: `cpack_add_component`, `cpack_add_component_group`, `cpack_add_install_type`
  - Missing: `cpack_configure_downloads`
- Legacy commands:
  - Mostly missing in evaluator command dispatch
  - `link_libraries(...)` is implemented
- Platform-specific list from request:
  - Implemented: `cmake_policy(...)`
  - Missing (from that list): `build_name`, `define_property`, `doxygen_add_docs`, `enable_language`, `export`, `fltk_wrap_ui`, `get_cmake_property`, `get_directory_property`, `get_filename_component`, `get_source_file_property`, `include_external_msproject`, `include_regular_expression`, `load_cache`, `load_command`, `mark_as_advanced`, `qt_wrap_cpp`, `qt_wrap_ui`, `separate_arguments`, `site_name`

## 8) Important architecture note

Generator expressions (`$<...>`) and target metadata queries are handled by `src_v2/genex`, but this does not replace missing evaluator command handlers (for example `get_target_property`, `target_sources`, `target_compile_features`).
