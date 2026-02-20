# Build Model Freeze v2 (Normative)

## 1. Overview

The `build_model_freeze.c` module implements the transition from a mutable, builder-owned state (`Build_Model_Builder`) to an immutable, optimized representation (`Build_Model`).

**Key Operations:**
1.  **Deep Copy:** All strings and structures are copied to a fresh, contiguous memory arena (`model_arena`).
2.  **Deduplication:** Repeated strings (e.g., file paths, flags) are interned to save space.
3.  **Optimization:** Lists are converted to flat arrays for O(1) access during codegen.
4.  **Indexing:** A hash map or sorted index is built for fast O(1) target lookups by name.

## 2. Motivation (Refinement D)

The Builder phase uses temporary memory (the Evaluator's arena or its own growing buffers).
*   **Problem:** This memory is fragmented and potentially short-lived.
*   **Solution:** The Freeze operation consolidates everything into a single, compact block. This allows the Evaluator's massive arena to be freed immediately, reducing peak memory usage before the code generation phase starts.

## 3. Data Structures

### 3.1. Frozen Model (`Build_Model`)
This struct is the **output** of the freeze operation.

```c
typedef struct {
    Arena *arena;           // Owns all memory for this model

    // Project Metadata
    String_View project_name;
    String_View project_version;

    // Targets (Flat Array)
    Build_Target **targets; 
    size_t target_count;

    // Fast Lookup Index (Optional optimization)
    // Hash Map: Target Name -> Index in targets[]
    void *target_index; 
} Build_Model;
```

### 3.2. Frozen Target (`Build_Target`)
Optimized for read access.
```c
typedef struct {
    Target_Type type;
    String_View name;
    
    // Properties
    String_View *sources;     // Flat array of source paths
    size_t source_count;
    
    String_View *deps;        // Flat array of dependency names
    size_t dep_count;
    
    String_View *includes;    // Flat array of include directories
    size_t include_count;
    
    // ... other properties ...
} Build_Target;
```

## 4. The Freeze Algorithm

### 4.1. Step 1: Arena Allocation
Create a new `Arena *final_arena`. This arena will survive until `nobify` exits.

### 4.2. Step 2: String Interning (Optional but Recommended)
Use a temporary hash map to track unique strings encountered.
*   If string exists in map -> Return existing pointer from `final_arena`.
*   If new -> Copy to `final_arena`, add to map.
*   *Benefit:* Massive reduction in memory for repeated paths/flags (e.g., `-I/usr/include` used 50 times becomes 1 string).

### 4.3. Step 3: Deep Copy
Iterate over the Builder's `model`:
1.  **Project Info:** Copy name/version strings.
2.  **Targets:**
    *   Allocate `Build_Target` struct in `final_arena`.
    *   Copy name string.
    *   **Convert Lists to Arrays:**
        *   Allocate array of `String_View` in `final_arena` (size = list.count).
        *   Copy each string from the list (using intern mechanism).
    *   *Note:* The Builder might use linked lists or dynamic arrays; the Frozen model uses fixed-size arrays.

### 4.4. Step 4: Index Construction
After all targets are copied:
1.  Allocate a hash map (or sort the targets array by name for binary search).
2.  Populate it: `target_name -> index`.
3.  Store this index in `Build_Model`.

### 4.5. Step 5: Cleanup
Return the `Build_Model`. The caller (Driver) can now safely destroy the `Evaluator` and `Builder` arenas.

## 5. Interface

```c
// build_model_freeze.h

// Freezes the builder state into an immutable model.
// - builder: The source mutable state.
// - out_arena: The destination arena for the frozen model.
// Returns NULL on allocation failure.
const Build_Model* build_model_freeze(Build_Model_Builder *builder, Arena *out_arena);

// Helper: Performs string interning during freeze.
String_View build_model_intern(Arena *arena, String_Map *intern_map, String_View str);
```

## 6. Thread Safety
The freeze operation is **single-threaded** (part of the main pipeline). The resulting `Build_Model` is **immutable** and theoretically thread-safe for parallel codegen (if ever implemented).
