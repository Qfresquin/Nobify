# Build Model Query (Normative)

## 1. Role

Query is the only public read surface over `Build_Model`.

Codegen and tooling must use query helpers instead of touching internal storage
directly.

## 2. Query Design Rules

- `Build_Model` is opaque.
- Entity access is ID-based.
- Public functions return explicit spans or scalar values.
- No public API returns a mutable pointer into the model.
- Effective-property queries may use a caller-provided scratch arena.

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

### Target Raw Accessors

```c
String_View bm_query_target_name(const Build_Model *model, BM_Target_Id id);
BM_Target_Kind bm_query_target_kind(const Build_Model *model, BM_Target_Id id);
BM_Directory_Id bm_query_target_owner_directory(const Build_Model *model, BM_Target_Id id);
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

### Effective Target Accessors

```c
bool bm_query_target_effective_include_directories(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out);

bool bm_query_target_effective_compile_definitions(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out);

bool bm_query_target_effective_link_libraries(const Build_Model *model,
                                              BM_Target_Id id,
                                              Arena *scratch,
                                              BM_String_Span *out);
```

### Other Domains

Release-1 must also include query helpers for:
- directory metadata and parent traversal
- tests
- install rules
- packages
- CPack install types, groups, and components

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

Effective queries must never mutate the model.

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
