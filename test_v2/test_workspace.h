#ifndef TEST_WORKSPACE_H_
#define TEST_WORKSPACE_H_

#include "tinydir.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char root[_TINYDIR_PATH_MAX];
    char work[_TINYDIR_PATH_MAX];
    char bin[_TINYDIR_PATH_MAX];
    char suite_copy[_TINYDIR_PATH_MAX];
} Test_Workspace;

bool test_ws_prepare(Test_Workspace *ws, const char *suite_name);
bool test_ws_enter(const Test_Workspace *ws, char prev_cwd[], size_t prev_cwd_cap);
bool test_ws_leave(const char *prev_cwd);
bool test_ws_cleanup(const Test_Workspace *ws);
const char *test_ws_root(const Test_Workspace *ws);
const char *test_ws_work(const Test_Workspace *ws);
const char *test_ws_bin(const Test_Workspace *ws);
const char *test_ws_suite_copy(const Test_Workspace *ws);

#endif // TEST_WORKSPACE_H_
