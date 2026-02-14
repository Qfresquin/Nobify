#include "../nob.h"
#include "../arena.h"
#include "../logic_model.h"

#include <stdio.h>
#include <string.h>

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

typedef struct {
    String_View key;
    String_View value;
} Test_Var;

typedef struct {
    const Test_Var *items;
    size_t count;
} Test_Var_Table;

static String_View test_logic_get_var(void *userdata, String_View name, bool *is_set) {
    const Test_Var_Table *table = (const Test_Var_Table*)userdata;
    if (is_set) *is_set = false;
    if (!table) return sv_from_cstr("");
    for (size_t i = 0; i < table->count; i++) {
        if (nob_sv_eq(table->items[i].key, name)) {
            if (is_set) *is_set = true;
            return table->items[i].value;
        }
    }
    return sv_from_cstr("");
}

static bool eval_tokens(Arena *arena,
                        const Test_Var_Table *vars,
                        const char **tokens_cstr,
                        const bool *quoted,
                        size_t token_count,
                        bool *ok) {
    String_View *tokens = arena_alloc_array(arena, String_View, token_count);
    if (!tokens) {
        if (ok) *ok = false;
        return false;
    }
    for (size_t i = 0; i < token_count; i++) {
        tokens[i] = sv_from_cstr(tokens_cstr[i]);
    }

    Logic_Parse_Input parse_in = {0};
    parse_in.arena = arena;
    parse_in.tokens = tokens;
    parse_in.quoted_tokens = quoted;
    parse_in.count = token_count;

    Logic_Eval_Context eval_ctx = {0};
    eval_ctx.get_var = test_logic_get_var;
    eval_ctx.userdata = (void*)vars;

    return logic_parse_and_evaluate(&parse_in, &eval_ctx, ok);
}

TEST(operator_precedence_and_parentheses) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    const char *expr1[] = {"TRUE", "OR", "FALSE", "AND", "FALSE"};
    bool quoted1[] = {false, false, false, false, false};
    bool ok = false;
    ASSERT(eval_tokens(arena, NULL, expr1, quoted1, 5, &ok) == true);
    ASSERT(ok == true);

    const char *expr2[] = {"(", "TRUE", "OR", "FALSE", ")", "AND", "FALSE"};
    bool quoted2[] = {false, false, false, false, false, false, false};
    ok = false;
    ASSERT(eval_tokens(arena, NULL, expr2, quoted2, 7, &ok) == false);
    ASSERT(ok == true);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(defined_and_variable_resolution) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    Test_Var vars_data[] = {
        { sv_from_cstr("A"), sv_from_cstr("ON") },
        { sv_from_cstr("B"), sv_from_cstr("0") },
        { sv_from_cstr("NUM"), sv_from_cstr("42") },
    };
    Test_Var_Table vars = { vars_data, 3 };

    const char *expr1[] = {"DEFINED", "A", "AND", "NOT", "DEFINED", "MISSING"};
    bool quoted1[] = {false, false, false, false, false, false};
    bool ok = false;
    ASSERT(eval_tokens(arena, &vars, expr1, quoted1, 6, &ok) == true);
    ASSERT(ok == true);

    const char *expr2[] = {"A", "AND", "B"};
    bool quoted2[] = {false, false, false};
    ok = false;
    ASSERT(eval_tokens(arena, &vars, expr2, quoted2, 3, &ok) == false);
    ASSERT(ok == true);

    const char *expr3[] = {"NUM", "GREATER_EQUAL", "40"};
    bool quoted3[] = {false, false, false};
    ok = false;
    ASSERT(eval_tokens(arena, &vars, expr3, quoted3, 3, &ok) == true);
    ASSERT(ok == true);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(comparisons_and_literals) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    const char *expr1[] = {"FOO", "STREQUAL", "FOO"};
    bool quoted1[] = {false, false, false};
    bool ok = false;
    ASSERT(eval_tokens(arena, NULL, expr1, quoted1, 3, &ok) == true);
    ASSERT(ok == true);

    const char *expr2[] = {"UNDEFINED_VAR"};
    bool quoted2[] = {false};
    ok = false;
    ASSERT(eval_tokens(arena, NULL, expr2, quoted2, 1, &ok) == false);
    ASSERT(ok == true);

    const char *expr3[] = {"1.2.0", "VERSION_LESS", "1.10.0"};
    bool quoted3[] = {false, false, false};
    ok = false;
    ASSERT(eval_tokens(arena, NULL, expr3, quoted3, 3, &ok) == true);
    ASSERT(ok == true);

    const char *expr4[] = {"NAME", "STREQUAL", "X"};
    bool quoted4[] = {false, false, true};
    ok = false;
    ASSERT(eval_tokens(arena, NULL, expr4, quoted4, 3, &ok) == false);
    ASSERT(ok == true);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(invalid_expression_reports_not_ok) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    const char *expr[] = {"(", "TRUE", "AND", "FALSE"};
    bool quoted[] = {false, false, false, false};
    bool ok = true;
    (void)eval_tokens(arena, NULL, expr, quoted, 4, &ok);
    ASSERT(ok == false);

    arena_destroy(arena);
    TEST_PASS();
}

void run_logic_model_tests(int *passed, int *failed) {
    test_operator_precedence_and_parentheses(passed, failed);
    test_defined_and_variable_resolution(passed, failed);
    test_comparisons_and_literals(passed, failed);
    test_invalid_expression_reports_not_ok(passed, failed);
}
