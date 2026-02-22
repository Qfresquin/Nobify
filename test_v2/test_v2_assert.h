#ifndef TEST_V2_ASSERT_H_
#define TEST_V2_ASSERT_H_

#include "nob.h"

#define TEST(name) static void test_##name(int *passed, int *failed)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        nob_log(NOB_ERROR, "FAILED: %s:%d: %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while (0)

#define TEST_PASS() do { (*passed)++; } while (0)

#endif // TEST_V2_ASSERT_H_
