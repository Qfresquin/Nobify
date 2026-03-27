#ifndef TEST_EVALUATOR_V2_COMMON_H_
#define TEST_EVALUATOR_V2_COMMON_H_

#include "test_v2_assert.h"
#include "test_case_pack.h"
#include "test_snapshot_support.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "event_ir.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef Test_Case_Pack_Entry Evaluator_Case;

typedef struct {
    Evaluator_Case *items;
    size_t count;
    size_t capacity;
} Evaluator_Case_List;

typedef struct {
    Arena *arena;
    Arena *event_arena;
    Cmake_Event_Stream *stream;
    String_View source_dir;
    String_View binary_dir;
    const char *current_file;
    Eval_Exec_Mode exec_mode;
    const EvalServices *services;
    Eval_Compat_Profile compat_profile;
    EvalRegistry *registry;
} Eval_Test_Init;

typedef struct {
    EvalSession *session;
    EvalRegistry *registry;
    EvalExec_Request request;
    EvalRunResult last_run;
    Cmake_Event_Stream *stream;
    const char *current_file;
    bool owns_registry;
} Eval_Test_Runtime;

static Nob_String_Builder g_evaluator_captured_nob_logs = {0};
static Nob_Log_Handler *g_evaluator_prev_log_handler = NULL;
static EvalRegistry *g_evaluator_runtime_mutation_registry = NULL;

static Eval_Result eval_test_result_ok(void) {
    Eval_Result result = { .kind = EVAL_RESULT_OK };
    return result;
}

static Eval_Result eval_test_result_fatal(void) {
    Eval_Result result = { .kind = EVAL_RESULT_FATAL };
    return result;
}

static Eval_Result eval_test_result_from_bool(bool ok) {
    return ok ? eval_test_result_ok() : eval_test_result_fatal();
}

static void evaluator_capture_nob_log(Nob_Log_Level level, const char *fmt, va_list args) {
    char message[4096] = {0};
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(message, sizeof(message), fmt, copy);
    va_end(copy);
    if (n < 0) return;
    nob_sb_appendf(&g_evaluator_captured_nob_logs, "[%d] %s\n", (int)level, message);
}

static void evaluator_begin_nob_log_capture(void) {
    nob_sb_free(g_evaluator_captured_nob_logs);
    g_evaluator_captured_nob_logs = (Nob_String_Builder){0};
    g_evaluator_prev_log_handler = nob_get_log_handler();
    nob_set_log_handler(evaluator_capture_nob_log);
}

static void evaluator_end_nob_log_capture(void) {
    nob_set_log_handler(g_evaluator_prev_log_handler);
    g_evaluator_prev_log_handler = NULL;
}

static void evaluator_set_source_date_epoch_value(const char *value) {
#if defined(_WIN32)
    _putenv_s("SOURCE_DATE_EPOCH", value ? value : "");
#else
    if (value) {
        setenv("SOURCE_DATE_EPOCH", value, 1);
    } else {
        unsetenv("SOURCE_DATE_EPOCH");
    }
#endif
}

static bool evaluator_remove_link_like_path(const char *path) {
    if (!path) return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        if (RemoveDirectoryA(path)) return true;
    }
    if (DeleteFileA(path)) return true;
    if (RemoveDirectoryA(path)) return true;
    return false;
#else
    struct stat st = {0};
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        return rmdir(path) == 0;
    }
    return unlink(path) == 0;
#endif
}

