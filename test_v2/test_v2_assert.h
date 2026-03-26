#ifndef TEST_V2_ASSERT_H_
#define TEST_V2_ASSERT_H_

#include "nob.h"
#include "test_workspace.h"

#define TEST(name) \
    static void test_impl_##name(int *passed, int *failed); \
    static void test_##name(int *passed, int *failed) { \
        Test_Case_Workspace _test_ws_case = {0}; \
        if (!test_ws_case_enter(&_test_ws_case, #name)) { \
            nob_log(NOB_ERROR, "FAILED: %s: could not enter isolated test workspace", __func__); \
            (*failed)++; \
            return; \
        } \
        test_impl_##name(passed, failed); \
        if (!test_ws_case_leave(&_test_ws_case)) { \
            nob_log(NOB_ERROR, "FAILED: %s: could not cleanup isolated test workspace", __func__); \
            (*failed)++; \
        } \
    } \
    static void test_impl_##name(int *passed, int *failed)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        nob_log(NOB_ERROR, "FAILED: %s:%d: %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while (0)

#define TEST_PASS() do { (*passed)++; } while (0)

#endif // TEST_V2_ASSERT_H_
