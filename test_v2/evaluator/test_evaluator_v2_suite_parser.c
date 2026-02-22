#include "test_v2_assert.h"
#include "test_v2_suite.h"

#include "test_evaluator_v2_env.h"

TEST(variable_expansion_and_if_ops) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(eval_var_set(env.ctx, nob_sv_from_cstr("FOO"), nob_sv_from_cstr("abc")));
    String_View expanded = eval_expand_vars(env.ctx, nob_sv_from_cstr("pre_${FOO}_post"));
    ASSERT(nob_sv_eq(expanded, nob_sv_from_cstr("pre_abc_post")));

    const char *script =
        "set(MYLIST \"b;a;c\")\n"
        "if(a IN_LIST MYLIST)\n"
        "  set(IN_LIST_OK 1)\n"
        "endif()\n"
        "if(\"a\\\\b\" PATH_EQUAL \"a/b\")\n"
        "  set(PATH_EQ_OK 1)\n"
        "endif()";

    ASSERT(run_script(&env, script));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("IN_LIST_OK")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("PATH_EQ_OK")), nob_sv_from_cstr("1")));

    env_free(&env);
    TEST_PASS();
}

TEST(builtin_variables_present) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_VERSION")));
    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_SYSTEM_NAME")));
    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME")));
    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_C_COMPILER_ID")));
    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID")));

    env_free(&env);
    TEST_PASS();
}

TEST(parser_keeps_if_condition_parentheses_compatible_with_eval_expr) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    const char *script =
        "set(A ON)\n"
        "set(B ON)\n"
        "if((A AND B) OR C)\n"
        "  set(CONDITION_OK 1)\n"
        "endif()\n";

    ASSERT(run_script(&env, script));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("CONDITION_OK")), nob_sv_from_cstr("1")));

    env_free(&env);
    TEST_PASS();
}

void run_evaluator_expr_tests(int *passed, int *failed) {
    test_variable_expansion_and_if_ops(passed, failed);
    test_builtin_variables_present(passed, failed);
    test_parser_keeps_if_condition_parentheses_compatible_with_eval_expr(passed, failed);
}
