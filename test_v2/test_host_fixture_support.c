#include "test_host_fixture_support.h"

#include "nob.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool test_host_remove_link_like_path(const char *path) {
    if (!path) return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        if (RemoveDirectoryA(path)) return true;
    }
    if (DeleteFileA(path)) return true;
    if (RemoveDirectoryA(path)) return true;
    return false;
#else
    struct stat st = {0};
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        return rmdir(path) == 0;
    }
    return unlink(path) == 0;
#endif
}

bool test_host_set_env_value(const char *name, const char *value) {
    if (!name || name[0] == '\0') return false;
#if defined(_WIN32)
    return _putenv_s(name, value ? value : "") == 0;
#else
    if (value) return setenv(name, value, 1) == 0;
    return unsetenv(name) == 0;
#endif
}

bool test_host_env_guard_begin(Test_Host_Env_Guard *guard, const char *name, const char *value) {
    const char *prev_value = NULL;
    size_t name_len = 0;

    if (!guard || !name || name[0] == '\0') return false;
    memset(guard, 0, sizeof(*guard));
    guard->heap_allocated = false;

    prev_value = getenv(name);
    name_len = strlen(name);
    guard->name = (char*)malloc(name_len + 1);
    if (!guard->name) return false;
    memcpy(guard->name, name, name_len + 1);

    if (prev_value) {
        size_t prev_len = strlen(prev_value);
        guard->prev_value = (char*)malloc(prev_len + 1);
        if (!guard->prev_value) {
            free(guard->name);
            guard->name = NULL;
            return false;
        }
        memcpy(guard->prev_value, prev_value, prev_len + 1);
        guard->had_prev_value = true;
    }

    if (!test_host_set_env_value(name, value)) {
        free(guard->name);
        free(guard->prev_value);
        memset(guard, 0, sizeof(*guard));
        return false;
    }

    return true;
}

bool test_host_env_guard_begin_heap(Test_Host_Env_Guard **out_guard,
                                    const char *name,
                                    const char *value) {
    Test_Host_Env_Guard *guard = NULL;
    if (!out_guard) return false;
    *out_guard = NULL;
    guard = (Test_Host_Env_Guard*)calloc(1, sizeof(*guard));
    if (!guard) return false;
    if (!test_host_env_guard_begin(guard, name, value)) {
        free(guard);
        return false;
    }
    guard->heap_allocated = true;
    *out_guard = guard;
    return true;
}

void test_host_env_guard_cleanup(void *ctx) {
    Test_Host_Env_Guard *guard = (Test_Host_Env_Guard*)ctx;
    bool heap_allocated = false;
    if (!guard) return;
    heap_allocated = guard->heap_allocated;
    if (guard->name) {
        (void)test_host_set_env_value(guard->name, guard->had_prev_value ? guard->prev_value : NULL);
    }
    free(guard->name);
    free(guard->prev_value);
    if (heap_allocated) {
        free(guard);
    } else {
        memset(guard, 0, sizeof(*guard));
    }
}

bool test_host_create_directory_link_like(const char *link_path, const char *target_path) {
    if (!link_path || !target_path) return false;
    if (!test_host_remove_link_like_path(link_path)) return false;

#if defined(_WIN32)
    char link_win[512] = {0};
    char target_win[512] = {0};
    int link_n = snprintf(link_win, sizeof(link_win), "%s", link_path);
    int target_n = snprintf(target_win, sizeof(target_win), "%s", target_path);
    if (link_n < 0 || link_n >= (int)sizeof(link_win) ||
        target_n < 0 || target_n >= (int)sizeof(target_win)) {
        return false;
    }
    for (size_t i = 0; link_win[i] != '\0'; i++) if (link_win[i] == '/') link_win[i] = '\\';
    for (size_t i = 0; target_win[i] != '\0'; i++) if (target_win[i] == '/') target_win[i] = '\\';

    {
        DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
        if (CreateSymbolicLinkA(link_win, target_win, flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
            return true;
        }
        if (CreateSymbolicLinkA(link_win, target_win, flags) != 0) return true;
    }

    {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "cmd", "/C", "mklink", "/J", link_win, target_win);
        return nob_cmd_run_sync_and_reset(&cmd);
    }
#else
    if (symlink(target_path, link_path) == 0) return true;
    return errno == EEXIST;
#endif
}

