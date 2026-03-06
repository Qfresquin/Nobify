#include "evaluator_internal.h"

#include "arena_dyn.h"
#include "stb_ds.h"
#include "subprocess.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
extern char **environ;
#endif

static double eval_process_now_seconds(void) {
    struct timespec ts = {0};
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void eval_process_sleep_millis(unsigned millis) {
#if defined(_WIN32)
    Sleep(millis);
#else
    usleep((useconds_t)millis * 1000u);
#endif
}

static String_View eval_process_sb_to_owned_sv(Evaluator_Context *ctx, Nob_String_Builder *sb) {
    if (!ctx || !sb || sb->count == 0) return nob_sv_from_cstr("");
    char *copy = arena_strndup(eval_temp_arena(ctx), sb->items, sb->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, nob_sv_from_cstr(""));
    return nob_sv_from_parts(copy, sb->count);
}

static String_View eval_process_env_key_sv_copy(Arena *arena, String_View name) {
    if (!arena || name.count == 0) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(arena, name.count + 1);
    if (!buf) return nob_sv_from_cstr("");
    for (size_t i = 0; i < name.count; i++) {
#if defined(_WIN32)
        buf[i] = (char)toupper((unsigned char)name.data[i]);
#else
        buf[i] = name.data[i];
#endif
    }
    buf[name.count] = '\0';
    return nob_sv_from_parts(buf, name.count);
}

static char *eval_process_env_key_cstr_temp(Evaluator_Context *ctx, const char *name) {
    if (!ctx || !name || name[0] == '\0') return NULL;
    String_View key = eval_process_env_key_sv_copy(eval_temp_arena(ctx), nob_sv_from_cstr(name));
    EVAL_OOM_RETURN_IF_NULL(ctx, key.data, NULL);
    return (char*)key.data;
}

static String_View eval_process_env_key_sv_temp(Evaluator_Context *ctx, String_View name) {
    if (!ctx || name.count == 0) return nob_sv_from_cstr("");
    String_View key = eval_process_env_key_sv_copy(eval_temp_arena(ctx), name);
    EVAL_OOM_RETURN_IF_NULL(ctx, key.data, nob_sv_from_cstr(""));
    return key;
}

static String_View eval_process_env_key_sv_event(Evaluator_Context *ctx, String_View name) {
    if (!ctx || name.count == 0) return nob_sv_from_cstr("");
    String_View key = eval_process_env_key_sv_copy(eval_event_arena(ctx), name);
    EVAL_OOM_RETURN_IF_NULL(ctx, key.data, nob_sv_from_cstr(""));
    return key;
}

static Eval_Process_Env_Entry *eval_process_env_find_sv(Evaluator_Context *ctx, String_View name) {
    if (!ctx || name.count == 0) return NULL;
    Eval_Process_State *process = eval_process_slice(ctx);
    if (!process || !process->env_overrides) return NULL;
    String_View lookup = eval_process_env_key_sv_temp(ctx, name);
    if (eval_should_stop(ctx)) return NULL;
    return stbds_shgetp_null(process->env_overrides, lookup.data);
}

static Eval_Process_Env_Entry *eval_process_env_find_cstr(Evaluator_Context *ctx, const char *name) {
    if (!ctx || !name || name[0] == '\0') return NULL;
    Eval_Process_State *process = eval_process_slice(ctx);
    if (!process || !process->env_overrides) return NULL;
    char *lookup = eval_process_env_key_cstr_temp(ctx, name);
    EVAL_OOM_RETURN_IF_NULL(ctx, lookup, NULL);
    return stbds_shgetp_null(process->env_overrides, lookup);
}

static const Eval_Process_Env_Entry *eval_process_env_find_cstr_const(const Evaluator_Context *ctx,
                                                                      const char *name) {
    if (!ctx || !name || name[0] == '\0') return NULL;
    if (!ctx->process_state.env_overrides) return NULL;

    Eval_Process_Env_Table entries = ctx->process_state.env_overrides;
    return stbds_shgetp_null(entries, (char*)name);
}

bool eval_process_env_set(Evaluator_Context *ctx, String_View name, String_View value) {
    if (!ctx || name.count == 0) return false;

    Eval_Process_State *process = eval_process_slice(ctx);
    Eval_Process_Env_Entry *entry = eval_process_env_find_sv(ctx, name);
    if (eval_should_stop(ctx)) return false;
    if (entry) {
        entry->value.text = sv_copy_to_event_arena(ctx, value);
        entry->value.is_set = true;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    String_View stable_key = eval_process_env_key_sv_event(ctx, name);
    if (eval_should_stop(ctx)) return false;

    Eval_Process_Env_Value stored = {
        .text = sv_copy_to_event_arena(ctx, value),
        .is_set = true,
    };
    if (eval_should_stop(ctx)) return false;

    Eval_Process_Env_Table entries = process->env_overrides;
    stbds_shput(entries, stable_key.data, stored);
    process->env_overrides = entries;
    return true;
}

bool eval_process_env_unset(Evaluator_Context *ctx, String_View name) {
    if (!ctx || name.count == 0) return false;

    Eval_Process_State *process = eval_process_slice(ctx);
    Eval_Process_Env_Entry *entry = eval_process_env_find_sv(ctx, name);
    if (eval_should_stop(ctx)) return false;
    if (entry) {
        entry->value.text = nob_sv_from_cstr("");
        entry->value.is_set = false;
        return true;
    }

    String_View stable_key = eval_process_env_key_sv_event(ctx, name);
    if (eval_should_stop(ctx)) return false;

    Eval_Process_Env_Value stored = {
        .text = nob_sv_from_cstr(""),
        .is_set = false,
    };

    Eval_Process_Env_Table entries = process->env_overrides;
    stbds_shput(entries, stable_key.data, stored);
    process->env_overrides = entries;
    return true;
}

String_View eval_process_cwd_temp(Evaluator_Context *ctx) {
    if (!ctx) return nob_sv_from_cstr("");

    char cwd_buf[4096] = {0};
#if defined(_WIN32)
    if (!_getcwd(cwd_buf, (int)sizeof(cwd_buf) - 1)) return nob_sv_from_cstr("");
#else
    if (!getcwd(cwd_buf, sizeof(cwd_buf) - 1)) return nob_sv_from_cstr("");
#endif
    return sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(cwd_buf));
}

static const char *eval_getenv_temp_platform(Evaluator_Context *ctx, const char *name) {
    if (!name || name[0] == '\0') return NULL;

#if defined(_WIN32)
    if (!ctx) return NULL;

    char *lookup_name = eval_process_env_key_cstr_temp(ctx, name);
    EVAL_OOM_RETURN_IF_NULL(ctx, lookup_name, NULL);

    SetLastError(ERROR_SUCCESS);
    DWORD needed = GetEnvironmentVariableA(lookup_name, NULL, 0);
    if (needed == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_ENVVAR_NOT_FOUND) return NULL;

        if (err == ERROR_SUCCESS) {
            char *empty = (char*)arena_alloc(eval_temp_arena(ctx), 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, empty, NULL);
            empty[0] = '\0';
            return empty;
        }
        return NULL;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)needed);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);

    DWORD written = GetEnvironmentVariableA(lookup_name, buf, needed);
    if (written >= needed) {
        char *retry = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)written + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, retry, NULL);
        DWORD retry_written = GetEnvironmentVariableA(lookup_name, retry, written + 1);
        if (retry_written == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return NULL;
        return retry;
    }

    if (written == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return NULL;
    return buf;
#else
    (void)ctx;
    return getenv(name);
#endif
}

