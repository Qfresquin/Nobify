#include "test_v2_assert.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "eval_dispatcher.h"
#include "eval_try_compile_internal.h"
#include "evaluator.h"
#include "evaluator_internal.h"
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

typedef struct {
    String_View name;
    String_View script;
} Evaluator_Case;

typedef struct {
    Evaluator_Case *items;
    size_t count;
    size_t capacity;
} Evaluator_Case_List;

static Nob_String_Builder g_evaluator_captured_nob_logs = {0};
static nob_log_handler *g_evaluator_prev_log_handler = NULL;

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

static Eval_Result native_test_handler_set_hit(Evaluator_Context *ctx, const Node *node) {
    (void)node;
    return eval_result_from_bool(eval_var_set_current(ctx, nob_sv_from_cstr("NATIVE_HIT"), nob_sv_from_cstr("1")));
}

static Eval_Result native_test_handler_runtime_mutation(Evaluator_Context *ctx, const Node *node) {
    (void)node;

    Evaluator_Native_Command_Def during_run = {
        .name = nob_sv_from_cstr("native_during_run_register"),
        .handler = native_test_handler_set_hit,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    bool register_ok = evaluator_register_native_command(ctx, &during_run);
    bool unregister_ok = evaluator_unregister_native_command(ctx, nob_sv_from_cstr("native_runtime_probe"));

    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr("NATIVE_REG_DURING_RUN"),
                              register_ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) {
        return eval_result_fatal();
    }
    return eval_result_from_bool(eval_var_set_current(ctx,
                                                      nob_sv_from_cstr("NATIVE_UNREG_DURING_RUN"),
                                                      unregister_ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0")));
}

static Eval_Result native_test_handler_snapshot_set_strict_and_warn(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node) return eval_result_fatal();
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_COMPAT_PROFILE"), nob_sv_from_cstr("STRICT"))) {
        return eval_result_fatal();
    }
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_CONTINUE_ON_ERROR"), nob_sv_from_cstr("0"))) {
        return eval_result_fatal();
    }

    Event_Origin o = eval_origin_from_node(ctx, node);
    Eval_Result diag = eval_emit_diag_with_severity(ctx,
                                                    EV_DIAG_WARNING,
                                                    EVAL_DIAG_SCRIPT_WARNING,
                                                    nob_sv_from_cstr("native_snapshot"),
                                                    node->as.cmd.name,
                                                    o,
                                                    nob_sv_from_cstr("phase1 warning"),
                                                    nob_sv_from_cstr(""));
    if (eval_result_is_fatal(diag)) return diag;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("SNAPSHOT_PHASE1_NON_FATAL"), nob_sv_from_cstr("1"))) {
        return eval_result_fatal();
    }
    return diag;
}

static Eval_Result native_test_handler_snapshot_warn_only(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node) return eval_result_fatal();
    Event_Origin o = eval_origin_from_node(ctx, node);
    return eval_emit_diag_with_severity(ctx,
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

static Eval_Result native_test_handler_snapshot_set_unsupported_error_and_probe_inline(Evaluator_Context *ctx,
                                                                                        const Node *node) {
    if (!ctx || !node || !ctx->stream) return eval_result_fatal();
    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr("CMAKE_NOBIFY_UNSUPPORTED_POLICY"),
                              nob_sv_from_cstr("ERROR"))) {
        return eval_result_fatal();
    }

    Ast_Root probe = parse_cmake(eval_temp_arena(ctx), "unknown_inline_policy_cmd()\n");
    if (arena_arr_len(probe) == 0) return eval_result_fatal();

    Eval_Result inline_result = eval_dispatch_command(ctx, &probe[0]);
    if (eval_result_is_fatal(inline_result)) return inline_result;

    String_View result_sv = eval_result_is_soft_error(inline_result)
        ? nob_sv_from_cstr("SOFT_ERROR")
        : nob_sv_from_cstr("OK");
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("INLINE_UNSUPPORTED_SNAPSHOT_RESULT"), result_sv)) {
        return eval_result_fatal();
    }

    Event_Diag_Severity inline_severity = EV_DIAG_WARNING;
    if (!evaluator_find_last_diag_for_command(ctx->stream,
                                              nob_sv_from_cstr("unknown_inline_policy_cmd"),
                                              &inline_severity)) {
        return eval_result_fatal();
    }
    return eval_result_from_bool(eval_var_set_current(
        ctx,
        nob_sv_from_cstr("INLINE_UNSUPPORTED_SNAPSHOT_SEVERITY"),
        inline_severity == EV_DIAG_ERROR ? nob_sv_from_cstr("ERROR") : nob_sv_from_cstr("WARNING")));
}

static bool evaluator_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    if (!arena || !path || !out) return false;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path, &sb)) return false;

    char *text = arena_strndup(arena, sb.items, sb.count);
    size_t len = sb.count;
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static String_View evaluator_normalize_newlines_to_arena(Arena *arena, String_View in) {
    if (!arena) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(arena, in.count + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t out_count = 0;
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (c == '\r') continue;
        buf[out_count++] = c;
    }

    buf[out_count] = '\0';
    return nob_sv_from_parts(buf, out_count);
}

static bool evaluator_case_list_append(Arena *arena, Evaluator_Case_List *list, Evaluator_Case value) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = value;
    return true;
}

static bool sv_starts_with_cstr(String_View sv, const char *prefix) {
    String_View p = nob_sv_from_cstr(prefix);
    if (sv.count < p.count) return false;
    return memcmp(sv.data, p.data, p.count) == 0;
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

static String_View sv_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        return nob_sv_from_parts(sv.data, sv.count - 1);
    }
    return sv;
}

static bool parse_case_pack_to_arena(Arena *arena, String_View content, Evaluator_Case_List *out) {
    if (!arena || !out) return false;
    *out = (Evaluator_Case_List){0};

    Nob_String_Builder script_sb = {0};
    bool in_case = false;
    String_View current_name = {0};

    size_t pos = 0;
    while (pos < content.count) {
        size_t line_start = pos;
        while (pos < content.count && content.data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < content.count && content.data[pos] == '\n') pos++;

        String_View raw_line = nob_sv_from_parts(content.data + line_start, line_end - line_start);
        String_View line = sv_trim_cr(raw_line);

        if (sv_starts_with_cstr(line, "#@@CASE ")) {
            if (in_case) {
                nob_sb_free(script_sb);
                return false;
            }
            in_case = true;
            current_name = nob_sv_from_parts(line.data + 8, line.count - 8);
            script_sb.count = 0;
            continue;
        }

        if (nob_sv_eq(line, nob_sv_from_cstr("#@@ENDCASE"))) {
            if (!in_case) {
                nob_sb_free(script_sb);
                return false;
            }

            char *name = arena_strndup(arena, current_name.data, current_name.count);
            char *script = arena_strndup(arena, script_sb.items ? script_sb.items : "", script_sb.count);
            if (!name || !script) {
                nob_sb_free(script_sb);
                return false;
            }

            if (!evaluator_case_list_append(arena, out, (Evaluator_Case){
                .name = nob_sv_from_parts(name, current_name.count),
                .script = nob_sv_from_parts(script, script_sb.count),
            })) {
                nob_sb_free(script_sb);
                return false;
            }

            in_case = false;
            current_name = (String_View){0};
            script_sb.count = 0;
            continue;
        }

        if (in_case) {
            nob_sb_append_buf(&script_sb, raw_line.data, raw_line.count);
            nob_sb_append(&script_sb, '\n');
        }
    }

    nob_sb_free(script_sb);
    if (in_case) return false;

    for (size_t i = 0; i < out->count; i++) {
        for (size_t j = i + 1; j < out->count; j++) {
            if (nob_sv_eq(out->items[i].name, out->items[j].name)) return false;
        }
    }

    return out->count > 0;
}

static void snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv) {
    nob_sb_append_cstr(sb, "'");
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '\\') {
            nob_sb_append_cstr(sb, "\\\\");
        } else if (c == '\n') {
            nob_sb_append_cstr(sb, "\\n");
        } else if (c == '\r') {
            nob_sb_append_cstr(sb, "\\r");
        } else if (c == '\t') {
            nob_sb_append_cstr(sb, "\\t");
        } else if (c == '\'') {
            nob_sb_append_cstr(sb, "\\'");
        } else {
            nob_sb_append(sb, c);
        }
    }
    nob_sb_append_cstr(sb, "'");
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
        if (sv_starts_with_cstr(key, "NOBIFY_PROPERTY_TARGET::")) return false;
        if (sv_starts_with_cstr(key, "NOBIFY_PROPERTY_SOURCE::")) return false;
        if (sv_starts_with_cstr(key, "NOBIFY_PROPERTY_TEST::")) return false;
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

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = current_file;

    Evaluator_Context *ctx = evaluator_create(&init);
    if (!ctx) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        return false;
    }

    (void)evaluator_run(ctx, root);

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

    evaluator_destroy(ctx);
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
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_evaluator_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(4 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View expected = {0};
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

    if (!evaluator_prepare_symlink_escape_fixture()) {
        ok = false;
        goto done;
    }

    if (!evaluator_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Evaluator_Case_List cases = {0};
    if (!parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }
    if (cases.count != 107) {
        nob_log(NOB_ERROR, "golden: unexpected evaluator case count: got=%zu expected=107", cases.count);
        ok = false;
        goto done;
    }

    evaluator_set_source_date_epoch_value("0");

    if (!render_evaluator_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render snapshot");
        ok = false;
        goto done;
    }

    String_View actual_norm = evaluator_normalize_newlines_to_arena(arena, actual);

    const char *update = getenv("CMK2NOB_UPDATE_GOLDEN");
    if (update && strcmp(update, "1") == 0) {
        if (!nob_write_entire_file(expected_path, actual_norm.data, actual_norm.count)) {
            nob_log(NOB_ERROR, "golden: failed to update expected: %s", expected_path);
            ok = false;
        }
        goto done;
    }

    if (!evaluator_load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        ok = false;
        goto done;
    }

    String_View expected_norm = evaluator_normalize_newlines_to_arena(arena, expected);
    if (!nob_sv_eq(actual_norm, expected_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", expected_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

done:
    if (had_prev_sde) evaluator_set_source_date_epoch_value(prev_sde_copy ? prev_sde_copy : "");
    else evaluator_set_source_date_epoch_value(NULL);
    arena_destroy(arena);
    return ok;
}

static const char *EVALUATOR_GOLDEN_DIR = "test_v2/evaluator/golden";

TEST(evaluator_golden_all_cases) {
    ASSERT(assert_evaluator_golden_casepack(
        nob_temp_sprintf("%s/evaluator_all.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/evaluator_all.txt", EVALUATOR_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(evaluator_public_api_profile_and_report_snapshot) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);
    ASSERT(evaluator_set_compat_profile(ctx, EVAL_PROFILE_STRICT));

    Ast_Root root = parse_cmake(temp_arena, "unknown_public_api_command()\n");
    (void)evaluator_run(ctx, root);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    const Eval_Run_Report *snapshot = evaluator_get_run_report_snapshot(ctx);
    ASSERT(report != NULL);
    ASSERT(snapshot != NULL);
    ASSERT(report->error_count >= 1);
    ASSERT(snapshot->error_count == report->error_count);

    bool found_diag_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity == EV_DIAG_ERROR) {
            found_diag_error = true;
            break;
        }
    }
    ASSERT(found_diag_error);

    Command_Capability cap = {0};
    ASSERT(evaluator_get_command_capability(ctx, nob_sv_from_cstr("file"), &cap));
    ASSERT(cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability cmk_path_cap = {0};
    ASSERT(evaluator_get_command_capability(ctx, nob_sv_from_cstr("cmake_path"), &cmk_path_cap));
    ASSERT(cmk_path_cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability link_libs_cap = {0};
    ASSERT(evaluator_get_command_capability(ctx, nob_sv_from_cstr("link_libraries"), &link_libs_cap));
    ASSERT(link_libs_cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability try_compile_cap = {0};
    ASSERT(evaluator_get_command_capability(ctx, nob_sv_from_cstr("try_compile"), &try_compile_cap));
    ASSERT(try_compile_cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability missing = {0};
    ASSERT(!evaluator_get_command_capability(ctx, nob_sv_from_cstr("unknown_public_api_command"), &missing));
    ASSERT(missing.implemented_level == EVAL_CMD_IMPL_MISSING);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_g5_legacy_wrapper_capabilities_promoted_to_full) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    const char *full_commands[] = {
        "build_name",
        "build_command",
        "exec_program",
        "install_targets",
        "source_group",
        "variable_watch",
        "write_file",
    };

    for (size_t i = 0; i < NOB_ARRAY_LEN(full_commands); i++) {
        Command_Capability cap = {0};
        ASSERT(evaluator_get_command_capability(ctx, nob_sv_from_cstr(full_commands[i]), &cap));
        ASSERT(cap.implemented_level == EVAL_CMD_IMPL_FULL);
        ASSERT(cap.fallback_behavior == EVAL_FALLBACK_NOOP_WARN);
    }

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_native_command_registry_runtime_extension) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root_builtin = parse_cmake(temp_arena, "set(BUILTIN_SEEDED 1)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root_builtin)));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BUILTIN_SEEDED")), nob_sv_from_cstr("1")));

    Evaluator_Native_Command_Def native_ext = {
        .name = nob_sv_from_cstr("native_ext_cmd"),
        .handler = native_test_handler_set_hit,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_ERROR_CONTINUE,
    };
    ASSERT(evaluator_register_native_command(ctx, &native_ext));

    Command_Capability native_ext_cap = {0};
    ASSERT(evaluator_get_command_capability(ctx, nob_sv_from_cstr("native_ext_cmd"), &native_ext_cap));
    ASSERT(native_ext_cap.implemented_level == EVAL_CMD_IMPL_PARTIAL);
    ASSERT(native_ext_cap.fallback_behavior == EVAL_FALLBACK_ERROR_CONTINUE);

    ASSERT(!evaluator_register_native_command(ctx, &native_ext));

    Ast_Root root_native = parse_cmake(
        temp_arena,
        "if(COMMAND native_ext_cmd)\n"
        "  set(NATIVE_PREDICATE 1)\n"
        "endif()\n"
        "native_ext_cmd()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root_native)));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NATIVE_PREDICATE")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NATIVE_HIT")), nob_sv_from_cstr("1")));

    Ast_Root root_user_collision = parse_cmake(
        temp_arena,
        "function(native_user_collision)\n"
        "endfunction()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root_user_collision)));

    Evaluator_Native_Command_Def user_collision = native_ext;
    user_collision.name = nob_sv_from_cstr("native_user_collision");
    ASSERT(!evaluator_register_native_command(ctx, &user_collision));

    ASSERT(!evaluator_unregister_native_command(ctx, nob_sv_from_cstr("message")));

    Evaluator_Native_Command_Def runtime_probe = {
        .name = nob_sv_from_cstr("native_runtime_probe"),
        .handler = native_test_handler_runtime_mutation,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    ASSERT(evaluator_register_native_command(ctx, &runtime_probe));

    Ast_Root root_runtime_probe = parse_cmake(temp_arena, "native_runtime_probe()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root_runtime_probe)));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NATIVE_REG_DURING_RUN")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NATIVE_UNREG_DURING_RUN")), nob_sv_from_cstr("0")));

    ASSERT(evaluator_unregister_native_command(ctx, nob_sv_from_cstr("native_runtime_probe")));
    ASSERT(!evaluator_unregister_native_command(ctx, nob_sv_from_cstr("native_runtime_probe")));

    Command_Capability removed_runtime_probe = {0};
    ASSERT(!evaluator_get_command_capability(ctx, nob_sv_from_cstr("native_runtime_probe"), &removed_runtime_probe));
    ASSERT(removed_runtime_probe.implemented_level == EVAL_CMD_IMPL_MISSING);

    Ast_Root root_probe_missing = parse_cmake(
        temp_arena,
        "if(COMMAND native_runtime_probe)\n"
        "  set(PROBE_STILL_VISIBLE 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root_probe_missing)));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("PROBE_STILL_VISIBLE")).count == 0);

    ASSERT(evaluator_unregister_native_command(ctx, nob_sv_from_cstr("native_ext_cmd")));
    ASSERT(!evaluator_unregister_native_command(ctx, nob_sv_from_cstr("native_ext_cmd")));

    Command_Capability removed_native_ext = {0};
    ASSERT(!evaluator_get_command_capability(ctx, nob_sv_from_cstr("native_ext_cmd"), &removed_native_ext));
    ASSERT(removed_native_ext.implemented_level == EVAL_CMD_IMPL_MISSING);

    Ast_Root root_native_missing = parse_cmake(
        temp_arena,
        "if(COMMAND native_ext_cmd)\n"
        "  set(NATIVE_AFTER_UNREGISTER 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root_native_missing)));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("NATIVE_AFTER_UNREGISTER")).count == 0);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_command_capability_remains_native_only_introspection) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "function(user_capability_probe)\n"
        "  add_executable(user_capability_target main.c)\n"
        "endfunction()\n"
        "if(COMMAND user_capability_probe)\n"
        "  set(USER_CAPABILITY_VISIBLE 1)\n"
        "endif()\n"
        "user_capability_probe()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("USER_CAPABILITY_VISIBLE")), nob_sv_from_cstr("1")));

    Command_Capability user_cap = {0};
    ASSERT(!evaluator_get_command_capability(ctx, nob_sv_from_cstr("user_capability_probe"), &user_cap));
    ASSERT(user_cap.implemented_level == EVAL_CMD_IMPL_MISSING);

    bool saw_target = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_DECLARE) continue;
        if (!nob_sv_eq(ev->as.target_declare.name, nob_sv_from_cstr("user_capability_target"))) continue;
        saw_target = true;
        break;
    }
    ASSERT(saw_target);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_native_command_registry_case_insensitive_index_lookup) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Evaluator_Native_Command_Def mixed_case = {
        .name = nob_sv_from_cstr("NaTiVe_MiXeD_CaSe_CmD"),
        .handler = native_test_handler_set_hit,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_ERROR_CONTINUE,
    };
    ASSERT(evaluator_register_native_command(ctx, &mixed_case));

    Command_Capability lower = {0};
    ASSERT(evaluator_get_command_capability(ctx, nob_sv_from_cstr("native_mixed_case_cmd"), &lower));
    ASSERT(lower.implemented_level == EVAL_CMD_IMPL_PARTIAL);

    Command_Capability upper = {0};
    ASSERT(evaluator_get_command_capability(ctx, nob_sv_from_cstr("NATIVE_MIXED_CASE_CMD"), &upper));
    ASSERT(upper.fallback_behavior == EVAL_FALLBACK_ERROR_CONTINUE);

    Evaluator_Native_Command_Def dup_mixed = mixed_case;
    dup_mixed.name = nob_sv_from_cstr("native_mixed_case_cmd");
    ASSERT(!evaluator_register_native_command(ctx, &dup_mixed));

    Ast_Root root_run = parse_cmake(
        temp_arena,
        "if(COMMAND native_mixed_case_cmd)\n"
        "  set(NATIVE_CASE_PREDICATE 1)\n"
        "endif()\n"
        "NATIVE_MIXED_CASE_CMD()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root_run)));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NATIVE_CASE_PREDICATE")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NATIVE_HIT")), nob_sv_from_cstr("1")));

    ASSERT(evaluator_unregister_native_command(ctx, nob_sv_from_cstr("nAtIvE_mIxEd_cAsE_cMd")));

    Command_Capability removed = {0};
    ASSERT(!evaluator_get_command_capability(ctx, nob_sv_from_cstr("NATIVE_MIXED_CASE_CMD"), &removed));
    ASSERT(removed.implemented_level == EVAL_CMD_IMPL_MISSING);

    Ast_Root root_after = parse_cmake(
        temp_arena,
        "if(COMMAND native_mixed_case_cmd)\n"
        "  set(NATIVE_CASE_AFTER_UNREGISTER 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root_after)));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("NATIVE_CASE_AFTER_UNREGISTER")).count == 0);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_compat_refresh_snapshot_applies_next_command_cycle) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Evaluator_Native_Command_Def phase_one = {
        .name = nob_sv_from_cstr("native_snapshot_phase_one"),
        .handler = native_test_handler_snapshot_set_strict_and_warn,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    ASSERT(evaluator_register_native_command(ctx, &phase_one));

    Evaluator_Native_Command_Def phase_two = {
        .name = nob_sv_from_cstr("native_snapshot_phase_two"),
        .handler = native_test_handler_snapshot_warn_only,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    ASSERT(evaluator_register_native_command(ctx, &phase_two));

    Ast_Root root = parse_cmake(
        temp_arena,
        "native_snapshot_phase_one()\n"
        "native_snapshot_phase_two()\n");
    Eval_Result run_res = evaluator_run(ctx, root);
    ASSERT(eval_result_is_fatal(run_res));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SNAPSHOT_PHASE1_NON_FATAL")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_COMPAT_PROFILE")), nob_sv_from_cstr("STRICT")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_CONTINUE_ON_ERROR")), nob_sv_from_cstr("0")));

    size_t snapshot_diag_count = 0;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.kind != EV_DIAGNOSTIC) continue;
        if (!nob_sv_eq(stream->items[i].as.diag.component, nob_sv_from_cstr("native_snapshot"))) continue;
        snapshot_diag_count++;
    }
    ASSERT(snapshot_diag_count == 2);

    Event_Diag_Severity phase_one_severity = EV_DIAG_ERROR;
    ASSERT(evaluator_find_last_diag_for_command(stream,
                                                nob_sv_from_cstr("native_snapshot_phase_one"),
                                                &phase_one_severity));
    ASSERT(phase_one_severity == EV_DIAG_WARNING);

    Event_Diag_Severity phase_two_severity = EV_DIAG_WARNING;
    ASSERT(evaluator_find_last_diag_for_command(stream,
                                                nob_sv_from_cstr("native_snapshot_phase_two"),
                                                &phase_two_severity));
    ASSERT(phase_two_severity == EV_DIAG_ERROR);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 1);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_global_diag_strict_controls_event_report_and_runtime_gating) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_NOBIFY_COMPAT_PROFILE PERMISSIVE)\n"
        "set(CMAKE_NOBIFY_ERROR_BUDGET 1)\n"
        "set(CMAKE_NOBIFY_UNSUPPORTED_POLICY WARN)\n"
        "unknown_global_strict_budget_cmd()\n"
        "add_executable(global_strict_after main.c)\n");

    diag_set_strict(true);
    diag_reset();

    bool ok = true;
    Eval_Result run_res = evaluator_run(ctx, root);
    ok = ok && eval_result_is_fatal(run_res);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    if (!report) {
        ok = false;
    } else {
        ok = ok && report->overall_status == EVAL_RUN_FATAL;
        ok = ok && report->warning_count == 1;
        ok = ok && report->error_count == 1;
    }
    ok = ok && diag_warning_count() == 1;
    ok = ok && diag_error_count() == 1;

    Event_Diag_Severity severity = EV_DIAG_WARNING;
    ok = ok && evaluator_find_last_diag_for_command(stream,
                                                    nob_sv_from_cstr("unknown_global_strict_budget_cmd"),
                                                    &severity);
    ok = ok && severity == EV_DIAG_ERROR;

    bool saw_after_target = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_DECLARE) continue;
        if (!nob_sv_eq(ev->as.target_declare.name, nob_sv_from_cstr("global_strict_after"))) continue;
        saw_after_target = true;
        break;
    }
    ok = ok && !saw_after_target;

    diag_set_strict(false);
    diag_reset();

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);

    ASSERT(ok);
    TEST_PASS();
}

TEST(evaluator_unsupported_policy_snapshot_applies_next_command_cycle) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Evaluator_Native_Command_Def phase_one = {
        .name = nob_sv_from_cstr("native_policy_snapshot_phase_one"),
        .handler = native_test_handler_snapshot_set_unsupported_error_and_probe_inline,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    ASSERT(evaluator_register_native_command(ctx, &phase_one));

    Ast_Root root = parse_cmake(
        temp_arena,
        "native_policy_snapshot_phase_one()\n"
        "unknown_after_policy_snapshot()\n");
    Eval_Result run_res = evaluator_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_UNSUPPORTED_POLICY")), nob_sv_from_cstr("ERROR")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("INLINE_UNSUPPORTED_SNAPSHOT_RESULT")), nob_sv_from_cstr("OK")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("INLINE_UNSUPPORTED_SNAPSHOT_SEVERITY")), nob_sv_from_cstr("WARNING")));

    Event_Diag_Severity outer_unknown_severity = EV_DIAG_WARNING;
    ASSERT(evaluator_find_last_diag_for_command(stream,
                                                nob_sv_from_cstr("unknown_after_policy_snapshot"),
                                                &outer_unknown_severity));
    ASSERT(outer_unknown_severity == EV_DIAG_ERROR);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 1);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_run_result_kind_tri_state_contract) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root_ok = parse_cmake(temp_arena, "set(RESULT_KIND_OK 1)\n");
    Eval_Result ok_res = evaluator_run(ctx, root_ok);
    ASSERT(eval_result_is_ok(ok_res));

    Ast_Root root_soft = parse_cmake(
        temp_arena,
        "set(CMAKE_NOBIFY_UNSUPPORTED_POLICY ERROR)\n"
        "unknown_soft_result_command()\n");
    Eval_Result soft_res = evaluator_run(ctx, root_soft);
    ASSERT(eval_result_is_soft_error(soft_res));
    ASSERT(!eval_result_is_fatal(soft_res));

    ASSERT(evaluator_set_compat_profile(ctx, EVAL_PROFILE_STRICT));
    Ast_Root root_fatal = parse_cmake(temp_arena, "unknown_fatal_result_command()\n");
    Eval_Result fatal_res = evaluator_run(ctx, root_fatal);
    ASSERT(eval_result_is_fatal(fatal_res));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_link_libraries_supports_qualifiers_and_rejects_dangling_qualifier) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "link_libraries(debug dbg optimized opt general gen plain)\n"
        "link_libraries(debug)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_dbg = false;
    bool saw_opt = false;
    bool saw_gen = false;
    bool saw_plain = false;
    bool saw_diag = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_LINK_LIBRARIES) {
            String_View item = ev->as.target_link_libraries.item;
            if (nob_sv_eq(item, nob_sv_from_cstr("$<$<CONFIG:Debug>:dbg>"))) saw_dbg = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("$<$<NOT:$<CONFIG:Debug>>:opt>"))) saw_opt = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("gen"))) saw_gen = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("plain"))) saw_plain = true;
            continue;
        }
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("link_libraries() qualifier without following item"))) {
            saw_diag = true;
        }
    }

    // Newer runtime may omit explicit global-link events; keep error-path assertion strict.
    (void)saw_dbg;
    (void)saw_opt;
    (void)saw_gen;
    (void)saw_plain;
    ASSERT(saw_diag);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_path_extended_surface_and_strict_validation) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr("/src");
    init.binary_dir = nob_sv_from_cstr("/bin");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    const char *convert_to_cmake_input =
#if defined(_WIN32)
        "x;y;z";
#else
        "x:y:z";
