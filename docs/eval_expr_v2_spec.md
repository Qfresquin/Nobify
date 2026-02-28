# Evaluator Expression Logic v2 (Normative)

## 1. Overview

The `eval_expr.c` module implements the boolean logic engine and variable expansion mechanism for the Evaluator. It is the "ALU" (Arithmetic Logic Unit) of the transpiler.

**Responsibilities:**
1.  **Variable Expansion:** `${VAR}`, `$ENV{VAR}`, and escaping logic.
2.  **Condition Evaluation:** `if()`, `while()` predicates (AND, OR, NOT, STREQUAL, VERSION_LESS, etc.).
3.  **Policy Compliance:** Handling CMake's "truthiness" quirks (e.g., "ON", "YES", "Y", "TRUE" vs "OFF", "NO", "N", "FALSE", "IGNORE").

## 2. Variable Expansion (`expand_vars`)

The Evaluator must flatten all variable references into string literals before they reach the Build Model.

### 2.1. Syntax Handling
*   **Simple:** `${VAR}` -> Looks up `VAR` in the current scope stack.
*   **Nested:** `${${VAR}}` -> Inner `${VAR}` is expanded first, then the result is used as the key.
*   **Environment:** `$ENV{VAR}` -> Reads from the host environment variable `VAR`.
*   **Escaped:** `\${VAR}` -> Preserved as literal string `${VAR}` (rare, but supported).

### 2.2. Expansion Rules
1.  **Undefined Variables:** Expand to empty string `""`.
2.  **Lists:** CMake lists are semicolon-separated strings (`"a;b;c"`). Expansion preserves semicolons unless inside a quoted argument where they are literal.
3.  **Recursion Limit:** The expander must track recursion depth to prevent stack overflow from circular references (limit: 100).

### 2.3. Memory Strategy
*   **Input:** `String_View` (raw argument from AST).
*   **Output:** `String_View` (allocated in `ctx->arena`).
*   **Lifetime:** The result is temporary (valid only for the current command execution). If needed for an event, it must be copied to `ctx->event_arena`.

## 3. Boolean Expression Evaluation (`eval_condition`)

CMake's `if()` command supports a complex expression language. The Evaluator must parse and evaluate this *at transpilation time* to determine which branch to take.

### 3.1. Operator Precedence (Standard CMake)
1.  `Parentheses ()`
2.  `Unary Ops` (NOT, EXISTS, COMMAND, DEFINED, TARGET, POLICY, TEST, IS_DIRECTORY, IS_SYMLINK, IS_ABSOLUTE, IS_READABLE, IS_WRITABLE, IS_EXECUTABLE)
3.  `Binary Ops` (STREQUAL, EQUAL, STRLESS, VERSION_LESS, MATCHES, IN_LIST, PATH_EQUAL, IS_NEWER_THAN)
4.  `Logical AND`
5.  `Logical OR`

### 3.2. Truthiness Table
The function `is_true(String_View val)` implements CMake's specific boolean logic:

| Input (Case Insensitive) | Boolean Result |
| :--- | :--- |
| `1`, `ON`, `YES`, `TRUE`, `Y` | **True** |
| `0`, `OFF`, `NO`, `FALSE`, `N`, `IGNORE`, `""`, `NOTFOUND` | **False** |
| `*-NOTFOUND` | **False** |
| numeric strings (integer/float, e.g. `2`, `-3`, `0.5`) | **True** if non-zero |
| *Any other string* | **False** (treated as string literal or variable name depending on context) |

### 3.3. Special Predicates
*   `if(DEFINED VAR)`: Checks if `VAR` exists in the symbol table.
*   `if(COMMAND cmd)`: Checks if `cmd` is a known command, macro, or function.
*   `if(EXISTS path)`:
    *   **Constraint:** Since the transpiler runs on the *host* machine but builds for a potentially different *target* environment, filesystem checks like `EXISTS` are tricky.
    *   **Policy:** `if(EXISTS)` checks the file on the **host machine** at transpilation time. This assumes the source tree is present.
*   `if(TARGET target_name)`: Checks if `target_name` has been declared *so far* in the execution flow.
*   `if(TEST test_name)`: Checks if `add_test()` registered the given test during evaluator execution.
*   `if(IS_READABLE|IS_WRITABLE|IS_EXECUTABLE path)`: Host-filesystem permission predicates.
*   `if(path1 IS_NEWER_THAN path2)`: Host-filesystem mtime comparison.

## 4. Interfaces

```c
// eval_expr.h

// Expands variables in a string (e.g., "${SRC_DIR}/main.c" -> "/path/src/main.c")
// Returns a new string allocated in ctx->arena.
String_View eval_expand_vars(Evaluator_Context *ctx, String_View input);

// Evaluates a list of arguments as a boolean condition.
// Returns true/false. Emits EV_DIAGNOSTIC on syntax error.
bool eval_condition(Evaluator_Context *ctx, const Args *args);

// Helper: Checks CMake truthiness
bool cmk_is_true(String_View value);
```

## 5. Generator Expressions (`$<...>`)

**Crucial Distinction:**
*   The Evaluator **DOES NOT** evaluate Generator Expressions (`$<CONFIG:...>`, `$<TARGET_FILE:...>`).
*   These are treated as **String Literals** and passed through to the Event Stream as-is.
*   **Reason:** Their value often depends on the build configuration (Debug vs Release), which is not known until `nob` compiles/runs. The Build Model stores them, and `nob_codegen` emits logic to handle them at runtime (if supported) or they are passed to the compiler/linker (if supported by toolchain).

## 6. Error Reporting

Errors in expressions (e.g., `if(NOT)` with missing operand) are **fatal** for the condition evaluation but handled gracefully:
1.  Emit `EV_DIAGNOSTIC` (Error: "Invalid boolean expression").
2.  Return `false` (default safe fallback).
3.  The Evaluator may choose to skip the block or abort depending on strictness policy.

All emitted diagnostics are classified with compatibility metadata (`code`, `error_class`) by the centralized evaluator diagnostic path.
