# Parser v2 Specification

Status: Canonical parsing contract for `src_v2/parser/parser.c` and `src_v2/parser/parser.h`.

## 1. Role

The v2 parser converts a flat lexer token stream into an arena-owned AST.

It is responsible for:
- validating command statement shape,
- grouping tokens into arguments,
- building structured control-flow nodes,
- emitting diagnostics and attempting recovery when syntax is invalid.

It is not responsible for:
- lexing source text,
- evaluating expressions,
- expanding variables,
- semantic validation of command meaning.

## 2. Inputs and Preconditions

### 2.1 Token Source

`parse_tokens()` consumes a `Token_List` produced by the lexer layer.

Expected caller contract:
- The list does not include `TOKEN_END`.
- Token text slices remain valid for the lifetime of the AST.

The parser treats `docs/lexer/lexer_v2_spec.md` as its lexical pre-contract.

### 2.2 Arena Requirement

`Arena *arena` is mandatory.

If `parse_tokens()` is called with `arena == NULL`:
- the parser emits an error diagnostic,
- returns `NULL`,
- and performs no parsing work.

## 3. Public API

### 3.1 Main Entry Point

- `Ast_Root parse_tokens(Arena *arena, Token_List tokens)`

Parses the entire token list as a top-level block and returns an arena-owned `Node_List`.

Return contract:
- On ordinary syntax errors, still returns the successfully recovered portion of the AST.
- On parser OOM / append-budget fault injection, returns `NULL`.
- On empty input, returns `NULL` because an empty `Node_List` is represented as a null pointer.

Important API ambiguity:
- `Ast_Root == NULL` can mean an empty AST, a null-arena early exit, or parser OOM.
- The caller cannot distinguish those cases from the return value alone; diagnostics/context must be consulted if that distinction matters.

### 3.2 Memory Ownership

All AST data is arena-owned.

This includes:
- the root `Node_List`,
- every nested `Node_List`,
- every `Args` array,
- every `Arg.items` token slice array,
- every `ElseIf_Clause_List`.

- `void ast_free(Ast_Root root)`
Compatibility no-op. Memory is released only when the owning arena is rewound/reset/destroyed.

### 3.3 Debug Helper

- `void print_ast(Ast_Root root, int indent)`
Pretty-prints the AST for debugging. It is not part of the semantic contract.

## 4. AST Surface

### 4.1 Argument Model

`Arg_Kind` values:

- `ARG_UNQUOTED`
Regular argument segment. This is the default for identifiers, variables, generator expressions, and most merged token runs.

- `ARG_QUOTED`
Argument originated from a `TOKEN_STRING`.

- `ARG_BRACKET`
Argument originated from a bracket/raw string token or any token slice that matches the CMake bracket-literal shape.

`Arg` contains:
- `Token *items`: the token fragments merged into that single logical argument.
- `Arg_Kind kind`: the quoting class tracked for that argument.

`Args` is a dynamic array of `Arg`.

### 4.2 Node Kinds

Current `Node_Kind` values:

- `NODE_COMMAND`
Regular command invocation such as `set(...)` or `message(...)`.

- `NODE_IF`
Structured `if()/elseif()/else()/endif()` block.

- `NODE_FOREACH`
Structured `foreach()/endforeach()` block.

- `NODE_WHILE`
Structured `while()/endwhile()` block.

- `NODE_FUNCTION`
Structured `function()/endfunction()` definition.

- `NODE_MACRO`
Structured `macro()/endmacro()` definition.

Every node stores:
- `line`
- `col`

These source positions are copied from the starting command token.

### 4.3 Structured Node Payloads

- `NODE_COMMAND`
Stores:
- `name`
- `args`

- `NODE_IF`
Stores:
- `condition`
- `then_block`
- `elseif_clauses`
- `else_block`

- `NODE_FOREACH`
Stores:
- `args`
- `body`

- `NODE_WHILE`
Stores:
- `condition`
- `body`

- `NODE_FUNCTION` / `NODE_MACRO`
Stores:
- `name`
- `params`
- `body`

`function()`/`macro()` names are taken from the first parsed argument if present. Remaining definition arguments become `params`.

## 5. Command Statement Contract

### 5.1 Base Statement Shape

Every statement must begin with:

1. `TOKEN_IDENTIFIER`
2. whose text matches a valid command name
3. followed immediately by `TOKEN_LPAREN`

Valid command name pattern:

```text
[A-Za-z_][A-Za-z0-9_]*
```

Command-name matching for parser control-flow dispatch is case-insensitive ASCII.

### 5.2 Invalid Statement Starts

