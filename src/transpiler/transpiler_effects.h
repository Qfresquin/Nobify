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

bool effect_execute(const Effect_Request *req, Effect_Result *out);
bool effect_toolchain_invoke(const Effect_Toolchain_Request *req, Effect_Toolchain_Result *out);
const char *effect_status_name(Effect_Status status);

#endif // TRANSPILER_EFFECTS_H_