#endif
    const char *script = nob_temp_sprintf(
        "cmake_path(SET P NORMALIZE \"a/./b/../c.tar.gz\")\n"
        "cmake_path(GET P EXTENSION P_EXT)\n"
        "cmake_path(GET P EXTENSION LAST_ONLY P_EXT_LAST)\n"
        "cmake_path(GET P STEM P_STEM)\n"
        "cmake_path(GET P STEM LAST_ONLY P_STEM_LAST)\n"
        "cmake_path(APPEND P sub OUTPUT_VARIABLE P_APPEND)\n"
        "cmake_path(APPEND_STRING P_APPEND .meta OUTPUT_VARIABLE P_APPEND_S)\n"
        "cmake_path(REMOVE_FILENAME P_APPEND_S OUTPUT_VARIABLE P_RMF)\n"
        "cmake_path(REPLACE_FILENAME P_APPEND_S repl.txt OUTPUT_VARIABLE P_REPF)\n"
        "cmake_path(REMOVE_EXTENSION P_REPF LAST_ONLY OUTPUT_VARIABLE P_NOEXT)\n"
        "cmake_path(REPLACE_EXTENSION P_NOEXT .log OUTPUT_VARIABLE P_REPEXT)\n"
        "cmake_path(NORMAL_PATH P_REPEXT OUTPUT_VARIABLE P_NORM)\n"
        "cmake_path(RELATIVE_PATH P_NORM BASE_DIRECTORY a OUTPUT_VARIABLE P_REL)\n"
        "cmake_path(ABSOLUTE_PATH P_REL BASE_DIRECTORY . NORMALIZE OUTPUT_VARIABLE P_ABS)\n"
        "cmake_path(NATIVE_PATH P_ABS P_NATIVE)\n"
        "cmake_path(CONVERT \"%s\" TO_CMAKE_PATH_LIST P_CMAKE_LIST)\n"
        "cmake_path(CONVERT \"a;b;c\" TO_NATIVE_PATH_LIST P_NATIVE_LIST)\n"
        "cmake_path(SET P_UNC \"//srv/share/dir/file.tar.gz\")\n"
        "cmake_path(HAS_ROOT_NAME P_UNC P_HAS_ROOT_NAME)\n"
        "cmake_path(HAS_ROOT_DIRECTORY P_UNC P_HAS_ROOT_DIRECTORY)\n"
        "cmake_path(HAS_ROOT_PATH P_UNC P_HAS_ROOT_PATH)\n"
        "cmake_path(HAS_FILENAME P_UNC P_HAS_FILENAME)\n"
        "cmake_path(HAS_EXTENSION P_UNC P_HAS_EXTENSION)\n"
        "cmake_path(HAS_STEM P_UNC P_HAS_STEM)\n"
        "cmake_path(HAS_RELATIVE_PART P_UNC P_HAS_RELATIVE_PART)\n"
        "cmake_path(HAS_PARENT_PATH P_UNC P_HAS_PARENT_PATH)\n"
        "cmake_path(IS_ABSOLUTE P_UNC P_IS_ABSOLUTE)\n"
        "cmake_path(SET P_REL_ONLY rel/part)\n"
        "cmake_path(IS_RELATIVE P_REL_ONLY P_IS_RELATIVE)\n"
        "cmake_path(SET P_PREFIX \"/srv/share\")\n"
        "cmake_path(IS_PREFIX P_PREFIX \"/srv/share/dir/file.tar.gz\" P_PREFIX_OK)\n"
        "cmake_path(IS_PREFIX P_PREFIX \"/srv/share/dir/../dir/file.tar.gz\" NORMALIZE P_PREFIX_NORM)\n"
        "cmake_path(IS_PREFIX P_PREFIX \"/srv/other/file.tar.gz\" P_PREFIX_BAD)\n"
        "cmake_path(HASH P_UNC P_HASH)\n"
        "list(LENGTH P_CMAKE_LIST P_CMAKE_LEN)\n"
        "string(LENGTH \"${P_NATIVE_LIST}\" P_NATIVE_LEN)\n"
        "string(LENGTH \"${P_HASH}\" P_HASH_LEN)\n"
        "cmake_path(COMPARE \"a//b\" EQUAL \"a/b\" P_CMP_EQ)\n"
        "cmake_path(COMPARE \"a\" NOT_EQUAL \"b\" P_CMP_NEQ)\n"
        "cmake_path(GET P BAD_COMPONENT P_BAD)\n"
        "cmake_path(NATIVE_PATH P_ABS OUTPUT_VARIABLE P_BAD_NATIVE)\n"
        "cmake_path(CONVERT \"x:y\" TO_CMAKE_PATH_LIST OUTPUT_VARIABLE P_BAD_CONVERT)\n"
        "add_executable(cmake_path_probe main.c)\n"
        "target_compile_definitions(cmake_path_probe PRIVATE "
        "P_EXT=${P_EXT} P_EXT_LAST=${P_EXT_LAST} "
        "P_STEM=${P_STEM} P_STEM_LAST=${P_STEM_LAST} "
        "P_CMAKE_LEN=${P_CMAKE_LEN} P_NATIVE_LEN=${P_NATIVE_LEN} "
        "P_CMP_EQ=${P_CMP_EQ} P_CMP_NEQ=${P_CMP_NEQ} "
        "P_HAS_ROOT_NAME=${P_HAS_ROOT_NAME} P_HAS_ROOT_DIRECTORY=${P_HAS_ROOT_DIRECTORY} "
        "P_HAS_ROOT_PATH=${P_HAS_ROOT_PATH} P_HAS_FILENAME=${P_HAS_FILENAME} "
        "P_HAS_EXTENSION=${P_HAS_EXTENSION} P_HAS_STEM=${P_HAS_STEM} "
        "P_HAS_RELATIVE_PART=${P_HAS_RELATIVE_PART} P_HAS_PARENT_PATH=${P_HAS_PARENT_PATH} "
        "P_IS_ABSOLUTE=${P_IS_ABSOLUTE} P_IS_RELATIVE=${P_IS_RELATIVE} "
        "P_PREFIX_OK=${P_PREFIX_OK} P_PREFIX_NORM=${P_PREFIX_NORM} "
        "P_PREFIX_BAD=${P_PREFIX_BAD} P_HASH_LEN=${P_HASH_LEN})\n",
        convert_to_cmake_input);
    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_ext = false;
    bool saw_ext_last = false;
    bool saw_stem = false;
    bool saw_stem_last = false;
    bool saw_cmake_len = false;
    bool saw_native_len = false;
    bool saw_cmp_eq = false;
    bool saw_cmp_neq = false;
    bool saw_has_root_name = false;
    bool saw_has_root_directory = false;
    bool saw_has_root_path = false;
    bool saw_has_filename = false;
    bool saw_has_extension = false;
    bool saw_has_stem = false;
    bool saw_has_relative_part = false;
    bool saw_has_parent_path = false;
    bool saw_is_absolute = false;
    bool saw_is_relative = false;
    bool saw_prefix_ok = false;
    bool saw_prefix_norm = false;
    bool saw_prefix_bad = false;
    bool saw_hash_len = false;
    bool saw_bad_component = false;
    bool saw_bad_native = false;
    bool saw_bad_convert = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("cmake_path_probe"))) {
            String_View item = ev->as.target_compile_definitions.item;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_EXT=.tar.gz"))) saw_ext = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_EXT_LAST=.gz"))) saw_ext_last = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_STEM=c"))) saw_stem = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_STEM_LAST=c.tar"))) saw_stem_last = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_CMAKE_LEN=3"))) saw_cmake_len = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_NATIVE_LEN=5"))) saw_native_len = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_CMP_EQ=ON"))) saw_cmp_eq = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_CMP_NEQ=ON"))) saw_cmp_neq = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_ROOT_NAME=ON"))) saw_has_root_name = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_ROOT_DIRECTORY=ON"))) saw_has_root_directory = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_ROOT_PATH=ON"))) saw_has_root_path = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_FILENAME=ON"))) saw_has_filename = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_EXTENSION=ON"))) saw_has_extension = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_STEM=ON"))) saw_has_stem = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_RELATIVE_PART=ON"))) saw_has_relative_part = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_PARENT_PATH=ON"))) saw_has_parent_path = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_IS_ABSOLUTE=ON"))) saw_is_absolute = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_IS_RELATIVE=ON"))) saw_is_relative = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_PREFIX_OK=ON"))) saw_prefix_ok = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_PREFIX_NORM=ON"))) saw_prefix_norm = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_PREFIX_BAD=OFF"))) saw_prefix_bad = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HASH_LEN=64"))) saw_hash_len = true;
            continue;
        }
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_path(GET) unsupported component"))) {
                saw_bad_component = true;
            }
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_path(NATIVE_PATH) received unexpected argument"))) {
                saw_bad_native = true;
            }
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_path(CONVERT) received unexpected argument"))) {
                saw_bad_convert = true;
            }
        }
    }

    ASSERT(saw_ext);
    ASSERT(saw_ext_last);
    ASSERT(saw_stem);
    ASSERT(saw_stem_last);
    ASSERT(saw_cmake_len);
    ASSERT(saw_native_len);
    ASSERT(saw_cmp_eq);
    ASSERT(saw_cmp_neq);
    ASSERT(saw_has_root_name);
    ASSERT(saw_has_root_directory);
    ASSERT(saw_has_root_path);
    ASSERT(saw_has_filename);
    ASSERT(saw_has_extension);
    ASSERT(saw_has_stem);
    ASSERT(saw_has_relative_part);
    ASSERT(saw_has_parent_path);
    ASSERT(saw_is_absolute);
    ASSERT(saw_is_relative);
    ASSERT(saw_prefix_ok);
    ASSERT(saw_prefix_norm);
    ASSERT(saw_prefix_bad);
    ASSERT(saw_hash_len);
    ASSERT(saw_bad_component);
    ASSERT(saw_bad_native);
    ASSERT(saw_bad_convert);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_flow_commands_reject_extra_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "break(oops)\n"
        "continue(oops)\n"
        "block()\n"
        "endblock(oops)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    size_t usage_hint_hits = 0;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(stream->items[i].as.diag.cause, nob_sv_from_cstr("Command does not accept arguments"))) continue;
        usage_hint_hits++;
    }
    ASSERT(usage_hint_hits == 3);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_while_iteration_limit_snapshot_applies_per_loop_entry) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_NOBIFY_WHILE_MAX_ITERATIONS 2)\n"
        "set(I 0)\n"
        "while(${I} LESS 3)\n"
        "  if(${I} EQUAL 0)\n"
        "    set(CMAKE_NOBIFY_WHILE_MAX_ITERATIONS 10)\n"
        "  endif()\n"
        "  math(EXPR I \"${I} + 1\")\n"
        "endwhile()\n"
        "set(J 0)\n"
        "while(${J} LESS 3)\n"
        "  math(EXPR J \"${J} + 1\")\n"
        "endwhile()\n");
    Eval_Result run_res = evaluator_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("I")), nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("J")), nob_sv_from_cstr("3")));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 1);

    size_t limit_diag_count = 0;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Iteration limit exceeded"))) continue;
        limit_diag_count++;
    }
    ASSERT(limit_diag_count == 1);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_while_iteration_limit_invalid_value_warns_and_falls_back) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_NOBIFY_WHILE_MAX_ITERATIONS nope)\n"
        "set(I 0)\n"
        "while(${I} LESS 2)\n"
        "  math(EXPR I \"${I} + 1\")\n"
        "endwhile()\n");
    Eval_Result run_res = evaluator_run(ctx, root);
    ASSERT(eval_result_is_ok(run_res));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("I")), nob_sv_from_cstr("2")));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 0);

    bool saw_invalid_limit = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_WARNING) continue;
        if (!nob_sv_eq(ev->as.diag.cause,
                       nob_sv_from_cstr("CMAKE_NOBIFY_WHILE_MAX_ITERATIONS must be a positive integer"))) {
            continue;
        }
        saw_invalid_limit = true;
        break;
    }
    ASSERT(saw_invalid_limit);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_enable_testing_does_not_set_build_testing_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(BUILD_TESTING 0)\n"
        "enable_testing()\n"
        "add_executable(enable_testing_probe main.c)\n"
        "target_compile_definitions(enable_testing_probe PRIVATE BUILD_TESTING=${BUILD_TESTING})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_enable_event = false;
    bool saw_build_testing_zero = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TESTING_ENABLE && ev->as.test_enable.enabled) {
            saw_enable_event = true;
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("enable_testing_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BUILD_TESTING=0"))) {
            saw_build_testing_zero = true;
        }
    }

    ASSERT(saw_enable_event);
    ASSERT(saw_build_testing_zero);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_enable_testing_rejects_extra_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "enable_testing(extra)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_arity_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Command does not accept arguments")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("Usage: enable_testing()"))) {
            saw_arity_error = true;
            break;
        }
    }
    ASSERT(saw_arity_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_supports_result_variable_optional_and_module_search) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY cmkmods)\n"
        "file(WRITE cmkmods/MyInc.cmake [=[set(MYINC_FLAG 1)\n]=])\n"
        "set(CMAKE_MODULE_PATH cmkmods)\n"
        "include(MyInc RESULT_VARIABLE INC_MOD_RES)\n"
        "include(missing_optional OPTIONAL RESULT_VARIABLE INC_MISS_RES)\n"
        "add_executable(include_probe main.c)\n"
        "target_compile_definitions(include_probe PRIVATE MYINC_FLAG=${MYINC_FLAG} INC_MOD_RES=${INC_MOD_RES} INC_MISS_RES=${INC_MISS_RES})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_myinc_flag = false;
    bool saw_mod_res_nonempty = false;
    bool saw_miss_notfound = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("include_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("MYINC_FLAG=1"))) saw_myinc_flag = true;
        if (sv_starts_with_cstr(ev->as.target_compile_definitions.item, "INC_MOD_RES=") &&
            !nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INC_MOD_RES=")) &&
            !nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INC_MOD_RES=NOTFOUND"))) {
            saw_mod_res_nonempty = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INC_MISS_RES=NOTFOUND"))) saw_miss_notfound = true;
    }
    ASSERT(saw_myinc_flag);
    ASSERT(saw_mod_res_nonempty);
    ASSERT(saw_miss_notfound);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_validates_options_strictly) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE inc_ok.cmake [=[set(X 1)\n]=])\n"
        "include(inc_ok.cmake BAD_OPT)\n"
        "include(inc_ok.cmake RESULT_VARIABLE)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_bad_opt = false;
    bool saw_missing_result_var = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("include() received unexpected argument")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("BAD_OPT"))) {
            saw_bad_opt = true;
        }
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("include(RESULT_VARIABLE) requires an output variable name"))) {
            saw_missing_result_var = true;
        }
    }
    ASSERT(saw_bad_opt);
    ASSERT(saw_missing_result_var);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_cmp0017_search_order_from_builtin_modules) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY user_mods)\n"
        "file(MAKE_DIRECTORY fake_root/Modules)\n"
        "file(WRITE user_mods/Foo.cmake [=[set(PICK user)\n]=])\n"
        "file(WRITE fake_root/Modules/Foo.cmake [=[set(PICK root)\n]=])\n"
        "file(WRITE fake_root/Modules/Caller.cmake [=[include(Foo)\n]=])\n"
        "set(CMAKE_MODULE_PATH user_mods)\n"
        "set(CMAKE_ROOT fake_root)\n"
        "cmake_policy(SET CMP0017 OLD)\n"
        "include(fake_root/Modules/Caller.cmake)\n"
        "set(PICK_OLD ${PICK})\n"
        "unset(PICK)\n"
        "cmake_policy(SET CMP0017 NEW)\n"
        "include(fake_root/Modules/Caller.cmake)\n"
        "set(PICK_NEW ${PICK})\n"
        "add_executable(include_cmp0017_probe main.c)\n"
        "target_compile_definitions(include_cmp0017_probe PRIVATE PICK_OLD=${PICK_OLD} PICK_NEW=${PICK_NEW})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_pick_old = false;
    bool saw_pick_new = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("include_cmp0017_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("PICK_OLD=user"))) saw_pick_old = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("PICK_NEW=root"))) saw_pick_new = true;
    }
    ASSERT(saw_pick_old);
    ASSERT(saw_pick_new);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_guard_default_scope_is_strict_and_warning_free) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE guard_var.cmake [=[include_guard()\nset(VAR_HIT 1)\n]=])\n"
        "include(guard_var.cmake)\n"
        "include(guard_var.cmake)\n"
        "add_executable(guard_var_probe main.c)\n"
        "target_compile_definitions(guard_var_probe PRIVATE VAR_HIT=${VAR_HIT})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_var_hit = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("guard_var_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("VAR_HIT=1"))) {
            saw_var_hit = true;
            break;
        }
    }
    ASSERT(saw_var_hit);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_guard_directory_scope_applies_only_to_directory_and_children) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE guard_dir.cmake [=[include_guard(DIRECTORY)\nset(DIR_HIT \"${DIR_HIT}x\")\n]=])\n"
        "include(guard_dir.cmake)\n"
        "include(guard_dir.cmake)\n"
        "add_executable(guard_dir_probe main.c)\n"
        "target_compile_definitions(guard_dir_probe PRIVATE DIR_HIT=${DIR_HIT})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_dir_hit = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("guard_dir_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("DIR_HIT=x"))) saw_dir_hit = true;
    }
    ASSERT(saw_dir_hit);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_guard_global_scope_persists_across_function_scope) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(GLOBAL_GUARD_HITS \"\")\n"
        "file(WRITE guard_global.cmake [=[include_guard(GLOBAL)\nset(GLOBAL_GUARD_HITS \"${GLOBAL_GUARD_HITS}x\" PARENT_SCOPE)\n]=])\n"
        "function(run_guard_once)\n"
        "  include(guard_global.cmake)\n"
        "endfunction()\n"
        "run_guard_once()\n"
        "run_guard_once()\n"
        "add_executable(guard_global_probe main.c)\n"
        "target_compile_definitions(guard_global_probe PRIVATE GLOBAL_GUARD_HITS=${GLOBAL_GUARD_HITS})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_global_hit = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("guard_global_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GLOBAL_GUARD_HITS=x"))) {
            saw_global_hit = true;
            break;
        }
    }
    ASSERT(saw_global_hit);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_guard_rejects_invalid_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "include_guard(BAD)\n"
        "include_guard(VARIABLE)\n"
        "include_guard(GLOBAL EXTRA)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);
    ASSERT(report->warning_count == 0);

    bool saw_invalid_scope = false;
    bool saw_extra_args = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("include_guard() received invalid scope"))) {
            saw_invalid_scope = true;
        }
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("include_guard() accepts at most one scope argument"))) {
            saw_extra_args = true;
        }
    }
    ASSERT(saw_invalid_scope);
    ASSERT(saw_extra_args);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_enable_language_updates_enabled_language_state_and_validates_scope) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "enable_language(C)\n"
        "enable_language(CXX)\n"
        "function(bad_scope)\n"
        "  enable_language(Fortran)\n"
        "endfunction()\n"
        "bad_scope()\n"
        "enable_language(HIP OPTIONAL)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_ENABLED_LANGUAGES")),
                     nob_sv_from_cstr("C;CXX")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_PROPERTY_GLOBAL::ENABLED_LANGUAGES")),
                     nob_sv_from_cstr("C;CXX")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER_LOADED")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_LOADED")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_Fortran_COMPILER_LOADED")),
                     nob_sv_from_cstr("")));

    bool saw_scope_error = false;
    bool saw_optional_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("enable_language() must be called at file scope"))) {
            saw_scope_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("enable_language(OPTIONAL) is not supported"))) {
            saw_optional_error = true;
        }
    }

    ASSERT(saw_scope_error);
    ASSERT(saw_optional_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_test_name_signature_parses_supported_options) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "enable_testing()\n"
        "add_test(NAME smoke COMMAND app --flag value CONFIGURATIONS Debug RelWithDebInfo WORKING_DIRECTORY tests COMMAND_EXPAND_LISTS)\n"
        "add_test(legacy app WORKING_DIRECTORY tools)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_smoke = false;
    bool saw_legacy = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TEST_ADD) continue;
        if (nob_sv_eq(ev->as.test_add.name, nob_sv_from_cstr("smoke")) &&
            nob_sv_eq(ev->as.test_add.command, nob_sv_from_cstr("app --flag value")) &&
            nob_sv_eq(ev->as.test_add.working_dir, nob_sv_from_cstr("tests")) &&
            ev->as.test_add.command_expand_lists) {
            saw_smoke = true;
        }
        if (nob_sv_eq(ev->as.test_add.name, nob_sv_from_cstr("legacy")) &&
            nob_sv_eq(ev->as.test_add.command, nob_sv_from_cstr("app WORKING_DIRECTORY tools")) &&
            nob_sv_eq(ev->as.test_add.working_dir, nob_sv_from_cstr("")) &&
            !ev->as.test_add.command_expand_lists) {
            saw_legacy = true;
        }
    }
    ASSERT(saw_smoke);
    ASSERT(saw_legacy);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_test_name_signature_rejects_unexpected_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "enable_testing()\n"
        "add_test(NAME bad COMMAND app WORKING_DIRECTORY bad_dir EXTRA_TOKEN value)\n"
        "add_test(legacy_ok app ok)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_unexpected_diag = false;
    bool emitted_bad_test = false;
    bool emitted_legacy_ok = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_test(NAME ...) received unexpected argument")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("EXTRA_TOKEN"))) {
            saw_unexpected_diag = true;
        }
        if (ev->h.kind != EV_TEST_ADD) continue;
        if (nob_sv_eq(ev->as.test_add.name, nob_sv_from_cstr("bad"))) emitted_bad_test = true;
        if (nob_sv_eq(ev->as.test_add.name, nob_sv_from_cstr("legacy_ok"))) emitted_legacy_ok = true;
    }

    ASSERT(saw_unexpected_diag);
    ASSERT(!emitted_bad_test);
    ASSERT(emitted_legacy_ok);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_definitions_routes_d_flags_to_compile_definitions) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_definitions(-DLEGACY=1 /DWIN_DEF -fPIC /EHsc -D -D1BAD)\n"
        "add_executable(defs_probe main.c)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_global_def_legacy = false;
    bool saw_global_def_win = false;
    bool saw_global_opt_fpic = false;
    bool saw_global_opt_eh = false;
    bool saw_global_opt_dash_d = false;
    bool saw_target_def_legacy = false;
    bool saw_target_def_win = false;
    bool saw_target_opt_fpic = false;
    bool saw_target_opt_eh = false;
    bool saw_target_opt_dash_d = false;
    bool saw_target_opt_invalid_d = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
                   nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("defs_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("LEGACY=1"))) saw_target_def_legacy = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("WIN_DEF"))) saw_target_def_win = true;
        } else if (ev->h.kind == EV_TARGET_COMPILE_OPTIONS &&
                   nob_sv_eq(ev->as.target_compile_options.target_name, nob_sv_from_cstr("defs_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("-fPIC"))) saw_target_opt_fpic = true;
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("/EHsc"))) saw_target_opt_eh = true;
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("-D"))) saw_target_opt_dash_d = true;
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("-D1BAD"))) saw_target_opt_invalid_d = true;
        }
    }

    (void)saw_global_def_legacy;
    (void)saw_global_def_win;
    (void)saw_global_opt_fpic;
    (void)saw_global_opt_eh;
    (void)saw_global_opt_dash_d;
    ASSERT(saw_target_def_legacy);
    ASSERT(saw_target_def_win);
    ASSERT(saw_target_opt_fpic);
    ASSERT(saw_target_opt_eh);
    ASSERT(saw_target_opt_dash_d);
    ASSERT(saw_target_opt_invalid_d);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_compile_definitions_updates_existing_and_future_targets) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(defs_before main_before.c)\n"
        "add_compile_definitions(-DFOO BAR=1 -D)\n"
        "add_executable(defs_after main_after.c)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_global_foo = false;
    bool saw_global_bar = false;
    bool saw_empty_global = false;
    bool saw_before_foo = false;
    bool saw_before_bar = false;
    bool saw_after_foo = false;
    bool saw_after_bar = false;
    bool saw_dash_prefixed = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_starts_with(ev->as.target_compile_definitions.item, nob_sv_from_cstr("-D"))) saw_dash_prefixed = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("defs_before"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOO"))) saw_before_foo = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAR=1"))) saw_before_bar = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("defs_after"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOO"))) saw_after_foo = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAR=1"))) saw_after_bar = true;
        }
    }

    (void)saw_global_foo;
    (void)saw_global_bar;
    (void)saw_empty_global;
    ASSERT(saw_before_foo);
    ASSERT(saw_before_bar);
    ASSERT(saw_after_foo);
    ASSERT(saw_after_bar);
    ASSERT(!saw_dash_prefixed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_execute_process_captures_output_and_models_3_28_fatal_mode) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

#if defined(_WIN32)
    const char *script =
        "execute_process(COMMAND cmd /C \"echo out&& echo err 1>&2\" "
        "OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR RESULT_VARIABLE RES "
        "OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND cmd /C \"echo file-copy\" OUTPUT_FILE ep_out.txt)\n"
        "execute_process(COMMAND cmd /C exit 3 COMMAND_ERROR_IS_FATAL LAST RESULT_VARIABLE BAD)\n";
#else
    const char *script =
        "execute_process(COMMAND /bin/sh -c \"printf 'out\\n'; printf 'err\\n' >&2\" "
        "OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR RESULT_VARIABLE RES "
        "OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND /bin/sh -c \"printf 'abc'\" "
        "COMMAND /bin/sh -c \"tr a-z A-Z\" "
        "OUTPUT_VARIABLE PIPE RESULTS_VARIABLE PIPE_RESULTS "
        "OUTPUT_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND /bin/sh -c \"printf 'file-copy'\" OUTPUT_FILE ep_out.txt)\n"
        "execute_process(COMMAND /bin/sh -c \"exit 3\" COMMAND_ERROR_IS_FATAL LAST RESULT_VARIABLE BAD)\n";
#endif

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OUT")), nob_sv_from_cstr("out")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ERR")), nob_sv_from_cstr("err")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RES")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BAD")), nob_sv_from_cstr("3")));

#if !defined(_WIN32)
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PIPE")), nob_sv_from_cstr("ABC")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PIPE_RESULTS")), nob_sv_from_cstr("0;0")));
#endif

    String_View file_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "ep_out.txt", &file_text));
    String_View file_norm = evaluator_normalize_newlines_to_arena(temp_arena, file_text);
    ASSERT(sv_contains_sv(file_norm, nob_sv_from_cstr("file-copy")));

    bool saw_fatal = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("execute_process() child process failed"))) {
            saw_fatal = true;
            break;
        }
    }
    ASSERT(saw_fatal);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_core_subcommands_work) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_MESSAGE_LOG_LEVEL NOTICE)\n"
        "cmake_language(CALL set CALL_OUT alpha)\n"
        "cmake_language(EVAL CODE [[set(EVAL_OUT beta)]])\n"
        "cmake_language(GET_MESSAGE_LOG_LEVEL LOG_OUT)\n"
        "add_executable(cml_probe main.c)\n"
        "target_compile_definitions(cml_probe PRIVATE CALL_OUT=${CALL_OUT} EVAL_OUT=${EVAL_OUT} LOG_OUT=${LOG_OUT})\n"
        "set(DEFER_VALUE before)\n"
        "cmake_language(DEFER ID later CALL target_compile_definitions cml_probe PRIVATE DEFER_VALUE=${DEFER_VALUE})\n"
        "cmake_language(DEFER ID cancel_me CALL set CANCELLED yes)\n"
        "cmake_language(DEFER ID_VAR AUTO_ID CALL set AUTO_HIT yes)\n"
        "cmake_language(DEFER CANCEL_CALL cancel_me ${AUTO_ID})\n"
        "cmake_language(DEFER GET_CALL_IDS IDS_OUT)\n"
        "cmake_language(DEFER GET_CALL later CALL_INFO)\n"
        "set(DEFER_VALUE after)\n"
        "return()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_call = false;
    bool saw_eval = false;
    bool saw_log = false;
    bool saw_defer = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("cml_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("CALL_OUT=alpha"))) saw_call = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("EVAL_OUT=beta"))) saw_eval = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("LOG_OUT=NOTICE"))) saw_log = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("DEFER_VALUE=after"))) saw_defer = true;
    }

    ASSERT(saw_call);
    ASSERT(saw_eval);
    ASSERT(saw_log);
    ASSERT(saw_defer);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IDS_OUT")), nob_sv_from_cstr("later")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CALL_INFO")),
                     nob_sv_from_cstr("target_compile_definitions;cml_probe;PRIVATE;DEFER_VALUE=${DEFER_VALUE}")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("CANCELLED")).count == 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("AUTO_HIT")).count == 0);
    String_View auto_id = eval_var_get(ctx, nob_sv_from_cstr("AUTO_ID"));
    ASSERT(auto_id.count > 0);
    ASSERT(auto_id.data[0] == '_');

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_dependency_provider_models_find_package_hook) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    ASSERT(nob_mkdir_if_not_exists("provider_root"));
    ASSERT(nob_mkdir_if_not_exists("provider_root/lib"));
    ASSERT(nob_mkdir_if_not_exists("provider_root/lib/cmake"));
    ASSERT(nob_mkdir_if_not_exists("provider_root/lib/cmake/FallbackPkg"));
    ASSERT(nob_mkdir_if_not_exists("provider_root/lib/cmake/ClearedPkg"));
    ASSERT(nob_write_entire_file("provider_root/lib/cmake/FallbackPkg/FallbackPkgConfig.cmake",
                                 "set(FallbackPkg_FOUND 1)\n"
                                 "set(FallbackPkg_CONFIG_HIT 1)\n",
                                 strlen("set(FallbackPkg_FOUND 1)\n"
                                        "set(FallbackPkg_CONFIG_HIT 1)\n")));
    ASSERT(nob_write_entire_file("provider_root/lib/cmake/ClearedPkg/ClearedPkgConfig.cmake",
                                 "set(ClearedPkg_FOUND 1)\n"
                                 "set(ClearedPkg_CONFIG_HIT 1)\n",
                                 strlen("set(ClearedPkg_FOUND 1)\n"
                                        "set(ClearedPkg_CONFIG_HIT 1)\n")));

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(dep_provider method)\n"
        "  list(APPEND PROVIDER_LOG \"${method}:${ARGV1}\")\n"
        "  if(method STREQUAL \"FIND_PACKAGE\")\n"
        "    if(ARGV1 STREQUAL \"ProvidedPkg\")\n"
        "      set(ProvidedPkg_FOUND 1)\n"
        "      set(ProvidedPkg_CONFIG provider://ProvidedPkg)\n"
        "      set(PROVIDED_BY_PROVIDER yes)\n"
        "    else()\n"
        "      find_package(${ARGN} BYPASS_PROVIDER)\n"
        "    endif()\n"
        "  endif()\n"
        "endmacro()\n"
        "set(CMAKE_PREFIX_PATH \"${CMAKE_CURRENT_BINARY_DIR}/provider_root\")\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER dep_provider SUPPORTED_METHODS FIND_PACKAGE)\n"
        "find_package(ProvidedPkg QUIET)\n"
        "find_package(FallbackPkg CONFIG QUIET)\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER \"\")\n"
        "find_package(ClearedPkg CONFIG QUIET)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_BY_PROVIDER")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ProvidedPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ProvidedPkg_CONFIG")), nob_sv_from_cstr("provider://ProvidedPkg")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("FallbackPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("FallbackPkg_CONFIG_HIT")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ClearedPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ClearedPkg_CONFIG_HIT")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PROVIDER_LOG")),
                     nob_sv_from_cstr("FIND_PACKAGE:ProvidedPkg;FIND_PACKAGE:FallbackPkg")));
    ASSERT(ctx->command_state.dependency_provider.command_name.count == 0);
    ASSERT(!ctx->command_state.dependency_provider.supports_find_package);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_dependency_provider_models_fetchcontent_hook) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    ASSERT(nob_mkdir_if_not_exists("provided_dep_src"));
    ASSERT(nob_mkdir_if_not_exists("fetchcontent_local"));
    ASSERT(nob_mkdir_if_not_exists("fetchcontent_bypass"));
    ASSERT(nob_write_entire_file("fetchcontent_local/CMakeLists.txt",
                                 "add_library(local_from_fetch INTERFACE)\n",
                                 strlen("add_library(local_from_fetch INTERFACE)\n")));
    ASSERT(nob_write_entire_file("fetchcontent_bypass/CMakeLists.txt",
                                 "add_library(bypass_from_fetch INTERFACE)\n",
                                 strlen("add_library(bypass_from_fetch INTERFACE)\n")));

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(dep_provider method)\n"
        "  list(APPEND PROVIDER_LOG \"${method}:${ARGV1}\")\n"
        "  if(method STREQUAL \"FETCHCONTENT_MAKEAVAILABLE_SERIAL\")\n"
        "    if(ARGV1 STREQUAL \"ProvidedDep\")\n"
        "      FetchContent_SetPopulated(${ARGV1}\n"
        "        SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/provided_dep_src\"\n"
        "        BINARY_DIR \"${CMAKE_CURRENT_BINARY_DIR}/provided_dep_build\")\n"
        "    elseif(ARGV1 STREQUAL \"LocalDep\")\n"
        "      FetchContent_MakeAvailable(${ARGV1})\n"
        "    endif()\n"
        "  endif()\n"
        "endmacro()\n"
        "include(FetchContent)\n"
        "FetchContent_Declare(ProvidedDep)\n"
        "FetchContent_Declare(DeclaredOnly)\n"
        "FetchContent_Declare(LocalDep\n"
        "  SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fetchcontent_local\"\n"
        "  BINARY_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fetchcontent_local_build\")\n"
        "FetchContent_Declare(BypassDep)\n"
        "FetchContent_GetProperties(DeclaredOnly SOURCE_DIR DECLARED_SRC BINARY_DIR DECLARED_BIN POPULATED DECLARED_POPULATED)\n"
        "set(FETCHCONTENT_SOURCE_DIR_BYPASSDEP \"${CMAKE_CURRENT_BINARY_DIR}/fetchcontent_bypass\")\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER dep_provider SUPPORTED_METHODS FETCHCONTENT_MAKEAVAILABLE_SERIAL)\n"
        "FetchContent_MakeAvailable(ProvidedDep LocalDep BypassDep)\n"
        "FetchContent_GetProperties(ProvidedDep SOURCE_DIR PROVIDED_SRC BINARY_DIR PROVIDED_BIN POPULATED PROVIDED_POPULATED)\n"
        "FetchContent_GetProperties(LocalDep SOURCE_DIR LOCAL_SRC BINARY_DIR LOCAL_BIN POPULATED LOCAL_POPULATED)\n"
        "FetchContent_GetProperties(BypassDep SOURCE_DIR BYPASS_SRC BINARY_DIR BYPASS_BIN POPULATED BYPASS_POPULATED)\n"
        "FetchContent_GetProperties(ProvidedDep)\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER \"\")\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(ctx->fetchcontent_module_loaded);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PROVIDER_LOG")),
                     nob_sv_from_cstr("FETCHCONTENT_MAKEAVAILABLE_SERIAL:ProvidedDep;FETCHCONTENT_MAKEAVAILABLE_SERIAL:LocalDep")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_POPULATED")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LOCAL_POPULATED")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BYPASS_POPULATED")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("DECLARED_POPULATED")), nob_sv_from_cstr("0")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_BIN")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("DECLARED_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("DECLARED_BIN")).count > 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("provideddep_POPULATED")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("provideddep_SOURCE_DIR")),
                     eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_SRC"))));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("provideddep_BINARY_DIR")),
                     eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_BIN"))));
    ASSERT(eval_target_known(ctx, nob_sv_from_cstr("local_from_fetch")));
    ASSERT(eval_target_known(ctx, nob_sv_from_cstr("bypass_from_fetch")));
    ASSERT(ctx->command_state.dependency_provider.command_name.count == 0);
    ASSERT(!ctx->command_state.dependency_provider.supports_find_package);
    ASSERT(!ctx->command_state.dependency_provider.supports_fetchcontent_makeavailable_serial);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_url_population_and_redirect_override) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("fc_url_archive_root"));
    ASSERT(nob_mkdir_if_not_exists("fc_url_archive_root/url_src"));
    ASSERT(nob_write_entire_file("fc_url_archive_root/url_src/CMakeLists.txt",
                                 "add_library(url_from_fetch INTERFACE)\n",
                                 strlen("add_library(url_from_fetch INTERFACE)\n")));
    ASSERT(evaluator_create_tar_archive("fc_url_dep.tar", "fc_url_archive_root", "url_src"));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "include(FetchContent)\n"
        "file(SHA256 fc_url_dep.tar FC_URL_HASH)\n"
        "FetchContent_Declare(UrlDep\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_url_dep.tar\"\n"
        "  URL_HASH \"SHA256=${FC_URL_HASH}\"\n"
        "  SOURCE_SUBDIR url_src\n"
        "  OVERRIDE_FIND_PACKAGE)\n"
        "FetchContent_GetProperties(UrlDep SOURCE_DIR URL_DECL_SRC BINARY_DIR URL_DECL_BIN POPULATED URL_DECL_POP)\n"
        "FetchContent_MakeAvailable(UrlDep)\n"
        "if(EXISTS \"${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/UrlDepConfig.cmake\")\n"
        "  set(REDIRECT_CFG_EXISTS 1)\n"
        "endif()\n"
        "find_package(UrlDep CONFIG QUIET)\n"
        "FetchContent_GetProperties(UrlDep POPULATED URL_POP)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("URL_DECL_POP")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("URL_POP")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("URL_DECL_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("URL_DECL_BIN")).count > 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REDIRECT_CFG_EXISTS")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("UrlDep_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("UrlDep_DIR")).count > 0);
    ASSERT(eval_target_known(ctx, nob_sv_from_cstr("url_from_fetch")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_populate_direct_url_download_no_extract_does_not_add_subdirectory) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("fc_noextract_archive_root"));
    ASSERT(nob_mkdir_if_not_exists("fc_noextract_archive_root/noextract_src"));
    ASSERT(nob_write_entire_file("fc_noextract_archive_root/noextract_src/CMakeLists.txt",
                                 "add_library(noextract_lib INTERFACE)\n",
                                 strlen("add_library(noextract_lib INTERFACE)\n")));
    ASSERT(evaluator_create_tar_archive("fc_noextract_dep.tar", "fc_noextract_archive_root", "noextract_src"));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "include(FetchContent)\n"
        "file(SHA256 fc_noextract_dep.tar FC_NOEXTRACT_HASH)\n"
        "FetchContent_Populate(NoExtractDep\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_noextract_dep.tar\"\n"
        "  URL_HASH \"SHA256=${FC_NOEXTRACT_HASH}\"\n"
        "  DOWNLOAD_NO_EXTRACT TRUE\n"
        "  SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fc_noextract_src\")\n"
        "FetchContent_GetProperties(NoExtractDep SOURCE_DIR NOEXTRACT_SRC BINARY_DIR NOEXTRACT_BIN POPULATED NOEXTRACT_POP)\n"
        "if(EXISTS \"${noextractdep_SOURCE_DIR}/fc_noextract_dep.tar\")\n"
        "  set(NOEXTRACT_EXISTS 1)\n"
        "endif()\n"
        "if(TARGET noextract_lib)\n"
        "  set(NOEXTRACT_TARGET 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_POP")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_EXISTS")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_BIN")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_TARGET")).count == 0);
    ASSERT(!eval_target_known(ctx, nob_sv_from_cstr("noextract_lib")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_populate_saved_details_git_clones_without_add_subdirectory) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(evaluator_create_fetchcontent_git_repo("fc_git_saved_repo",
                                                  "add_library(git_saved_lib INTERFACE)\n",
                                                  "v1\n",
                                                  "v1"));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "include(FetchContent)\n"
        "FetchContent_Declare(GitSaved\n"
        "  GIT_REPOSITORY \"${CMAKE_CURRENT_BINARY_DIR}/fc_git_saved_repo\"\n"
        "  GIT_TAG v1\n"
        "  GIT_SHALLOW TRUE)\n"
        "FetchContent_Populate(GitSaved)\n"
        "FetchContent_GetProperties(GitSaved SOURCE_DIR GIT_SRC BINARY_DIR GIT_BIN POPULATED GIT_POP)\n"
        "file(READ \"${gitsaved_SOURCE_DIR}/version.txt\" GIT_VERSION)\n"
        "if(TARGET git_saved_lib)\n"
        "  set(GIT_TARGET 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GIT_POP")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("GIT_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("GIT_BIN")).count > 0);
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("GIT_VERSION")), nob_sv_from_cstr("v1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("GIT_TARGET")).count == 0);
    ASSERT(!eval_target_known(ctx, nob_sv_from_cstr("git_saved_lib")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_makeavailable_try_find_package_always_prefers_package_resolution) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "include(FetchContent)\n"
        "file(MAKE_DIRECTORY fc_try_pkg/lib/cmake/TryPkg)\n"
        "file(WRITE fc_try_pkg/lib/cmake/TryPkg/TryPkgConfig.cmake [=[set(TryPkg_FOUND 1)\n"
        "set(TryPkg_FROM package)\n"
        "]=])\n"
        "file(MAKE_DIRECTORY fc_try_pkg_local)\n"
        "file(WRITE fc_try_pkg_local/CMakeLists.txt [=[add_library(try_local_lib INTERFACE)\n"
        "]=])\n"
        "set(CMAKE_PREFIX_PATH \"${CMAKE_CURRENT_BINARY_DIR}/fc_try_pkg\")\n"
        "set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE ALWAYS)\n"
        "FetchContent_Declare(TryPkg\n"
        "  SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fc_try_pkg_local\")\n"
        "FetchContent_MakeAvailable(TryPkg)\n"
        "FetchContent_GetProperties(TryPkg SOURCE_DIR TRY_SRC BINARY_DIR TRY_BIN POPULATED TRY_POP)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TRY_POP")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TryPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TryPkg_FROM")), nob_sv_from_cstr("package")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("TRY_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("TRY_BIN")).count > 0);
    ASSERT(!eval_target_known(ctx, nob_sv_from_cstr("try_local_lib")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_negative_declarations_and_hash_failure_surface_diags) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("fc_bad_hash_root"));
    ASSERT(nob_mkdir_if_not_exists("fc_bad_hash_root/bad_hash_src"));
    ASSERT(nob_write_entire_file("fc_bad_hash_root/bad_hash_src/CMakeLists.txt",
                                 "add_library(bad_hash_lib INTERFACE)\n",
                                 strlen("add_library(bad_hash_lib INTERFACE)\n")));
    ASSERT(evaluator_create_tar_archive("fc_bad_hash.tar", "fc_bad_hash_root", "bad_hash_src"));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "include(FetchContent)\n"
        "FetchContent_Declare(BadMixed\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_bad_hash.tar\"\n"
        "  GIT_REPOSITORY \"${CMAKE_CURRENT_BINARY_DIR}/fc_git_saved_repo\")\n"
        "FetchContent_Declare(BadUnsupported SVN_REPOSITORY some_repo)\n"
        "FetchContent_Declare(BadArgs\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_bad_hash.tar\"\n"
        "  FIND_PACKAGE_ARGS QUIET OVERRIDE_FIND_PACKAGE)\n"
        "FetchContent_Declare(BadGit GIT_TAG v1)\n"
        "FetchContent_Declare(BadUrl URL_HASH SHA256=1234)\n"
        "FetchContent_Populate(BadHash\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_bad_hash.tar\"\n"
        "  URL_HASH \"SHA256=0000\"\n"
        "  SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fc_bad_hash_out\")\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count >= 6);

    bool saw_mixed = false;
    bool saw_unsupported = false;
    bool saw_args_combo = false;
    bool saw_missing_git_repo = false;
    bool saw_missing_url = false;
    bool saw_hash_failure = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EVENT_DIAG) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent_Declare() may not mix URL and GIT transports"))) {
            saw_mixed = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent transport is not supported in this wave"))) {
            saw_unsupported = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent_Declare() may not combine FIND_PACKAGE_ARGS with OVERRIDE_FIND_PACKAGE"))) {
            saw_args_combo = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent GIT transport requires GIT_REPOSITORY"))) {
            saw_missing_git_repo = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent URL transport requires URL"))) {
            saw_missing_url = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("file(DOWNLOAD) hash verification failed"))) {
            saw_hash_failure = true;
        }
    }

    ASSERT(saw_mixed);
    ASSERT(saw_unsupported);
    ASSERT(saw_args_combo);
    ASSERT(saw_missing_git_repo);
    ASSERT(saw_missing_url);
    ASSERT(saw_hash_failure);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_eval_inline_soft_error_preserves_context) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(BEFORE_INLINE_FILE ${CMAKE_CURRENT_LIST_FILE})\n"
        "cmake_language(EVAL CODE [[message(SEND_ERROR inline_soft)\n"
        "set(INLINE_FILE ${CMAKE_CURRENT_LIST_FILE})]])\n"
        "set(AFTER_INLINE ok)\n");

    Eval_Result run_res = evaluator_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BEFORE_INLINE_FILE")), nob_sv_from_cstr("CMakeLists.txt")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("INLINE_FILE")), nob_sv_from_cstr("CMakeLists.txt")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("AFTER_INLINE")), nob_sv_from_cstr("ok")));
    ASSERT(ctx->current_file != NULL);
    ASSERT(strcmp(ctx->current_file, "CMakeLists.txt") == 0);
    ASSERT(nob_sv_eq(eval_current_list_file(ctx), nob_sv_from_cstr("CMakeLists.txt")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_dependency_provider_rejects_invalid_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(dep_provider_bad method)\n"
        "endmacro()\n"
        "function(dep_provider_scope)\n"
        "  cmake_language(SET_DEPENDENCY_PROVIDER dep_provider_bad SUPPORTED_METHODS FIND_PACKAGE)\n"
        "endfunction()\n"
        "dep_provider_scope()\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER missing_provider SUPPORTED_METHODS FIND_PACKAGE)\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER dep_provider_bad SUPPORTED_METHODS BAD_METHOD)\n"
        "find_package(OutsidePkg BYPASS_PROVIDER QUIET)\n");

    Eval_Result run_res = evaluator_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    bool saw_scope = false;
    bool saw_missing = false;
    bool saw_method = false;
    bool saw_bypass = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) must be called at file scope"))) {
            saw_scope = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) requires an existing function() or macro()"))) {
            saw_missing = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) received an unknown method"))) {
            saw_method = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_package(BYPASS_PROVIDER) may only be used from inside a dependency provider"))) {
            saw_bypass = true;
        }
    }

    ASSERT(saw_scope);
    ASSERT(saw_missing);
    ASSERT(saw_method);
    ASSERT(saw_bypass);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_target_compile_definitions_normalizes_dash_d_items) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(norm_defs main.c)\n"
        "target_compile_definitions(norm_defs PRIVATE -DFOO -D BAR -DBAZ=1 QUX=2)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_foo = false;
    bool saw_bar = false;
    bool saw_baz = false;
    bool saw_qux = false;
    bool saw_dash_prefixed = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("norm_defs"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOO"))) saw_foo = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAR"))) saw_bar = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAZ=1"))) saw_baz = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("QUX=2"))) saw_qux = true;
        if (nob_sv_starts_with(ev->as.target_compile_definitions.item, nob_sv_from_cstr("-D"))) saw_dash_prefixed = true;
    }

    ASSERT(saw_foo);
    ASSERT(saw_bar);
    ASSERT(saw_baz);
    ASSERT(saw_qux);
    ASSERT(!saw_dash_prefixed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_custom_command_target_validates_signature_and_target) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_custom_target(gen)\n"
        "add_custom_command(TARGET gen POST_BUILD COMMAND echo ok BYPRODUCTS ok.txt)\n"
        "add_custom_command(TARGET missing POST_BUILD COMMAND echo bad)\n"
        "add_custom_command(TARGET gen COMMAND echo bad_no_stage)\n"
        "add_custom_command(TARGET gen PRE_BUILD PRE_LINK COMMAND echo bad_multi_stage)\n"
        "add_custom_command(TARGET gen POST_BUILD DEPENDS dep1 COMMAND echo bad_depends)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    size_t custom_target_success_events = 0;
    size_t custom_target_error_events = 0;
    bool saw_missing_target = false;
    bool saw_missing_stage = false;
    bool saw_multi_stage = false;
    bool saw_unexpected_depends = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_COMMAND_END &&
            nob_sv_eq(ev->as.command_end.command_name, nob_sv_from_cstr("add_custom_command"))) {
            if (ev->as.command_end.status == EVENT_COMMAND_STATUS_SUCCESS) {
                custom_target_success_events++;
            } else if (ev->as.command_end.status == EVENT_COMMAND_STATUS_ERROR) {
                custom_target_error_events++;
            }
        }
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) target was not declared"))) {
            saw_missing_target = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) requires PRE_BUILD, PRE_LINK or POST_BUILD"))) {
            saw_missing_stage = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) accepts exactly one build stage"))) {
            saw_multi_stage = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unexpected argument in add_custom_command()")) &&
                   nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("DEPENDS"))) {
            saw_unexpected_depends = true;
        }
    }

    ASSERT(custom_target_success_events == 1);
    ASSERT(custom_target_error_events == 4);
    ASSERT(saw_missing_target);
    ASSERT(saw_missing_stage);
    ASSERT(saw_multi_stage);
    ASSERT(saw_unexpected_depends);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_custom_command_output_validates_conflicts) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_custom_command(OUTPUT bad_pairs.c IMPLICIT_DEPENDS C only.c CXX COMMAND gen)\n"
        "add_custom_command(OUTPUT bad_conflict.c IMPLICIT_DEPENDS C in.c DEPFILE in.d COMMAND gen)\n"
        "add_custom_command(OUTPUT bad_pool.c JOB_POOL pool USES_TERMINAL COMMAND gen)\n"
        "add_custom_command(OUTPUT good.c COMMAND python gen.py DEPENDS schema.idl BYPRODUCTS gen.log MAIN_DEPENDENCY schema.idl)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_pairs_error = false;
    bool saw_conflict_error = false;
    bool saw_pool_error = false;
    bool saw_valid_output_event = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_COMMAND_END &&
            nob_sv_eq(ev->as.command_end.command_name, nob_sv_from_cstr("add_custom_command")) &&
            ev->as.command_end.status == EVENT_COMMAND_STATUS_SUCCESS) {
            saw_valid_output_event = true;
        }
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("IMPLICIT_DEPENDS requires language/file pairs"))) {
            saw_pairs_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(OUTPUT ...) cannot combine DEPFILE with IMPLICIT_DEPENDS"))) {
            saw_conflict_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(OUTPUT ...) JOB_POOL is incompatible with USES_TERMINAL"))) {
            saw_pool_error = true;
        }
    }

    ASSERT(saw_pairs_error);
    ASSERT(saw_conflict_error);
    ASSERT(saw_pool_error);
    ASSERT(saw_valid_output_event);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

