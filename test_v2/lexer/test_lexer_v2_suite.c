#include "test_v2_assert.h"
#include "test_v2_suite.h"

#include "arena.h"
#include "arena_dyn.h"
#include "lexer.h"

#include <string.h>

/*
v1->v2 migration matrix
- empty_input -> lexer_empty_input [ported]
- simple_parens -> lexer_simple_parens [ported]
- identifier -> lexer_identifier [ported]
- string_token -> lexer_string_token [ported]
- string_with_escapes -> lexer_string_with_escapes [ported]
- variable_simple -> lexer_variable_simple [ported]
- variable_env -> lexer_variable_env [ported]
- generator_expression -> lexer_generator_expression [ported]
- raw_string -> lexer_raw_string [ported]
- raw_string_with_equals -> lexer_raw_string_with_equals [ported]
- line_comment -> lexer_line_comment [ported]
- block_comment -> lexer_block_comment [ported]
- semicolon -> lexer_semicolon [ported]
- multiple_tokens -> lexer_multiple_tokens [ported]
- whitespace_handling -> lexer_whitespace_handling [ported]
- concatenated_args -> lexer_concatenated_args [ported]
- line_continuation -> lexer_line_continuation [ported]
- nested_variables -> lexer_nested_variables [ported]
- unclosed_string_is_invalid -> lexer_unclosed_string_is_invalid [ported]
- unclosed_variable_is_invalid -> lexer_unclosed_variable_is_invalid [ported]
- unclosed_genexp_is_invalid -> lexer_unclosed_genexp_is_invalid [ported]
- unclosed_raw_string_is_invalid -> lexer_unclosed_raw_string_is_invalid [ported]
- single_dollar_is_identifier -> lexer_single_dollar_is_identifier [ported]
- line_and_col_tracking_lf -> lexer_line_and_col_tracking_lf [ported]
- crlf_newlines_are_handled -> lexer_crlf_newlines_are_handled [ported]
- block_comment_with_equals -> lexer_block_comment_with_equals [ported]
- raw_string_with_internal_brackets -> lexer_raw_string_with_internal_brackets [ported]
- raw_string_mismatched_equals_does_not_close_early -> lexer_raw_string_mismatched_equals_does_not_close_early [ported]
- genexp_nested_angle_brackets -> lexer_genexp_nested_angle_brackets [ported]
- var_with_escaped_braces -> lexer_var_with_escaped_braces [ported]
- identifier_with_escaped_delimiters -> lexer_identifier_with_escaped_delimiters [ported]
- identifier_stops_before_comment -> lexer_identifier_stops_before_comment [ported]
- concatenated_multiple_vars_has_space_left_false -> lexer_concatenated_multiple_vars_has_space_left_false [ported]
*/

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = token;
    return true;
}

static bool lex_script_local(Arena *arena, const char *script, Token_List *out) {
    if (!arena || !script || !out) return false;
    *out = (Token_List){0};

    Lexer lx = lexer_init(nob_sv_from_cstr(script));
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, out, t)) return false;
    }
    return true;
}

static bool assert_token_eq(Token t,
                            Token_Kind kind,
                            const char *text_or_null,
                            int has_space_left_or_minus1,
                            size_t line_or_0,
                            size_t col_or_0) {
    if (t.kind != kind) return false;
    if (text_or_null && !nob_sv_eq(t.text, nob_sv_from_cstr(text_or_null))) return false;
    if (has_space_left_or_minus1 >= 0 && ((t.has_space_left ? 1 : 0) != has_space_left_or_minus1)) return false;
    if (line_or_0 > 0 && t.line != line_or_0) return false;
    if (col_or_0 > 0 && t.col != col_or_0) return false;
    return true;
}

static bool load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    if (!arena || !path || !out) return false;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path, &sb)) return false;

    char *text = arena_strndup(arena, sb.items, sb.count);
    size_t len = sb.count;
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static String_View normalize_newlines_to_arena(Arena *arena, String_View in) {
    if (!arena) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(arena, in.count + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t out_count = 0;
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (c == '\r') continue;
        buf[out_count++] = c;
    }

    buf[out_count] = '\0';
    return nob_sv_from_parts(buf, out_count);
}

