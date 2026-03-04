# Parser v2 Grammar and Recovery Notes

Status: Annex to `parser_v2_spec.md`. This file is a quick grammar/reference view, not the canonical ownership or API contract.

## Command Shape

- A statement is normally `name(...)`.
- `name` must match:

```text
[A-Za-z_][A-Za-z0-9_]*
```

- Control-flow dispatch is case-insensitive for:
  - `if(...)`
  - `foreach(...)`
  - `while(...)`
  - `function(...)`
  - `macro(...)`

If a closing keyword such as `endif` is encountered in the matching nested parse context, it acts as a block terminator. Outside that context, it is just another command name.

## AST-Oriented Grammar

### Regular Command

```text
command := IDENTIFIER "(" args_default ")"
```

Produces:
- `NODE_COMMAND`

### If Block

```text
if_stmt :=
  "if" "(" args_condition ")"
  block
  { "elseif" "(" args_condition ")" block }
  [ "else" "(" args_default ")" block ]
  "endif" [ "(" args_default ")" ]
```

Produces:
- `NODE_IF`

### Foreach Block

```text
foreach_stmt :=
  "foreach" "(" args_default ")"
  block
  "endforeach" [ "(" args_default ")" ]
```

Produces:
- `NODE_FOREACH`

### While Block

```text
while_stmt :=
  "while" "(" args_condition ")"
  block
  "endwhile" [ "(" args_default ")" ]
```

Produces:
- `NODE_WHILE`

### Function / Macro

```text
function_stmt :=
  "function" "(" args_default ")"
  block
  "endfunction" [ "(" args_default ")" ]

macro_stmt :=
  "macro" "(" args_default ")"
  block
  "endmacro" [ "(" args_default ")" ]
```

Produces:
- `NODE_FUNCTION`
- `NODE_MACRO`

## Argument Modes

The parser uses two argument-grouping modes.

### `args_default`

- Used for ordinary command headers and most closing markers.
- Adjacent tokens without left spacing merge into the same `Arg`.
- Nested `(` and `)` are preserved as token fragments within arguments.
- Top-level `;` inside the current paren level is ignored.

Example:

```cmake
func(a(b))
```

becomes one argument containing the tokens:
- `a`
- `(`
- `b`
- `)`

### `args_condition`

- Used for `if`, `elseif`, and `while`.
- Nested `(` and `)` become standalone argument atoms.
- Tokens do not merge across an immediately preceding `(` or `)`.
- Top-level `;` inside the current paren level is ignored.

Example:

```cmake
if((A AND B) OR C)
```

produces condition args conceptually shaped as:
- `(`
- `A`
- `AND`
- `B`
- `)`
- `OR`
- `C`

## Argument Kind Tagging

Newly created arguments are tagged as:

- `ARG_QUOTED` for `TOKEN_STRING`
- `ARG_BRACKET` for `TOKEN_RAW_STRING` or bracket-literal text
- `ARG_UNQUOTED` otherwise

Merged follow-on tokens do not change the original `Arg.kind`.

## Diagnostics and Recovery

The parser emits diagnostics and tries to continue whenever possible.

Primary recovery behaviors:

- Command name without `(`:
  - emits an error
  - skips to the next line

- Invalid command name:
  - emits an error
  - skips to the next line

- Invalid lexer token at statement start:
  - emits an error
  - skips to the next line

- Stray `(` or `)` at block level:
  - emits an error
  - consumes only that token

- Missing closing `)` for an argument list:
  - emits an error at command origin
  - drops the owning node from the AST

- Missing structural terminator (`endif`, `endforeach`, `endwhile`, `endfunction`, `endmacro`):
  - emits an error
  - drops the owning node from the AST

- End-signature mismatch (`endif(foo)` closing `if(bar)` etc.):
  - emits a warning
  - keeps the node

## Depth Limits

- Max block nesting depth:
  - Env: `CMK2NOB_PARSER_MAX_BLOCK_DEPTH`
  - Default: `512`

- Max parenthesis depth while parsing args:
  - Env: `CMK2NOB_PARSER_MAX_PAREN_DEPTH`
  - Default: `2048`

When a limit is exceeded, the parser emits an error and switches into recovery behavior rather than aborting immediately.

## Memory Ownership

AST memory is arena-owned.

- `ast_free()` is a no-op.
- Use the owning arena's lifetime controls to release parser allocations.
