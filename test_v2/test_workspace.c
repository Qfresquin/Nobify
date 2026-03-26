#include "test_workspace.h"

#include "nob.h"
#include "test_fs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define TEMP_TESTS_BASE "Temp_tests"

static size_t g_test_ws_case_counter = 0;

static unsigned long test_ws_pid(void) {
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static bool build_abs_path(const char *cwd, const char *rel, char out[_TINYDIR_PATH_MAX]) {
    if (!cwd || !rel || !out) return false;
    int n = snprintf(out, _TINYDIR_PATH_MAX, "%s/%s", cwd, rel);
    if (n < 0 || n >= _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "path too long while composing '%s/%s'", cwd, rel);
        return false;
    }
    return true;
}

static bool test_ws_is_absolute_path(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    if (path[0] == '/' || path[0] == '\\') return true;
    return strlen(path) >= 3 &&
           ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
#else
    return path[0] == '/';
#endif
}

static bool test_ws_resolve_repo_path(const char *path, char out[_TINYDIR_PATH_MAX]) {
    if (!path || !out) return false;

    if (test_ws_is_absolute_path(path)) {
        int n = snprintf(out, _TINYDIR_PATH_MAX, "%s", path);
        if (n < 0 || n >= _TINYDIR_PATH_MAX) {
            nob_log(NOB_ERROR, "path too long while copying '%s'", path);
            return false;
        }
        return true;
    }

    const char *repo_root = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    if (!repo_root || repo_root[0] == '\0') {
        repo_root = nob_get_current_dir_temp();
    }
    if (!repo_root || repo_root[0] == '\0') {
        nob_log(NOB_ERROR, "workspace: could not resolve repository root for %s", path);
        return false;
    }

    return build_abs_path(repo_root, path, out);
}

static void test_ws_sanitize_case_name(const char *name,
                                       char out[128]) {
    size_t wi = 0;
    if (!out) return;
    memset(out, 0, 128);
    if (!name || name[0] == '\0') {
        memcpy(out, "case", 5);
        return;
    }

    for (size_t i = 0; name[i] != '\0' && wi + 1 < 128; i++) {
        unsigned char ch = (unsigned char)name[i];
        bool is_alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        bool is_digit = (ch >= '0' && ch <= '9');
        out[wi++] = (char)(is_alpha || is_digit ? ch : '_');
    }
    out[wi] = '\0';
    if (wi == 0) memcpy(out, "case", 5);
}

