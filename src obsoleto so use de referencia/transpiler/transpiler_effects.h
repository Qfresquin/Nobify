#ifndef TRANSPILER_EFFECTS_H_
#define TRANSPILER_EFFECTS_H_

#include "sys_utils.h"
#include "toolchain_driver.h"

typedef enum {
    EFFECT_STATUS_OK = 0,
    EFFECT_STATUS_INVALID_INPUT,
    EFFECT_STATUS_EXEC_ERROR,
    EFFECT_STATUS_TIMEOUT,
    EFFECT_STATUS_EXIT_NONZERO,
} Effect_Status;

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
} Effect_Request;

typedef struct {
    Effect_Status status;
    int exit_code;
    bool timed_out;
    String_View stdout_text;
    String_View stderr_text;
} Effect_Result;

typedef struct {
    const Toolchain_Driver *driver;
    const Toolchain_Compile_Request *compile_request;
    const String_List *run_args;
    bool run_binary;
} Effect_Toolchain_Request;

typedef struct {
    Effect_Status status;
    bool compile_ok;
    String_View compile_output;
    int run_exit_code;
    String_View run_output;
} Effect_Toolchain_Result;

typedef enum {
    EFFECT_FS_ENSURE_PARENT_DIRS = 0,
    EFFECT_FS_WRITE_FILE_BYTES,
    EFFECT_FS_MKDIR,
    EFFECT_FS_DELETE_PATH_RECURSIVE,
    EFFECT_FS_COPY_ENTRY_TO_DESTINATION,
    EFFECT_FS_DOWNLOAD_TO_PATH,
    EFFECT_FS_DELETE_FILE,
    EFFECT_FS_COPY_DIRECTORY_RECURSIVE,
    EFFECT_FS_GET_FILE_TYPE,
    EFFECT_FS_COPY_FILE,
    EFFECT_FS_READ_DIR,
} Effect_Fs_Kind;

typedef struct {
    Effect_Fs_Kind kind;
    Arena *arena;
    String_View path;
    String_View path2;
    const char *bytes;
    size_t bytes_count;
    String_View url;
    Nob_File_Paths *out_paths;
} Effect_Fs_Request;

typedef struct {
    Effect_Status status;
    bool ok;
    Nob_File_Type file_type;
    String_View log_msg;
} Effect_Fs_Result;

bool effect_execute(const Effect_Request *req, Effect_Result *out);
bool effect_toolchain_invoke(const Effect_Toolchain_Request *req, Effect_Toolchain_Result *out);
bool effect_fs_invoke(const Effect_Fs_Request *req, Effect_Fs_Result *out);
const char *effect_status_name(Effect_Status status);

#endif // TRANSPILER_EFFECTS_H_