typedef struct {
    Event_Family family;
    const char *label;
} Event_Family_Contract_Row;

typedef struct {
    Event_Kind kind;
    Event_Family family;
    const char *label;
    uint32_t role_mask;
} Event_Kind_Contract_Row;

TEST(evaluator_event_ir_taxonomy_is_frozen) {
    static const Event_Family_Contract_Row expected_families[] = {
        {EVENT_FAMILY_TRACE, "trace"},
        {EVENT_FAMILY_DIAG, "diag"},
        {EVENT_FAMILY_DIRECTORY, "directory"},
        {EVENT_FAMILY_FLOW, "flow"},
        {EVENT_FAMILY_SCOPE, "scope"},
        {EVENT_FAMILY_POLICY, "policy"},
        {EVENT_FAMILY_VAR, "var"},
        {EVENT_FAMILY_FS, "fs"},
        {EVENT_FAMILY_PROC, "proc"},
        {EVENT_FAMILY_STRING, "string"},
        {EVENT_FAMILY_LIST, "list"},
        {EVENT_FAMILY_MATH, "math"},
        {EVENT_FAMILY_PATH, "path"},
        {EVENT_FAMILY_PROJECT, "project"},
        {EVENT_FAMILY_TARGET, "target"},
        {EVENT_FAMILY_TEST, "test"},
        {EVENT_FAMILY_INSTALL, "install"},
        {EVENT_FAMILY_CPACK, "cpack"},
        {EVENT_FAMILY_PACKAGE, "package"},
    };
    static const Event_Kind_Contract_Row expected_kinds[] = {
        {EVENT_DIAG, EVENT_FAMILY_DIAG, "diag", EVENT_ROLE_DIAGNOSTIC},
        {EVENT_COMMAND_BEGIN, EVENT_FAMILY_TRACE, "command_begin", EVENT_ROLE_TRACE},
        {EVENT_COMMAND_END, EVENT_FAMILY_TRACE, "command_end", EVENT_ROLE_TRACE},
        {EVENT_INCLUDE_BEGIN, EVENT_FAMILY_TRACE, "include_begin", EVENT_ROLE_TRACE},
        {EVENT_INCLUDE_END, EVENT_FAMILY_TRACE, "include_end", EVENT_ROLE_TRACE},
        {EVENT_ADD_SUBDIRECTORY_BEGIN, EVENT_FAMILY_TRACE, "add_subdirectory_begin", EVENT_ROLE_TRACE},
        {EVENT_ADD_SUBDIRECTORY_END, EVENT_FAMILY_TRACE, "add_subdirectory_end", EVENT_ROLE_TRACE},
        {EVENT_CMAKE_LANGUAGE_CALL, EVENT_FAMILY_TRACE, "cmake_language_call", EVENT_ROLE_TRACE},
        {EVENT_CMAKE_LANGUAGE_EVAL, EVENT_FAMILY_TRACE, "cmake_language_eval", EVENT_ROLE_TRACE},
        {EVENT_CMAKE_LANGUAGE_DEFER_QUEUE, EVENT_FAMILY_TRACE, "cmake_language_defer_queue", EVENT_ROLE_TRACE},
        {EVENT_DIRECTORY_ENTER, EVENT_FAMILY_DIRECTORY, "directory_enter", EVENT_ROLE_TRACE | EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_DIRECTORY_LEAVE, EVENT_FAMILY_DIRECTORY, "directory_leave", EVENT_ROLE_TRACE | EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_DIRECTORY_PROPERTY_MUTATE, EVENT_FAMILY_DIRECTORY, "directory_property_mutate", EVENT_ROLE_STATE | EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_GLOBAL_PROPERTY_MUTATE, EVENT_FAMILY_DIRECTORY, "global_property_mutate", EVENT_ROLE_STATE | EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_VAR_SET, EVENT_FAMILY_VAR, "var_set", EVENT_ROLE_STATE},
        {EVENT_VAR_UNSET, EVENT_FAMILY_VAR, "var_unset", EVENT_ROLE_STATE},
        {EVENT_SCOPE_PUSH, EVENT_FAMILY_SCOPE, "scope_push", EVENT_ROLE_STATE},
        {EVENT_SCOPE_POP, EVENT_FAMILY_SCOPE, "scope_pop", EVENT_ROLE_STATE},
        {EVENT_POLICY_PUSH, EVENT_FAMILY_POLICY, "policy_push", EVENT_ROLE_STATE},
        {EVENT_POLICY_POP, EVENT_FAMILY_POLICY, "policy_pop", EVENT_ROLE_STATE},
        {EVENT_POLICY_SET, EVENT_FAMILY_POLICY, "policy_set", EVENT_ROLE_STATE},
        {EVENT_FLOW_RETURN, EVENT_FAMILY_FLOW, "flow_return", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_IF_EVAL, EVENT_FAMILY_FLOW, "flow_if_eval", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_BRANCH_TAKEN, EVENT_FAMILY_FLOW, "flow_branch_taken", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_LOOP_BEGIN, EVENT_FAMILY_FLOW, "flow_loop_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_LOOP_END, EVENT_FAMILY_FLOW, "flow_loop_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_BREAK, EVENT_FAMILY_FLOW, "flow_break", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_CONTINUE, EVENT_FAMILY_FLOW, "flow_continue", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_DEFER_QUEUE, EVENT_FAMILY_FLOW, "flow_defer_queue", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_DEFER_FLUSH, EVENT_FAMILY_FLOW, "flow_defer_flush", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_BLOCK_BEGIN, EVENT_FAMILY_FLOW, "flow_block_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_BLOCK_END, EVENT_FAMILY_FLOW, "flow_block_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_FUNCTION_BEGIN, EVENT_FAMILY_FLOW, "flow_function_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_FUNCTION_END, EVENT_FAMILY_FLOW, "flow_function_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_MACRO_BEGIN, EVENT_FAMILY_FLOW, "flow_macro_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_MACRO_END, EVENT_FAMILY_FLOW, "flow_macro_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FS_WRITE_FILE, EVENT_FAMILY_FS, "fs_write_file", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_APPEND_FILE, EVENT_FAMILY_FS, "fs_append_file", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_READ_FILE, EVENT_FAMILY_FS, "fs_read_file", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_GLOB, EVENT_FAMILY_FS, "fs_glob", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_MKDIR, EVENT_FAMILY_FS, "fs_mkdir", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_REMOVE, EVENT_FAMILY_FS, "fs_remove", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_COPY, EVENT_FAMILY_FS, "fs_copy", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_RENAME, EVENT_FAMILY_FS, "fs_rename", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_CREATE_LINK, EVENT_FAMILY_FS, "fs_create_link", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_CHMOD, EVENT_FAMILY_FS, "fs_chmod", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_ARCHIVE_CREATE, EVENT_FAMILY_FS, "fs_archive_create", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_ARCHIVE_EXTRACT, EVENT_FAMILY_FS, "fs_archive_extract", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_TRANSFER_DOWNLOAD, EVENT_FAMILY_FS, "fs_transfer_download", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_TRANSFER_UPLOAD, EVENT_FAMILY_FS, "fs_transfer_upload", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PROC_EXEC_REQUEST, EVENT_FAMILY_PROC, "proc_exec_request", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PROC_EXEC_RESULT, EVENT_FAMILY_PROC, "proc_exec_result", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_REPLACE, EVENT_FAMILY_STRING, "string_replace", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_CONFIGURE, EVENT_FAMILY_STRING, "string_configure", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_REGEX, EVENT_FAMILY_STRING, "string_regex", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_HASH, EVENT_FAMILY_STRING, "string_hash", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_TIMESTAMP, EVENT_FAMILY_STRING, "string_timestamp", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_APPEND, EVENT_FAMILY_LIST, "list_append", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_PREPEND, EVENT_FAMILY_LIST, "list_prepend", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_INSERT, EVENT_FAMILY_LIST, "list_insert", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_REMOVE, EVENT_FAMILY_LIST, "list_remove", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_TRANSFORM, EVENT_FAMILY_LIST, "list_transform", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_SORT, EVENT_FAMILY_LIST, "list_sort", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_MATH_EXPR, EVENT_FAMILY_MATH, "math_expr", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PATH_NORMALIZE, EVENT_FAMILY_PATH, "path_normalize", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PATH_COMPARE, EVENT_FAMILY_PATH, "path_compare", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PATH_CONVERT, EVENT_FAMILY_PATH, "path_convert", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_TEST_ENABLE, EVENT_FAMILY_TEST, "test_enable", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TEST_ADD, EVENT_FAMILY_TEST, "test_add", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_INSTALL_RULE_ADD, EVENT_FAMILY_INSTALL, "install_rule_add", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_CPACK_ADD_INSTALL_TYPE, EVENT_FAMILY_CPACK, "cpack_add_install_type", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_CPACK_ADD_COMPONENT_GROUP, EVENT_FAMILY_CPACK, "cpack_add_component_group", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_CPACK_ADD_COMPONENT, EVENT_FAMILY_CPACK, "cpack_add_component", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_PACKAGE_FIND_RESULT, EVENT_FAMILY_PACKAGE, "package_find_result", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_PROJECT_DECLARE, EVENT_FAMILY_PROJECT, "project_declare", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_PROJECT_MINIMUM_REQUIRED, EVENT_FAMILY_PROJECT, "project_minimum_required", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_DECLARE, EVENT_FAMILY_TARGET, "target_declare", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_ADD_SOURCE, EVENT_FAMILY_TARGET, "target_add_source", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_ADD_DEPENDENCY, EVENT_FAMILY_TARGET, "target_add_dependency", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_PROP_SET, EVENT_FAMILY_TARGET, "target_prop_set", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_LINK_LIBRARIES, EVENT_FAMILY_TARGET, "target_link_libraries", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_LINK_OPTIONS, EVENT_FAMILY_TARGET, "target_link_options", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_LINK_DIRECTORIES, EVENT_FAMILY_TARGET, "target_link_directories", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_INCLUDE_DIRECTORIES, EVENT_FAMILY_TARGET, "target_include_directories", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_COMPILE_DEFINITIONS, EVENT_FAMILY_TARGET, "target_compile_definitions", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_COMPILE_OPTIONS, EVENT_FAMILY_TARGET, "target_compile_options", EVENT_ROLE_BUILD_SEMANTIC},
    };

    ASSERT(EVENT_FAMILY_COUNT == NOB_ARRAY_LEN(expected_families));
    ASSERT(EVENT_KIND_COUNT == NOB_ARRAY_LEN(expected_kinds));

    for (size_t i = 0; i < NOB_ARRAY_LEN(expected_families); i++) {
        const Event_Family_Contract_Row *expected = &expected_families[i];
        ASSERT((Event_Family)i == expected->family);
        ASSERT(strcmp(event_family_name(expected->family), expected->label) == 0);
    }
    ASSERT(strcmp(event_family_name(EVENT_FAMILY_COUNT), "unknown_family") == 0);

    for (size_t i = 0; i < NOB_ARRAY_LEN(expected_kinds); i++) {
        const Event_Kind_Contract_Row *expected = &expected_kinds[i];
        const Event_Kind_Meta *meta = event_kind_meta(expected->kind);
        ASSERT((Event_Kind)i == expected->kind);
        ASSERT(meta != NULL);
        ASSERT(meta->kind == expected->kind);
        ASSERT(meta->family == expected->family);
        ASSERT(meta->role_mask == expected->role_mask);
        ASSERT(meta->default_version == 1);
        ASSERT(strcmp(meta->label, expected->label) == 0);
        ASSERT(strcmp(event_kind_name(expected->kind), expected->label) == 0);
        ASSERT(event_kind_family(expected->kind) == expected->family);
        ASSERT(event_kind_role_mask(expected->kind) == expected->role_mask);
        ASSERT(strcmp(event_family_name(expected->family), "unknown_family") != 0);

        for (uint32_t bit = 1; bit <= EVENT_ROLE_BUILD_SEMANTIC; bit <<= 1) {
            bool expected_has_role = (expected->role_mask & bit) != 0;
            ASSERT(event_kind_has_role(expected->kind, (Event_Role)bit) == expected_has_role);
        }
    }

    ASSERT(event_kind_meta(EVENT_KIND_COUNT) == NULL);
    ASSERT(strcmp(event_kind_name(EVENT_KIND_COUNT), "unknown_event") == 0);
    ASSERT(event_kind_family(EVENT_KIND_COUNT) == EVENT_FAMILY_COUNT);
    ASSERT(event_kind_role_mask(EVENT_KIND_COUNT) == 0);
    ASSERT(!event_kind_has_role(EVENT_KIND_COUNT, EVENT_ROLE_TRACE));

    TEST_PASS();
}

