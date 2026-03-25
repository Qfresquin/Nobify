#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "test_v2_suite.h"

int main(void) {
    int passed = 0;
    int failed = 0;

    if (!test_v2_require_official_runner()) return 1;

    run_codegen_v2_tests(&passed, &failed);

    nob_log(NOB_INFO, "codegen v2 tests: passed=%d failed=%d", passed, failed);
    return failed == 0 ? 0 : 1;
}
