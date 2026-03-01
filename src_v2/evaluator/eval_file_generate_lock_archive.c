#include "eval_file_internal.h"
#include "eval_expr.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <time.h>
#endif

enum {
    EVAL_FILE_LOCK_GUARD_PROCESS = 0,
    EVAL_FILE_LOCK_GUARD_FILE = 1,
    EVAL_FILE_LOCK_GUARD_FUNCTION = 2,
};

static ssize_t eval_file_lock_find(Evaluator_Context *ctx, String_View path) {
    if (!ctx) return -1;
    for (size_t i = 0; i < ctx->file_locks.count; i++) {
        if (eval_sv_key_eq(ctx->file_locks.items[i].path, path)) return (ssize_t)i;
    }
    return -1;
}

static void eval_file_lock_close_entry(Eval_File_Lock *lock) {
    if (!lock) return;
#if defined(_WIN32)
    HANDLE h = (HANDLE)lock->handle;
    if (h) CloseHandle(h);
    lock->handle = NULL;
#else
    if (lock->fd >= 0) {
        (void)flock(lock->fd, LOCK_UN);
        (void)close(lock->fd);
        lock->fd = -1;
    }
#endif
}

static void eval_file_lock_release_by_scope(Evaluator_Context *ctx, int guard_kind, size_t owner_depth) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->file_locks.count;) {
        Eval_File_Lock *lock = &ctx->file_locks.items[i];
        if (lock->guard_kind != guard_kind) {
            i++;
            continue;
        }

        size_t depth = (guard_kind == EVAL_FILE_LOCK_GUARD_FILE) ? lock->owner_file_depth : lock->owner_function_depth;
        if (depth < owner_depth) {
            i++;
            continue;
        }

        eval_file_lock_close_entry(lock);
        ctx->file_locks.items[i] = ctx->file_locks.items[ctx->file_locks.count - 1];
        ctx->file_locks.count--;
    }
}

void eval_file_lock_release_file_scope(Evaluator_Context *ctx, size_t owner_depth) {
    eval_file_lock_release_by_scope(ctx, EVAL_FILE_LOCK_GUARD_FILE, owner_depth);
}

void eval_file_lock_release_function_scope(Evaluator_Context *ctx, size_t owner_depth) {
    eval_file_lock_release_by_scope(ctx, EVAL_FILE_LOCK_GUARD_FUNCTION, owner_depth);
}

static bool file_parse_permission_token(mode_t *mode, String_View token) {
    if (!mode) return false;
    if (eval_sv_eq_ci_lit(token, "OWNER_READ")) {
#ifdef S_IRUSR
        *mode |= S_IRUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "OWNER_WRITE")) {
#ifdef S_IWUSR
        *mode |= S_IWUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "OWNER_EXECUTE")) {
#ifdef S_IXUSR
        *mode |= S_IXUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_READ")) {
#ifdef S_IRGRP
        *mode |= S_IRGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_WRITE")) {
#ifdef S_IWGRP
        *mode |= S_IWGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_EXECUTE")) {
#ifdef S_IXGRP
        *mode |= S_IXGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_READ")) {
#ifdef S_IROTH
        *mode |= S_IROTH;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_WRITE")) {
#ifdef S_IWOTH
        *mode |= S_IWOTH;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_EXECUTE")) {
#ifdef S_IXOTH
        *mode |= S_IXOTH;
#endif
        return true;
    }
    return false;
}

void eval_file_lock_cleanup(Evaluator_Context *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->file_locks.count; i++) {
        eval_file_lock_close_entry(&ctx->file_locks.items[i]);
    }
    ctx->file_locks.count = 0;
}