static void snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv) {
    nob_sb_append_cstr(sb, "'");
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '\\') {
            nob_sb_append_cstr(sb, "\\\\");
        } else if (c == '\n') {
            nob_sb_append_cstr(sb, "\\n");
        } else if (c == '\r') {
            nob_sb_append_cstr(sb, "\\r");
        } else if (c == '\t') {
            nob_sb_append_cstr(sb, "\\t");
        } else if (c == '\'') {
            nob_sb_append_cstr(sb, "\\'");
        } else {
            nob_sb_append(sb, c);
        }
    }
    nob_sb_append_cstr(sb, "'");
}

static bool render_lexer_snapshot_to_arena(Arena *arena, Token_List tokens, String_View *out) {
    if (!arena || !out) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, nob_temp_sprintf("TOKENS count=%zu\n", tokens.count));
    for (size_t i = 0; i < tokens.count; i++) {
        Token tok = tokens.items[i];
        nob_sb_append_cstr(
            &sb,
            nob_temp_sprintf(
                "TOK[%zu] kind=%s line=%zu col=%zu space=%d text=",
                i,
                token_kind_name(tok.kind),
                tok.line,
                tok.col,
                tok.has_space_left ? 1 : 0));
        snapshot_append_escaped_sv(&sb, tok.text);
        nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_lexer_golden(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(512 * 1024);
    if (!arena) return false;

    String_View script = {0};
    String_View expected = {0};
    String_View actual = {0};
    bool ok = true;

    if (!load_text_file_to_arena(arena, input_path, &script)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }
    if (!load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        ok = false;
        goto done;
    }

    Token_List tokens = {0};
    if (!lex_script_local(arena, script.data, &tokens)) {
        nob_log(NOB_ERROR, "golden: failed to lex input: %s", input_path);
        ok = false;
        goto done;
    }
    if (!render_lexer_snapshot_to_arena(arena, tokens, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render snapshot");
        ok = false;
        goto done;
    }

    String_View expected_norm = normalize_newlines_to_arena(arena, expected);
    String_View actual_norm = normalize_newlines_to_arena(arena, actual);
    if (!nob_sv_eq(expected_norm, actual_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", expected_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

done:
    arena_destroy(arena);
    return ok;
}

static const char *LEXER_GOLDEN_DIR = "test_v2/lexer/golden";

TEST(lexer_empty_input) {
    Lexer l = lexer_init(nob_sv_from_cstr(""));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_END);
    TEST_PASS();
}

TEST(lexer_simple_parens) {
    Lexer l = lexer_init(nob_sv_from_cstr("()"));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_LPAREN, "(", -1, 0, 0));

    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_RPAREN, ")", -1, 0, 0));

    Token t3 = lexer_next(&l);
    ASSERT(t3.kind == TOKEN_END);
    TEST_PASS();
}