const char *eval_getenv_temp(Evaluator_Context *ctx, const char *name) {
    if (!name || name[0] == '\0') return NULL;

    Eval_Process_Env_Entry *entry = eval_process_env_find_cstr(ctx, name);
    if (entry) {
        return entry->value.is_set ? entry->value.text.data : NULL;
    }
    if (eval_should_stop(ctx)) return NULL;
    return eval_getenv_temp_platform(ctx, name);
}

bool eval_has_env(Evaluator_Context *ctx, const char *name) {
    if (!name || name[0] == '\0') return false;

    Eval_Process_Env_Entry *entry = eval_process_env_find_cstr(ctx, name);
    if (entry) return entry->value.is_set;
    if (eval_should_stop(ctx)) return false;

#if defined(_WIN32)
    if (!ctx) return false;
    char *lookup_name = eval_process_env_key_cstr_temp(ctx, name);
    EVAL_OOM_RETURN_IF_NULL(ctx, lookup_name, false);
    SetLastError(ERROR_SUCCESS);
    DWORD needed = GetEnvironmentVariableA(lookup_name, NULL, 0);
    return (needed > 0) || (GetLastError() == ERROR_SUCCESS);
#else
    (void)ctx;
    return getenv(name) != NULL;
#endif
}

static bool eval_process_seen_key_contains(const SV_List *seen_keys, const char *key) {
    if (!seen_keys || !key) return false;
    String_View key_sv = nob_sv_from_cstr(key);
    for (size_t i = 0; i < arena_arr_len(*seen_keys); i++) {
        if (eval_sv_key_eq((*seen_keys)[i], key_sv)) return true;
    }
    return false;
}

