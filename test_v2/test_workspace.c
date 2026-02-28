#include "test_workspace.h"

#include "nob.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define TEMP_TESTS_BASE "Temp_tests"

typedef struct {
    bool exists;
    bool is_dir;
    bool is_link_like;
} Tiny_Path_Info;

static unsigned long test_ws_pid(void) {
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static bool tiny_is_dot_or_dotdot(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static bool tiny_join_path(const char *lhs, const char *rhs, char out[_TINYDIR_PATH_MAX]) {
    int n = snprintf(out, _TINYDIR_PATH_MAX, "%s/%s", lhs, rhs);
    if (n < 0 || n >= _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "path too long while joining '%s' and '%s'", lhs, rhs);
        return false;
    }
    return true;
}

static bool tiny_get_path_info(const char *path, Tiny_Path_Info *out) {
    if (!path || !out) return false;
    *out = (Tiny_Path_Info){0};

#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        nob_log(NOB_ERROR, "could not stat path %s: %s", path, nob_win32_error_message(err));
        return false;
    }
    out->exists = true;
    out->is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->is_link_like = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    return true;
#else
    struct stat st = {0};
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) return true;
        nob_log(NOB_ERROR, "could not lstat path %s: %s", path, strerror(errno));
        return false;
    }
    out->exists = true;
    out->is_dir = S_ISDIR(st.st_mode);
    out->is_link_like = S_ISLNK(st.st_mode);
    return true;
#endif
}

static bool tiny_delete_file_like(const char *path) {
#if defined(_WIN32)
    if (DeleteFileA(path)) return true;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
    nob_log(NOB_ERROR, "could not delete file %s: %s", path, nob_win32_error_message(err));
    return false;
#else
    if (unlink(path) == 0) return true;
    if (errno == ENOENT) return true;
    nob_log(NOB_ERROR, "could not delete file %s: %s", path, strerror(errno));
    return false;
#endif
}

static bool tiny_delete_dir_like(const char *path) {
#if defined(_WIN32)
    if (RemoveDirectoryA(path)) return true;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
    nob_log(NOB_ERROR, "could not remove directory %s: %s", path, nob_win32_error_message(err));
    return false;
#else
    if (rmdir(path) == 0) return true;
    if (errno == ENOENT) return true;
    nob_log(NOB_ERROR, "could not remove directory %s: %s", path, strerror(errno));
    return false;
#endif
}

static bool tiny_remove_tree(const char *path) {
    Tiny_Path_Info info = {0};
    if (!tiny_get_path_info(path, &info)) return false;
    if (!info.exists) return true;

    if (info.is_link_like) {
        if (info.is_dir) return tiny_delete_dir_like(path);
        return tiny_delete_file_like(path);
    }

    if (!info.is_dir) return tiny_delete_file_like(path);

    tinydir_dir dir = {0};
    if (tinydir_open(&dir, path) != 0) {
#if defined(_WIN32)
        nob_log(NOB_ERROR, "could not open directory %s", path);
#else
        nob_log(NOB_ERROR, "could not open directory %s: %s", path, strerror(errno));
#endif
        return false;
    }

    bool ok = true;
    while (dir.has_next) {
        tinydir_file file = {0};
        if (tinydir_readfile(&dir, &file) != 0) {
#if defined(_WIN32)
            nob_log(NOB_ERROR, "could not read entry from %s", path);
#else
            nob_log(NOB_ERROR, "could not read entry from %s: %s", path, strerror(errno));
#endif
            ok = false;
            break;
        }
        if (tinydir_next(&dir) != 0 && dir.has_next) {
#if defined(_WIN32)
            nob_log(NOB_ERROR, "could not advance directory %s", path);
#else
            nob_log(NOB_ERROR, "could not advance directory %s: %s", path, strerror(errno));
#endif
            ok = false;
            break;
        }

        if (tiny_is_dot_or_dotdot(file.name)) continue;
        if (!tiny_remove_tree(file.path)) {
            ok = false;
            break;
        }
    }

    tinydir_close(&dir);
    if (!tiny_delete_dir_like(path)) ok = false;
    return ok;
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

bool test_ws_prepare(Test_Workspace *ws, const char *suite_name) {
    if (!ws || !suite_name || suite_name[0] == '\0') return false;
    memset(ws, 0, sizeof(*ws));

    const char *cwd = nob_get_current_dir_temp();
    if (!cwd) {
        nob_log(NOB_ERROR, "workspace: could not read current directory");
        return false;
    }

    const char *reuse = getenv("CMK2NOB_TEST_WS_REUSE_CWD");
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
        if (!tiny_join_path(ws->work, "test_v2", ws->suite_copy)) return false;
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
    if (!tiny_join_path(ws->root, "work", ws->work)) return false;
    if (!tiny_join_path(ws->root, "bin", ws->bin)) return false;
    if (!tiny_join_path(ws->work, "test_v2", ws->suite_copy)) return false;

    if (!nob_mkdir_if_not_exists(TEMP_TESTS_BASE)) return false;
    if (!tiny_remove_tree(ws->root)) return false;
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
    const char *reuse = getenv("CMK2NOB_TEST_WS_REUSE_CWD");
    if (reuse && strcmp(reuse, "1") == 0) {
        nob_log(NOB_INFO, "workspace cleaned: %s", ws->root);
        return true;
    }
    if (!tiny_remove_tree(ws->root)) return false;
    (void)tiny_delete_dir_like(TEMP_TESTS_BASE);
    nob_log(NOB_INFO, "workspace cleaned: %s", ws->root);
    return true;
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
