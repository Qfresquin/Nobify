#include "lexer.h"
#include <ctype.h>

const char* token_kind_name(Token_Kind kind) {
    switch (kind) {
        case TOKEN_END:        return "END";
        case TOKEN_INVALID:    return "INVALID";
        case TOKEN_LPAREN:     return "LPAREN";
        case TOKEN_RPAREN:     return "RPAREN";
        case TOKEN_SEMICOLON:  return "SEMICOLON";
        case TOKEN_STRING:     return "STRING";
        case TOKEN_RAW_STRING: return "RAW_STRING";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_VAR:        return "VAR";
        case TOKEN_GEN_EXP:    return "GEN_EXP";
        default:               return "UNKNOWN";
    }
}

Lexer lexer_init(String_View content) {
    return (Lexer){
        .content = content,
        .line = 1,
        .bol = 0,
        .cursor = 0,
    };
}

// --- Navigation Helpers ---

static char peek(Lexer *l, size_t offset) {
    if (l->cursor + offset >= l->content.count) return 0;
    return l->content.data[l->cursor + offset];
}

static char current(Lexer *l) {
    return peek(l, 0);
}

static void advance(Lexer *l) {
    if (l->cursor < l->content.count) {
        // `bol` stores the index of the current line start for cheap column tracking.
        if (current(l) == '\n') {
            l->line++;
            l->bol = l->cursor + 1;
        }
        l->cursor++;
    }
}

static void advance_n(Lexer *l, size_t n) {
    for (size_t i = 0; i < n; ++i) advance(l);
}

// --- Bracket Argument Logic (Raw Strings and Block Comments) ---

static size_t get_bracket_open_len(Lexer *l) {
    if (current(l) != '[') return 0;

    // In CMake, `[[`, `[=[`, `[==[` and similar forms share the same rule:
    // the number of `=` in the closer must match the opener.
    size_t k = 1;
    while (l->cursor + k < l->content.count && peek(l, k) == '=') {
        k++;
    }

    if (l->cursor + k < l->content.count && peek(l, k) == '[') {
        return k + 1;
    }
    return 0;
}

static void skip_bracket_content(Lexer *l, size_t open_len) {
    size_t eq_count = open_len - 2;

    // This helper is reused for both raw strings and `#[[ ... ]]` comments,
    // while still preserving line/column tracking through `advance()`.
    while (l->cursor < l->content.count) {
        if (current(l) == ']') {
            bool match = true;
            for (size_t k = 1; k <= eq_count; ++k) {
                if (peek(l, k) != '=') { match = false; break; }
            }
            if (match && peek(l, eq_count + 1) == ']') {
                advance_n(l, open_len);
                return;
            }
        }
        advance(l);
    }
}

// --- Variable Detection Helpers ---

static size_t check_var_start(Lexer *l) {
    if (current(l) != '$') return 0;
    if (peek(l, 1) == '{') return 2;
    if (peek(l, 1) == '<') return 0;

    // `$<...>` is a generator expression; this helper only recognizes
    // variable-style prefixes such as `${VAR}` and `$ENV{VAR}`.
    size_t k = 1;
    while (isalnum((unsigned char)peek(l, k)) || peek(l, k) == '_') {
        k++;
    }
    if (peek(l, k) == '{') return k + 1;

    return 0;
}

// --- Main Lexing Logic ---

