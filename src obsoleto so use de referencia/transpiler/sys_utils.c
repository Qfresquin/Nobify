#include "sys_utils.h"
#include "build_model.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "subprocess.h"
#include "tinydir.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <windows.h>
#else
#include <fcntl.h>
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

typedef struct {
    int exit_code_raw;
    bool timed_out;
    String_View stdout_text;
    String_View stderr_text;
} Sys_Subprocess_Run_Result;

static uint64_t sys_now_nanos(void) {
    return nob_nanos_since_unspecified_epoch();
}

static void sys_sleep_ms(unsigned long ms) {
    if (ms == 0) return;
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000UL));
#endif
}

static String_View sys_builder_to_arena_sv(Arena *arena, const String_Builder *sb) {
    if (!arena) return sv_from_cstr("");
    const char *src = (sb && sb->items) ? sb->items : "";
    size_t count = sb ? sb->count : 0;
    const char *dup = arena_strndup(arena, src, count);
    if (!dup) return sv_from_cstr("");
    return sv_from_cstr(dup);
}

static const char *sys_temp_path_for_tinydir(String_View path) {
#if defined(_WIN32)
    char *normalized = nob_temp_strndup(path.data ? path.data : "", path.count);
    if (!normalized) return "";
    for (size_t i = 0; i < path.count; i++) {
        if (normalized[i] == '/') normalized[i] = '\\';
    }
    return normalized;
#else
    return nob_temp_sv_to_cstr(path);
#endif
}

static size_t sys_subprocess_drain_stdout(struct subprocess_s *process, bool capture, String_Builder *out) {
    if (!process || !subprocess_stdout(process)) return 0;
    char chunk[4096];
    unsigned n = subprocess_read_stdout(process, chunk, (unsigned)sizeof(chunk));
    if (n > 0 && capture) sb_append_buf(out, chunk, (size_t)n);
    return (size_t)n;
}

static size_t sys_subprocess_drain_stderr(struct subprocess_s *process, bool capture, String_Builder *out) {
    if (!process || !subprocess_stderr(process)) return 0;
    char chunk[4096];
    unsigned n = subprocess_read_stderr(process, chunk, (unsigned)sizeof(chunk));
    if (n > 0 && capture) sb_append_buf(out, chunk, (size_t)n);
    return (size_t)n;
}

