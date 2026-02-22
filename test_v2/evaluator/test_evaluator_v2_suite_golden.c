#include "test_v2_assert.h"

#include "test_evaluator_v2_shared.h"

static const char *EVALUATOR_GOLDEN_DIR = "test_v2/evaluator/golden";

TEST(evaluator_golden_core_flow) {
    ASSERT(assert_evaluator_golden(
        nob_temp_sprintf("%s/core_flow.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/core_flow.txt", EVALUATOR_GOLDEN_DIR),
        "CMakeLists.txt"));
    TEST_PASS();
}

TEST(evaluator_golden_targets_props_events) {
    ASSERT(assert_evaluator_golden(
        nob_temp_sprintf("%s/targets_props_events.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/targets_props_events.txt", EVALUATOR_GOLDEN_DIR),
        "CMakeLists.txt"));
    TEST_PASS();
}

TEST(evaluator_golden_find_package_and_include) {
    ASSERT(assert_evaluator_golden(
        nob_temp_sprintf("%s/find_package_and_include.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/find_package_and_include.txt", EVALUATOR_GOLDEN_DIR),
        "CMakeLists.txt"));
    TEST_PASS();
}

TEST(evaluator_golden_file_ops_and_security) {
    ASSERT(assert_evaluator_golden(
        nob_temp_sprintf("%s/file_ops_and_security.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/file_ops_and_security.txt", EVALUATOR_GOLDEN_DIR),
        "CMakeLists.txt"));
    TEST_PASS();
}

TEST(evaluator_golden_cpack_commands) {
    ASSERT(assert_evaluator_golden(
        nob_temp_sprintf("%s/cpack_commands.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/cpack_commands.txt", EVALUATOR_GOLDEN_DIR),
        "CMakeLists.txt"));
    TEST_PASS();
}

TEST(evaluator_golden_probes_and_try_compile) {
    ASSERT(assert_evaluator_golden(
        nob_temp_sprintf("%s/probes_and_try_compile.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/probes_and_try_compile.txt", EVALUATOR_GOLDEN_DIR),
        "CMakeLists.txt"));
    TEST_PASS();
}

TEST(evaluator_golden_ctest_meta) {
    ASSERT(assert_evaluator_golden(
        nob_temp_sprintf("%s/ctest_meta.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/ctest_meta.txt", EVALUATOR_GOLDEN_DIR),
        "CMakeLists.txt"));
    TEST_PASS();
}

TEST(evaluator_golden_misc_path_and_property) {
    ASSERT(assert_evaluator_golden(
        nob_temp_sprintf("%s/misc_path_and_property.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/misc_path_and_property.txt", EVALUATOR_GOLDEN_DIR),
        "CMakeLists.txt"));
    TEST_PASS();
}

void run_evaluator_golden_tests(int *passed, int *failed) {
    test_evaluator_golden_core_flow(passed, failed);
    test_evaluator_golden_targets_props_events(passed, failed);
    test_evaluator_golden_find_package_and_include(passed, failed);
    test_evaluator_golden_file_ops_and_security(passed, failed);
    test_evaluator_golden_cpack_commands(passed, failed);
    test_evaluator_golden_probes_and_try_compile(passed, failed);
    test_evaluator_golden_ctest_meta(passed, failed);
    test_evaluator_golden_misc_path_and_property(passed, failed);
}