static bool eval_file_lock_add(Evaluator_Context *ctx, String_View path, int fd_or_dummy, int guard_kind) {
    if (!ctx) return false;
    if (!arena_da_reserve(ctx->event_arena,
                          (void**)&ctx->file_locks.items,
                          &ctx->file_locks.capacity,
                          sizeof(ctx->file_locks.items[0]),
                          ctx->file_locks.count + 1)) {
        return ctx_oom(ctx);
    }

    Eval_File_Lock lock = {0};
    lock.path = sv_copy_to_event_arena(ctx, path);
    lock.guard_kind = guard_kind;
    lock.owner_file_depth = ctx->file_eval_depth;
    lock.owner_function_depth = ctx->function_eval_depth;
#if defined(_WIN32)
    lock.handle = (void*)(uintptr_t)fd_or_dummy;
#else
    lock.fd = fd_or_dummy;
#endif
    ctx->file_locks.items[ctx->file_locks.count++] = lock;
    return true;
}

static void eval_file_lock_remove_at(Evaluator_Context *ctx, size_t idx) {
    if (!ctx || idx >= ctx->file_locks.count) return;
    ctx->file_locks.items[idx] = ctx->file_locks.items[ctx->file_locks.count - 1];
    ctx->file_locks.count--;
}

static String_View file_apply_newline_style_temp(Evaluator_Context *ctx, String_View in, String_View style) {
    if (!ctx) return nob_sv_from_cstr("");
    if (in.count == 0) return nob_sv_from_cstr("");

    const char *nl = "\n";
    if (eval_sv_eq_ci_lit(style, "DOS") || eval_sv_eq_ci_lit(style, "WIN32") || eval_sv_eq_ci_lit(style, "CRLF")) {
        nl = "\r\n";
    } else if (eval_sv_eq_ci_lit(style, "UNIX") || eval_sv_eq_ci_lit(style, "LF")) {
        nl = "\n";
    }
    size_t nl_len = strlen(nl);

    size_t cap = in.count * 2 + 1;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), cap);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t out = 0;
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (c == '\r') {
            if (i + 1 < in.count && in.data[i + 1] == '\n') i++;
            memcpy(buf + out, nl, nl_len);
            out += nl_len;
            continue;
        }
        if (c == '\n') {
            memcpy(buf + out, nl, nl_len);
            out += nl_len;
            continue;
        }
        buf[out++] = c;
    }
    buf[out] = '\0';
    return nob_sv_from_parts(buf, out);
}

static bool handle_file_generate(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    String_View output = nob_sv_from_cstr("");
    String_View input = nob_sv_from_cstr("");
    String_View content = nob_sv_from_cstr("");
    String_View condition = nob_sv_from_cstr("");
    String_View newline_style = nob_sv_from_cstr("");
    bool has_input = false;
    bool has_content = false;
    bool use_source_permissions = false;
    bool has_file_permissions = false;
    mode_t file_mode = 0;

    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "OUTPUT") && i + 1 < args.count) {
            output = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "INPUT") && i + 1 < args.count) {
            has_input = true;
            input = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "CONTENT") && i + 1 < args.count) {
            has_content = true;
            content = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "CONDITION") && i + 1 < args.count) {
            condition = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "NEWLINE_STYLE") && i + 1 < args.count) {
            newline_style = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "USE_SOURCE_PERMISSIONS")) {
            use_source_permissions = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "FILE_PERMISSIONS")) {
            has_file_permissions = true;
            while (i + 1 < args.count) {
                if (eval_sv_eq_ci_lit(args.items[i + 1], "OUTPUT") ||
                    eval_sv_eq_ci_lit(args.items[i + 1], "INPUT") ||
                    eval_sv_eq_ci_lit(args.items[i + 1], "CONTENT") ||
                    eval_sv_eq_ci_lit(args.items[i + 1], "CONDITION") ||
                    eval_sv_eq_ci_lit(args.items[i + 1], "NEWLINE_STYLE")) {
                    break;
                }
                i++;
                if (!file_parse_permission_token(&file_mode, args.items[i])) {
                    eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("file(GENERATE) unknown FILE_PERMISSIONS token"), args.items[i]);
                }
            }
        }
    }

    if (output.count == 0 || (has_input == has_content)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GENERATE) requires OUTPUT and exactly one of INPUT/CONTENT"),
                       nob_sv_from_cstr("Usage: file(GENERATE OUTPUT <out> INPUT <in>|CONTENT <txt> ...)"));
        return true;
    }

    if (condition.count > 0 && !eval_truthy(ctx, condition)) {
        return true;
    }

    String_View out_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, output, eval_file_current_bin_dir(ctx), &out_path)) return true;
    if (!eval_file_mkdir_p(ctx, svu_dirname(out_path))) return true;

    String_View final_content = content;
    struct stat in_stat = {0};
    bool have_input_stat = false;
    if (has_input) {
        String_View in_path = nob_sv_from_cstr("");
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, input, eval_file_current_src_dir(ctx), &in_path)) return true;
        char *in_c = eval_sv_to_cstr_temp(ctx, in_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, in_c, true);
        Nob_String_Builder sb = {0};
        if (!nob_read_entire_file(in_c, &sb)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(GENERATE) failed to read INPUT"), in_path);
            return true;
        }
        final_content = nob_sv_from_parts(sb.items, sb.count);
        if (stat(in_c, &in_stat) == 0) have_input_stat = true;
    }

    if (newline_style.count > 0) {
        final_content = file_apply_newline_style_temp(ctx, final_content, newline_style);
    }

    char *out_c = eval_sv_to_cstr_temp(ctx, out_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, true);
    if (!nob_write_entire_file(out_c, final_content.data, final_content.count)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GENERATE) failed to write OUTPUT"), out_path);
        return true;
    }

