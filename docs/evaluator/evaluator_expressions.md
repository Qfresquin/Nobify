# Evaluator Expressions (Rewrite Draft)

Status: Draft rewrite. This document describes the current expression subsystem in `src_v2/evaluator/eval_expr.c`, including variable expansion, truthiness, and `if()/while()` condition parsing.

## 1. Scope

This document covers:
- `eval_expand_vars(...)` variable/environment expansion,
- expansion recursion/cycle limits,
- `eval_truthy(...)` rules,
- `eval_condition(...)` expression parsing/evaluation,
- unary predicates and binary operators currently supported,
- diagnostics and known behavior limits in expression handling.

It does not restate full command execution flow outside expression semantics.

## 2. Source of Truth

Primary implementation files for this slice:
- `src_v2/evaluator/eval_expr.c`
- `src_v2/evaluator/eval_expr.h`
- `src_v2/evaluator/evaluator.c` (argument resolution feeding expressions)
- `src_v2/evaluator/evaluator_internal.h`
- `src_v2/evaluator/eval_utils.c` (environment/path helpers)

## 3. Public Expression API

Current API:

```c
String_View eval_expand_vars(struct Evaluator_Context *ctx, String_View input);
bool eval_condition(struct Evaluator_Context *ctx, const Args *raw_condition);
bool eval_truthy(struct Evaluator_Context *ctx, String_View v);
```

High-level contract:
- `eval_expand_vars(...)` returns a temp-arena `String_View`.
- `eval_condition(...)` returns `false` for empty/invalid expressions and for false conditions.
- `eval_truthy(...)` is the shared boolean conversion helper used by expression parsing and other evaluator paths.

## 4. Condition Input Token Model

`eval_condition(...)` does not parse raw source text directly. It receives parser `Args` and starts by calling:

```c
SV_List toks = eval_resolve_args(ctx, raw_condition);
```

Important pre-parser effects:
- `${...}` and `$ENV{...}` expansion already run on unquoted args.
- unquoted semicolon lists are split into multiple tokens.
- quoted/bracket args preserve semicolons as text.

Practical consequence:
- expression semantics depend on both parser arg grouping and evaluator arg resolution rules, not only on `eval_expr.c`.

## 5. Variable Expansion Model (`eval_expand_vars`)

### 5.1 Supported Forms

Current forms supported:
- `${VAR}`
- nested `${${VAR}}`
- `$ENV{VAR}`
- escaped dollar `\${...}` (literal `${...}` output)

Lookup behavior for `${...}`:
- first macro bindings (`eval_macro_bind_get`)
- then visible variables (`eval_var_get_visible`)
- missing names expand to empty string

Environment lookup:
- `$ENV{X}` reads process environment via `eval_getenv_temp(...)`

### 5.2 Expansion Iteration

Expansion is iterative (`expand_once` loop) until:
- output stabilizes (no change), or
- cycle is detected, or
- recursion limit is reached.

### 5.3 Memory Contract

`eval_expand_vars(...)`:
- works in temp arena (`ctx->arena`)
- returns temporary views invalidated by later rewinds
- returns empty on null/stopped context

## 6. Expansion Limits and Cycle Handling

Defaults:
- soft limit: `100` (`EVAL_EXPAND_MAX_RECURSION`)
- hard cap for configured values: `10000`

Configuration sources:
- `CMAKE_NOBIFY_EXPAND_MAX_RECURSION` (context variable)
- `NOBIFY_EVAL_EXPAND_MAX_RECURSION` (process environment)

Current precedence:
- environment override wins when valid
- otherwise variable value wins
- otherwise default is used

Cycle behavior:
- repeated expansion state triggers warning diagnostic (`Cyclic variable expansion detected`)
- function returns the repeated value instead of aborting

Limit behavior:
- exceeding iteration limit triggers warning diagnostic (`Recursion limit exceeded`)
- function returns the last computed value

## 7. Truthiness Model (`eval_truthy`)

### 7.1 Direct Constants

True constants:
- `1`, `ON`, `YES`, `TRUE`, `Y` (case-insensitive)
- any non-zero numeric string accepted by `strtod(...)`

False constants:
- empty string
- `0`, `OFF`, `NO`, `FALSE`, `N`, `IGNORE`, `NOTFOUND` (case-insensitive)
- values ending with `-NOTFOUND` (case-insensitive)
- numeric zero forms accepted by `strtod(...)`

### 7.2 Non-Constant Tokens

If token is not a known constant:
- macro binding lookup is attempted,
- then visible variable lookup is attempted,
- if neither exists, the token is treated as true.

Important current nuance:
- when macro/variable lookup succeeds, its value is interpreted only by constant rules (`eval_truthy_constant_only`), which can produce false for non-constant textual values.

## 8. Expression Grammar and Precedence

The parser is recursive-descent with this precedence:

1. parenthesized / unary / primary
2. comparison layer
3. `AND`
4. `OR`

Conceptual grammar:

