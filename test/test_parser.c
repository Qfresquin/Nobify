#include "../nob.h"
#include "../lexer.h"
#include "../parser.h"
#include "../arena.h" // <--- Necessário agora
#include "../diagnostics.h"
#include <stdio.h>

// Macros de teste adaptadas para o nob
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

// Helper para tokenizar input string em lista de tokens
// Tokens ainda usam malloc, mas a AST usará a arena passada nos testes.
static Token_List tokenize(const char *input) {
    Lexer l = lexer_init(sv_from_cstr(input));
    Token_List tokens = {0};
    
    Token t = lexer_next(&l);
    while (t.kind != TOKEN_END) {
        nob_da_append(&tokens, t);
        t = lexer_next(&l);
    }
    
    return tokens;
}

// --- Testes ---

TEST(empty_input) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 0);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(simple_command) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("set(VAR value)");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_COMMAND);
    ASSERT(nob_sv_eq(root.items[0].as.cmd.name, sv_from_cstr("set")));
    ASSERT(root.items[0].as.cmd.args.count == 2);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(multiple_commands) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("set(VAR1 val1)\nset(VAR2 val2)");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 2);
    ASSERT(root.items[0].kind == NODE_COMMAND);
    ASSERT(root.items[1].kind == NODE_COMMAND);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_statement) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("if(WIN32)\n  set(VAR value)\nendif()");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_IF);
    ASSERT(root.items[0].as.if_stmt.condition.count > 0);
    ASSERT(root.items[0].as.if_stmt.then_block.count == 1);
    ASSERT(root.items[0].as.if_stmt.else_block.count == 0);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_else_statement) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("if(WIN32)\n  set(VAR win)\nelse()\n  set(VAR unix)\nendif()");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_IF);
    ASSERT(root.items[0].as.if_stmt.then_block.count == 1);
    ASSERT(root.items[0].as.if_stmt.else_block.count == 1);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_elseif_statement) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("if(WIN32)\n  set(VAR win)\nelseif(UNIX)\n  set(VAR unix)\nendif()");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_IF);
    ASSERT(root.items[0].as.if_stmt.then_block.count == 1);
    ASSERT(root.items[0].as.if_stmt.else_block.count == 1);
    // O elseif é transformado em um novo IF no bloco else
    ASSERT(root.items[0].as.if_stmt.else_block.items[0].kind == NODE_IF);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(foreach_statement) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("foreach(item IN LISTS items)\n  message(${item})\nendforeach()");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_FOREACH);
    ASSERT(root.items[0].as.foreach_stmt.args.count > 0);
    ASSERT(root.items[0].as.foreach_stmt.body.count == 1);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(while_statement) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("while(ENABLE_LOOP)\n  message(loop)\nendwhile()");
    Ast_Root root = parse_tokens(arena, tokens);

    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_WHILE);
    ASSERT(root.items[0].as.while_stmt.condition.count > 0);
    ASSERT(root.items[0].as.while_stmt.body.count == 1);

    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(function_definition) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("function(my_func arg1 arg2)\n  set(VAR value)\nendfunction()");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_FUNCTION);
    ASSERT(root.items[0].as.func_def.params.count == 2);
    ASSERT(root.items[0].as.func_def.body.count == 1);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(function_params_exclude_name) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("function(my_func arg1 arg2)\nendfunction()");
    Ast_Root root = parse_tokens(arena, tokens);

    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_FUNCTION);
    ASSERT(nob_sv_eq(root.items[0].as.func_def.name, sv_from_cstr("my_func")));
    ASSERT(root.items[0].as.func_def.params.count == 2);
    ASSERT(root.items[0].as.func_def.params.items[0].count > 0);
    ASSERT(nob_sv_eq(root.items[0].as.func_def.params.items[0].items[0].text, sv_from_cstr("arg1")));
    ASSERT(root.items[0].as.func_def.params.items[1].count > 0);
    ASSERT(nob_sv_eq(root.items[0].as.func_def.params.items[1].items[0].text, sv_from_cstr("arg2")));

    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(invalid_token_is_ignored_as_command) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("$");
    Ast_Root root = parse_tokens(arena, tokens);

    ASSERT(root.count == 0);

    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(invalid_command_name_with_parentheses_is_ignored) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("$(VERSIONINFO)");
    Ast_Root root = parse_tokens(arena, tokens);

    ASSERT(root.count == 0);

    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(parse_tokens_requires_arena) {
    Token_List tokens = tokenize("set(VAR value)");
    diag_reset();
    Ast_Root root = parse_tokens(NULL, tokens);

    ASSERT(root.count == 0);
    ASSERT(diag_has_errors() == true);
    ASSERT(diag_error_count() > 0);

    free(tokens.items);
    TEST_PASS();
}