static bool evaluator_prepare_symlink_escape_fixture(void) {
    const char *outside_dir = "../evaluator_symlink_outside";
    const char *outside_file = "../evaluator_symlink_outside/outside.txt";
    const char *inside_link = "temp_symlink_escape_link";

    if (!nob_mkdir_if_not_exists(outside_dir)) {
        nob_log(NOB_ERROR, "evaluator fixture: failed to create %s", outside_dir);
        return false;
    }
    if (!nob_write_entire_file(outside_file, "outside\n", 8)) {
        nob_log(NOB_ERROR, "evaluator fixture: failed to write %s", outside_file);
        return false;
    }

    if (!evaluator_remove_link_like_path(inside_link)) {
        nob_log(NOB_ERROR, "evaluator fixture: failed to clean %s", inside_link);
        return false;
    }

#if defined(_WIN32)
    DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (CreateSymbolicLinkA(inside_link, "..\\evaluator_symlink_outside",
                            flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }
    if (CreateSymbolicLinkA(inside_link, "..\\evaluator_symlink_outside", flags) != 0) {
        return true;
    }

    int mklink_rc = system("cmd /C mklink /J temp_symlink_escape_link ..\\evaluator_symlink_outside >NUL 2>NUL");
    if (mklink_rc == 0) return true;

    nob_log(NOB_ERROR, "evaluator fixture: failed to create symlink/junction at %s", inside_link);
    return false;
#else
    if (symlink("../evaluator_symlink_outside", inside_link) == 0) return true;
    if (errno == EEXIST) return true;

    nob_log(NOB_ERROR, "evaluator fixture: failed to create symlink %s -> %s: %s",
            inside_link, outside_dir, strerror(errno));
    return false;
#endif
}

static bool evaluator_create_directory_link_like(const char *link_path, const char *target_path) {
    if (!link_path || !target_path) return false;
    if (!evaluator_remove_link_like_path(link_path)) return false;

#if defined(_WIN32)
    char link_win[512] = {0};
    char target_win[512] = {0};
    int link_n = snprintf(link_win, sizeof(link_win), "%s", link_path);
    int target_n = snprintf(target_win, sizeof(target_win), "%s", target_path);
    if (link_n < 0 || link_n >= (int)sizeof(link_win) ||
        target_n < 0 || target_n >= (int)sizeof(target_win)) {
        return false;
    }
    for (size_t i = 0; link_win[i] != '\0'; i++) if (link_win[i] == '/') link_win[i] = '\\';
    for (size_t i = 0; target_win[i] != '\0'; i++) if (target_win[i] == '/') target_win[i] = '\\';

    DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (CreateSymbolicLinkA(link_win, target_win, flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }
    if (CreateSymbolicLinkA(link_win, target_win, flags) != 0) return true;

    char cmd[1200] = {0};
    int n = snprintf(cmd, sizeof(cmd), "cmd /C mklink /J %s %s >NUL 2>NUL", link_win, target_win);
    if (n < 0 || n >= (int)sizeof(cmd)) return false;
    return system(cmd) == 0;
#else
    if (symlink(target_path, link_path) == 0) return true;
    return errno == EEXIST;
#endif
}

static bool evaluator_prepare_site_name_command(char *out_path, size_t out_path_size) {
    if (!out_path || out_path_size == 0) return false;
#if defined(_WIN32)
    int n = snprintf(out_path, out_path_size, "%s", "hostname");
    return n > 0 && (size_t)n < out_path_size;
#else
    const char *path = "./temp_site_name_cmd.sh";
    const char *script = "#!/bin/sh\nprintf 'mock-site\\n'\n";
    if (!nob_write_entire_file(path, script, strlen(script))) return false;
    if (chmod(path, 0755) != 0) return false;
    int n = snprintf(out_path, out_path_size, "%s", path);
    return n > 0 && (size_t)n < out_path_size;
#endif
}

static bool evaluator_run_system_command(const char *cmd) {
    if (!cmd) return false;
    return system(cmd) == 0;
}

static bool evaluator_sv_is_decimal(String_View value) {
    if (value.count == 0) return false;
    for (size_t i = 0; i < value.count; i++) {
        if (value.data[i] < '0' || value.data[i] > '9') return false;
    }
    return true;
}

static bool evaluator_sv_is_bool01(String_View value) {
    return value.count == 1 && (value.data[0] == '0' || value.data[0] == '1');
}

static bool evaluator_sv_list_contains(String_View list, String_View wanted) {
    size_t start = 0;
    while (start <= list.count) {
        size_t end = start;
        while (end < list.count && list.data[end] != ';') end++;
        if (end - start == wanted.count &&
            memcmp(list.data + start, wanted.data, wanted.count) == 0) {
            return true;
        }
        if (end == list.count) break;
        start = end + 1;
    }
    return false;
}

static bool evaluator_create_tar_archive(const char *archive_path,
                                         const char *parent_dir,
                                         const char *entry_name) {
    if (!archive_path || !parent_dir || !entry_name) return false;
    char cmd[2048] = {0};
    int n = snprintf(cmd,
                     sizeof(cmd),
                     "tar -cf \"%s\" -C \"%s\" \"%s\"",
                     archive_path,
                     parent_dir,
                     entry_name);
    if (n < 0 || n >= (int)sizeof(cmd)) return false;
    return evaluator_run_system_command(cmd);
}

static bool evaluator_create_fetchcontent_git_repo(const char *repo_dir,
                                                   const char *cmakelists_text,
                                                   const char *version_text,
                                                   const char *tag_name) {
    if (!repo_dir || !cmakelists_text || !version_text || !tag_name) return false;

    char cmakelists_path[1024] = {0};
    char version_path[1024] = {0};
    int cmakelists_n = snprintf(cmakelists_path, sizeof(cmakelists_path), "%s/CMakeLists.txt", repo_dir);
    int version_n = snprintf(version_path, sizeof(version_path), "%s/version.txt", repo_dir);
    if (cmakelists_n < 0 || cmakelists_n >= (int)sizeof(cmakelists_path) ||
        version_n < 0 || version_n >= (int)sizeof(version_path)) {
        return false;
    }

    if (!nob_mkdir_if_not_exists(repo_dir)) return false;
    if (!nob_write_entire_file(cmakelists_path, cmakelists_text, strlen(cmakelists_text))) return false;
    if (!nob_write_entire_file(version_path, version_text, strlen(version_text))) return false;

    char git_init[2048] = {0};
    char git_add[2048] = {0};
    char git_commit[2048] = {0};
    char git_tag[2048] = {0};
    int init_n = snprintf(git_init, sizeof(git_init), "git -C \"%s\" init", repo_dir);
    int add_n = snprintf(git_add, sizeof(git_add), "git -C \"%s\" add .", repo_dir);
    int commit_n = snprintf(git_commit,
                            sizeof(git_commit),
                            "git -C \"%s\" -c user.name=\"Nobify Tests\" -c user.email=\"tests@example.com\" commit -m init",
                            repo_dir);
    int tag_n = snprintf(git_tag, sizeof(git_tag), "git -C \"%s\" tag \"%s\"", repo_dir, tag_name);
    if (init_n < 0 || init_n >= (int)sizeof(git_init) ||
        add_n < 0 || add_n >= (int)sizeof(git_add) ||
        commit_n < 0 || commit_n >= (int)sizeof(git_commit) ||
        tag_n < 0 || tag_n >= (int)sizeof(git_tag)) {
        return false;
    }

    return evaluator_run_system_command(git_init) &&
           evaluator_run_system_command(git_add) &&
           evaluator_run_system_command(git_commit) &&
           evaluator_run_system_command(git_tag);
}

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, token);
}

static Ast_Root parse_cmake(Arena *arena, const char *script) {
    Lexer lx = lexer_init(nob_sv_from_cstr(script ? script : ""));
    Token_List toks = {0};
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &toks, t)) return (Ast_Root){0};
    }
    return parse_tokens(arena, toks);
}

