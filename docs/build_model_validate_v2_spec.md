# Build Model Validation v2 (Normative)

## 1. Overview

The `build_model_validate.c` module acts as the semantic gatekeeper of the build process. It inspects the `Build_Model` (constructed by the Builder) for logical errors, dependency cycles, and policy violations.

**Key Rule:** Validation is **read-only**. It does not modify the model structure; it only reports diagnostics.

**Timing:** This phase runs *after* all events have been processed by the Builder and *before* the model is frozen for Codegen.

## 2. Validation Passes

Validation is performed in a sequence of passes, ordered by severity and dependency.

### 2.1. Structural Integrity (Pass 1 - Fatal)
Checks for basic data consistency that would cause crashes in later stages.
*   **Unique Names:** Verify all targets have unique names (though the Builder should catch this, a second check is safer).
*   **Required Fields:** Ensure every `TARGET_*` has a `NAME` and `TYPE`.
*   **Valid Types:** Confirm `TYPE` is one of `EXECUTABLE`, `STATIC_LIBRARY`, `SHARED_LIBRARY`, `MODULE_LIBRARY`, `INTERFACE_LIBRARY`, `OBJECT_LIBRARY`, or `UTILITY`.

### 2.2. Dependency Resolution (Pass 2 - Fatal)
Ensures the dependency graph is valid.
*   **Missing Dependencies:**
    *   Iterate over `LINK_LIBRARIES` of every target.
    *   If an item looks like a target name (not a file path/flag) but does not exist in the model -> Emit Error.
    *   *Note:* Heuristics distinguish targets from system libs (e.g., `-lm` vs `my_lib`).
*   **Cycle Detection (DFS):**
    *   Perform Depth-First Search on the dependency graph.
    *   If a back-edge is detected (A -> B -> ... -> A) -> Emit Error ("Dependency cycle detected").

### 2.3. Semantic Rules (Pass 3 - Errors/Warnings)
Enforces CMake policies and logical constraints.
*   **Interface Libraries:**
    *   Error if `INTERFACE_LIBRARY` has source files (unless `SOURCES` property is explicitly set to empty/interface-compatible).
    *   Error if `INTERFACE_LIBRARY` links to a `PRIVATE` dependency (must be `INTERFACE`).
*   **Visibility:**
    *   Warning if a `STATIC_LIBRARY` links `PUBLIC` to a `PRIVATE` dependency (transitive visibility issue).
*   **Duplicate Sources:**
    *   Warning if a source file appears multiple times in the same target (though the Builder might deduplicate).

## 3. Implementation Details

### 3.1. Diagnostics Reporting
The validator uses the `Diag_Sink` interface to report issues.

```c
typedef enum {
    BM_VALIDATE_OK = 0,
    BM_VALIDATE_WARNING,
    BM_VALIDATE_ERROR
} BM_Validate_Result;

// Reports an issue with context
void bm_report(Diag_Sink *sink, BM_Validate_Result level, 
               const char *msg, const Build_Target *target, 
               const char *detail);
```

### 3.2. Cycle Detection Algorithm
Standard DFS with 3-color marking:
*   `WHITE`: Unvisited.
*   `GRAY`: Visiting (currently in recursion stack).
*   `BLACK`: Visited (fully processed).

If `GRAY` node is encountered -> Cycle!

### 3.3. Target Lookup Optimization
Validation requires frequent lookups (`find_target("foo")`).
*   **Constraint:** The model stores targets in a flat array/list.
*   **Optimization:** Build a temporary Hash Map (name -> index) at the start of validation to make lookups O(1) instead of O(N). Destroy map after validation.

## 4. Interface

```c
// build_model_validate.h

// Validates the entire model.
// Returns true if no FATAL errors were found.
// Warnings do not cause a return of false.
bool build_model_validate(const Build_Model *model, Diag_Sink *sink);

// Helper: Checks for cycles specifically (can be run independently).
bool build_model_check_cycles(const Build_Model *model, Diag_Sink *sink);
```

## 5. Error Recovery Strategy

If validation fails with Errors:
1.  **Stop:** Do not proceed to Freeze/Codegen.
2.  **Report:** Print all accumulated errors to stderr.
3.  **Exit Code:** Return non-zero from `nobify`.

If validation passes with Warnings only:
1.  **Proceed:** Continue to Freeze/Codegen.
2.  **Report:** Print warnings.
3.  **Exit Code:** Return zero (success).