static bool eval_process_make_env_assignment(Evaluator_Context *ctx,
                                             const char *key,
                                             String_View value,
                                             const char **out_assignment) {
    if (!ctx || !key || !out_assignment) return false;
    *out_assignment = NULL;

    size_t key_len = strlen(key);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), key_len + 1 + value.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    memcpy(buf, key, key_len);
    buf[key_len] = '=';
    if (value.count > 0) memcpy(buf + key_len + 1, value.data, value.count);
    buf[key_len + 1 + value.count] = '\0';
    *out_assignment = buf;
    return true;
}

static const char *eval_process_strdup_temp(Evaluator_Context *ctx, const char *text) {
    if (!ctx || !text) return NULL;

    size_t len = strlen(text);
    char *copy = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, NULL);
    memcpy(copy, text, len + 1);
    return copy;
}

static bool eval_process_env_append_effective(Evaluator_Context *ctx,
                                              const char *entry_text,
                                              const char ***io_envp,
                                              SV_List *seen_keys) {
    if (!ctx || !entry_text || !io_envp || !seen_keys) return false;

    const char *eq = strchr(entry_text, '=');
    if (!eq || eq == entry_text) {
        const char *copy = eval_process_strdup_temp(ctx, entry_text);
        EVAL_OOM_RETURN_IF_NULL(ctx, copy, false);
        return arena_arr_push(eval_temp_arena(ctx), *io_envp, copy);
    }

    String_View raw_key = nob_sv_from_parts(entry_text, (size_t)(eq - entry_text));
    String_View lookup_key = eval_process_env_key_sv_temp(ctx, raw_key);
    if (eval_should_stop(ctx)) return false;

    if (!arena_arr_push(eval_temp_arena(ctx), *seen_keys, lookup_key)) return false;

    const Eval_Process_Env_Entry *override =
        eval_process_env_find_cstr_const(ctx, lookup_key.data);
    if (override) {
        if (!override->value.is_set) return true;
        const char *assignment = NULL;
        if (!eval_process_make_env_assignment(ctx,
                                              override->key,
                                              override->value.text,
                                              &assignment)) {
            return false;
        }
        return arena_arr_push(eval_temp_arena(ctx), *io_envp, assignment);
    }

    const char *copy = eval_process_strdup_temp(ctx, entry_text);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, false);
    return arena_arr_push(eval_temp_arena(ctx), *io_envp, copy);
}

