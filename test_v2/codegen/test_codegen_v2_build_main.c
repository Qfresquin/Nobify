#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "test_codegen_v2_support.h"

#include <stdio.h>

void run_codegen_v2_build_tests(int *passed, int *failed, int *skipped);

static void run_codegen_build_v2_suite(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    char repo_root[_TINYDIR_PATH_MAX] = {0};
    const char *repo_root_env = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    bool prepared = test_ws_prepare(&ws, "codegen-build");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "codegen-build suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "codegen-build suite: failed to enter isolated workspace");
        (void)test_ws_cleanup(&ws);
        if (failed) (*failed)++;
        return;
    }

    snprintf(repo_root, sizeof(repo_root), "%s", repo_root_env ? repo_root_env : "");
    codegen_test_set_repo_root(repo_root);

    run_codegen_v2_build_tests(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        nob_log(NOB_ERROR, "codegen-build suite: failed to restore cwd");
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "codegen-build suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}

int main(void) {
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    if (!test_v2_require_official_runner()) return 1;

    run_codegen_build_v2_suite(&passed, &failed, &skipped);

    nob_log(NOB_INFO, "codegen build v2 tests: passed=%d failed=%d skipped=%d", passed, failed, skipped);
    return failed == 0 ? 0 : 1;
}
