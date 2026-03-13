# Build Model Types (Canonical)

## 1. Status

This document defines the canonical public type system for the new build model.

The type system exists to keep reconstructed semantics stable and explicit
before any downstream Nob optimization consumes them.

The top-level principle is:
- mutable draft state is opaque
- frozen model state is opaque
- entity identity is expressed through typed IDs
- read access happens through query APIs and explicit spans

## 2. Top-Level Opaque Types

The canonical public headers must expose these opaque types:

```c
typedef struct BM_Builder BM_Builder;
typedef struct Build_Model_Draft Build_Model_Draft;
typedef struct Build_Model Build_Model;
typedef struct Diag_Sink Diag_Sink;
```

There is no public `build_model_core.h` equivalent in the new design.

## 3. Typed IDs

Every first-class entity uses a distinct typed ID:

```c
typedef uint32_t BM_Directory_Id;
typedef uint32_t BM_Target_Id;
typedef uint32_t BM_Test_Id;
typedef uint32_t BM_Install_Rule_Id;
typedef uint32_t BM_Package_Id;
typedef uint32_t BM_CPack_Install_Type_Id;
typedef uint32_t BM_CPack_Component_Group_Id;
typedef uint32_t BM_CPack_Component_Id;
```

Each ID type must define an invalid sentinel in its public header. The sentinel
value is part of the ABI contract and must remain stable.

## 4. Common View Types

The public surface uses explicit spans instead of implicit collections:

```c
typedef struct {
    const String_View *items;
    size_t count;
} BM_String_Span;

typedef struct {
    const BM_Target_Id *items;
    size_t count;
} BM_Target_Id_Span;
```

The same pattern must be used for other public span types when needed. Public
APIs must not require `arena_arr_len(...)`, hidden sentinels, or macro-backed
array conventions.

## 5. Provenance

Every first-class entity stores provenance:

```c
typedef struct {
    uint64_t event_seq;
    Event_Kind event_kind;
    String_View file_path;
    uint32_t line;
    uint32_t col;
} BM_Provenance;
```

`BM_Provenance` is required on:
- directories
- targets
- tests
- install rules
- packages
- CPack entities

For entities updated by multiple events, the canonical record stores:
- declaration provenance
- optional last-mutation provenance per append-only property family

## 6. Canonical Enums

The build model uses its own canonical enums instead of re-exporting
transitional evaluator compatibility enums.

Required enums:

```c
typedef enum {
    BM_TARGET_EXECUTABLE = 0,
    BM_TARGET_STATIC_LIBRARY,
    BM_TARGET_SHARED_LIBRARY,
    BM_TARGET_MODULE_LIBRARY,
    BM_TARGET_INTERFACE_LIBRARY,
    BM_TARGET_OBJECT_LIBRARY,
    BM_TARGET_UTILITY,
} BM_Target_Kind;

typedef enum {
    BM_VISIBILITY_PRIVATE = 0,
    BM_VISIBILITY_PUBLIC,
    BM_VISIBILITY_INTERFACE,
} BM_Visibility;

typedef enum {
    BM_INSTALL_RULE_TARGET = 0,
    BM_INSTALL_RULE_FILE,
    BM_INSTALL_RULE_PROGRAM,
    BM_INSTALL_RULE_DIRECTORY,
} BM_Install_Rule_Kind;
```

Imported and alias state are target flags, not separate target kinds.

Typed raw property entries use an explicit value-view type:

```c
typedef enum {
    BM_ITEM_FLAG_NONE = 0,
    BM_ITEM_FLAG_BEFORE = 1u << 0,
    BM_ITEM_FLAG_SYSTEM = 1u << 1,
} BM_Item_Flags;

typedef struct {
    String_View value;
    BM_Visibility visibility;
    uint32_t flags;
    BM_Provenance provenance;
} BM_String_Item_View;

typedef struct {
    const BM_String_Item_View *items;
    size_t count;
} BM_String_Item_Span;
```

This view type is used for raw visibility-aware target and directory property
families.

## 7. Draft vs Frozen Shape

`Build_Model_Draft` and `Build_Model` are distinct concepts.

### Draft

The draft is builder-owned and may contain:
- append-only entity records
- unresolved symbolic references
- raw property bags for future promotion
- transient indexes required for incremental construction

The draft stores symbolic relations by name where forward references are
allowed. Example:
- target dependency references
- alias target references
- install target rule item names
- CPack component group references
- CPack component dependency references

### Frozen Model

The frozen model stores:
- compact arrays only
- typed IDs for resolved relations
- stable lookup indexes
- interned strings
- no mutable builder-only indexes
- no pointers back into the draft or the event stream

## 8. Canonical Domain Entities

The release-1 domain set is fixed.

### Project

The public query surface exposes one canonical root-project view:
- `name`
- `version`
- `description`
- `homepage_url`
- `languages`

The implementation may keep an internal project declaration log, but the public
contract exposes a single effective project record in release 1.

### Directory

Every directory record contains:
- `id`
- `parent_id`
- `owner_directory_id` equal to `id`
- `source_dir`
- `binary_dir`
- `provenance`
- typed raw property families for include dirs, system include dirs, link dirs,
  compile definitions, compile options, and link options
- a future-facing raw property bag for additional promoted directory/global
  properties

### Target

Every target record contains:
- `id`
- `name`
- `owner_directory_id`
- declaration `provenance`
- `kind`
- flags: `imported`, `alias`, `exclude_from_all`, `win32_executable`,
  `macosx_bundle`
- `alias_of` as a symbolic reference in draft and a typed ID in the frozen model
- raw sources
- raw explicit dependency references
- raw typed property entries for:
  `link_libraries`, `link_options`, `link_directories`,
  `include_directories`, `compile_definitions`, `compile_options`
- typed scalar properties promoted from `EVENT_TARGET_PROP_SET`:
  `OUTPUT_NAME`, `PREFIX`, `SUFFIX`,
  `ARCHIVE_OUTPUT_DIRECTORY`, `LIBRARY_OUTPUT_DIRECTORY`,
  `RUNTIME_OUTPUT_DIRECTORY`, `FOLDER`
- a future-facing raw property bag for unsupported target properties

### Test

Every test record contains:
- `id`
- `name`
- `owner_directory_id`
- `provenance`
- `command`
- `working_dir`
- `command_expand_lists`

### Install Rule

Every install rule contains:
- `id`
- `kind`
- `owner_directory_id`
- `provenance`
- raw `item`
- `destination`

For `BM_INSTALL_RULE_TARGET`, the draft keeps `item` as a symbolic target name.
The frozen model stores the resolved `BM_Target_Id`.

### Package

Every package record contains:
- `id`
- `owner_directory_id`
- `provenance`
- `package_name`
- `mode`
- `found_path`
- `found`
- `required`
- `quiet`

### CPack

Release-1 CPack entities are:
- install types
- component groups
- components

Where payload fields encode CMake list syntax in one string
(`languages`, `depends`, `install_types`), the builder splits them into
normalized item arrays during ingest.

## 9. Raw vs Effective State

The canonical model separates:
- raw state: direct reconstruction from supported events
- effective state: inherited or transitive views computed by query helpers

The builder never precomputes global effective views. That work belongs to
query functions over the frozen model.