static bool test_ws_create_directory_link_like(const char *link_path,
                                               const char *target_path) {
    if (!link_path || !target_path) return false;

#if defined(_WIN32)
    char link_win[_TINYDIR_PATH_MAX] = {0};
    char target_win[_TINYDIR_PATH_MAX] = {0};
    int link_n = snprintf(link_win, sizeof(link_win), "%s", link_path);
    int target_n = snprintf(target_win, sizeof(target_win), "%s", target_path);
    if (link_n < 0 || link_n >= (int)sizeof(link_win) ||
        target_n < 0 || target_n >= (int)sizeof(target_win)) {
        nob_log(NOB_ERROR, "workspace case link path too long");
        return false;
    }
    for (size_t i = 0; link_win[i] != '\0'; i++) {
        if (link_win[i] == '/') link_win[i] = '\\';
    }
    for (size_t i = 0; target_win[i] != '\0'; i++) {
        if (target_win[i] == '/') target_win[i] = '\\';
    }

    DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (CreateSymbolicLinkA(link_win,
                            target_win,
                            flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }
    if (CreateSymbolicLinkA(link_win, target_win, flags) != 0) return true;
    return test_fs_copy_tree(target_path, link_path);
#else
    if (symlink(target_path, link_path) == 0) return true;
    return test_fs_copy_tree(target_path, link_path);
#endif
}

bool test_ws_prepare(Test_Workspace *ws, const char *suite_name) {
    if (!ws || !suite_name || suite_name[0] == '\0') return false;
    memset(ws, 0, sizeof(*ws));

    const char *cwd = nob_get_current_dir_temp();
    if (!cwd) {
        nob_log(NOB_ERROR, "workspace: could not read current directory");
        return false;
    }

    const char *reuse = getenv(CMK2NOB_TEST_WS_REUSE_CWD_ENV);
    if (reuse && strcmp(reuse, "1") == 0) {
        int n_root = snprintf(ws->root, sizeof(ws->root), "%s", cwd);
        int n_work = snprintf(ws->work, sizeof(ws->work), "%s", cwd);
        int n_bin = snprintf(ws->bin, sizeof(ws->bin), "%s", cwd);
        if (n_root < 0 || n_root >= (int)sizeof(ws->root) ||
            n_work < 0 || n_work >= (int)sizeof(ws->work) ||
            n_bin < 0 || n_bin >= (int)sizeof(ws->bin)) {
            nob_log(NOB_ERROR, "workspace: current directory path too long");
            return false;
        }
        if (!test_fs_join_path(ws->work, "test_v2", ws->suite_copy)) return false;
        nob_log(NOB_INFO, "workspace created: %s", ws->root);
        return true;
    }

    time_t now = time(NULL);
    unsigned long pid = test_ws_pid();
    char root_rel[_TINYDIR_PATH_MAX] = {0};
    int root_rel_n = snprintf(root_rel, sizeof(root_rel), "%s/%s-%lu-%lld",
                              TEMP_TESTS_BASE, suite_name, pid, (long long)now);
    if (root_rel_n < 0 || root_rel_n >= (int)sizeof(root_rel)) {
        nob_log(NOB_ERROR, "workspace: generated root path is too long");
        return false;
    }

    if (!build_abs_path(cwd, root_rel, ws->root)) return false;
    if (!test_fs_join_path(ws->root, "work", ws->work)) return false;
    if (!test_fs_join_path(ws->root, "bin", ws->bin)) return false;
    if (!test_fs_join_path(ws->work, "test_v2", ws->suite_copy)) return false;

    if (!nob_mkdir_if_not_exists(TEMP_TESTS_BASE)) return false;
    if (!test_fs_remove_tree(ws->root)) return false;
    if (!nob_mkdir_if_not_exists(ws->root)) return false;
    if (!nob_mkdir_if_not_exists(ws->work)) return false;
    if (!nob_mkdir_if_not_exists(ws->bin)) return false;
    if (!nob_copy_directory_recursively("test_v2", ws->suite_copy)) return false;

    nob_log(NOB_INFO, "workspace created: %s", ws->root);
    return true;
}

bool test_ws_enter(const Test_Workspace *ws, char prev_cwd[], size_t prev_cwd_cap) {
    if (!ws || !prev_cwd || prev_cwd_cap == 0) return false;

    const char *cwd = nob_get_current_dir_temp();
    if (!cwd) {
        nob_log(NOB_ERROR, "workspace: could not capture current directory");
        return false;
    }

    size_t n = strlen(cwd);
    if (n + 1 > prev_cwd_cap) {
        nob_log(NOB_ERROR, "workspace: previous cwd buffer too small");
        return false;
    }
    memcpy(prev_cwd, cwd, n + 1);

    if (!nob_set_current_dir(ws->work)) {
        nob_log(NOB_ERROR, "workspace: failed to enter %s", ws->work);
        return false;
    }
    return true;
}

bool test_ws_leave(const char *prev_cwd) {
    if (!prev_cwd || prev_cwd[0] == '\0') return false;
    if (!nob_set_current_dir(prev_cwd)) {
        nob_log(NOB_ERROR, "workspace: failed to restore cwd to %s", prev_cwd);
        return false;
    }
    return true;
}

bool test_ws_cleanup(const Test_Workspace *ws) {
    if (!ws || ws->root[0] == '\0') return false;
    const char *reuse = getenv(CMK2NOB_TEST_WS_REUSE_CWD_ENV);
    if (reuse && strcmp(reuse, "1") == 0) {
        nob_log(NOB_INFO, "workspace cleaned: %s", ws->root);
        return true;
    }
    if (!test_fs_remove_tree(ws->root)) return false;
    (void)test_fs_delete_dir_like(TEMP_TESTS_BASE);
    nob_log(NOB_INFO, "workspace cleaned: %s", ws->root);
    return true;
}

bool test_ws_case_enter(Test_Case_Workspace *ws, const char *test_name) {
    char cases_root[_TINYDIR_PATH_MAX] = {0};
    char case_name[128] = {0};
    char suite_copy[_TINYDIR_PATH_MAX] = {0};
    char case_suite_copy[_TINYDIR_PATH_MAX] = {0};
    Test_Fs_Path_Info info = {0};

    if (!ws) return false;
    memset(ws, 0, sizeof(*ws));

    if (!test_fs_save_current_dir(ws->prev_cwd)) return false;
    if (!test_fs_join_path(ws->prev_cwd, "__testcases", cases_root)) return false;
    if (!nob_mkdir_if_not_exists(cases_root)) return false;

    test_ws_sanitize_case_name(test_name, case_name);
    g_test_ws_case_counter++;
    {
        int n = snprintf(ws->root,
                         sizeof(ws->root),
                         "%s/%04zu_%s",
                         cases_root,
                         g_test_ws_case_counter,
                         case_name);
        if (n < 0 || n >= (int)sizeof(ws->root)) {
            nob_log(NOB_ERROR, "workspace case path too long");
            return false;
        }
    }

    if (!test_fs_remove_tree(ws->root)) return false;
    if (!nob_mkdir_if_not_exists(ws->root)) return false;
    if (!test_fs_join_path(ws->root, "work", ws->work)) return false;
    if (!nob_mkdir_if_not_exists(ws->work)) return false;

    if (!test_fs_join_path(ws->prev_cwd, "test_v2", suite_copy)) return false;
    if (!test_fs_get_path_info(suite_copy, &info)) return false;
    if (info.exists && info.is_dir) {
        if (!test_fs_join_path(ws->work, "test_v2", case_suite_copy)) return false;
        if (!test_ws_create_directory_link_like(case_suite_copy, suite_copy)) {
            (void)test_fs_remove_tree(ws->root);
            return false;
        }
    }

    if (!nob_set_current_dir(ws->work)) {
        nob_log(NOB_ERROR, "workspace: failed to enter test case dir %s", ws->work);
        (void)test_fs_remove_tree(ws->root);
        return false;
    }

    return true;
}

bool test_ws_case_leave(const Test_Case_Workspace *ws) {
    char cases_root[_TINYDIR_PATH_MAX] = {0};
    bool left = false;
    bool cleaned = false;
    if (!ws || ws->prev_cwd[0] == '\0' || ws->root[0] == '\0') return false;

    left = test_ws_leave(ws->prev_cwd);
    if (!left) return false;

    cleaned = test_fs_remove_tree(ws->root);
    if (!cleaned) return false;

    if (test_fs_join_path(ws->prev_cwd, "__testcases", cases_root)) {
        (void)test_fs_delete_dir_like(cases_root);
    }
    return true;
}

bool test_ws_should_update_golden(void) {
    const char *update = getenv("CMK2NOB_UPDATE_GOLDEN");
    return update && strcmp(update, "1") == 0;
}

bool test_ws_update_golden_file(const char *expected_path, const void *data, size_t size) {
    char repo_path[_TINYDIR_PATH_MAX] = {0};
    if (!expected_path) return false;
    if (!test_ws_resolve_repo_path(expected_path, repo_path)) return false;
    return nob_write_entire_file(repo_path, data, size);
}

const char *test_ws_root(const Test_Workspace *ws) {
    return ws ? ws->root : NULL;
}

const char *test_ws_work(const Test_Workspace *ws) {
    return ws ? ws->work : NULL;
}

const char *test_ws_bin(const Test_Workspace *ws) {
    return ws ? ws->bin : NULL;
}

const char *test_ws_suite_copy(const Test_Workspace *ws) {
    return ws ? ws->suite_copy : NULL;
}
