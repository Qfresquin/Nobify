#include "eval_file_internal.h"
#include "eval_expr.h"
#include "eval_file_backend_archive.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
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

static bool eval_file_lock_add(Evaluator_Context *ctx, String_View path, intptr_t fd_or_dummy, int guard_kind) {
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
    lock.fd = (int)fd_or_dummy;
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

static bool file_generate_is_keyword(String_View t) {
    return eval_sv_eq_ci_lit(t, "OUTPUT") ||
           eval_sv_eq_ci_lit(t, "INPUT") ||
           eval_sv_eq_ci_lit(t, "CONTENT") ||
           eval_sv_eq_ci_lit(t, "CONDITION") ||
           eval_sv_eq_ci_lit(t, "TARGET") ||
           eval_sv_eq_ci_lit(t, "NO_SOURCE_PERMISSIONS") ||
           eval_sv_eq_ci_lit(t, "USE_SOURCE_PERMISSIONS") ||
           eval_sv_eq_ci_lit(t, "FILE_PERMISSIONS") ||
           eval_sv_eq_ci_lit(t, "NEWLINE_STYLE");
}

static bool file_generate_enqueue_job(Evaluator_Context *ctx, const Eval_File_Generate_Job *job) {
    if (!ctx || !job) return false;
    if (!arena_da_reserve(ctx->event_arena,
                          (void**)&ctx->file_generate_jobs.items,
                          &ctx->file_generate_jobs.capacity,
                          sizeof(ctx->file_generate_jobs.items[0]),
                          ctx->file_generate_jobs.count + 1)) {
        return ctx_oom(ctx);
    }
    ctx->file_generate_jobs.items[ctx->file_generate_jobs.count++] = *job;
    return true;
}

static bool file_read_content_temp(Evaluator_Context *ctx, String_View path, String_View *out_content, struct stat *out_st, bool *out_have_st) {
    if (!ctx || !out_content) return false;
    *out_content = nob_sv_from_cstr("");
    if (out_have_st) *out_have_st = false;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) return false;
    char *dup = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, dup, false);
    if (sb.count > 0) memcpy(dup, sb.items, sb.count);
    dup[sb.count] = '\0';
    *out_content = nob_sv_from_parts(dup, sb.count);
    nob_sb_free(sb);

    if (out_have_st && out_st && stat(path_c, out_st) == 0) *out_have_st = true;
    return true;
}

static bool file_path_content_same(Evaluator_Context *ctx, String_View path, String_View content, bool *out_same) {
    if (!ctx || !out_same) return false;
    *out_same = false;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) return true;
    String_View current = nob_sv_from_parts(sb.items, sb.count);
    *out_same = nob_sv_eq(current, content);
    nob_sb_free(sb);
    return true;
}

static bool handle_file_generate(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    Eval_File_Generate_Job job = {0};
    mode_t parsed_mode = 0;
    job.origin = o;
    job.command_name = node->as.cmd.name;

    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "OUTPUT")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(GENERATE) OUTPUT requires a value"), args.items[i]);
                return true;
            }
            String_View out_path = nob_sv_from_cstr("");
            if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[++i], eval_file_current_bin_dir(ctx), &out_path)) return true;
            job.output_path = sv_copy_to_event_arena(ctx, out_path);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "INPUT")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(GENERATE) INPUT requires a value"), args.items[i]);
                return true;
            }
            String_View in_path = nob_sv_from_cstr("");
            if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[++i], eval_file_current_src_dir(ctx), &in_path)) return true;
            job.has_input = true;
            job.input_path = sv_copy_to_event_arena(ctx, in_path);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "CONTENT")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(GENERATE) CONTENT requires a value"), args.items[i]);
                return true;
            }
            job.has_content = true;
            job.content = sv_copy_to_event_arena(ctx, args.items[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "CONDITION")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(GENERATE) CONDITION requires a value"), args.items[i]);
                return true;
            }
            job.has_condition = true;
            job.condition = sv_copy_to_event_arena(ctx, args.items[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "TARGET")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(GENERATE) TARGET requires a value"), args.items[i]);
                return true;
            }
            job.has_target = true;
            job.target = sv_copy_to_event_arena(ctx, args.items[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "NEWLINE_STYLE")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(GENERATE) NEWLINE_STYLE requires a value"), args.items[i]);
                return true;
            }
            job.has_newline_style = true;
            job.newline_style = sv_copy_to_event_arena(ctx, args.items[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "USE_SOURCE_PERMISSIONS")) {
            job.use_source_permissions = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "NO_SOURCE_PERMISSIONS")) {
            job.no_source_permissions = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "FILE_PERMISSIONS")) {
            job.has_file_permissions = true;
            while (i + 1 < args.count && !file_generate_is_keyword(args.items[i + 1])) {
                i++;
                if (!file_parse_permission_token(&parsed_mode, args.items[i])) {
                    eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("file(GENERATE) unknown FILE_PERMISSIONS token"), args.items[i]);
                }
            }
            continue;
        }

        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GENERATE) received unexpected argument"), args.items[i]);
        return true;
    }

    if (job.output_path.count == 0 || (job.has_input == job.has_content)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GENERATE) requires OUTPUT and exactly one of INPUT/CONTENT"),
                       nob_sv_from_cstr("Usage: file(GENERATE OUTPUT <out> INPUT <in>|CONTENT <txt> ...)"));
        return true;
    }
    if (job.use_source_permissions && job.no_source_permissions) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GENERATE) cannot combine USE_SOURCE_PERMISSIONS and NO_SOURCE_PERMISSIONS"),
                       nob_sv_from_cstr(""));
        return true;
    }

    job.file_mode = (unsigned int)parsed_mode;
    if (!file_generate_enqueue_job(ctx, &job)) return true;
    return true;
}

