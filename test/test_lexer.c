#include "../nob.h"
#include "../lexer.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Macros de teste
#define TEST(name) static void test_##name(int *passed, int *failed)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        nob_log(NOB_ERROR, " FAILED: %s (line %d): %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while(0)

#define TEST_PASS() do { \
    (*passed)++; \
} while(0)

// --- Testes ---

TEST(empty_input) {
    Lexer l = lexer_init(sv_from_cstr(""));
    Token t = lexer_next(&l);
    
    ASSERT(t.kind == TOKEN_END);
    TEST_PASS();
}

TEST(simple_parens) {
    Lexer l = lexer_init(sv_from_cstr("()"));
    
    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_LPAREN);
    
    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_RPAREN);
    
    Token t3 = lexer_next(&l);
    ASSERT(t3.kind == TOKEN_END);
    
    TEST_PASS();
}

TEST(identifier) {
    Lexer l = lexer_init(sv_from_cstr("add_executable"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("add_executable")));
    
    TEST_PASS();
}

TEST(string_token) {
    Lexer l = lexer_init(sv_from_cstr("\"hello world\""));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_STRING);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("\"hello world\"")));
    
    TEST_PASS();
}

TEST(string_with_escapes) {
    Lexer l = lexer_init(sv_from_cstr("\"hello\\nworld\""));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_STRING);
    // Verifica se o texto capturado inclui as aspas e o escape
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("\"hello\\nworld\"")));
    
    TEST_PASS();
}

TEST(variable_simple) {
    Lexer l = lexer_init(sv_from_cstr("${VAR}"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_VAR);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("${VAR}")));
    
    TEST_PASS();
}

TEST(variable_env) {
    Lexer l = lexer_init(sv_from_cstr("$ENV{PATH}"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_VAR);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("$ENV{PATH}")));
    
    TEST_PASS();
}

TEST(generator_expression) {
    Lexer l = lexer_init(sv_from_cstr("$<TARGET_FILE:app>"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_GEN_EXP);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("$<TARGET_FILE:app>")));
    
    TEST_PASS();
}

TEST(raw_string) {
    Lexer l = lexer_init(sv_from_cstr("[[raw content]]"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_RAW_STRING);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("[[raw content]]")));
    
    TEST_PASS();
}

TEST(raw_string_with_equals) {
    Lexer l = lexer_init(sv_from_cstr("[=[raw content]=]"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_RAW_STRING);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("[=[raw content]=]")));
    
    TEST_PASS();
}

TEST(line_comment) {
    Lexer l = lexer_init(sv_from_cstr("# this is a comment\nadd_executable"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("add_executable")));
    // A flag has_space_left deve ser true porque houve um comentário antes
    ASSERT(t.has_space_left == true); 
    
    TEST_PASS();
}

TEST(block_comment) {
    Lexer l = lexer_init(sv_from_cstr("#[[ this is a \nblock comment ]]add_executable"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("add_executable")));
    ASSERT(t.has_space_left == true);
    
    TEST_PASS();
}

TEST(semicolon) {
    Lexer l = lexer_init(sv_from_cstr(";"));
    
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_SEMICOLON);
    
    TEST_PASS();
}

TEST(multiple_tokens) {
    Lexer l = lexer_init(sv_from_cstr("set(VAR \"value\")"));
    
    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("set")));
    
    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_LPAREN);
    
    Token t3 = lexer_next(&l);
    ASSERT(t3.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t3.text, sv_from_cstr("VAR")));
    
    Token t4 = lexer_next(&l);
    ASSERT(t4.kind == TOKEN_STRING);
    ASSERT(nob_sv_eq(t4.text, sv_from_cstr("\"value\"")));
    
    Token t5 = lexer_next(&l);
    ASSERT(t5.kind == TOKEN_RPAREN);
    
    Token t6 = lexer_next(&l);
    ASSERT(t6.kind == TOKEN_END);
    
    TEST_PASS();
}

TEST(whitespace_handling) {
    Lexer l = lexer_init(sv_from_cstr("  set  (  VAR  )  "));
    
    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_IDENTIFIER);
    ASSERT(t1.has_space_left == true); // Espaço antes do set
    
    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_LPAREN);
    ASSERT(t2.has_space_left == true);
    
    Token t3 = lexer_next(&l);
    ASSERT(t3.kind == TOKEN_IDENTIFIER);
    ASSERT(t3.has_space_left == true);
    
    TEST_PASS();
}