static bool eval_process_collect_envp(Evaluator_Context *ctx, const char ***out_envp) {
    if (!ctx || !out_envp) return false;
    *out_envp = NULL;

    const Eval_Process_State *process = eval_process_slice_const(ctx);
    if (!process || stbds_shlen(process->env_overrides) == 0) return true;

    const char **envp = NULL;
    SV_List seen_keys = NULL;

#if defined(_WIN32)
    LPCH env_block = GetEnvironmentStringsA();
    if (!env_block) return true;
    for (LPCH cursor = env_block; *cursor != '\0'; cursor += strlen(cursor) + 1) {
        if (!eval_process_env_append_effective(ctx, cursor, &envp, &seen_keys)) {
            FreeEnvironmentStringsA(env_block);
            return false;
        }
    }
    FreeEnvironmentStringsA(env_block);
#else
    for (char **cursor = environ; cursor && *cursor; ++cursor) {
        if (!eval_process_env_append_effective(ctx, *cursor, &envp, &seen_keys)) return false;
    }
#endif

    for (ptrdiff_t i = 0; i < stbds_shlen(process->env_overrides); i++) {
        const Eval_Process_Env_Entry *entry = &process->env_overrides[i];
        if (!entry->key) continue;
        if (eval_process_seen_key_contains(&seen_keys, entry->key)) continue;
        if (!entry->value.is_set) continue;

        const char *assignment = NULL;
        if (!eval_process_make_env_assignment(ctx, entry->key, entry->value.text, &assignment)) {
            return false;
        }
        if (!arena_arr_push(eval_temp_arena(ctx), envp, assignment)) return false;
    }

    if (!arena_arr_push(eval_temp_arena(ctx), envp, NULL)) return false;
    *out_envp = envp;
    return true;
}

