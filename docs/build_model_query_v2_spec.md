# Build Model Query v2 (Normative)

## 1. Overview

The `build_model_query.c` module provides a read-only API over the `Build_Model` (constructed by Freeze). It abstracts the internal memory layout (arrays, arenas, hash maps) from consumers like the Codegen.

**Key Design Goal:** The Codegen should ask "What sources does target X have?" not "What is `model->targets[i]->sources[j]`?".

**Performance:** Queries should be O(1) or O(log N) using the optimized index built during Freeze.

## 2. API Design

### 2.1. Target Lookup
Find a target by name efficiently.
```c
// Returns NULL if target not found.
const Build_Target* bm_query_target_by_name(const Build_Model *model, String_View name);
```

### 2.2. Property Accessors
Get properties of a specific target.
```c
// Returns the type (EXECUTABLE, STATIC_LIBRARY, etc.)
Target_Type bm_query_target_type(const Build_Target *target);

// Returns the flat array of sources (pointer + count).
// The caller does NOT own the memory.
void bm_query_target_sources(const Build_Target *target, 
                             const String_View **out_items, 
                             size_t *out_count);

// Returns includes, dependencies, link libraries, compile options, etc.
void bm_query_target_includes(const Build_Target *target, ...);
void bm_query_target_deps(const Build_Target *target, ...);
```

### 2.3. Project Metadata
Global information about the build.
```c
String_View bm_query_project_name(const Build_Model *model);
String_View bm_query_project_version(const Build_Model *model);
```

## 3. Implementation Strategy

### 3.1. Index Usage
The query module utilizes the `target_index` hash map (or sorted array) created during `build_model_freeze`.
*   **Lookup:** `hash_get(model->target_index, name)` -> `size_t index`.
*   **Access:** `return &model->targets[index]`.

### 3.2. Safety Checks
*   **Null Checks:** All functions verify `model` or `target` is not NULL.
*   **Bounds:** Array access is guarded by `count`.
*   **Const Correctness:** All inputs are `const Build_Model*` or `const Build_Target*`.

## 4. Helper: Dependency Resolution (Transitive)
While the Codegen handles writing the `nob.c` logic, the Query module can provide helper functions to traverse the dependency graph.

```c
// Returns a flattened list of ALL transitive link libraries for a target.
// Useful for generating `nob_cmd_append` calls for linking.
// The result is allocated in `scratch_arena`.
String_View* bm_query_transitive_libs(const Build_Model *model, 
                                      const Build_Target *target, 
                                      Arena *scratch_arena, 
                                      size_t *out_count);
```

## 5. Interface

```c
// build_model_query.h

// --- Core Lookup ---
const Build_Target* bm_get_target(const Build_Model *m, String_View name);

// --- Target Properties ---
String_View bm_target_name(const Build_Target *t);
Target_Type bm_target_type(const Build_Target *t);

// Array Accessors
size_t bm_target_source_count(const Build_Target *t);
String_View bm_target_source_at(const Build_Target *t, size_t index);

size_t bm_target_include_count(const Build_Target *t);
String_View bm_target_include_at(const Build_Target *t, size_t index);

// --- Global Config ---
bool bm_is_windows(const Build_Model *m);
bool bm_is_linux(const Build_Model *m);
```