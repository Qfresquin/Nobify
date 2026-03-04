# Lexer v2 Specification

Status: Canonical tokenization contract for `src_v2/lexer/lexer.c` and `src_v2/lexer/lexer.h`.

## 1. Role

The v2 lexer converts raw CMake-like source text into a flat token stream for the parser.

It is responsible for:
- skipping whitespace, comments, and line continuations,
- recognizing the project's token classes,
- preserving exact source slices for each token,
- attaching source position metadata (`line`, `col`, `has_space_left`).

It is not responsible for:
- validating command names,
- building AST structure,
- interpreting escapes,
- normalizing token text.

## 2. Public API

### 2.1 Token Kinds

Current `Token_Kind` values:

- `TOKEN_END`
End-of-input sentinel.

- `TOKEN_INVALID`
Malformed lexeme that could not be closed correctly (for example an unterminated string, variable, generator expression, or bracket argument).

- `TOKEN_LPAREN`
Literal `(`.

- `TOKEN_RPAREN`
Literal `)`.

- `TOKEN_SEMICOLON`
Literal `;`.

- `TOKEN_STRING`
Double-quoted string token. The token text includes both quotes.

- `TOKEN_RAW_STRING`
Bracket argument token such as `[[text]]` or `[=[text]=]`. The token text includes both delimiters.

- `TOKEN_IDENTIFIER`
General unquoted token segment. This is intentionally broader than a valid CMake command name.

- `TOKEN_VAR`
Variable-like reference starting with `$` and using `{...}` delimiters, including `${VAR}` and `$ENV{PATH}`.

- `TOKEN_GEN_EXP`
Generator expression starting with `$<` and ending at the matching `>`.

### 2.2 Token Structure

Each `Token` contains:

- `kind`
The token kind.

- `text`
`String_View` slice into the original source buffer.

- `line`
1-based source line at token start.

- `col`
1-based source column at token start.

- `has_space_left`
`true` when at least one byte of whitespace, comment text, or line continuation was skipped immediately before this token.

### 2.3 Lexer State

`Lexer` stores:

- `content`
Full source buffer being scanned.

- `cursor`
Current byte offset.

- `line`
Current 1-based line counter.

- `bol`
Byte offset of the beginning of the current line.

Callers should treat `Lexer` as mutable scan state, not as a reusable immutable config object.

### 2.4 Functions

- `Lexer lexer_init(String_View content)`
Initializes a lexer over `content`.

Initial state:
- `cursor = 0`
- `line = 1`
- `bol = 0`

- `Token lexer_next(Lexer *l)`
Returns the next token after running the skip phase.

- `const char *token_kind_name(Token_Kind kind)`
Returns a stable debug name for the token kind. Unknown enum values map to `"UNKNOWN"`.

## 3. Memory and Ownership

The lexer performs no allocations.

All token text slices alias `Lexer.content`, so the caller must keep the original source buffer alive for as long as the tokens are needed.

The lexer never copies, unescapes, or re-encodes token text.

## 4. Scan Loop

Each call to `lexer_next()` follows this sequence:

1. Record the starting cursor.
2. Skip ignorable input (whitespace, comments, and line continuations).
3. Set `has_space_left` if the cursor moved during the skip phase.
4. Capture token `line`/`col` at the post-skip cursor.
5. Emit one token, or `TOKEN_END` if EOF was reached.

`TOKEN_END` is returned only after the skip phase has consumed all remaining ignorable input.

## 5. Skip Phase

### 5.1 Whitespace

The lexer skips any byte recognized by `isspace((unsigned char)c)`.

Line tracking is LF-driven:
- `line` increments only when the consumed byte is `'\n'`.
- `bol` is updated to the byte after that `'\n'`.

Practical consequence:
- LF and CRLF inputs produce correct line advancement because the LF is still consumed.
- Bare `'\r'` is skipped as whitespace, but does not itself advance the line counter.

### 5.2 Line Comments

A `#` starts a line comment unless it is immediately followed by a bracket opener that forms a block comment.

For ordinary line comments, the lexer consumes:
- the `#`,
- then every byte until `'\n'` or EOF.

The terminating newline is not part of the comment; it is consumed by the surrounding skip loop as whitespace.

### 5.3 Bracket Block Comments

If the lexer sees `#` followed immediately by a bracket opener, it treats the sequence as a block comment.

Supported openers:
- `#[[`
- `#[=[`
- `#[==[`
- and any `#` + `[=*[` form

The closing delimiter must use the same number of `=` bytes:
- `]]`
- `]=]`
- `]==]`
- etc.

Behavior:
- The entire block comment is skipped.
- The content may span lines.
- Mismatched closer lengths do not terminate the comment early.
- Nested block comments are not recognized specially; the first matching closer ends the comment.

If EOF is reached before a matching closer, the lexer treats the rest of the file as skipped comment content. No `TOKEN_INVALID` is emitted for unterminated comments.

### 5.4 Line Continuations

The lexer skips CMake-style physical line continuations:

- `\\\n`
- `\\\r\n`

These are removed only when encountered in the skip phase.

If a backslash-newline sequence appears while lexing an identifier, the current identifier stops before the backslash, and the continuation is then removed by the next call's skip phase.

## 6. Tokenization Rules

### 6.1 Simple Punctuation

Single-byte tokens:
- `(` => `TOKEN_LPAREN`
- `)` => `TOKEN_RPAREN`
- `;` => `TOKEN_SEMICOLON`

Their `text` field is the one-byte source slice.

### 6.2 Quoted Strings

A `"` starts a `TOKEN_STRING`.