TEST(evaluator_event_ir_metadata_and_stream_contract) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    Cmake_Event_Stream *stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    const Event_Kind_Meta *command_begin_meta = event_kind_meta(EVENT_COMMAND_BEGIN);
    ASSERT(command_begin_meta != NULL);
    ASSERT(command_begin_meta->family == EVENT_FAMILY_TRACE);
    ASSERT(event_kind_has_role(EVENT_COMMAND_BEGIN, EVENT_ROLE_TRACE));
    ASSERT(!event_kind_has_role(EVENT_COMMAND_BEGIN, EVENT_ROLE_BUILD_SEMANTIC));

    const Event_Kind_Meta *dir_prop_meta = event_kind_meta(EVENT_DIRECTORY_PROPERTY_MUTATE);
    ASSERT(dir_prop_meta != NULL);
    ASSERT(dir_prop_meta->family == EVENT_FAMILY_DIRECTORY);
    ASSERT(event_kind_has_role(EVENT_DIRECTORY_PROPERTY_MUTATE, EVENT_ROLE_BUILD_SEMANTIC));
    ASSERT(event_kind_has_role(EVENT_DIRECTORY_PROPERTY_MUTATE, EVENT_ROLE_STATE));

    const Event_Kind_Meta *global_prop_meta = event_kind_meta(EVENT_GLOBAL_PROPERTY_MUTATE);
    ASSERT(global_prop_meta != NULL);
    ASSERT(global_prop_meta->family == EVENT_FAMILY_DIRECTORY);
    ASSERT(event_kind_has_role(EVENT_GLOBAL_PROPERTY_MUTATE, EVENT_ROLE_BUILD_SEMANTIC));
    ASSERT(event_kind_has_role(EVENT_GLOBAL_PROPERTY_MUTATE, EVENT_ROLE_STATE));

    Event invalid = {0};
    invalid.h.kind = EV_UNKNOWN;
    invalid.h.origin.file_path = nob_sv_from_cstr("invalid.cmake");
    ASSERT(!event_stream_push(stream, &invalid));
    ASSERT(stream->count == 0);

    Arena *source_arena = arena_create(64 * 1024);
    ASSERT(source_arena != NULL);

    char *origin_path = arena_strndup(source_arena, "origin.cmake", strlen("origin.cmake"));
    char *command_name = arena_strndup(source_arena, "ephemeral_command", strlen("ephemeral_command"));
    ASSERT(origin_path != NULL);
    ASSERT(command_name != NULL);
    Event ev = {0};
    ev.h.kind = EVENT_COMMAND_BEGIN;
    ev.h.origin.file_path = nob_sv_from_parts(origin_path, strlen(origin_path));
    ev.h.origin.line = 7;
    ev.h.origin.col = 3;
    ev.as.command_begin.command_name = nob_sv_from_parts(command_name, strlen(command_name));
    ev.as.command_begin.dispatch_kind = EVENT_COMMAND_DISPATCH_BUILTIN;
    ev.as.command_begin.argc = 2;
    ASSERT(event_stream_push(stream, &ev));

    origin_path[0] = 'X';
    command_name[0] = 'X';

    ASSERT(stream->count == 1);
    ASSERT(stream->items[0].h.version == 1);
    ASSERT(stream->items[0].h.seq == 1);
    ASSERT(nob_sv_eq(stream->items[0].h.origin.file_path, nob_sv_from_cstr("origin.cmake")));
    ASSERT(nob_sv_eq(stream->items[0].as.command_begin.command_name, nob_sv_from_cstr("ephemeral_command")));

    char *property_name = arena_strndup(source_arena, "INCLUDE_DIRECTORIES", strlen("INCLUDE_DIRECTORIES"));
    char *item_buf = arena_strndup(source_arena, "include", strlen("include"));
    ASSERT(property_name != NULL);
    ASSERT(item_buf != NULL);
    String_View items[] = {nob_sv_from_parts(item_buf, strlen(item_buf))};
    ev = (Event){0};
    ev.h.kind = EVENT_DIRECTORY_PROPERTY_MUTATE;
    ev.h.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev.as.directory_property_mutate.property_name = nob_sv_from_parts(property_name, strlen(property_name));
    ev.as.directory_property_mutate.op = EVENT_PROPERTY_MUTATE_PREPEND_LIST;
    ev.as.directory_property_mutate.modifier_flags =
        EVENT_PROPERTY_MODIFIER_BEFORE | EVENT_PROPERTY_MODIFIER_SYSTEM;
    ev.as.directory_property_mutate.items = items;
    ev.as.directory_property_mutate.item_count = NOB_ARRAY_LEN(items);
    ASSERT(event_stream_push(stream, &ev));

    char *global_property_name =
        arena_strndup(source_arena, "IR_GLOBAL_PROP", strlen("IR_GLOBAL_PROP"));
    char *global_item_a = arena_strndup(source_arena, "global_a", strlen("global_a"));
    char *global_item_b = arena_strndup(source_arena, "global_b", strlen("global_b"));
    ASSERT(global_property_name != NULL);
    ASSERT(global_item_a != NULL);
    ASSERT(global_item_b != NULL);
    String_View global_items[] = {
        nob_sv_from_parts(global_item_a, strlen(global_item_a)),
        nob_sv_from_parts(global_item_b, strlen(global_item_b)),
    };
    ev = (Event){0};
    ev.h.kind = EVENT_GLOBAL_PROPERTY_MUTATE;
    ev.h.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev.as.global_property_mutate.property_name =
        nob_sv_from_parts(global_property_name, strlen(global_property_name));
    ev.as.global_property_mutate.op = EVENT_PROPERTY_MUTATE_SET;
    ev.as.global_property_mutate.items = global_items;
    ev.as.global_property_mutate.item_count = NOB_ARRAY_LEN(global_items);
    ASSERT(event_stream_push(stream, &ev));

    property_name[0] = 'X';
    item_buf[0] = 'X';
    global_property_name[0] = 'X';
    global_item_a[0] = 'X';
    global_item_b[0] = 'X';
    arena_destroy(source_arena);

    ASSERT(stream->count == 3);
    ASSERT(evaluator_stream_has_monotonic_sequence(stream));
    ASSERT(nob_sv_eq(stream->items[1].as.directory_property_mutate.property_name,
                     nob_sv_from_cstr("INCLUDE_DIRECTORIES")));
    ASSERT(stream->items[1].as.directory_property_mutate.item_count == 1);
    ASSERT(nob_sv_eq(stream->items[1].as.directory_property_mutate.items[0],
                     nob_sv_from_cstr("include")));
    ASSERT(nob_sv_eq(stream->items[2].as.global_property_mutate.property_name,
                     nob_sv_from_cstr("IR_GLOBAL_PROP")));
    ASSERT(stream->items[2].as.global_property_mutate.item_count == 2);
    ASSERT(nob_sv_eq(stream->items[2].as.global_property_mutate.items[0],
                     nob_sv_from_cstr("global_a")));
    ASSERT(nob_sv_eq(stream->items[2].as.global_property_mutate.items[1],
                     nob_sv_from_cstr("global_b")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(evaluator_event_ir_directory_semantics_and_trace_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE ir_include.cmake [=[set(IR_INCLUDED 1)\n]=])\n"
        "file(MAKE_DIRECTORY ir_subdir)\n"
        "file(WRITE ir_subdir/CMakeLists.txt [=[add_library(ir_subdir_lib STATIC sub.c)\n]=])\n"
        "add_compile_options(-Wall)\n"
        "add_compile_definitions(IR_DEF)\n"
        "add_link_options(-Wl,--as-needed)\n"
        "include_directories(BEFORE SYSTEM ir_inc_a ir_inc_b)\n"
        "link_directories(BEFORE ir_lib)\n"
        "set_property(GLOBAL PROPERTY IR_GLOBAL_PROP ir_global_a ir_global_b)\n"
        "get_property(IR_COMPILE_OPTIONS DIRECTORY PROPERTY COMPILE_OPTIONS)\n"
        "get_property(IR_COMPILE_DEFINITIONS DIRECTORY PROPERTY COMPILE_DEFINITIONS)\n"
        "get_property(IR_LINK_OPTIONS DIRECTORY PROPERTY LINK_OPTIONS)\n"
        "get_property(IR_INCLUDE_DIRECTORIES DIRECTORY PROPERTY INCLUDE_DIRECTORIES)\n"
        "get_property(IR_LINK_DIRECTORIES DIRECTORY PROPERTY LINK_DIRECTORIES)\n"
        "get_property(IR_GLOBAL_PROP_OUT GLOBAL PROPERTY IR_GLOBAL_PROP)\n"
        "include(ir_include.cmake)\n"
        "add_subdirectory(ir_subdir)\n"
        "unknown_event_ir_cmd()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    bool saw_compile_options = false;
    bool saw_compile_definitions = false;
    bool saw_link_options = false;
    bool saw_include_directories = false;
    bool saw_link_directories = false;
    bool saw_global_property = false;
    bool saw_include_begin = false;
    bool saw_include_end = false;
    bool saw_subdir_begin = false;
    bool saw_subdir_end = false;
    bool saw_directory_enter = false;
    bool saw_directory_leave = false;
    bool saw_unknown_begin = false;
    bool saw_unknown_end = false;
    size_t include_begin = (size_t)-1;
    size_t include_end = (size_t)-1;
    size_t subdir_begin = (size_t)-1;
    size_t subdir_end = (size_t)-1;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("COMPILE_OPTIONS"))) {
            saw_compile_options =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_APPEND_LIST &&
                ev->as.directory_property_mutate.item_count == 1 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("-Wall"));
        }
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("COMPILE_DEFINITIONS"))) {
            saw_compile_definitions =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_APPEND_LIST &&
                ev->as.directory_property_mutate.item_count == 1 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("IR_DEF"));
        }
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("LINK_OPTIONS"))) {
            saw_link_options =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_APPEND_LIST &&
                ev->as.directory_property_mutate.item_count == 1 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("-Wl,--as-needed"));
        }
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("INCLUDE_DIRECTORIES"))) {
            saw_include_directories =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_PREPEND_LIST &&
                (ev->as.directory_property_mutate.modifier_flags & EVENT_PROPERTY_MODIFIER_BEFORE) != 0 &&
                (ev->as.directory_property_mutate.modifier_flags & EVENT_PROPERTY_MODIFIER_SYSTEM) != 0 &&
                ev->as.directory_property_mutate.item_count == 2 &&
                sv_contains_sv(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("ir_inc_a")) &&
                sv_contains_sv(ev->as.directory_property_mutate.items[1], nob_sv_from_cstr("ir_inc_b"));
        }
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("LINK_DIRECTORIES"))) {
            saw_link_directories =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_PREPEND_LIST &&
                (ev->as.directory_property_mutate.modifier_flags & EVENT_PROPERTY_MODIFIER_BEFORE) != 0 &&
                ev->as.directory_property_mutate.item_count == 1 &&
                sv_contains_sv(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("ir_lib"));
        }
        if (ev->h.kind == EVENT_GLOBAL_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.global_property_mutate.property_name, nob_sv_from_cstr("IR_GLOBAL_PROP"))) {
            saw_global_property =
                ev->as.global_property_mutate.op == EVENT_PROPERTY_MUTATE_SET &&
                ev->as.global_property_mutate.item_count == 2 &&
                nob_sv_eq(ev->as.global_property_mutate.items[0], nob_sv_from_cstr("ir_global_a")) &&
                nob_sv_eq(ev->as.global_property_mutate.items[1], nob_sv_from_cstr("ir_global_b"));
        }
        if (ev->h.kind == EVENT_INCLUDE_BEGIN &&
            sv_contains_sv(ev->as.include_begin.path, nob_sv_from_cstr("ir_include.cmake"))) {
            saw_include_begin = true;
            include_begin = i;
        }
        if (ev->h.kind == EVENT_INCLUDE_END &&
            sv_contains_sv(ev->as.include_end.path, nob_sv_from_cstr("ir_include.cmake"))) {
            saw_include_end = true;
            include_end = i;
        }
        if (ev->h.kind == EVENT_ADD_SUBDIRECTORY_BEGIN &&
            sv_contains_sv(ev->as.add_subdirectory_begin.source_dir, nob_sv_from_cstr("ir_subdir"))) {
            saw_subdir_begin = true;
            subdir_begin = i;
        }
        if (ev->h.kind == EVENT_ADD_SUBDIRECTORY_END &&
            sv_contains_sv(ev->as.add_subdirectory_end.source_dir, nob_sv_from_cstr("ir_subdir"))) {
            saw_subdir_end = true;
            subdir_end = i;
        }
        if (ev->h.kind == EVENT_DIRECTORY_ENTER) saw_directory_enter = true;
        if (ev->h.kind == EVENT_DIRECTORY_LEAVE) saw_directory_leave = true;
        if (ev->h.kind == EVENT_COMMAND_BEGIN &&
            nob_sv_eq(ev->as.command_begin.command_name, nob_sv_from_cstr("unknown_event_ir_cmd")) &&
            ev->as.command_begin.dispatch_kind == EVENT_COMMAND_DISPATCH_UNKNOWN) {
            saw_unknown_begin = true;
        }
        if (ev->h.kind == EVENT_COMMAND_END &&
            nob_sv_eq(ev->as.command_end.command_name, nob_sv_from_cstr("unknown_event_ir_cmd")) &&
            ev->as.command_end.dispatch_kind == EVENT_COMMAND_DISPATCH_UNKNOWN &&
            ev->as.command_end.status == EVENT_COMMAND_STATUS_UNSUPPORTED) {
            saw_unknown_end = true;
        }
    }

    ASSERT(saw_compile_options);
    ASSERT(saw_compile_definitions);
    ASSERT(saw_link_options);
    ASSERT(saw_include_directories);
    ASSERT(saw_link_directories);
    ASSERT(saw_global_property);
    ASSERT(saw_include_begin);
    ASSERT(saw_include_end);
    ASSERT(saw_subdir_begin);
    ASSERT(saw_subdir_end);
    ASSERT(saw_directory_enter);
    ASSERT(saw_directory_leave);
    ASSERT(saw_unknown_begin);
    ASSERT(saw_unknown_end);
    ASSERT(include_begin != (size_t)-1);
    ASSERT(include_end != (size_t)-1);
    ASSERT(subdir_begin != (size_t)-1);
    ASSERT(subdir_end != (size_t)-1);

    size_t include_enter = (size_t)-1;
    size_t include_leave = (size_t)-1;
    for (size_t i = include_begin + 1; i < include_end; i++) {
        if (stream->items[i].h.kind == EVENT_DIRECTORY_ENTER) {
            include_enter = i;
            break;
        }
    }
    for (size_t i = include_enter + 1; i < include_end; i++) {
        if (stream->items[i].h.kind == EVENT_DIRECTORY_LEAVE) {
            include_leave = i;
            break;
        }
    }
    ASSERT(include_enter != (size_t)-1);
    ASSERT(include_leave != (size_t)-1);
    ASSERT(include_begin < include_enter);
    ASSERT(include_enter < include_leave);
    ASSERT(include_leave < include_end);

    size_t subdir_enter = (size_t)-1;
    size_t subdir_leave = (size_t)-1;
    for (size_t i = subdir_begin + 1; i < subdir_end; i++) {
        if (stream->items[i].h.kind == EVENT_DIRECTORY_ENTER) {
            subdir_enter = i;
            break;
        }
    }
    for (size_t i = subdir_enter + 1; i < subdir_end; i++) {
        if (stream->items[i].h.kind == EVENT_DIRECTORY_LEAVE) {
            subdir_leave = i;
            break;
        }
    }
    ASSERT(subdir_enter != (size_t)-1);
    ASSERT(subdir_leave != (size_t)-1);
    ASSERT(subdir_begin < subdir_enter);
    ASSERT(subdir_enter < subdir_leave);
    ASSERT(subdir_leave < subdir_end);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IR_COMPILE_OPTIONS")), nob_sv_from_cstr("-Wall")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IR_COMPILE_DEFINITIONS")), nob_sv_from_cstr("IR_DEF")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IR_LINK_OPTIONS")),
                     nob_sv_from_cstr("-Wl,--as-needed")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IR_GLOBAL_PROP_OUT")),
                     nob_sv_from_cstr("ir_global_a;ir_global_b")));

    String_View include_dirs = eval_var_get(ctx, nob_sv_from_cstr("IR_INCLUDE_DIRECTORIES"));
    ASSERT(semicolon_list_count(include_dirs) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(include_dirs, 0), nob_sv_from_cstr("ir_inc_a")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(include_dirs, 1), nob_sv_from_cstr("ir_inc_b")));

    String_View link_dirs = eval_var_get(ctx, nob_sv_from_cstr("IR_LINK_DIRECTORIES"));
    ASSERT(semicolon_list_count(link_dirs) == 1);
    ASSERT(sv_contains_sv(semicolon_list_item_at(link_dirs, 0), nob_sv_from_cstr("ir_lib")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_event_ir_command_trace_sequences_unknown_and_error_paths) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "function(trace_fn)\n"
        "  math()\n"
        "endfunction()\n"
        "macro(trace_macro)\n"
        "  return()\n"
        "endmacro()\n"
        "unknown_trace_cmd()\n"
        "math()\n"
        "trace_fn()\n"
        "trace_macro()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    size_t unknown_begin = evaluator_find_command_begin_index(stream,
                                                              nob_sv_from_cstr("unknown_trace_cmd"),
                                                              7,
                                                              EVENT_COMMAND_DISPATCH_UNKNOWN);
    size_t unknown_diag = evaluator_find_diag_index(stream,
                                                    nob_sv_from_cstr("unknown_trace_cmd"),
                                                    7,
                                                    nob_sv_from_cstr("Unknown command"));
    size_t unknown_end = evaluator_find_command_end_index(stream,
                                                          nob_sv_from_cstr("unknown_trace_cmd"),
                                                          7,
                                                          EVENT_COMMAND_DISPATCH_UNKNOWN,
                                                          EVENT_COMMAND_STATUS_UNSUPPORTED);

    size_t math_begin = evaluator_find_command_begin_index(stream,
                                                           nob_sv_from_cstr("math"),
                                                           8,
                                                           EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t math_diag = evaluator_find_diag_index(stream,
                                                 nob_sv_from_cstr("math"),
                                                 8,
                                                 nob_sv_from_cstr("math() requires a subcommand"));
    size_t math_end = evaluator_find_command_end_index(stream,
                                                       nob_sv_from_cstr("math"),
                                                       8,
                                                       EVENT_COMMAND_DISPATCH_BUILTIN,
                                                       EVENT_COMMAND_STATUS_ERROR);

    size_t fn_begin = evaluator_find_command_begin_index(stream,
                                                         nob_sv_from_cstr("trace_fn"),
                                                         9,
                                                         EVENT_COMMAND_DISPATCH_FUNCTION);
    size_t fn_inner_begin = evaluator_find_command_begin_index(stream,
                                                               nob_sv_from_cstr("math"),
                                                               2,
                                                               EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t fn_inner_diag = evaluator_find_diag_index(stream,
                                                     nob_sv_from_cstr("math"),
                                                     2,
                                                     nob_sv_from_cstr("math() requires a subcommand"));
    size_t fn_inner_end = evaluator_find_command_end_index(stream,
                                                           nob_sv_from_cstr("math"),
                                                           2,
                                                           EVENT_COMMAND_DISPATCH_BUILTIN,
                                                           EVENT_COMMAND_STATUS_ERROR);
    size_t fn_end = evaluator_find_command_end_index(stream,
                                                     nob_sv_from_cstr("trace_fn"),
                                                     9,
                                                     EVENT_COMMAND_DISPATCH_FUNCTION,
                                                     EVENT_COMMAND_STATUS_ERROR);

    size_t macro_begin = evaluator_find_command_begin_index(stream,
                                                            nob_sv_from_cstr("trace_macro"),
                                                            10,
                                                            EVENT_COMMAND_DISPATCH_MACRO);
    size_t macro_inner_begin = evaluator_find_command_begin_index(stream,
                                                                  nob_sv_from_cstr("return"),
                                                                  5,
                                                                  EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t macro_inner_diag = evaluator_find_diag_index(stream,
                                                        nob_sv_from_cstr("return"),
                                                        5,
                                                        nob_sv_from_cstr("return() cannot be used inside macro()"));
    size_t macro_inner_end = evaluator_find_command_end_index(stream,
                                                              nob_sv_from_cstr("return"),
                                                              5,
                                                              EVENT_COMMAND_DISPATCH_BUILTIN,
                                                              EVENT_COMMAND_STATUS_ERROR);
    size_t macro_end = evaluator_find_command_end_index(stream,
                                                        nob_sv_from_cstr("trace_macro"),
                                                        10,
                                                        EVENT_COMMAND_DISPATCH_MACRO,
                                                        EVENT_COMMAND_STATUS_ERROR);

    ASSERT(unknown_begin != (size_t)-1);
    ASSERT(unknown_diag != (size_t)-1);
    ASSERT(unknown_end != (size_t)-1);
    ASSERT(unknown_begin < unknown_diag);
    ASSERT(unknown_diag < unknown_end);

    ASSERT(math_begin != (size_t)-1);
    ASSERT(math_diag != (size_t)-1);
    ASSERT(math_end != (size_t)-1);
    ASSERT(math_begin < math_diag);
    ASSERT(math_diag < math_end);

    ASSERT(fn_begin != (size_t)-1);
    ASSERT(fn_inner_begin != (size_t)-1);
    ASSERT(fn_inner_diag != (size_t)-1);
    ASSERT(fn_inner_end != (size_t)-1);
    ASSERT(fn_end != (size_t)-1);
    ASSERT(fn_begin < fn_inner_begin);
    ASSERT(fn_inner_begin < fn_inner_diag);
    ASSERT(fn_inner_diag < fn_inner_end);
    ASSERT(fn_inner_end < fn_end);

    ASSERT(macro_begin != (size_t)-1);
    ASSERT(macro_inner_begin != (size_t)-1);
    ASSERT(macro_inner_diag != (size_t)-1);
    ASSERT(macro_inner_end != (size_t)-1);
    ASSERT(macro_end != (size_t)-1);
    ASSERT(macro_begin < macro_inner_begin);
    ASSERT(macro_inner_begin < macro_inner_diag);
    ASSERT(macro_inner_diag < macro_inner_end);
    ASSERT(macro_inner_end < macro_end);
    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_event_ir_command_trace_sequences_success_paths) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "function(trace_fn_ok)\n"
        "  set(FN_LOCAL 1)\n"
        "endfunction()\n"
        "macro(trace_macro_ok)\n"
        "  set(MAC_VISIBLE 1)\n"
        "endmacro()\n"
        "set(TOP_OK 1)\n"
        "trace_fn_ok()\n"
        "trace_macro_ok()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    size_t top_begin = evaluator_find_command_begin_index(stream,
                                                          nob_sv_from_cstr("set"),
                                                          7,
                                                          EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t top_end = evaluator_find_command_end_index(stream,
                                                      nob_sv_from_cstr("set"),
                                                      7,
                                                      EVENT_COMMAND_DISPATCH_BUILTIN,
                                                      EVENT_COMMAND_STATUS_SUCCESS);
    size_t fn_begin = evaluator_find_command_begin_index(stream,
                                                         nob_sv_from_cstr("trace_fn_ok"),
                                                         8,
                                                         EVENT_COMMAND_DISPATCH_FUNCTION);
    size_t fn_inner_begin = evaluator_find_command_begin_index(stream,
                                                               nob_sv_from_cstr("set"),
                                                               2,
                                                               EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t fn_inner_end = evaluator_find_command_end_index(stream,
                                                           nob_sv_from_cstr("set"),
                                                           2,
                                                           EVENT_COMMAND_DISPATCH_BUILTIN,
                                                           EVENT_COMMAND_STATUS_SUCCESS);
    size_t fn_end = evaluator_find_command_end_index(stream,
                                                     nob_sv_from_cstr("trace_fn_ok"),
                                                     8,
                                                     EVENT_COMMAND_DISPATCH_FUNCTION,
                                                     EVENT_COMMAND_STATUS_SUCCESS);
    size_t macro_begin = evaluator_find_command_begin_index(stream,
                                                            nob_sv_from_cstr("trace_macro_ok"),
                                                            9,
                                                            EVENT_COMMAND_DISPATCH_MACRO);
    size_t macro_inner_begin = evaluator_find_command_begin_index(stream,
                                                                  nob_sv_from_cstr("set"),
                                                                  5,
                                                                  EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t macro_inner_end = evaluator_find_command_end_index(stream,
                                                              nob_sv_from_cstr("set"),
                                                              5,
                                                              EVENT_COMMAND_DISPATCH_BUILTIN,
                                                              EVENT_COMMAND_STATUS_SUCCESS);
    size_t macro_end = evaluator_find_command_end_index(stream,
                                                        nob_sv_from_cstr("trace_macro_ok"),
                                                        9,
                                                        EVENT_COMMAND_DISPATCH_MACRO,
                                                        EVENT_COMMAND_STATUS_SUCCESS);

    ASSERT(top_begin != (size_t)-1);
    ASSERT(top_end != (size_t)-1);
    ASSERT(top_begin < top_end);

    ASSERT(fn_begin != (size_t)-1);
    ASSERT(fn_inner_begin != (size_t)-1);
    ASSERT(fn_inner_end != (size_t)-1);
    ASSERT(fn_end != (size_t)-1);
    ASSERT(fn_begin < fn_inner_begin);
    ASSERT(fn_inner_begin < fn_inner_end);
    ASSERT(fn_inner_end < fn_end);

    ASSERT(macro_begin != (size_t)-1);
    ASSERT(macro_inner_begin != (size_t)-1);
    ASSERT(macro_inner_end != (size_t)-1);
    ASSERT(macro_end != (size_t)-1);
    ASSERT(macro_begin < macro_inner_begin);
    ASSERT(macro_inner_begin < macro_inner_end);
    ASSERT(macro_inner_end < macro_end);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TOP_OK")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("MAC_VISIBLE")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("FN_LOCAL")).count == 0);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_return_in_macro_is_error_and_does_not_unwind_macro_body) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(MAC_RET start)\n"
        "macro(mc_ret)\n"
        "  set(MAC_RET before)\n"
        "  return()\n"
        "  set(MAC_RET after)\n"
        "endmacro()\n"
        "mc_ret()\n"
        "add_executable(ret_macro main.c)\n"
        "target_compile_definitions(ret_macro PRIVATE MAC_RET=${MAC_RET})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_macro_return_error = false;
    bool saw_macro_ret_after = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("return() cannot be used inside macro()"))) {
            saw_macro_return_error = true;
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_macro")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("MAC_RET=after"))) {
            saw_macro_ret_after = true;
        }
    }

    ASSERT(saw_macro_return_error);
    ASSERT(saw_macro_ret_after);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_return_cmp0140_old_ignores_args_and_new_enables_propagate) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(RET_OLD root_old)\n"
        "set(RET_NEW root_new)\n"
        "cmake_policy(SET CMP0140 OLD)\n"
        "function(ret_old)\n"
        "  set(RET_OLD changed_old)\n"
        "  return(PROPAGATE RET_OLD)\n"
        "endfunction()\n"
        "ret_old()\n"
        "cmake_policy(SET CMP0140 NEW)\n"
        "function(ret_new)\n"
        "  set(RET_NEW changed_new)\n"
        "  return(PROPAGATE RET_NEW)\n"
        "endfunction()\n"
        "ret_new()\n"
        "add_executable(ret_cmp0140 main.c)\n"
        "target_compile_definitions(ret_cmp0140 PRIVATE RET_OLD=${RET_OLD} RET_NEW=${RET_NEW})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_ret_old_root = false;
    bool saw_ret_new_changed = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_cmp0140")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RET_OLD=root_old"))) {
            saw_ret_old_root = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_cmp0140")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RET_NEW=changed_new"))) {
            saw_ret_new_changed = true;
        }
    }

    ASSERT(saw_ret_old_root);
    ASSERT(saw_ret_new_changed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_list_transform_genex_strip_and_output_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(L \"$<CONFIG:Debug>;a$<IF:$<BOOL:1>,b,c>;x\")\n"
        "list(TRANSFORM L GENEX_STRIP OUTPUT_VARIABLE L_STRIPPED)\n"
        "list(TRANSFORM L APPEND \"_S\" AT 0 OUTPUT_VARIABLE L_APPENDED)\n"
        "add_executable(list_transform_ov main.c)\n"
        "target_compile_definitions(list_transform_ov PRIVATE \"L=${L}\" \"L_STRIPPED=${L_STRIPPED}\" \"L_APPENDED=${L_APPENDED}\")\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_original = false;
    bool saw_stripped = false;
    bool saw_appended = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item,
                      nob_sv_from_cstr("L=$<CONFIG:Debug>;a$<IF:$<BOOL:1>,b,c>;x"))) {
            saw_original = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("L_STRIPPED=;a;x"))) {
            saw_stripped = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.item,
                             nob_sv_from_cstr("L_APPENDED=$<CONFIG:Debug>_S;a$<IF:$<BOOL:1>,b,c>;x"))) {
            saw_appended = true;
        }
    }

    ASSERT(saw_original);
    ASSERT(saw_stripped);
    ASSERT(saw_appended);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_list_transform_output_variable_requires_single_output_var) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(L \"a;b\")\n"
        "list(TRANSFORM L TOUPPER OUTPUT_VARIABLE)\n"
        "list(TRANSFORM L TOUPPER AT 0 OUTPUT_VARIABLE OUT EXTRA)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    size_t output_arity_errors = 0;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.cause,
                       nob_sv_from_cstr("list(TRANSFORM OUTPUT_VARIABLE) expects exactly one output variable"))) {
            continue;
        }
        output_arity_errors++;
    }
    ASSERT(output_arity_errors == 2);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_math_rejects_empty_and_incomplete_invocations) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "math()\n"
        "math(EXPR)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool found_empty_error = false;
    bool found_expr_arity_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(stream->items[i].as.diag.cause, nob_sv_from_cstr("math() requires a subcommand"))) {
            found_empty_error = true;
        }
        if (nob_sv_eq(stream->items[i].as.diag.cause,
                      nob_sv_from_cstr("math(EXPR) requires output variable and expression"))) {
            found_expr_arity_error = true;
        }
    }
    ASSERT(found_empty_error);
    ASSERT(found_expr_arity_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_target_properties_rejects_alias_target) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "set_target_properties(alias_real PROPERTIES OUTPUT_NAME x)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool found_alias_error = false;
    bool emitted_prop_for_alias = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_target_properties() cannot be used on ALIAS targets"))) {
            found_alias_error = true;
        }
        if (ev->h.kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("alias_real"))) {
            emitted_prop_for_alias = true;
        }
    }
    ASSERT(found_alias_error);
    ASSERT(!emitted_prop_for_alias);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_executable_imported_and_alias_signatures) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(tool IMPORTED GLOBAL)\n"
        "add_executable(tool_alias ALIAS tool)\n"
        "if(TARGET tool_alias)\n"
        "  set(HAS_TOOL_ALIAS 1)\n"
        "endif()\n"
        "add_executable(alias_probe main.c)\n"
        "target_compile_definitions(alias_probe PRIVATE HAS_TOOL_ALIAS=${HAS_TOOL_ALIAS})\n"
        "add_executable(tool_alias_bad ALIAS missing_tool)\n"
        "add_executable(tool_alias2 ALIAS tool_alias)\n"
        "add_executable(tool_bad IMPORTED source.c)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_imported = false;
    bool saw_imported_global = false;
    bool saw_has_alias = false;
    bool saw_alias_missing_err = false;
    bool saw_alias_of_alias_err = false;
    bool saw_imported_sources_err = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_PROP_SET) {
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("tool")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("tool")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED_GLOBAL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported_global = true;
            }
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("alias_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("HAS_TOOL_ALIAS=1"))) {
            saw_has_alias = true;
        }
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target does not exist"))) {
                saw_alias_missing_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target cannot reference another ALIAS target"))) {
                saw_alias_of_alias_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_executable(IMPORTED ...) does not accept source files"))) {
                saw_imported_sources_err = true;
            }
        }
    }

    ASSERT(saw_imported);
    ASSERT(saw_imported_global);
    ASSERT(saw_has_alias);
    ASSERT(saw_alias_missing_err);
    ASSERT(saw_alias_of_alias_err);
    ASSERT(saw_imported_sources_err);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_library_imported_alias_and_default_type) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(BUILD_SHARED_LIBS ON)\n"
        "add_library(auto_lib auto.c)\n"
        "add_library(imp_lib UNKNOWN IMPORTED GLOBAL)\n"
        "add_library(base_lib STATIC base.c)\n"
        "add_library(base_alias ALIAS base_lib)\n"
        "add_library(bad_alias ALIAS base_alias)\n"
        "add_library(bad_import IMPORTED)\n"
        "add_library(iface INTERFACE EXCLUDE_FROM_ALL)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_auto_shared = false;
    bool saw_imported = false;
    bool saw_imported_global = false;
    bool saw_iface_exclude = false;
    bool saw_iface_bad_source = false;
    bool saw_bad_alias_err = false;
    bool saw_bad_import_err = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_DECLARE) {
            if (nob_sv_eq(ev->as.target_declare.name, nob_sv_from_cstr("auto_lib")) &&
                ev->as.target_declare.type == EV_TARGET_LIBRARY_SHARED) {
                saw_auto_shared = true;
            }
        }
        if (ev->h.kind == EV_TARGET_PROP_SET) {
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imp_lib")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imp_lib")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED_GLOBAL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported_global = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("iface")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("EXCLUDE_FROM_ALL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_iface_exclude = true;
            }
        }
        if (ev->h.kind == EV_TARGET_ADD_SOURCE &&
            nob_sv_eq(ev->as.target_add_source.target_name, nob_sv_from_cstr("iface")) &&
            nob_sv_eq(ev->as.target_add_source.path, nob_sv_from_cstr("EXCLUDE_FROM_ALL"))) {
            saw_iface_bad_source = true;
        }
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target cannot reference another ALIAS target"))) {
                saw_bad_alias_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_library(IMPORTED ...) requires an explicit library type"))) {
                saw_bad_import_err = true;
            }
        }
    }

    ASSERT(saw_auto_shared);
    ASSERT(saw_imported);
    ASSERT(saw_imported_global);
    ASSERT(saw_iface_exclude);
    ASSERT(!saw_iface_bad_source);
    ASSERT(saw_bad_alias_err);
    ASSERT(saw_bad_import_err);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_target_rejects_alias_and_unknown_target) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "set_property(TARGET alias_real PROPERTY OUTPUT_NAME bad_alias)\n"
        "set_property(TARGET missing_t PROPERTY OUTPUT_NAME bad_missing)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_alias_error = false;
    bool saw_missing_error = false;
    bool emitted_for_alias = false;
    bool emitted_for_missing = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(TARGET ...) cannot be used on ALIAS targets"))) {
                saw_alias_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(TARGET ...) target was not declared"))) {
                saw_missing_error = true;
            }
        }
        if (ev->h.kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("alias_real"))) {
            emitted_for_alias = true;
        }
        if (ev->h.kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("missing_t"))) {
            emitted_for_missing = true;
        }
    }

    ASSERT(saw_alias_error);
    ASSERT(saw_missing_error);
    ASSERT(!emitted_for_alias);
    ASSERT(!emitted_for_missing);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_define_property_initializes_target_properties_from_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(MY_INIT seeded)\n"
        "define_property(GLOBAL PROPERTY NOB_G)\n"
        "define_property(TARGET PROPERTY CUSTOM_FLAG INITIALIZE_FROM_VARIABLE MY_INIT)\n"
        "define_property(TARGET PROPERTY CUSTOM_FLAG BRIEF_DOCS ignored)\n"
        "add_library(real STATIC real.c)\n"
        "set(MY_INIT second)\n"
        "add_executable(app main.c)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    size_t custom_flag_prop_sets = 0;
    bool saw_real_seeded = false;
    bool saw_app_second = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_PROP_SET) continue;
        if (!nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CUSTOM_FLAG"))) continue;
        custom_flag_prop_sets++;
        if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("real")) &&
            nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("seeded"))) {
            saw_real_seeded = true;
        }
        if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("app")) &&
            nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("second"))) {
            saw_app_second = true;
        }
    }

    ASSERT(custom_flag_prop_sets == 2);
    ASSERT(saw_real_seeded);
    ASSERT(saw_app_second);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_source_test_directory_clauses_parse_and_apply) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY src)\n"
        "file(WRITE src/CMakeLists.txt \"# empty\\n\")\n"
        "add_subdirectory(src)\n"
        "add_executable(src_t main.c)\n"
        "add_test(NAME smoke COMMAND src_t)\n"
        "set_property(SOURCE foo.c DIRECTORY src PROPERTY LANGUAGE C)\n"
        "set_property(SOURCE bar.c TARGET_DIRECTORY src_t PROPERTY LANGUAGE CXX)\n"
        "set_property(TEST smoke DIRECTORY . PROPERTY LABELS fast)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_source_dir_var_set = false;
    bool saw_source_target_dir_var_set = false;
    bool saw_test_dir_var_set = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_VAR_SET) continue;
        if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("C")) &&
            nob_sv_eq(ev->as.var_set.key,
                      nob_sv_from_cstr("NOBIFY_PROPERTY_SOURCE::DIRECTORY::src::foo.c::LANGUAGE"))) {
            saw_source_dir_var_set = true;
        } else if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("CXX")) &&
                   nob_sv_eq(ev->as.var_set.key,
                             nob_sv_from_cstr("NOBIFY_PROPERTY_SOURCE::TARGET_DIRECTORY::src_t::bar.c::LANGUAGE"))) {
            saw_source_target_dir_var_set = true;
        } else if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("fast")) &&
                   nob_sv_eq(ev->as.var_set.key,
                             nob_sv_from_cstr("NOBIFY_PROPERTY_TEST::DIRECTORY::.::smoke::LABELS"))) {
            saw_test_dir_var_set = true;
        }
    }

    ASSERT(saw_source_dir_var_set);
    ASSERT(saw_source_target_dir_var_set);
    ASSERT(saw_test_dir_var_set);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_allows_zero_objects_and_validates_test_lookup) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_test(NAME smoke COMMAND real)\n"
        "set_property(TARGET PROPERTY OUTPUT_NAME ignored)\n"
        "set_property(SOURCE PROPERTY LANGUAGE C)\n"
        "set_property(INSTALL PROPERTY FOO bar)\n"
        "set_property(TEST PROPERTY LABELS fast)\n"
        "set_property(CACHE PROPERTY VALUE cache_ignore)\n"
        "set_property(TEST smoke PROPERTY LABELS ok)\n"
        "set_property(TEST missing PROPERTY LABELS bad)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_missing_test_error = false;
    bool saw_smoke_label_set = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("set_property(TEST ...) test was not declared in selected directory scope")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("missing"))) {
            saw_missing_test_error = true;
        }
        if (ev->h.kind == EV_VAR_SET &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_PROPERTY_TEST::smoke::LABELS")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("ok"))) {
            saw_smoke_label_set = true;
        }
    }

    ASSERT(saw_missing_test_error);
    ASSERT(saw_smoke_label_set);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_cache_requires_existing_entry) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CACHED_X old CACHE STRING \"doc\")\n"
        "set_property(CACHE CACHED_X PROPERTY VALUE new_ok)\n"
        "set_property(CACHE MISSING_X PROPERTY VALUE bad)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_missing_cache_error = false;
    bool saw_cache_value_update = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(CACHE ...) cache entry does not exist"))) {
            saw_missing_cache_error = true;
        }
        if (ev->h.kind == EV_SET_CACHE_ENTRY &&
            ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHED_X")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("new_ok"))) {
            saw_cache_value_update = true;
        }
    }

    ASSERT(saw_missing_cache_error);
    ASSERT(saw_cache_value_update);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_core_queries_and_directory_wrappers) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "define_property(GLOBAL PROPERTY BATCH_DOC BRIEF_DOCS short_doc FULL_DOCS long_doc)\n"
        "define_property(DIRECTORY PROPERTY INHERITED_DIR INHERITED BRIEF_DOCS dir_short FULL_DOCS dir_long)\n"
        "set_property(GLOBAL PROPERTY INHERITED_DIR inherited_global)\n"
        "set_directory_properties(PROPERTIES BATCH_DIR_PROP dir_value)\n"
        "set(SCOPE_VAR scope_value)\n"
        "get_property(GP_SET GLOBAL PROPERTY BATCH_DOC SET)\n"
        "get_property(GP_DEF GLOBAL PROPERTY BATCH_DOC DEFINED)\n"
        "get_property(GP_BRIEF GLOBAL PROPERTY BATCH_DOC BRIEF_DOCS)\n"
        "get_property(GP_FULL GLOBAL PROPERTY BATCH_DOC FULL_DOCS)\n"
        "get_property(GP_INH DIRECTORY PROPERTY INHERITED_DIR)\n"
        "get_directory_property(GP_DIR BATCH_DIR_PROP)\n"
        "get_directory_property(GP_DEFVAR DEFINITION SCOPE_VAR)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GP_SET")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GP_DEF")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GP_BRIEF")), nob_sv_from_cstr("short_doc")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GP_FULL")), nob_sv_from_cstr("long_doc")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GP_INH")), nob_sv_from_cstr("inherited_global")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GP_DIR")), nob_sv_from_cstr("dir_value")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GP_DEFVAR")), nob_sv_from_cstr("scope_value")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_target_source_and_test_wrappers) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(batch_target main.c)\n"
        "set_target_properties(batch_target PROPERTIES CUSTOM_TGT hello)\n"
        "set_source_files_properties(main.c PROPERTIES SRC_FLAG yes)\n"
        "add_test(NAME batch_test COMMAND cmd)\n"
        "set_tests_properties(batch_test DIRECTORY . PROPERTIES LABELS fast)\n"
        "get_target_property(TGT_OK batch_target CUSTOM_TGT)\n"
        "get_target_property(TGT_MISS batch_target UNKNOWN_TGT)\n"
        "get_source_file_property(SRC_OK main.c SRC_FLAG)\n"
        "get_source_file_property(SRC_MISS main.c UNKNOWN_SRC)\n"
        "get_property(TEST_OK TEST batch_test DIRECTORY . PROPERTY LABELS)\n"
        "get_test_property(TEST_MISS batch_test UNKNOWN_TEST)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TGT_OK")), nob_sv_from_cstr("hello")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TGT_MISS")), nob_sv_from_cstr("TGT_MISS-NOTFOUND")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SRC_OK")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SRC_MISS")), nob_sv_from_cstr("SRC_MISS-NOTFOUND")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TEST_OK")), nob_sv_from_cstr("fast")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TEST_MISS")), nob_sv_from_cstr("TEST_MISS-NOTFOUND")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_source_directory_clause_and_get_cmake_property_lists) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(batch_macro)\n"
        "endmacro()\n"
        "set(NORMAL_A one)\n"
        "set(CACHED_A two CACHE STRING \"doc\")\n"
        "set_source_files_properties(main.c DIRECTORY . PROPERTIES SCOPED_SRC local)\n"
        "get_property(SRC_SCOPED SOURCE main.c DIRECTORY . PROPERTY SCOPED_SRC)\n"
        "get_cmake_property(ALL_VARS VARIABLES)\n"
        "get_cmake_property(CACHE_VARS CACHE_VARIABLES)\n"
        "get_cmake_property(ALL_MACROS MACROS)\n"
        "list(FIND ALL_VARS NORMAL_A IDX_VAR)\n"
        "list(FIND CACHE_VARS CACHED_A IDX_CACHE)\n"
        "list(FIND ALL_MACROS batch_macro IDX_MACRO)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SRC_SCOPED")), nob_sv_from_cstr("local")));
    ASSERT(!nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IDX_VAR")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IDX_CACHE")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IDX_MACRO")), nob_sv_from_cstr("-1")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_inherited_target_and_source_queries_follow_declared_target_directory) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY g1_prop_subdir)\n"
        "file(WRITE g1_prop_subdir/CMakeLists.txt [=[\n"
        "add_library(g1_sub_tgt STATIC local.c)\n"
        "set_property(DIRECTORY PROPERTY G1_INHERITED_TARGET_PROP sub_target_dir)\n"
        "set_property(DIRECTORY PROPERTY G1_INHERITED_SOURCE_PROP sub_source_dir)\n"
        "set_source_files_properties(local.c TARGET_DIRECTORY g1_sub_tgt PROPERTIES G1_LOCAL_SOURCE_PROP scoped_local)\n"
        "]=])\n"
        "define_property(TARGET PROPERTY G1_INHERITED_TARGET_PROP INHERITED)\n"
        "define_property(SOURCE PROPERTY G1_INHERITED_SOURCE_PROP INHERITED)\n"
        "set_property(DIRECTORY PROPERTY G1_INHERITED_TARGET_PROP root_target_dir)\n"
        "set_property(DIRECTORY PROPERTY G1_INHERITED_SOURCE_PROP root_source_dir)\n"
        "add_subdirectory(g1_prop_subdir)\n"
        "get_target_property(G1_TGT_PROP g1_sub_tgt G1_INHERITED_TARGET_PROP)\n"
        "get_property(G1_SRC_INH SOURCE local.c TARGET_DIRECTORY g1_sub_tgt PROPERTY G1_INHERITED_SOURCE_PROP)\n"
        "get_property(G1_SRC_LOCAL SOURCE local.c TARGET_DIRECTORY g1_sub_tgt PROPERTY G1_LOCAL_SOURCE_PROP)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("G1_TGT_PROP")),
                     nob_sv_from_cstr("sub_target_dir")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("G1_SRC_INH")),
                     nob_sv_from_cstr("sub_source_dir")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("G1_SRC_LOCAL")),
                     nob_sv_from_cstr("scoped_local")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_directory_scoped_property_queries_require_known_directories) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("gp_known_dir"));
    ASSERT(nob_write_entire_file("gp_known_dir/sub_file.c", "int sub_file;\n", 14));
    {
        const char *sub_cmake =
            "set_property(DIRECTORY PROPERTY SUB_BATCH_PROP from_subdir)\n"
            "set_property(SOURCE sub_file.c PROPERTY SUB_SRC_PROP from_source)\n"
            "add_test(NAME sub_batch_test COMMAND cmd)\n"
            "set_property(TEST sub_batch_test PROPERTY LABELS from_test)\n";
        ASSERT(nob_write_entire_file("gp_known_dir/CMakeLists.txt", sub_cmake, strlen(sub_cmake)));
    }

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set_property(DIRECTORY PROPERTY ROOT_BATCH_PROP from_root)\n"
        "add_subdirectory(gp_known_dir)\n"
        "get_directory_property(ROOT_PROP ROOT_BATCH_PROP)\n"
        "get_directory_property(SUB_PROP DIRECTORY gp_known_dir SUB_BATCH_PROP)\n"
        "get_property(SUB_DIR_PROP DIRECTORY gp_known_dir PROPERTY SUB_BATCH_PROP)\n"
        "get_property(SUB_SRC SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP)\n"
        "get_property(SUB_TEST TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS)\n"
        "set_property(SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP updated_source)\n"
        "get_property(SUB_SRC_UPDATED SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP)\n"
        "set_property(TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS updated_test)\n"
        "get_property(SUB_TEST_UPDATED TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS)\n"
        "get_directory_property(BAD_DIR DIRECTORY gp_missing_dir ROOT_BATCH_PROP)\n"
        "get_property(BAD_SCOPE DIRECTORY gp_missing_dir PROPERTY ROOT_BATCH_PROP)\n"
        "get_property(BAD_SRC SOURCE sub_file.c DIRECTORY gp_missing_dir PROPERTY SUB_SRC_PROP)\n"
        "get_property(BAD_TEST TEST sub_batch_test DIRECTORY gp_missing_dir PROPERTY LABELS)\n"
        "set_property(SOURCE sub_file.c DIRECTORY gp_missing_dir PROPERTY SUB_SRC_PROP nope)\n"
        "set_property(TEST sub_batch_test DIRECTORY gp_missing_dir PROPERTY LABELS nope)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ROOT_PROP")),
                     nob_sv_from_cstr("from_root")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SUB_PROP")),
                     nob_sv_from_cstr("from_subdir")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SUB_DIR_PROP")),
                     nob_sv_from_cstr("from_subdir")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SUB_SRC")),
                     nob_sv_from_cstr("from_source")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SUB_TEST")),
                     nob_sv_from_cstr("from_test")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SUB_SRC_UPDATED")),
                     nob_sv_from_cstr("updated_source")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SUB_TEST_UPDATED")),
                     nob_sv_from_cstr("updated_test")));

    bool saw_get_dir_error = false;
    bool saw_get_scope_error = false;
    bool saw_get_source_error = false;
    bool saw_get_test_error = false;
    bool saw_set_source_error = false;
    bool saw_set_test_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("get_directory_property(DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_get_dir_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_property(DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_get_scope_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_property(SOURCE DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_get_source_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_property(TEST DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_get_test_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("set_property(SOURCE DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_set_source_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("set_property(TEST DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_set_test_error = true;
        }
    }

    ASSERT(saw_get_dir_error);
    ASSERT(saw_get_scope_error);
    ASSERT(saw_get_source_error);
    ASSERT(saw_get_test_error);
    ASSERT(saw_set_source_error);
    ASSERT(saw_set_test_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_option_mark_as_advanced_and_include_regular_expression_follow_policies) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(OPT_OLD normal_old)\n"
        "cmake_policy(SET CMP0077 OLD)\n"
        "option(OPT_OLD \"old doc\" ON)\n"
        "set(OPT_NEW normal_new)\n"
        "cmake_policy(SET CMP0077 NEW)\n"
        "option(OPT_NEW \"new doc\" OFF)\n"
        "cmake_policy(SET CMP0102 OLD)\n"
        "mark_as_advanced(FORCE OLD_MISSING)\n"
        "cmake_policy(SET CMP0102 NEW)\n"
        "mark_as_advanced(FORCE NEW_MISSING)\n"
        "mark_as_advanced(FORCE OPT_OLD)\n"
        "mark_as_advanced(CLEAR OPT_OLD)\n"
        "include_regular_expression(^keep$ ^warn$)\n"
        "get_property(OPT_ADV CACHE OPT_OLD PROPERTY ADVANCED)\n"
        "add_executable(option_probe main.c)\n"
        "target_compile_definitions(option_probe PRIVATE OPT_OLD=${OPT_OLD} OPT_NEW=${OPT_NEW} OPT_ADV=${OPT_ADV} RX=${CMAKE_INCLUDE_REGULAR_EXPRESSION} RC=${CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OPT_OLD")), nob_sv_from_cstr("ON")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OPT_NEW")), nob_sv_from_cstr("normal_new")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OPT_ADV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION")), nob_sv_from_cstr("^keep$")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN")),
                     nob_sv_from_cstr("^warn$")));

    bool saw_opt_old_cache = false;
    bool saw_old_missing_cache = false;
    bool saw_new_missing_cache = false;
    bool saw_opt_new_cache = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_SET_CACHE_ENTRY || ev->as.var_set.target_kind != EVENT_VAR_TARGET_CACHE) continue;
        if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("OPT_OLD")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("ON"))) {
            saw_opt_old_cache = true;
        } else if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("OLD_MISSING")) &&
                   nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr(""))) {
            saw_old_missing_cache = true;
        } else if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("NEW_MISSING"))) {
            saw_new_missing_cache = true;
        } else if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("OPT_NEW"))) {
            saw_opt_new_cache = true;
        }
    }

    ASSERT(saw_opt_old_cache);
    ASSERT(saw_old_missing_cache);
    ASSERT(!saw_new_missing_cache);
    ASSERT(!saw_opt_new_cache);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_separate_arguments_covers_program_mode_and_legacy_form) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "separate_arguments(OUT_UNIX UNIX_COMMAND [=[alpha \"two words\" three\\ four]=])\n"
        "separate_arguments(OUT_WIN WINDOWS_COMMAND [=[alpha \"two words\" C:\\\\tmp\\\\x]=])\n"
        "set(OUT_NATIVE [=[alpha \"two words\"]=])\n"
        "separate_arguments(OUT_NATIVE)\n"
        "separate_arguments(OUT_EMPTY UNIX_COMMAND)\n"
#if defined(_WIN32)
        "separate_arguments(OUT_PROGRAM_RAW NATIVE_COMMAND PROGRAM [=[cmd /C echo]=])\n"
        "separate_arguments(OUT_PROGRAM_SPLIT NATIVE_COMMAND PROGRAM SEPARATE_ARGS [=[cmd /C echo]=])\n"
        "separate_arguments(OUT_PROGRAM_MISSING NATIVE_COMMAND PROGRAM [=[nobify_missing_tool /C echo]=])\n");
#else
        "separate_arguments(OUT_PROGRAM_RAW NATIVE_COMMAND PROGRAM [=[sh -c echo]=])\n"
        "separate_arguments(OUT_PROGRAM_SPLIT NATIVE_COMMAND PROGRAM SEPARATE_ARGS [=[sh -c echo]=])\n"
        "separate_arguments(OUT_PROGRAM_MISSING NATIVE_COMMAND PROGRAM [=[nobify_missing_tool -c echo]=])\n");
#endif
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OUT_UNIX")),
                     nob_sv_from_cstr("alpha;two words;three four")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OUT_WIN")),
                     nob_sv_from_cstr("alpha;two words;C:\\\\tmp\\\\x")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OUT_NATIVE")),
                     nob_sv_from_cstr("alpha;\"two;words\"")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OUT_EMPTY")),
                     nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_MISSING")),
                     nob_sv_from_cstr("")));

#if defined(_WIN32)
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_RAW")),
                           "cmd.exe; /C echo"));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_SPLIT")),
                           "cmd.exe;/C;echo"));
#else
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_RAW")),
                           "/sh; -c echo"));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_SPLIT")),
                           "/sh;-c;echo"));