static Eval_Test_Runtime *eval_test_create(const Eval_Test_Init *init) {
    if (!init || !init->arena || !init->event_arena) return NULL;

    Eval_Test_Runtime *ctx = arena_alloc_zero(init->event_arena, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->registry = init->registry;
    if (!ctx->registry) {
        ctx->registry = eval_registry_create(init->event_arena);
        if (!ctx->registry) return NULL;
        ctx->owns_registry = true;
    }

    EvalSession_Config cfg = {0};
    cfg.persistent_arena = init->event_arena;
    cfg.services = init->services;
    cfg.registry = ctx->registry;
    cfg.compat_profile = init->compat_profile;
    cfg.source_root = init->source_dir;
    cfg.binary_root = init->binary_dir;

    ctx->session = eval_session_create(&cfg);
    if (!ctx->session) {
        if (ctx->owns_registry) eval_registry_destroy(ctx->registry);
        return NULL;
    }

    ctx->request.scratch_arena = init->arena;
    ctx->request.source_dir = init->source_dir;
    ctx->request.binary_dir = init->binary_dir;
    ctx->request.list_file = init->current_file;
    ctx->request.mode = init->exec_mode;
    ctx->request.stream = init->stream;
    ctx->stream = init->stream;
    ctx->current_file = init->current_file;
    return ctx;
}

static void eval_test_destroy(Eval_Test_Runtime *ctx) {
    if (!ctx) return;
    if (ctx->session) eval_session_destroy(ctx->session);
    if (ctx->owns_registry && ctx->registry) eval_registry_destroy(ctx->registry);
}

static Eval_Result eval_test_run(Eval_Test_Runtime *ctx, Ast_Root ast) {
    if (!ctx) return eval_test_result_fatal();
    ctx->last_run = eval_session_run(ctx->session, &ctx->request, ast);
    return ctx->last_run.result;
}

static const Eval_Run_Report *eval_test_report(const Eval_Test_Runtime *ctx) {
    return ctx ? &ctx->last_run.report : NULL;
}

static const Eval_Run_Report *eval_test_report_snapshot(const Eval_Test_Runtime *ctx) {
    return eval_test_report(ctx);
}

static bool eval_test_set_compat_profile(Eval_Test_Runtime *ctx, Eval_Compat_Profile profile) {
    return ctx && eval_session_set_compat_profile(ctx->session, profile);
}

static bool eval_test_register_native_command(Eval_Test_Runtime *ctx, const EvalNativeCommandDef *def) {
    return ctx && eval_session_register_native_command(ctx->session, def);
}

static bool eval_test_unregister_native_command(Eval_Test_Runtime *ctx, String_View command_name) {
    return ctx && eval_session_unregister_native_command(ctx->session, command_name);
}

static bool eval_test_get_command_capability(const Eval_Test_Runtime *ctx,
                                             String_View command_name,
                                             Command_Capability *out_capability) {
    return ctx && eval_registry_get_command_capability(ctx->registry, command_name, out_capability);
}

static String_View eval_test_var_get(const Eval_Test_Runtime *ctx, String_View key) {
    String_View out = {0};
    if (!ctx || !ctx->session) return out;
    if (!eval_session_get_visible_var(ctx->session, key, &out)) return nob_sv_from_cstr("");
    return out;
}

static String_View eval_test_session_var_get(const EvalSession *session, String_View key) {
    String_View out = {0};
    if (!session) return out;
    if (!eval_session_get_visible_var(session, key, &out)) return nob_sv_from_cstr("");
    return out;
}

static bool eval_test_var_defined(const Eval_Test_Runtime *ctx, String_View key) {
    String_View out = {0};
    return ctx && ctx->session && eval_session_get_visible_var(ctx->session, key, &out);
}

static bool eval_test_cache_defined(const Eval_Test_Runtime *ctx, String_View key) {
    return ctx && ctx->session && eval_session_cache_defined(ctx->session, key);
}

static bool eval_test_target_known(const Eval_Test_Runtime *ctx, String_View target_name) {
    return ctx && ctx->session && eval_session_target_known(ctx->session, target_name);
}

static bool eval_test_var_event_seen(const Cmake_Event_Stream *stream, String_View key) {
    if (!stream) return false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_VAR_SET) continue;
        if (nob_sv_eq(ev->as.var_set.key, key)) return true;
    }
    return false;
}

static Eval_Result native_test_handler_set_hit(EvalExecContext *ctx, const Node *node) {
    (void)node;
    return eval_test_result_from_bool(eval_exec_set_current_var(ctx,
                                                                nob_sv_from_cstr("NATIVE_HIT"),
                                                                nob_sv_from_cstr("1")));
}

