#ifndef TEST_V2_SUITE_H_
#define TEST_V2_SUITE_H_

#include <stdlib.h>
#include <string.h>

#include "test_workspace.h"

typedef void (*Test_Suite_Fn)(int *passed, int *failed);

void run_arena_v2_tests(int *passed, int *failed);
void run_lexer_v2_tests(int *passed, int *failed);
void run_parser_v2_tests(int *passed, int *failed);
void run_evaluator_v2_tests(int *passed, int *failed);
void run_pipeline_v2_tests(int *passed, int *failed);
void run_codegen_v2_tests(int *passed, int *failed);

static inline int test_v2_require_official_runner(void) {
    const char *runner = getenv(CMK2NOB_TEST_RUNNER_ENV);
    if (!runner || strcmp(runner, "1") != 0) {
        nob_log(NOB_ERROR, "test suites must be launched via ./build/nob_test");
        return 0;
    }
    return 1;
}

#endif // TEST_V2_SUITE_H_