#if !defined(_WIN32)
    if (has_file_permissions && file_mode != 0) {
        (void)chmod(out_c, file_mode);
    } else if (use_source_permissions && have_input_stat) {
        (void)chmod(out_c, in_stat.st_mode & 0777);
    }
#endif
    return true;
}

static bool handle_file_lock(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 2) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(LOCK) requires path"), nob_sv_from_cstr(""));
        return true;
    }

    bool release = false;
    size_t timeout_sec = 0;
    bool has_timeout = false;
    int guard_kind = EVAL_FILE_LOCK_GUARD_PROCESS;
    String_View result_var = nob_sv_from_cstr("");
    for (size_t i = 2; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "RELEASE")) {
            release = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "TIMEOUT") && i + 1 < args.count) {
            has_timeout = eval_file_parse_size_sv(args.items[++i], &timeout_sec);
        } else if (eval_sv_eq_ci_lit(args.items[i], "RESULT_VARIABLE") && i + 1 < args.count) {
            result_var = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "GUARD") && i + 1 < args.count) {
            String_View guard = args.items[++i];
            if (eval_sv_eq_ci_lit(guard, "PROCESS")) guard_kind = EVAL_FILE_LOCK_GUARD_PROCESS;
            else if (eval_sv_eq_ci_lit(guard, "FILE")) guard_kind = EVAL_FILE_LOCK_GUARD_FILE;
            else if (eval_sv_eq_ci_lit(guard, "FUNCTION")) guard_kind = EVAL_FILE_LOCK_GUARD_FUNCTION;
            else {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(LOCK) invalid GUARD value"), guard);
                return true;
            }
        }
    }

    String_View lock_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_bin_dir(ctx), &lock_path)) return true;

    ssize_t existing = eval_file_lock_find(ctx, lock_path);
    if (release) {
        if (existing >= 0) {
            eval_file_lock_close_entry(&ctx->file_locks.items[existing]);
            eval_file_lock_remove_at(ctx, (size_t)existing);
        }
        if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("0"));
        return true;
    }

    if (existing >= 0) {
        if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("0"));
        return true;
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, lock_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, true);

#if defined(_WIN32)
    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                   nob_sv_from_cstr("file(LOCK) backend is not implemented on Windows"), lock_path);
    if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("1"));
    return true;