static Eval_Result native_test_handler_runtime_mutation(EvalExecContext *ctx, const Node *node) {
    (void)node;

    EvalNativeCommandDef during_run = {
        .name = nob_sv_from_cstr("native_during_run_register"),
        .handler = native_test_handler_set_hit,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    bool register_ok = g_evaluator_runtime_mutation_registry &&
                       eval_registry_register_native_command(g_evaluator_runtime_mutation_registry, &during_run);
    bool unregister_ok = g_evaluator_runtime_mutation_registry &&
                         eval_registry_unregister_native_command(g_evaluator_runtime_mutation_registry,
                                                                 nob_sv_from_cstr("native_runtime_probe"));

    if (!eval_exec_set_current_var(ctx,
                                   nob_sv_from_cstr("NATIVE_REG_DURING_RUN"),
                                   register_ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) {
        return eval_test_result_fatal();
    }
    return eval_test_result_from_bool(eval_exec_set_current_var(ctx,
                                                                nob_sv_from_cstr("NATIVE_UNREG_DURING_RUN"),
                                                                unregister_ok ? nob_sv_from_cstr("1")
                                                                              : nob_sv_from_cstr("0")));
}

static Eval_Result native_test_handler_snapshot_set_strict_and_warn(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node) return eval_test_result_fatal();
    if (!eval_exec_set_current_var(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_COMPAT_PROFILE"), nob_sv_from_cstr("STRICT"))) {
        return eval_test_result_fatal();
    }
    if (!eval_exec_set_current_var(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_CONTINUE_ON_ERROR"), nob_sv_from_cstr("0"))) {
        return eval_test_result_fatal();
    }

    Event_Origin o = eval_exec_origin_from_node(ctx, node);
    Eval_Result diag = eval_exec_emit_diag(ctx,
                                           EV_DIAG_WARNING,
                                           EVAL_DIAG_SCRIPT_WARNING,
                                           nob_sv_from_cstr("native_snapshot"),
                                           node->as.cmd.name,
                                           o,
                                           nob_sv_from_cstr("phase1 warning"),
                                           nob_sv_from_cstr(""));
    if (eval_result_is_fatal(diag)) return diag;
    if (!eval_exec_set_current_var(ctx, nob_sv_from_cstr("SNAPSHOT_PHASE1_NON_FATAL"), nob_sv_from_cstr("1"))) {
        return eval_test_result_fatal();
    }
    return diag;
}

static Eval_Result native_test_handler_snapshot_warn_only(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node) return eval_test_result_fatal();
    Event_Origin o = eval_exec_origin_from_node(ctx, node);
    return eval_exec_emit_diag(ctx,
                               EV_DIAG_WARNING,
                               EVAL_DIAG_SCRIPT_WARNING,
                               nob_sv_from_cstr("native_snapshot"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("phase2 warning"),
                               nob_sv_from_cstr(""));
}

static bool evaluator_find_last_diag_for_command(const Cmake_Event_Stream *stream,
                                                 String_View command,
                                                 Event_Diag_Severity *out_severity) {
    if (!stream || !out_severity) return false;
    for (size_t i = stream->count; i > 0; i--) {
        const Cmake_Event *ev = &stream->items[i - 1];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (!nob_sv_eq(ev->as.diag.command, command)) continue;
        *out_severity = ev->as.diag.severity;
        return true;
    }
    return false;
}

static Eval_Result native_test_handler_snapshot_set_unsupported_error(EvalExecContext *ctx, const Node *node) {
    (void)node;
    if (!ctx) return eval_test_result_fatal();
    if (!eval_exec_set_current_var(ctx,
                                   nob_sv_from_cstr("CMAKE_NOBIFY_UNSUPPORTED_POLICY"),
                                   nob_sv_from_cstr("ERROR"))) {
        return eval_test_result_fatal();
    }
    return eval_test_result_ok();
}

typedef struct {
    const char *name;
    const char *value;
} Eval_Test_Env_Service_Data;

static const char *evaluator_test_env_service_get(void *user_data,
                                                  Arena *scratch_arena,
                                                  const char *name) {
    Eval_Test_Env_Service_Data *data = (Eval_Test_Env_Service_Data*)user_data;
    if (!data || !name || !data->name || strcmp(name, data->name) != 0) return NULL;
    if (!scratch_arena || !data->value) return data ? data->value : NULL;
    return arena_strndup(scratch_arena, data->value, strlen(data->value));
}

static Eval_Result native_test_handler_tx_rollback_target(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node) return eval_test_result_fatal();

    Event_Origin o = eval_exec_origin_from_node(ctx, node);
    if (!eval_exec_set_current_var(ctx, nob_sv_from_cstr("TX_ROLLBACK_HIT"), nob_sv_from_cstr("1"))) {
        return eval_test_result_fatal();
    }

    return eval_exec_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               EVAL_DIAG_INVALID_STATE,
                               nob_sv_from_cstr("phase_e_tx"),
                               node->as.cmd.name,
                               o,
                               nob_sv_from_cstr("transaction rollback probe"),
                               nob_sv_from_cstr(""));
}

static bool evaluator_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    return test_snapshot_load_text_file_to_arena(arena, path, out);
}

static bool evaluator_read_entire_file_cstr(const char *path, Nob_String_Builder *out) {
    if (!path || !out) return false;
    if (!nob_read_entire_file(path, out)) return false;
    nob_da_append(out, '\0');
    return true;
}

static bool eval_test_append_file_to_log_if_nonempty(Arena *arena,
                                                     const char *path,
                                                     Nob_String_Builder *log) {
    String_View content = {0};
    if (!arena || !path || !log) return false;
    if (!evaluator_load_text_file_to_arena(arena, path, &content)) return false;
    if (content.count == 0) return true;
    nob_sb_append_buf(log, content.data, content.count);
    return true;
}

static String_View evaluator_normalize_newlines_to_arena(Arena *arena, String_View in) {
    return test_snapshot_normalize_newlines_to_arena(arena, in);
}

static bool sv_contains_sv(String_View haystack, String_View needle) {
    if (needle.count == 0) return true;
    if (haystack.count < needle.count) return false;
    for (size_t i = 0; i + needle.count <= haystack.count; i++) {
        if (memcmp(haystack.data + i, needle.data, needle.count) == 0) return true;
    }
    return false;
}

static size_t semicolon_list_count(String_View list) {
    if (list.count == 0) return 0;
    size_t count = 1;
    for (size_t i = 0; i < list.count; i++) {
        if (list.data[i] == ';') count++;
    }
    return count;
}