TEST(lexer_identifier) {
    Lexer l = lexer_init(nob_sv_from_cstr("add_executable"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_IDENTIFIER, "add_executable", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_string_token) {
    Lexer l = lexer_init(nob_sv_from_cstr("\"hello world\""));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_STRING, "\"hello world\"", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_string_with_escapes) {
    Lexer l = lexer_init(nob_sv_from_cstr("\"hello\\nworld\""));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_STRING, "\"hello\\nworld\"", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_variable_simple) {
    Lexer l = lexer_init(nob_sv_from_cstr("${VAR}"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_VAR, "${VAR}", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_variable_env) {
    Lexer l = lexer_init(nob_sv_from_cstr("$ENV{PATH}"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_VAR, "$ENV{PATH}", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_generator_expression) {
    Lexer l = lexer_init(nob_sv_from_cstr("$<TARGET_FILE:app>"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_GEN_EXP, "$<TARGET_FILE:app>", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_raw_string) {
    Lexer l = lexer_init(nob_sv_from_cstr("[[raw content]]"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_RAW_STRING, "[[raw content]]", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_raw_string_with_equals) {
    Lexer l = lexer_init(nob_sv_from_cstr("[=[raw content]=]"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_RAW_STRING, "[=[raw content]=]", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_line_comment) {
    Lexer l = lexer_init(nob_sv_from_cstr("# this is a comment\nadd_executable"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_IDENTIFIER, "add_executable", 1, 0, 0));
    TEST_PASS();
}

TEST(lexer_block_comment) {
    Lexer l = lexer_init(nob_sv_from_cstr("#[[ this is a \nblock comment ]]add_executable"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_IDENTIFIER, "add_executable", 1, 0, 0));
    TEST_PASS();
}

TEST(lexer_semicolon) {
    Lexer l = lexer_init(nob_sv_from_cstr(";"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_SEMICOLON, ";", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_multiple_tokens) {
    Lexer l = lexer_init(nob_sv_from_cstr("set(VAR \"value\")"));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_IDENTIFIER, "set", -1, 0, 0));
    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_LPAREN, "(", -1, 0, 0));
    Token t3 = lexer_next(&l);
    ASSERT(assert_token_eq(t3, TOKEN_IDENTIFIER, "VAR", -1, 0, 0));
    Token t4 = lexer_next(&l);
    ASSERT(assert_token_eq(t4, TOKEN_STRING, "\"value\"", -1, 0, 0));
    Token t5 = lexer_next(&l);
    ASSERT(assert_token_eq(t5, TOKEN_RPAREN, ")", -1, 0, 0));
    Token t6 = lexer_next(&l);
    ASSERT(t6.kind == TOKEN_END);

    TEST_PASS();
}

TEST(lexer_whitespace_handling) {
    Lexer l = lexer_init(nob_sv_from_cstr("  set  (  VAR  )  "));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_IDENTIFIER, "set", 1, 0, 0));
    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_LPAREN, "(", 1, 0, 0));
    Token t3 = lexer_next(&l);
    ASSERT(assert_token_eq(t3, TOKEN_IDENTIFIER, "VAR", 1, 0, 0));

    TEST_PASS();
}

TEST(lexer_concatenated_args) {
    Lexer l = lexer_init(nob_sv_from_cstr("lib${VAR}.a"));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_IDENTIFIER, "lib", -1, 0, 0));
    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_VAR, "${VAR}", 0, 0, 0));
    Token t3 = lexer_next(&l);
    ASSERT(assert_token_eq(t3, TOKEN_IDENTIFIER, ".a", 0, 0, 0));

    TEST_PASS();
}

TEST(lexer_line_continuation) {
    Lexer l = lexer_init(nob_sv_from_cstr("set\\\n(VAR value)"));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_IDENTIFIER, "set", -1, 0, 0));
    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_LPAREN, "(", 1, 0, 0));

    TEST_PASS();
}

TEST(lexer_nested_variables) {
    Lexer l = lexer_init(nob_sv_from_cstr("${${NESTED}}"));
    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_VAR, "${${NESTED}}", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_unclosed_string_is_invalid) {
    Lexer l = lexer_init(nob_sv_from_cstr("\"unterminated"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_INVALID);
    TEST_PASS();
}

TEST(lexer_unclosed_variable_is_invalid) {
    Lexer l = lexer_init(nob_sv_from_cstr("${VAR"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_INVALID);
    TEST_PASS();
}

TEST(lexer_unclosed_genexp_is_invalid) {
    Lexer l = lexer_init(nob_sv_from_cstr("$<TARGET_FILE:app"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_INVALID);
    TEST_PASS();
}

TEST(lexer_unclosed_raw_string_is_invalid) {
    Lexer l = lexer_init(nob_sv_from_cstr("[[raw content"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_INVALID);
    TEST_PASS();
}

TEST(lexer_single_dollar_is_identifier) {
    Lexer l = lexer_init(nob_sv_from_cstr("$"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_IDENTIFIER, "$", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_line_and_col_tracking_lf) {
    Lexer l = lexer_init(nob_sv_from_cstr("one\n  two\nthree"));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_IDENTIFIER, "one", -1, 1, 1));
    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_IDENTIFIER, "two", 1, 2, 3));
    Token t3 = lexer_next(&l);
    ASSERT(assert_token_eq(t3, TOKEN_IDENTIFIER, "three", -1, 3, 1));

    TEST_PASS();
}

TEST(lexer_crlf_newlines_are_handled) {
    Lexer l = lexer_init(nob_sv_from_cstr("a\r\nb"));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_IDENTIFIER, "a", -1, 1, 1));
    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_IDENTIFIER, "b", 1, 2, 1));

    TEST_PASS();
}