#else
    int fd = open(path_c, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("1"));
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(LOCK) failed to open lock file"), lock_path);
        return true;
    }

    bool ok = false;
    time_t start = time(NULL);
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            ok = true;
            break;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) break;
        if (has_timeout) {
            time_t now = time(NULL);
            if ((size_t)(now - start) >= timeout_sec) break;
        }
        struct timespec req = {.tv_sec = 0, .tv_nsec = 100000000L};
        nanosleep(&req, NULL);
    }

    if (!ok) {
        close(fd);
        if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("1"));
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(LOCK) failed to acquire lock"), lock_path);
        return true;
    }

    if (!eval_file_lock_add(ctx, lock_path, fd, guard_kind)) {
        flock(fd, LOCK_UN);
        close(fd);
        return true;
    }

    if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("0"));
    return true;
#endif
}

static bool handle_file_archive_create(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    String_View out = nob_sv_from_cstr("");
    String_View format = nob_sv_from_cstr("tar");
    String_View compression = nob_sv_from_cstr("NONE");
    SV_List paths = {0};

    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "OUTPUT") && i + 1 < args.count) {
            out = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "FORMAT") && i + 1 < args.count) {
            format = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "COMPRESSION") && i + 1 < args.count) {
            compression = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "PATHS")) {
            for (size_t j = i + 1; j < args.count; j++) {
                if (eval_sv_eq_ci_lit(args.items[j], "OUTPUT") ||
                    eval_sv_eq_ci_lit(args.items[j], "FORMAT") ||
                    eval_sv_eq_ci_lit(args.items[j], "COMPRESSION")) {
                    break;
                }
                if (!svu_list_push_temp(ctx, &paths, args.items[j])) return true;
                i = j;
            }
        }
    }

    if (out.count == 0 || paths.count == 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(ARCHIVE_CREATE) requires OUTPUT and PATHS"),
                       nob_sv_from_cstr("Usage: file(ARCHIVE_CREATE OUTPUT <archive> PATHS <path>...)"));
        return true;
    }
    bool format_tar_like = eval_sv_eq_ci_lit(format, "TAR") ||
                           eval_sv_eq_ci_lit(format, "PAXR") ||
                           eval_sv_eq_ci_lit(format, "PAX") ||
                           eval_sv_eq_ci_lit(format, "GNUTAR");
    bool format_zip = eval_sv_eq_ci_lit(format, "ZIP");
    if (!(format_tar_like || format_zip)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(ARCHIVE_CREATE) unsupported FORMAT in local backend"), format);
        return true;
    }

    String_View out_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, out, eval_file_current_bin_dir(ctx), &out_path)) return true;
    if (!eval_file_mkdir_p(ctx, svu_dirname(out_path))) return true;

    char *out_c = eval_sv_to_cstr_temp(ctx, out_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, true);

    char cwd_buf[4096] = {0};
#if defined(_WIN32)
    if (!_getcwd(cwd_buf, (int)sizeof(cwd_buf) - 1)) cwd_buf[0] = '\0';
#else
    if (!getcwd(cwd_buf, sizeof(cwd_buf) - 1)) cwd_buf[0] = '\0';