static String_View semicolon_list_item_at(String_View list, size_t index) {
    if (list.count == 0) return nob_sv_from_cstr("");

    size_t current = 0;
    const char *part = list.data;
    for (size_t i = 0; i <= list.count; i++) {
        bool at_end = (i == list.count);
        if (!at_end && list.data[i] != ';') continue;
        if (current == index) return nob_sv_from_parts(part, (size_t)((list.data + i) - part));
        current++;
        if (at_end) break;
        part = list.data + i + 1;
    }

    return nob_sv_from_cstr("");
}

static bool evaluator_parse_case_pack_list(Arena *arena, String_View content, Evaluator_Case_List *out) {
    Test_Case_Pack_Entry *items = NULL;
    if (!out) return false;
    *out = (Evaluator_Case_List){0};
    if (!test_snapshot_parse_case_pack_to_arena(arena, content, &items)) return false;
    out->items = (Evaluator_Case*)items;
    out->count = arena_arr_len(items);
    out->capacity = arena_arr_cap(items);
    return true;
}

static void snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv) {
    test_snapshot_append_escaped_sv(sb, sv);
}

static const char *snapshot_event_kind_name(const Cmake_Event *ev) {
    if (!ev) return "EV_UNKNOWN";
    Cmake_Event_Kind kind = (Cmake_Event_Kind)ev->h.kind;
    if (kind == EVENT_VAR_SET && ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE) {
        return "EV_SET_CACHE_ENTRY";
    }
    switch (kind) {
        case EV_DIAGNOSTIC: return "EV_DIAGNOSTIC";
        case EV_PROJECT_DECLARE: return "EV_PROJECT_DECLARE";
        case EV_VAR_SET: return "EV_VAR_SET";
        case EV_TARGET_DECLARE: return "EV_TARGET_DECLARE";
        case EV_TARGET_ADD_SOURCE: return "EV_TARGET_ADD_SOURCE";
        case EV_TARGET_ADD_DEPENDENCY: return "EV_TARGET_ADD_DEPENDENCY";
        case EV_TARGET_PROP_SET: return "EV_TARGET_PROP_SET";
        case EV_TARGET_INCLUDE_DIRECTORIES: return "EV_TARGET_INCLUDE_DIRECTORIES";
        case EV_TARGET_COMPILE_DEFINITIONS: return "EV_TARGET_COMPILE_DEFINITIONS";
        case EV_TARGET_COMPILE_OPTIONS: return "EV_TARGET_COMPILE_OPTIONS";
        case EV_TARGET_LINK_LIBRARIES: return "EV_TARGET_LINK_LIBRARIES";
        case EV_TARGET_LINK_OPTIONS: return "EV_TARGET_LINK_OPTIONS";
        case EV_TARGET_LINK_DIRECTORIES: return "EV_TARGET_LINK_DIRECTORIES";
        case EV_DIR_PUSH: return "EV_DIR_PUSH";
        case EV_DIR_POP: return "EV_DIR_POP";
        case EV_TESTING_ENABLE: return "EV_TESTING_ENABLE";
        case EV_TEST_ADD: return "EV_TEST_ADD";
        case EV_INSTALL_ADD_RULE: return "EV_INSTALL_ADD_RULE";
        case EV_CPACK_ADD_INSTALL_TYPE: return "EV_CPACK_ADD_INSTALL_TYPE";
        case EV_CPACK_ADD_COMPONENT_GROUP: return "EV_CPACK_ADD_COMPONENT_GROUP";
        case EV_CPACK_ADD_COMPONENT: return "EV_CPACK_ADD_COMPONENT";
        case EV_FIND_PACKAGE: return "EV_FIND_PACKAGE";
        default: return "EV_UNKNOWN";
    }
}

static bool snapshot_event_is_visible(const Cmake_Event *ev) {
    if (!ev) return false;
    Cmake_Event_Kind kind = (Cmake_Event_Kind)ev->h.kind;
    if (kind == EVENT_VAR_SET && ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE) {
        return true;
    }
    if (kind == EVENT_VAR_SET) {
        String_View key = ev->as.var_set.key;
        if (nob_sv_starts_with(key, nob_sv_from_cstr("NOBIFY_PROPERTY_TARGET::"))) return false;
        if (nob_sv_starts_with(key, nob_sv_from_cstr("NOBIFY_PROPERTY_SOURCE::"))) return false;
        if (nob_sv_starts_with(key, nob_sv_from_cstr("NOBIFY_PROPERTY_TEST::"))) return false;
    }
    switch (kind) {
        case EV_DIAGNOSTIC:
        case EV_PROJECT_DECLARE:
        case EV_VAR_SET:
        case EV_TARGET_DECLARE:
        case EV_TARGET_ADD_SOURCE:
        case EV_TARGET_ADD_DEPENDENCY:
        case EV_TARGET_PROP_SET:
        case EV_TARGET_INCLUDE_DIRECTORIES:
        case EV_TARGET_COMPILE_DEFINITIONS:
        case EV_TARGET_COMPILE_OPTIONS:
        case EV_TARGET_LINK_LIBRARIES:
        case EV_TARGET_LINK_OPTIONS:
        case EV_TARGET_LINK_DIRECTORIES:
        case EV_DIR_PUSH:
        case EV_DIR_POP:
        case EV_TESTING_ENABLE:
        case EV_TEST_ADD:
        case EV_INSTALL_ADD_RULE:
        case EV_CPACK_ADD_INSTALL_TYPE:
        case EV_CPACK_ADD_COMPONENT_GROUP:
        case EV_CPACK_ADD_COMPONENT:
        case EV_FIND_PACKAGE:
            return true;
        default:
            return false;
    }
}

