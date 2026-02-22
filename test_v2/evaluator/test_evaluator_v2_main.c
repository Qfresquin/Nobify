#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "test_v2_suite.h"

int main(void) {
    int passed = 0;
    int failed = 0;

    const char *golden_only = getenv("CMK2NOB_EVAL_GOLDEN_ONLY");
    if (golden_only && strcmp(golden_only, "1") == 0) {
        run_evaluator_golden_tests(&passed, &failed);
    } else {
        run_evaluator_v2_tests(&passed, &failed);
    }

    nob_log(NOB_INFO, "evaluator v2 tests: passed=%d failed=%d", passed, failed);
    return failed == 0 ? 0 : 1;
}
