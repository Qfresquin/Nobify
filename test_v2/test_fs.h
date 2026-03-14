#ifndef TEST_FS_H_
#define TEST_FS_H_

#include "tinydir.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    bool exists;
    bool is_dir;
    bool is_link_like;
} Test_Fs_Path_Info;

static inline bool test_fs_is_dot_or_dotdot(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static inline bool test_fs_join_path(const char *lhs,
                                     const char *rhs,
                                     char out[_TINYDIR_PATH_MAX]) {
    int n = snprintf(out, _TINYDIR_PATH_MAX, "%s/%s", lhs, rhs);
    if (n < 0 || n >= _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "path too long while joining '%s' and '%s'", lhs, rhs);
        return false;
    }
    return true;
}

static inline bool test_fs_get_path_info(const char *path, Test_Fs_Path_Info *out) {
    if (!path || !out) return false;
    *out = (Test_Fs_Path_Info){0};

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

static inline bool test_fs_delete_file_like(const char *path) {
#if defined(_WIN32)
    if (DeleteFileA(path)) return true;
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        nob_log(NOB_ERROR, "could not delete file %s: %s", path, nob_win32_error_message(err));
        return false;
    }
#else
    if (unlink(path) == 0) return true;
    if (errno == ENOENT) return true;
    nob_log(NOB_ERROR, "could not delete file %s: %s", path, strerror(errno));
    return false;
#endif
}

static inline bool test_fs_delete_dir_like(const char *path) {
#if defined(_WIN32)
    if (RemoveDirectoryA(path)) return true;
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        nob_log(NOB_ERROR, "could not remove directory %s: %s", path, nob_win32_error_message(err));
        return false;
    }
#else
    if (rmdir(path) == 0) return true;
    if (errno == ENOENT) return true;
    nob_log(NOB_ERROR, "could not remove directory %s: %s", path, strerror(errno));
    return false;
#endif
}

typedef struct {
    bool ok;
} Test_Fs_Remove_Walk_State;

static inline bool test_fs_remove_tree_walk(Nob_Walk_Entry entry) {
    Test_Fs_Remove_Walk_State *state = (Test_Fs_Remove_Walk_State*)entry.data;
    bool ok = entry.type == NOB_FILE_DIRECTORY
        ? test_fs_delete_dir_like(entry.path)
        : test_fs_delete_file_like(entry.path);
    if (state) state->ok = state->ok && ok;
    return ok;
}

static inline bool test_fs_remove_tree(const char *path) {
    Test_Fs_Path_Info info = {0};
    Test_Fs_Remove_Walk_State state = { .ok = true };
    if (!test_fs_get_path_info(path, &info)) return false;
    if (!info.exists) return true;

    if (info.is_link_like) {
        if (info.is_dir) return test_fs_delete_dir_like(path);
        return test_fs_delete_file_like(path);
    }

    if (!info.is_dir) return test_fs_delete_file_like(path);
    if (!nob_walk_dir(path, test_fs_remove_tree_walk, .data = &state, .post_order = true)) {
        return false;
    }
    return state.ok;
}

static inline bool test_fs_copy_tree(const char *src, const char *dst) {
    Test_Fs_Path_Info info = {0};
    Nob_Dir_Entry dir = {0};
    bool ok = true;

    if (!test_fs_get_path_info(src, &info)) return false;
    if (!info.exists) {
        nob_log(NOB_ERROR, "source path does not exist: %s", src);
        return false;
    }
    if (info.is_link_like) {
        nob_log(NOB_ERROR, "refusing to copy link/reparse path: %s", src);
        return false;
    }
    if (!info.is_dir) return nob_copy_file(src, dst);
    if (!nob_mkdir_if_not_exists(dst)) return false;
    if (!nob_dir_entry_open(src, &dir)) return false;

    while (nob_dir_entry_next(&dir)) {
        char src_child[_TINYDIR_PATH_MAX] = {0};
        char dst_child[_TINYDIR_PATH_MAX] = {0};

        if (test_fs_is_dot_or_dotdot(dir.name)) continue;
        if (!test_fs_join_path(src, dir.name, src_child) ||
            !test_fs_join_path(dst, dir.name, dst_child) ||
            !test_fs_copy_tree(src_child, dst_child)) {
            ok = false;
            break;
        }
    }

    if (dir.error) ok = false;
    nob_dir_entry_close(dir);
    return ok;
}

static inline bool test_fs_save_current_dir(char out[_TINYDIR_PATH_MAX]) {
    const char *cwd = nob_get_current_dir_temp();
    size_t n = 0;
    if (!cwd) {
        nob_log(NOB_ERROR, "could not get current directory");
        return false;
    }

    n = strlen(cwd);
    if (n + 1 > _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "current directory path is too long");
        return false;
    }

    memcpy(out, cwd, n + 1);
    return true;
}

#endif // TEST_FS_H_
