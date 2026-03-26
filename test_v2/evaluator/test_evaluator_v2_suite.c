#include "test_v2_assert.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

void run_evaluator_v2_batch1(int *passed, int *failed, int *skipped);
void run_evaluator_v2_batch2(int *passed, int *failed, int *skipped);
void run_evaluator_v2_batch3(int *passed, int *failed, int *skipped);
void run_evaluator_v2_batch4(int *passed, int *failed, int *skipped);
void run_evaluator_v2_batch5(int *passed, int *failed, int *skipped);
void run_evaluator_v2_integration_batch1(int *passed, int *failed, int *skipped);
void run_evaluator_v2_integration_batch2(int *passed, int *failed, int *skipped);
void run_evaluator_v2_integration_batch3(int *passed, int *failed, int *skipped);
void run_evaluator_v2_integration_batch4(int *passed, int *failed, int *skipped);
void run_evaluator_v2_integration_batch5(int *passed, int *failed, int *skipped);

void run_evaluator_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "evaluator");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "evaluator suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "evaluator suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    run_evaluator_v2_batch1(passed, failed, skipped);
    run_evaluator_v2_batch2(passed, failed, skipped);
    run_evaluator_v2_batch3(passed, failed, skipped);
    run_evaluator_v2_batch4(passed, failed, skipped);
    run_evaluator_v2_batch5(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "evaluator suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}

void run_evaluator_v2_integration_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "evaluator_integration");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "evaluator integration suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "evaluator integration suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    run_evaluator_v2_integration_batch1(passed, failed, skipped);
    run_evaluator_v2_integration_batch2(passed, failed, skipped);
    run_evaluator_v2_integration_batch3(passed, failed, skipped);
    run_evaluator_v2_integration_batch4(passed, failed, skipped);
    run_evaluator_v2_integration_batch5(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "evaluator integration suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