TEST(concatenated_args) {
    // Tokens sem espaço entre eles devem ter has_space_left == false
    // Ex: -I${VAR} -> -I, ${VAR}
    Lexer l = lexer_init(sv_from_cstr("lib${VAR}.a"));
    
    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("lib")));
    // Nota: A flag has_space_left refere-se ao espaço ANTES do token
    
    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_VAR);
    ASSERT(nob_sv_eq(t2.text, sv_from_cstr("${VAR}")));
    ASSERT(t2.has_space_left == false); // Grudado no "lib"
    
    Token t3 = lexer_next(&l);
    ASSERT(t3.kind == TOKEN_IDENTIFIER); // .a é lido como identificador
    ASSERT(nob_sv_eq(t3.text, sv_from_cstr(".a")));
    ASSERT(t3.has_space_left == false); // Grudado no "}"
    
    TEST_PASS();
}

TEST(line_continuation) {
    Lexer l = lexer_init(sv_from_cstr("set\\\n(VAR value)"));
    
    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("set")));
    
    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_LPAREN);
    // A continuação de linha consome a quebra de linha, então visualmente estão grudados?
    // O lexer trata \\\n como se não existisse ou como separador?
    // Na implementação atual do lexer.c: o loop de "skip" consome \\\n.
    // Como ele consome algo no loop inicial, has_space_left será true.
    ASSERT(t2.has_space_left == true); 
    
    TEST_PASS();
}

TEST(nested_variables) {
    // CMake permite aninhamento, mas o lexer simplificado pode ou não suportar
    // O lexer.c tem um contador de profundidade (depth), então deve suportar.
    Lexer l = lexer_init(sv_from_cstr("${${NESTED}}"));
    
    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_VAR);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("${${NESTED}}")));
    
    TEST_PASS();
}

TEST(unclosed_string_is_invalid) {
    Lexer l = lexer_init(sv_from_cstr("\"unterminated"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_INVALID);
    TEST_PASS();
}

TEST(unclosed_variable_is_invalid) {
    Lexer l = lexer_init(sv_from_cstr("${VAR"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_INVALID);
    TEST_PASS();
}

TEST(unclosed_genexp_is_invalid) {
    Lexer l = lexer_init(sv_from_cstr("$<TARGET_FILE:app"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_INVALID);
    TEST_PASS();
}

TEST(unclosed_raw_string_is_invalid) {
    Lexer l = lexer_init(sv_from_cstr("[[raw content"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_INVALID);
    TEST_PASS();
}

TEST(single_dollar_is_identifier) {
    Lexer l = lexer_init(sv_from_cstr("$"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("$")));
    TEST_PASS();
}


TEST(line_and_col_tracking_lf) {
    // token positions should track line/col after '\n'
    Lexer l = lexer_init(sv_from_cstr("one\n  two\nthree"));

    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("one")));
    ASSERT(t1.line == 1);
    ASSERT(t1.col  == 1);

    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t2.text, sv_from_cstr("two")));
    ASSERT(t2.line == 2);
    ASSERT(t2.col  == 3); // two spaces before "two"
    ASSERT(t2.has_space_left == true);

    Token t3 = lexer_next(&l);
    ASSERT(t3.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t3.text, sv_from_cstr("three")));
    ASSERT(t3.line == 3);
    ASSERT(t3.col  == 1);

    TEST_PASS();
}

TEST(crlf_newlines_are_handled) {
    Lexer l = lexer_init(sv_from_cstr("a\r\nb"));

    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("a")));
    ASSERT(t1.line == 1);
    ASSERT(t1.col  == 1);

    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t2.text, sv_from_cstr("b")));
    ASSERT(t2.line == 2);
    ASSERT(t2.col  == 1);
    ASSERT(t2.has_space_left == true);

    TEST_PASS();
}

TEST(block_comment_with_equals) {
    Lexer l = lexer_init(sv_from_cstr("#[=[ comment ]=]add_executable"));

    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("add_executable")));
    ASSERT(t.has_space_left == true);

    TEST_PASS();
}

TEST(raw_string_with_internal_brackets) {
    Lexer l = lexer_init(sv_from_cstr("[=[abc]def]=]"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_RAW_STRING);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("[=[abc]def]=]")));
    TEST_PASS();
}

TEST(raw_string_mismatched_equals_does_not_close_early) {
    Lexer l = lexer_init(sv_from_cstr("[==[a]=]b]==]"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_RAW_STRING);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("[==[a]=]b]==]")));
    TEST_PASS();
}

TEST(genexp_nested_angle_brackets) {
    Lexer l = lexer_init(sv_from_cstr("$<A<B>>"));
    Token t = lexer_next(&l);
    ASSERT(t.kind == TOKEN_GEN_EXP);
    ASSERT(nob_sv_eq(t.text, sv_from_cstr("$<A<B>>")));
    TEST_PASS();
}