#endif
    size_t cwd_len = strlen(cwd_buf);
    for (size_t i = 0; i < cwd_len; i++) {
        if (cwd_buf[i] == '\\') cwd_buf[i] = '/';
    }

    SV_List mapped_paths = {0};
    for (size_t i = 0; i < paths.count; i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, paths.items[i], eval_file_current_src_dir(ctx), &p)) return true;
        String_View p_arg = p;
        if (cwd_len > 0 && p.count > cwd_len && memcmp(p.data, cwd_buf, cwd_len) == 0) {
            size_t off = cwd_len;
            if (off < p.count && (p.data[off] == '/' || p.data[off] == '\\')) off++;
            p_arg = nob_sv_from_parts(p.data + off, p.count - off);
        }
        if (!svu_list_push_temp(ctx, &mapped_paths, p_arg)) return true;
    }

    Nob_Cmd cmd = {0};
    if (format_zip) {
        nob_da_append(&cmd, "zip");
        nob_da_append(&cmd, "-r");
        nob_da_append(&cmd, out_c);
        for (size_t i = 0; i < mapped_paths.count; i++) {
            char *pc = eval_sv_to_cstr_temp(ctx, mapped_paths.items[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, pc, true);
            nob_da_append(&cmd, pc);
        }
    } else {
        nob_da_append(&cmd, "tar");
        if (compression.count == 0 || eval_sv_eq_ci_lit(compression, "NONE")) {
            nob_da_append(&cmd, "-cf");
        } else if (eval_sv_eq_ci_lit(compression, "GZIP")) {
            nob_da_append(&cmd, "-czf");
        } else if (eval_sv_eq_ci_lit(compression, "BZIP2")) {
            nob_da_append(&cmd, "-cjf");
        } else if (eval_sv_eq_ci_lit(compression, "XZ")) {
            nob_da_append(&cmd, "-cJf");
        } else if (eval_sv_eq_ci_lit(compression, "ZSTD")) {
            nob_da_append(&cmd, "--zstd");
            nob_da_append(&cmd, "-cf");
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(ARCHIVE_CREATE) unsupported COMPRESSION in local backend"), compression);
            return true;
        }
        nob_da_append(&cmd, out_c);
        if (eval_sv_eq_ci_lit(format, "PAXR")) nob_da_append(&cmd, "--format=pax");
        else if (eval_sv_eq_ci_lit(format, "PAX")) nob_da_append(&cmd, "--format=pax");
        else if (eval_sv_eq_ci_lit(format, "GNUTAR")) nob_da_append(&cmd, "--format=gnu");
        nob_da_append(&cmd, "--");
        for (size_t i = 0; i < mapped_paths.count; i++) {
            char *pc = eval_sv_to_cstr_temp(ctx, mapped_paths.items[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, pc, true);
            nob_da_append(&cmd, pc);
        }
    }

    if (!nob_cmd_run_sync(cmd)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(ARCHIVE_CREATE) failed to run tar backend"), out_path);
    }
    return true;
}

static bool handle_file_archive_extract(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    String_View in = nob_sv_from_cstr("");
    String_View dst = nob_sv_from_cstr("");
    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "INPUT") && i + 1 < args.count) in = args.items[++i];
        else if (eval_sv_eq_ci_lit(args.items[i], "DESTINATION") && i + 1 < args.count) dst = args.items[++i];
    }
    if (in.count == 0 || dst.count == 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(ARCHIVE_EXTRACT) requires INPUT and DESTINATION"),
                       nob_sv_from_cstr("Usage: file(ARCHIVE_EXTRACT INPUT <archive> DESTINATION <dir>)"));
        return true;
    }

    String_View in_path = nob_sv_from_cstr("");
    String_View dst_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, in, eval_file_current_src_dir(ctx), &in_path)) return true;
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, dst, eval_file_current_bin_dir(ctx), &dst_path)) return true;
    if (!eval_file_mkdir_p(ctx, dst_path)) return true;

    char *in_c = eval_sv_to_cstr_temp(ctx, in_path);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, in_c, true);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, true);

    Nob_Cmd cmd = {0};
    if (in_path.count >= 4 &&
        (eval_sv_eq_ci_lit(nob_sv_from_parts(in_path.data + in_path.count - 4, 4), ".zip"))) {
        nob_da_append(&cmd, "unzip");
        nob_da_append(&cmd, "-o");
        nob_da_append(&cmd, in_c);
        nob_da_append(&cmd, "-d");
        nob_da_append(&cmd, dst_c);
    } else {
        nob_da_append(&cmd, "tar");
        nob_da_append(&cmd, "-xf");
        nob_da_append(&cmd, in_c);
        nob_da_append(&cmd, "-C");
        nob_da_append(&cmd, dst_c);
    }
    if (!nob_cmd_run_sync(cmd)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(ARCHIVE_EXTRACT) failed to run tar backend"), in_path);
    }
    return true;
}

bool eval_file_handle_generate_lock_archive(Evaluator_Context *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || args.count == 0) return false;
    if (eval_sv_eq_ci_lit(args.items[0], "GENERATE")) return handle_file_generate(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "LOCK")) return handle_file_lock(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "ARCHIVE_CREATE")) return handle_file_archive_create(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "ARCHIVE_EXTRACT")) return handle_file_archive_extract(ctx, node, args);
    return false;
}
