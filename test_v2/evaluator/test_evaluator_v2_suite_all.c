#include "test_v2_suite.h"

void run_transpiler_suite_core(int *passed, int *failed);
void run_transpiler_suite_targets_codegen(int *passed, int *failed);
void run_transpiler_suite_find_include(int *passed, int *failed);
void run_transpiler_suite_file_io(int *passed, int *failed);
void run_transpiler_suite_cpack(int *passed, int *failed);
void run_transpiler_suite_probes(int *passed, int *failed);
void run_transpiler_suite_ctest_meta(int *passed, int *failed);
void run_transpiler_suite_misc(int *passed, int *failed);
void run_evaluator_golden_tests(int *passed, int *failed);

void run_evaluator_v2_tests(int *passed, int *failed) {
    run_evaluator_expr_tests(passed, failed);
    run_evaluator_dispatcher_tests(passed, failed);
    run_evaluator_file_security_tests(passed, failed);

    run_transpiler_suite_core(passed, failed);
    run_transpiler_suite_targets_codegen(passed, failed);
    run_transpiler_suite_find_include(passed, failed);
    run_transpiler_suite_file_io(passed, failed);
    run_transpiler_suite_cpack(passed, failed);
    run_transpiler_suite_probes(passed, failed);
    run_transpiler_suite_ctest_meta(passed, failed);
    run_transpiler_suite_misc(passed, failed);

    run_evaluator_golden_tests(passed, failed);
}