bool eval_process_run_capture(Evaluator_Context *ctx,
                              const Eval_Process_Run_Request *req,
                              Eval_Process_Run_Result *out) {
    if (!ctx || !req || !out || arena_arr_len(req->argv) == 0) return false;

    *out = (Eval_Process_Run_Result){
        .result_text = nob_sv_from_cstr("1"),
    };

    const char **argv = arena_alloc_array(eval_temp_arena(ctx), const char *, arena_arr_len(req->argv) + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, argv, false);
    for (size_t i = 0; i < arena_arr_len(req->argv); i++) {
        argv[i] = eval_sv_to_cstr_temp(ctx, req->argv[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, argv[i], false);
    }
    argv[arena_arr_len(req->argv)] = NULL;

    const char **envp = NULL;
    if (!eval_process_collect_envp(ctx, &envp)) return false;

    bool changed_cwd = false;
    char old_cwd[4096] = {0};
    if (req->working_directory.count > 0) {
        const char *cwd_c = eval_sv_to_cstr_temp(ctx, req->working_directory);
        EVAL_OOM_RETURN_IF_NULL(ctx, cwd_c, false);
#if defined(_WIN32)
        if (!_getcwd(old_cwd, sizeof(old_cwd))) {
            out->result_text = nob_sv_from_cstr("failed to capture working directory");
            return true;
        }
        if (_chdir(cwd_c) != 0) {
            out->result_text = nob_sv_from_cstr("failed to enter WORKING_DIRECTORY");
            return true;
        }
#else
        if (!getcwd(old_cwd, sizeof(old_cwd))) {
            out->result_text = nob_sv_from_cstr("failed to capture working directory");
            return true;
        }
        if (chdir(cwd_c) != 0) {
            out->result_text = nob_sv_from_cstr("failed to enter WORKING_DIRECTORY");
            return true;
        }
#endif
        changed_cwd = true;
    }

    struct subprocess_s proc = {0};
    int options = subprocess_option_search_user_path | subprocess_option_enable_async;
#if defined(_WIN32)
    options |= subprocess_option_no_window;
#endif
    if (!envp) options |= subprocess_option_inherit_environment;

    int create_rc = envp ? subprocess_create_ex(argv, options, envp, &proc)
                         : subprocess_create(argv, options, &proc);
    if (create_rc != 0) {
        if (changed_cwd) {
#if defined(_WIN32)
            if (_chdir(old_cwd) != 0) {}
#else
            if (chdir(old_cwd) != 0) {}
#endif
        }
        out->result_text = nob_sv_from_cstr("process failed to start");
        return true;
    }
    out->started = true;

    if (changed_cwd) {
#if defined(_WIN32)
        if (_chdir(old_cwd) != 0) {
            (void)subprocess_terminate(&proc);
            (void)subprocess_join(&proc, NULL);
            (void)subprocess_destroy(&proc);
            out->result_text = nob_sv_from_cstr("failed to restore working directory");
            out->started = false;
            return true;
        }
#else
        if (chdir(old_cwd) != 0) {
            (void)subprocess_terminate(&proc);
            (void)subprocess_join(&proc, NULL);
            (void)subprocess_destroy(&proc);
            out->result_text = nob_sv_from_cstr("failed to restore working directory");
            out->started = false;
            return true;
        }
#endif
    }

    FILE *child_stdin = subprocess_stdin(&proc);
    if (child_stdin) {
        if (req->stdin_data.count > 0) {
            size_t written = fwrite(req->stdin_data.data, 1, req->stdin_data.count, child_stdin);
            if (written != req->stdin_data.count) {
                fclose(child_stdin);
                proc.stdin_file = NULL;
                (void)subprocess_terminate(&proc);
                (void)subprocess_join(&proc, NULL);
                (void)subprocess_destroy(&proc);
                out->result_text = nob_sv_from_cstr("failed to write process input");
                out->started = false;
                return true;
            }
        }
        fclose(child_stdin);
        proc.stdin_file = NULL;
    }

    Nob_String_Builder out_sb = {0};
    Nob_String_Builder err_sb = {0};
    double deadline = 0.0;
    if (req->has_timeout) deadline = eval_process_now_seconds() + req->timeout_seconds;

    for (;;) {
        char buf[512];
        unsigned n_out = subprocess_read_stdout(&proc, buf, sizeof(buf));
        if (n_out > 0) nob_sb_append_buf(&out_sb, buf, (size_t)n_out);

        unsigned n_err = subprocess_read_stderr(&proc, buf, sizeof(buf));
        if (n_err > 0) nob_sb_append_buf(&err_sb, buf, (size_t)n_err);

        if (ctx->oom) {
            nob_sb_free(out_sb);
            nob_sb_free(err_sb);
            (void)subprocess_terminate(&proc);
            (void)subprocess_join(&proc, NULL);
            (void)subprocess_destroy(&proc);
            return false;
        }

        int alive = subprocess_alive(&proc);
        if (req->has_timeout && alive && eval_process_now_seconds() >= deadline) {
            out->timed_out = true;
            (void)subprocess_terminate(&proc);
            alive = subprocess_alive(&proc);
        }

        if (!alive && n_out == 0 && n_err == 0) break;
        if (n_out == 0 && n_err == 0) eval_process_sleep_millis(10);
    }

    int exit_code = 1;
    if (subprocess_join(&proc, &exit_code) != 0) {
        nob_sb_free(out_sb);
        nob_sb_free(err_sb);
        (void)subprocess_destroy(&proc);
        out->result_text = nob_sv_from_cstr("failed to wait for process");
        out->started = false;
        return true;
    }
    (void)subprocess_destroy(&proc);

    out->exit_code = exit_code;
    out->stdout_text = eval_process_sb_to_owned_sv(ctx, &out_sb);
    out->stderr_text = eval_process_sb_to_owned_sv(ctx, &err_sb);
    nob_sb_free(out_sb);
    nob_sb_free(err_sb);
    if (eval_should_stop(ctx)) return false;

    if (out->timed_out) {
        out->result_text = nob_sv_from_cstr("Process terminated due to timeout");
    } else {
        char *result_buf = arena_alloc(eval_temp_arena(ctx), 32);
        EVAL_OOM_RETURN_IF_NULL(ctx, result_buf, false);
        int n = snprintf(result_buf, 32, "%d", exit_code);
        if (n < 0 || n >= 32) return ctx_oom(ctx);
        out->result_text = nob_sv_from_parts(result_buf, (size_t)n);
    }

    return true;
}

bool eval_process_run_nob_cmd_capture(Evaluator_Context *ctx,
                                      const Nob_Cmd *cmd,
                                      String_View working_directory,
                                      String_View stdin_data,
                                      Eval_Process_Run_Result *out) {
    if (!ctx || !cmd || !out || cmd->count == 0) return false;

    SV_List argv = NULL;
    for (size_t i = 0; i < cmd->count; i++) {
        if (!cmd->items[i]) continue;
        if (!arena_arr_push(eval_temp_arena(ctx), argv, nob_sv_from_cstr(cmd->items[i]))) {
            return ctx_oom(ctx);
        }
    }

    Eval_Process_Run_Request req = {
        .argv = argv,
        .working_directory = working_directory,
        .stdin_data = stdin_data,
    };
    return eval_process_run_capture(ctx, &req, out);
}
