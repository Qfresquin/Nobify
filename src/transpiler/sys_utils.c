#include "sys_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

static const char *sys_arena_sv_to_cstr(Arena *arena, String_View sv) {
    if (!arena || sv.count == 0 || !sv.data) return "";
    return arena_strndup(arena, sv.data, sv.count);
}

static size_t sys_path_last_separator_index(String_View path) {
    for (size_t i = path.count; i > 0; i--) {
        char c = path.data[i - 1];
        if (c == '/' || c == '\\') return i - 1;
    }
    return SIZE_MAX;
}

static String_View sys_strip_trailing_ws(Arena *arena, String_View value) {
    if (!arena || !value.data) return value;
    size_t end = value.count;
    while (end > 0 && isspace((unsigned char)value.data[end - 1])) end--;
    return sv_from_cstr(arena_strndup(arena, value.data, end));
}

bool sys_path_has_separator(String_View path) {
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') return true;
    }
    return false;
}

String_View sys_path_basename(String_View path) {
    size_t sep = sys_path_last_separator_index(path);
    if (sep == SIZE_MAX) return path;
    return nob_sv_from_parts(path.data + sep + 1, path.count - (sep + 1));
}

bool sys_ensure_parent_dirs(Arena *arena, String_View file_path) {
    if (!arena || file_path.count == 0) return true;

    size_t sep = file_path.count;
    while (sep > 0) {
        char c = file_path.data[sep - 1];
        if (c == '/' || c == '\\') break;
        sep--;
    }
    if (sep == 0) return true;

    char *dir = arena_strndup(arena, file_path.data, sep - 1);
    if (!dir) return false;

    size_t len = strlen(dir);
    if (len == 0) return true;

    for (size_t i = 0; i < len; i++) {
        if (dir[i] != '/' && dir[i] != '\\') continue;

        char saved = dir[i];
        dir[i] = '\0';
        if (i > 0 && !(i == 2 && isalpha((unsigned char)dir[0]) && dir[1] == ':')) {
            if (!nob_mkdir_if_not_exists(dir)) {
                dir[i] = saved;
                return false;
            }
        }
        dir[i] = saved;
    }

    return nob_mkdir_if_not_exists(dir);
}

String_View sys_read_file(Arena *arena, String_View path) {
    if (!arena || path.count == 0) return (String_View){0};

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(nob_temp_sv_to_cstr(path), &sb)) {
        return (String_View){0};
    }

    char *buf = arena_alloc(arena, sb.count + 1);
    if (!buf) {
        nob_sb_free(sb);
        return (String_View){0};
    }

    memcpy(buf, sb.items, sb.count);
    buf[sb.count] = '\0';
    String_View out = nob_sv_from_parts(buf, sb.count);
    nob_sb_free(sb);
    return out;
}

bool sys_write_file(String_View path, String_View content) {
    if (path.count == 0) return false;
    return nob_write_entire_file(nob_temp_sv_to_cstr(path), content.data ? content.data : "", content.count);
}

static bool sys_path_is_dot_or_dotdot(String_View path) {
    return nob_sv_eq(path, sv_from_cstr(".")) || nob_sv_eq(path, sv_from_cstr(".."));
}

bool sys_delete_path_recursive(Arena *arena, String_View path) {
    if (!arena || path.count == 0) return false;

    const char *path_c = nob_temp_sv_to_cstr(path);
    Nob_File_Type file_type = nob_get_file_type(path_c);
    if ((int)file_type < 0) return true;

    if (file_type == NOB_FILE_REGULAR || file_type == NOB_FILE_SYMLINK || file_type == NOB_FILE_OTHER) {
        return nob_delete_file(path_c);
    }

    if (file_type != NOB_FILE_DIRECTORY) return false;

    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir(path_c, &children)) return false;

    for (size_t i = 0; i < children.count; i++) {
        String_View name = sv_from_cstr(children.items[i]);
        if (sys_path_is_dot_or_dotdot(name)) continue;
        String_View child = build_path_join(arena, path, name);
        if (!sys_delete_path_recursive(arena, child)) {
            nob_da_free(children);
            return false;
        }
    }

    nob_da_free(children);
