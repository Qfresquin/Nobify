#ifndef TEST_V2_SUITE_H_
#define TEST_V2_SUITE_H_

#include <stdlib.h>

typedef void (*Test_Suite_Fn)(int *passed, int *failed);

void run_arena_v2_tests(int *passed, int *failed);
void run_lexer_v2_tests(int *passed, int *failed);
void run_parser_v2_tests(int *passed, int *failed);
void run_evaluator_v2_tests(int *passed, int *failed);
void run_pipeline_v2_tests(int *passed, int *failed);
void run_codegen_v2_tests(int *passed, int *failed);

static inline int test_v2_require_result_type_conventions(void) {
    int rc = system("bash test_v2/evaluator/check_result_type_conventions.sh");
    if (rc != 0) {
        nob_log(NOB_ERROR, "result type conventions preflight failed");
        return 0;
    }
    return 1;
}

#endif // TEST_V2_SUITE_H_