TEST(if_nested_parentheses_dont_escape_as_commands) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize(
        "if((A AND B) OR (C AND (D OR E)))\n"
        "  set(VAR ok)\n"
        "endif()"
    );
    Ast_Root root = parse_tokens(arena, tokens);

    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_IF);
    ASSERT(root.items[0].as.if_stmt.then_block.count == 1);
    ASSERT(root.items[0].as.if_stmt.then_block.items[0].kind == NODE_COMMAND);

    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(macro_definition) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("macro(my_macro arg1)\n  set(VAR value)\nendmacro()");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_MACRO);
    ASSERT(root.items[0].as.func_def.body.count == 1);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(nested_if) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize(
        "if(A)\n"
        "  if(B)\n"
        "    set(VAR nested)\n"
        "  endif()\n"
        "endif()"
    );
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_IF);
    ASSERT(root.items[0].as.if_stmt.then_block.count == 1);
    ASSERT(root.items[0].as.if_stmt.then_block.items[0].kind == NODE_IF);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(command_with_multiple_args) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("add_executable(app main.c util.c helper.c)");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_COMMAND);
    ASSERT(nob_sv_eq(root.items[0].as.cmd.name, sv_from_cstr("add_executable")));
    ASSERT(root.items[0].as.cmd.args.count == 4);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(command_with_concatenated_args) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("set(LIB lib${VAR}.a)");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_COMMAND);
    ASSERT(root.items[0].as.cmd.args.count == 2);
    ASSERT(root.items[0].as.cmd.args.items[1].count > 1);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(project_command) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("project(MyProject VERSION 1.0 LANGUAGES C CXX)");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_COMMAND);
    ASSERT(nob_sv_eq(root.items[0].as.cmd.name, sv_from_cstr("project")));
    ASSERT(root.items[0].as.cmd.args.count >= 5);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(comment_ignored) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("# comment\nset(VAR value)");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_COMMAND);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(empty_parentheses) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("message()");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_COMMAND);
    ASSERT(root.items[0].as.cmd.args.count == 0);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(quoted_strings) {
    Arena *arena = arena_create(1024);
    Token_List tokens = tokenize("set(VAR \"hello world\")");
    Ast_Root root = parse_tokens(arena, tokens);
    
    ASSERT(root.count == 1);
    ASSERT(root.items[0].kind == NODE_COMMAND);
    ASSERT(root.items[0].as.cmd.args.count == 2);
    
    free(tokens.items);
    arena_destroy(arena);
    TEST_PASS();
}

void run_parser_tests(int *passed, int *failed) {
    test_empty_input(passed, failed);
    test_simple_command(passed, failed);
    test_multiple_commands(passed, failed);
    test_if_statement(passed, failed);
    test_if_else_statement(passed, failed);
    test_if_elseif_statement(passed, failed);
    test_foreach_statement(passed, failed);
    test_while_statement(passed, failed);
    test_function_definition(passed, failed);
    test_function_params_exclude_name(passed, failed);
    test_macro_definition(passed, failed);
    test_nested_if(passed, failed);
    test_command_with_multiple_args(passed, failed);
    test_command_with_concatenated_args(passed, failed);
    test_project_command(passed, failed);
    test_comment_ignored(passed, failed);
    test_empty_parentheses(passed, failed);
    test_quoted_strings(passed, failed);
    test_invalid_token_is_ignored_as_command(passed, failed);
    test_invalid_command_name_with_parentheses_is_ignored(passed, failed);
    test_parse_tokens_requires_arena(passed, failed);
    test_if_nested_parentheses_dont_escape_as_commands(passed, failed);
}
