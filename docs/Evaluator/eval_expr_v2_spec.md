# Evaluator Expression Logic v2 (Annex)

Status: Normative annex for `src_v2/evaluator/eval_expr.c`.

## 1. Scope

This module provides:
- Variable expansion for evaluator argument resolution.
- Truthiness evaluation.
- Condition parsing/evaluation for `if()` and `while()`.

## 2. Variable Expansion

Entry point:
- `String_View eval_expand_vars(Evaluator_Context *ctx, String_View input)`

Supported expansion forms:
- `${VAR}`
- nested `${${VAR}}`
- `$ENV{VAR}`
- escaped `\${VAR}` (preserved literal)

Undefined variable behavior:
- expands to empty string.

Recursion control:
- default recursion limit: `100`
- hard cap: `10000`
- script/runtime override: `CMAKE_NOBIFY_EXPAND_MAX_RECURSION`
- process env override: `NOBIFY_EVAL_EXPAND_MAX_RECURSION`

Memory:
- expansion output is allocated in temp arena.
- persistent use must copy to event arena.

## 3. Truthiness

Core evaluator truthiness includes:
- True constants: `1`, `ON`, `YES`, `TRUE`, `Y`.
- False constants: `0`, `OFF`, `NO`, `FALSE`, `N`, `IGNORE`, empty string, `NOTFOUND`, and `*-NOTFOUND` suffix.
- Numeric strings: parsed as floating-point; non-zero is true.

Unknown token resolution path:
1. Try macro binding lookup.
2. Try variable lookup.
3. Re-evaluate resolved token as constant truthiness.
4. If still unknown, false.

## 4. Condition Grammar and Operators

Condition parser supports parentheses and `NOT`, with precedence:
1. unary/predicate and comparisons
2. `AND`
3. `OR`

Unary predicates:
- `DEFINED`
- `TARGET`
- `COMMAND`
- `POLICY`
- `EXISTS`
- `IS_DIRECTORY`
- `IS_SYMLINK`
- `IS_ABSOLUTE`
- `IS_READABLE`
- `IS_WRITABLE`
- `IS_EXECUTABLE`
- `TEST`

Binary operators:
- string: `STREQUAL`, `STRLESS`, `STRGREATER`, `STRLESS_EQUAL`, `STRGREATER_EQUAL`
- numeric: `EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`
- version: `VERSION_LESS`, `VERSION_GREATER`, `VERSION_EQUAL`, `VERSION_LESS_EQUAL`, `VERSION_GREATER_EQUAL`
- regex/list/path/time: `MATCHES`, `IN_LIST`, `PATH_EQUAL`, `IS_NEWER_THAN`

## 5. Host Environment Semantics

Filesystem and permission predicates are evaluated against host runtime environment:
- `EXISTS`, directory/symlink checks
- readability/writability/executability checks
- mtime comparison for `IS_NEWER_THAN`

`if(COMMAND name)` checks both:
- built-in dispatcher command table
- user-defined function/macro registry

`if(TEST name)` checks evaluator-created test marker state.

## 6. Error Behavior

Invalid expression syntax triggers diagnostic emission and evaluates as false in `eval_condition` path.

Typical syntax errors include:
- unbalanced parentheses
- unexpected trailing tokens
- malformed comparison expressions

## 7. Generator Expressions

Generator expressions (`$<...>`) are not evaluated in this module.
They are treated as literal text at evaluator stage.

## 8. Implemented Divergences

Current intentional behavior:
- Host-based predicate evaluation during transpilation.
- Unknown/non-resolved tokens default to false after variable/macro lookup path.
- Recursion hard cap to prevent pathological expansion loops.

## 9. Roadmap (Not Yet Implemented)

Roadmap, not current behavior:
- Additional CMake expression predicates beyond current implemented set.
- Wider policy-conditioned expression quirks matching legacy CMake edge cases.
