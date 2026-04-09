#include "test_v2_assert.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define TEST_V2_CASE_BUFFER_CAPACITY 256

Test_V2_Cleanup_Stack g_test_v2_cleanup_stack = {0};

typedef struct {
    char current_case_name[TEST_V2_CASE_BUFFER_CAPACITY];
    bool current_case_failure_recorded;
} Test_V2_Runtime_State;

static Test_V2_Runtime_State g_test_v2_runtime = {0};

static void test_v2_copy_case_name(char dst[TEST_V2_CASE_BUFFER_CAPACITY], const char *src) {
    if (!dst) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    if (snprintf(dst, TEST_V2_CASE_BUFFER_CAPACITY, "%s", src) >= TEST_V2_CASE_BUFFER_CAPACITY) {
        dst[0] = '\0';
    }
}

static const char *test_v2_normalize_case_name(const char *func,
                                               char buffer[TEST_V2_CASE_BUFFER_CAPACITY]) {
    const char *name = func;

    if (!buffer) return func;
    buffer[0] = '\0';
    if (!func || func[0] == '\0') return "";

    if (strncmp(func, "test_impl_", 10) == 0) {
        name = func + 10;
    } else if (strncmp(func, "test_", 5) == 0) {
        name = func + 5;
    }

    test_v2_copy_case_name(buffer, name);
    return buffer;
}

static void test_v2_note_case_match(void) {
    const char *match_path = getenv(CMK2NOB_TEST_CASE_MATCH_PATH_ENV);
    FILE *file = NULL;

    if (!match_path || match_path[0] == '\0') return;
    file = fopen(match_path, "ab");
    if (!file) return;
    fclose(file);
}

static void test_v2_record_failure_summary(const char *case_name,
                                           const char *file,
                                           int line,
                                           const char *message) {
    const char *summary_path = getenv(CMK2NOB_TEST_FAILURE_SUMMARY_ENV);
    FILE *summary = NULL;

    if (!case_name || case_name[0] == '\0') return;
    if (!summary_path || summary_path[0] == '\0') return;

    summary = fopen(summary_path, "ab");
    if (!summary) return;

    (void)fprintf(summary,
                  "case=%s\tfile=%s\tline=%d\tmessage=%s\n",
                  case_name,
                  file ? file : "",
                  line > 0 ? line : 0,
                  message ? message : "");
    fclose(summary);
}

const char *test_v2_case_filter(void) {
    return getenv(CMK2NOB_TEST_CASE_FILTER_ENV);
}

bool test_v2_case_matches(const char *case_name) {
    const char *filter = test_v2_case_filter();

    if (!filter || filter[0] == '\0') return true;
    if (!case_name) return false;
    return strcmp(filter, case_name) == 0;
}

void test_v2_case_begin(const char *case_name) {
    test_v2_copy_case_name(g_test_v2_runtime.current_case_name, case_name);
    g_test_v2_runtime.current_case_failure_recorded = false;
    test_v2_note_case_match();
}

void test_v2_case_end(void) {
    g_test_v2_runtime.current_case_name[0] = '\0';
    g_test_v2_runtime.current_case_failure_recorded = false;
}

void test_v2_emit_failure_message(const char *func,
                                  const char *file,
                                  int line,
                                  const char *message) {
    char normalized_case_name[TEST_V2_CASE_BUFFER_CAPACITY] = {0};
    const char *case_name = g_test_v2_runtime.current_case_name;

    if (!func || !message) return;
    if (!case_name || case_name[0] == '\0') {
        case_name = test_v2_normalize_case_name(func, normalized_case_name);
    }

    if (!g_test_v2_runtime.current_case_failure_recorded) {
        test_v2_record_failure_summary(case_name, file, line, message);
        if (g_test_v2_runtime.current_case_name[0] != '\0') {
            g_test_v2_runtime.current_case_failure_recorded = true;
        }
    }

    if (file && file[0] != '\0' && line > 0) {
        fprintf(stderr, "FAILED: %s (%s:%d): %s\n", case_name, file, line, message);
    } else if (file && file[0] != '\0') {
        fprintf(stderr, "FAILED: %s (%s): %s\n", case_name, file, message);
    } else if (line > 0) {
        fprintf(stderr, "FAILED: %s:%d: %s\n", case_name, line, message);
    } else {
        fprintf(stderr, "FAILED: %s: %s\n", case_name, message);
    }
    fflush(stderr);
}