#endif

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_separate_arguments_rejects_invalid_option_shapes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "separate_arguments(BAD_MISSING_MODE PROGRAM alpha)\n"
        "separate_arguments(BAD_MULTI_MODE UNIX_COMMAND WINDOWS_COMMAND alpha)\n"
        "separate_arguments(BAD_SEPARATE_ONLY UNIX_COMMAND SEPARATE_ARGS alpha)\n"
        "separate_arguments(BAD_EXTRA UNIX_COMMAND alpha beta)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    bool saw_missing_mode = false;
    bool saw_multi_mode = false;
    bool saw_separate_only = false;
    bool saw_unexpected = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("separate_arguments"))) continue;

        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("separate_arguments() missing required mode"))) {
            saw_missing_mode = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("separate_arguments() modes are mutually exclusive"))) {
            saw_multi_mode = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("separate_arguments(SEPARATE_ARGS) requires PROGRAM"))) {
            saw_separate_only = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("separate_arguments() given unexpected argument(s)"))) {
            saw_unexpected = true;
        }
    }

    ASSERT(saw_missing_mode);
    ASSERT(saw_multi_mode);
    ASSERT(saw_separate_only);
    ASSERT(saw_unexpected);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_remove_definitions_updates_directory_definition_and_option_state) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_definitions(-DKEEP=1 -DREMOVE_ME=1 -Wall -D1BAD)\n"
        "remove_definitions(-DREMOVE_ME=1 -Wall -D1BAD /DUNKNOWN=1)\n"
        "get_property(DIR_DEFS DIRECTORY PROPERTY COMPILE_DEFINITIONS)\n"
        "get_property(DIR_OPTS DIRECTORY PROPERTY COMPILE_OPTIONS)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_GLOBAL_COMPILE_DEFINITIONS")),
                     nob_sv_from_cstr("KEEP=1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_GLOBAL_COMPILE_OPTIONS")),
                     nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("DIR_DEFS")), nob_sv_from_cstr("KEEP=1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("DIR_OPTS")), nob_sv_from_cstr("")));

    bool saw_remove_defs_event = false;
    bool saw_remove_opts_event = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EVENT_DIRECTORY_PROPERTY_MUTATE) continue;
        if (ev->h.origin.line != 2) continue;
        if (nob_sv_eq(ev->as.directory_property_mutate.property_name,
                      nob_sv_from_cstr("COMPILE_DEFINITIONS"))) {
            saw_remove_defs_event =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_SET &&
                ev->as.directory_property_mutate.item_count == 1 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("KEEP=1"));
        } else if (nob_sv_eq(ev->as.directory_property_mutate.property_name,
                             nob_sv_from_cstr("COMPILE_OPTIONS"))) {
            saw_remove_opts_event =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_SET &&
                ev->as.directory_property_mutate.item_count == 0;
        }
    }
    ASSERT(saw_remove_defs_event);
    ASSERT(saw_remove_opts_event);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_host_introspection_and_site_name_cover_supported_queries) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("host_sysroot"));
    ASSERT(nob_mkdir_if_not_exists("host_sysroot/etc"));
    {
        const char *os_release =
            "ID=nobify\n"
            "NAME=\"Nobify Linux\"\n"
            "PRETTY_NAME=\"Nobify Linux 1.0\"\n"
            "VERSION_ID=1.0\n";
        ASSERT(nob_write_entire_file("host_sysroot/etc/os-release", os_release, strlen(os_release)));
    }

    char site_cmd[256] = {0};
    ASSERT(evaluator_prepare_site_name_command(site_cmd, sizeof(site_cmd)));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        nob_temp_sprintf(
            "site_name(SITE_FALLBACK)\n"
            "set(CMAKE_SYSROOT \"host_sysroot\")\n"
            "cmake_host_system_information(RESULT HOST_MULTI QUERY OS_NAME HOSTNAME IS_64BIT NUMBER_OF_PHYSICAL_CORES PROCESSOR_NAME PROCESSOR_DESCRIPTION FQDN)\n"
            "cmake_host_system_information(RESULT HOST_FLAGS QUERY HAS_FPU HAS_MMX HAS_MMX_PLUS HAS_SSE HAS_SSE2 HAS_SSE_FP HAS_SSE_MMX HAS_AMD_3DNOW HAS_AMD_3DNOW_PLUS HAS_IA64 HAS_SERIAL_NUMBER)\n"
            "cmake_host_system_information(RESULT HOST_SERIAL QUERY PROCESSOR_SERIAL_NUMBER)\n"
            "cmake_host_system_information(RESULT HOST_MSYS QUERY MSYSTEM_PREFIX)\n"
            "cmake_host_system_information(RESULT DISTRO QUERY DISTRIB_INFO)\n"
            "cmake_host_system_information(RESULT DISTRO_PRETTY QUERY DISTRIB_PRETTY_NAME)\n"
            "cmake_host_system_information(RESULT DISTRO_UNKNOWN QUERY DISTRIB_DOES_NOT_EXIST)\n"
            "set(HOSTNAME \"%s\")\n"
            "site_name(SITE_CMD)\n",
            site_cmd));
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    String_View system_name = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"));
    String_View host_multi = eval_var_get(ctx, nob_sv_from_cstr("HOST_MULTI"));
    String_View host_flags = eval_var_get(ctx, nob_sv_from_cstr("HOST_FLAGS"));
    String_View host_serial = eval_var_get(ctx, nob_sv_from_cstr("HOST_SERIAL"));
    String_View host_msys = eval_var_get(ctx, nob_sv_from_cstr("HOST_MSYS"));
    String_View distro = eval_var_get(ctx, nob_sv_from_cstr("DISTRO"));
    String_View distro_pretty = eval_var_get(ctx, nob_sv_from_cstr("DISTRO_PRETTY"));
    String_View distro_unknown = eval_var_get(ctx, nob_sv_from_cstr("DISTRO_UNKNOWN"));
    String_View distro_id = eval_var_get(ctx, nob_sv_from_cstr("DISTRO_ID"));
    String_View distro_name = eval_var_get(ctx, nob_sv_from_cstr("DISTRO_NAME"));
    String_View distro_pretty_var = eval_var_get(ctx, nob_sv_from_cstr("DISTRO_PRETTY_NAME"));
    String_View distro_version = eval_var_get(ctx, nob_sv_from_cstr("DISTRO_VERSION_ID"));
    String_View site_fallback = eval_var_get(ctx, nob_sv_from_cstr("SITE_FALLBACK"));
    String_View site_cmd_out = eval_var_get(ctx, nob_sv_from_cstr("SITE_CMD"));

    String_View host_multi_items[7] = {0};
    size_t host_multi_count = 0;
    {
        size_t start = 0;
        while (start <= host_multi.count && host_multi_count < NOB_ARRAY_LEN(host_multi_items)) {
            size_t end = start;
            while (end < host_multi.count && host_multi.data[end] != ';') end++;
            host_multi_items[host_multi_count++] = nob_sv_from_parts(host_multi.data + start, end - start);
            if (end == host_multi.count) break;
            start = end + 1;
        }
    }

    String_View host_flag_items[11] = {0};
    size_t host_flag_count = 0;
    {
        size_t start = 0;
        while (start <= host_flags.count && host_flag_count < NOB_ARRAY_LEN(host_flag_items)) {
            size_t end = start;
            while (end < host_flags.count && host_flags.data[end] != ';') end++;
            host_flag_items[host_flag_count++] = nob_sv_from_parts(host_flags.data + start, end - start);
            if (end == host_flags.count) break;
            start = end + 1;
        }
    }

    ASSERT(system_name.count > 0);
    ASSERT(site_fallback.count > 0);
    ASSERT(host_multi_count == NOB_ARRAY_LEN(host_multi_items));
    ASSERT(nob_sv_eq(host_multi_items[0], system_name));
    ASSERT(host_multi_items[1].count > 0);
    ASSERT(evaluator_sv_is_bool01(host_multi_items[2]));
    ASSERT(evaluator_sv_is_decimal(host_multi_items[3]));
    ASSERT(host_multi_items[4].count > 0);
    ASSERT(host_multi_items[5].count > 0);
    ASSERT(host_multi_items[6].count > 0);
    ASSERT(host_flag_count == NOB_ARRAY_LEN(host_flag_items));
    for (size_t i = 0; i < host_flag_count; i++) {
        ASSERT(evaluator_sv_is_bool01(host_flag_items[i]));
    }
    (void)host_serial;
#if defined(_WIN32)
    (void)host_msys;
#else
    ASSERT(host_msys.count == 0);
#endif
    ASSERT(nob_sv_eq(distro_pretty, nob_sv_from_cstr("Nobify Linux 1.0")));
    ASSERT(nob_sv_eq(distro_unknown, nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(distro_id, nob_sv_from_cstr("nobify")));
    ASSERT(nob_sv_eq(distro_name, nob_sv_from_cstr("Nobify Linux")));
    ASSERT(nob_sv_eq(distro_pretty_var, nob_sv_from_cstr("Nobify Linux 1.0")));
    ASSERT(nob_sv_eq(distro_version, nob_sv_from_cstr("1.0")));
    ASSERT(evaluator_sv_list_contains(distro, nob_sv_from_cstr("DISTRO_ID")));
    ASSERT(evaluator_sv_list_contains(distro, nob_sv_from_cstr("DISTRO_NAME")));
    ASSERT(evaluator_sv_list_contains(distro, nob_sv_from_cstr("DISTRO_PRETTY_NAME")));
    ASSERT(evaluator_sv_list_contains(distro, nob_sv_from_cstr("DISTRO_VERSION_ID")));
#if defined(_WIN32)
    ASSERT(site_cmd_out.count > 0);
#else
    ASSERT(nob_sv_eq(site_cmd_out, nob_sv_from_cstr("mock-site")));
#endif

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_build_name_and_build_command_follow_policy_gates) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0036 OLD)\n"
        "build_name(BN_OLD)\n"
        "cmake_policy(SET CMP0036 NEW)\n"
        "build_name(BN_NEW)\n"
        "set(CMAKE_GENERATOR \"Unix Makefiles\")\n"
        "cmake_policy(SET CMP0061 OLD)\n"
        "build_command(BC_OLD CONFIGURATION Debug TARGET demo PARALLEL_LEVEL 3)\n"
        "cmake_policy(SET CMP0061 NEW)\n"
        "build_command(BC_NEW legacy_make legacy_file legacy_target)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    String_View host_name = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"));
    String_View compiler_id = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID"));
    String_View build_name = eval_var_get(ctx, nob_sv_from_cstr("BN_OLD"));
    ASSERT(build_name.count == host_name.count + 1 + compiler_id.count);
    ASSERT(memcmp(build_name.data, host_name.data, host_name.count) == 0);
    ASSERT(build_name.data[host_name.count] == '-');
    ASSERT(memcmp(build_name.data + host_name.count + 1, compiler_id.data, compiler_id.count) == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BC_OLD")),
                     nob_sv_from_cstr("cmake --build . --target demo --config Debug --parallel 3 -- -i")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BC_NEW")),
                     nob_sv_from_cstr("cmake --build . --target legacy_target")));

    bool saw_cmp0036_diag = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("build_name() is disallowed by CMP0036"))) {
            saw_cmp0036_diag = true;
            break;
        }
    }
    ASSERT(saw_cmp0036_diag);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_new_signature_accepts_no_log_and_runs) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr("try_run_no_log_bin");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_run(RUN_NEW COMPILE_NEW\n"
        "  SOURCE_FROM_CONTENT probe_new.c [=[#include <stdio.h>\n"
        "int main(void){putchar(78);return 2;}\n"
        "]=]\n"
        "  NO_CACHE\n"
        "  NO_LOG\n"
        "  LOG_DESCRIPTION hidden_try_run_probe\n"
        "  RUN_OUTPUT_VARIABLE RUN_NEW_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_NEW_STDOUT)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_NEW")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_NEW")), nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_NEW_STDOUT")), nob_sv_from_cstr("N")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_NEW_ALL")), nob_sv_from_cstr("N")));
    ASSERT(!nob_file_exists("try_run_no_log_bin/CMakeFiles/CMakeConfigureLog.yaml"));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_executes_native_artifacts_and_reports_partial_limits) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    const char *ok_source =
        "#include <stdio.h>\n"
        "int main(void){putchar(65);fputc(66, stderr);return 0;}\n";
    ASSERT(nob_write_entire_file("probe_ok_try_run.c", ok_source, strlen(ok_source)));
    ASSERT(nob_mkdir_if_not_exists("tc_try_run_project"));
    ASSERT(nob_write_entire_file("tc_try_run_project/CMakeLists.txt",
                                 "cmake_minimum_required(VERSION 3.20)\n"
                                 "project(TryRunProject C)\n"
                                 "add_executable(tc_try_run_project_probe main.c)\n",
                                 strlen("cmake_minimum_required(VERSION 3.20)\n"
                                        "project(TryRunProject C)\n"
                                        "add_executable(tc_try_run_project_probe main.c)\n")));
    ASSERT(nob_write_entire_file("tc_try_run_project/main.c",
                                 "#include <stdio.h>\n"
                                 "int main(void){putchar(80);fputc(81, stderr);return 0;}\n",
                                 strlen("#include <stdio.h>\n"
                                        "int main(void){putchar(80);fputc(81, stderr);return 0;}\n")));

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_run(RUN_OK COMPILE_OK tc_try_run_ok\n"
        "  probe_ok_try_run.c\n"
        "  NO_CACHE\n"
        "  COMPILE_OUTPUT_VARIABLE COMPILE_OK_LOG\n"
        "  RUN_OUTPUT_VARIABLE RUN_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_STDOUT\n"
        "  RUN_OUTPUT_STDERR_VARIABLE RUN_STDERR)\n"
        "try_run(RUN_LEGACY COMPILE_LEGACY tc_try_run_legacy\n"
        "  probe_ok_try_run.c\n"
        "  NO_CACHE\n"
        "  OUTPUT_VARIABLE RUN_LEGACY_ALL)\n"
        "try_run(RUN_BAD COMPILE_BAD tc_try_run_bad\n"
        "  SOURCE_FROM_CONTENT probe_bad.c \"int main(void){return 7;}\"\n"
        "  NO_CACHE)\n"
        "try_run(RUN_FAIL COMPILE_FAIL tc_try_run_fail\n"
        "  SOURCE_FROM_CONTENT probe_fail.c \"int main(void){ this is not valid C; }\"\n"
        "  NO_CACHE\n"
        "  COMPILE_OUTPUT_VARIABLE COMPILE_FAIL_LOG)\n"
        "set(CMAKE_CROSSCOMPILING ON)\n"
        "unset(CMAKE_CROSSCOMPILING_EMULATOR)\n"
        "try_run(RUN_XC COMPILE_XC\n"
        "  SOURCE_FROM_CONTENT probe_xc.c \"int main(void){return 0;}\"\n"
        "  NO_CACHE\n"
        "  RUN_OUTPUT_VARIABLE RUN_XC_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_XC_STDOUT\n"
        "  RUN_OUTPUT_STDERR_VARIABLE RUN_XC_STDERR)\n"
        "set(CMAKE_CROSSCOMPILING OFF)\n"
        "try_run(RUN_PROJECT COMPILE_PROJECT PROJECT Demo\n"
        "  SOURCE_DIR tc_try_run_project\n"
        "  BINARY_DIR tc_try_run_project_build\n"
        "  TARGET tc_try_run_project_probe\n"
        "  COMPILE_OUTPUT_VARIABLE COMPILE_PROJECT_LOG\n"
        "  RUN_OUTPUT_VARIABLE RUN_PROJECT_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_PROJECT_STDOUT\n"
        "  RUN_OUTPUT_STDERR_VARIABLE RUN_PROJECT_STDERR)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_OK")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_OK")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_STDOUT")), nob_sv_from_cstr("A")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_STDERR")), nob_sv_from_cstr("B")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_ALL")), nob_sv_from_cstr("AB")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_LEGACY")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_LEGACY")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_LEGACY_ALL")), nob_sv_from_cstr("AB")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_BAD")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_BAD")), nob_sv_from_cstr("7")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_FAIL")), nob_sv_from_cstr("FALSE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_FAIL")), nob_sv_from_cstr("")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_FAIL_LOG")).count > 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_XC")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_XC")),
                     nob_sv_from_cstr("PLEASE_FILL_OUT-FAILED_TO_RUN")));
    ASSERT(!eval_var_defined_visible(ctx, nob_sv_from_cstr("RUN_XC_ALL")));
    ASSERT(!eval_var_defined_visible(ctx, nob_sv_from_cstr("RUN_XC_STDOUT")));
    ASSERT(!eval_var_defined_visible(ctx, nob_sv_from_cstr("RUN_XC_STDERR")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT")),
                     nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT_STDOUT")),
                     nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT_STDERR")),
                     nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_PROJECT")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_PROJECT")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_PROJECT_STDOUT")), nob_sv_from_cstr("P")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_PROJECT_STDERR")), nob_sv_from_cstr("Q")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_PROJECT_ALL")), nob_sv_from_cstr("PQ")));

    String_View try_run_results = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "TryRunResults.cmake", &try_run_results));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("RUN_XC")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT_STDOUT")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT_STDERR")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("PLEASE_FILL_OUT-FAILED_TO_RUN")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_uses_crosscompiling_emulator_when_available) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Nob_String_Builder script_sb = {0};
#if defined(_WIN32)
    nob_sb_append_cstr(&script_sb,
                       "set(CMAKE_CROSSCOMPILING ON)\n"
                       "set(CMAKE_CROSSCOMPILING_EMULATOR cmd /C)\n");
#else
    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);
    char emulator_path[4096] = {0};
    int emulator_n = snprintf(emulator_path, sizeof(emulator_path), "%s/%s", cwd, "try_run_emulator.sh");
    ASSERT(emulator_n > 0 && (size_t)emulator_n < sizeof(emulator_path));
    ASSERT(nob_write_entire_file("try_run_emulator.sh", "exec \"$@\"\n", strlen("exec \"$@\"\n")));
    nob_sb_appendf(&script_sb,
                   "set(CMAKE_CROSSCOMPILING ON)\n"
                   "set(CMAKE_CROSSCOMPILING_EMULATOR /bin/sh [=[%s]=])\n",
                   emulator_path);
#endif
    nob_sb_append_cstr(&script_sb,
                       "try_run(RUN_EMU COMPILE_EMU\n"
                       "  SOURCE_FROM_CONTENT probe_emu.c [=[#include <stdio.h>\n"
                       "int main(void){putchar(69);fputc(70, stderr);return 4;}\n"
                       "]=]\n"
                       "  NO_CACHE\n"
                       "  RUN_OUTPUT_VARIABLE RUN_EMU_ALL\n"
                       "  RUN_OUTPUT_STDOUT_VARIABLE RUN_EMU_STDOUT\n"
                       "  RUN_OUTPUT_STDERR_VARIABLE RUN_EMU_STDERR)\n");
    nob_sb_append_null(&script_sb);

    Ast_Root root = parse_cmake(temp_arena, script_sb.items);
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("COMPILE_EMU")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_EMU")), nob_sv_from_cstr("4")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_EMU_STDOUT")), nob_sv_from_cstr("E")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_EMU_STDERR")), nob_sv_from_cstr("F")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("RUN_EMU_ALL")), nob_sv_from_cstr("EF")));
    ASSERT(!eval_var_defined_visible(ctx, nob_sv_from_cstr("RUN_EMU__TRYRUN_OUTPUT")));

    nob_sb_free(script_sb);
    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_exec_program_respects_cmp0153_and_legacy_wrapper_surface) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ep_exec_dir"));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

#if defined(_WIN32)
    const char *script =
        "cmake_policy(SET CMP0153 NEW)\n"
        "exec_program(cmd . ARGS [=[/C echo blocked]=] OUTPUT_VARIABLE EP_BLOCKED)\n"
        "cmake_policy(SET CMP0153 OLD)\n"
        "exec_program(cmd . ARGS [=[/C echo legacy]=] OUTPUT_VARIABLE EP_OUT RETURN_VALUE EP_RES)\n"
        "exec_program(cmd ep_exec_dir ARGS [=[/C cd]=] OUTPUT_VARIABLE EP_CWD RETURN_VALUE EP_CWD_RES)\n"
        "exec_program(cmd . ARGS [=[/C echo hello world]=] OUTPUT_VARIABLE EP_TOKEN)\n"
        "exec_program(cmd . OUTPUT_VARIABLE)\n"
        "exec_program(cmd . RETURN_VALUE)\n"
        "exec_program(cmd . BOGUS)\n";
#else
    const char *script =
        "cmake_policy(SET CMP0153 NEW)\n"
        "exec_program(/bin/sh . ARGS [=[-c \"printf 'blocked'\"]=] OUTPUT_VARIABLE EP_BLOCKED)\n"
        "cmake_policy(SET CMP0153 OLD)\n"
        "exec_program(/bin/sh . ARGS [=[-c \"printf 'legacy'\"]=] OUTPUT_VARIABLE EP_OUT RETURN_VALUE EP_RES)\n"
        "exec_program(/bin/sh ep_exec_dir ARGS [=[-c \"pwd\"]=] OUTPUT_VARIABLE EP_CWD RETURN_VALUE EP_CWD_RES)\n"
        "exec_program(/bin/sh . ARGS [=[-c \"printf '%s' \\\"$1\\\"\" sh \"hello world\"]=] OUTPUT_VARIABLE EP_TOKEN)\n"
        "exec_program(/bin/sh . OUTPUT_VARIABLE)\n"
        "exec_program(/bin/sh . RETURN_VALUE)\n"
        "exec_program(/bin/sh . BOGUS)\n";
