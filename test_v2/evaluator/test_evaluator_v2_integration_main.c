#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "test_v2_suite.h"

int main(void) {
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    if (!test_v2_require_official_runner()) return 1;

    run_evaluator_v2_integration_tests(&passed, &failed, &skipped);

    nob_log(NOB_INFO,
            "evaluator v2 integration tests: passed=%d failed=%d skipped=%d",
            passed,
            failed,
            skipped);
    return failed == 0 ? 0 : 1;
}