static const char *target_type_name(Cmake_Target_Type type) {
    switch (type) {
        case EV_TARGET_EXECUTABLE: return "EXECUTABLE";
        case EV_TARGET_LIBRARY_STATIC: return "LIB_STATIC";
        case EV_TARGET_LIBRARY_SHARED: return "LIB_SHARED";
        case EV_TARGET_LIBRARY_MODULE: return "LIB_MODULE";
        case EV_TARGET_LIBRARY_INTERFACE: return "LIB_INTERFACE";
        case EV_TARGET_LIBRARY_OBJECT: return "LIB_OBJECT";
        case EV_TARGET_LIBRARY_UNKNOWN: return "LIB_UNKNOWN";
    }
    return "UNKNOWN";
}

static const char *visibility_name(Cmake_Visibility vis) {
    switch (vis) {
        case EV_VISIBILITY_UNSPECIFIED: return "UNSPECIFIED";
        case EV_VISIBILITY_PRIVATE: return "PRIVATE";
        case EV_VISIBILITY_PUBLIC: return "PUBLIC";
        case EV_VISIBILITY_INTERFACE: return "INTERFACE";
    }
    return "UNKNOWN";
}

static const char *diag_severity_name(Cmake_Diag_Severity sev) {
    switch (sev) {
        case EV_DIAG_NOTE: return "NOTE";
        case EV_DIAG_WARNING: return "WARNING";
        case EV_DIAG_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

static const char *prop_op_name(Cmake_Target_Property_Op op) {
    switch (op) {
        case EV_PROP_SET: return "SET";
        case EV_PROP_APPEND_LIST: return "APPEND_LIST";
        case EV_PROP_APPEND_STRING: return "APPEND_STRING";
    }
    return "UNKNOWN";
}

static void append_event_line(Nob_String_Builder *sb, size_t index, const Cmake_Event *ev) {
    Cmake_Event_Kind kind = (Cmake_Event_Kind)ev->h.kind;
    nob_sb_append_cstr(sb, nob_temp_sprintf("EV[%zu] kind=%s file=", index, snapshot_event_kind_name(ev)));
    snapshot_append_escaped_sv(sb, ev->h.origin.file_path);
    nob_sb_append_cstr(sb, nob_temp_sprintf(" line=%zu col=%zu", ev->h.origin.line, ev->h.origin.col));

    switch (kind) {
        case EV_DIAGNOSTIC:
            nob_sb_append_cstr(sb, nob_temp_sprintf(" sev=%s component=", diag_severity_name(ev->as.diag.severity)));
            snapshot_append_escaped_sv(sb, ev->as.diag.component);
            nob_sb_append_cstr(sb, " command=");
            snapshot_append_escaped_sv(sb, ev->as.diag.command);
            nob_sb_append_cstr(sb, " code=");
            snapshot_append_escaped_sv(sb, ev->as.diag.code);
            nob_sb_append_cstr(sb, " class=");
            snapshot_append_escaped_sv(sb, ev->as.diag.error_class);
            nob_sb_append_cstr(sb, " cause=");
            snapshot_append_escaped_sv(sb, ev->as.diag.cause);
            nob_sb_append_cstr(sb, " hint=");
            snapshot_append_escaped_sv(sb, ev->as.diag.hint);
            break;

        case EV_PROJECT_DECLARE:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.project_declare.name);
            nob_sb_append_cstr(sb, " version=");
            snapshot_append_escaped_sv(sb, ev->as.project_declare.version);
            nob_sb_append_cstr(sb, " description=");
            snapshot_append_escaped_sv(sb, ev->as.project_declare.description);
            nob_sb_append_cstr(sb, " languages=");
            snapshot_append_escaped_sv(sb, ev->as.project_declare.languages);
            break;

        case EV_VAR_SET:
            nob_sb_append_cstr(sb, " key=");
            snapshot_append_escaped_sv(sb, ev->as.var_set.key);
            nob_sb_append_cstr(sb, " value=");
            snapshot_append_escaped_sv(sb, ev->as.var_set.value);
            if (ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE) {
                // Backward-compatible textual snapshot for legacy golden files.
            }
            break;

        case EV_TARGET_DECLARE:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.target_declare.name);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" type=%s", target_type_name(ev->as.target_declare.target_type)));
            break;

        case EV_TARGET_ADD_SOURCE:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_add_source.target_name);
            nob_sb_append_cstr(sb, " path=");
            snapshot_append_escaped_sv(sb, ev->as.target_add_source.path);
            break;

        case EV_TARGET_ADD_DEPENDENCY:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_add_dependency.target_name);
            nob_sb_append_cstr(sb, " dependency=");
            snapshot_append_escaped_sv(sb, ev->as.target_add_dependency.dependency_name);
            break;

        case EV_TARGET_PROP_SET:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_prop_set.target_name);
            nob_sb_append_cstr(sb, " key=");
            snapshot_append_escaped_sv(sb, ev->as.target_prop_set.key);
            nob_sb_append_cstr(sb, " value=");
            snapshot_append_escaped_sv(sb, ev->as.target_prop_set.value);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" op=%s", prop_op_name(ev->as.target_prop_set.op)));
            break;

        case EV_TARGET_INCLUDE_DIRECTORIES:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_include_directories.target_name);
            nob_sb_append_cstr(sb, " path=");
            snapshot_append_escaped_sv(sb, ev->as.target_include_directories.path);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s is_system=%d is_before=%d",
                visibility_name(ev->as.target_include_directories.visibility),
                ev->as.target_include_directories.is_system ? 1 : 0,
                ev->as.target_include_directories.is_before ? 1 : 0));
            break;

        case EV_TARGET_COMPILE_DEFINITIONS:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_compile_definitions.target_name);
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.target_compile_definitions.item);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_compile_definitions.visibility)));
            break;

        case EV_TARGET_COMPILE_OPTIONS:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_compile_options.target_name);
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.target_compile_options.item);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s is_before=%d",
                visibility_name(ev->as.target_compile_options.visibility),
                ev->as.target_compile_options.is_before ? 1 : 0));
            break;

        case EV_TARGET_LINK_LIBRARIES:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_libraries.target_name);
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_libraries.item);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_link_libraries.visibility)));
            break;

        case EV_TARGET_LINK_OPTIONS:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_options.target_name);
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_options.item);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s is_before=%d",
                visibility_name(ev->as.target_link_options.visibility),
                ev->as.target_link_options.is_before ? 1 : 0));
            break;

        case EV_TARGET_LINK_DIRECTORIES:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_directories.target_name);
            nob_sb_append_cstr(sb, " path=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_directories.path);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_link_directories.visibility)));
            break;

        case EV_DIR_PUSH:
            nob_sb_append_cstr(sb, " source_dir=");
            snapshot_append_escaped_sv(sb, ev->as.dir_push.source_dir);
            nob_sb_append_cstr(sb, " binary_dir=");
            snapshot_append_escaped_sv(sb, ev->as.dir_push.binary_dir);
            break;

        case EV_DIR_POP:
            break;

        case EV_TESTING_ENABLE:
            nob_sb_append_cstr(sb, nob_temp_sprintf(" enabled=%d",
                ev->as.test_enable.enabled ? 1 : 0));
            break;

        case EV_TEST_ADD:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.test_add.name);
            nob_sb_append_cstr(sb, " command=");
            snapshot_append_escaped_sv(sb, ev->as.test_add.command);
            nob_sb_append_cstr(sb, " working_dir=");
            snapshot_append_escaped_sv(sb, ev->as.test_add.working_dir);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" command_expand_lists=%d",
                ev->as.test_add.command_expand_lists ? 1 : 0));
            break;

        case EV_INSTALL_ADD_RULE:
            nob_sb_append_cstr(sb, nob_temp_sprintf(" rule_type=%d", (int)ev->as.install_add_rule.rule_type));
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.install_add_rule.item);
            nob_sb_append_cstr(sb, " destination=");
            snapshot_append_escaped_sv(sb, ev->as.install_add_rule.destination);
            break;

        case EV_CPACK_ADD_INSTALL_TYPE:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_install_type.name);
            nob_sb_append_cstr(sb, " display_name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_install_type.display_name);
            break;

        case EV_CPACK_ADD_COMPONENT_GROUP:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component_group.name);
            nob_sb_append_cstr(sb, " display_name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component_group.display_name);
            nob_sb_append_cstr(sb, " description=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component_group.description);
            nob_sb_append_cstr(sb, " parent_group=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component_group.parent_group);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" expanded=%d bold_title=%d",
                ev->as.cpack_add_component_group.expanded ? 1 : 0,
                ev->as.cpack_add_component_group.bold_title ? 1 : 0));
            break;

        case EV_CPACK_ADD_COMPONENT:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.name);
            nob_sb_append_cstr(sb, " display_name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.display_name);
            nob_sb_append_cstr(sb, " description=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.description);
            nob_sb_append_cstr(sb, " group=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.group);
            nob_sb_append_cstr(sb, " depends=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.depends);
            nob_sb_append_cstr(sb, " install_types=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.install_types);
            nob_sb_append_cstr(sb, " archive_file=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.archive_file);
            nob_sb_append_cstr(sb, " plist=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.plist);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" required=%d hidden=%d disabled=%d downloaded=%d",
                ev->as.cpack_add_component.required ? 1 : 0,
                ev->as.cpack_add_component.hidden ? 1 : 0,
                ev->as.cpack_add_component.disabled ? 1 : 0,
                ev->as.cpack_add_component.downloaded ? 1 : 0));
            break;

        case EV_FIND_PACKAGE:
            nob_sb_append_cstr(sb, " package=");
            snapshot_append_escaped_sv(sb, ev->as.package_find_result.package_name);
            nob_sb_append_cstr(sb, " mode=");
            snapshot_append_escaped_sv(sb, ev->as.package_find_result.mode);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" required=%d found=%d location=",
                ev->as.package_find_result.required ? 1 : 0,
                ev->as.package_find_result.found ? 1 : 0));
            snapshot_append_escaped_sv(sb, ev->as.package_find_result.found_path);
            break;
        default:
            break;
    }

    nob_sb_append_cstr(sb, "\n");
}

static bool evaluator_snapshot_from_ast(Ast_Root root, const char *current_file, Nob_String_Builder *out_sb) {
    if (!out_sb) return false;

    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(4 * 1024 * 1024);
    if (!temp_arena || !event_arena) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        return false;
    }

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    if (!stream) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        return false;
    }

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = current_file;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    if (!ctx) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        return false;
    }

    (void)eval_test_run(ctx, root);

    nob_sb_append_cstr(out_sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    size_t visible_count = 0;
    for (size_t i = 0; i < stream->count; i++) {
        if (snapshot_event_is_visible(&stream->items[i])) visible_count++;
    }

    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVENTS count=%zu\n", visible_count));

    size_t visible_index = 0;
    for (size_t i = 0; i < stream->count; i++) {
        if (!snapshot_event_is_visible(&stream->items[i])) continue;
        append_event_line(out_sb, visible_index++, &stream->items[i]);
    }

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    return true;
}