TEST(lexer_block_comment_with_equals) {
    Lexer l = lexer_init(nob_sv_from_cstr("#[=[ comment ]=]add_executable"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_IDENTIFIER, "add_executable", 1, 0, 0));
    TEST_PASS();
}

TEST(lexer_raw_string_with_internal_brackets) {
    Lexer l = lexer_init(nob_sv_from_cstr("[=[abc]def]=]"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_RAW_STRING, "[=[abc]def]=]", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_raw_string_mismatched_equals_does_not_close_early) {
    Lexer l = lexer_init(nob_sv_from_cstr("[==[a]=]b]==]"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_RAW_STRING, "[==[a]=]b]==]", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_genexp_nested_angle_brackets) {
    Lexer l = lexer_init(nob_sv_from_cstr("$<A<B>>"));
    Token t = lexer_next(&l);
    ASSERT(assert_token_eq(t, TOKEN_GEN_EXP, "$<A<B>>", -1, 0, 0));
    TEST_PASS();
}

TEST(lexer_var_with_escaped_braces) {
    Lexer l1 = lexer_init(nob_sv_from_cstr("${A\\}B}"));
    Token t1 = lexer_next(&l1);
    ASSERT(assert_token_eq(t1, TOKEN_VAR, "${A\\}B}", -1, 0, 0));

    Lexer l2 = lexer_init(nob_sv_from_cstr("${A\\{B\\}}"));
    Token t2 = lexer_next(&l2);
    ASSERT(assert_token_eq(t2, TOKEN_VAR, "${A\\{B\\}}", -1, 0, 0));
    ASSERT(lexer_next(&l2).kind == TOKEN_END);

    TEST_PASS();
}

TEST(lexer_identifier_with_escaped_delimiters) {
    Lexer l1 = lexer_init(nob_sv_from_cstr("abc\\ def"));
    Token a = lexer_next(&l1);
    ASSERT(assert_token_eq(a, TOKEN_IDENTIFIER, "abc\\ def", -1, 0, 0));
    ASSERT(lexer_next(&l1).kind == TOKEN_END);

    Lexer l2 = lexer_init(nob_sv_from_cstr("abc\\;def;"));
    Token b = lexer_next(&l2);
    ASSERT(assert_token_eq(b, TOKEN_IDENTIFIER, "abc\\;def", -1, 0, 0));
    Token semi = lexer_next(&l2);
    ASSERT(assert_token_eq(semi, TOKEN_SEMICOLON, ";", -1, 0, 0));

    TEST_PASS();
}

TEST(lexer_identifier_stops_before_comment) {
    Lexer l = lexer_init(nob_sv_from_cstr("a#b\nc"));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_IDENTIFIER, "a", -1, 0, 0));
    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_IDENTIFIER, "c", 1, 0, 0));

    TEST_PASS();
}

TEST(lexer_concatenated_multiple_vars_has_space_left_false) {
    Lexer l = lexer_init(nob_sv_from_cstr("a${B}${C}d"));

    Token t1 = lexer_next(&l);
    ASSERT(assert_token_eq(t1, TOKEN_IDENTIFIER, "a", -1, 0, 0));
    Token t2 = lexer_next(&l);
    ASSERT(assert_token_eq(t2, TOKEN_VAR, "${B}", 0, 0, 0));
    Token t3 = lexer_next(&l);
    ASSERT(assert_token_eq(t3, TOKEN_VAR, "${C}", 0, 0, 0));
    Token t4 = lexer_next(&l);
    ASSERT(assert_token_eq(t4, TOKEN_IDENTIFIER, "d", 0, 0, 0));

    TEST_PASS();
}