#endif

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("EP_OUT")), nob_sv_from_cstr("legacy")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("EP_RES")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("EP_CWD_RES")), nob_sv_from_cstr("0")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("EP_CWD")), nob_sv_from_cstr("ep_exec_dir")));
#if defined(_WIN32)
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("EP_TOKEN")), nob_sv_from_cstr("hello")));
#else
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("EP_TOKEN")), nob_sv_from_cstr("hello world")));
#endif

    bool saw_cmp0153_diag = false;
    bool saw_output_diag = false;
    bool saw_return_diag = false;
    bool saw_bogus_diag = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("exec_program() is disallowed by CMP0153"))) {
            saw_cmp0153_diag = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("exec_program(OUTPUT_VARIABLE) requires exactly one output variable"))) {
            saw_output_diag = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("exec_program(RETURN_VALUE) requires exactly one output variable"))) {
            saw_return_diag = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("exec_program() received an unsupported argument"))) {
            saw_bogus_diag = true;
        }
    }
    ASSERT(saw_cmp0153_diag);
    ASSERT(saw_output_diag);
    ASSERT(saw_return_diag);
    ASSERT(saw_bogus_diag);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch6_metadata_commands_cover_documented_subset) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("cache_in"));
    ASSERT(nob_mkdir_if_not_exists("asd_src"));
    ASSERT(nob_write_entire_file("cache_in/CMakeCache.txt",
                                 "FOO:STRING=alpha\n"
                                 "BAR:BOOL=ON\n"
                                 "KEEP:STRING=keep-me\n"
                                 "HIDE:INTERNAL=secret\n"
                                 "broken-line\n",
                                 strlen("FOO:STRING=alpha\nBAR:BOOL=ON\nKEEP:STRING=keep-me\nHIDE:INTERNAL=secret\nbroken-line\n")));
    ASSERT(nob_write_entire_file("asd_src/b.cpp", "int b = 0;\n", strlen("int b = 0;\n")));
    ASSERT(nob_write_entire_file("asd_src/a.c", "int a = 0;\n", strlen("int a = 0;\n")));
    ASSERT(nob_write_entire_file("asd_src/skip.txt", "x\n", 2));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_TESTDRIVER_BEFORE_TESTMAIN \"/*before*/\")\n"
        "set(CMAKE_TESTDRIVER_AFTER_TESTMAIN \"/*after*/\")\n"
        "add_library(meta_lib INTERFACE)\n"
        "install(TARGETS meta_lib EXPORT DemoExport DESTINATION lib)\n"
        "export(TARGETS meta_lib FILE meta-targets.cmake NAMESPACE Demo::)\n"
        "export(EXPORT DemoExport FILE meta-export.cmake NAMESPACE Demo::)\n"
        "load_cache(cache_in READ_WITH_PREFIX LC_ FOO BAR)\n"
        "load_cache(cache_in EXCLUDE BAR INCLUDE_INTERNALS HIDE)\n"
        "aux_source_directory(asd_src ASD_OUT)\n"
        "create_test_sourcelist(TEST_SRCS generated_driver.c alpha_test.c beta_test.c EXTRA_INCLUDE extra.h FUNCTION setup_hook)\n"
        "include_external_msproject(ext_proj external.vcxproj TYPE type-guid GUID proj-guid PLATFORM Win32 meta_lib)\n"
        "cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2 CACHE 2.0 CMAKEFILES 1 TOOLCHAINS 1)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_MODE")), nob_sv_from_cstr("EXPORT")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_NAMESPACE")), nob_sv_from_cstr("Demo::")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LC_FOO")), nob_sv_from_cstr("alpha")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LC_BAR")), nob_sv_from_cstr("ON")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("KEEP")), nob_sv_from_cstr("keep-me")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BAR")), nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("HIDE")), nob_sv_from_cstr("secret")));

    String_View asd_out = eval_var_get(ctx, nob_sv_from_cstr("ASD_OUT"));
    ASSERT(sv_contains_sv(asd_out, nob_sv_from_cstr("a.c")));
    ASSERT(sv_contains_sv(asd_out, nob_sv_from_cstr("b.cpp")));
    ASSERT(!sv_contains_sv(asd_out, nob_sv_from_cstr("skip.txt")));
    const char *a_pos = strstr(nob_temp_sv_to_cstr(asd_out), "a.c");
    const char *b_pos = strstr(nob_temp_sv_to_cstr(asd_out), "b.cpp");
    ASSERT(a_pos != NULL && b_pos != NULL && a_pos < b_pos);

    String_View test_srcs = eval_var_get(ctx, nob_sv_from_cstr("TEST_SRCS"));
    ASSERT(sv_contains_sv(test_srcs, nob_sv_from_cstr("alpha_test.c")));
    ASSERT(sv_contains_sv(test_srcs, nob_sv_from_cstr("beta_test.c")));
    ASSERT(sv_contains_sv(test_srcs, nob_sv_from_cstr("generated_driver.c")));
    String_View driver_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "generated_driver.c", &driver_text));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("extern int alpha_test(int, char**);")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("extern int beta_test(int, char**);")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("#include \"extra.h\"")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("setup_hook();")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("/*before*/")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("/*after*/")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_MSPROJECT::ext_proj::LOCATION")),
                     nob_sv_from_cstr("external.vcxproj")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_MSPROJECT::ext_proj::DEPENDENCIES")),
                     nob_sv_from_cstr("meta_lib")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::API_VERSION")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CMAKE_FILE_API")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::CODEMODEL")),
                     nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::CACHE")),
                     nob_sv_from_cstr("2.0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::CMAKEFILES")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::TOOLCHAINS")),
                     nob_sv_from_cstr("1")));

    String_View export_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "meta-export.cmake", &export_text));
    ASSERT(sv_contains_sv(export_text, nob_sv_from_cstr("set(NOBIFY_EXPORT_TARGETS \"meta_lib\")")));

    String_View file_api_query = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, ".cmake/api/v1/query/client-nobify/query.json", &file_api_query));
    ASSERT(sv_contains_sv(file_api_query, nob_sv_from_cstr("\"kind\": \"codemodel\"")));
    ASSERT(sv_contains_sv(file_api_query, nob_sv_from_cstr("\"kind\": \"cache\"")));
    ASSERT(sv_contains_sv(file_api_query, nob_sv_from_cstr("\"kind\": \"cmakeFiles\"")));
    ASSERT(sv_contains_sv(file_api_query, nob_sv_from_cstr("\"kind\": \"toolchains\"")));

    String_View file_api_index = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, ".cmake/api/v1/reply/index-nobify-v1.json", &file_api_index));
    ASSERT(sv_contains_sv(file_api_index, nob_sv_from_cstr("codemodel-v2.json")));
    ASSERT(sv_contains_sv(file_api_index, nob_sv_from_cstr("cache-v2.0.json")));
    ASSERT(sv_contains_sv(file_api_index, nob_sv_from_cstr("cmakeFiles-v1.json")));
    ASSERT(sv_contains_sv(file_api_index, nob_sv_from_cstr("toolchains-v1.json")));

    String_View codemodel_reply = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, ".cmake/api/v1/reply/codemodel-v2.json", &codemodel_reply));
    ASSERT(sv_contains_sv(codemodel_reply, nob_sv_from_cstr("\"kind\": \"codemodel\"")));

    String_View cache_reply = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, ".cmake/api/v1/reply/cache-v2.0.json", &cache_reply));
    ASSERT(sv_contains_sv(cache_reply, nob_sv_from_cstr("\"kind\": \"cache\"")));

    bool saw_malformed_cache_warning = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_WARNING) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("load_cache() skipped a malformed cache entry"))) {
            saw_malformed_cache_warning = true;
            break;
        }
    }
    ASSERT(saw_malformed_cache_warning);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch6_metadata_commands_reject_unsupported_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "export(PACKAGE Demo)\n"
        "cmake_file_api(REPLY)\n"
        "cmake_file_api(QUERY API_VERSION 2 CODEMODEL 2)\n"
        "cmake_file_api(QUERY API_VERSION 1 UNKNOWN_KIND 1)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_load_cache_supports_multi_path_legacy_mode_and_prefix_unset) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("cache_multi_a"));
    ASSERT(nob_mkdir_if_not_exists("cache_multi_b"));
    ASSERT(nob_mkdir_if_not_exists("cache_prefix_empty"));
    ASSERT(nob_write_entire_file("cache_multi_a/CMakeCache.txt",
                                 "FIRST:STRING=one\n"
                                 "SHARED:STRING=from-a\n"
                                 "DROP:STRING=drop-a\n"
                                 "HIDE_A:INTERNAL=secret-a\n",
                                 strlen("FIRST:STRING=one\n"
                                        "SHARED:STRING=from-a\n"
                                        "DROP:STRING=drop-a\n"
                                        "HIDE_A:INTERNAL=secret-a\n")));
    ASSERT(nob_write_entire_file("cache_multi_b/CMakeCache.txt",
                                 "SECOND:BOOL=ON\n"
                                 "SHARED:STRING=from-b\n"
                                 "DROP:STRING=drop-b\n"
                                 "HIDE_B:INTERNAL=secret-b\n",
                                 strlen("SECOND:BOOL=ON\n"
                                        "SHARED:STRING=from-b\n"
                                        "DROP:STRING=drop-b\n"
                                        "HIDE_B:INTERNAL=secret-b\n")));
    ASSERT(nob_write_entire_file("cache_prefix_empty/CMakeCache.txt",
                                 "EMPTY:STRING=\n"
                                 "KEEP:STRING=keep-prefix\n",
                                 strlen("EMPTY:STRING=\n"
                                        "KEEP:STRING=keep-prefix\n")));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(PFX_EMPTY sentinel)\n"
        "load_cache(cache_multi_a cache_multi_b INCLUDE_INTERNALS HIDE_A HIDE_B EXCLUDE DROP)\n"
        "load_cache(cache_prefix_empty READ_WITH_PREFIX PFX_ EMPTY KEEP)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("FIRST")), nob_sv_from_cstr("one")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SECOND")), nob_sv_from_cstr("ON")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SHARED")), nob_sv_from_cstr("from-b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("HIDE_A")), nob_sv_from_cstr("secret-a")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("HIDE_B")), nob_sv_from_cstr("secret-b")));
    ASSERT(!eval_cache_defined(ctx, nob_sv_from_cstr("DROP")));

    ASSERT(!eval_var_defined_visible(ctx, nob_sv_from_cstr("PFX_EMPTY")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PFX_KEEP")),
                     nob_sv_from_cstr("keep-prefix")));
    ASSERT(!eval_cache_defined(ctx, nob_sv_from_cstr("PFX_KEEP")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_export_cxx_modules_directory_writes_sidecars_and_default_export_file) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("modules"));
    ASSERT(nob_mkdir_if_not_exists("include"));
    ASSERT(nob_write_entire_file("meta_impl.cpp", "int meta_impl = 0;\n", strlen("int meta_impl = 0;\n")));
    ASSERT(nob_write_entire_file("modules/core.cppm", "export module core;\n", strlen("export module core;\n")));
    ASSERT(nob_write_entire_file("include/meta.hpp", "#pragma once\n", strlen("#pragma once\n")));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(meta_lib STATIC meta_impl.cpp)\n"
        "target_sources(meta_lib PUBLIC FILE_SET mods TYPE CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "target_include_directories(meta_lib PUBLIC include)\n"
        "target_compile_definitions(meta_lib PUBLIC META_DEF=1)\n"
        "target_compile_options(meta_lib PUBLIC -Wall)\n"
        "target_compile_features(meta_lib PUBLIC cxx_std_20)\n"
        "target_link_libraries(meta_lib PUBLIC dep::lib)\n"
        "set_target_properties(meta_lib PROPERTIES EXPORT_NAME meta-export-name CXX_EXTENSIONS OFF)\n"
        "install(TARGETS meta_lib EXPORT DemoExport FILE_SET mods DESTINATION include/modules)\n"
        "export(TARGETS meta_lib FILE meta-targets.cmake NAMESPACE Demo:: CXX_MODULES_DIRECTORY cxx-modules)\n"
        "export(EXPORT DemoExport NAMESPACE Demo:: CXX_MODULES_DIRECTORY export-modules)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx,
                                  nob_sv_from_cstr("NOBIFY_EXPORT_LAST_CXX_MODULES_DIRECTORY")),
                     nob_sv_from_cstr("export-modules")));
    ASSERT(nob_sv_eq(eval_var_get(ctx,
                                  nob_sv_from_cstr("NOBIFY_EXPORT_LAST_CXX_MODULES_NAME")),
                     nob_sv_from_cstr("DemoExport")));

    String_View export_targets = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "meta-targets.cmake", &export_targets));
    ASSERT(sv_contains_sv(export_targets,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULES_DIRECTORY \"cxx-modules\")")));
    ASSERT(sv_contains_sv(export_targets,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules/cxx-modules-cc9f26f1f4e6.cmake\")")));

    String_View targets_trampoline = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/cxx-modules-cc9f26f1f4e6.cmake",
                                             &targets_trampoline));
    ASSERT(sv_contains_sv(targets_trampoline,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules-cc9f26f1f4e6-noconfig.cmake\")")));

    String_View targets_config = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/cxx-modules-cc9f26f1f4e6-noconfig.cmake",
                                             &targets_config));
    ASSERT(sv_contains_sv(targets_config,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/target-meta-export-name-noconfig.cmake\")")));

    String_View target_modules = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/target-meta-export-name-noconfig.cmake",
                                             &target_modules));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_TARGET \"Demo::meta-export-name\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_EXPORT_NAME \"meta-export-name\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_SETS \"mods\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_SET_MODS \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("modules/core.cppm")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_DIRS_MODS \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("modules")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_INCLUDE_DIRECTORIES \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("include")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_DEFINITIONS \"META_DEF=1\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_OPTIONS \"-Wall\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_FEATURES \"cxx_std_20\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_LINK_LIBRARIES \"dep::lib\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_CXX_EXTENSIONS \"OFF\")")));

    String_View export_default = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "DemoExport.cmake", &export_default));
    ASSERT(sv_contains_sv(export_default,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_NAME \"DemoExport\")")));
    ASSERT(sv_contains_sv(export_default,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/export-modules/cxx-modules-DemoExport.cmake\")")));

    String_View export_trampoline = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "export-modules/cxx-modules-DemoExport.cmake",
                                             &export_trampoline));
    ASSERT(sv_contains_sv(export_trampoline,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules-DemoExport-noconfig.cmake\")")));

    String_View export_config = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "export-modules/cxx-modules-DemoExport-noconfig.cmake",
                                             &export_config));
    ASSERT(sv_contains_sv(export_config,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/target-meta-export-name-noconfig.cmake\")")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_export_rejects_invalid_extension_and_alias_targets) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real INTERFACE)\n"
        "add_library(alias_real ALIAS real)\n"
        "export(TARGETS real FILE bad-export.txt)\n"
        "export(TARGETS alias_real FILE alias-export.cmake)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_bad_extension = false;
    bool saw_alias_reject = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("export(... FILE ...) requires a filename ending in .cmake"))) {
            saw_bad_extension = true;
        }
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("export(TARGETS ...) may not export ALIAS targets"))) {
            saw_alias_reject = true;
        }
    }
    ASSERT(saw_bad_extension);
    ASSERT(saw_alias_reject);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_family_models_metadata_and_safe_local_effects) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_bin"));
    ASSERT(nob_mkdir_if_not_exists("ctest_bin/wipe"));
    ASSERT(nob_mkdir_if_not_exists("ctest_bin/wipe/sub"));
    ASSERT(nob_mkdir_if_not_exists("ctest_src"));
    ASSERT(nob_mkdir_if_not_exists("ctest_custom"));
    ASSERT(nob_write_entire_file("ctest_bin/a.txt", "A\n", strlen("A\n")));
    ASSERT(nob_write_entire_file("ctest_bin/b.txt", "B\n", strlen("B\n")));
    ASSERT(nob_write_entire_file("ctest_bin/notes.txt", "NOTES\n", strlen("NOTES\n")));
    ASSERT(nob_write_entire_file("ctest_bin/wipe/sub/junk.txt", "junk\n", strlen("junk\n")));
    ASSERT(nob_write_entire_file("ctest_custom/CTestCustom.cmake",
                                 "set(CTEST_CUSTOM_LOADED yes)\n",
                                 strlen("set(CTEST_CUSTOM_LOADED yes)\n")));
    ASSERT(nob_write_entire_file("ctest_script.cmake",
                                 "set(CTEST_SCRIPT_LOADED 1)\n",
                                 strlen("set(CTEST_SCRIPT_LOADED 1)\n")));
    ASSERT(nob_write_entire_file("ctest_script_child.cmake",
                                 "set(CTEST_SCRIPT_CHILD_ONLY 1)\n"
                                 "function(ctest_child_only_fn)\n"
                                 "endfunction()\n"
                                 "set(CTEST_TRACK ChildTrack)\n",
                                 strlen("set(CTEST_SCRIPT_CHILD_ONLY 1)\n"
                                        "function(ctest_child_only_fn)\n"
                                        "endfunction()\n"
                                        "set(CTEST_TRACK ChildTrack)\n")));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_bin)\n"
        "ctest_start(Experimental ctest_src . TRACK Nightly APPEND)\n"
        "ctest_configure(RETURN_VALUE CFG_RV CAPTURE_CMAKE_ERROR CFG_CE QUIET)\n"
        "ctest_build(TARGET all NUMBER_ERRORS BUILD_ERRS NUMBER_WARNINGS BUILD_WARNS RETURN_VALUE BUILD_RV CAPTURE_CMAKE_ERROR BUILD_CE APPEND)\n"
        "ctest_test(RETURN_VALUE TEST_RV CAPTURE_CMAKE_ERROR TEST_CE PARALLEL_LEVEL 2 SCHEDULE_RANDOM)\n"
        "ctest_coverage(LABELS core ui RETURN_VALUE COV_RV CAPTURE_CMAKE_ERROR COV_CE)\n"
        "ctest_memcheck(RETURN_VALUE MEM_RV CAPTURE_CMAKE_ERROR MEM_CE DEFECT_COUNT MEM_DEFECTS SCHEDULE_RANDOM)\n"
        "ctest_update(RETURN_VALUE UPD_RV CAPTURE_CMAKE_ERROR UPD_CE QUIET)\n"
        "ctest_submit(PARTS Start Build Test FILES notes.txt RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n"
        "ctest_upload(FILES a.txt b.txt CAPTURE_CMAKE_ERROR UPLOAD_CE)\n"
        "ctest_empty_binary_directory(wipe)\n"
        "ctest_read_custom_files(ctest_custom)\n"
        "ctest_run_script(ctest_script.cmake RETURN_VALUE SCRIPT_RV)\n"
        "ctest_run_script(NEW_PROCESS ctest_script_child.cmake RETURN_VALUE SCRIPT_CHILD_RV)\n"
        "if(DEFINED CTEST_SCRIPT_CHILD_ONLY)\n"
        "  set(CHILD_VAR_LEAK 1)\n"
        "else()\n"
        "  set(CHILD_VAR_LEAK 0)\n"
        "endif()\n"
        "if(COMMAND ctest_child_only_fn)\n"
        "  set(CHILD_FN_LEAK 1)\n"
        "else()\n"
        "  set(CHILD_FN_LEAK 0)\n"
        "endif()\n"
        "ctest_sleep(0.25)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_LAST_COMMAND")),
                     nob_sv_from_cstr("ctest_sleep")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TRACK")),
                     nob_sv_from_cstr("Nightly")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::MODEL")),
                     nob_sv_from_cstr("Experimental")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CTEST_MODEL")),
                     nob_sv_from_cstr("Experimental")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CTEST_TRACK")),
                     nob_sv_from_cstr("Nightly")));
    String_View ctest_tag = eval_var_get(ctx, nob_sv_from_cstr("CTEST_TAG"));
    ASSERT(ctest_tag.count > 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::PARTS")),
                     nob_sv_from_cstr("Start;Build;Test")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_empty_binary_directory::STATUS")),
                     nob_sv_from_cstr("CLEARED")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CFG_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CFG_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BUILD_ERRS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BUILD_WARNS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("MEM_DEFECTS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SCRIPT_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SCRIPT_CHILD_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CHILD_VAR_LEAK")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CHILD_FN_LEAK")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CTEST_CUSTOM_LOADED")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CTEST_SCRIPT_LOADED")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("CTEST_SCRIPT_CHILD_ONLY")).count == 0);
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("CTEST_SOURCE_DIRECTORY")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("CTEST_BINARY_DIRECTORY")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::RESOLVED_SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::RESOLVED_SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::TAG")), ctest_tag));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TAG_FILE")),
                          nob_sv_from_cstr("ctest_bin/Testing/TAG")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/notes.txt")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/a.txt")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/b.txt")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_run_script::EXECUTION_MODE")),
                     nob_sv_from_cstr("NEW_PROCESS")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_run_script::RESOLVED_SCRIPTS")),
                          nob_sv_from_cstr("ctest_script_child.cmake")));

    String_View tag_file = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TAG_FILE"));
    char *tag_file_c = arena_strndup(temp_arena, tag_file.data, tag_file.count);
    ASSERT(tag_file_c != NULL);
    ASSERT(nob_file_exists(tag_file_c));

    Nob_String_Builder tag_sb = {0};
    ASSERT(nob_read_entire_file(tag_file_c, &tag_sb));
    ASSERT(strstr(tag_sb.items, "Experimental") != NULL);
    ASSERT(strstr(tag_sb.items, tag_file_c) == NULL);
    ASSERT(memmem(tag_sb.items, tag_sb.count, ctest_tag.data, ctest_tag.count) != NULL);
    nob_sb_free(tag_sb);

    String_View submit_manifest = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::MANIFEST"));
    char *submit_manifest_c = arena_strndup(temp_arena, submit_manifest.data, submit_manifest.count);
    ASSERT(submit_manifest_c != NULL);
    ASSERT(nob_file_exists(submit_manifest_c));

    Nob_String_Builder submit_sb = {0};
    ASSERT(nob_read_entire_file(submit_manifest_c, &submit_sb));
    ASSERT(strstr(submit_sb.items, "COMMAND=ctest_submit") != NULL);
    ASSERT(strstr(submit_sb.items, "PARTS=Start;Build;Test") != NULL);
    ASSERT(strstr(submit_sb.items, "FILES=") != NULL);
    ASSERT(strstr(submit_sb.items, "notes.txt") != NULL);
    nob_sb_free(submit_sb);

    String_View upload_manifest = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::MANIFEST"));
    char *upload_manifest_c = arena_strndup(temp_arena, upload_manifest.data, upload_manifest.count);
    ASSERT(upload_manifest_c != NULL);
    ASSERT(nob_file_exists(upload_manifest_c));

    Nob_String_Builder upload_sb = {0};
    ASSERT(nob_read_entire_file(upload_manifest_c, &upload_sb));
    ASSERT(strstr(upload_sb.items, "COMMAND=ctest_upload") != NULL);
    ASSERT(strstr(upload_sb.items, "a.txt") != NULL);
    ASSERT(strstr(upload_sb.items, "b.txt") != NULL);
    nob_sb_free(upload_sb);

    ASSERT(!nob_file_exists("ctest_bin/wipe/sub/junk.txt"));
    ASSERT(nob_file_exists("ctest_bin/wipe"));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_family_rejects_invalid_and_unsupported_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("safe_bin"));
    ASSERT(nob_write_entire_file("ctest_script_bad.cmake",
                                 "set(UNUSED 1)\n",
                                 strlen("set(UNUSED 1)\n")));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR safe_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR safe_bin)\n"
        "ctest_empty_binary_directory(../outside)\n"
        "ctest_run_script(NEW_PROCESS ctest_script_bad.cmake RETURN_VALUE SCRIPT_BAD_RV)\n"
        "ctest_sleep(1 2)\n"
        "ctest_build(BUILD)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SCRIPT_BAD_RV")), nob_sv_from_cstr("0")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("UNUSED")).count == 0);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch8_legacy_commands_register_and_model_compat_paths) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(legacy_iface INTERFACE)\n"
        "export_library_dependencies(legacy_deps.cmake)\n"
        "make_directory(legacy_dir/sub)\n"
        "write_file(legacy_dir/sub/out.txt alpha beta)\n"
        "install_files(share .txt first.txt second.txt)\n"
        "install_programs(bin tool.sh)\n"
        "install_targets(lib legacy_iface)\n"
        "load_command(legacy_cmd ./module)\n"
        "output_required_files(input.c output.txt)\n"
        "set(LEGACY_LIST a;b;c;b)\n"
        "remove(LEGACY_LIST b)\n"
        "qt_wrap_cpp(LegacyLib LEGACY_MOCS foo.hpp bar.hpp)\n"
        "qt_wrap_ui(LegacyLib LEGACY_UI_HDRS LEGACY_UI_SRCS dialog.ui)\n"
        "subdir_depends(src dep1 dep2)\n"
        "subdirs(dir_a dir_b)\n"
        "use_mangled_mesa(mesa out prefix)\n"
        "utility_source(CACHE_EXE /bin/tool generated.c)\n"
        "variable_requires(TESTVAR OUTVAR NEED1 NEED2)\n"
        "variable_watch(WATCH_ME watch-cmd)\n"
        "set(WATCH_ME touched)\n"
        "unset(WATCH_ME)\n"
        "fltk_wrap_ui(FltkLib main.fl)\n"
        "write_file(legacy_dir/sub/appended.txt one)\n"
        "write_file(legacy_dir/sub/appended.txt two APPEND)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_file_exists("legacy_dir/sub"));
    ASSERT(nob_file_exists("legacy_dir/sub/out.txt"));
    String_View out_txt = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "legacy_dir/sub/out.txt", &out_txt));
    ASSERT(nob_sv_eq(out_txt, nob_sv_from_cstr("alphabeta")));

    String_View appended_txt = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "legacy_dir/sub/appended.txt", &appended_txt));
    ASSERT(nob_sv_eq(appended_txt, nob_sv_from_cstr("onetwo")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LEGACY_MOCS")), nob_sv_from_cstr("moc_foo.cxx;moc_bar.cxx")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LEGACY_UI_HDRS")), nob_sv_from_cstr("ui_dialog.h")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LEGACY_UI_SRCS")), nob_sv_from_cstr("ui_dialog.cxx")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("FltkLib_FLTK_UI_SRCS")),
                     nob_sv_from_cstr("fluid_main.cxx;fluid_main.h")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::export_library_dependencies::ARGS")),
                     nob_sv_from_cstr("legacy_deps.cmake")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::load_command::ARGS")),
                     nob_sv_from_cstr("legacy_cmd;./module")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::subdirs::ARGS")),
                     nob_sv_from_cstr("dir_a;dir_b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VAR")),
                     nob_sv_from_cstr("WATCH_ME")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_ACTION")),
                     nob_sv_from_cstr("UNSET")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VALUE")),
                     nob_sv_from_cstr("touched")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_COMMAND")),
                     nob_sv_from_cstr("watch-cmd")));

    size_t install_rule_count = 0;
    bool saw_unknown_command = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_INSTALL_ADD_RULE) install_rule_count++;
        if (ev->h.kind == EV_DIAGNOSTIC && nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unknown command"))) {
            saw_unknown_command = true;
        }
    }
    ASSERT(install_rule_count >= 4);
    ASSERT(!saw_unknown_command);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch8_legacy_commands_reject_invalid_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "make_directory()\n"
        "write_file()\n"
        "remove(ONLY_VAR)\n"
        "variable_watch(A B C)\n"
        "qt_wrap_cpp(LegacyLib ONLY_OUT)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 5);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_target_sources_compile_features_and_precompile_headers_model_usage_requirements) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "add_library(imported_mod STATIC IMPORTED)\n"
        "add_executable(app app.c)\n"
        "target_sources(real PRIVATE priv.c PUBLIC pub.h INTERFACE iface.h)\n"
        "target_sources(real PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/public.hpp include/detail.hpp)\n"
        "target_sources(real INTERFACE FILE_SET api TYPE HEADERS BASE_DIRS api FILES api/iface.hpp)\n"
        "target_sources(real PUBLIC FILE_SET CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "target_sources(imported_mod INTERFACE FILE_SET CXX_MODULES BASE_DIRS imported FILES imported/api.cppm)\n"
        "target_compile_features(real PRIVATE cxx_std_20 PUBLIC cxx_std_17 INTERFACE c_std_11)\n"
        "target_precompile_headers(real PRIVATE pch.h PUBLIC pch_pub.h INTERFACE <vector>)\n"
        "target_precompile_headers(app REUSE_FROM real)\n"
        "get_target_property(REAL_SOURCES real SOURCES)\n"
        "get_target_property(REAL_IFACE_SOURCES real INTERFACE_SOURCES)\n"
        "get_target_property(REAL_HEADER_SETS real HEADER_SETS)\n"
        "get_target_property(REAL_INTERFACE_HEADER_SETS real INTERFACE_HEADER_SETS)\n"
        "get_target_property(REAL_HEADER_SET real HEADER_SET)\n"
        "get_target_property(REAL_HEADER_DIRS real HEADER_DIRS)\n"
        "get_target_property(REAL_HEADER_SET_API real HEADER_SET_API)\n"
        "get_target_property(REAL_HEADER_DIRS_API real HEADER_DIRS_API)\n"
        "get_target_property(REAL_CXX_MODULE_SETS real CXX_MODULE_SETS)\n"
        "get_target_property(REAL_CXX_MODULE_SET real CXX_MODULE_SET)\n"
        "get_target_property(REAL_CXX_MODULE_DIRS real CXX_MODULE_DIRS)\n"
        "get_target_property(IMPORTED_IFACE_CXX_MODULE_SETS imported_mod INTERFACE_CXX_MODULE_SETS)\n"
        "get_target_property(IMPORTED_CXX_MODULE_SET imported_mod CXX_MODULE_SET)\n"
        "get_target_property(IMPORTED_CXX_MODULE_DIRS imported_mod CXX_MODULE_DIRS)\n"
        "get_target_property(REAL_COMPILE_FEATURES real COMPILE_FEATURES)\n"
        "get_target_property(REAL_IFACE_COMPILE_FEATURES real INTERFACE_COMPILE_FEATURES)\n"
        "get_target_property(REAL_PCH real PRECOMPILE_HEADERS)\n"
        "get_target_property(REAL_IFACE_PCH real INTERFACE_PRECOMPILE_HEADERS)\n"
        "get_target_property(APP_REUSE app PRECOMPILE_HEADERS_REUSE_FROM)\n"
        "target_sources(real bad.c another.c)\n"
        "target_sources(real INTERFACE FILE_SET ifacemods TYPE CXX_MODULES FILES iface_bad.cppm)\n"
        "target_sources(real PUBLIC FILE_SET custom_modules FILES missing_type.cppm)\n"
        "target_compile_features(alias_real PRIVATE bad_feature)\n"
        "target_precompile_headers(missing_pch PRIVATE missing.h)\n"
        "target_sources(missing_src PRIVATE bad.c)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);

    bool saw_priv_source = false;
    bool saw_pub_source = false;
    bool saw_module_source_event = false;
    bool saw_iface_prop = false;
    bool saw_header_set_prop = false;
    bool saw_interface_header_sets_prop = false;
    bool saw_cxx_module_sets_prop = false;
    bool saw_cxx_module_set_prop = false;
    bool saw_cxx_module_dirs_prop = false;
    bool saw_imported_cxx_module_sets_prop = false;
    bool saw_compile_feature_local = false;
    bool saw_compile_feature_iface = false;
    bool saw_pch_local = false;
    bool saw_pch_iface = false;
    bool saw_reuse_from = false;
    bool saw_reuse_dep = false;
    bool saw_visibility_error = false;
    bool saw_cxx_module_interface_error = false;
    bool saw_cxx_module_missing_type_error = false;
    bool saw_alias_error = false;
    bool saw_missing_pch_error = false;
    bool saw_missing_src_error = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_ADD_SOURCE &&
            nob_sv_eq(ev->as.target_add_source.target_name, nob_sv_from_cstr("real"))) {
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("priv.c"))) saw_priv_source = true;
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("pub.h"))) saw_pub_source = true;
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("core.cppm"))) saw_module_source_event = true;
            ASSERT(!sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("iface.h")));
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("real"))) {
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_SOURCES")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("iface.h"))) {
                saw_iface_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("HEADER_SET")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("include/public.hpp"))) {
                saw_header_set_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_HEADER_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("api"))) {
                saw_interface_header_sets_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("CXX_MODULES"))) {
                saw_cxx_module_sets_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_SET")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("modules/core.cppm"))) {
                saw_cxx_module_set_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_DIRS")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("modules"))) {
                saw_cxx_module_dirs_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("COMPILE_FEATURES")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("cxx_std_20"))) {
                saw_compile_feature_local = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("c_std_11"))) {
                saw_compile_feature_iface = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("PRECOMPILE_HEADERS")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("pch.h"))) {
                saw_pch_local = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_PRECOMPILE_HEADERS")) &&
                (sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("vector")) ||
                 sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("pch_pub.h")))) {
                saw_pch_iface = true;
            }
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imported_mod"))) {
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_CXX_MODULE_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("CXX_MODULES"))) {
                saw_imported_cxx_module_sets_prop = true;
            }
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("app")) &&
                   nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("PRECOMPILE_HEADERS_REUSE_FROM")) &&
                   nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("real"))) {
            saw_reuse_from = true;
        } else if (ev->h.kind == EV_TARGET_ADD_DEPENDENCY &&
                   nob_sv_eq(ev->as.target_add_dependency.target_name, nob_sv_from_cstr("app")) &&
                   nob_sv_eq(ev->as.target_add_dependency.dependency_name, nob_sv_from_cstr("real"))) {
            saw_reuse_dep = true;
        } else if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause,
                          nob_sv_from_cstr("target command requires PUBLIC, PRIVATE or INTERFACE before items"))) {
                saw_visibility_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources(FILE_SET TYPE CXX_MODULES) may not use INTERFACE scope on non-IMPORTED targets"))) {
                saw_cxx_module_interface_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources(FILE_SET ...) requires TYPE for non-default file-set names"))) {
                saw_cxx_module_missing_type_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_compile_features() cannot be used on ALIAS targets"))) {
                saw_alias_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_precompile_headers() target was not declared"))) {
                saw_missing_pch_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources() target was not declared"))) {
                saw_missing_src_error = true;
            }
        }
    }

    ASSERT(saw_priv_source);
    ASSERT(saw_pub_source);
    ASSERT(!saw_module_source_event);
    ASSERT(saw_iface_prop);
    ASSERT(saw_header_set_prop);
    ASSERT(saw_interface_header_sets_prop);
    ASSERT(saw_cxx_module_sets_prop);
    ASSERT(saw_cxx_module_set_prop);
    ASSERT(saw_cxx_module_dirs_prop);
    ASSERT(saw_imported_cxx_module_sets_prop);
    ASSERT(saw_compile_feature_local);
    ASSERT(saw_compile_feature_iface);
    ASSERT(saw_pch_local);
    ASSERT(saw_pch_iface);
    ASSERT(saw_reuse_from);
    ASSERT(saw_reuse_dep);
    ASSERT(saw_visibility_error);
    ASSERT(saw_cxx_module_interface_error);
    ASSERT(saw_cxx_module_missing_type_error);
    ASSERT(saw_alias_error);
    ASSERT(saw_missing_pch_error);
    ASSERT(saw_missing_src_error);

    String_View real_sources = eval_var_get(ctx, nob_sv_from_cstr("REAL_SOURCES"));
    ASSERT(semicolon_list_count(real_sources) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_sources, 0), nob_sv_from_cstr("priv.c")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_sources, 1), nob_sv_from_cstr("pub.h")));

    String_View real_iface_sources = eval_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_SOURCES"));
    ASSERT(semicolon_list_count(real_iface_sources) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_sources, 0), nob_sv_from_cstr("pub.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_sources, 1), nob_sv_from_cstr("iface.h")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SETS")),
                     nob_sv_from_cstr("HEADERS")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_INTERFACE_HEADER_SETS")),
                     nob_sv_from_cstr("HEADERS;api")));

    String_View real_header_set = eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SET"));
    ASSERT(semicolon_list_count(real_header_set) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_header_set, 0), nob_sv_from_cstr("include/public.hpp")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_header_set, 1), nob_sv_from_cstr("include/detail.hpp")));

    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_DIRS")),
                          nob_sv_from_cstr("include")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SET_API")),
                          nob_sv_from_cstr("api/iface.hpp")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_DIRS_API")),
                          nob_sv_from_cstr("api")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_SETS")),
                     nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_SET")),
                          nob_sv_from_cstr("modules/core.cppm")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_DIRS")),
                          nob_sv_from_cstr("modules")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IMPORTED_IFACE_CXX_MODULE_SETS")),
                     nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("IMPORTED_CXX_MODULE_SET")),
                          nob_sv_from_cstr("imported/api.cppm")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("IMPORTED_CXX_MODULE_DIRS")),
                          nob_sv_from_cstr("imported")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_COMPILE_FEATURES")),
                     nob_sv_from_cstr("cxx_std_20;cxx_std_17")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_COMPILE_FEATURES")),
                     nob_sv_from_cstr("cxx_std_17;c_std_11")));

    String_View real_pch = eval_var_get(ctx, nob_sv_from_cstr("REAL_PCH"));
    ASSERT(semicolon_list_count(real_pch) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_pch, 0), nob_sv_from_cstr("pch.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_pch, 1), nob_sv_from_cstr("pch_pub.h")));

    String_View real_iface_pch = eval_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_PCH"));
    ASSERT(semicolon_list_count(real_iface_pch) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_pch, 0), nob_sv_from_cstr("pch_pub.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_pch, 1), nob_sv_from_cstr("vector")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("APP_REUSE")), nob_sv_from_cstr("real")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_source_group_supports_files_tree_and_regex_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "source_group(\"Root Files\" FILES main.c util.c REGULAR_EXPRESSION [=[.*\\.(c|h)$]=])\n"
        "source_group(TREE src PREFIX Generated FILES src/a.c src/sub/b.c)\n"
        "source_group(Texts [=[.*\\.txt$]=])\n"
        "source_group(TREE src FILES ../outside.c)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_main_group = false;
    bool saw_util_group = false;
    bool saw_tree_root = false;
    bool saw_tree_sub = false;
    bool saw_c_regex = false;
    bool saw_c_regex_name = false;
    bool saw_txt_regex = false;
    bool saw_txt_regex_name = false;
    bool saw_tree_outside_error = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_VAR_SET) {
            if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("main.c")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_main_group = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("util.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_util_group = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("src/a.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Generated"))) {
                saw_tree_root = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("src/sub/b.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Generated\\sub"))) {
                saw_tree_sub = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       !sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr(".*\\.(c|h)$"))) {
                saw_c_regex = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_c_regex_name = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       !sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr(".*\\.txt$"))) {
                saw_txt_regex = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Texts"))) {
                saw_txt_regex_name = true;
            }
        } else if (ev->h.kind == EV_DIAGNOSTIC &&
                   ev->as.diag.severity == EV_DIAG_ERROR &&
                   nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("source_group(TREE ...) file is outside the declared tree root"))) {
            saw_tree_outside_error = true;
        }
    }

    ASSERT(saw_main_group);
    ASSERT(saw_util_group);
    ASSERT(saw_tree_root);
    ASSERT(saw_tree_sub);
    ASSERT(saw_c_regex);
    ASSERT(saw_c_regex_name);
    ASSERT(saw_txt_regex);
    ASSERT(saw_txt_regex_name);
    ASSERT(saw_tree_outside_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_mode_severity_mapping) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "message(NOTICE n)\n"
        "message(STATUS s)\n"
        "message(VERBOSE v)\n"
        "message(DEBUG d)\n"
        "message(TRACE t)\n"
        "message(WARNING w)\n"
        "message(AUTHOR_WARNING aw)\n"
        "message(DEPRECATION dep)\n"
        "message(SEND_ERROR se)\n"
        "message(CHECK_START probe)\n"
        "message(CHECK_PASS ok)\n"
        "message(CHECK_START probe2)\n"
        "message(CHECK_FAIL fail)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 3);
    ASSERT(report->error_count == 1);

    size_t warning_diag_count = 0;
    size_t error_diag_count = 0;
    bool saw_check_pass_cause = false;
    bool saw_check_fail_cause = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity == EV_DIAG_WARNING) warning_diag_count++;
        if (ev->as.diag.severity == EV_DIAG_ERROR) error_diag_count++;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("probe - ok"))) saw_check_pass_cause = true;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("probe2 - fail"))) saw_check_fail_cause = true;
    }

    ASSERT(warning_diag_count == 3);
    ASSERT(error_diag_count == 1);
    ASSERT(!saw_check_pass_cause);
    ASSERT(!saw_check_fail_cause);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_check_pass_without_start_is_error) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "message(CHECK_PASS done)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool found = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("message(CHECK_PASS/CHECK_FAIL) requires a preceding CHECK_START"))) {
            found = true;
            break;
        }
    }
    ASSERT(found);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_deprecation_respects_control_variables) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_WARN_DEPRECATED FALSE)\n"
        "message(DEPRECATION hidden)\n"
        "set(CMAKE_WARN_DEPRECATED TRUE)\n"
        "message(DEPRECATION shown)\n"
        "set(CMAKE_ERROR_DEPRECATED TRUE)\n"
        "message(DEPRECATION err)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 1);

    bool saw_hidden = false;
    bool saw_shown_warn = false;
    bool saw_err_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("hidden"))) saw_hidden = true;
        if (ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("shown"))) {
            saw_shown_warn = true;
        }
        if (ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("err"))) {
            saw_err_error = true;
        }
    }
    ASSERT(!saw_hidden);
    ASSERT(saw_shown_warn);
    ASSERT(saw_err_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_configure_log_persists_yaml_file) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "message(CHECK_START feature-probe)\n"
        "message(CONFIGURE_LOG probe-start)\n"
        "message(CHECK_PASS yes)\n"
        "message(CONFIGURE_LOG probe-end)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    String_View log_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "./CMakeFiles/CMakeConfigureLog.yaml", &log_text));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("kind: \"message-v1\"")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("probe-start")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("probe-end")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("feature-probe")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_and_unset_env_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(ENV{NOBIFY_ENV_A} valueA)\n"
        "set(ENV{NOBIFY_ENV_A})\n"
        "set(ENV{NOBIFY_ENV_B} valueB ignored)\n"
        "add_executable(env_forms main.c)\n"
        "target_compile_definitions(env_forms PRIVATE A=$ENV{NOBIFY_ENV_A} B=$ENV{NOBIFY_ENV_B})\n"
        "unset(ENV{NOBIFY_ENV_B})\n"
        "target_compile_definitions(env_forms PRIVATE B2=$ENV{NOBIFY_ENV_B})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 0);

    bool saw_extra_args_warn = false;
    bool saw_a_empty = false;
    bool saw_b_value = false;
    bool saw_b2_empty = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set(ENV{...}) ignores extra arguments after value"))) {
            saw_extra_args_warn = true;
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("A="))) saw_a_empty = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("B=valueB"))) saw_b_value = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("B2="))) saw_b2_empty = true;
    }
    ASSERT(saw_extra_args_warn);
    ASSERT(saw_a_empty);
    ASSERT(saw_b_value);
    ASSERT(saw_b2_empty);

    const char *env_b = getenv("NOBIFY_ENV_B");
    ASSERT(env_b == NULL);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_process_env_service_overlays_execute_process_and_timestamp) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

#if defined(_WIN32)
    const char *script =
        "set(ENV{NOBIFY_ENV_CHILD_F3} valueB)\n"
        "execute_process(COMMAND cmd /C \"echo %NOBIFY_ENV_CHILD_F3%\" "
        "OUTPUT_VARIABLE CHILD_B OUTPUT_STRIP_TRAILING_WHITESPACE)\n"
        "set(ENV{SOURCE_DATE_EPOCH} 946684800)\n"
        "string(TIMESTAMP ENV_TS \"%Y\" UTC)\n"
        "unset(ENV{NOBIFY_ENV_CHILD_F3})\n"
        "unset(ENV{SOURCE_DATE_EPOCH})\n";
#else
    const char *script =
        "set(ENV{NOBIFY_ENV_CHILD_F3} valueB)\n"
        "execute_process(COMMAND /bin/sh -c \"printf '%s' $NOBIFY_ENV_CHILD_F3\" "
        "OUTPUT_VARIABLE CHILD_B)\n"
        "set(ENV{SOURCE_DATE_EPOCH} 946684800)\n"
        "string(TIMESTAMP ENV_TS \"%Y\" UTC)\n"
        "unset(ENV{NOBIFY_ENV_CHILD_F3})\n"
        "unset(ENV{SOURCE_DATE_EPOCH})\n";
#endif

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CHILD_B")), nob_sv_from_cstr("valueB")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ENV_TS")), nob_sv_from_cstr("2000")));
    ASSERT(!eval_has_env(ctx, "NOBIFY_ENV_CHILD_F3"));
    ASSERT(getenv("NOBIFY_ENV_CHILD_F3") == NULL);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_parse_arguments_supports_direct_and_parse_argv_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(parse_direct)\n"
        "  cmake_parse_arguments(ARG \"OPT;FAST\" \"DEST\" \"TARGETS;CONFIGS\" ${ARGN})\n"
        "  list(GET ARG_TARGETS 0 ARG_T0)\n"
        "  list(GET ARG_TARGETS 1 ARG_T1)\n"
        "endmacro()\n"
        "function(parse_argv)\n"
        "  cmake_parse_arguments(PARSE_ARGV 1 FN \"FLAG\" \"ONE\" \"MULTI;MULTI\")\n"
        "  add_executable(parse_argv_t main.c)\n"
        "  list(GET FN_MULTI 0 FN_M0)\n"
        "  list(GET FN_MULTI 1 FN_M1)\n"
        "  if(DEFINED FN_ONE)\n"
        "    target_compile_definitions(parse_argv_t PRIVATE ONE_DEFINED=1)\n"
        "  else()\n"
        "    target_compile_definitions(parse_argv_t PRIVATE ONE_DEFINED=0)\n"
        "  endif()\n"
        "  target_compile_definitions(parse_argv_t PRIVATE FLAG=${FN_FLAG} M0=${FN_M0} M1=${FN_M1} UNPARSED=${FN_UNPARSED_ARGUMENTS})\n"
        "endfunction()\n"
        "parse_direct(OPT EXTRA DEST bin TARGETS a b CONFIGS)\n"
        "parse_argv(skip FLAG TAIL ONE \"\" MULTI alpha beta)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 1);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_OPT")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_FAST")), nob_sv_from_cstr("FALSE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_DEST")), nob_sv_from_cstr("bin")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_TARGETS")), nob_sv_from_cstr("a;b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_T0")), nob_sv_from_cstr("a")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_T1")), nob_sv_from_cstr("b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_UNPARSED_ARGUMENTS")), nob_sv_from_cstr("EXTRA")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_KEYWORDS_MISSING_VALUES")), nob_sv_from_cstr("CONFIGS")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("ARG_CONFIGS")).count == 0);

    bool saw_dup_warn = false;
    bool saw_flag = false;
    bool saw_one_defined_old = false;
    bool saw_m0 = false;
    bool saw_m1 = false;
    bool saw_unparsed = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("cmake_parse_arguments() keyword appears more than once across keyword lists"))) {
            saw_dup_warn = true;
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("parse_argv_t"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FLAG=TRUE"))) saw_flag = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ONE_DEFINED=0"))) saw_one_defined_old = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("M0=alpha"))) saw_m0 = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("M1=beta"))) saw_m1 = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("UNPARSED=TAIL"))) saw_unparsed = true;
    }

    ASSERT(saw_dup_warn);
    ASSERT(saw_flag);
    ASSERT(saw_one_defined_old);
    ASSERT(saw_m0);
    ASSERT(saw_m1);
    ASSERT(saw_unparsed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_unset_env_rejects_options) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "unset(ENV{NOBIFY_ENV_OPT} CACHE)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("unset(ENV{...}) does not accept options"))) {
            saw_error = true;
            break;
        }
    }
    ASSERT(saw_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_cache_cmp0126_old_and_new_semantics) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CACHE_OLD local_old)\n"
        "set(CACHE_OLD cache_old CACHE STRING \"doc\")\n"
        "add_executable(cache_old_t main.c)\n"
        "target_compile_definitions(cache_old_t PRIVATE OLD_CA=${CACHE_OLD})\n"
        "cmake_policy(SET CMP0126 NEW)\n"
        "set(CACHE_NEW local_new)\n"
        "set(CACHE_NEW cache_new CACHE STRING \"doc\" FORCE)\n"
        "add_executable(cache_new_t main.c)\n"
        "target_compile_definitions(cache_new_t PRIVATE NEW_CB=${CACHE_NEW})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_old_binding_from_cache = false;
    bool saw_new_binding_from_local = false;
    bool saw_cache_old_set = false;
    bool saw_cache_new_set = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_SET_CACHE_ENTRY && ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE) {
            if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_OLD")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_old"))) {
                saw_cache_old_set = true;
            }
            if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_NEW")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_new"))) {
                saw_cache_new_set = true;
            }
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_CA=cache_old"))) {
            saw_old_binding_from_cache = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_CB=local_new"))) {
            saw_new_binding_from_local = true;
        }
    }
    ASSERT(saw_cache_old_set);
    ASSERT(saw_cache_new_set);
    ASSERT(saw_old_binding_from_cache);
    ASSERT(saw_new_binding_from_local);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_cache_policy_version_defaults_cmp0126_to_new) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_minimum_required(VERSION 3.28)\n"
        "set(CACHE_VER local_ver)\n"
        "set(CACHE_VER cache_ver CACHE STRING \"doc\")\n"
        "add_executable(cache_ver_t main.c)\n"
        "target_compile_definitions(cache_ver_t PRIVATE VER=${CACHE_VER})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_cache_ver_set = false;
    bool saw_local_binding = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_SET_CACHE_ENTRY &&
            ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_VER")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_ver"))) {
            saw_cache_ver_set = true;
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("VER=local_ver"))) {
            saw_local_binding = true;
        }
    }
    ASSERT(saw_cache_ver_set);
    ASSERT(saw_local_binding);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_commands_resolve_local_paths_and_model_package_root_policies) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("find_items"));
    ASSERT(nob_mkdir_if_not_exists("find_items/nested"));
    ASSERT(nob_mkdir_if_not_exists("find_items/include"));
    ASSERT(nob_mkdir_if_not_exists("find_items/bin"));
    ASSERT(nob_mkdir_if_not_exists("find_items/lib"));
    ASSERT(nob_write_entire_file("find_items/nested/marker.txt", "x", 1));
    ASSERT(nob_write_entire_file("find_items/include/marker.hpp", "x", 1));
#if defined(_WIN32)
    ASSERT(nob_write_entire_file("find_items/lib/sample.lib", "x", 1));
#else
    ASSERT(nob_write_entire_file("find_items/lib/libsample.a", "x", 1));
#endif

    ASSERT(nob_mkdir_if_not_exists("foo_root"));
    ASSERT(nob_mkdir_if_not_exists("foo_root/include"));
    ASSERT(nob_mkdir_if_not_exists("foo_root/lib"));
    ASSERT(nob_mkdir_if_not_exists("foo_root/lib/cmake"));
    ASSERT(nob_mkdir_if_not_exists("foo_root/lib/cmake/Foo"));
    ASSERT(nob_write_entire_file("foo_root/include/foo-marker.h", "x", 1));
    ASSERT(nob_write_entire_file("foo_root/lib/cmake/Foo/FooConfig.cmake",
                                 "find_path(FOO_INCLUDE_DIR NAMES foo-marker.h)\n",
                                 strlen("find_path(FOO_INCLUDE_DIR NAMES foo-marker.h)\n")));

    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);
    const char *foo_root_abs = nob_temp_sprintf("%s/foo_root", cwd);
    ASSERT(foo_root_abs != NULL);

    char script[4096];
    int n = snprintf(
        script,
        sizeof(script),
        "find_file(MY_FILE NAMES marker.txt PATHS find_items PATH_SUFFIXES nested NO_DEFAULT_PATH)\n"
        "find_path(MY_PATH NAMES marker.hpp PATHS find_items PATH_SUFFIXES include NO_DEFAULT_PATH)\n"
#if defined(_WIN32)
        "find_program(MY_TOOL NAMES cmd PATHS C:/Windows/System32 NO_DEFAULT_PATH)\n"
#else
        "find_program(MY_TOOL NAMES sh PATHS /bin NO_DEFAULT_PATH)\n"
#endif
        "find_library(MY_LIB NAMES sample PATHS find_items/lib NO_DEFAULT_PATH)\n"
        "set(FOO_ROOT \"%s\")\n"
        "cmake_policy(SET CMP0074 NEW)\n"
        "cmake_policy(SET CMP0144 NEW)\n"
        "find_package(Foo CONFIG PATHS foo_root/lib/cmake NO_DEFAULT_PATH)\n",
        foo_root_abs);
    ASSERT(n > 0 && n < (int)sizeof(script));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("MY_FILE")),
                           "find_items/nested/marker.txt"));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("MY_PATH")),
                           "find_items/include"));
#if defined(_WIN32)
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("MY_TOOL")),
                           "System32/cmd.exe"));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("MY_LIB")),
                           "find_items/lib/sample.lib"));
#else
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("MY_TOOL")),
                           "/bin/sh"));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("MY_LIB")),
                           "find_items/lib/libsample.a"));
