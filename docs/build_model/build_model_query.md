# Build Model Query (Normative)

## 1. Role

Query is the only public read surface over `Build_Model`.

Codegen and tooling must use query helpers instead of touching internal storage
directly.

This keeps semantic correctness primary: downstream Nob optimization should
consume query-visible model semantics instead of bypassing the frozen model.

## 2. Query Design Rules

- `Build_Model` is opaque.
- Entity access is ID-based.
- Public functions return explicit spans or scalar values.
- No public API returns a mutable pointer into the model.
- Effective-property queries may use a caller-provided scratch arena.
- Repeated derived queries may also use a canonical `BM_Query_Session`.

## 3. Public Query Surface

The canonical release-1 query surface must include:

### Model and Project

```c
bool bm_model_has_project(const Build_Model *model);
String_View bm_query_project_name(const Build_Model *model);
String_View bm_query_project_version(const Build_Model *model);
BM_String_Span bm_query_project_languages(const Build_Model *model);
```

### Lookups

```c
BM_Target_Id bm_query_target_by_name(const Build_Model *model, String_View name);
BM_Test_Id bm_query_test_by_name(const Build_Model *model, String_View name);
BM_Package_Id bm_query_package_by_name(const Build_Model *model, String_View name);
bool bm_target_id_is_valid(BM_Target_Id id);
```

### Directory and Global Raw Accessors

```c
BM_Directory_Id bm_query_root_directory(const Build_Model *model);
size_t bm_query_directory_count(const Build_Model *model);
BM_Directory_Id bm_query_directory_parent(const Build_Model *model, BM_Directory_Id id);
String_View bm_query_directory_source_dir(const Build_Model *model, BM_Directory_Id id);
String_View bm_query_directory_binary_dir(const Build_Model *model, BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_include_directories_raw(const Build_Model *model,
                                                               BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_system_include_directories_raw(const Build_Model *model,
                                                                      BM_Directory_Id id);
BM_String_Item_Span bm_query_directory_link_directories_raw(const Build_Model *model,
                                                            BM_Directory_Id id);

BM_String_Item_Span bm_query_global_include_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_system_include_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_link_directories_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_compile_definitions_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_compile_options_raw(const Build_Model *model);
BM_String_Item_Span bm_query_global_link_options_raw(const Build_Model *model);
BM_String_Span bm_query_global_raw_property_items(const Build_Model *model,
                                                  String_View property_name);
```

`bm_query_global_raw_property_items(...)` is the canonical escape hatch for
global raw properties that are reconstructed but not yet promoted to dedicated
named accessors. Codegen and tests may use it for keys such as
`LINK_LIBRARIES`, while the model remains fully opaque.

### Target Raw Accessors

```c
String_View bm_query_target_name(const Build_Model *model, BM_Target_Id id);
BM_Target_Kind bm_query_target_kind(const Build_Model *model, BM_Target_Id id);
BM_Directory_Id bm_query_target_owner_directory(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_is_imported(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_is_imported_global(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_is_alias(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_is_alias_global(const Build_Model *model, BM_Target_Id id);
BM_Target_Id bm_query_target_alias_of(const Build_Model *model, BM_Target_Id id);
bool bm_query_target_exclude_from_all(const Build_Model *model, BM_Target_Id id);
BM_String_Span bm_query_target_sources_raw(const Build_Model *model, BM_Target_Id id);
BM_Target_Id_Span bm_query_target_dependencies_explicit(const Build_Model *model,
                                                        BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_libraries_raw(const Build_Model *model,
                                                       BM_Target_Id id);
BM_String_Item_Span bm_query_target_include_directories_raw(const Build_Model *model,
                                                            BM_Target_Id id);
BM_String_Item_Span bm_query_target_compile_definitions_raw(const Build_Model *model,
                                                            BM_Target_Id id);
BM_String_Item_Span bm_query_target_compile_options_raw(const Build_Model *model,
                                                        BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_options_raw(const Build_Model *model,
                                                     BM_Target_Id id);
BM_String_Item_Span bm_query_target_link_directories_raw(const Build_Model *model,
                                                         BM_Target_Id id);
```