typedef struct {
    String_View path;
    String_View content;
} File_Generate_Output_Seen;

bool eval_file_generate_flush(Evaluator_Context *ctx) {
    if (!ctx) return false;
    if (ctx->file_generate_jobs.count == 0) return true;

    File_Generate_Output_Seen *seen = NULL;
    size_t seen_count = 0;
    size_t seen_cap = 0;

    for (size_t i = 0; i < ctx->file_generate_jobs.count; i++) {
        const Eval_File_Generate_Job *job = &ctx->file_generate_jobs.items[i];
        if (job->has_condition && !eval_truthy(ctx, job->condition)) continue;
        if (job->has_target && !eval_target_known(ctx, job->target)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), job->command_name, job->origin,
                           nob_sv_from_cstr("file(GENERATE) TARGET does not name an existing target"),
                           job->target);
            continue;
        }

        String_View final_content = job->content;
        struct stat in_st = {0};
        bool have_input_st = false;
        if (job->has_input) {
            if (!file_read_content_temp(ctx, job->input_path, &final_content, &in_st, &have_input_st)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), job->command_name, job->origin,
                               nob_sv_from_cstr("file(GENERATE) failed to read INPUT"), job->input_path);
                continue;
            }
        }
        if (job->has_newline_style) {
            final_content = file_apply_newline_style_temp(ctx, final_content, job->newline_style);
        }

        ssize_t seen_idx = -1;
        for (size_t s = 0; s < seen_count; s++) {
            if (eval_sv_key_eq(seen[s].path, job->output_path)) {
                seen_idx = (ssize_t)s;
                break;
            }
        }
        if (seen_idx >= 0) {
            if (!nob_sv_eq(seen[seen_idx].content, final_content)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), job->command_name, job->origin,
                               nob_sv_from_cstr("file(GENERATE) duplicate OUTPUT requires identical content"),
                               job->output_path);
            }
            continue;
        }

        if (!arena_da_reserve(eval_temp_arena(ctx), (void**)&seen, &seen_cap, sizeof(seen[0]), seen_count + 1)) {
            ctx_oom(ctx);
            break;
        }
        seen[seen_count].path = job->output_path;
        seen[seen_count].content = final_content;
        seen_count++;

        if (!eval_file_mkdir_p(ctx, svu_dirname(job->output_path))) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), job->command_name, job->origin,
                           nob_sv_from_cstr("file(GENERATE) failed to create output directory"), job->output_path);
            continue;
        }

        bool same = false;
        if (!file_path_content_same(ctx, job->output_path, final_content, &same)) continue;
        if (same) continue;

        char *out_c = eval_sv_to_cstr_temp(ctx, job->output_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);
        if (!nob_write_entire_file(out_c, final_content.data, final_content.count)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), job->command_name, job->origin,
                           nob_sv_from_cstr("file(GENERATE) failed to write OUTPUT"), job->output_path);
            continue;
        }

#if !defined(_WIN32)
        if (job->has_file_permissions && job->file_mode != 0) {
            (void)chmod(out_c, (mode_t)job->file_mode);
        } else if (job->use_source_permissions && job->has_input && have_input_st) {
            (void)chmod(out_c, in_st.st_mode & 0777);
        } else if (job->no_source_permissions) {
            // Explicitly keep default backend permissions.
        }
#endif
    }

    ctx->file_generate_jobs.count = 0;
    return !ctx->oom;
}