If a statement begins with a non-identifier token:
- `TOKEN_INVALID` produces an error about invalid token.
- any other token produces an error about unexpected statement start.

Recovery:
- the parser syncs to the next source line,
- drops the malformed statement,
- continues parsing later statements.

### 5.3 Identifier That Is Not a Valid Command Name

If the leading identifier fails the command-name regex:
- the parser emits an error,
- syncs to the next source line,
- and drops that statement.

This is intentionally stricter than the lexer, which allows broader `TOKEN_IDENTIFIER` content.

### 5.4 Missing Parentheses

If a valid command name is not followed by `(`:
- the parser emits an error,
- syncs to the next source line,
- and drops that statement.

## 6. Argument Parsing

The parser uses two argument modes:

- `ARGS_COMMAND_DEFAULT`
- `ARGS_CONDITION_EXPR`

Both modes require the caller to be positioned at the opening `(` token.

### 6.1 Parenthesis Consumption

Argument parsing:
- consumes the opening `(`,
- parses until the matching closing `)`,
- consumes that closing `)`,
- returns the resulting `Args`.

If EOF is reached before the matching `)`:
- the parser emits an error at the origin command location,
- marks the parse as unsuccessful,
- and the owning statement node is discarded.

### 6.2 Parenthesis Depth

The parser tracks parenthesis nesting depth while parsing arguments.

Depth starts at `1` after the opening `(` and decreases when the matching close is found.

If nesting exceeds the configured maximum:
- an error is emitted once for that argument list,
- the parser enters overflow-skipping mode,
- extra nested content is skipped until the overflow depth unwinds,
- then parsing resumes.

This is recovery behavior, not full-fidelity preservation.

### 6.3 Top-Level Semicolons

Inside an argument list, `TOKEN_SEMICOLON` at depth `1` is ignored as a standalone separator token.

Consequences:
- the parser does not create a dedicated AST separator node for `;`,
- semicolon semantics are left to later stages that interpret string/list content.

Nested semicolons (inside deeper parenthesis depth) are preserved as ordinary tokens.

### 6.4 Default Command Mode (`ARGS_COMMAND_DEFAULT`)

Used for:
- regular commands,
- `foreach(...)`,
- `function(...)`,
- `macro(...)`,
- `else(...)`,
- end-marker argument checks such as `endif(...)`.

Merging rule:
- if the next token has `has_space_left == false`, it is appended to the previous `Arg`,
- otherwise a new `Arg` is created.

This preserves concatenated argument fragments such as:
- `lib${VAR}.a` => one logical argument made of multiple tokens.

Nested `(` and `)` tokens inside deeper argument depth are preserved as ordinary token fragments and follow the same merge rule.

### 6.5 Condition Mode (`ARGS_CONDITION_EXPR`)

Used for:
- `if(...)`
- `elseif(...)`
- `while(...)`

Condition mode changes grouping so parentheses remain explicit expression atoms:
- nested `(` always starts a new `Arg`
- nested `)` always starts a new `Arg`

Additional merge guard:
- even when `has_space_left == false`, a token does not merge into the previous argument if the previous token is `(` or `)`.

This is intentional so `eval_expr` can see grouping boundaries directly.

Example:
- `if((A AND B) OR C)` becomes separate args for `(`, `A`, `AND`, `B`, `)`, `OR`, `C`.

### 6.6 Argument Kind Inference

When a new `Arg` is created, its `kind` is inferred from the first token:

- `TOKEN_STRING` => `ARG_QUOTED`
- `TOKEN_RAW_STRING` => `ARG_BRACKET`
- bracket-shaped literal text => `ARG_BRACKET`
- otherwise => `ARG_UNQUOTED`

If later tokens merge into an existing `Arg`, the original `kind` is preserved.

## 7. Structured Control Flow

### 7.1 `if()`

`if()` parses as:
- condition in condition mode,
- `then_block` until one of `else`, `elseif`, or `endif`,
- zero or more `elseif` clauses,
- optional `else()` block,
- mandatory `endif`.

`elseif(...)` clauses:
- are parsed in a loop,
- each carries its own condition and block.

`else()`:
- its parentheses are parsed in default mode,
- the resulting args are currently discarded,
- malformed `else(...)` args still invalidate the parent `if` node.

`endif(...)`:
- if present with parentheses, its args are parsed in default mode,
- if both the original condition's first argument and the closing args' first argument exist, mismatches emit a warning.

If no terminating `endif` is found:
- an error is emitted,
- the entire `if` node is discarded.

### 7.2 `foreach()`

`foreach()` parses:
- header args in default mode,
- `body` until `endforeach`.

