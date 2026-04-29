#include "eval_file_internal.h"
#include "eval_expr.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    EVAL_FILE_LOCK_GUARD_PROCESS = 0,
    EVAL_FILE_LOCK_GUARD_FILE = 1,
    EVAL_FILE_LOCK_GUARD_FUNCTION = 2,
};

static ssize_t eval_file_lock_find(EvalExecContext *ctx, String_View path) {
    if (!ctx) return -1;
    for (size_t i = 0; i < arena_arr_len(ctx->file_state.file_locks); i++) {
        if (eval_sv_key_eq(ctx->file_state.file_locks[i].path, path)) return (ssize_t)i;
    }
    return -1;
}

static void eval_file_lock_close_entry(EvalExecContext *ctx, Eval_File_Lock *lock) {
    if (!ctx || !lock || !lock->has_token) return;
    (void)eval_service_lock_release(ctx, lock->token);
    lock->token = 0;
    lock->has_token = false;
}

static void eval_file_lock_release_by_scope(EvalExecContext *ctx, int guard_kind, size_t owner_depth) {
    if (!ctx) return;
    for (size_t i = 0; i < arena_arr_len(ctx->file_state.file_locks);) {
        Eval_File_Lock *lock = &ctx->file_state.file_locks[i];
        if (lock->guard_kind != guard_kind) {
            i++;
            continue;
        }

        size_t depth = (guard_kind == EVAL_FILE_LOCK_GUARD_FILE) ? lock->owner_file_depth : lock->owner_function_depth;
        if (depth < owner_depth) {
            i++;
            continue;
        }

        eval_file_lock_close_entry(ctx, lock);
        ctx->file_state.file_locks[i] = ctx->file_state.file_locks[arena_arr_len(ctx->file_state.file_locks) - 1];
        arena_arr_set_len(ctx->file_state.file_locks, arena_arr_len(ctx->file_state.file_locks) - 1);
    }
}

void eval_file_lock_release_file_scope(EvalExecContext *ctx, size_t owner_depth) {
    eval_file_lock_release_by_scope(ctx, EVAL_FILE_LOCK_GUARD_FILE, owner_depth);
}

void eval_file_lock_release_function_scope(EvalExecContext *ctx, size_t owner_depth) {
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

void eval_file_lock_cleanup(EvalExecContext *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < arena_arr_len(ctx->file_state.file_locks); i++) {
        eval_file_lock_close_entry(ctx, &ctx->file_state.file_locks[i]);
    }
    if (ctx->file_state.file_locks) {
        arena_arr_set_len(ctx->file_state.file_locks, 0);
    }
}

static bool eval_file_lock_add(EvalExecContext *ctx, String_View path, uintptr_t token, int guard_kind) {
    if (!ctx) return false;
    Eval_File_Lock lock = {0};
    lock.path = sv_copy_to_event_arena(ctx, path);
    lock.guard_kind = guard_kind;
    lock.owner_file_depth = ctx->file_eval_depth;
    lock.owner_function_depth = ctx->function_eval_depth;
    lock.token = token;
    lock.has_token = token != 0;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->file_state.file_locks, lock);
}

static void eval_file_lock_remove_at(EvalExecContext *ctx, size_t idx) {
    if (!ctx || idx >= arena_arr_len(ctx->file_state.file_locks)) return;
    ctx->file_state.file_locks[idx] = ctx->file_state.file_locks[arena_arr_len(ctx->file_state.file_locks) - 1];
    arena_arr_set_len(ctx->file_state.file_locks, arena_arr_len(ctx->file_state.file_locks) - 1);
}

static String_View file_apply_newline_style_temp(EvalExecContext *ctx, String_View in, String_View style) {
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

static bool file_generate_enqueue_job(EvalExecContext *ctx, const Eval_File_Generate_Job *job) {
    if (!ctx || !job) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->file_state.file_generate_jobs, *job);
}