static size_t evaluator_find_command_begin_index(const Cmake_Event_Stream *stream,
                                                 String_View command_name,
                                                 size_t origin_line,
                                                 Event_Command_Dispatch_Kind dispatch_kind) {
    if (!stream) return (size_t)-1;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EVENT_COMMAND_BEGIN) continue;
        if (ev->h.origin.line != origin_line) continue;
        if (ev->as.command_begin.dispatch_kind != dispatch_kind) continue;
        if (!nob_sv_eq(ev->as.command_begin.command_name, command_name)) continue;
        return i;
    }
    return (size_t)-1;
}

static size_t evaluator_find_command_end_index(const Cmake_Event_Stream *stream,
                                               String_View command_name,
                                               size_t origin_line,
                                               Event_Command_Dispatch_Kind dispatch_kind,
                                               Event_Command_Status status) {
    if (!stream) return (size_t)-1;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EVENT_COMMAND_END) continue;
        if (ev->h.origin.line != origin_line) continue;
        if (ev->as.command_end.dispatch_kind != dispatch_kind) continue;
        if (ev->as.command_end.status != status) continue;
        if (!nob_sv_eq(ev->as.command_end.command_name, command_name)) continue;
        return i;
    }
    return (size_t)-1;
}

static size_t evaluator_find_diag_index(const Cmake_Event_Stream *stream,
                                        String_View command_name,
                                        size_t origin_line,
                                        String_View cause) {
    if (!stream) return (size_t)-1;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EVENT_DIAG) continue;
        if (ev->h.origin.line != origin_line) continue;
        if (!nob_sv_eq(ev->as.diag.command, command_name)) continue;
        if (!nob_sv_eq(ev->as.diag.cause, cause)) continue;
        return i;
    }
    return (size_t)-1;
}