#if defined(_WIN32)
    return _rmdir(path_c) == 0;
#else
    return rmdir(path_c) == 0;
#endif
}

bool sys_copy_entry_to_destination(Arena *arena, String_View src, String_View destination) {
    if (!arena || src.count == 0 || destination.count == 0) return false;

    const char *src_c = nob_temp_sv_to_cstr(src);
    Nob_File_Type file_type = nob_get_file_type(src_c);
    if ((int)file_type < 0) return false;

    String_View final_dst = destination;
    if (file_type == NOB_FILE_REGULAR || file_type == NOB_FILE_SYMLINK || file_type == NOB_FILE_OTHER) {
        String_View name = sys_path_basename(src);
        final_dst = build_path_join(arena, destination, name);
        if (!sys_ensure_parent_dirs(arena, final_dst)) return false;
        return nob_copy_file(src_c, nob_temp_sv_to_cstr(final_dst));
    }

    if (file_type == NOB_FILE_DIRECTORY) {
        String_View name = sys_path_basename(src);
        final_dst = build_path_join(arena, destination, name);
        if (!nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(destination))) return false;
        return nob_copy_directory_recursively(src_c, nob_temp_sv_to_cstr(final_dst));
    }

    return false;
}

bool sys_download_to_path(Arena *arena, String_View url, String_View out_path, String_View *log_msg) {
    if (!arena || url.count == 0 || out_path.count == 0) return false;

    if (log_msg) *log_msg = sv_from_cstr("");

    if (!sys_ensure_parent_dirs(arena, out_path)) {
        if (log_msg) *log_msg = sv_from_cstr("failed to prepare output directory");
        return false;
    }

    if (nob_sv_starts_with(url, sv_from_cstr("file://"))) {
        String_View src = nob_sv_from_parts(url.data + 7, url.count - 7);
        bool ok = nob_copy_file(nob_temp_sv_to_cstr(src), nob_temp_sv_to_cstr(out_path));
        if (log_msg) {
            *log_msg = ok ? sv_from_cstr("downloaded via file://")
                          : sv_from_cstr("failed to copy file:// source");
        }
        return ok;
    }

#if defined(_WIN32)
    int rc = system(nob_temp_sprintf(
        "powershell -NoProfile -Command \"try {(New-Object Net.WebClient).DownloadFile('%s','%s'); exit 0} catch { exit 1 }\"",
        nob_temp_sv_to_cstr(url),
        nob_temp_sv_to_cstr(out_path)));
#else
    int rc = system(nob_temp_sprintf(
        "curl -L --fail -o '%s' '%s' >/dev/null 2>&1",
        nob_temp_sv_to_cstr(out_path),
        nob_temp_sv_to_cstr(url)));
#endif

    bool ok = (rc == 0);
    if (log_msg) {
        *log_msg = ok ? sv_from_cstr("downloaded via external tool")
                      : sv_from_cstr("download failed");
    }
    return ok;
}