static bool file_read_content_temp(EvalExecContext *ctx, String_View path, String_View *out_content, Eval_Fs_Stat *out_st, bool *out_have_st) {
    if (!ctx || !out_content) return false;
    *out_content = nob_sv_from_cstr("");
    if (out_have_st) *out_have_st = false;

    String_View read_content = nob_sv_from_cstr("");
    bool found = false;
    if (!eval_service_read_file(ctx, path, &read_content, &found) || !found) return false;
    char *dup = (char*)arena_alloc(eval_temp_arena(ctx), read_content.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, dup, false);
    if (read_content.count > 0) memcpy(dup, read_content.data, read_content.count);
    dup[read_content.count] = '\0';
    *out_content = nob_sv_from_parts(dup, read_content.count);

    if (out_have_st && out_st && eval_service_stat(ctx, path, true, out_st) && out_st->exists) {
        *out_have_st = true;
    }
    return true;
}

static bool file_path_content_same(EvalExecContext *ctx, String_View path, String_View content, bool *out_same) {
    if (!ctx || !out_same) return false;
    *out_same = false;

    String_View current = nob_sv_from_cstr("");
    bool found = false;
    if (!eval_service_read_file(ctx, path, &current, &found)) return false;
    if (!found) return true;
    *out_same = nob_sv_eq(current, content);
    return true;
}

static bool file_emit_replay_reject_marker(EvalExecContext *ctx,
                                           Cmake_Event_Origin origin,
                                           Event_Replay_Action_Kind kind) {
    String_View action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    return eval_begin_replay_action(ctx,
                                    origin,
                                    kind,
                                    EVENT_REPLAY_OPCODE_NONE,
                                    EVENT_REPLAY_PHASE_CONFIGURE,
                                    eval_current_binary_dir(ctx),
                                    &action_key);
}

static bool file_emit_replay_write_text(EvalExecContext *ctx,
                                        Cmake_Event_Origin origin,
                                        String_View output_path,
                                        String_View content,
                                        String_View mode_octal) {
    String_View action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_FILESYSTEM,
                                  EVENT_REPLAY_OPCODE_FS_WRITE_TEXT,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, output_path) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, content) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 1, mode_octal)) {
        return false;
    }
    return true;
}

static bool file_emit_replay_lock(EvalExecContext *ctx,
                                  Cmake_Event_Origin origin,
                                  Event_Replay_Opcode opcode,
                                  String_View lock_path) {
    String_View action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_HOST_EFFECT,
                                  opcode,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, lock_path)) {
        return false;
    }
    return true;
}

static bool file_emit_replay_archive_create(EvalExecContext *ctx,
                                            Cmake_Event_Origin origin,
                                            String_View output_path,
                                            SV_List inputs,
                                            long long mtime_epoch) {
    String_View action_key = nob_sv_from_cstr("");
    String_View mtime = nob_sv_from_cstr(nob_temp_sprintf("%lld", mtime_epoch));
    if (!ctx) return false;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_HOST_EFFECT,
                                  EVENT_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(inputs); ++i) {
        if (!eval_emit_replay_action_add_input(ctx, origin, action_key, inputs[i])) return false;
    }
    if (!eval_emit_replay_action_add_output(ctx, origin, action_key, output_path) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, mtime)) {
        return false;
    }
    return true;
}

static bool file_emit_replay_archive_extract(EvalExecContext *ctx,
                                             Cmake_Event_Origin origin,
                                             String_View input_path,
                                             String_View output_path) {
    String_View action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_HOST_EFFECT,
                                  EVENT_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key) ||
        !eval_emit_replay_action_add_input(ctx, origin, action_key, input_path) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, output_path)) {
        return false;
    }
    return true;
}

static bool file_replay_is_internal_fetchcontent_extract(EvalExecContext *ctx) {
    return ctx &&
           arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable) > 0;
}