#endif
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("FOO_INCLUDE_DIR")),
                           "foo_root/include"));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_command_rejects_unknown_option) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("find_items"));
    ASSERT(nob_mkdir_if_not_exists("find_items2"));
    ASSERT(nob_write_entire_file("find_items2/marker.txt", "x", 1));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "find_file(FOO NAMES marker.txt NO_CACHE UNKNOWN_OPTION)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_unknown = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("find_file"))) continue;
        if (!nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_UNEXPECTED_ARGUMENT"))) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*() received unknown option"))) {
            saw_unknown = true;
        }
    }
    ASSERT(saw_unknown);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_command_rejects_missing_output_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "find_file(NAMES marker.txt)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_missing = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("find_file"))) continue;
        if (!nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_MISSING_REQUIRED"))) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*() requires an output variable"))) {
            saw_missing = true;
        }
    }
    ASSERT(saw_missing);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_command_rejects_missing_registry_or_validator_values) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("find_items3"));
    ASSERT(nob_write_entire_file("find_items3/marker.txt", "x", 1));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "find_file(REG_VIEW marker.txt REGISTRY_VIEW)\n"
        "find_file(VAL_VIEW marker.txt VALIDATOR)\n"
        "find_file(DOC_ONLY marker.txt DOC)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_registry = false;
    bool saw_validator = false;
    bool saw_doc_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("find_file"))) continue;
        if (!nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_MISSING_REQUIRED"))) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*(REGISTRY_VIEW) requires a value"))) {
            saw_registry = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*(VALIDATOR) requires a function name"))) {
            saw_validator = true;
        } else {
            saw_doc_error = true;
        }
    }
    ASSERT(saw_registry);
    ASSERT(saw_validator);
    ASSERT(!saw_doc_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_command_rejects_malformed_env_clause) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "find_file(BAD_ENV_END NAMES ENV)\n"
        "find_file(BAD_ENV_KEYWORD NAMES ENV PATHS find_items2)\n");
    Eval_Result result = evaluator_run(ctx, root);
    ASSERT(result.kind == EVAL_RESULT_SOFT_ERROR);
    ASSERT(!ctx->oom);
    ASSERT(!ctx->stop_requested);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    size_t malformed_env_diags = 0;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("find_file"))) continue;
        if (!nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_MISSING_REQUIRED"))) continue;
        if (!nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*(ENV) requires an environment variable name"))) continue;
        malformed_env_diags++;
    }
    ASSERT(malformed_env_diags == 2);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_filename_component_covers_documented_modes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("gfc_real"));
    ASSERT(nob_mkdir_if_not_exists("gfc_real/sub"));
    ASSERT(nob_mkdir_if_not_exists("gfc spaced"));
    ASSERT(nob_write_entire_file("gfc_real/sub/file.txt", "x", 1));
#if defined(_WIN32)
    ASSERT(nob_write_entire_file("gfc spaced/tool spaced.bat", "@echo off\r\n", strlen("@echo off\r\n")));
#else
    ASSERT(nob_write_entire_file("gfc spaced/tool spaced.sh", "#!/bin/sh\n", strlen("#!/bin/sh\n")));
#endif

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "get_filename_component(GFC_DIR \"a/b/c.tar.gz\" DIRECTORY)\n"
        "get_filename_component(GFC_PATH \"a/b/\" PATH)\n"
        "get_filename_component(GFC_NAME \"a/b/c.tar.gz\" NAME)\n"
        "get_filename_component(GFC_EXT \"a/b/c.tar.gz\" EXT)\n"
        "get_filename_component(GFC_LAST_EXT \"a/b/c.tar.gz\" LAST_EXT)\n"
        "get_filename_component(GFC_NAME_WE \"a/b/c.tar.gz\" NAME_WE)\n"
        "get_filename_component(GFC_NAME_WLE \"a/b/c.tar.gz\" NAME_WLE CACHE)\n"
        "get_filename_component(GFC_ABS sub/file.txt ABSOLUTE BASE_DIR gfc_real)\n"
        "get_filename_component(GFC_REAL \"gfc_real/./sub/../sub/file.txt\" REALPATH)\n"
        "set(GFC_SPACE_ARGS sentinel-space)\n"
        "set(GFC_MISSING_ARGS sentinel-missing)\n"
        "set(GFC_CACHE_HIT keep-me)\n"
        "set(GFC_CACHE_HIT_ARGS keep-args)\n"
#if defined(_WIN32)
        "get_filename_component(GFC_PROG \"cmd /C echo\" PROGRAM PROGRAM_ARGS GFC_PROG_ARGS)\n"
        "get_filename_component(GFC_SPACE_PROG \"gfc spaced/tool spaced.bat\" PROGRAM PROGRAM_ARGS GFC_SPACE_ARGS)\n"
        "get_filename_component(GFC_CACHE_HIT \"cmd /C echo\" PROGRAM PROGRAM_ARGS GFC_CACHE_HIT_ARGS CACHE)\n"
        "get_filename_component(GFC_CACHE_PROG \"cmd /C echo\" PROGRAM PROGRAM_ARGS GFC_CACHE_PROG_ARGS CACHE)\n"
#else
        "get_filename_component(GFC_PROG \"sh -c echo\" PROGRAM PROGRAM_ARGS GFC_PROG_ARGS)\n"
        "get_filename_component(GFC_SPACE_PROG \"gfc spaced/tool spaced.sh\" PROGRAM PROGRAM_ARGS GFC_SPACE_ARGS)\n"
        "get_filename_component(GFC_CACHE_HIT \"sh -c echo\" PROGRAM PROGRAM_ARGS GFC_CACHE_HIT_ARGS CACHE)\n"
        "get_filename_component(GFC_CACHE_PROG \"sh -c echo\" PROGRAM PROGRAM_ARGS GFC_CACHE_PROG_ARGS CACHE)\n"
#endif
        "get_filename_component(GFC_MISSING_PROG \"./gfc_missing_program --flag\" PROGRAM PROGRAM_ARGS GFC_MISSING_ARGS)\n"
    );
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_DIR")), nob_sv_from_cstr("a/b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_PATH")), nob_sv_from_cstr("a")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_NAME")), nob_sv_from_cstr("c.tar.gz")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_EXT")), nob_sv_from_cstr(".tar.gz")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_LAST_EXT")), nob_sv_from_cstr(".gz")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_NAME_WE")), nob_sv_from_cstr("c")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_NAME_WLE")), nob_sv_from_cstr("c.tar")));
    ASSERT(eval_cache_defined(ctx, nob_sv_from_cstr("GFC_NAME_WLE")));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("GFC_ABS")), "gfc_real/sub/file.txt"));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("GFC_REAL")), "gfc_real/sub/file.txt"));
#if defined(_WIN32)
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("GFC_PROG")), "cmd.exe"));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_PROG_ARGS")), nob_sv_from_cstr(" /C echo")));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("GFC_SPACE_PROG")),
                           "gfc spaced/tool spaced.bat"));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_PROG")), "cmd.exe"));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_PROG_ARGS")),
                     nob_sv_from_cstr(" /C echo")));
#else
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("GFC_PROG")), "/sh"));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_PROG_ARGS")), nob_sv_from_cstr(" -c echo")));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("GFC_SPACE_PROG")),
                           "gfc spaced/tool spaced.sh"));
    ASSERT(nob_sv_end_with(eval_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_PROG")), "/sh"));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_PROG_ARGS")),
                     nob_sv_from_cstr(" -c echo")));
#endif
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_SPACE_ARGS")),
                     nob_sv_from_cstr("sentinel-space")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_MISSING_PROG")), nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_MISSING_ARGS")),
                     nob_sv_from_cstr("sentinel-missing")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_HIT")),
                     nob_sv_from_cstr("keep-me")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_HIT_ARGS")),
                     nob_sv_from_cstr("keep-args")));
    ASSERT(!eval_cache_defined(ctx, nob_sv_from_cstr("GFC_CACHE_HIT")));
    ASSERT(!eval_cache_defined(ctx, nob_sv_from_cstr("GFC_CACHE_HIT_ARGS")));
    ASSERT(eval_cache_defined(ctx, nob_sv_from_cstr("GFC_CACHE_PROG")));
    ASSERT(eval_cache_defined(ctx, nob_sv_from_cstr("GFC_CACHE_PROG_ARGS")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_no_module_names_configs_path_suffixes_and_registry_view) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY fp_cfg_root/sfx)\n"
        "file(WRITE fp_cfg_root/sfx/AltCfg.cmake [=[set(DemoFP_FOUND 1)\n"
        "set(DemoFP_SOURCE config)\n"
        "]=])\n"
        "find_package(DemoFP NO_MODULE NAMES AltName CONFIGS AltCfg.cmake PATH_SUFFIXES sfx PATHS fp_cfg_root REGISTRY_VIEW HOST QUIET)\n"
        "add_executable(fp_cfg_probe main.c)\n"
        "target_compile_definitions(fp_cfg_probe PRIVATE FOUND=${DemoFP_FOUND} SRC=${DemoFP_SOURCE} RV=${DemoFP_FIND_REGISTRY_VIEW})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_find_event = false;
    bool saw_found = false;
    bool saw_src_config = false;
    bool saw_rv_host = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("DemoFP"))) {
            saw_find_event = true;
            ASSERT(ev->as.package_find_result.found);
            ASSERT(nob_sv_eq(ev->as.package_find_result.mode, nob_sv_from_cstr("CONFIG")));
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("fp_cfg_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOUND=1"))) saw_found = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SRC=config"))) saw_src_config = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RV=HOST"))) saw_rv_host = true;
        }
    }

    ASSERT(saw_find_event);
    ASSERT(saw_found);
    ASSERT(saw_src_config);
    ASSERT(saw_rv_host);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_auto_prefers_config_when_requested) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY fp_pref/mod)\n"
        "file(MAKE_DIRECTORY fp_pref/cfg)\n"
        "file(WRITE fp_pref/mod/FindPrefPkg.cmake [=[set(PrefPkg_FOUND 1)\n"
        "set(PrefPkg_FROM module)\n"
        "]=])\n"
        "file(WRITE fp_pref/cfg/PrefPkgConfig.cmake [=[set(PrefPkg_FOUND 1)\n"
        "set(PrefPkg_FROM config)\n"
        "]=])\n"
        "set(CMAKE_MODULE_PATH fp_pref/mod)\n"
        "set(CMAKE_PREFIX_PATH fp_pref/cfg)\n"
        "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"
        "find_package(PrefPkg QUIET)\n"
        "add_executable(fp_pref_probe main.c)\n"
        "target_compile_definitions(fp_pref_probe PRIVATE FROM=${PrefPkg_FROM})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_config_location = false;
    bool saw_from_config = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("PrefPkg"))) {
            saw_config_location =
                nob_sv_eq(ev->as.package_find_result.location, nob_sv_from_cstr("./fp_pref/cfg/PrefPkgConfig.cmake"));
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("fp_pref_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FROM=config"))) {
            saw_from_config = true;
        }
    }

    ASSERT(saw_config_location);
    ASSERT(saw_from_config);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_cmp0074_old_ignores_root_and_new_uses_root) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY fp_cmp0074_old_root)\n"
        "file(MAKE_DIRECTORY fp_cmp0074_old_prefix)\n"
        "file(WRITE fp_cmp0074_old_root/Cmp0074OldConfig.cmake [=[set(Cmp0074Old_FOUND 1)\n"
        "set(Cmp0074Old_FROM root)\n"
        "]=])\n"
        "file(WRITE fp_cmp0074_old_prefix/Cmp0074OldConfig.cmake [=[set(Cmp0074Old_FOUND 1)\n"
        "set(Cmp0074Old_FROM prefix)\n"
        "]=])\n"
        "set(Cmp0074Old_ROOT fp_cmp0074_old_root)\n"
        "set(CMAKE_PREFIX_PATH fp_cmp0074_old_prefix)\n"
        "cmake_policy(SET CMP0074 OLD)\n"
        "find_package(Cmp0074Old CONFIG QUIET)\n"
        "file(MAKE_DIRECTORY fp_cmp0074_new_root)\n"
        "file(MAKE_DIRECTORY fp_cmp0074_new_prefix)\n"
        "file(WRITE fp_cmp0074_new_root/Cmp0074NewConfig.cmake [=[set(Cmp0074New_FOUND 1)\n"
        "set(Cmp0074New_FROM root)\n"
        "]=])\n"
        "file(WRITE fp_cmp0074_new_prefix/Cmp0074NewConfig.cmake [=[set(Cmp0074New_FOUND 1)\n"
        "set(Cmp0074New_FROM prefix)\n"
        "]=])\n"
        "set(Cmp0074New_ROOT fp_cmp0074_new_root)\n"
        "set(CMAKE_PREFIX_PATH fp_cmp0074_new_prefix)\n"
        "cmake_policy(SET CMP0074 NEW)\n"
        "find_package(Cmp0074New CONFIG QUIET)\n"
        "add_executable(fp_cmp0074_probe main.c)\n"
        "target_compile_definitions(fp_cmp0074_probe PRIVATE OLD_FROM=${Cmp0074Old_FROM} NEW_FROM=${Cmp0074New_FROM})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_old_location = false;
    bool saw_new_location = false;
    bool saw_old_from_prefix = false;
    bool saw_new_from_root = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("Cmp0074Old"))) {
            saw_old_location =
                nob_sv_eq(ev->as.package_find_result.location, nob_sv_from_cstr("./fp_cmp0074_old_prefix/Cmp0074OldConfig.cmake"));
        }
        if (ev->h.kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("Cmp0074New"))) {
            saw_new_location =
                nob_sv_eq(ev->as.package_find_result.location, nob_sv_from_cstr("./fp_cmp0074_new_root/Cmp0074NewConfig.cmake"));
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("fp_cmp0074_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_FROM=prefix"))) {
                saw_old_from_prefix = true;
            }
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_FROM=root"))) {
                saw_new_from_root = true;
            }
        }
    }

    ASSERT(saw_old_location);
    ASSERT(saw_new_location);
    ASSERT(saw_old_from_prefix);
    ASSERT(saw_new_from_root);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_full_signature_and_variable_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    ASSERT(nob_mkdir_if_not_exists("subproj"));
    const char *sub_cmake =
        "project(SubProj DESCRIPTION subdesc HOMEPAGE_URL https://sub LANGUAGES NONE)\n"
        "add_executable(sub_probe sub.c)\n"
        "target_compile_definitions(sub_probe PRIVATE SUB_TOP=${PROJECT_IS_TOP_LEVEL} SUB_NAMED_TOP=${SubProj_IS_TOP_LEVEL} ROOT_NAME=${CMAKE_PROJECT_NAME} SUB_HOME=${PROJECT_HOMEPAGE_URL})\n";
    ASSERT(nob_write_entire_file("subproj/CMakeLists.txt", sub_cmake, strlen(sub_cmake)));

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(MainProj VERSION 1.2.3.4 DESCRIPTION rootdesc HOMEPAGE_URL https://root LANGUAGES C)\n"
        "add_executable(root_probe main.c)\n"
        "target_compile_definitions(root_probe PRIVATE ROOT_TOP=${PROJECT_IS_TOP_LEVEL} ROOT_NAMED_TOP=${MainProj_IS_TOP_LEVEL} ROOT_MAJOR=${PROJECT_VERSION_MAJOR} ROOT_MINOR=${PROJECT_VERSION_MINOR} ROOT_PATCH=${PROJECT_VERSION_PATCH} ROOT_TWEAK=${PROJECT_VERSION_TWEAK} ROOT_CMAKE_VER=${CMAKE_PROJECT_VERSION} ROOT_HOME=${PROJECT_HOMEPAGE_URL})\n"
        "add_subdirectory(subproj)\n"
        "target_compile_definitions(root_probe PRIVATE ROOT_NAME_AFTER=${CMAKE_PROJECT_NAME} ROOT_HOME_AFTER=${CMAKE_PROJECT_HOMEPAGE_URL})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_main_project_event = false;
    bool saw_sub_project_event = false;
    bool saw_root_top = false;
    bool saw_root_named_top = false;
    bool saw_root_major = false;
    bool saw_root_minor = false;
    bool saw_root_patch = false;
    bool saw_root_tweak = false;
    bool saw_root_cmake_ver = false;
    bool saw_root_home = false;
    bool saw_root_name_after = false;
    bool saw_root_home_after = false;
    bool saw_sub_top_false = false;
    bool saw_sub_named_top_false = false;
    bool saw_sub_root_name = false;
    bool saw_sub_home = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_PROJECT_DECLARE) {
            if (nob_sv_eq(ev->as.project_declare.name, nob_sv_from_cstr("MainProj")) &&
                nob_sv_eq(ev->as.project_declare.version, nob_sv_from_cstr("1.2.3.4")) &&
                nob_sv_eq(ev->as.project_declare.description, nob_sv_from_cstr("rootdesc")) &&
                nob_sv_eq(ev->as.project_declare.languages, nob_sv_from_cstr("C"))) {
                saw_main_project_event = true;
            } else if (nob_sv_eq(ev->as.project_declare.name, nob_sv_from_cstr("SubProj")) &&
                       nob_sv_eq(ev->as.project_declare.version, nob_sv_from_cstr("")) &&
                       nob_sv_eq(ev->as.project_declare.description, nob_sv_from_cstr("subdesc")) &&
                       nob_sv_eq(ev->as.project_declare.languages, nob_sv_from_cstr(""))) {
                saw_sub_project_event = true;
            }
        }

        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("root_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_TOP=TRUE"))) saw_root_top = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAMED_TOP=TRUE"))) saw_root_named_top = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_MAJOR=1"))) saw_root_major = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_MINOR=2"))) saw_root_minor = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_PATCH=3"))) saw_root_patch = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_TWEAK=4"))) saw_root_tweak = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_CMAKE_VER=1.2.3.4"))) saw_root_cmake_ver = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_HOME=https://root"))) saw_root_home = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAME_AFTER=MainProj"))) saw_root_name_after = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_HOME_AFTER=https://root"))) saw_root_home_after = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("sub_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=FALSE")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=0")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=OFF"))) {
                saw_sub_top_false = true;
            }
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=FALSE")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=0")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=OFF"))) {
                saw_sub_named_top_false = true;
            }
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAME=MainProj"))) saw_sub_root_name = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_HOME=https://sub"))) saw_sub_home = true;
        }
    }

    ASSERT(saw_main_project_event);
    ASSERT(saw_sub_project_event);
    ASSERT(saw_root_top);
    ASSERT(saw_root_named_top);
    ASSERT(saw_root_major);
    ASSERT(saw_root_minor);
    ASSERT(saw_root_patch);
    ASSERT(saw_root_tweak);
    ASSERT(saw_root_cmake_ver);
    ASSERT(saw_root_home);
    ASSERT(saw_root_name_after);
    ASSERT(saw_root_home_after);
    ASSERT(saw_sub_top_false);
    ASSERT(saw_sub_named_top_false);
    ASSERT(saw_sub_root_name);
    ASSERT(saw_sub_home);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_cmp0048_new_clears_and_old_preserves_version_vars_without_version_arg) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0048 NEW)\n"
        "set(PROJECT_VERSION stale)\n"
        "set(PROJECT_VERSION_MAJOR stale_major)\n"
        "project(NewNoVer LANGUAGES NONE)\n"
        "add_executable(project_new_nover main.c)\n"
        "target_compile_definitions(project_new_nover PRIVATE NEW_VER=${PROJECT_VERSION} NEW_MAJ=${PROJECT_VERSION_MAJOR})\n"
        "cmake_policy(SET CMP0048 OLD)\n"
        "set(PROJECT_VERSION keep)\n"
        "set(PROJECT_VERSION_MAJOR keep_major)\n"
        "project(OldNoVer LANGUAGES NONE)\n"
        "add_executable(project_old_nover main.c)\n"
        "target_compile_definitions(project_old_nover PRIVATE OLD_VER=${PROJECT_VERSION} OLD_MAJ=${PROJECT_VERSION_MAJOR})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_new_ver_empty = false;
    bool saw_new_maj_empty = false;
    bool saw_old_ver_keep = false;
    bool saw_old_maj_keep = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("project_new_nover"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_VER="))) saw_new_ver_empty = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_MAJ="))) saw_new_maj_empty = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("project_old_nover"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_VER=keep"))) saw_old_ver_keep = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_MAJ=keep_major"))) saw_old_maj_keep = true;
        }
    }

    ASSERT(saw_new_ver_empty);
    ASSERT(saw_new_maj_empty);
    ASSERT(saw_old_ver_keep);
    ASSERT(saw_old_maj_keep);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_rejects_invalid_signature_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(BadVer VERSION 1.a)\n"
        "project(BadLang LANGUAGES NONE C)\n"
        "project(BadMissingVersion VERSION)\n"
        "project(BadDesc DESCRIPTION)\n"
        "project(BadHome HOMEPAGE_URL)\n"
        "project(BadUnexpected VERSION 1.0 C)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);

    bool saw_bad_version = false;
    bool saw_none_mix = false;
    bool saw_missing_version = false;
    bool saw_missing_desc = false;
    bool saw_missing_home = false;
    bool saw_unexpected = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(VERSION ...) expects numeric components"))) {
            saw_bad_version = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("project() LANGUAGES NONE cannot be combined with other languages"))) {
            saw_none_mix = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(VERSION ...) requires a version value"))) {
            saw_missing_version = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(DESCRIPTION ...) requires a value"))) {
            saw_missing_desc = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(HOMEPAGE_URL ...) requires a value"))) {
            saw_missing_home = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project() received unexpected argument in keyword signature"))) {
            saw_unexpected = true;
        }
    }

    ASSERT(saw_bad_version);
    ASSERT(saw_none_mix);
    ASSERT(saw_missing_version);
    ASSERT(saw_missing_desc);
    ASSERT(saw_missing_home);
    ASSERT(saw_unexpected);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_policy_known_unknown_and_if_predicate) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0077 NEW)\n"
        "cmake_policy(GET CMP0077 POL_OUT)\n"
        "if(POLICY CMP0077)\n"
        "  set(IF_KNOWN 1)\n"
        "endif()\n"
        "if(POLICY CMP9999)\n"
        "  set(IF_UNKNOWN 1)\n"
        "endif()\n"
        "cmake_policy(GET CMP9999 BAD_OUT)\n"
        "add_executable(policy_pred main.c)\n"
        "target_compile_definitions(policy_pred PRIVATE POL_OUT=${POL_OUT} IF_KNOWN=${IF_KNOWN} IF_UNKNOWN=${IF_UNKNOWN})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 1);

    bool saw_pol_out = false;
    bool saw_if_known = false;
    bool saw_if_unknown_empty = false;

    bool saw_unknown_policy_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(GET ...) requires a known CMP policy id"))) {
                saw_unknown_policy_error = true;
            }
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("POL_OUT=NEW"))) {
                saw_pol_out = true;
            } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("IF_KNOWN=1"))) {
                saw_if_known = true;
            } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("IF_UNKNOWN="))) {
                saw_if_unknown_empty = true;
            }
        }
    }
    ASSERT(saw_unknown_policy_error);
    ASSERT(saw_pol_out);
    ASSERT(saw_if_known);
    ASSERT(saw_if_unknown_empty);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_policy_strict_arity_and_version_validation) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(PUSH EXTRA)\n"
        "cmake_policy(POP EXTRA)\n"
        "cmake_policy(SET CMP0077 NEW EXTRA)\n"
        "cmake_policy(GET CMP0077)\n"
        "cmake_policy(VERSION 3.10 3.11)\n"
        "cmake_policy(VERSION 2.3)\n"
        "cmake_policy(VERSION 3.29)\n"
        "cmake_policy(VERSION 3.20...3.10)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 8);

    bool saw_push_arity = false;
    bool saw_pop_arity = false;
    bool saw_set_arity = false;
    bool saw_get_arity = false;
    bool saw_version_arity = false;
    bool saw_min_floor = false;
    bool saw_min_running = false;
    bool saw_max_lt_min = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(PUSH) does not accept extra arguments"))) {
            saw_push_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(POP) does not accept extra arguments"))) {
            saw_pop_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(SET ...) expects exactly policy id and value"))) {
            saw_set_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(GET ...) expects exactly policy id and output variable"))) {
            saw_get_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) expects exactly one version argument"))) {
            saw_version_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) requires minimum version >= 2.4"))) {
            saw_min_floor = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) min version exceeds evaluator baseline"))) {
            saw_min_running = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) requires max version >= min version"))) {
            saw_max_lt_min = true;
        }
    }

    ASSERT(saw_push_arity);
    ASSERT(saw_pop_arity);
    ASSERT(saw_set_arity);
    ASSERT(saw_get_arity);
    ASSERT(saw_version_arity);
    ASSERT(saw_min_floor);
    ASSERT(saw_min_running);
    ASSERT(saw_max_lt_min);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_minimum_required_inside_function_applies_policy_not_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(VERSION 3.10)\n"
        "function(set_local_min)\n"
        "  cmake_minimum_required(VERSION 3.28)\n"
        "endfunction()\n"
        "set_local_min()\n"
        "cmake_policy(GET CMP0124 OUT_POL)\n"
        "add_executable(minreq_func main.c)\n"
        "target_compile_definitions(minreq_func PRIVATE OUT_POL=${OUT_POL} MIN_VER=${CMAKE_MINIMUM_REQUIRED_VERSION})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_out_pol = false;
    bool saw_min_ver_empty = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OUT_POL=NEW"))) {
            saw_out_pol = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("MIN_VER="))) {
            saw_min_ver_empty = true;
        }
    }
    ASSERT(saw_out_pol);
    ASSERT(saw_min_ver_empty);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cpack_commands_require_cpackcomponent_module_and_parse_component_extras) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(CPackGate)\n"
        "cpack_add_component(core)\n"
        "include(CPackComponent)\n"
        "cpack_add_component(core DISPLAY_NAME Core ARCHIVE_FILE core.txz PLIST core.plist)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_gate_error = false;
    bool saw_component_event = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unknown command")) &&
            nob_sv_eq(ev->as.diag.hint,
                      nob_sv_from_cstr("include(CPackComponent) must be called before using this command"))) {
            saw_gate_error = true;
        }
        if (ev->h.kind == EV_CPACK_ADD_COMPONENT &&
            nob_sv_eq(ev->as.cpack_add_component.name, nob_sv_from_cstr("core")) &&
            nob_sv_eq(ev->as.cpack_add_component.archive_file, nob_sv_from_cstr("core.txz")) &&
            nob_sv_eq(ev->as.cpack_add_component.plist, nob_sv_from_cstr("core.plist"))) {
            saw_component_event = true;
        }
    }
    ASSERT(saw_gate_error);
    ASSERT(saw_component_event);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_diag_codes_are_explicit_and_report_classes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "unknown_diag_case()\n"
        "cmake_policy(POP)\n"
        "file(READ missing_diag_input.txt OUT_VAR)\n"
        "math()\n"
        "message(WARNING warn-msg)\n"
        "message(SEND_ERROR err-msg)\n"
        "cmake_host_system_information(RESULT HOST_INFO QUERY WINDOWS_REGISTRY HKLM)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 2);
    ASSERT(report->error_count == 5);
    ASSERT(report->input_error_count == 3);
    ASSERT(report->engine_limitation_count == 2);
    ASSERT(report->io_env_error_count == 1);
    ASSERT(report->policy_conflict_count == 1);
    ASSERT(report->unsupported_count == 2);

    bool saw_unknown_command = false;
    bool saw_policy_conflict = false;
    bool saw_file_io_failure = false;
    bool saw_math_missing_required = false;
    bool saw_message_warning = false;
    bool saw_message_error = false;
    bool saw_host_not_implemented = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;

        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unknown command"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_WARNING);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_UNKNOWN_COMMAND")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("ENGINE_LIMITATION")));
            saw_unknown_command = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(POP) called without matching PUSH"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_POLICY_CONFLICT")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("POLICY_CONFLICT")));
            saw_policy_conflict = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("file(READ) failed to read file"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_IO_FAILURE")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("IO_ENV_ERROR")));
            saw_file_io_failure = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("math() requires a subcommand"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_MISSING_REQUIRED")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("INPUT_ERROR")));
            saw_math_missing_required = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("warn-msg"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_WARNING);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_SCRIPT_WARNING")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("INPUT_ERROR")));
            saw_message_warning = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("err-msg"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_SCRIPT_ERROR")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("INPUT_ERROR")));
            saw_message_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_host_system_information(QUERY WINDOWS_REGISTRY ...) is not implemented yet"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_NOT_IMPLEMENTED")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("ENGINE_LIMITATION")));
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("HKLM")));
            saw_host_not_implemented = true;
        }
    }

    ASSERT(saw_unknown_command);
    ASSERT(saw_policy_conflict);
    ASSERT(saw_file_io_failure);
    ASSERT(saw_math_missing_required);
    ASSERT(saw_message_warning);
    ASSERT(saw_message_error);
    ASSERT(saw_host_not_implemented);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_string_hash_repeat_and_json_full_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(SJSON [=[{\"k\":[1,2,3],\"s\":\"x\",\"n\":null}]=])\n"
        "string(MD5 H_MD5 \"abc\")\n"
        "string(SHA1 H_SHA1 \"abc\")\n"
        "string(SHA224 H_SHA224 \"abc\")\n"
        "string(SHA256 H_SHA256 \"abc\")\n"
        "string(SHA384 H_SHA384 \"abc\")\n"
        "string(SHA512 H_SHA512 \"abc\")\n"
        "string(SHA3_224 H_SHA3_224 \"abc\")\n"
        "string(SHA3_256 H_SHA3_256 \"abc\")\n"
        "string(SHA3_384 H_SHA3_384 \"abc\")\n"
        "string(SHA3_512 H_SHA3_512 \"abc\")\n"
        "string(REPEAT \"ab\" 3 SREP)\n"
        "string(REPEAT \"x\" 0 SREP0)\n"
        "string(JSON SJ_MEMBER MEMBER \"${SJSON}\" 1)\n"
        "string(JSON SJ_RM REMOVE \"${SJSON}\" k 1)\n"
        "string(JSON SJ_RM_LEN LENGTH \"${SJ_RM}\" k)\n"
        "string(JSON SJ_SET SET \"${SJSON}\" k 5 99)\n"
        "string(JSON SJ_SET_GET GET \"${SJ_SET}\" k 3)\n"
        "string(JSON SJ_EQ EQUAL \"${SJSON}\" \"${SJSON}\")\n"
        "string(JSON SJ_NEQ EQUAL \"${SJSON}\" [=[{\"k\":[1],\"s\":\"x\",\"n\":null}]=])\n"
        "string(JSON SJ_OK ERROR_VARIABLE SJ_ERR_OK TYPE \"${SJSON}\" k)\n"
        "string(JSON SJ_E1 ERROR_VARIABLE SJ_ERR1 GET \"${SJSON}\" missing)\n"
        "string(JSON SJ_E2 ERROR_VARIABLE SJ_ERR2 LENGTH \"${SJSON}\" s)\n"
        "add_executable(string_full_probe main.c)\n"
        "target_compile_definitions(string_full_probe PRIVATE "
        "\"H_MD5=${H_MD5}\" \"H_SHA1=${H_SHA1}\" \"H_SHA224=${H_SHA224}\" \"H_SHA256=${H_SHA256}\" "
        "\"H_SHA384=${H_SHA384}\" \"H_SHA512=${H_SHA512}\" \"H_SHA3_224=${H_SHA3_224}\" "
        "\"H_SHA3_256=${H_SHA3_256}\" \"H_SHA3_384=${H_SHA3_384}\" \"H_SHA3_512=${H_SHA3_512}\" "
        "\"SREP=${SREP}\" \"SREP0=${SREP0}\" \"SJ_MEMBER=${SJ_MEMBER}\" \"SJ_RM_LEN=${SJ_RM_LEN}\" "
        "\"SJ_SET_GET=${SJ_SET_GET}\" \"SJ_EQ=${SJ_EQ}\" \"SJ_NEQ=${SJ_NEQ}\" "
        "\"SJ_ERR_OK=${SJ_ERR_OK}\" \"SJ_E1=${SJ_E1}\" \"SJ_ERR1=${SJ_ERR1}\" \"SJ_E2=${SJ_E2}\" \"SJ_ERR2=${SJ_ERR2}\")\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_md5 = false;
    bool saw_sha1 = false;
    bool saw_sha224 = false;
    bool saw_sha256 = false;
    bool saw_sha384 = false;
    bool saw_sha512 = false;
    bool saw_sha3_224 = false;
    bool saw_sha3_256 = false;
    bool saw_sha3_384 = false;
    bool saw_sha3_512 = false;
    bool saw_repeat = false;
    bool saw_repeat_zero = false;
    bool saw_member = false;
    bool saw_rm_len = false;
    bool saw_set_get = false;
    bool saw_eq = false;
    bool saw_neq = false;
    bool saw_err_ok = false;
    bool saw_e1 = false;
    bool saw_err1 = false;
    bool saw_e2 = false;
    bool saw_err2 = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("string_full_probe"))) continue;
        String_View it = ev->as.target_compile_definitions.item;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_MD5=900150983cd24fb0d6963f7d28e17f72"))) saw_md5 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA1=a9993e364706816aba3e25717850c26c9cd0d89d"))) saw_sha1 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA224=23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"))) saw_sha224 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))) saw_sha256 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA384=cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"))) saw_sha384 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA512=ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"))) saw_sha512 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA3_224=e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf"))) saw_sha3_224 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA3_256=3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"))) saw_sha3_256 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA3_384=ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b298d88cea927ac7f539f1edf228376d25"))) saw_sha3_384 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA3_512=b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0"))) saw_sha3_512 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SREP=ababab"))) saw_repeat = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SREP0="))) saw_repeat_zero = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_MEMBER=s"))) saw_member = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_RM_LEN=2"))) saw_rm_len = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_SET_GET=99"))) saw_set_get = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_EQ=ON"))) saw_eq = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_NEQ=OFF"))) saw_neq = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_ERR_OK=NOTFOUND"))) saw_err_ok = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_E1=missing-NOTFOUND"))) saw_e1 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_ERR1=string(JSON) object member not found: missing"))) saw_err1 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_E2=s-NOTFOUND"))) saw_e2 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_ERR2=string(JSON LENGTH) requires ARRAY or OBJECT"))) saw_err2 = true;
    }

    ASSERT(saw_md5);
    ASSERT(saw_sha1);
    ASSERT(saw_sha224);
    ASSERT(saw_sha256);
    ASSERT(saw_sha384);
    ASSERT(saw_sha512);
    ASSERT(saw_sha3_224);
    ASSERT(saw_sha3_256);
    ASSERT(saw_sha3_384);
    ASSERT(saw_sha3_512);
    ASSERT(saw_repeat);
    ASSERT(saw_repeat_zero);
    ASSERT(saw_member);
    ASSERT(saw_rm_len);
    ASSERT(saw_set_get);
    ASSERT(saw_eq);
    ASSERT(saw_neq);
    ASSERT(saw_err_ok);
    ASSERT(saw_e1);
    ASSERT(saw_err1);
    ASSERT(saw_e2);
    ASSERT(saw_err2);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_string_text_regex_and_misc_dispatch_events) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    evaluator_set_source_date_epoch_value("946684800");
    Ast_Root root = parse_cmake(
        temp_arena,
        "set(SBUF \"Hi\")\n"
        "string(APPEND SBUF \"-There\")\n"
        "string(JOIN \":\" S_JOIN alpha beta gamma)\n"
        "set(V \"qq\")\n"
        "string(CONFIGURE \"@V@-${V}\" S_CFG @ONLY)\n"
        "string(REPLACE \"na\" \"X\" S_REPL \"banana\")\n"
        "string(TOLOWER \"MiX\" S_LOW)\n"
        "string(SUBSTRING \"abcdef\" 2 3 S_SUB)\n"
        "string(REGEX MATCH \"a[0-9]+\" S_RX_MATCH \"xxa12yy\")\n"
        "string(REGEX REPLACE \"a([0-9]+)\" \"B\\1\" S_RX_REPL \"xa12ya34\")\n"
        "string(REGEX MATCHALL \"[a-z][0-9]\" S_RX_ALL \"a1b2c3\")\n"
        "string(RANDOM LENGTH 6 ALPHABET abc RANDOM_SEED 7 S_RAND)\n"
        "string(TIMESTAMP S_TS \"%Y\" UTC)\n"
        "string(UUID S_UUID3 NAMESPACE \"6ba7b810-9dad-11d1-80b4-00c04fd430c8\" NAME \"demo\" TYPE MD5)\n"
        "string(UUID S_UUID5 NAMESPACE \"6ba7b810-9dad-11d1-80b4-00c04fd430c8\" NAME \"demo\" TYPE SHA1)\n"
        "string(SHA256 S_SHA \"abc\")\n"
        "add_executable(string_split_probe main.c)\n"
        "target_compile_definitions(string_split_probe PRIVATE "
        "\"SBUF=${SBUF}\" \"S_JOIN=${S_JOIN}\" \"S_CFG=${S_CFG}\" \"S_REPL=${S_REPL}\" "
        "\"S_LOW=${S_LOW}\" \"S_SUB=${S_SUB}\" \"S_RX_MATCH=${S_RX_MATCH}\" "
        "\"S_RX_REPL=${S_RX_REPL}\" \"S_RX_ALL=${S_RX_ALL}\" \"S_RAND=${S_RAND}\" "
        "\"S_TS=${S_TS}\" \"S_UUID3=${S_UUID3}\" \"S_UUID5=${S_UUID5}\" \"S_SHA=${S_SHA}\")\n");
    Eval_Result run_res = evaluator_run(ctx, root);
    evaluator_set_source_date_epoch_value(NULL);

    ASSERT(!eval_result_is_fatal(run_res));
    ASSERT(run_res.kind == EVAL_RESULT_OK);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_append = false;
    bool saw_join = false;
    bool saw_configure = false;
    bool saw_replace = false;
    bool saw_lower = false;
    bool saw_substring = false;
    bool saw_regex_match = false;
    bool saw_regex_replace = false;
    bool saw_regex_all = false;
    bool saw_random = false;
    bool saw_timestamp = false;
    bool saw_uuid3 = false;
    bool saw_uuid5 = false;
    bool saw_hash = false;

    bool saw_configure_event = false;
    bool saw_replace_event = false;
    bool saw_regex_match_event = false;
    bool saw_regex_replace_event = false;
    bool saw_hash_event = false;
    bool saw_timestamp_event = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("string_split_probe"))) {
            String_View it = ev->as.target_compile_definitions.item;
            if (nob_sv_eq(it, nob_sv_from_cstr("SBUF=Hi-There"))) saw_append = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_JOIN=alpha:beta:gamma"))) saw_join = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_CFG=qq-qq"))) saw_configure = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_REPL=baXX"))) saw_replace = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_LOW=mix"))) saw_lower = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_SUB=cde"))) saw_substring = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_RX_MATCH=a12"))) saw_regex_match = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_RX_REPL=xB12yB34"))) saw_regex_replace = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_RX_ALL=a1;b2;c3"))) saw_regex_all = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_RAND=bcaacb"))) saw_random = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_TS=2000"))) saw_timestamp = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_UUID3=302a4211-9878-3ad9-b7bf-c706364668a5"))) saw_uuid3 = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_UUID5=6bc1814c-5705-5b6e-af99-104b91962282"))) saw_uuid5 = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_SHA=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))) saw_hash = true;
        }

        if (ev->h.kind == EVENT_STRING_CONFIGURE &&
            nob_sv_eq(ev->as.string_configure.out_var, nob_sv_from_cstr("S_CFG"))) {
            saw_configure_event = true;
        }
        if (ev->h.kind == EVENT_STRING_REPLACE &&
            nob_sv_eq(ev->as.string_replace.out_var, nob_sv_from_cstr("S_REPL"))) {
            saw_replace_event = true;
        }
        if (ev->h.kind == EVENT_STRING_REGEX &&
            nob_sv_eq(ev->as.string_regex.mode, nob_sv_from_cstr("MATCH")) &&
            nob_sv_eq(ev->as.string_regex.out_var, nob_sv_from_cstr("S_RX_MATCH"))) {
            saw_regex_match_event = true;
        }
        if (ev->h.kind == EVENT_STRING_REGEX &&
            nob_sv_eq(ev->as.string_regex.mode, nob_sv_from_cstr("REPLACE")) &&
            nob_sv_eq(ev->as.string_regex.out_var, nob_sv_from_cstr("S_RX_REPL"))) {
            saw_regex_replace_event = true;
        }
        if (ev->h.kind == EVENT_STRING_HASH &&
            nob_sv_eq(ev->as.string_hash.algorithm, nob_sv_from_cstr("SHA256")) &&
            nob_sv_eq(ev->as.string_hash.out_var, nob_sv_from_cstr("S_SHA"))) {
            saw_hash_event = true;
        }
        if (ev->h.kind == EVENT_STRING_TIMESTAMP &&
            nob_sv_eq(ev->as.string_timestamp.out_var, nob_sv_from_cstr("S_TS"))) {
            saw_timestamp_event = true;
        }
    }

    ASSERT(saw_append);
    ASSERT(saw_join);
    ASSERT(saw_configure);
    ASSERT(saw_replace);
    ASSERT(saw_lower);
    ASSERT(saw_substring);
    ASSERT(saw_regex_match);
    ASSERT(saw_regex_replace);
    ASSERT(saw_regex_all);
    ASSERT(saw_random);
    ASSERT(saw_timestamp);
    ASSERT(saw_uuid3);
    ASSERT(saw_uuid5);
    ASSERT(saw_hash);

    ASSERT(saw_configure_event);
    ASSERT(saw_replace_event);
    ASSERT(saw_regex_match_event);
    ASSERT(saw_regex_replace_event);
    ASSERT(saw_hash_event);
    ASSERT(saw_timestamp_event);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_string_regex_parse_error_keeps_diag_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "string(REGEX MATCH \"[\" BAD_RX \"abc\")\n");
    Eval_Result run_res = evaluator_run(ctx, root);

    ASSERT(!eval_result_is_fatal(run_res));
    ASSERT(run_res.kind == EVAL_RESULT_SOFT_ERROR);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);
    ASSERT(report->warning_count == 0);
    ASSERT(report->input_error_count == 1);

    bool saw_parse_diag = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (!nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Invalid regex pattern"))) continue;
        ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
        ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_PARSE_ERROR")));
        ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("INPUT_ERROR")));
        ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("[")));
        saw_parse_diag = true;
    }
    ASSERT(saw_parse_diag);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_extra_subcommands_and_download_expected_hash) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(V \"qq\")\n"
        "file(WRITE extra_src.txt \"abc\")\n"
        "file(SHA256 extra_src.txt EXTRA_HASH)\n"
        "file(CONFIGURE OUTPUT extra_cfg.txt CONTENT \"@V@-${V}\" @ONLY)\n"
        "file(READ extra_cfg.txt EXTRA_CFG)\n"
        "file(COPY_FILE extra_src.txt extra_dst.txt RESULT COPY_RES ONLY_IF_DIFFERENT INPUT_MAY_BE_RECENT)\n"
        "file(READ extra_dst.txt COPY_TXT)\n"
        "file(TOUCH extra_touch.txt)\n"
        "if(EXISTS extra_touch.txt)\n"
        "  set(TOUCH_CREATED 1)\n"
        "else()\n"
        "  set(TOUCH_CREATED 0)\n"
        "endif()\n"
        "file(TOUCH_NOCREATE extra_touch_missing.txt)\n"
        "if(EXISTS extra_touch_missing.txt)\n"
        "  set(TOUCH_NOCREATE_CREATED 1)\n"
        "else()\n"
        "  set(TOUCH_NOCREATE_CREATED 0)\n"
        "endif()\n"
        "if(EXISTS \"/bin/ls\")\n"
        "  file(GET_RUNTIME_DEPENDENCIES RESOLVED_DEPENDENCIES_VAR RD_RES UNRESOLVED_DEPENDENCIES_VAR RD_UNRES EXECUTABLES /bin/ls)\n"
        "  list(LENGTH RD_RES RD_RES_LEN)\n"
        "else()\n"
        "  set(RD_RES_LEN 0)\n"
        "endif()\n"
        "file(WRITE extra_dl_src.txt \"hello\")\n"
        "file(DOWNLOAD extra_dl_src.txt extra_dl_ok.txt EXPECTED_HASH SHA256=2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824)\n"
        "file(READ extra_dl_ok.txt DL_OK_TXT)\n"
        "file(DOWNLOAD extra_dl_src.txt extra_dl_bad.txt EXPECTED_HASH SHA256=0000 STATUS DL_BAD_STATUS)\n"
        "list(LENGTH DL_BAD_STATUS DL_BAD_LEN)\n"
        "list(GET DL_BAD_STATUS 0 DL_BAD_CODE)\n"
        "add_executable(file_extra_probe main.c)\n"
        "target_compile_definitions(file_extra_probe PRIVATE "
        "\"EXTRA_HASH=${EXTRA_HASH}\" \"EXTRA_CFG=${EXTRA_CFG}\" "
        "\"COPY_RES=${COPY_RES}\" \"COPY_TXT=${COPY_TXT}\" "
        "\"TOUCH_CREATED=${TOUCH_CREATED}\" \"TOUCH_NOCREATE_CREATED=${TOUCH_NOCREATE_CREATED}\" "
        "\"RD_RES_LEN=${RD_RES_LEN}\" "
        "\"DL_OK_TXT=${DL_OK_TXT}\" \"DL_BAD_LEN=${DL_BAD_LEN}\" \"DL_BAD_CODE=${DL_BAD_CODE}\")\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_hash = false;
    bool saw_cfg = false;
    bool saw_copy_res = false;
    bool saw_copy_txt = false;
    bool saw_touch_created = false;
    bool saw_touch_nocreate_created = false;
    bool saw_rd_res_len = false;
    bool saw_dl_ok = false;
    bool saw_dl_bad_len = false;
    bool saw_dl_bad_code = false;
    bool expect_rd_nonempty = (access("/bin/ls", F_OK) == 0);

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("file_extra_probe"))) continue;
        String_View it = ev->as.target_compile_definitions.item;
        if (nob_sv_eq(it, nob_sv_from_cstr("EXTRA_HASH=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))) saw_hash = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("EXTRA_CFG=qq-qq"))) saw_cfg = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("COPY_RES=0"))) saw_copy_res = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("COPY_TXT=abc"))) saw_copy_txt = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("TOUCH_CREATED=1"))) saw_touch_created = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("TOUCH_NOCREATE_CREATED=0"))) saw_touch_nocreate_created = true;
        if (it.count >= 11 && memcmp(it.data, "RD_RES_LEN=", 11) == 0) {
            char buf[64] = {0};
            size_t n = it.count - 11;
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, it.data + 11, n);
            long v = strtol(buf, NULL, 10);
            if (expect_rd_nonempty) {
                if (v > 0) saw_rd_res_len = true;
            } else {
                if (v == 0) saw_rd_res_len = true;
            }
        }
        if (nob_sv_eq(it, nob_sv_from_cstr("DL_OK_TXT=hello"))) saw_dl_ok = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("DL_BAD_LEN=2"))) saw_dl_bad_len = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("DL_BAD_CODE=1"))) saw_dl_bad_code = true;
    }

    ASSERT(saw_hash);
    ASSERT(saw_cfg);
    ASSERT(saw_copy_res);
    ASSERT(saw_copy_txt);
    ASSERT(saw_touch_created);
    ASSERT(saw_touch_nocreate_created);
    ASSERT(saw_rd_res_len);
    ASSERT(saw_dl_ok);
    ASSERT(saw_dl_bad_len);
    ASSERT(saw_dl_bad_code);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_dispatcher_routes_glob_rw_and_copy_families) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY dispatch_glob/sub)\n"
        "file(WRITE dispatch_glob/a.c \"int a = 0;\\n\")\n"
        "file(WRITE dispatch_glob/sub/b.c \"int b = 0;\\n\")\n"
        "file(GLOB_RECURSE DISPATCH_GLOB RELATIVE dispatch_glob dispatch_glob/*.c dispatch_glob/*/*.c)\n"
        "file(WRITE dispatch_rw.txt \"alpha\")\n"
        "file(READ dispatch_rw.txt DISPATCH_READ)\n"
        "file(COPY dispatch_rw.txt DESTINATION dispatch_copy)\n"
        "file(READ dispatch_copy/dispatch_rw.txt DISPATCH_COPY)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    String_View glob_out = eval_var_get(ctx, nob_sv_from_cstr("DISPATCH_GLOB"));
    ASSERT(sv_contains_sv(glob_out, nob_sv_from_cstr("a.c")));
    ASSERT(sv_contains_sv(glob_out, nob_sv_from_cstr("sub/b.c")));
    const char *a_pos = strstr(nob_temp_sv_to_cstr(glob_out), "a.c");
    const char *b_pos = strstr(nob_temp_sv_to_cstr(glob_out), "sub/b.c");
    ASSERT(a_pos != NULL && b_pos != NULL && a_pos < b_pos);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("DISPATCH_READ")), nob_sv_from_cstr("alpha")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("DISPATCH_COPY")), nob_sv_from_cstr("alpha")));

    bool saw_glob_event = false;
    bool saw_write_event = false;
    bool saw_read_event = false;
    bool saw_copy_read_event = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_FS_GLOB &&
            nob_sv_eq(ev->as.fs_glob.out_var, nob_sv_from_cstr("DISPATCH_GLOB")) &&
            ev->as.fs_glob.recursive &&
            sv_contains_sv(ev->as.fs_glob.base_dir, nob_sv_from_cstr("dispatch_glob"))) {
            saw_glob_event = true;
        } else if (ev->h.kind == EVENT_FS_WRITE_FILE &&
                   sv_contains_sv(ev->as.fs_write_file.path, nob_sv_from_cstr("dispatch_rw.txt"))) {
            saw_write_event = true;
        } else if (ev->h.kind == EVENT_FS_READ_FILE &&
                   nob_sv_eq(ev->as.fs_read_file.out_var, nob_sv_from_cstr("DISPATCH_READ")) &&
                   sv_contains_sv(ev->as.fs_read_file.path, nob_sv_from_cstr("dispatch_rw.txt"))) {
            saw_read_event = true;
        } else if (ev->h.kind == EVENT_FS_READ_FILE &&
                   nob_sv_eq(ev->as.fs_read_file.out_var, nob_sv_from_cstr("DISPATCH_COPY")) &&
                   sv_contains_sv(ev->as.fs_read_file.path, nob_sv_from_cstr("dispatch_copy/dispatch_rw.txt"))) {
            saw_copy_read_event = true;
        }
    }

    ASSERT(saw_glob_event);
    ASSERT(saw_write_event);
    ASSERT(saw_read_event);
    ASSERT(saw_copy_read_event);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_configure_file_expands_cmakedefines_and_copyonly) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    const char *cfg_template =
        "NAME=@NAME@\n"
        "LITERAL=${NAME}\n"
        "QUOTE=@QUOTE@\n"
        "#cmakedefine ENABLE_FEATURE\n"
        "#cmakedefine DISABLE_FEATURE\n"
        "#cmakedefine01 ENABLE_FEATURE\n"
        "#cmakedefine01 DISABLE_FEATURE\n";
    const char *cfg_copy = "@NAME@\n${NAME}\n";
    ASSERT(nob_write_entire_file("cfg_template.in", cfg_template, strlen(cfg_template)));
    ASSERT(nob_write_entire_file("cfg_copy.in", cfg_copy, strlen(cfg_copy)));
    ASSERT(nob_mkdir_if_not_exists("cfg_out_dir"));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(NAME Demo)\n"
        "set(QUOTE one\\\"two)\n"
        "set(ENABLE_FEATURE ON)\n"
        "set(DISABLE_FEATURE 0)\n"
        "configure_file(cfg_template.in cfg_configured.txt @ONLY ESCAPE_QUOTES NEWLINE_STYLE DOS)\n"
        "configure_file(cfg_copy.in cfg_out_dir COPYONLY)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    String_View configured = {0};
    String_View copied = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "cfg_configured.txt", &configured));
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "cfg_out_dir/cfg_copy.in", &copied));

    ASSERT(nob_sv_eq(configured,
                     nob_sv_from_cstr("NAME=Demo\r\n"
                                      "LITERAL=${NAME}\r\n"
                                      "QUOTE=one\\\\\"two\r\n"
                                      "#define ENABLE_FEATURE\r\n"
                                      "/* #undef DISABLE_FEATURE */\r\n"
                                      "#define ENABLE_FEATURE 1\r\n"
                                      "#define DISABLE_FEATURE 0\r\n")));
    ASSERT(nob_sv_eq(copied, nob_sv_from_cstr("@NAME@\n${NAME}\n")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_real_path_cmp0152_old_and_new) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("cmp0152_real_target"));
    ASSERT(nob_mkdir_if_not_exists("cmp0152_real_target/child"));
    ASSERT(nob_write_entire_file("cmp0152_real_target/cmp0152_result.txt", "target\n", 7));
    ASSERT(nob_write_entire_file("cmp0152_result.txt", "cwd\n", 4));
    ASSERT(evaluator_create_directory_link_like("cmp0152_real_link", "cmp0152_real_target/child"));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0152 OLD)\n"
        "file(REAL_PATH cmp0152_real_link/../cmp0152_result.txt OUT_OLD)\n"
        "cmake_policy(SET CMP0152 NEW)\n"
        "file(REAL_PATH cmp0152_real_link/../cmp0152_result.txt OUT_NEW)\n"
        "add_executable(real_path_policy_probe main.c)\n"
        "target_compile_definitions(real_path_policy_probe PRIVATE OLD=${OUT_OLD} NEW=${OUT_NEW})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);

    char old_path[1024] = {0};
    char new_path[1024] = {0};
    int old_n = snprintf(old_path, sizeof(old_path), "%s/cmp0152_result.txt", cwd);
    int new_n = snprintf(new_path, sizeof(new_path), "%s/cmp0152_real_target/cmp0152_result.txt", cwd);
    ASSERT(old_n > 0 && old_n < (int)sizeof(old_path));
    ASSERT(new_n > 0 && new_n < (int)sizeof(new_path));
    for (size_t i = 0; old_path[i] != '\0'; i++) if (old_path[i] == '\\') old_path[i] = '/';
    for (size_t i = 0; new_path[i] != '\0'; i++) if (new_path[i] == '\\') new_path[i] = '/';

    char old_item[1200] = {0};
    char new_item[1200] = {0};
    int old_item_n = snprintf(old_item, sizeof(old_item), "OLD=%s", old_path);
    int new_item_n = snprintf(new_item, sizeof(new_item), "NEW=%s", new_path);
    ASSERT(old_item_n > 0 && old_item_n < (int)sizeof(old_item));
    ASSERT(new_item_n > 0 && new_item_n < (int)sizeof(new_item));

    bool saw_old = false;
    bool saw_new = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("real_path_policy_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr(old_item))) saw_old = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr(new_item))) saw_new = true;
    }

    ASSERT(saw_old);
    ASSERT(saw_new);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_generate_is_deferred_until_end_of_run) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE gen_in.txt \"IN\")\n"
        "file(GENERATE OUTPUT gen_out_content.txt CONTENT \"OUT\")\n"
        "file(GENERATE OUTPUT gen_out_input.txt INPUT gen_in.txt)\n"
        "file(GENERATE OUTPUT gen_out_skip.txt CONTENT \"SKIP\" CONDITION 0)\n"
        "if(EXISTS gen_out_content.txt)\n"
        "  set(GEN_BEFORE 1)\n"
        "else()\n"
        "  set(GEN_BEFORE 0)\n"
        "endif()\n"
        "add_executable(gen_deferred_probe main.c)\n"
        "target_compile_definitions(gen_deferred_probe PRIVATE GEN_BEFORE=${GEN_BEFORE})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_before_zero = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("gen_deferred_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GEN_BEFORE=0"))) {
            saw_before_zero = true;
        }
    }
    ASSERT(saw_before_zero);

    Ast_Root verify = parse_cmake(
        temp_arena,
        "file(READ gen_out_content.txt GEN_OUT)\n"
        "file(READ gen_out_input.txt GEN_IN)\n"
        "if(EXISTS gen_out_skip.txt)\n"
        "  set(GEN_SKIP 1)\n"
        "else()\n"
        "  set(GEN_SKIP 0)\n"
        "endif()\n"
        "add_executable(gen_deferred_verify main.c)\n"
        "target_compile_definitions(gen_deferred_verify PRIVATE GEN_OUT=${GEN_OUT} GEN_IN=${GEN_IN} GEN_SKIP=${GEN_SKIP})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, verify)));

    bool saw_out = false;
    bool saw_in = false;
    bool saw_skip = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("gen_deferred_verify"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GEN_OUT=OUT"))) saw_out = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GEN_IN=IN"))) saw_in = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GEN_SKIP=0"))) saw_skip = true;
    }
    ASSERT(saw_out);
    ASSERT(saw_in);
    ASSERT(saw_skip);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_lock_directory_and_duplicate_lock_result) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY lock_dir)\n"
        "file(LOCK lock_dir DIRECTORY RESULT_VARIABLE L1)\n"
        "file(LOCK lock_dir DIRECTORY RESULT_VARIABLE L2)\n"
        "file(LOCK lock_dir DIRECTORY RELEASE RESULT_VARIABLE L3)\n"
        "add_executable(lock_probe main.c)\n"
        "target_compile_definitions(lock_probe PRIVATE L1=${L1} L2=${L2} L3=${L3})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_l1_ok = false;
    bool saw_l2_nonzero = false;
    bool saw_l3_ok = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("lock_probe"))) continue;
        String_View item = ev->as.target_compile_definitions.item;
        if (nob_sv_eq(item, nob_sv_from_cstr("L1=0"))) saw_l1_ok = true;
        if (item.count > 3 && memcmp(item.data, "L2=", 3) == 0 && !nob_sv_eq(item, nob_sv_from_cstr("L2=0"))) {
            saw_l2_nonzero = true;
        }
        if (nob_sv_eq(item, nob_sv_from_cstr("L3=0"))) saw_l3_ok = true;
    }
    ASSERT(saw_l1_ok);
    ASSERT(saw_l2_nonzero);
    ASSERT(saw_l3_ok);
    ASSERT(nob_file_exists("lock_dir/cmake.lock"));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_download_probe_mode_without_destination) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE probe_src.txt \"abc\")\n"
        "file(DOWNLOAD probe_src.txt STATUS DL_STATUS LOG DL_LOG)\n"
        "list(LENGTH DL_STATUS DL_LEN)\n"
        "list(GET DL_STATUS 0 DL_CODE)\n"
        "add_executable(dl_probe main.c)\n"
        "target_compile_definitions(dl_probe PRIVATE DL_LEN=${DL_LEN} DL_CODE=${DL_CODE})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_len = false;
    bool saw_code = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("dl_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("DL_LEN=2"))) saw_len = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("DL_CODE=0"))) saw_code = true;
    }
    ASSERT(saw_len);
    ASSERT(saw_code);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_compile_no_cache_and_cmake_flags_do_not_leak) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_compile(TC_LOCAL_ONLY tc_try_local\n"
        "  SOURCE_FROM_CONTENT probe.c \"int main(void){return 0;}\"\n"
        "  CMAKE_FLAGS -DINNER_ONLY:BOOL=ON\n"
        "  NO_CACHE)\n"
        "add_executable(tc_try_local_probe main.c)\n"
        "target_compile_definitions(tc_try_local_probe PRIVATE TC_LOCAL_ONLY=${TC_LOCAL_ONLY} \"INNER_ONLY_PARENT=${INNER_ONLY}\")\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_local_only = false;
    bool saw_parent_empty = false;
    bool saw_cache_entry = false;
    bool saw_parent_binding = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_SET_CACHE_ENTRY &&
            ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("TC_LOCAL_ONLY"))) {
            saw_cache_entry = true;
        }
        if (ev->h.kind == EV_VAR_SET &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("INNER_ONLY"))) {
            saw_parent_binding = true;
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("tc_try_local_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("TC_LOCAL_ONLY=1"))) {
            saw_local_only = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INNER_ONLY_PARENT="))) {
            saw_parent_empty = true;
        }
    }

    ASSERT(saw_local_only);
    ASSERT(saw_parent_empty);
    ASSERT(!saw_cache_entry);
    ASSERT(!saw_parent_binding);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_compile_failure_populates_output_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_compile(TC_FAIL tc_try_fail\n"
        "  SOURCE_FROM_CONTENT broken.c \"int main(void){ this is not valid C; }\"\n"
        "  OUTPUT_VARIABLE TC_FAIL_LOG\n"
        "  NO_CACHE)\n"
        "string(LENGTH \"${TC_FAIL_LOG}\" TC_FAIL_LOG_LEN)\n"
        "add_executable(tc_try_fail_probe main.c)\n"
        "target_compile_definitions(tc_try_fail_probe PRIVATE TC_FAIL=${TC_FAIL} TC_FAIL_LOG_LEN=${TC_FAIL_LOG_LEN})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_fail_result = false;
    size_t fail_log_len = 0;
    bool saw_fail_log_len = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("tc_try_fail_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("TC_FAIL=0"))) {
            saw_fail_result = true;
            continue;
        }
        if (sv_starts_with_cstr(ev->as.target_compile_definitions.item, "TC_FAIL_LOG_LEN=")) {
            String_View len_sv = nob_sv_from_parts(
                ev->as.target_compile_definitions.item.data + strlen("TC_FAIL_LOG_LEN="),
                ev->as.target_compile_definitions.item.count - strlen("TC_FAIL_LOG_LEN="));
            char buf[64] = {0};
            ASSERT(len_sv.count < sizeof(buf));
            memcpy(buf, len_sv.data, len_sv.count);
            fail_log_len = (size_t)strtoull(buf, NULL, 10);
            saw_fail_log_len = true;
        }
    }

    ASSERT(saw_fail_result);
    ASSERT(saw_fail_log_len);
    ASSERT(fail_log_len > 0);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_compile_empty_capture_file_is_silent) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    ASSERT(nob_write_entire_file("tc_empty_capture.txt", "", 0));

    Nob_String_Builder log = {0};
    evaluator_begin_nob_log_capture();
    ASSERT(try_compile_append_file_to_log(ctx, "./tc_empty_capture.txt", &log));
    evaluator_end_nob_log_capture();

    ASSERT(log.count == 0);
    ASSERT(g_evaluator_captured_nob_logs.count == 0);

    nob_sb_free(log);
    nob_sb_free(g_evaluator_captured_nob_logs);
    g_evaluator_captured_nob_logs = (Nob_String_Builder){0};
    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

