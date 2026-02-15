#ifndef SYS_UTILS_H_
#define SYS_UTILS_H_

#include "build_model_types.h"

typedef struct {
    int exit_code;
    bool timed_out;
    String_View stdout_text;
    String_View stderr_text;
} Sys_Process_Result;

typedef struct {
    Arena *arena;
    String_View working_dir;
    unsigned long timeout_ms;
    const String_View *argv;
    size_t argv_count;
    bool capture_stdout;
    bool capture_stderr;
    bool strip_stdout_trailing_ws;
    bool strip_stderr_trailing_ws;
    String_View scratch_dir;
} Sys_Process_Request;

bool sys_ensure_parent_dirs(Arena *arena, String_View file_path);
String_View sys_read_file(Arena *arena, String_View path);
bool sys_write_file(String_View path, String_View content);
bool sys_write_file_bytes(String_View path, const char *data, size_t count);
bool sys_read_file_builder(String_View path, Nob_String_Builder *out);
bool sys_file_exists(String_View path);
bool sys_mkdir(String_View path);
bool sys_delete_file(String_View path);
bool sys_copy_file(String_View src, String_View dst);
bool sys_copy_directory_recursive(String_View src, String_View dst);
bool sys_read_dir(String_View dir, Nob_File_Paths *out);
Nob_File_Type sys_get_file_type(String_View path);
bool sys_delete_path_recursive(Arena *arena, String_View path);
bool sys_copy_entry_to_destination(Arena *arena, String_View src, String_View destination);
bool sys_download_to_path(Arena *arena, String_View url, String_View out_path, String_View *log_msg);
bool sys_run_process(const Sys_Process_Request *req, Sys_Process_Result *out);
int sys_run_shell_with_timeout(Arena *arena, String_View cmdline, unsigned long timeout_ms, bool *timed_out);
bool sys_path_has_separator(String_View path);
String_View sys_path_basename(String_View path);

#endif // SYS_UTILS_H_