static bool handle_file_generate(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    Eval_File_Generate_Job job = {0};
    mode_t parsed_mode = 0;
    job.origin = o;
    job.command_name = node->as.cmd.name;

    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "OUTPUT")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(GENERATE) OUTPUT requires a value"), args[i]);
                return true;
            }
            String_View out_path = nob_sv_from_cstr("");
            if (!eval_file_resolve_project_scoped_path(ctx, node, o, args[++i], eval_file_current_bin_dir(ctx), &out_path)) return true;
            job.output_path = sv_copy_to_event_arena(ctx, out_path);
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "INPUT")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(GENERATE) INPUT requires a value"), args[i]);
                return true;
            }
            String_View in_path = nob_sv_from_cstr("");
            if (!eval_file_resolve_project_scoped_path(ctx, node, o, args[++i], eval_file_current_src_dir(ctx), &in_path)) return true;
            job.has_input = true;
            job.input_path = sv_copy_to_event_arena(ctx, in_path);
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "CONTENT")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(GENERATE) CONTENT requires a value"), args[i]);
                return true;
            }
            job.has_content = true;
            job.content = sv_copy_to_event_arena(ctx, args[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "CONDITION")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(GENERATE) CONDITION requires a value"), args[i]);
                return true;
            }
            job.has_condition = true;
            job.condition = sv_copy_to_event_arena(ctx, args[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "TARGET")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(GENERATE) TARGET requires a value"), args[i]);
                return true;
            }
            job.has_target = true;
            job.target = sv_copy_to_event_arena(ctx, args[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "NEWLINE_STYLE")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(GENERATE) NEWLINE_STYLE requires a value"), args[i]);
                return true;
            }
            job.has_newline_style = true;
            job.newline_style = sv_copy_to_event_arena(ctx, args[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "USE_SOURCE_PERMISSIONS")) {
            job.use_source_permissions = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "NO_SOURCE_PERMISSIONS")) {
            job.no_source_permissions = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "FILE_PERMISSIONS")) {
            job.has_file_permissions = true;
            while (i + 1 < arena_arr_len(args) && !file_generate_is_keyword(args[i + 1])) {
                i++;
                if (!file_parse_permission_token(&parsed_mode, args[i])) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(GENERATE) unknown FILE_PERMISSIONS token"), args[i]);
                }
            }
            continue;
        }

        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_file", nob_sv_from_cstr("file(GENERATE) received unexpected argument"), args[i]);
        return true;
    }

    if (job.output_path.count == 0 || (job.has_input == job.has_content)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(GENERATE) requires OUTPUT and exactly one of INPUT/CONTENT"), nob_sv_from_cstr("Usage: file(GENERATE OUTPUT <out> INPUT <in>|CONTENT <txt> ...)"));
        return true;
    }
    if (job.use_source_permissions && job.no_source_permissions) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_CONFLICTING_OPTIONS, "eval_file", nob_sv_from_cstr("file(GENERATE) cannot combine USE_SOURCE_PERMISSIONS and NO_SOURCE_PERMISSIONS"), nob_sv_from_cstr(""));
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