TEST(var_with_escaped_braces) {
    Lexer l1 = lexer_init(sv_from_cstr("${A\\}B}"));
    Token t1 = lexer_next(&l1);
    ASSERT(t1.kind == TOKEN_VAR);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("${A\\}B}")));

    // Para manter um '}' literal dentro de ${...}, ele precisa ser escapado.
    // Um '}' não-escapado fecha a expressão de variável.
    Lexer l2 = lexer_init(sv_from_cstr("${A\\{B\\}}"));
    Token t2 = lexer_next(&l2);
    ASSERT(t2.kind == TOKEN_VAR);
    ASSERT(nob_sv_eq(t2.text, sv_from_cstr("${A\\{B\\}}")));
    ASSERT(lexer_next(&l2).kind == TOKEN_END);

    TEST_PASS();
}

TEST(identifier_with_escaped_delimiters) {
    // backslash escapes should keep delimiters inside the same identifier
    Lexer l1 = lexer_init(sv_from_cstr("abc\\ def"));
    Token a = lexer_next(&l1);
    ASSERT(a.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(a.text, sv_from_cstr("abc\\ def")));
    ASSERT(lexer_next(&l1).kind == TOKEN_END);

    Lexer l2 = lexer_init(sv_from_cstr("abc\\;def;"));
    Token b = lexer_next(&l2);
    ASSERT(b.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(b.text, sv_from_cstr("abc\\;def")));
    Token semi = lexer_next(&l2);
    ASSERT(semi.kind == TOKEN_SEMICOLON);

    TEST_PASS();
}

TEST(identifier_stops_before_comment) {
    Lexer l = lexer_init(sv_from_cstr("a#b\nc"));
    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("a")));

    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t2.text, sv_from_cstr("c")));
    ASSERT(t2.has_space_left == true);

    TEST_PASS();
}

TEST(concatenated_multiple_vars_has_space_left_false) {
    Lexer l = lexer_init(sv_from_cstr("a${B}${C}d"));

    Token t1 = lexer_next(&l);
    ASSERT(t1.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t1.text, sv_from_cstr("a")));

    Token t2 = lexer_next(&l);
    ASSERT(t2.kind == TOKEN_VAR);
    ASSERT(nob_sv_eq(t2.text, sv_from_cstr("${B}")));
    ASSERT(t2.has_space_left == false);

    Token t3 = lexer_next(&l);
    ASSERT(t3.kind == TOKEN_VAR);
    ASSERT(nob_sv_eq(t3.text, sv_from_cstr("${C}")));
    ASSERT(t3.has_space_left == false);

    Token t4 = lexer_next(&l);
    ASSERT(t4.kind == TOKEN_IDENTIFIER);
    ASSERT(nob_sv_eq(t4.text, sv_from_cstr("d")));
    ASSERT(t4.has_space_left == false);

    TEST_PASS();
}


void run_lexer_tests(int *passed, int *failed) {
    test_empty_input(passed, failed);
    test_simple_parens(passed, failed);
    test_identifier(passed, failed);
    test_string_token(passed, failed);
    test_string_with_escapes(passed, failed);
    test_variable_simple(passed, failed);
    test_variable_env(passed, failed);
    test_generator_expression(passed, failed);
    test_raw_string(passed, failed);
    test_raw_string_with_equals(passed, failed);
    test_line_comment(passed, failed);
    test_block_comment(passed, failed);
    test_semicolon(passed, failed);
    test_multiple_tokens(passed, failed);
    test_whitespace_handling(passed, failed);
    test_concatenated_args(passed, failed);
    test_line_continuation(passed, failed);
    test_nested_variables(passed, failed);
    test_unclosed_string_is_invalid(passed, failed);
    test_unclosed_variable_is_invalid(passed, failed);
    test_unclosed_genexp_is_invalid(passed, failed);
    test_unclosed_raw_string_is_invalid(passed, failed);
    test_single_dollar_is_identifier(passed, failed);
    test_line_and_col_tracking_lf(passed, failed);
    test_crlf_newlines_are_handled(passed, failed);
    test_block_comment_with_equals(passed, failed);
    test_raw_string_with_internal_brackets(passed, failed);
    test_raw_string_mismatched_equals_does_not_close_early(passed, failed);
    test_genexp_nested_angle_brackets(passed, failed);
    test_var_with_escaped_braces(passed, failed);
    test_identifier_with_escaped_delimiters(passed, failed);
    test_identifier_stops_before_comment(passed, failed);
    test_concatenated_multiple_vars_has_space_left_false(passed, failed);
}
