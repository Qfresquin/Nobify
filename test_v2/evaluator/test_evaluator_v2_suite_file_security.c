#include "test_v2_assert.h"
#include "test_v2_suite.h"

#include "test_evaluator_v2_env.h"

#include <stdlib.h>

TEST(file_read_rejects_absolute_outside_project_scope) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(!run_script(&env, "file(READ /tmp/nobify_forbidden OUT)\n"));
    ASSERT(count_diag_errors_for_command(&env, "file") >= 1);
    ASSERT(has_diag_cause_contains(&env, EV_DIAG_ERROR, "Security Violation"));

    env_free(&env);
    TEST_PASS();
}

TEST(file_strings_rejects_absolute_outside_project_scope) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(!run_script(&env, "file(STRINGS /tmp/nobify_forbidden OUT)\n"));
    ASSERT(count_diag_errors_for_command(&env, "file") >= 1);
    ASSERT(has_diag_cause_contains(&env, EV_DIAG_ERROR, "Security Violation"));

    env_free(&env);
    TEST_PASS();
}

TEST(file_read_relative_inside_project_scope_still_works) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(nob_write_entire_file("temp_read_ok.txt", "hello\n", 6));
    ASSERT(run_script(&env, "file(READ temp_read_ok.txt OUT)\n"));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("OUT")), nob_sv_from_cstr("hello\n")));

    (void)nob_delete_file("temp_read_ok.txt");
    env_free(&env);
    TEST_PASS();
}

TEST(file_copy_with_permissions_executes_without_legacy_no_effect_warning) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(nob_write_entire_file("temp_copy_perm_src.txt", "x", 1));
    ASSERT(run_script(&env, "file(COPY temp_copy_perm_src.txt DESTINATION temp_copy_perm_dst PERMISSIONS OWNER_READ OWNER_WRITE)\n"));
    ASSERT(nob_file_exists("temp_copy_perm_dst/temp_copy_perm_src.txt"));
    ASSERT(!has_diag_cause_contains(&env, EV_DIAG_WARNING, "currently have no effect"));

    (void)nob_delete_file("temp_copy_perm_src.txt");
    (void)nob_delete_file("temp_copy_perm_dst/temp_copy_perm_src.txt");
#if defined(_WIN32)
    (void)system("cmd /C if exist temp_copy_perm_dst rmdir /S /Q temp_copy_perm_dst");
#else
    (void)system("rm -rf temp_copy_perm_dst");
#endif
    env_free(&env);
    TEST_PASS();
}

void run_evaluator_file_security_tests(int *passed, int *failed) {
    test_file_read_rejects_absolute_outside_project_scope(passed, failed);
    test_file_strings_rejects_absolute_outside_project_scope(passed, failed);
    test_file_read_relative_inside_project_scope_still_works(passed, failed);
    test_file_copy_with_permissions_executes_without_legacy_no_effect_warning(passed, failed);
}