bool eval_file_generate_flush(EvalExecContext *ctx) {
    if (!ctx) return false;
    if (arena_arr_len(ctx->file_state.file_generate_jobs) == 0) return true;

    File_Generate_Output_Seen *seen = NULL;
    size_t seen_count = 0;
    for (size_t i = 0; i < arena_arr_len(ctx->file_state.file_generate_jobs); i++) {
        const Eval_File_Generate_Job *job = &ctx->file_state.file_generate_jobs[i];
        if (job->has_condition && !eval_truthy(ctx, job->condition)) continue;
        if (job->has_target && !eval_target_known(ctx, job->target)) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, nob_sv_from_cstr("eval_file"), job->command_name, job->origin, nob_sv_from_cstr("file(GENERATE) TARGET does not name an existing target"), job->target);
            continue;
        }

        String_View final_content = job->content;
        Eval_Fs_Stat in_st = {0};
        bool have_input_st = false;
        if (job->has_input) {
            if (!file_read_content_temp(ctx, job->input_path, &final_content, &in_st, &have_input_st)) {
                EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("eval_file"), job->command_name, job->origin, nob_sv_from_cstr("file(GENERATE) failed to read INPUT"), job->input_path);
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
                EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_DUPLICATE_ARGUMENT, nob_sv_from_cstr("eval_file"), job->command_name, job->origin, nob_sv_from_cstr("file(GENERATE) duplicate OUTPUT requires identical content"), job->output_path);
            }
            continue;
        }

        File_Generate_Output_Seen seen_entry = {
            .path = job->output_path,
            .content = final_content,
        };
        if (!EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), seen, seen_entry)) {
            break;
        }
        seen_count = arena_arr_len(seen);

        if (!eval_file_mkdir_p(ctx, svu_dirname(job->output_path))) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("eval_file"), job->command_name, job->origin, nob_sv_from_cstr("file(GENERATE) failed to create output directory"), job->output_path);
            continue;
        }

        String_View mode_octal = nob_sv_from_cstr("");
#if !defined(_WIN32)
        if (job->has_file_permissions && job->file_mode != 0) {
            mode_octal = eval_replay_mode_octal_temp(ctx, job->file_mode);
        } else if (job->use_source_permissions && job->has_input && have_input_st && in_st.have_mode) {
            mode_octal = eval_replay_mode_octal_temp(ctx, (unsigned int)(in_st.mode & 0777));
        }
#endif
        bool same = false;
        if (!file_path_content_same(ctx, job->output_path, final_content, &same)) continue;
        if (same) {
            (void)file_emit_replay_write_text(ctx, job->origin, job->output_path, final_content, mode_octal);
            continue;
        }

        if (!eval_write_text_file(ctx, job->output_path, final_content, false)) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("eval_file"), job->command_name, job->origin, nob_sv_from_cstr("file(GENERATE) failed to write OUTPUT"), job->output_path);
            continue;
        }

#if !defined(_WIN32)
        if (job->has_file_permissions && job->file_mode != 0) {
            (void)eval_service_chmod(ctx, job->output_path, job->file_mode, false);
        } else if (job->use_source_permissions && job->has_input && have_input_st && in_st.have_mode) {
            (void)eval_service_chmod(ctx, job->output_path, in_st.mode & 0777, false);
        } else if (job->no_source_permissions) {
            // Explicitly keep default backend permissions.
        }
#endif
        (void)file_emit_replay_write_text(ctx, job->origin, job->output_path, final_content, mode_octal);
    }

    arena_arr_set_len(ctx->file_state.file_generate_jobs, 0);
    return !ctx->oom;
}