int sys_run_shell_with_timeout(Arena *arena, String_View cmdline, unsigned long timeout_ms, bool *timed_out) {
    if (timed_out) *timed_out = false;
    if (!arena) return -1;

    char *cmd = arena_strndup(arena, cmdline.data ? cmdline.data : "", cmdline.count);
    if (!cmd) return -1;

#if defined(_WIN32)
    String_Builder full = {0};
    sb_append_cstr(&full, "cmd.exe /C ");
    sb_append_cstr(&full, cmd);
    sb_append(&full, '\0');

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    BOOL created = CreateProcessA(NULL, full.items, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!created) {
        nob_sb_free(full);
        return -1;
    }

    DWORD wait_ms = timeout_ms > 0 ? (DWORD)timeout_ms : INFINITE;
    DWORD wait_rc = WaitForSingleObject(pi.hProcess, wait_ms);
    int rc = 1;
    if (wait_rc == WAIT_TIMEOUT) {
        if (timed_out) *timed_out = true;
        (void)TerminateProcess(pi.hProcess, 124);
        rc = 124;
    } else {
        DWORD exit_code = 1;
        if (!GetExitCodeProcess(pi.hProcess, &exit_code)) exit_code = 1;
        rc = (int)exit_code;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    nob_sb_free(full);
    return rc;
#else
    (void)timeout_ms;
    return system(cmd);
#endif
}

bool sys_run_process(const Sys_Process_Request *req, Sys_Process_Result *out) {
    if (!req || !req->arena || !out || !req->argv || req->argv_count == 0) return false;

    memset(out, 0, sizeof(*out));
    out->exit_code = 1;

    String_View scratch_dir = req->scratch_dir;
    if (scratch_dir.count == 0) {
        scratch_dir = sv_from_cstr(".cmk2nob_exec");
    }

    (void)nob_mkdir_if_not_exists(sys_arena_sv_to_cstr(req->arena, scratch_dir));

    static size_t s_sys_process_counter = 0;
    s_sys_process_counter++;

    String_View stdout_path = sv_from_cstr("");
    String_View stderr_path = sv_from_cstr("");
    const char *stdout_path_c = NULL;
    const char *stderr_path_c = NULL;

    if (req->capture_stdout) {
        stdout_path = build_path_join(req->arena, scratch_dir,
            sv_from_cstr(nob_temp_sprintf("proc_%zu.out", s_sys_process_counter)));
        stdout_path_c = sys_arena_sv_to_cstr(req->arena, stdout_path);
    }
    if (req->capture_stderr) {
        stderr_path = build_path_join(req->arena, scratch_dir,
            sv_from_cstr(nob_temp_sprintf("proc_%zu.err", s_sys_process_counter)));
        stderr_path_c = sys_arena_sv_to_cstr(req->arena, stderr_path);
    }

    Nob_Cmd cmd = {0};
    cmd.items = arena_alloc_array(req->arena, const char *, req->argv_count);
    if (!cmd.items) return false;
    cmd.count = req->argv_count;
    cmd.capacity = req->argv_count;

    for (size_t i = 0; i < req->argv_count; i++) {
        cmd.items[i] = sys_arena_sv_to_cstr(req->arena, req->argv[i]);
    }

    String_View old_cwd = sv_from_cstr(arena_strdup(req->arena, nob_get_current_dir_temp()));
    if (req->working_dir.count > 0) {
        (void)nob_set_current_dir(sys_arena_sv_to_cstr(req->arena, req->working_dir));
    }

    bool ok = nob_cmd_run(&cmd, .stdout_path = stdout_path_c, .stderr_path = stderr_path_c);
    (void)nob_set_current_dir(sys_arena_sv_to_cstr(req->arena, old_cwd));

    out->exit_code = ok ? 0 : 1;
    out->timed_out = false;

    if (req->capture_stdout) {
        String_View text = sys_read_file(req->arena, stdout_path);
        if (!text.data) text = sv_from_cstr("");
        if (req->strip_stdout_trailing_ws) text = sys_strip_trailing_ws(req->arena, text);
        out->stdout_text = text;
        if (nob_file_exists(nob_temp_sv_to_cstr(stdout_path))) (void)nob_delete_file(nob_temp_sv_to_cstr(stdout_path));
    } else {
        out->stdout_text = sv_from_cstr("");
    }

    if (req->capture_stderr) {
        String_View text = sys_read_file(req->arena, stderr_path);
        if (!text.data) text = sv_from_cstr("");
        if (req->strip_stderr_trailing_ws) text = sys_strip_trailing_ws(req->arena, text);
        out->stderr_text = text;
        if (nob_file_exists(nob_temp_sv_to_cstr(stderr_path))) (void)nob_delete_file(nob_temp_sv_to_cstr(stderr_path));
    } else {
        out->stderr_text = sv_from_cstr("");
    }

    return true;
}
