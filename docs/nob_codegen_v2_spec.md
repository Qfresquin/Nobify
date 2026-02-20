# Nob Codegen v2 (Normative)

## 1. Overview

The `nob_codegen.c` module is the transpiler's backend. It consumes the immutable `Build_Model` and emits a standalone C source file (`nob.c`) that utilizes the `nob.h` library to build the project.

**Core Philosophy:**
1.  **Passive:** The Codegen makes no semantic decisions. It strictly outputs what is in the model.
2.  **Imperative:** It translates the declarative model into a sequence of C statements.
3.  **Data-Driven:** It prefers generating static arrays and loops over unrolled function calls to keep the generated file size manageable.

## 2. Output Structure (`nob.c` Anatomy)

The generated file will follow this structure:

1.  **Preamble:**
    *   `#define NOB_IMPLEMENTATION`
    *   `#include "nob.h"`
    *   (Optional) `N_TRACE` macro definition (Refinement C).
2.  **Main Function:**
    *   `NOB_GO_REBUILD_URSELF(argc, argv);`
    *   Directory creation (e.g., `nob_mkdir_if_not_exists("build")`).
3.  **Target Blocks (Topologically Sorted):**
    *   For each target:
        *   Define source list (Static Array).
        *   Define flag list (Static Array).
        *   Compilation loop (Object files).
        *   Linking command (Binary/Library).
4.  **Epilogue:**
    *   `return 0;`

## 3. Data-Driven Generation Strategy

To avoid bloating `nob.c` with thousands of lines like `nob_cmd_append(&cmd, "file1.c"); nob_cmd_append(&cmd, "file2.c");`, the codegen **must** use static arrays.

### 3.1. Source Files
**Bad (v1 approach):**
```c
nob_cmd_append(&cmd, "src/main.c");
nob_cmd_append(&cmd, "src/utils.c");
// ... 100 more lines ...
```

**Good (v2 approach):**
```c
const char *srcs_target_app[] = {
    "src/main.c",
    "src/utils.c",
    // ...
};
// Loop generation handles the rest
```

### 3.2. Compiler Flags
Common flags (like `-Iinclude`, `-Wall`) should also be grouped.
*   **Optimization:** If multiple targets share the exact same flags (Global flags), generate a shared array `const char *global_cflags[]` to reuse data.

## 4. Origin Traceability (Refinement C)

The Codegen checks `Codegen_Options.trace_origin`.

*   **If Enabled:**
    *   Emits the `N_TRACE` macro at the top of `nob.c`.
    *   Instead of standard arrays, it may need to emit calls or struct arrays to carry the line number metadata.
    *   *Alternative Data-Driven Approach for Tracing:*
        ```c
        typedef struct { const char *val; const char *file; int line; } Trace_Item;
        const Trace_Item srcs[] = {
             {"main.c", "CMakeLists.txt", 10},
             ...
        };
        ```

## 5. Implementation Logic

### 5.1. Target Iteration
The Codegen relies on `build_model_sort_targets(model)` (provided by `build_model_utils` or the Freeze phase) to ensure dependencies are built before dependents.

### 5.2. Compilation Command
For a target named `MyLib`:
1.  Sanitize name for C identifier compatibility (`MyLib` -> `target_MyLib`).
2.  Generate `Nob_Cmd cmd_MyLib = {0};`.
3.  Iterate over sources.
4.  Generate `nob_cc` invocation logic.
    *   *Note:* The Codegen handles the mapping of "Object Library" targets by compiling them but skipping the link step, exposing their `.o` files to dependents.

### 5.3. Linking Command
1.  Gather all object files (own + linked OBJECT libraries).
2.  Gather all link flags (libs, directories).
3.  Invoke `nob_cc` (for executables/shared) or `ar` (for static).

## 6. Interface

```c
// nob_codegen.h

typedef struct {
    bool trace_origin;      // Enable Refinement C
    bool force_rebuild;     // Generate code that always forces rebuild (debug)
    const char *compiler;   // Default compiler override (optional)
} Codegen_Options;

// Writes the complete nob.c file to the stream.
// Returns true on success.
bool nob_codegen_write(const Build_Model *model, FILE *stream, Codegen_Options opts);
```

## 7. String Escaping
The Codegen must implement `codegen_emit_cstr(FILE *f, const char *s)` to correctly escape paths containing quotes (`"`), backslashes (`\`), or control characters within the generated C string literals.