static bool handle_file_lock(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(LOCK) requires path"), nob_sv_from_cstr(""));
        return true;
    }

    bool release = false;
    bool directory_lock = false;
    size_t timeout_sec = 0;
    bool has_timeout = false;
    int guard_kind = EVAL_FILE_LOCK_GUARD_PROCESS;
    String_View result_var = nob_sv_from_cstr("");
    for (size_t i = 2; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "RELEASE")) {
            release = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "DIRECTORY")) {
            directory_lock = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "TIMEOUT")) {
            if (i + 1 >= arena_arr_len(args) || !eval_file_parse_size_sv(args[i + 1], &timeout_sec)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(LOCK) invalid TIMEOUT value"), args[i]);
                return true;
            }
            has_timeout = true;
            i++;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "RESULT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(LOCK) RESULT_VARIABLE requires a variable name"), args[i]);
                return true;
            }
            result_var = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "GUARD")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(LOCK) GUARD requires PROCESS|FILE|FUNCTION"), args[i]);
                return true;
            }
            String_View guard = args[++i];
            if (eval_sv_eq_ci_lit(guard, "PROCESS")) guard_kind = EVAL_FILE_LOCK_GUARD_PROCESS;
            else if (eval_sv_eq_ci_lit(guard, "FILE")) guard_kind = EVAL_FILE_LOCK_GUARD_FILE;
            else if (eval_sv_eq_ci_lit(guard, "FUNCTION")) guard_kind = EVAL_FILE_LOCK_GUARD_FUNCTION;
            else {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(LOCK) invalid GUARD value"), guard);
                return true;
            }
            continue;
        }
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_file", nob_sv_from_cstr("file(LOCK) received unexpected argument"), args[i]);
        return true;
    }

    String_View lock_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args[1], eval_file_current_bin_dir(ctx), &lock_path)) return true;
    if (directory_lock) {
        if (!eval_file_mkdir_p(ctx, lock_path)) {
            if (result_var.count > 0) (void)eval_var_set_current(ctx, result_var, nob_sv_from_cstr("failed to create lock directory"));
            else {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(LOCK) failed to create directory for DIRECTORY lock"), lock_path);
            }
            return true;
        }
        lock_path = eval_sv_path_join(eval_temp_arena(ctx), lock_path, nob_sv_from_cstr("cmake.lock"));
    }

    ssize_t existing = eval_file_lock_find(ctx, lock_path);
    if (release) {
        if (existing >= 0) {
            eval_file_lock_close_entry(ctx, &ctx->file_state.file_locks[existing]);
            eval_file_lock_remove_at(ctx, (size_t)existing);
        }
        if (result_var.count > 0) (void)eval_var_set_current(ctx, result_var, nob_sv_from_cstr("0"));
        (void)file_emit_replay_lock(ctx, o, EVENT_REPLAY_OPCODE_HOST_LOCK_RELEASE, lock_path);
        return true;
    }

    if (existing >= 0) {
        if (result_var.count > 0) {
            (void)eval_var_set_current(ctx, result_var, nob_sv_from_cstr("lock already held by current evaluator context"));
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_DUPLICATE_ARGUMENT, "eval_file", nob_sv_from_cstr("file(LOCK) duplicate lock acquisition without RELEASE"), lock_path);
        }
        return true;
    }

    Eval_Host_Lock_Request lock_req = {
        .path = lock_path,
        .has_timeout = has_timeout,
        .timeout_sec = timeout_sec,
    };
    Eval_Host_Lock_Result lock_res = {0};
    if (!eval_service_lock_acquire(ctx, &lock_req, &lock_res)) return true;

    if (!lock_res.acquired) {
        String_View msg = lock_res.message.count > 0 ? lock_res.message : nob_sv_from_cstr("failed to acquire lock");
        if (result_var.count > 0) {
            (void)eval_var_set_current(ctx, result_var, msg);
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(LOCK) failed to acquire lock"), msg);
        }
        return true;
    }

    if (!eval_file_lock_add(ctx, lock_path, lock_res.token, guard_kind)) {
        (void)eval_service_lock_release(ctx, lock_res.token);
        return true;
    }

    if (result_var.count > 0) (void)eval_var_set_current(ctx, result_var, nob_sv_from_cstr("0"));
    (void)file_emit_replay_lock(ctx, o, EVENT_REPLAY_OPCODE_HOST_LOCK_ACQUIRE, lock_path);
    return true;
}