static bool handle_file_lock(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 2) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(LOCK) requires path"), nob_sv_from_cstr(""));
        return true;
    }

    bool release = false;
    bool directory_lock = false;
    size_t timeout_sec = 0;
    bool has_timeout = false;
    int guard_kind = EVAL_FILE_LOCK_GUARD_PROCESS;
    String_View result_var = nob_sv_from_cstr("");
    for (size_t i = 2; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "RELEASE")) {
            release = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "DIRECTORY")) {
            directory_lock = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "TIMEOUT")) {
            if (i + 1 >= args.count || !eval_file_parse_size_sv(args.items[i + 1], &timeout_sec)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(LOCK) invalid TIMEOUT value"), args.items[i]);
                return true;
            }
            has_timeout = true;
            i++;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "RESULT_VARIABLE")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(LOCK) RESULT_VARIABLE requires a variable name"), args.items[i]);
                return true;
            }
            result_var = args.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "GUARD")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(LOCK) GUARD requires PROCESS|FILE|FUNCTION"), args.items[i]);
                return true;
            }
            String_View guard = args.items[++i];
            if (eval_sv_eq_ci_lit(guard, "PROCESS")) guard_kind = EVAL_FILE_LOCK_GUARD_PROCESS;
            else if (eval_sv_eq_ci_lit(guard, "FILE")) guard_kind = EVAL_FILE_LOCK_GUARD_FILE;
            else if (eval_sv_eq_ci_lit(guard, "FUNCTION")) guard_kind = EVAL_FILE_LOCK_GUARD_FUNCTION;
            else {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(LOCK) invalid GUARD value"), guard);
                return true;
            }
            continue;
        }
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(LOCK) received unexpected argument"), args.items[i]);
        return true;
    }

    String_View lock_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_bin_dir(ctx), &lock_path)) return true;
    if (directory_lock) {
        if (!eval_file_mkdir_p(ctx, lock_path)) {
            if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("failed to create lock directory"));
            else {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(LOCK) failed to create directory for DIRECTORY lock"), lock_path);
            }
            return true;
        }
        lock_path = eval_sv_path_join(eval_temp_arena(ctx), lock_path, nob_sv_from_cstr("cmake.lock"));
    }

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
        if (result_var.count > 0) {
            (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("lock already held by current evaluator context"));
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(LOCK) duplicate lock acquisition without RELEASE"), lock_path);
        }
        return true;
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, lock_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, true);

#if defined(_WIN32)
    HANDLE h = CreateFileA(path_c,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("failed to open lock file"));
        else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(LOCK) failed to open lock file"), lock_path);
        }
        return true;
    }

    bool ok = false;
    DWORD start_ms = GetTickCount();
    for (;;) {
        OVERLAPPED ov = {0};
        if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &ov) != 0) {
            ok = true;
            break;
        }
        DWORD err = GetLastError();
        if (err != ERROR_LOCK_VIOLATION) break;
        if (has_timeout) {
            DWORD elapsed = GetTickCount() - start_ms;
            if ((size_t)(elapsed / 1000) >= timeout_sec) break;
        }
        Sleep(100);
    }
    if (!ok) {
        CloseHandle(h);
        if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("failed to acquire lock"));
        else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(LOCK) failed to acquire lock"), lock_path);
        }
        return true;
    }

    if (!eval_file_lock_add(ctx, lock_path, (intptr_t)h, guard_kind)) {
        OVERLAPPED ov = {0};
        (void)UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ov);
        CloseHandle(h);
        return true;
    }
    if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("0"));
    return true;