Behavior:
- The opening quote is included in `text`.
- The lexer advances until it finds an unescaped closing `"`.
- A backslash causes the lexer to consume the backslash and at most one following byte as literal token content.
- The lexer does not interpret escape sequences; it only groups bytes.

Current implementation allows embedded newlines inside the token if they appear before a closing `"`.

If EOF is reached before a closing quote:
- the token kind is downgraded to `TOKEN_INVALID`,
- the token text still covers everything consumed from the opening quote to EOF.

### 6.3 Bracket Arguments / Raw Strings

A bracket opener at token position starts `TOKEN_RAW_STRING`.

Supported opener forms:
- `[[`
- `[=[`
- `[==[`
- and any `[=*[` form

Behavior:
- The full token text includes the opening and closing delimiters.
- The closer must match the opener's `=` count exactly.
- Internal `]` bytes or mismatched closer shapes are treated as content.
- The token may span multiple lines.

If EOF is reached before a matching closer:
- the token kind becomes `TOKEN_INVALID`,
- the token text still covers the opener plus all remaining source bytes.

### 6.4 Variables

A `$` starts `TOKEN_VAR` when one of these prefixes matches:

- `${`
- `$NAME{`, where `NAME` is one or more `[A-Za-z0-9_]`

Examples accepted by the implementation:
- `${VAR}`
- `$ENV{HOME}`
- `$CACHE{X}`

Behavior:
- The full token text includes the `$` prefix and the closing `}`.
- The lexer tracks nested braces using a simple depth counter.
- `{` increments depth.
- `}` decrements depth.
- A backslash consumes itself and at most one following byte without applying brace depth logic to that escaped byte.

This allows nested and escaped forms such as:
- `${${NESTED}}`
- `${A\}B}`
- `${A\{B\}}`

If EOF is reached before brace depth returns to zero:
- the token kind becomes `TOKEN_INVALID`,
- the token text still covers the consumed bytes.

### 6.5 Generator Expressions

A `$<` starts `TOKEN_GEN_EXP`.

Behavior:
- The full token text includes `$<` and the closing `>`.
- The lexer tracks nesting with a simple angle-bracket depth counter.
- Each `<` increments depth.
- Each `>` decrements depth.

This permits nested forms such as `$<A<B>>`.

The lexer does not recognize quotes, escapes, or other sub-grammars inside a generator expression. Only raw `<` and `>` affect nesting.

If EOF is reached before the depth returns to zero:
- the token kind becomes `TOKEN_INVALID`,
- the token text still covers the consumed bytes.

### 6.6 Identifiers / Unquoted Segments

Any non-whitespace byte that does not start one of the token classes above falls back to `TOKEN_IDENTIFIER`.

Identifier scanning continues until one of these boundaries:
- whitespace,
- `(`,
- `)`,
- `"`,
- `#`,
- `;`,
- start of `$<...>` or `$...{...}`.

Additional rules:
- A backslash inside an identifier consumes itself and at most one following byte as part of the identifier.
- This means escaped delimiters remain in the token text, for example `abc\ def`.
- If the backslash starts a line continuation (`\\\n` or `\\\r\n`), the identifier stops before the backslash.

Concatenated token example:
- `lib${VAR}.a` lexes as `IDENTIFIER("lib")`, `VAR("${VAR}")`, `IDENTIFIER(".a")`.

### 6.7 Bare Dollar Recovery

If the current byte is `$` but it is neither:
- a generator expression (`$<...>`),
- nor a recognized variable prefix (`${...}` or `$NAME{...}`),

the lexer emits `TOKEN_IDENTIFIER` for the single `$` byte.

This is an intentional resilience rule so foreign embedded syntax does not immediately break parsing.

## 7. Position Tracking Contract

Token position is captured after the skip phase and before the token body is consumed.

Implications:
- Leading whitespace/comments affect `has_space_left`, not the token text.
- Adjacent concatenated tokens (for example `a${B}${C}d`) all have `has_space_left == false` after the first token.
- A token immediately following a skipped comment or line continuation has `has_space_left == true`.

`col` is computed as:

```c
cursor - bol + 1
```

so it is byte-based, not Unicode code-point based.

## 8. Error Model

The lexer is permissive and streaming:

- Malformed lexemes become `TOKEN_INVALID` instead of hard failure.
- The lexer still advances past the malformed region and can continue producing later tokens.
- Unterminated comments are silently skipped instead of emitted as invalid tokens.

This design supports parser-level recovery and diagnostic emission without requiring the lexer to abort.

## 9. Current Divergences / Non-Goals

Current intentional divergences from stricter CMake-style lexical validation:

- `TOKEN_IDENTIFIER` accepts many byte patterns that are not valid command names.
- Bare `$` is accepted as an identifier token.
- Unterminated block comments do not produce `TOKEN_INVALID`.
- String, variable, and generator-expression bodies are grouped structurally, but escapes are not interpreted.
- Generator expression lexing is purely bracket-depth based and does not model CMake's full genex grammar.

## 10. Example

Input:

```cmake
set(X "a b" [=[raw;txt]=] ${VAR} $ENV{HOME} $<CONFIG:Debug>)
```

Output token sequence:

1. `IDENTIFIER` with text `set`
2. `LPAREN` with text `(`
3. `IDENTIFIER` with text `X`
4. `STRING` with text `"a b"`
5. `RAW_STRING` with text `[=[raw;txt]=]`
6. `VAR` with text `${VAR}`
7. `VAR` with text `$ENV{HOME}`
8. `GEN_EXP` with text `$<CONFIG:Debug>`
9. `RPAREN` with text `)`