static bool handle_file_archive_create(EvalExecContext *ctx, const Node *node, SV_List args) {
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

    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "OUTPUT") && i + 1 < arena_arr_len(args)) {
            out = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "FORMAT") && i + 1 < arena_arr_len(args)) {
            format = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "COMPRESSION") && i + 1 < arena_arr_len(args)) {
            compression = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "COMPRESSION_LEVEL") && i + 1 < arena_arr_len(args)) {
            char *end = NULL;
            char *lvl = eval_sv_to_cstr_temp(ctx, args[++i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, lvl, true);
            compression_level = strtol(lvl, &end, 10);
            if (!end || *end != '\0') {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(ARCHIVE_CREATE) invalid COMPRESSION_LEVEL"), args[i]);
                return true;
            }
            has_compression_level = true;
        } else if (eval_sv_eq_ci_lit(args[i], "MTIME") && i + 1 < arena_arr_len(args)) {
            char *end = NULL;
            char *tv = eval_sv_to_cstr_temp(ctx, args[++i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, tv, true);
            mtime_epoch = strtoll(tv, &end, 10);
            if (!end || *end != '\0') {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(ARCHIVE_CREATE) invalid MTIME epoch value"), args[i]);
                return true;
            }
            has_mtime = true;
        } else if (eval_sv_eq_ci_lit(args[i], "VERBOSE")) {
            verbose = true;
        } else if (eval_sv_eq_ci_lit(args[i], "PATHS")) {
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (eval_sv_eq_ci_lit(args[j], "OUTPUT") ||
                    eval_sv_eq_ci_lit(args[j], "FORMAT") ||
                    eval_sv_eq_ci_lit(args[j], "COMPRESSION") ||
                    eval_sv_eq_ci_lit(args[j], "COMPRESSION_LEVEL") ||
                    eval_sv_eq_ci_lit(args[j], "MTIME") ||
                    eval_sv_eq_ci_lit(args[j], "VERBOSE")) {
                    break;
                }
                if (!svu_list_push_temp(ctx, &paths, args[j])) return true;
                i = j;
            }
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_file", nob_sv_from_cstr("file(ARCHIVE_CREATE) received unexpected argument"), args[i]);
            return true;
        }
    }

    if (out.count == 0 || arena_arr_len(paths) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(ARCHIVE_CREATE) requires OUTPUT and PATHS"), nob_sv_from_cstr("Usage: file(ARCHIVE_CREATE OUTPUT <archive> PATHS <path>...)"));
        return true;
    }
    bool format_tar_like = eval_sv_eq_ci_lit(format, "TAR") ||
                           eval_sv_eq_ci_lit(format, "PAXR") ||
                           eval_sv_eq_ci_lit(format, "PAX") ||
                           eval_sv_eq_ci_lit(format, "GNUTAR");
    bool format_zip = eval_sv_eq_ci_lit(format, "ZIP");
    if (!(format_tar_like || format_zip)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "eval_file", nob_sv_from_cstr("file(ARCHIVE_CREATE) unsupported FORMAT in local backend"), format);
        return true;
    }

    String_View out_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, out, eval_file_current_bin_dir(ctx), &out_path)) return true;
    if (!eval_file_mkdir_p(ctx, svu_dirname(out_path))) return true;

    SV_List resolved_paths = {0};
    for (size_t i = 0; i < arena_arr_len(paths); i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, paths[i], eval_file_current_src_dir(ctx), &p)) return true;
        if (!svu_list_push_temp(ctx, &resolved_paths, p)) return true;
    }

    Eval_Archive_Create_Request req = {
        .output = out_path,
        .paths = resolved_paths,
        .path_count = arena_arr_len(resolved_paths),
        .format = format,
        .compression = compression,
        .has_compression_level = has_compression_level,
        .compression_level = compression_level,
        .has_mtime = has_mtime,
        .mtime_epoch = mtime_epoch,
        .verbose = verbose,
    };
    Eval_Backend_Result backend = {0};
    if (!eval_service_archive_create(ctx, &req, &backend)) return true;
    if (backend.status_code != 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(ARCHIVE_CREATE) failed in archive backend"), backend.log.count > 0 ? backend.log : out_path);
        return true;
    }

    if (eval_sv_eq_ci_lit(format, "PAXR") &&
        (compression.count == 0 || eval_sv_eq_ci_lit(compression, "NONE")) &&
        !has_compression_level &&
        has_mtime) {
        (void)file_emit_replay_archive_create(ctx, o, out_path, resolved_paths, mtime_epoch);
    } else {
        (void)file_emit_replay_reject_marker(ctx, o, EVENT_REPLAY_ACTION_HOST_EFFECT);
    }
    return true;
}

