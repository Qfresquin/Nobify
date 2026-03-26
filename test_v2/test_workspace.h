#ifndef TEST_WORKSPACE_H_
#define TEST_WORKSPACE_H_

#include "tinydir.h"

#include <stdbool.h>
#include <stddef.h>

#define CMK2NOB_TEST_RUNNER_ENV "CMK2NOB_TEST_RUNNER"
#define CMK2NOB_TEST_WS_REUSE_CWD_ENV "CMK2NOB_TEST_WS_REUSE_CWD"
#define CMK2NOB_TEST_REPO_ROOT_ENV "CMK2NOB_TEST_REPO_ROOT"

typedef struct {
    char root[_TINYDIR_PATH_MAX];
    char work[_TINYDIR_PATH_MAX];
    char bin[_TINYDIR_PATH_MAX];
    char suite_copy[_TINYDIR_PATH_MAX];
} Test_Workspace;

typedef struct {
    char prev_cwd[_TINYDIR_PATH_MAX];
    char root[_TINYDIR_PATH_MAX];
    char work[_TINYDIR_PATH_MAX];
} Test_Case_Workspace;

bool test_ws_prepare(Test_Workspace *ws, const char *suite_name);
bool test_ws_enter(const Test_Workspace *ws, char prev_cwd[], size_t prev_cwd_cap);
bool test_ws_leave(const char *prev_cwd);
bool test_ws_cleanup(const Test_Workspace *ws);
bool test_ws_case_enter(Test_Case_Workspace *ws, const char *test_name);
bool test_ws_case_leave(const Test_Case_Workspace *ws);
bool test_ws_should_update_golden(void);
bool test_ws_update_golden_file(const char *expected_path, const void *data, size_t size);
const char *test_ws_root(const Test_Workspace *ws);
const char *test_ws_work(const Test_Workspace *ws);
const char *test_ws_bin(const Test_Workspace *ws);
const char *test_ws_suite_copy(const Test_Workspace *ws);

#endif // TEST_WORKSPACE_H_
