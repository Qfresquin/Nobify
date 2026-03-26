#ifndef TEST_V2_ASSERT_H_
#define TEST_V2_ASSERT_H_

#include "nob.h"
#include "test_workspace.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef void (*Test_V2_Cleanup_Fn)(void *ctx);

typedef struct {
    Test_V2_Cleanup_Fn fn;
    void *ctx;
} Test_V2_Cleanup_Entry;

typedef struct {
    Test_V2_Cleanup_Entry *items;
    size_t count;
    size_t capacity;
} Test_V2_Cleanup_Stack;

extern Test_V2_Cleanup_Stack g_test_v2_cleanup_stack;

static inline void test_v2_emit_failure_message(const char *func, int line, const char *message) {
    if (!func || !message) return;
    if (line > 0) {
        fprintf(stderr, "FAILED: %s:%d: %s\n", func, line, message);
    } else {
        fprintf(stderr, "FAILED: %s: %s\n", func, message);
    }
    fflush(stderr);
}

static inline void test_v2_cleanup_stack_reset(void) {
    free(g_test_v2_cleanup_stack.items);
    g_test_v2_cleanup_stack.items = NULL;
    g_test_v2_cleanup_stack.count = 0;
    g_test_v2_cleanup_stack.capacity = 0;
}

static inline Test_V2_Cleanup_Stack test_v2_cleanup_scope_enter(void) {
    Test_V2_Cleanup_Stack previous = g_test_v2_cleanup_stack;
    g_test_v2_cleanup_stack = (Test_V2_Cleanup_Stack){0};
    return previous;
}

static inline void test_v2_cleanup_scope_leave(Test_V2_Cleanup_Stack previous) {
    test_v2_cleanup_stack_reset();
    g_test_v2_cleanup_stack = previous;
}

static inline bool test_v2_cleanup_push(Test_V2_Cleanup_Fn fn, void *ctx) {
    if (!fn) return false;
    if (g_test_v2_cleanup_stack.count == g_test_v2_cleanup_stack.capacity) {
        size_t new_capacity = g_test_v2_cleanup_stack.capacity == 0 ? 8 : g_test_v2_cleanup_stack.capacity * 2;
        Test_V2_Cleanup_Entry *new_items = (Test_V2_Cleanup_Entry*)realloc(
            g_test_v2_cleanup_stack.items,
            new_capacity * sizeof(*new_items));
        if (!new_items) return false;
        g_test_v2_cleanup_stack.items = new_items;
        g_test_v2_cleanup_stack.capacity = new_capacity;
    }

    g_test_v2_cleanup_stack.items[g_test_v2_cleanup_stack.count++] = (Test_V2_Cleanup_Entry){
        .fn = fn,
        .ctx = ctx,
    };
    return true;
}

static inline void test_v2_cleanup_run_all(void) {
    while (g_test_v2_cleanup_stack.count > 0) {
        Test_V2_Cleanup_Entry entry = g_test_v2_cleanup_stack.items[--g_test_v2_cleanup_stack.count];
        if (entry.fn) entry.fn(entry.ctx);
    }
    test_v2_cleanup_stack_reset();
}

#define TEST(name) \
    static void test_impl_##name(int *passed, int *failed, int *skipped); \
    static void test_##name(int *passed, int *failed, int *skipped) { \
        Test_Case_Workspace _test_ws_case = {0}; \
        Test_V2_Cleanup_Stack _test_v2_prev_cleanup_stack = test_v2_cleanup_scope_enter(); \
        if (!test_ws_case_enter(&_test_ws_case, #name)) { \
            test_v2_emit_failure_message(__func__, 0, "could not enter isolated test workspace"); \
            nob_log(NOB_ERROR, "FAILED: %s: could not enter isolated test workspace", __func__); \
            (*failed)++; \
            test_v2_cleanup_scope_leave(_test_v2_prev_cleanup_stack); \
            return; \
        } \
        test_impl_##name(passed, failed, skipped); \
        test_v2_cleanup_run_all(); \
        if (!test_ws_case_leave(&_test_ws_case)) { \
            test_v2_emit_failure_message(__func__, 0, "could not cleanup isolated test workspace"); \
            nob_log(NOB_ERROR, "FAILED: %s: could not cleanup isolated test workspace", __func__); \
            (*failed)++; \
        } \
        test_v2_cleanup_scope_leave(_test_v2_prev_cleanup_stack); \
    } \
    static void test_impl_##name(int *passed, int *failed, int *skipped)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        test_v2_emit_failure_message(__func__, __LINE__, #cond); \
        nob_log(NOB_ERROR, "FAILED: %s:%d: %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while (0)

#define TEST_DEFER(cleanup_fn, cleanup_ctx) do { \
    if (!test_v2_cleanup_push((cleanup_fn), (cleanup_ctx))) { \
        test_v2_emit_failure_message(__func__, __LINE__, "could not register deferred cleanup"); \
        nob_log(NOB_ERROR, "FAILED: %s:%d: could not register deferred cleanup", __func__, __LINE__); \
        (*failed)++; \
        return; \
    } \
} while (0)

#define TEST_SKIP(reason) do { \
    nob_log(NOB_INFO, "SKIPPED: %s: %s", __func__, (reason)); \
    (*skipped)++; \
    return; \
} while (0)

#define TEST_PASS() do { \
    (*passed)++; \
} while (0)

#endif // TEST_V2_ASSERT_H_