#else
    int fd = open(path_c, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("failed to open lock file"));
        else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(LOCK) failed to open lock file"), lock_path);
        }
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
        if (result_var.count > 0) (void)eval_var_set(ctx, result_var, nob_sv_from_cstr("failed to acquire lock"));
        else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(LOCK) failed to acquire lock"), lock_path);
        }
        return true;
    }

    if (!eval_file_lock_add(ctx, lock_path, (intptr_t)fd, guard_kind)) {
        (void)flock(fd, LOCK_UN);
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
    long compression_level = 0;
    bool has_compression_level = false;
    long long mtime_epoch = 0;
    bool has_mtime = false;
    bool verbose = false;
    SV_List paths = {0};

    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "OUTPUT") && i + 1 < args.count) {
            out = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "FORMAT") && i + 1 < args.count) {
            format = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "COMPRESSION") && i + 1 < args.count) {
            compression = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "COMPRESSION_LEVEL") && i + 1 < args.count) {
            char *end = NULL;
            char *lvl = eval_sv_to_cstr_temp(ctx, args.items[++i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, lvl, true);
            compression_level = strtol(lvl, &end, 10);
            if (!end || *end != '\0') {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(ARCHIVE_CREATE) invalid COMPRESSION_LEVEL"), args.items[i]);
                return true;
            }
            has_compression_level = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "MTIME") && i + 1 < args.count) {
            char *end = NULL;
            char *tv = eval_sv_to_cstr_temp(ctx, args.items[++i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, tv, true);
            mtime_epoch = strtoll(tv, &end, 10);
            if (!end || *end != '\0') {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(ARCHIVE_CREATE) invalid MTIME epoch value"), args.items[i]);
                return true;
            }
            has_mtime = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "VERBOSE")) {
            verbose = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "PATHS")) {
            for (size_t j = i + 1; j < args.count; j++) {
                if (eval_sv_eq_ci_lit(args.items[j], "OUTPUT") ||
                    eval_sv_eq_ci_lit(args.items[j], "FORMAT") ||
                    eval_sv_eq_ci_lit(args.items[j], "COMPRESSION") ||
                    eval_sv_eq_ci_lit(args.items[j], "COMPRESSION_LEVEL") ||
                    eval_sv_eq_ci_lit(args.items[j], "MTIME") ||
                    eval_sv_eq_ci_lit(args.items[j], "VERBOSE")) {
                    break;
                }
                if (!svu_list_push_temp(ctx, &paths, args.items[j])) return true;
                i = j;
            }
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(ARCHIVE_CREATE) received unexpected argument"), args.items[i]);
            return true;
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

    SV_List resolved_paths = {0};
    for (size_t i = 0; i < paths.count; i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, paths.items[i], eval_file_current_src_dir(ctx), &p)) return true;
        if (!svu_list_push_temp(ctx, &resolved_paths, p)) return true;
    }

    Eval_File_Archive_Create_Options bopt = {0};
    bopt.output = out_path;
    bopt.paths = resolved_paths;
    bopt.format = format;
    bopt.compression = compression;
    bopt.has_compression_level = has_compression_level;
    bopt.compression_level = compression_level;
    bopt.has_mtime = has_mtime;
    bopt.mtime_epoch = mtime_epoch;
    bopt.verbose = verbose;

    int rc_backend = 1;
    String_View backend_log = nob_sv_from_cstr("");
    bool backend_ok = eval_file_backend_archive_create(ctx, &bopt, &rc_backend, &backend_log);
    if (backend_ok) {
        if (rc_backend != 0) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(ARCHIVE_CREATE) failed in libarchive backend"),
                           backend_log.count > 0 ? backend_log : out_path);
        }
        return true;
    }

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
    for (size_t i = 0; i < resolved_paths.count; i++) {
        String_View p = resolved_paths.items[i];
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
    SV_List patterns = {0};
    bool list_only = false;
    bool verbose = false;
    bool touch = false;
    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "INPUT") && i + 1 < args.count) in = args.items[++i];
        else if (eval_sv_eq_ci_lit(args.items[i], "DESTINATION") && i + 1 < args.count) dst = args.items[++i];
        else if (eval_sv_eq_ci_lit(args.items[i], "PATTERNS")) {
            for (size_t j = i + 1; j < args.count; j++) {
                if (eval_sv_eq_ci_lit(args.items[j], "INPUT") ||
                    eval_sv_eq_ci_lit(args.items[j], "DESTINATION") ||
                    eval_sv_eq_ci_lit(args.items[j], "LIST_ONLY") ||
                    eval_sv_eq_ci_lit(args.items[j], "VERBOSE") ||
                    eval_sv_eq_ci_lit(args.items[j], "TOUCH")) {
                    break;
                }
                if (!svu_list_push_temp(ctx, &patterns, args.items[j])) return true;
                i = j;
            }
        } else if (eval_sv_eq_ci_lit(args.items[i], "LIST_ONLY")) list_only = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "VERBOSE")) verbose = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "TOUCH")) touch = true;
        else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(ARCHIVE_EXTRACT) received unexpected argument"), args.items[i]);
            return true;
        }
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
    if (!list_only && !eval_file_mkdir_p(ctx, dst_path)) return true;

    Eval_File_Archive_Extract_Options bopt = {0};
    bopt.input = in_path;
    bopt.destination = dst_path;
    bopt.patterns = patterns;
    bopt.list_only = list_only;
    bopt.verbose = verbose;
    bopt.touch = touch;

    int rc_backend = 1;
    String_View backend_log = nob_sv_from_cstr("");
    bool backend_ok = eval_file_backend_archive_extract(ctx, &bopt, &rc_backend, &backend_log);
    if (backend_ok) {
        if (rc_backend != 0) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(ARCHIVE_EXTRACT) failed in libarchive backend"),
                           backend_log.count > 0 ? backend_log : in_path);
        }
        return true;
    }

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