Release-1 query also includes scalar accessors for the promoted target property
keys:
- `OUTPUT_NAME`
- `PREFIX`
- `SUFFIX`
- `ARCHIVE_OUTPUT_DIRECTORY`
- `LIBRARY_OUTPUT_DIRECTORY`
- `RUNTIME_OUTPUT_DIRECTORY`
- `FOLDER`

Identity rules:
- `bm_query_target_kind(...)` returns the effective target family for aliases
- `bm_query_target_is_imported(...)` and `bm_query_target_is_alias(...)`
  report direct target flags, not resolved alias state
- `bm_query_target_is_imported_global(...)` and
  `bm_query_target_is_alias_global(...)` expose frozen visibility scope for
  imported targets and aliases
- `BM_TARGET_UNKNOWN_LIBRARY` remains distinct from `BM_TARGET_UTILITY`

### Effective Target Accessors

```c
bool bm_query_target_effective_include_directories_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         Arena *scratch,
                                                         BM_String_Item_Span *out);

bool bm_query_target_effective_compile_definitions_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         Arena *scratch,
                                                         BM_String_Item_Span *out);

bool bm_query_target_effective_compile_options_items(const Build_Model *model,
                                                     BM_Target_Id id,
                                                     Arena *scratch,
                                                     BM_String_Item_Span *out);

bool bm_query_target_effective_link_libraries_items(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    Arena *scratch,
                                                    BM_String_Item_Span *out);

bool bm_query_target_effective_link_options_items(const Build_Model *model,
                                                  BM_Target_Id id,
                                                  Arena *scratch,
                                                  BM_String_Item_Span *out);

bool bm_query_target_effective_link_directories_items(const Build_Model *model,
                                                      BM_Target_Id id,
                                                      Arena *scratch,
                                                      BM_String_Item_Span *out);

bool bm_query_target_effective_include_directories(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out);

bool bm_query_target_effective_compile_definitions(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out);

bool bm_query_target_effective_compile_options(const Build_Model *model,
                                               BM_Target_Id id,
                                               Arena *scratch,
                                               BM_String_Span *out);

bool bm_query_target_effective_link_libraries(const Build_Model *model,
                                              BM_Target_Id id,
                                              Arena *scratch,
                                              BM_String_Span *out);

bool bm_query_target_effective_link_options(const Build_Model *model,
                                            BM_Target_Id id,
                                            Arena *scratch,
                                            BM_String_Span *out);

bool bm_query_target_effective_link_directories(const Build_Model *model,
                                                BM_Target_Id id,
                                                Arena *scratch,
                                                BM_String_Span *out);
```

The `*_items(...)` variants are the canonical codegen-facing API. They preserve
`visibility`, `flags`, and `provenance` for each reconstructed item. The
`BM_String_Span` variants remain compatibility wrappers that strip each item
down to `value` only.

### Query Session

`C1M` adds an arena-owned memoized derived-query surface for consumers that
issue many repeated effective or imported-target lookups:

```c
typedef struct BM_Query_Session BM_Query_Session;

typedef struct {
    size_t effective_item_hits;
    size_t effective_item_misses;
    size_t effective_value_hits;
    size_t effective_value_misses;
    size_t target_file_hits;
    size_t target_file_misses;
    size_t imported_link_language_hits;
    size_t imported_link_language_misses;
} BM_Query_Session_Stats;

BM_Query_Session *bm_query_session_create(Arena *arena, const Build_Model *model);
const BM_Query_Session_Stats *bm_query_session_stats(const BM_Query_Session *session);
```

The session is query-time only:

- it does not mutate or persist anything back into `Build_Model`
- it preserves the same raw/effective semantics as the stateless APIs
- it owns memoized lifetime through the caller-provided arena

The canonical memoized surface covers:

- effective include directories, compile definitions, compile options, link
  libraries, link options, and link directories in both item and value form
- effective compile features
- effective imported/local target file and linker file resolution
- imported link-language lookup

Memoization keys must include all caller-visible semantic context required to
keep derived results correct, including target, family, usage mode, current
target, config, compile language, platform id, and build/install interface
state.