TEST(lexer_golden_basic) {
    ASSERT(assert_lexer_golden(
        nob_temp_sprintf("%s/basic.cmake", LEXER_GOLDEN_DIR),
        nob_temp_sprintf("%s/basic.txt", LEXER_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(lexer_golden_strings_raw_var_genexp) {
    ASSERT(assert_lexer_golden(
        nob_temp_sprintf("%s/strings_raw_var_genexp.cmake", LEXER_GOLDEN_DIR),
        nob_temp_sprintf("%s/strings_raw_var_genexp.txt", LEXER_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(lexer_golden_invalid_tokens) {
    ASSERT(assert_lexer_golden(
        nob_temp_sprintf("%s/invalid_tokens.cmake", LEXER_GOLDEN_DIR),
        nob_temp_sprintf("%s/invalid_tokens.txt", LEXER_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(lexer_golden_comments_and_whitespace) {
    ASSERT(assert_lexer_golden(
        nob_temp_sprintf("%s/comments_and_whitespace.cmake", LEXER_GOLDEN_DIR),
        nob_temp_sprintf("%s/comments_and_whitespace.txt", LEXER_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(lexer_golden_concatenation_and_vars) {
    ASSERT(assert_lexer_golden(
        nob_temp_sprintf("%s/concatenation_and_vars.cmake", LEXER_GOLDEN_DIR),
        nob_temp_sprintf("%s/concatenation_and_vars.txt", LEXER_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(lexer_golden_linecol_and_newlines) {
    ASSERT(assert_lexer_golden(
        nob_temp_sprintf("%s/linecol_and_newlines.cmake", LEXER_GOLDEN_DIR),
        nob_temp_sprintf("%s/linecol_and_newlines.txt", LEXER_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(lexer_golden_nested_and_balancing) {
    ASSERT(assert_lexer_golden(
        nob_temp_sprintf("%s/nested_and_balancing.cmake", LEXER_GOLDEN_DIR),
        nob_temp_sprintf("%s/nested_and_balancing.txt", LEXER_GOLDEN_DIR)));
    TEST_PASS();
}

void run_lexer_v2_tests(int *passed, int *failed) {
    test_lexer_empty_input(passed, failed);
    test_lexer_simple_parens(passed, failed);
    test_lexer_identifier(passed, failed);
    test_lexer_string_token(passed, failed);
    test_lexer_string_with_escapes(passed, failed);
    test_lexer_variable_simple(passed, failed);
    test_lexer_variable_env(passed, failed);
    test_lexer_generator_expression(passed, failed);
    test_lexer_raw_string(passed, failed);
    test_lexer_raw_string_with_equals(passed, failed);
    test_lexer_line_comment(passed, failed);
    test_lexer_block_comment(passed, failed);
    test_lexer_semicolon(passed, failed);
    test_lexer_multiple_tokens(passed, failed);
    test_lexer_whitespace_handling(passed, failed);
    test_lexer_concatenated_args(passed, failed);
    test_lexer_line_continuation(passed, failed);
    test_lexer_nested_variables(passed, failed);
    test_lexer_unclosed_string_is_invalid(passed, failed);
    test_lexer_unclosed_variable_is_invalid(passed, failed);
    test_lexer_unclosed_genexp_is_invalid(passed, failed);
    test_lexer_unclosed_raw_string_is_invalid(passed, failed);
    test_lexer_single_dollar_is_identifier(passed, failed);
    test_lexer_line_and_col_tracking_lf(passed, failed);
    test_lexer_crlf_newlines_are_handled(passed, failed);
    test_lexer_block_comment_with_equals(passed, failed);
    test_lexer_raw_string_with_internal_brackets(passed, failed);
    test_lexer_raw_string_mismatched_equals_does_not_close_early(passed, failed);
    test_lexer_genexp_nested_angle_brackets(passed, failed);
    test_lexer_var_with_escaped_braces(passed, failed);
    test_lexer_identifier_with_escaped_delimiters(passed, failed);
    test_lexer_identifier_stops_before_comment(passed, failed);
    test_lexer_concatenated_multiple_vars_has_space_left_false(passed, failed);

    test_lexer_golden_basic(passed, failed);
    test_lexer_golden_strings_raw_var_genexp(passed, failed);
    test_lexer_golden_invalid_tokens(passed, failed);
    test_lexer_golden_comments_and_whitespace(passed, failed);
    test_lexer_golden_concatenation_and_vars(passed, failed);
    test_lexer_golden_linecol_and_newlines(passed, failed);
    test_lexer_golden_nested_and_balancing(passed, failed);
}
