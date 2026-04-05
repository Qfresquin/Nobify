#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "test_v2_suite.h"

int main(void) {
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    if (!test_v2_require_official_runner()) return 1;

    run_artifact_parity_corpus_v2_tests(&passed, &failed, &skipped);

    nob_log(NOB_INFO,
            "artifact parity corpus tests: passed=%d failed=%d skipped=%d",
            passed,
            failed,
            skipped);
    return failed == 0 ? 0 : 1;
}