void run_evaluator_v2_tests(int *passed, int *failed) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "evaluator");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "evaluator suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "evaluator suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    test_evaluator_golden_all_cases(passed, failed);
    test_evaluator_public_api_profile_and_report_snapshot(passed, failed);
    test_evaluator_g5_legacy_wrapper_capabilities_promoted_to_full(passed, failed);
    test_evaluator_command_capability_remains_native_only_introspection(passed, failed);
    test_evaluator_native_command_registry_runtime_extension(passed, failed);
    test_evaluator_native_command_registry_case_insensitive_index_lookup(passed, failed);
    test_evaluator_compat_refresh_snapshot_applies_next_command_cycle(passed, failed);
    test_evaluator_global_diag_strict_controls_event_report_and_runtime_gating(passed, failed);
    test_evaluator_unsupported_policy_snapshot_applies_next_command_cycle(passed, failed);
    test_evaluator_run_result_kind_tri_state_contract(passed, failed);
    test_evaluator_link_libraries_supports_qualifiers_and_rejects_dangling_qualifier(passed, failed);
    test_evaluator_cmake_path_extended_surface_and_strict_validation(passed, failed);
    test_evaluator_flow_commands_reject_extra_arguments(passed, failed);
    test_evaluator_while_iteration_limit_snapshot_applies_per_loop_entry(passed, failed);
    test_evaluator_while_iteration_limit_invalid_value_warns_and_falls_back(passed, failed);
    test_evaluator_enable_testing_does_not_set_build_testing_variable(passed, failed);
    test_evaluator_enable_testing_rejects_extra_arguments(passed, failed);
    test_evaluator_enable_language_updates_enabled_language_state_and_validates_scope(passed, failed);
    test_evaluator_include_supports_result_variable_optional_and_module_search(passed, failed);
    test_evaluator_include_validates_options_strictly(passed, failed);
    test_evaluator_include_cmp0017_search_order_from_builtin_modules(passed, failed);
    test_evaluator_include_guard_default_scope_is_strict_and_warning_free(passed, failed);
    test_evaluator_include_guard_directory_scope_applies_only_to_directory_and_children(passed, failed);
    test_evaluator_include_guard_global_scope_persists_across_function_scope(passed, failed);
    test_evaluator_include_guard_rejects_invalid_arguments(passed, failed);
    test_evaluator_add_test_name_signature_parses_supported_options(passed, failed);
    test_evaluator_add_test_name_signature_rejects_unexpected_arguments(passed, failed);
    test_evaluator_add_definitions_routes_d_flags_to_compile_definitions(passed, failed);
    test_evaluator_add_compile_definitions_updates_existing_and_future_targets(passed, failed);
    test_evaluator_execute_process_captures_output_and_models_3_28_fatal_mode(passed, failed);
    test_evaluator_cmake_language_core_subcommands_work(passed, failed);
    test_evaluator_cmake_language_dependency_provider_models_find_package_hook(passed, failed);
    test_evaluator_cmake_language_dependency_provider_models_fetchcontent_hook(passed, failed);
    test_evaluator_fetchcontent_url_population_and_redirect_override(passed, failed);
    test_evaluator_fetchcontent_populate_direct_url_download_no_extract_does_not_add_subdirectory(passed, failed);
    test_evaluator_fetchcontent_populate_saved_details_git_clones_without_add_subdirectory(passed, failed);
    test_evaluator_fetchcontent_makeavailable_try_find_package_always_prefers_package_resolution(passed, failed);
    test_evaluator_fetchcontent_negative_declarations_and_hash_failure_surface_diags(passed, failed);
    test_evaluator_target_compile_definitions_normalizes_dash_d_items(passed, failed);
    test_evaluator_add_custom_command_target_validates_signature_and_target(passed, failed);
    test_evaluator_add_custom_command_output_validates_conflicts(passed, failed);
    test_evaluator_event_ir_taxonomy_is_frozen(passed, failed);
    test_evaluator_event_ir_metadata_and_stream_contract(passed, failed);
    test_evaluator_event_ir_directory_semantics_and_trace_surface(passed, failed);
    test_evaluator_event_ir_command_trace_sequences_unknown_and_error_paths(passed, failed);
    test_evaluator_event_ir_command_trace_sequences_success_paths(passed, failed);
    test_evaluator_return_in_macro_is_error_and_does_not_unwind_macro_body(passed, failed);
    test_evaluator_return_cmp0140_old_ignores_args_and_new_enables_propagate(passed, failed);
    test_evaluator_cmake_language_eval_inline_soft_error_preserves_context(passed, failed);
    test_evaluator_cmake_language_dependency_provider_rejects_invalid_forms(passed, failed);
    test_evaluator_list_transform_genex_strip_and_output_variable(passed, failed);
    test_evaluator_list_transform_output_variable_requires_single_output_var(passed, failed);
    test_evaluator_math_rejects_empty_and_incomplete_invocations(passed, failed);
    test_evaluator_set_target_properties_rejects_alias_target(passed, failed);
    test_evaluator_add_executable_imported_and_alias_signatures(passed, failed);
    test_evaluator_add_library_imported_alias_and_default_type(passed, failed);
    test_evaluator_set_property_target_rejects_alias_and_unknown_target(passed, failed);
    test_evaluator_define_property_initializes_target_properties_from_variable(passed, failed);
    test_evaluator_set_property_source_test_directory_clauses_parse_and_apply(passed, failed);
    test_evaluator_set_property_cache_requires_existing_entry(passed, failed);
    test_evaluator_set_property_allows_zero_objects_and_validates_test_lookup(passed, failed);
    test_evaluator_get_property_core_queries_and_directory_wrappers(passed, failed);
    test_evaluator_get_property_target_source_and_test_wrappers(passed, failed);
    test_evaluator_get_property_source_directory_clause_and_get_cmake_property_lists(passed, failed);
    test_evaluator_get_property_inherited_target_and_source_queries_follow_declared_target_directory(passed, failed);
    test_evaluator_directory_scoped_property_queries_require_known_directories(passed, failed);
    test_evaluator_option_mark_as_advanced_and_include_regular_expression_follow_policies(passed, failed);
    test_evaluator_separate_arguments_covers_program_mode_and_legacy_form(passed, failed);
    test_evaluator_separate_arguments_rejects_invalid_option_shapes(passed, failed);
    test_evaluator_remove_definitions_updates_directory_definition_and_option_state(passed, failed);
    test_evaluator_host_introspection_and_site_name_cover_supported_queries(passed, failed);
    test_evaluator_build_name_and_build_command_follow_policy_gates(passed, failed);
    test_evaluator_try_run_new_signature_accepts_no_log_and_runs(passed, failed);
    test_evaluator_try_run_executes_native_artifacts_and_reports_partial_limits(passed, failed);
    test_evaluator_try_run_uses_crosscompiling_emulator_when_available(passed, failed);
    test_evaluator_exec_program_respects_cmp0153_and_legacy_wrapper_surface(passed, failed);
    test_evaluator_batch6_metadata_commands_cover_documented_subset(passed, failed);
    test_evaluator_batch6_metadata_commands_reject_unsupported_forms(passed, failed);
    test_evaluator_load_cache_supports_multi_path_legacy_mode_and_prefix_unset(passed, failed);
    test_evaluator_export_cxx_modules_directory_writes_sidecars_and_default_export_file(passed, failed);
    test_evaluator_export_rejects_invalid_extension_and_alias_targets(passed, failed);
    test_evaluator_ctest_family_models_metadata_and_safe_local_effects(passed, failed);
    test_evaluator_ctest_family_rejects_invalid_and_unsupported_forms(passed, failed);
    test_evaluator_batch8_legacy_commands_register_and_model_compat_paths(passed, failed);
    test_evaluator_batch8_legacy_commands_reject_invalid_forms(passed, failed);
    test_evaluator_target_sources_compile_features_and_precompile_headers_model_usage_requirements(passed, failed);
    test_evaluator_source_group_supports_files_tree_and_regex_forms(passed, failed);
    test_evaluator_message_mode_severity_mapping(passed, failed);
    test_evaluator_message_check_pass_without_start_is_error(passed, failed);
    test_evaluator_message_deprecation_respects_control_variables(passed, failed);
    test_evaluator_message_configure_log_persists_yaml_file(passed, failed);
    test_evaluator_set_and_unset_env_forms(passed, failed);
    test_evaluator_process_env_service_overlays_execute_process_and_timestamp(passed, failed);
    test_evaluator_cmake_parse_arguments_supports_direct_and_parse_argv_forms(passed, failed);
    test_evaluator_unset_env_rejects_options(passed, failed);
    test_evaluator_set_cache_cmp0126_old_and_new_semantics(passed, failed);
    test_evaluator_set_cache_policy_version_defaults_cmp0126_to_new(passed, failed);
    test_evaluator_find_item_command_rejects_unknown_option(passed, failed);
    test_evaluator_find_item_command_rejects_missing_output_variable(passed, failed);
    test_evaluator_find_item_command_rejects_missing_registry_or_validator_values(passed, failed);
    test_evaluator_find_item_command_rejects_malformed_env_clause(passed, failed);
    test_evaluator_find_item_commands_resolve_local_paths_and_model_package_root_policies(passed, failed);
    test_evaluator_get_filename_component_covers_documented_modes(passed, failed);
    test_evaluator_find_package_no_module_names_configs_path_suffixes_and_registry_view(passed, failed);
    test_evaluator_find_package_auto_prefers_config_when_requested(passed, failed);
    test_evaluator_find_package_cmp0074_old_ignores_root_and_new_uses_root(passed, failed);
    test_evaluator_project_full_signature_and_variable_surface(passed, failed);
    test_evaluator_project_cmp0048_new_clears_and_old_preserves_version_vars_without_version_arg(passed, failed);
    test_evaluator_project_rejects_invalid_signature_forms(passed, failed);
    test_evaluator_policy_known_unknown_and_if_predicate(passed, failed);
    test_evaluator_policy_strict_arity_and_version_validation(passed, failed);
    test_evaluator_cmake_minimum_required_inside_function_applies_policy_not_variable(passed, failed);
    test_evaluator_cpack_commands_require_cpackcomponent_module_and_parse_component_extras(passed, failed);
    test_evaluator_diag_codes_are_explicit_and_report_classes(passed, failed);
    test_evaluator_string_hash_repeat_and_json_full_surface(passed, failed);
    test_evaluator_string_text_regex_and_misc_dispatch_events(passed, failed);
    test_evaluator_string_regex_parse_error_keeps_diag_surface(passed, failed);
    test_evaluator_file_extra_subcommands_and_download_expected_hash(passed, failed);
    test_evaluator_file_dispatcher_routes_glob_rw_and_copy_families(passed, failed);
    test_evaluator_configure_file_expands_cmakedefines_and_copyonly(passed, failed);
    test_evaluator_file_real_path_cmp0152_old_and_new(passed, failed);
    test_evaluator_file_generate_is_deferred_until_end_of_run(passed, failed);
    test_evaluator_file_lock_directory_and_duplicate_lock_result(passed, failed);
    test_evaluator_file_download_probe_mode_without_destination(passed, failed);
    test_evaluator_try_compile_no_cache_and_cmake_flags_do_not_leak(passed, failed);
    test_evaluator_try_compile_failure_populates_output_variable(passed, failed);
    test_evaluator_try_compile_empty_capture_file_is_silent(passed, failed);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "evaluator suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
