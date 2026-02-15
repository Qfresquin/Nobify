#include "transpiler_effects.h"
#include <string.h>

const char *effect_status_name(Effect_Status status) {
    switch (status) {
        case EFFECT_STATUS_OK: return "ok";
        case EFFECT_STATUS_INVALID_INPUT: return "invalid_input";
        case EFFECT_STATUS_EXEC_ERROR: return "exec_error";
        case EFFECT_STATUS_TIMEOUT: return "timeout";
        case EFFECT_STATUS_EXIT_NONZERO: return "exit_nonzero";
        default: return "unknown";
    }
}

bool effect_execute(const Effect_Request *req, Effect_Result *out) {
    if (!out) return false;

    memset(out, 0, sizeof(*out));
    out->status = EFFECT_STATUS_INVALID_INPUT;
    out->exit_code = 1;
    out->stdout_text = sv_from_cstr("");
    out->stderr_text = sv_from_cstr("");

    if (!req || !req->arena || !req->argv || req->argv_count == 0) {
        return false;
    }

    Sys_Process_Request sys_req = {0};
    sys_req.arena = req->arena;
    sys_req.working_dir = req->working_dir;
    sys_req.timeout_ms = req->timeout_ms;
    sys_req.argv = req->argv;
    sys_req.argv_count = req->argv_count;
    sys_req.capture_stdout = req->capture_stdout;
    sys_req.capture_stderr = req->capture_stderr;
    sys_req.strip_stdout_trailing_ws = req->strip_stdout_trailing_ws;
    sys_req.strip_stderr_trailing_ws = req->strip_stderr_trailing_ws;
    sys_req.scratch_dir = req->scratch_dir;

    Sys_Process_Result sys_out = {0};
    bool ran = sys_run_process(&sys_req, &sys_out);
    out->exit_code = sys_out.exit_code;
    out->timed_out = sys_out.timed_out;
    out->stdout_text = sys_out.stdout_text;
    out->stderr_text = sys_out.stderr_text;

    if (!ran) {
        out->status = sys_out.timed_out ? EFFECT_STATUS_TIMEOUT : EFFECT_STATUS_EXEC_ERROR;
        return true;
    }
    out->status = (sys_out.exit_code == 0) ? EFFECT_STATUS_OK : EFFECT_STATUS_EXIT_NONZERO;
    return true;
}

bool effect_toolchain_invoke(const Effect_Toolchain_Request *req, Effect_Toolchain_Result *out) {
    if (!out) return false;

    memset(out, 0, sizeof(*out));
    out->status = EFFECT_STATUS_INVALID_INPUT;
    out->compile_output = sv_from_cstr("");
    out->run_output = sv_from_cstr("");
    out->run_exit_code = 1;

    if (!req || !req->driver || !req->compile_request) {
        return false;
    }

    Toolchain_Compile_Result compile_out = {0};
    bool invoked = false;
    if (req->run_binary) {
        int run_exit = 1;
        String_View run_output = sv_from_cstr("");
        invoked = toolchain_try_run(req->driver,
                                    req->compile_request,
                                    req->run_args,
                                    &compile_out,
                                    &run_exit,
                                    &run_output);
        out->run_exit_code = run_exit;
        out->run_output = run_output;
    } else {
        invoked = toolchain_try_compile(req->driver, req->compile_request, &compile_out);
        out->run_exit_code = 0;
    }

    out->compile_ok = compile_out.ok;
    out->compile_output = compile_out.output;

    if (!invoked) {
        out->status = EFFECT_STATUS_EXEC_ERROR;
        return true;
    }
    if (!compile_out.ok) {
        out->status = EFFECT_STATUS_EXIT_NONZERO;
        return true;
    }
    if (req->run_binary && out->run_exit_code != 0) {
        out->status = EFFECT_STATUS_EXIT_NONZERO;
        return true;
    }

    out->status = EFFECT_STATUS_OK;
    return true;
}

bool effect_fs_invoke(const Effect_Fs_Request *req, Effect_Fs_Result *out) {
    if (!out) return false;

    memset(out, 0, sizeof(*out));
    out->status = EFFECT_STATUS_INVALID_INPUT;
    out->file_type = (Nob_File_Type)-1;
    out->log_msg = sv_from_cstr("");

    if (!req) return false;

    switch (req->kind) {
        case EFFECT_FS_ENSURE_PARENT_DIRS:
            if (!req->arena) return false;
            out->ok = sys_ensure_parent_dirs(req->arena, req->path);
            break;
        case EFFECT_FS_WRITE_FILE_BYTES:
            out->ok = sys_write_file_bytes(req->path, req->bytes ? req->bytes : "", req->bytes_count);
            break;
        case EFFECT_FS_MKDIR:
            out->ok = sys_mkdir(req->path);
            break;
        case EFFECT_FS_DELETE_PATH_RECURSIVE:
            if (!req->arena) return false;
            out->ok = sys_delete_path_recursive(req->arena, req->path);
            break;
        case EFFECT_FS_COPY_ENTRY_TO_DESTINATION:
            if (!req->arena) return false;
            out->ok = sys_copy_entry_to_destination(req->arena, req->path, req->path2);
            break;
        case EFFECT_FS_DOWNLOAD_TO_PATH:
            if (!req->arena) return false;
            out->ok = sys_download_to_path(req->arena, req->url, req->path, &out->log_msg);
            break;
        case EFFECT_FS_DELETE_FILE:
            out->ok = sys_delete_file(req->path);
            break;
        case EFFECT_FS_COPY_DIRECTORY_RECURSIVE:
            out->ok = sys_copy_directory_recursive(req->path, req->path2);
            break;
        case EFFECT_FS_GET_FILE_TYPE:
            out->file_type = sys_get_file_type(req->path);
            out->ok = ((int)out->file_type >= 0);
            break;
        case EFFECT_FS_COPY_FILE:
            out->ok = sys_copy_file(req->path, req->path2);
            break;
        default:
            return false;
    }

    out->status = out->ok ? EFFECT_STATUS_OK : EFFECT_STATUS_EXEC_ERROR;
    return true;
}