```text
expr      := and_expr { OR and_expr }
and_expr  := cmp_expr { AND cmp_expr }
cmp_expr  := unary_expr | value [binary_op value]
unary_expr:= NOT unary_expr
          | DEFINED ...
          | TARGET ...
          | COMMAND ...
          | POLICY ...
          | EXISTS ...
          | IS_DIRECTORY ...
          | IS_SYMLINK ...
          | IS_ABSOLUTE ...
          | IS_READABLE ...
          | IS_WRITABLE ...
          | IS_EXECUTABLE ...
          | TEST ...
          | primary
primary   := "(" expr ")" | value
```

Current evaluation model:
- `AND` / `OR` are evaluated left-to-right
- parser always parses RHS terms (no short-circuit parser skip)

## 9. Unary Predicate Semantics

Supported unary predicates in `parse_unary(...)`:
- `NOT <expr>`
- `DEFINED <name>`
- `DEFINED ENV{<name>}`
- `TARGET <name>`
- `COMMAND <name>`
- `POLICY <CMPxxxx>`
- `EXISTS <path>`
- `IS_DIRECTORY <path>`
- `IS_SYMLINK <path>`
- `IS_ABSOLUTE <path>`
- `IS_READABLE <path>`
- `IS_WRITABLE <path>`
- `IS_EXECUTABLE <path>`
- `TEST <name>`

Current meaning notes:
- `TARGET` checks `eval_target_known(...)`.
- `COMMAND` checks context-native registry plus registered user commands.
- `POLICY` checks whether policy id is known, not whether current effective value is `NEW`/`OLD`.
- `TEST` checks definition of `NOBIFY_TEST::<name>` variable.
- filesystem predicates use host filesystem APIs (`stat`, `access`, etc.).

## 10. Binary Operator Semantics

Supported binary operators:
- `STREQUAL`
- `EQUAL`
- `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`
- `STRLESS`, `STRGREATER`, `STRLESS_EQUAL`, `STRGREATER_EQUAL`
- `VERSION_LESS`, `VERSION_GREATER`, `VERSION_EQUAL`, `VERSION_LESS_EQUAL`, `VERSION_GREATER_EQUAL`
- `MATCHES`
- `IN_LIST`
- `PATH_EQUAL`
- `IS_NEWER_THAN`

Current behavior notes:
- numeric operators use integer `strtol(...)` parsing (`long`); parse failure yields false.
- version operators compare dot-separated segments, numeric when both segments are digits.
- `MATCHES` uses POSIX regex (`regcomp` with `REG_EXTENDED`); invalid regex currently returns false.
- `IN_LIST` compares `needle` against semicolon items of RHS text.
- `PATH_EQUAL` compares lexically normalized paths (no filesystem stat).
- `IS_NEWER_THAN` compares file mtimes (`stat`) and returns false on missing/stat failure.

### 10.1 Variable Lookup Inside Operators

Most binary operators consume resolved tokens directly.

Current explicit one-step macro/variable lookup helper (`sv_lookup_if_var`) is used by:
- `IN_LIST`
- `PATH_EQUAL`
- `IS_NEWER_THAN`

Other operators rely on earlier arg-expansion results and do not perform additional token-to-variable indirection.

## 11. Diagnostics and Error Handling

Current expression diagnostics:
- missing `)` emits error diagnostic (`Missing ')' in expression`)
- leftover tokens after parse emit error diagnostic (`Invalid if() syntax`)
- expansion cycle/limit emit warning diagnostics from `eval_expand_vars(...)`

Current behavior for many malformed forms:
- parser may return false without a dedicated diagnostic (for example missing RHS in some operators)

Current origin detail:
- `eval_condition(...)` currently does not populate parser origin in its local `Expr` struct, so expression diagnostics may have zeroed line/column metadata.

Current command tag detail:
- some expression diagnostics are tagged with command `"if"` even when condition evaluation is used by `while()`.

## 12. Current Limits and Divergences

Current visible limitations:
- no dedicated short-circuit skip behavior for `AND`/`OR` parse path.
- no capture-group variable side effects for `MATCHES` (`CMAKE_MATCH_<n>` style behavior is not produced here).
- truthiness semantics are evaluator-specific and include the fallback where unresolved non-constant bare tokens evaluate as true.
- `PATH_EQUAL` is lexical normalization-based and does not canonicalize via filesystem resolution.
- expression parser consumes tokens after `eval_resolve_args(...)`, so list-splitting and expansion behavior can affect condition shape before parsing.

## 13. Relationship to Other Docs

- `evaluator_v2_spec.md`
Top-level canonical evaluator contract.

- `evaluator_execution_model.md`
Defines where `eval_condition(...)` is invoked in `if`/`while` traversal.

- `evaluator_variables_and_scope.md`
Defines variable/macro lookup model used by expansion and truthiness.

- `evaluator_compatibility_model.md`
Defines compatibility knobs that can indirectly affect expression diagnostics/stop behavior.