Token lexer_next(Lexer *l) {
    size_t start_cursor = l->cursor;

    // Normalize the cursor to the start of the next visible token.
    // Anything consumed here makes `has_space_left = true`.
    while (l->cursor < l->content.count) {
        char c = current(l);

        if (isspace((unsigned char)c)) {
            advance(l);
        }
        else if (c == '#') {
            advance(l);
            size_t open_len = get_bracket_open_len(l);
            if (open_len > 0) {
                // After `#`, a bracket opener starts a block comment.
                advance_n(l, open_len);
                skip_bracket_content(l, open_len);
            } else {
                while (l->cursor < l->content.count && current(l) != '\n') {
                    advance(l);
                }
            }
        }
        else if (c == '\\') {
            char next = peek(l, 1);
            if (next == '\n') {
                advance_n(l, 2);
            } else if (next == '\r' && peek(l, 2) == '\n') {
                advance_n(l, 3);
            } else {
                break; // Literal backslash, stop skipping trivia
            }
        }
        else {
            break;
        }
    }

    bool has_space = (l->cursor > start_cursor);

    Token token = {0};
    token.line = l->line;
    token.col = l->cursor - l->bol + 1;
    token.has_space_left = has_space;

    if (l->cursor >= l->content.count) {
        token.kind = TOKEN_END;
        return token;
    }

    const char *start_ptr = &l->content.data[l->cursor];
    char c = current(l);

    // --- Single-Character Tokens ---
    if (c == '(') {
        token.kind = TOKEN_LPAREN;
        token.text = sv_from_parts(start_ptr, 1);
        advance(l);
        return token;
    }
    if (c == ')') {
        token.kind = TOKEN_RPAREN;
        token.text = sv_from_parts(start_ptr, 1);
        advance(l);
        return token;
    }
    if (c == ';') {
        token.kind = TOKEN_SEMICOLON;
        token.text = sv_from_parts(start_ptr, 1);
        advance(l);
        return token;
    }

    // --- Quoted Strings ---
    if (c == '"') {
        token.kind = TOKEN_STRING;
        advance(l);
        size_t len = 1;
        bool closed = false;

        // Keep the raw lexeme, including quotes and escapes.
        while (l->cursor < l->content.count) {
            char cc = current(l);
            if (cc == '"') {
                advance(l); len++;
                closed = true;
                break;
            }
            if (cc == '\\') {
                advance(l); len++;
                if (l->cursor < l->content.count) {
                    advance(l); len++;
                }
            } else {
                advance(l); len++;
            }
        }
        if (!closed) {
            token.kind = TOKEN_INVALID;
        }
        token.text = sv_from_parts(start_ptr, len);
        return token;
    }

    // --- Raw Strings ---
    size_t bracket_len = get_bracket_open_len(l);
    if (bracket_len > 0) {
        token.kind = TOKEN_RAW_STRING;
        advance_n(l, bracket_len);

        size_t content_start = l->cursor;
        skip_bracket_content(l, bracket_len);
        size_t total_len = bracket_len + (l->cursor - content_start);
        if (l->cursor >= l->content.count) {
            bool has_closer = false;
            // If we reached EOF, only accept the token as valid when the
            // suffix still matches the expected closing delimiter.
            if (l->content.count >= bracket_len) {
                size_t closer_start = l->content.count - bracket_len;
                has_closer = (l->content.data[closer_start] == ']');
                for (size_t k = 1; has_closer && k < bracket_len - 1; ++k) {
                    if (l->content.data[closer_start + k] != '=') {
                        has_closer = false;
                    }
                }
                if (has_closer && l->content.data[l->content.count - 1] != ']') {
                    has_closer = false;
                }
            }
            if (!has_closer) {
                token.kind = TOKEN_INVALID;
            }
        }
        
        token.text = sv_from_parts(start_ptr, total_len);
        return token;
    }

    // --- Variables and Generator Expressions ---
    if (c == '$') {
        if (peek(l, 1) == '<') {
            token.kind = TOKEN_GEN_EXP;
            advance_n(l, 2);
            size_t len = 2;
            int depth = 1;
            // Generator expressions can nest `<` and `>` in some forms.
            while (l->cursor < l->content.count && depth > 0) {
                char cc = current(l);
                if (cc == '<') depth++;
                if (cc == '>') depth--;
                advance(l); len++;
            }
            if (depth != 0) {
                token.kind = TOKEN_INVALID;
            }
            token.text = sv_from_parts(start_ptr, len);
            return token;
        }

        size_t var_prefix = check_var_start(l);
        if (var_prefix > 0) {
            token.kind = TOKEN_VAR;
            advance_n(l, var_prefix);
            size_t len = var_prefix;

            int depth = 1;
            // Track brace depth so nested forms like `${outer_${inner}}`
            // remain a single token.
            while (l->cursor < l->content.count && depth > 0) {
                char cc = current(l);
                if (cc == '{') depth++;
                if (cc == '}') depth--;
                if (cc == '\\') {
                    advance(l); len++;
                    if (l->cursor < l->content.count) {
                        advance(l); len++;
                    }
                    continue;
                }
                advance(l); len++;
            }
            if (depth != 0) {
                token.kind = TOKEN_INVALID;
            }
            token.text = sv_from_parts(start_ptr, len);
            return token;
        }

        // Real-world CMake files sometimes embed foreign syntax
        // (for example make snippets). Treat a bare `$` as an identifier
        // to keep the lexer resilient to that noise.
        token.kind = TOKEN_IDENTIFIER;
        token.text = sv_from_parts(start_ptr, 1);
        advance(l);
        return token;
    }

    // --- Identifiers / Arguments ---
    if (!isspace((unsigned char)c)) {
        token.kind = TOKEN_IDENTIFIER;
        size_t len = 0;

        while (l->cursor < l->content.count) {
            char cc = current(l);

            if (isspace((unsigned char)cc) || cc == '(' || cc == ')' || cc == '"' || cc == '#' || cc == ';') {
                break;
            }

            if (cc == '$') {
                // `$` starts a new token only when it actually opens a variable
                // or generator expression; otherwise it stays in the literal.
                if (peek(l, 1) == '<' || check_var_start(l) > 0) {
                    break;
                }
            }

            // A line continuation splits tokens; a regular backslash stays inside
            // the same argument.
            if (cc == '\\') {
                char next = peek(l, 1);
                if (next == '\n' || (next == '\r' && peek(l, 2) == '\n')) {
                    break;
                }

                advance(l); len++;
                if (l->cursor < l->content.count) { advance(l); len++; }
                continue;
            }

            advance(l);
            len++;
        }

        token.text = sv_from_parts(start_ptr, len);
        return token;
    }

    token.kind = TOKEN_INVALID;
    advance(l);
    return token;
}