static bool handle_file_archive_extract(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    String_View in = nob_sv_from_cstr("");
    String_View dst = nob_sv_from_cstr("");
    SV_List patterns = {0};
    bool list_only = false;
    bool verbose = false;
    bool touch = false;
    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "INPUT") && i + 1 < arena_arr_len(args)) in = args[++i];
        else if (eval_sv_eq_ci_lit(args[i], "DESTINATION") && i + 1 < arena_arr_len(args)) dst = args[++i];
        else if (eval_sv_eq_ci_lit(args[i], "PATTERNS")) {
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (eval_sv_eq_ci_lit(args[j], "INPUT") ||
                    eval_sv_eq_ci_lit(args[j], "DESTINATION") ||
                    eval_sv_eq_ci_lit(args[j], "LIST_ONLY") ||
                    eval_sv_eq_ci_lit(args[j], "VERBOSE") ||
                    eval_sv_eq_ci_lit(args[j], "TOUCH")) {
                    break;
                }
                if (!svu_list_push_temp(ctx, &patterns, args[j])) return true;
                i = j;
            }
        } else if (eval_sv_eq_ci_lit(args[i], "LIST_ONLY")) list_only = true;
        else if (eval_sv_eq_ci_lit(args[i], "VERBOSE")) verbose = true;
        else if (eval_sv_eq_ci_lit(args[i], "TOUCH")) touch = true;
        else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_file", nob_sv_from_cstr("file(ARCHIVE_EXTRACT) received unexpected argument"), args[i]);
            return true;
        }
    }
    if (in.count == 0 || dst.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(ARCHIVE_EXTRACT) requires INPUT and DESTINATION"), nob_sv_from_cstr("Usage: file(ARCHIVE_EXTRACT INPUT <archive> DESTINATION <dir>)"));
        return true;
    }

    String_View in_path = nob_sv_from_cstr("");
    String_View dst_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, in, eval_file_current_src_dir(ctx), &in_path)) return true;
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, dst, eval_file_current_bin_dir(ctx), &dst_path)) return true;
    if (!list_only && !eval_file_mkdir_p(ctx, dst_path)) return true;

    Eval_Archive_Extract_Request req = {
        .input = in_path,
        .destination = dst_path,
        .patterns = patterns,
        .pattern_count = arena_arr_len(patterns),
        .list_only = list_only,
        .verbose = verbose,
        .touch = touch,
    };
    Eval_Backend_Result backend = {0};
    if (!eval_service_archive_extract(ctx, &req, &backend)) return true;
    if (backend.status_code != 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(ARCHIVE_EXTRACT) failed in archive backend"), backend.log.count > 0 ? backend.log : in_path);
        return true;
    }

    if (!list_only &&
        !verbose &&
        !touch &&
        arena_arr_len(patterns) == 0 &&
        !(in_path.count >= 4 && eval_sv_eq_ci_lit(nob_sv_from_parts(in_path.data + in_path.count - 4, 4), ".zip"))) {
        (void)file_emit_replay_archive_extract(ctx, o, in_path, dst_path);
    } else if (!file_replay_is_internal_fetchcontent_extract(ctx)) {
        (void)file_emit_replay_reject_marker(ctx, o, EVENT_REPLAY_ACTION_HOST_EFFECT);
    }
    return true;
}

bool eval_file_handle_generate_lock_archive(EvalExecContext *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || arena_arr_len(args) == 0) return false;
    if (eval_sv_eq_ci_lit(args[0], "GENERATE")) return handle_file_generate(ctx, node, args);
    if (eval_sv_eq_ci_lit(args[0], "LOCK")) return handle_file_lock(ctx, node, args);
    if (eval_sv_eq_ci_lit(args[0], "ARCHIVE_CREATE")) return handle_file_archive_create(ctx, node, args);
    if (eval_sv_eq_ci_lit(args[0], "ARCHIVE_EXTRACT")) return handle_file_archive_extract(ctx, node, args);
    return false;
}