### Other Domains

Release-1 must also include query helpers for:
- directory metadata and parent traversal
- replay actions
- tests
- raw promoted directory/global property access
- install rules
- packages
- CPack install types, groups, and components

### Replay Domain

The canonical replay query surface is:

```c
bool bm_replay_action_id_is_valid(BM_Replay_Action_Id id);
size_t bm_query_replay_action_count(const Build_Model *model);
BM_Replay_Action_Kind bm_query_replay_action_kind(const Build_Model *model,
                                                  BM_Replay_Action_Id id);
BM_Replay_Opcode bm_query_replay_action_opcode(const Build_Model *model,
                                               BM_Replay_Action_Id id);
BM_Replay_Phase bm_query_replay_action_phase(const Build_Model *model,
                                             BM_Replay_Action_Id id);
BM_Directory_Id bm_query_replay_action_owner_directory(const Build_Model *model,
                                                       BM_Replay_Action_Id id);
String_View bm_query_replay_action_working_directory(const Build_Model *model,
                                                     BM_Replay_Action_Id id);
BM_String_Span bm_query_replay_action_inputs(const Build_Model *model,
                                             BM_Replay_Action_Id id);
BM_String_Span bm_query_replay_action_outputs(const Build_Model *model,
                                              BM_Replay_Action_Id id);
BM_String_Span bm_query_replay_action_argv(const Build_Model *model,
                                           BM_Replay_Action_Id id);
BM_String_Span bm_query_replay_action_environment(const Build_Model *model,
                                                  BM_Replay_Action_Id id);
```

This is the minimum stable replay-domain surface. Future query helpers may add
kind-specific payload accessors, but codegen must be implementable through the
generic spans above, the typed opcode, and existing domain queries.

### Test Domain

The canonical test query surface includes:

```c
size_t bm_query_test_count(const Build_Model *model);
String_View bm_query_test_name(const Build_Model *model, BM_Test_Id id);
BM_Directory_Id bm_query_test_owner_directory(const Build_Model *model, BM_Test_Id id);
String_View bm_query_test_working_directory(const Build_Model *model, BM_Test_Id id);
bool bm_query_test_command_expand_lists(const Build_Model *model, BM_Test_Id id);
String_View bm_query_test_command(const Build_Model *model, BM_Test_Id id);
BM_String_Span bm_query_test_configurations(const Build_Model *model, BM_Test_Id id);
```

These queries are the canonical downstream surface for both:

- the product-facing generated `test` command
- `C3` test-driver replay such as `ctest_test`

Codegen must not reconstruct test plans from raw Event IR once the frozen test
domain is available.

## 4. Raw vs Effective Query Rules

Raw queries return directly reconstructed semantics:
- sources
- explicit dependency edges
- raw visibility-aware property entries
- scalar target properties

Effective queries may compute:
- inherited directory properties
- transitive interface usage requirements
- generator-expression-aware value expansion when explicitly supported by the
  caller context

For release-1 effective queries, usage propagation is resolved at query time:
- global properties contribute first
- then the full owner-directory chain contributes
- then target-local properties contribute
- then transitive usage is discovered from local target references found in
  `target_link_libraries(...)`

Alias targets may be resolved during that walk through Query helpers.
`add_dependencies(...)` remains a build-order edge only; it must not be treated
as compile/link usage inheritance.

Effective link-library reconstruction also includes raw `LINK_LIBRARIES`
properties from global and directory scopes when they exist in the model.

Effective queries must never mutate the model.
They also must not persist inferred edges back into `Build_Model`; all such
projection remains query-time only.

## 5. Query Safety

Required safety behavior:
- invalid IDs return empty or false-style results
- missing names return invalid ID sentinels
- all span outputs remain owned by the model or by the caller-provided scratch
  arena, never by hidden globals

## 6. Forbidden Legacy Pattern

The new query layer forbids the old pattern:
- `const Build_Target *t = bm_get_target(...);`
- direct field walking by codegen

The canonical API is accessor-based and ID-oriented.
