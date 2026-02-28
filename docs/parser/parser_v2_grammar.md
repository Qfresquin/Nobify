# Parser v2 Grammar and Recovery Notes

This document describes the parser behavior implemented in `src_v2/parser/parser.c`.

## Command Shape

- A statement is a command invocation: `name(...)`.
- `name` must match `[A-Za-z_][A-Za-z0-9_]*`.
- Control-flow commands are recognized case-insensitively:
  - `if(...) ... [elseif(...) ...]* [else(...) ...] endif()`
  - `foreach(...) ... endforeach()`
  - `while(...) ... endwhile()`
  - `function(...) ... endfunction()`
  - `macro(...) ... endmacro()`

## Argument Parsing Modes

The parser uses two argument modes:

- `ARGS_COMMAND_DEFAULT`
  - Used for regular commands and non-condition argument lists.
  - Nested `(` and `)` are kept as part of arguments.
- `ARGS_CONDITION_EXPR`
  - Used for `if(...)`, `elseif(...)`, and `while(...)` conditions.
  - Nested `(` and `)` are emitted as standalone argument atoms so `eval_expr` can parse grouping.

Quoted and bracket arguments are tagged as:

- `ARG_QUOTED` for `TOKEN_STRING`
- `ARG_BRACKET` for `TOKEN_RAW_STRING` / bracket literals
- `ARG_UNQUOTED` otherwise

## Diagnostics and Recovery

The parser emits diagnostics and continues whenever possible:

- Command identifier without `(` emits an error and syncs to the next line.
- Stray `(` or `)` at block level emits an error.
- Missing closing `)` in argument lists emits an error at command origin.
- Invalid lexer token (`TOKEN_INVALID`) emits an error with token position.
- Missing block terminators (`endif`, `endforeach`, etc.) emit errors.

## Depth Limits

To avoid pathological inputs:

- Max block nesting depth:
  - Env: `CMK2NOB_PARSER_MAX_BLOCK_DEPTH`
  - Default: `512`
- Max parenthesis depth in argument parsing:
  - Env: `CMK2NOB_PARSER_MAX_PAREN_DEPTH`
  - Default: `2048`

When a depth limit is exceeded, the parser emits an error and attempts recovery.

## Memory Ownership

AST memory is arena-owned.

- `ast_free()` is a no-op compatibility API.
- Use `arena_destroy()` to release memory.