static void sys_subprocess_set_nonblocking_stream(FILE *stream) {
#if defined(_WIN32)
    (void)stream;
#else
    if (!stream) return;
    int fd = fileno(stream);
    if (fd < 0) return;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return;
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static bool sys_run_subprocess_argv(Arena *arena,
                                    String_View working_dir,
                                    const String_View *argv,
                                    size_t argv_count,
                                    unsigned long timeout_ms,
                                    bool capture_stdout,
                                    bool capture_stderr,
                                    Sys_Subprocess_Run_Result *out) {
    if (!arena || !argv || argv_count == 0 || !out) return false;

    memset(out, 0, sizeof(*out));
    out->exit_code_raw = 1;
    out->stdout_text = sv_from_cstr("");
    out->stderr_text = sv_from_cstr("");

    const char **command_line = arena_alloc_array(arena, const char *, argv_count + 1);
    if (!command_line) return false;

    for (size_t i = 0; i < argv_count; i++) {
        command_line[i] = sys_arena_sv_to_cstr(arena, argv[i]);
    }
    command_line[argv_count] = NULL;

    const char *old_cwd = NULL;
    bool cwd_switched = false;
    if (working_dir.count > 0) {
        old_cwd = arena_strdup(arena, nob_get_current_dir_temp());
        if (!old_cwd) return false;
        if (!nob_set_current_dir(sys_arena_sv_to_cstr(arena, working_dir))) return false;
        cwd_switched = true;
    }

    struct subprocess_s process = {0};
    int options = subprocess_option_inherit_environment |
                  subprocess_option_search_user_path |
                  subprocess_option_enable_async;
#if defined(_WIN32)
    options |= subprocess_option_no_window;
#endif

    int create_rc = subprocess_create(command_line, options, &process);

    if (cwd_switched && old_cwd) {
        (void)nob_set_current_dir(old_cwd);
    }
    if (create_rc != 0) return false;

    sys_subprocess_set_nonblocking_stream(subprocess_stdout(&process));
    sys_subprocess_set_nonblocking_stream(subprocess_stderr(&process));

    String_Builder stdout_sb = {0};
    String_Builder stderr_sb = {0};
    bool timed_out = false;
    bool alive_error = false;
    int exit_code_raw = 1;

    uint64_t started_ns = sys_now_nanos();
    const unsigned long poll_ms = 10;

    for (;;) {
        size_t drained = 0;
        drained += sys_subprocess_drain_stdout(&process, capture_stdout, &stdout_sb);
        drained += sys_subprocess_drain_stderr(&process, capture_stderr, &stderr_sb);

        int alive = subprocess_alive(&process);
        if (alive < 0) {
            alive_error = true;
            break;
        }
        if (!alive) break;

        if (timeout_ms > 0) {
            uint64_t elapsed_ms = (sys_now_nanos() - started_ns) / 1000000ULL;
            if (elapsed_ms >= (uint64_t)timeout_ms) {
                timed_out = true;
                (void)subprocess_terminate(&process);
                break;
            }
        }

        if (drained == 0) {
            sys_sleep_ms(poll_ms);
        }
    }

    if (alive_error) {
        (void)subprocess_terminate(&process);
    }

    int join_rc = subprocess_join(&process, &exit_code_raw);
    if (timed_out) exit_code_raw = 124;

    for (;;) {
        size_t drained = 0;
        drained += sys_subprocess_drain_stdout(&process, capture_stdout, &stdout_sb);
        drained += sys_subprocess_drain_stderr(&process, capture_stderr, &stderr_sb);
        if (drained == 0) break;
    }

    int destroy_rc = subprocess_destroy(&process);

    out->timed_out = timed_out;
    out->exit_code_raw = exit_code_raw;
    out->stdout_text = capture_stdout ? sys_builder_to_arena_sv(arena, &stdout_sb) : sv_from_cstr("");
    out->stderr_text = capture_stderr ? sys_builder_to_arena_sv(arena, &stderr_sb) : sv_from_cstr("");

    nob_sb_free(stdout_sb);
    nob_sb_free(stderr_sb);

    return (join_rc == 0) && (destroy_rc == 0) && !alive_error;
}

static char *sys_escape_powershell_single_quoted(Arena *arena, String_View value) {
    if (!arena) return NULL;
    size_t extra = 0;
    for (size_t i = 0; i < value.count; i++) {
        if (value.data[i] == '\'') extra++;
    }
    char *out = arena_alloc(arena, value.count + extra + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < value.count; i++) {
        char c = value.data[i];
        if (c == '\'') {
            out[j++] = '\'';
            out[j++] = '\'';
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return out;
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

bool sys_write_file_bytes(String_View path, const char *data, size_t count) {
    if (path.count == 0) return false;
    return nob_write_entire_file(nob_temp_sv_to_cstr(path), data ? data : "", count);
}

bool sys_read_file_builder(String_View path, Nob_String_Builder *out) {
    if (!out || path.count == 0) return false;
    return nob_read_entire_file(nob_temp_sv_to_cstr(path), out);
}

bool sys_file_exists(String_View path) {
    if (path.count == 0) return false;
    return nob_file_exists(nob_temp_sv_to_cstr(path));
}

bool sys_mkdir(String_View path) {
    if (path.count == 0) return false;
    return nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(path));
}

bool sys_delete_file(String_View path) {
    if (path.count == 0) return false;
    return nob_delete_file(nob_temp_sv_to_cstr(path));
}

bool sys_copy_file(String_View src, String_View dst) {
    if (src.count == 0 || dst.count == 0) return false;
    return nob_copy_file(nob_temp_sv_to_cstr(src), nob_temp_sv_to_cstr(dst));
}

bool sys_copy_directory_recursive(String_View src, String_View dst) {
    if (src.count == 0 || dst.count == 0) return false;
    return nob_copy_directory_recursively(nob_temp_sv_to_cstr(src), nob_temp_sv_to_cstr(dst));
}

bool sys_read_dir(String_View dir, Nob_File_Paths *out) {
    if (!out || dir.count == 0) return false;

    const char *dir_c = sys_temp_path_for_tinydir(dir);
    tinydir_dir tiny_dir = {0};
    if (tinydir_open(&tiny_dir, dir_c) != 0) return false;

    nob_da_append(out, ".");
    nob_da_append(out, "..");

    while (tiny_dir.has_next) {
        tinydir_file file = {0};
        if (tinydir_readfile(&tiny_dir, &file) != 0) {
            tinydir_close(&tiny_dir);
            return false;
        }

        if (strcmp(file.name, ".") != 0 && strcmp(file.name, "..") != 0) {
            nob_da_append(out, nob_temp_strdup(file.name));
        }

        if (tinydir_next(&tiny_dir) != 0) {
            tinydir_close(&tiny_dir);
            return false;
        }
    }

    tinydir_close(&tiny_dir);
    return true;
}

Nob_File_Type sys_get_file_type(String_View path) {
    if (path.count == 0) return (Nob_File_Type)-1;

    tinydir_file file = {0};
    const char *path_c = sys_temp_path_for_tinydir(path);
    errno = 0;
    if (tinydir_file_open(&file, path_c) != 0) return (Nob_File_Type)-1;

    if (file.is_dir) return NOB_FILE_DIRECTORY;
    if (file.is_reg) return NOB_FILE_REGULAR;
#if !defined(_WIN32) && defined(S_ISLNK)
    if (S_ISLNK(file._s.st_mode)) return NOB_FILE_SYMLINK;
#endif
    return NOB_FILE_OTHER;
}

static bool sys_path_is_dot_or_dotdot(String_View path) {
    return nob_sv_eq(path, sv_from_cstr(".")) || nob_sv_eq(path, sv_from_cstr(".."));
}

bool sys_delete_path_recursive(Arena *arena, String_View path) {
    if (!arena || path.count == 0) return false;

    const char *path_c = nob_temp_sv_to_cstr(path);
    Nob_File_Type file_type = sys_get_file_type(path);
    if ((int)file_type < 0) return true;

    if (file_type == NOB_FILE_REGULAR || file_type == NOB_FILE_SYMLINK || file_type == NOB_FILE_OTHER) {
        return sys_delete_file(path);
    }

    if (file_type != NOB_FILE_DIRECTORY) return false;

    Nob_File_Paths children = {0};
    if (!sys_read_dir(path, &children)) return false;

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

    Nob_File_Type file_type = sys_get_file_type(src);
    if ((int)file_type < 0) return false;

    String_View final_dst = destination;
    if (file_type == NOB_FILE_REGULAR || file_type == NOB_FILE_SYMLINK || file_type == NOB_FILE_OTHER) {
        String_View name = sys_path_basename(src);
        final_dst = build_path_join(arena, destination, name);
        if (!sys_ensure_parent_dirs(arena, final_dst)) return false;
        return sys_copy_file(src, final_dst);
    }

    if (file_type == NOB_FILE_DIRECTORY) {
        String_View name = sys_path_basename(src);
        final_dst = build_path_join(arena, destination, name);
        if (!sys_mkdir(destination)) return false;
        return sys_copy_directory_recursive(src, final_dst);
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
        bool ok = sys_copy_file(src, out_path);
        if (log_msg) {
            *log_msg = ok ? sv_from_cstr("downloaded via file://")
                          : sv_from_cstr("failed to copy file:// source");
        }
        return ok;
    }

    Sys_Subprocess_Run_Result run = {0};
    bool ran = false;
#if defined(_WIN32)
    char *escaped_url = sys_escape_powershell_single_quoted(arena, url);
    char *escaped_out = sys_escape_powershell_single_quoted(arena, out_path);
    if (!escaped_url || !escaped_out) {
        if (log_msg) *log_msg = sv_from_cstr("download failed");
        return false;
    }

    const char *script = arena_strdup(arena,
        nob_temp_sprintf("try {(New-Object Net.WebClient).DownloadFile('%s','%s'); exit 0} catch { exit 1 }",
                         escaped_url,
                         escaped_out));
    if (!script) {
        if (log_msg) *log_msg = sv_from_cstr("download failed");
        return false;
    }

    String_View argv[] = {
        sv_from_cstr("powershell"),
        sv_from_cstr("-NoProfile"),
        sv_from_cstr("-Command"),
        sv_from_cstr(script),
    };
    ran = sys_run_subprocess_argv(arena,
                                  sv_from_cstr(""),
                                  argv,
                                  sizeof(argv) / sizeof(argv[0]),
                                  0,
                                  false,
                                  false,
                                  &run);
#else
    String_View argv[] = {
        sv_from_cstr("curl"),
        sv_from_cstr("-L"),
        sv_from_cstr("--fail"),
        sv_from_cstr("-o"),
        out_path,
        url,
    };
    ran = sys_run_subprocess_argv(arena,
                                  sv_from_cstr(""),
                                  argv,
                                  sizeof(argv) / sizeof(argv[0]),
                                  0,
                                  false,
                                  false,
                                  &run);
#endif

    bool ok = ran && !run.timed_out && run.exit_code_raw == 0;
    if (log_msg) {
        *log_msg = ok ? sv_from_cstr("downloaded via external tool")
                      : sv_from_cstr("download failed");
    }
    return ok;
}

int sys_run_shell_with_timeout(Arena *arena, String_View cmdline, unsigned long timeout_ms, bool *timed_out) {
    if (timed_out) *timed_out = false;
    if (!arena) return -1;

    Sys_Subprocess_Run_Result run = {0};

#if defined(_WIN32)
    static size_t s_sys_shell_script_counter = 0;
    s_sys_shell_script_counter++;

    String_View script_dir = sv_from_cstr(".cmk2nob_exec");
    (void)sys_mkdir(script_dir);
    const char *script_path_c = arena_strdup(
        arena,
        nob_temp_sprintf(".cmk2nob_exec\\shell_%zu.bat", s_sys_shell_script_counter));
    if (!script_path_c) return -1;
    String_View script_path = sv_from_cstr(script_path_c);

    String_Builder script = {0};
    sb_append_buf(&script, cmdline.data ? cmdline.data : "", cmdline.count);
    sb_append_cstr(&script, "\r\n");
    if (!sys_write_file_bytes(script_path, script.items ? script.items : "", script.count)) {
        nob_sb_free(script);
        return -1;
    }
    nob_sb_free(script);

    String_View argv[] = {
        sv_from_cstr("cmd.exe"),
        sv_from_cstr("/C"),
        script_path,
    };

    bool ran = sys_run_subprocess_argv(arena,
                                       sv_from_cstr(""),
                                       argv,
                                       sizeof(argv) / sizeof(argv[0]),
                                       timeout_ms,
                                       false,
                                       false,
                                       &run);
    if (sys_file_exists(script_path)) (void)sys_delete_file(script_path);
    if (!ran) return -1;
#else
    String_View argv[] = {
        sv_from_cstr("sh"),
        sv_from_cstr("-c"),
        cmdline,
    };

    bool ran = sys_run_subprocess_argv(arena,
                                       sv_from_cstr(""),
                                       argv,
                                       sizeof(argv) / sizeof(argv[0]),
                                       timeout_ms,
                                       false,
                                       false,
                                       &run);
    if (!ran) return -1;
#endif

    if (timed_out) *timed_out = run.timed_out;
    return run.exit_code_raw;
}

bool sys_run_process(const Sys_Process_Request *req, Sys_Process_Result *out) {
    if (!req || !req->arena || !out || !req->argv || req->argv_count == 0) return false;

    memset(out, 0, sizeof(*out));
    out->exit_code = 1;
    out->stdout_text = sv_from_cstr("");
    out->stderr_text = sv_from_cstr("");
    out->timed_out = false;

    String_View scratch_dir = req->scratch_dir;
    if (scratch_dir.count == 0) {
        scratch_dir = sv_from_cstr(".cmk2nob_exec");
    }
    String_View scratch_marker = build_path_join(req->arena, scratch_dir, sv_from_cstr(".keep"));
    (void)sys_ensure_parent_dirs(req->arena, scratch_marker);
    (void)sys_mkdir(scratch_dir);
    (void)scratch_dir;

    Sys_Subprocess_Run_Result run = {0};
    bool ran = sys_run_subprocess_argv(req->arena,
                                       req->working_dir,
                                       req->argv,
                                       req->argv_count,
                                       req->timeout_ms,
                                       req->capture_stdout,
                                       req->capture_stderr,
                                       &run);
    if (!ran) return false;

    out->timed_out = run.timed_out;
    out->exit_code = (run.exit_code_raw == 0 && !run.timed_out) ? 0 : 1;

    if (req->capture_stdout) {
        String_View text = run.stdout_text;
        if (req->strip_stdout_trailing_ws) text = sys_strip_trailing_ws(req->arena, text);
        out->stdout_text = text;
    } else {
        out->stdout_text = sv_from_cstr("");
    }

    if (req->capture_stderr) {
        String_View text = run.stderr_text;
        if (req->strip_stderr_trailing_ws) text = sys_strip_trailing_ws(req->arena, text);
        out->stderr_text = text;
    } else {
        out->stderr_text = sv_from_cstr("");
    }

    return true;
}
