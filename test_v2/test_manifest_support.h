#ifndef TEST_MANIFEST_SUPPORT_H_
#define TEST_MANIFEST_SUPPORT_H_

#include "arena.h"
#include "nob.h"

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    TEST_MANIFEST_CAPTURE_TREE = 0,
    TEST_MANIFEST_CAPTURE_FILE_TEXT,
    TEST_MANIFEST_CAPTURE_FILE_SHA256,
} Test_Manifest_Capture_Kind;

typedef struct {
    Test_Manifest_Capture_Kind capture;
    const char *label;
    const char *relpath;
} Test_Manifest_Request;

bool test_manifest_capture_tree(Arena *arena,
                                const char *abs_path,
                                String_View *out);
bool test_manifest_capture(Arena *arena,
                           const char *base_dir,
                           const Test_Manifest_Request *requests,
                           size_t request_count,
                           String_View *out);
bool test_manifest_assert_equal(Arena *arena,
                                const char *subject,
                                const char *expected_label,
                                const char *actual_label,
                                String_View expected,
                                String_View actual);

#endif // TEST_MANIFEST_SUPPORT_H_
