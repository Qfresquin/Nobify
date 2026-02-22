#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "test_v2_suite.h"

int main(void) {
    int passed = 0;
    int failed = 0;

    run_evaluator_v2_tests(&passed, &failed);

    nob_log(NOB_INFO, "evaluator v2 tests: passed=%d failed=%d", passed, failed);
    return failed == 0 ? 0 : 1;
}