If `endforeach(...)` has arguments:
- they are parsed in default mode,
- its first arg is compared to the first `foreach` arg (usually the loop variable),
- mismatch emits a warning.

Missing `endforeach` discards the whole node with an error.

### 7.3 `while()`

`while()` parses:
- condition in condition mode,
- `body` until `endwhile`.

If `endwhile(...)` has arguments:
- they are parsed in default mode,
- the first arg is compared to the first `while` condition arg,
- mismatch emits a warning.

Missing `endwhile` discards the whole node with an error.

### 7.4 `function()` and `macro()`

`function()`/`macro()` parse:
- header args in default mode,
- first arg becomes the definition name when present,
- remaining args become `params`,
- `body` is parsed until `endfunction` / `endmacro`.

If the end marker includes args:
- they are parsed in default mode,
- the first closing arg is compared against the stored definition name,
- mismatch emits a warning.

Missing terminators discard the whole node with an error.

## 8. Block Parsing

### 8.1 Top-Level Root

`parse_tokens()` parses the entire file as a top-level block with no terminators.

The root is therefore just a `Node_List`.

### 8.2 Terminator Matching

Nested blocks are terminated by identifier tokens matched case-insensitively against the expected closing keywords.

Only identifiers are eligible as terminators.

Practical consequence:
- `endif()` is a structural terminator only when encountered in the correct nested parser context.
- At top level, `endif()` is parsed as a regular command statement.

### 8.3 Stray Parentheses at Block Level

If `(` or `)` appears directly at block level:
- the parser emits an error,
- consumes the token,
- and continues scanning the block.

## 9. Diagnostics and Recovery

### 9.1 Diagnostic Style

The parser reports syntax issues through `diag_log(...)` using component `"parser"`.

It emits:
- errors for malformed syntax and hard parser limits,
- warnings for end-signature mismatches only.

### 9.2 Statement Drop Rule

Internally, recovery often returns an empty placeholder command node.

That placeholder is filtered out before insertion into the parent block unless it has a real command name.

Practical consequence:
- syntax-failed statements usually disappear entirely from the final AST rather than remaining as explicit error nodes.

### 9.3 Line-Based Sync

For statement-start and missing-parenthesis failures, recovery syncs by advancing to the next token whose `line` differs from the failing statement's start line.

This makes recovery deterministic and coarse-grained:
- multiple malformed tokens on the same physical line are skipped together.

## 10. Limits and Environment Controls

### 10.1 Block Depth Limit

Environment variable:
- `CMK2NOB_PARSER_MAX_BLOCK_DEPTH`

Default:
- `512`

Minimum accepted value:
- `1`

If exceeded:
- the parser emits an error,
- scans forward to the nearest matching terminator for the current block when possible,
- and returns from that block early.

This can leave outer closing markers to be parsed later as ordinary commands, depending on where recovery resumes.

### 10.2 Parenthesis Depth Limit

Environment variable:
- `CMK2NOB_PARSER_MAX_PAREN_DEPTH`

Default:
- `2048`

Minimum accepted value:
- `1`

If exceeded, see the overflow-skipping behavior in section 6.2.

### 10.3 Fault-Injection Append Budget

Environment variable:
- `CMK2NOB_PARSER_FAIL_APPEND_AFTER`

This is an internal testing hook used by the suite to simulate append/allocation failure.

Behavior:
- after the configured number of successful append operations, further appends fail,
- the parser emits one OOM-style error,
- sets internal OOM state,
- and `parse_tokens()` returns `NULL`.

This is not a normal runtime tuning knob.

## 11. OOM Behavior

If any arena-backed append fails:
- the parser emits one internal allocation error (once),
- sets sticky OOM state,
- aborts further structural building,
- and returns `NULL` from `parse_tokens()`.

Partially built arena data may still exist, but the returned AST root is `NULL`.

## 12. Current Non-Goals / Divergences

Current intentional limits of the parser:

- It validates only command shape, not command semantics.
- It does not create dedicated error nodes in the AST.
- It does not preserve standalone top-level semicolon tokens inside arg lists.
- It treats top-level unmatched terminators like `endif()` as ordinary commands if not in a matching parse context.
- `else(...)` arguments are parsed only for syntax validity; they are not stored in the AST.

## 13. Example

Input:

```cmake
if((A AND B) OR C)
  message("then")
elseif(D)
  message("elseif")
else()
  foreach(item a b)
    message(${item})
  endforeach()
endif()
```

High-level AST:

1. `NODE_IF`
2. `condition` contains standalone grouping-paren args plus expression atoms
3. `then_block` contains one `NODE_COMMAND`
4. `elseif_clauses` contains one clause
5. `else_block` contains one `NODE_FOREACH`