bool test_host_prepare_symlink_escape_fixture(const char *outside_dir,
                                              const char *outside_file_path,
                                              const char *outside_file_contents,
                                              const char *inside_link,
                                              const char *link_target) {
    size_t outside_len = 0;

    if (!outside_dir || !outside_file_path || !outside_file_contents || !inside_link || !link_target) {
        return false;
    }

    if (!nob_mkdir_if_not_exists(outside_dir)) {
        nob_log(NOB_ERROR, "host fixture: failed to create %s", outside_dir);
        return false;
    }

    outside_len = strlen(outside_file_contents);
    if (!nob_write_entire_file(outside_file_path, outside_file_contents, outside_len)) {
        nob_log(NOB_ERROR, "host fixture: failed to write %s", outside_file_path);
        return false;
    }

    if (!test_host_create_directory_link_like(inside_link, link_target)) {
        nob_log(NOB_ERROR,
                "host fixture: failed to create directory link %s -> %s",
                inside_link,
                link_target);
        return false;
    }

    return true;
}

bool test_host_prepare_mock_site_name_command(char *out_path, size_t out_path_size) {
    if (!out_path || out_path_size == 0) return false;
#if defined(_WIN32)
    {
        int n = snprintf(out_path, out_path_size, "%s", "hostname");
        return n > 0 && (size_t)n < out_path_size;
    }
#else
    {
        const char *path = "./temp_site_name_cmd.sh";
        const char *script = "#!/bin/sh\nprintf 'mock-site\\n'\n";
        if (!nob_write_entire_file(path, script, strlen(script))) return false;
        if (chmod(path, 0755) != 0) return false;
        {
            int n = snprintf(out_path, out_path_size, "%s", path);
            return n > 0 && (size_t)n < out_path_size;
        }
    }
#endif
}

bool test_host_create_tar_archive(const char *archive_path,
                                  const char *parent_dir,
                                  const char *entry_name) {
    Nob_Cmd cmd = {0};

    if (!archive_path || !parent_dir || !entry_name) return false;
    nob_cmd_append(&cmd, "tar", "-cf", archive_path, "-C", parent_dir, entry_name);
    return nob_cmd_run_sync_and_reset(&cmd);
}

bool test_host_create_git_repo_with_tag(const char *repo_dir,
                                        const char *cmakelists_text,
                                        const char *version_text,
                                        const char *tag_name) {
    char cmakelists_path[1024] = {0};
    char version_path[1024] = {0};
    Nob_Cmd cmd = {0};
    int cmakelists_n = 0;
    int version_n = 0;

    if (!repo_dir || !cmakelists_text || !version_text || !tag_name) return false;

    cmakelists_n = snprintf(cmakelists_path, sizeof(cmakelists_path), "%s/CMakeLists.txt", repo_dir);
    version_n = snprintf(version_path, sizeof(version_path), "%s/version.txt", repo_dir);
    if (cmakelists_n < 0 || cmakelists_n >= (int)sizeof(cmakelists_path) ||
        version_n < 0 || version_n >= (int)sizeof(version_path)) {
        return false;
    }

    if (!nob_mkdir_if_not_exists(repo_dir)) return false;
    if (!nob_write_entire_file(cmakelists_path, cmakelists_text, strlen(cmakelists_text))) return false;
    if (!nob_write_entire_file(version_path, version_text, strlen(version_text))) return false;

    nob_cmd_append(&cmd, "git", "-C", repo_dir, "init");
    if (!nob_cmd_run_sync_and_reset(&cmd)) return false;

    nob_cmd_append(&cmd, "git", "-C", repo_dir, "add", ".");
    if (!nob_cmd_run_sync_and_reset(&cmd)) return false;

    nob_cmd_append(&cmd,
                   "git",
                   "-C",
                   repo_dir,
                   "-c",
                   "user.name=Nobify Tests",
                   "-c",
                   "user.email=tests@example.com",
                   "commit",
                   "-m",
                   "init");
    if (!nob_cmd_run_sync_and_reset(&cmd)) return false;

    nob_cmd_append(&cmd, "git", "-C", repo_dir, "tag", tag_name);
    return nob_cmd_run_sync_and_reset(&cmd);
}