static bool evaluator_stream_has_monotonic_sequence(const Cmake_Event_Stream *stream) {
    if (!stream) return false;
    uint64_t prev_seq = 0;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.version != 1) return false;
        if (stream->items[i].h.seq <= prev_seq) return false;
        prev_seq = stream->items[i].h.seq;
    }
    return true;
}

static bool render_evaluator_case_snapshot_to_sb(Arena *arena,
                                                  Evaluator_Case eval_case,
                                                  Nob_String_Builder *out_sb) {
    diag_reset();
    Ast_Root root = parse_cmake(arena, eval_case.script.data);

    Nob_String_Builder case_sb = {0};
    bool ok = evaluator_snapshot_from_ast(root, "CMakeLists.txt", &case_sb);
    if (!ok || case_sb.count == 0) {
        nob_sb_free(case_sb);
        return false;
    }

    nob_sb_append_buf(out_sb, case_sb.items, case_sb.count);
    nob_sb_free(case_sb);
    return true;
}

static bool render_evaluator_casepack_snapshot_to_arena(Arena *arena,
                                                         Evaluator_Case_List cases,
                                                         String_View *out) {
    if (!arena || !out) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "MODULE evaluator\n");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", cases.count));

    for (size_t i = 0; i < cases.count; i++) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases.items[i].name.data, cases.items[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");

        if (!render_evaluator_case_snapshot_to_sb(arena, cases.items[i], &sb)) {
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < cases.count) nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static void evaluator_log_golden_mismatch(const char *subject_path,
                                          const char *expected_path,
                                          String_View expected_norm,
                                          String_View actual_norm,
                                          void *userdata) {
    size_t mismatch = 0;
    size_t shared = actual_norm.count < expected_norm.count ? actual_norm.count : expected_norm.count;

    (void)userdata;

    while (mismatch < shared && actual_norm.data[mismatch] == expected_norm.data[mismatch]) mismatch++;

    nob_log(NOB_ERROR, "golden mismatch for %s", subject_path);
    nob_log(NOB_ERROR,
            "golden lengths: expected=%zu actual=%zu first_mismatch=%zu",
            expected_norm.count,
            actual_norm.count,
            mismatch);
    if (mismatch < shared) {
        nob_log(NOB_ERROR,
                "golden bytes: expected=0x%02X actual=0x%02X",
                (unsigned char)expected_norm.data[mismatch],
                (unsigned char)actual_norm.data[mismatch]);
    }
    nob_log(NOB_ERROR,
            "--- expected (%s) ---\n%.*s",
            expected_path,
            (int)expected_norm.count,
            expected_norm.data);
    nob_log(NOB_ERROR,
            "--- actual ---\n%.*s",
            (int)actual_norm.count,
            actual_norm.data);
}

static bool assert_evaluator_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(4 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View actual = {0};
    bool ok = true;
    const char *prev_sde = getenv("SOURCE_DATE_EPOCH");
    bool had_prev_sde = prev_sde != NULL;
    char *prev_sde_copy = NULL;
    if (had_prev_sde) {
        size_t n = strlen(prev_sde);
        prev_sde_copy = arena_strndup(arena, prev_sde, n);
        if (!prev_sde_copy) {
            ok = false;
            goto done;
        }
    }

    if (!evaluator_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    if (sv_contains_sv(input, nob_sv_from_cstr("temp_symlink_escape_link")) &&
        !evaluator_prepare_symlink_escape_fixture()) {
        ok = false;
        goto done;
    }

    Evaluator_Case_List cases = {0};
    if (!evaluator_parse_case_pack_list(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }
    evaluator_set_source_date_epoch_value("0");

    if (!render_evaluator_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render snapshot");
        ok = false;
        goto done;
    }

    if (!test_snapshot_assert_golden_output(
            arena,
            input_path,
            expected_path,
            actual,
            evaluator_log_golden_mismatch,
            NULL)) {
        ok = false;
    }

done:
    if (had_prev_sde) evaluator_set_source_date_epoch_value(prev_sde_copy ? prev_sde_copy : "");
    else evaluator_set_source_date_epoch_value(NULL);
    arena_destroy(arena);
    return ok;
}

#endif // TEST_EVALUATOR_V2_COMMON_H_
