#include "eval_file_internal.h"
#include "eval_hash.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#if !defined(_WIN32)
#include <regex.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static bool file_read_bytes(Evaluator_Context *ctx, String_View path, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);
    return nob_read_entire_file(p, out);
}

static bool file_write_bytes(Evaluator_Context *ctx, String_View path, const char *data, size_t len) {
    if (!ctx) return false;
    if (!eval_file_mkdir_p(ctx, svu_dirname(path))) return false;
    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);
    return nob_write_entire_file(p, data, len);
}

static bool file_same_content(Evaluator_Context *ctx, String_View path, String_View content, bool *out_same) {
    if (!ctx || !out_same) return false;
    *out_same = false;

    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(p, &sb)) return true;
    String_View cur = nob_sv_from_parts(sb.items, sb.count);
    *out_same = nob_sv_eq(cur, content);
    nob_sb_free(sb);
    return true;
}

static bool handle_file_hash(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count != 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(<HASH>) requires filename and output variable"),
                       nob_sv_from_cstr("Usage: file(<HASH> <filename> <out-var>)"));
        return true;
    }

    String_View in_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args.items[1],
                                eval_file_current_src_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &in_path)) {
        return true;
    }

    Nob_String_Builder data = {0};
    if (!file_read_bytes(ctx, in_path, &data)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(<HASH>) failed to read file"), in_path);
        return true;
    }

    String_View digest = nob_sv_from_cstr("");
    String_View payload = nob_sv_from_parts(data.items, data.count);
    bool ok = eval_hash_compute_hex_temp(ctx, args.items[0], payload, &digest);
    nob_sb_free(data);
    if (!ok) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("Unsupported hash algorithm"), args.items[0]);
        return true;
    }

    (void)eval_var_set(ctx, args.items[2], digest);
    return true;
}

static String_View file_expand_configure_once(Evaluator_Context *ctx,
                                              String_View input,
                                              bool at_only,
                                              bool escape_quotes) {
    if (!ctx) return nob_sv_from_cstr("");
    Nob_String_Builder out = {0};

    for (size_t i = 0; i < input.count;) {
        if (!at_only && i + 3 < input.count && input.data[i] == '$' && input.data[i + 1] == '{') {
            size_t j = i + 2;
            while (j < input.count && input.data[j] != '}') j++;
            if (j < input.count && input.data[j] == '}') {
                String_View key = nob_sv_from_parts(input.data + i + 2, j - (i + 2));
                String_View val = eval_var_get(ctx, key);
                if (escape_quotes) {
                    for (size_t k = 0; k < val.count; k++) {
                        if (val.data[k] == '"') nob_sb_append(&out, '\\');
                        nob_sb_append(&out, val.data[k]);
                    }
                } else {
                    nob_sb_append_buf(&out, val.data, val.count);
                }
                i = j + 1;
                continue;
            }
        }
        if (i + 2 < input.count && input.data[i] == '@') {
            size_t j = i + 1;
            while (j < input.count && input.data[j] != '@') j++;
            if (j < input.count && input.data[j] == '@' && j > i + 1) {
                String_View key = nob_sv_from_parts(input.data + i + 1, j - (i + 1));
                String_View val = eval_var_get(ctx, key);
                if (escape_quotes) {
                    for (size_t k = 0; k < val.count; k++) {
                        if (val.data[k] == '"') nob_sb_append(&out, '\\');
                        nob_sb_append(&out, val.data[k]);
                    }
                } else {
                    nob_sb_append_buf(&out, val.data, val.count);
                }
                i = j + 1;
                continue;
            }
        }

        nob_sb_append(&out, input.data[i]);
        i++;
    }

    nob_sb_append_null(&out);
    String_View r = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(out.items, out.count - 1));
    nob_sb_free(out);
    return r;
}

static String_View file_apply_newline_style(Evaluator_Context *ctx, String_View in, String_View style) {
    if (!ctx || style.count == 0) return in;
    const char *nl = "\n";
    if (eval_sv_eq_ci_lit(style, "DOS") || eval_sv_eq_ci_lit(style, "WIN32") || eval_sv_eq_ci_lit(style, "CRLF")) {
        nl = "\r\n";
    }

    Nob_String_Builder out = {0};
    for (size_t i = 0; i < in.count; i++) {
        if (in.data[i] == '\r') {
            if (i + 1 < in.count && in.data[i + 1] == '\n') i++;
            nob_sb_append_cstr(&out, nl);
            continue;
        }
        if (in.data[i] == '\n') {
            nob_sb_append_cstr(&out, nl);
            continue;
        }
        nob_sb_append(&out, in.data[i]);
    }
    nob_sb_append_null(&out);
    String_View r = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(out.items, out.count - 1));
    nob_sb_free(out);
    return r;
}

static bool handle_file_configure(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    String_View output = nob_sv_from_cstr("");
    String_View content = nob_sv_from_cstr("");
    String_View newline_style = nob_sv_from_cstr("");
    bool at_only = false;
    bool escape_quotes = false;

    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "OUTPUT") && i + 1 < args.count) {
            output = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "CONTENT") && i + 1 < args.count) {
            content = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "NEWLINE_STYLE") && i + 1 < args.count) {
            newline_style = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "@ONLY")) {
            at_only = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "ESCAPE_QUOTES")) {
            escape_quotes = true;
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(CONFIGURE) received unexpected argument"), args.items[i]);
            return true;
        }
    }

    if (output.count == 0 || content.count == 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CONFIGURE) requires OUTPUT and CONTENT"),
                       nob_sv_from_cstr("Usage: file(CONFIGURE OUTPUT <out> CONTENT <text> [@ONLY] [ESCAPE_QUOTES] [NEWLINE_STYLE <style>])"));
        return true;
    }

    String_View out_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                output,
                                eval_file_current_bin_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &out_path)) {
        return true;
    }

    String_View expanded = file_expand_configure_once(ctx, content, at_only, escape_quotes);
    expanded = file_apply_newline_style(ctx, expanded, newline_style);

    bool same = false;
    if (!file_same_content(ctx, out_path, expanded, &same)) return true;
    if (same) return true;

    if (!file_write_bytes(ctx, out_path, expanded.data, expanded.count)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CONFIGURE) failed to write OUTPUT"), out_path);
    }
    return true;
}

static bool file_copy_file_do(Evaluator_Context *ctx, String_View src, String_View dst, bool only_if_different) {
    if (!ctx) return false;
    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);

    if (only_if_different) {
        Nob_String_Builder a = {0};
        Nob_String_Builder b = {0};
        bool ra = nob_read_entire_file(src_c, &a);
        bool rb = nob_read_entire_file(dst_c, &b);
        if (ra && rb && a.count == b.count && (a.count == 0 || memcmp(a.items, b.items, a.count) == 0)) {
            nob_sb_free(a);
            nob_sb_free(b);
            return true;
        }
        nob_sb_free(a);
        nob_sb_free(b);
    }

    if (!eval_file_mkdir_p(ctx, svu_dirname(dst))) return false;
    return nob_copy_file(src_c, dst_c);
}

static bool handle_file_copy_file(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY_FILE) requires source and destination"),
                       nob_sv_from_cstr("Usage: file(COPY_FILE <old> <new> [RESULT <var>] [ONLY_IF_DIFFERENT] [INPUT_MAY_BE_RECENT])"));
        return true;
    }

    bool only_if_different = false;
    String_View result_var = nob_sv_from_cstr("");
    for (size_t i = 3; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "ONLY_IF_DIFFERENT")) {
            only_if_different = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "INPUT_MAY_BE_RECENT")) {
            // Accepted for parity; no extra behavior needed in evaluator backend.
        } else if (eval_sv_eq_ci_lit(args.items[i], "RESULT") && i + 1 < args.count) {
            result_var = args.items[++i];
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(COPY_FILE) received unexpected argument"), args.items[i]);
            return true;
        }
    }

    String_View src = nob_sv_from_cstr("");
    String_View dst = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args.items[1],
                                eval_file_current_src_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &src)) {
        return true;
    }
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args.items[2],
                                eval_file_current_bin_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &dst)) {
        return true;
    }

    bool ok = file_copy_file_do(ctx, src, dst, only_if_different);
    if (result_var.count > 0) {
        (void)eval_var_set(ctx, result_var, ok ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"));
        return true;
    }

    if (!ok) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY_FILE) failed"), src);
    }
    return true;
}

static bool file_touch_one(Evaluator_Context *ctx, String_View path, bool create) {
    if (!ctx) return false;
    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);

    struct stat st = {0};
    if (stat(p, &st) != 0) {
        if (!create) return true;
        if (!file_write_bytes(ctx, path, "", 0)) return false;
        if (stat(p, &st) != 0) return false;
    }

    struct utimbuf tb = {0};
    tb.actime = st.st_atime;
    tb.modtime = time(NULL);
    return utime(p, &tb) == 0;
}

static bool handle_file_touch(Evaluator_Context *ctx, const Node *node, SV_List args, bool nocreate) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 2) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nocreate ? nob_sv_from_cstr("file(TOUCH_NOCREATE) requires at least one file")
                                : nob_sv_from_cstr("file(TOUCH) requires at least one file"),
                       nocreate ? nob_sv_from_cstr("Usage: file(TOUCH_NOCREATE <file>...)")
                                : nob_sv_from_cstr("Usage: file(TOUCH <file>...)"));
        return true;
    }

    for (size_t i = 1; i < args.count; i++) {
        String_View path = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx,
                                    node,
                                    o,
                                    args.items[i],
                                    eval_file_current_bin_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE,
                                    &path)) {
            return true;
        }
        if (!file_touch_one(ctx, path, !nocreate)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nocreate ? nob_sv_from_cstr("file(TOUCH_NOCREATE) failed")
                                    : nob_sv_from_cstr("file(TOUCH) failed"),
                           path);
            return true;
        }
    }
    return true;
}

typedef struct {
    String_View resolved_var;
    String_View unresolved_var;
    String_View conflicts_prefix;
    SV_List executables;
    SV_List libraries;
    SV_List modules;
    SV_List directories;
    SV_List pre_include_regexes;
    SV_List pre_exclude_regexes;
    SV_List post_include_regexes;
    SV_List post_exclude_regexes;
    SV_List post_include_files;
    SV_List post_exclude_files;
} Runtime_Deps_Args;

static bool runtime_push_temp(Evaluator_Context *ctx, SV_List *list, String_View v) {
    return svu_list_push_temp(ctx, list, v);
}

static bool runtime_parse_lists(Evaluator_Context *ctx,
                                SV_List args,
                                size_t *idx,
                                SV_List *out,
                                const char **stop_tokens,
                                size_t stop_count) {
    while (*idx < args.count) {
        bool stop = false;
        for (size_t s = 0; s < stop_count; s++) {
            if (eval_sv_eq_ci_lit(args.items[*idx], stop_tokens[s])) {
                stop = true;
                break;
            }
        }
        if (stop) break;
        if (!runtime_push_temp(ctx, out, args.items[*idx])) return false;
        (*idx)++;
    }
    return true;
}

static int runtime_sv_cmp_qsort(const void *a, const void *b) {
    const String_View *sa = (const String_View *)a;
    const String_View *sb = (const String_View *)b;
    size_t n = sa->count < sb->count ? sa->count : sb->count;
    int c = (n > 0) ? memcmp(sa->data, sb->data, n) : 0;
    if (c != 0) return c;
    if (sa->count < sb->count) return -1;
    if (sa->count > sb->count) return 1;
    return 0;
}

static String_View runtime_trim_ascii(String_View sv) {
    size_t s = 0;
    size_t e = sv.count;
    while (s < e && (unsigned char)sv.data[s] <= ' ') s++;
    while (e > s && (unsigned char)sv.data[e - 1] <= ' ') e--;
    return nob_sv_from_parts(sv.data + s, e - s);
}

static String_View runtime_sv_basename(String_View path) {
    size_t p = path.count;
    while (p > 0 && path.data[p - 1] != '/' && path.data[p - 1] != '\\') p--;
    return nob_sv_from_parts(path.data + p, path.count - p);
}

static bool runtime_sv_eq(String_View a, String_View b) {
    return nob_sv_eq(a, b);
}

static bool runtime_list_contains(SV_List list, String_View v) {
    for (size_t i = 0; i < list.count; i++) {
        if (runtime_sv_eq(list.items[i], v)) return true;
    }
    return false;
}

static bool runtime_list_add_unique_noempty(Evaluator_Context *ctx, SV_List *list, String_View v) {
    if (!ctx || !list) return false;
    if (v.count == 0) return true;
    if (runtime_list_contains(*list, v)) return true;
    return svu_list_push_temp(ctx, list, v);
}

static bool runtime_sv_starts_with_lit(String_View sv, const char *lit) {
    if (!lit) return false;
    size_t n = strlen(lit);
    return sv.count >= n && memcmp(sv.data, lit, n) == 0;
}

#if !defined(_WIN32)
static bool runtime_run_capture(Evaluator_Context *ctx, char *const argv[], int *out_status_code, String_View *out_log) {
    if (!ctx || !argv || !out_status_code || !out_log) return false;
    *out_status_code = 1;
    *out_log = nob_sv_from_cstr("");

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        *out_log = nob_sv_from_cstr("failed to create pipe for runtime dependency backend");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        *out_log = nob_sv_from_cstr("failed to fork runtime dependency backend");
        return false;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        dprintf(STDERR_FILENO, "failed to execute backend: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);
    Nob_String_Builder sb = {0};
    for (;;) {
        char buf[512];
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n <= 0) break;
        nob_sb_append_buf(&sb, buf, (size_t)n);
    }
    close(pipefd[0]);

    int ws = 0;
    if (waitpid(pid, &ws, 0) < 0) {
        nob_sb_free(sb);
        *out_log = nob_sv_from_cstr("failed waiting runtime dependency backend");
        return false;
    }

    if (WIFEXITED(ws)) *out_status_code = WEXITSTATUS(ws);
    else *out_status_code = 1;

    *out_log = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items, sb.count));
    nob_sb_free(sb);
    return true;
}

static bool runtime_match_any_regex(Evaluator_Context *ctx,
                                    const Node *node,
                                    Cmake_Event_Origin o,
                                    SV_List regexes,
                                    String_View value,
                                    bool *out_match) {
    if (!ctx || !out_match) return false;
    *out_match = false;
    if (regexes.count == 0) return true;

    char *val_c = eval_sv_to_cstr_temp(ctx, value);
    EVAL_OOM_RETURN_IF_NULL(ctx, val_c, false);
    for (size_t i = 0; i < regexes.count; i++) {
        char *rx_c = eval_sv_to_cstr_temp(ctx, regexes.items[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, rx_c, false);
        regex_t re = {0};
        int rc = regcomp(&re, rx_c, REG_EXTENDED);
        if (rc != 0) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(GET_RUNTIME_DEPENDENCIES) invalid regex"),
                           regexes.items[i]);
            return false;
        }
        int m = regexec(&re, val_c, 0, NULL, 0);
        regfree(&re);
        if (m == 0) {
            *out_match = true;
            return true;
        }
    }
    return true;
}

static bool runtime_allow_pre(Evaluator_Context *ctx,
                              const Node *node,
                              Cmake_Event_Origin o,
                              const Runtime_Deps_Args *rd,
                              String_View dep_name,
                              bool *out_allow) {
    if (!ctx || !rd || !out_allow) return false;
    *out_allow = true;

    if (rd->pre_include_regexes.count > 0) {
        bool m = false;
        if (!runtime_match_any_regex(ctx, node, o, rd->pre_include_regexes, dep_name, &m)) return false;
        if (!m) {
            *out_allow = false;
            return true;
        }
    }

    if (rd->pre_exclude_regexes.count > 0) {
        bool m = false;
        if (!runtime_match_any_regex(ctx, node, o, rd->pre_exclude_regexes, dep_name, &m)) return false;
        if (m) {
            *out_allow = false;
            return true;
        }
    }
    return true;
}

static bool runtime_allow_post(Evaluator_Context *ctx,
                               const Node *node,
                               Cmake_Event_Origin o,
                               const Runtime_Deps_Args *rd,
                               String_View resolved_path,
                               bool *out_allow) {
    if (!ctx || !rd || !out_allow) return false;
    *out_allow = true;

    bool include_gate = (rd->post_include_regexes.count > 0 || rd->post_include_files.count > 0);
    if (include_gate) {
        bool matched = false;
        if (runtime_list_contains(rd->post_include_files, resolved_path)) matched = true;
        if (!matched && rd->post_include_regexes.count > 0) {
            if (!runtime_match_any_regex(ctx, node, o, rd->post_include_regexes, resolved_path, &matched)) return false;
        }
        if (!matched) {
            *out_allow = false;
            return true;
        }
    }

    if (runtime_list_contains(rd->post_exclude_files, resolved_path)) {
        *out_allow = false;
        return true;
    }
    if (rd->post_exclude_regexes.count > 0) {
        bool m = false;
        if (!runtime_match_any_regex(ctx, node, o, rd->post_exclude_regexes, resolved_path, &m)) return false;
        if (m) {
            *out_allow = false;
            return true;
        }
    }
    return true;
}

static bool runtime_try_resolve_in_dirs(Evaluator_Context *ctx, String_View name, SV_List dirs, String_View *out_path) {
    if (!ctx || !out_path) return false;
    *out_path = nob_sv_from_cstr("");
    if (name.count == 0) return true;
    for (size_t i = 0; i < dirs.count; i++) {
        String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dirs.items[i], name);
        char *cand_c = eval_sv_to_cstr_temp(ctx, candidate);
        EVAL_OOM_RETURN_IF_NULL(ctx, cand_c, false);
        struct stat st = {0};
        if (stat(cand_c, &st) == 0 && S_ISREG(st.st_mode)) {
            *out_path = candidate;
            return true;
        }
    }
    return true;
}

static bool runtime_collect_ldd_deps(Evaluator_Context *ctx,
                                     const Node *node,
                                     Cmake_Event_Origin o,
                                     String_View file_path,
                                     const Runtime_Deps_Args *rd,
                                     SV_List resolved_dirs,
                                     SV_List *queue,
                                     SV_List *processed,
                                     SV_List *resolved,
                                     SV_List *unresolved) {
    if (!ctx || !queue || !processed || !resolved || !unresolved || !rd) return false;

    if (!runtime_list_add_unique_noempty(ctx, processed, file_path)) return false;

    char *file_c = eval_sv_to_cstr_temp(ctx, file_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, file_c, false);
    char *argv[] = {"ldd", file_c, NULL};

    int status_code = 1;
    String_View log = nob_sv_from_cstr("");
    if (!runtime_run_capture(ctx, argv, &status_code, &log)) return false;
    if (status_code != 0 && log.count == 0) return true;

    size_t pos = 0;
    while (pos < log.count) {
        size_t start = pos;
        while (pos < log.count && log.data[pos] != '\n') pos++;
        String_View line = nob_sv_from_parts(log.data + start, pos - start);
        line = runtime_trim_ascii(line);
        if (pos < log.count && log.data[pos] == '\n') pos++;
        if (line.count == 0) continue;
        if (runtime_sv_starts_with_lit(line, "statically linked")) continue;
        if (runtime_sv_starts_with_lit(line, "not a dynamic executable")) continue;

        String_View dep_name = nob_sv_from_cstr("");
        String_View dep_path = nob_sv_from_cstr("");
        bool dep_missing = false;

        size_t arrow_pos = line.count;
        for (size_t k = 0; k + 1 < line.count; k++) {
            if (line.data[k] == '=' && line.data[k + 1] == '>') {
                arrow_pos = k;
                break;
            }
        }
        if (arrow_pos < line.count) {
            size_t left_n = arrow_pos;
            dep_name = runtime_trim_ascii(nob_sv_from_parts(line.data, left_n));
            String_View right = runtime_trim_ascii(nob_sv_from_parts(line.data + arrow_pos + 2, line.count - left_n - 2));
            if (runtime_sv_starts_with_lit(right, "not found")) {
                dep_missing = true;
            } else {
                size_t tok = 0;
                while (tok < right.count && right.data[tok] != ' ' && right.data[tok] != '\t' && right.data[tok] != '(') tok++;
                dep_path = nob_sv_from_parts(right.data, tok);
            }
        } else if (line.count > 0 && line.data[0] == '/') {
            size_t tok = 0;
            while (tok < line.count && line.data[tok] != ' ' && line.data[tok] != '\t' && line.data[tok] != '(') tok++;
            dep_path = nob_sv_from_parts(line.data, tok);
            dep_name = runtime_sv_basename(dep_path);
        } else {
            continue;
        }

        dep_name = runtime_trim_ascii(dep_name);
        dep_path = runtime_trim_ascii(dep_path);

        bool allow_pre = true;
        if (!runtime_allow_pre(ctx, node, o, rd, dep_name, &allow_pre)) return false;
        if (!allow_pre) continue;

        if (dep_missing) {
            String_View resolved_from_dir = nob_sv_from_cstr("");
            if (!runtime_try_resolve_in_dirs(ctx, dep_name, resolved_dirs, &resolved_from_dir)) return false;
            if (resolved_from_dir.count > 0) {
                dep_path = resolved_from_dir;
                dep_missing = false;
            }
        }

        if (dep_missing || dep_path.count == 0) {
            if (!runtime_list_add_unique_noempty(ctx, unresolved, dep_name)) return false;
            continue;
        }

        bool allow_post = true;
        if (!runtime_allow_post(ctx, node, o, rd, dep_path, &allow_post)) return false;
        if (!allow_post) continue;

        if (!runtime_list_add_unique_noempty(ctx, resolved, dep_path)) return false;
        if (!runtime_list_contains(*processed, dep_path) && !runtime_list_contains(*queue, dep_path)) {
            if (!svu_list_push_temp(ctx, queue, dep_path)) return false;
        }
    }

    return true;
}
#endif

static bool handle_file_get_runtime_dependencies(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    Runtime_Deps_Args rd = {0};

    static const char *k_stops[] = {
        "RESOLVED_DEPENDENCIES_VAR",
        "UNRESOLVED_DEPENDENCIES_VAR",
        "CONFLICTING_DEPENDENCIES_PREFIX",
        "EXECUTABLES",
        "LIBRARIES",
        "MODULES",
        "DIRECTORIES",
        "PRE_INCLUDE_REGEXES",
        "PRE_EXCLUDE_REGEXES",
        "POST_INCLUDE_REGEXES",
        "POST_EXCLUDE_REGEXES",
        "POST_INCLUDE_FILES",
        "POST_EXCLUDE_FILES",
    };

    for (size_t i = 1; i < args.count;) {
        if (eval_sv_eq_ci_lit(args.items[i], "RESOLVED_DEPENDENCIES_VAR") && i + 1 < args.count) {
            rd.resolved_var = args.items[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "UNRESOLVED_DEPENDENCIES_VAR") && i + 1 < args.count) {
            rd.unresolved_var = args.items[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "CONFLICTING_DEPENDENCIES_PREFIX") && i + 1 < args.count) {
            rd.conflicts_prefix = args.items[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "EXECUTABLES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.executables, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "LIBRARIES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.libraries, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "MODULES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.modules, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "DIRECTORIES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.directories, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "PRE_INCLUDE_REGEXES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.pre_include_regexes, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "PRE_EXCLUDE_REGEXES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.pre_exclude_regexes, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "POST_INCLUDE_REGEXES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_include_regexes, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "POST_EXCLUDE_REGEXES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_exclude_regexes, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "POST_INCLUDE_FILES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_include_files, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "POST_EXCLUDE_FILES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_exclude_files, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }

        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GET_RUNTIME_DEPENDENCIES) received unexpected argument"),
                       args.items[i]);
        return true;
    }

    if (rd.resolved_var.count == 0 && rd.unresolved_var.count == 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GET_RUNTIME_DEPENDENCIES) requires RESOLVED_DEPENDENCIES_VAR or UNRESOLVED_DEPENDENCIES_VAR"),
                       nob_sv_from_cstr("Provide at least one output variable"));
        return true;
    }

#if defined(_WIN32)
    // Linux-first implementation: Windows remains deterministic no-op for now.
    if (rd.resolved_var.count > 0) (void)eval_var_set(ctx, rd.resolved_var, nob_sv_from_cstr(""));
    if (rd.unresolved_var.count > 0) (void)eval_var_set(ctx, rd.unresolved_var, nob_sv_from_cstr(""));
    if (rd.conflicts_prefix.count > 0) {
        Nob_String_Builder key = {0};
        nob_sb_append_buf(&key, rd.conflicts_prefix.data, rd.conflicts_prefix.count);
        nob_sb_append_cstr(&key, "_FILENAMES");
        nob_sb_append_null(&key);
        (void)eval_var_set(ctx, nob_sv_from_cstr(key.items), nob_sv_from_cstr(""));
        nob_sb_free(key);
    }
    return true;
#else
    SV_List resolved_dirs = {0};
    for (size_t i = 0; i < rd.directories.count; i++) {
        String_View dir = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx,
                                    node,
                                    o,
                                    rd.directories.items[i],
                                    eval_file_current_src_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE,
                                    &dir)) {
            return true;
        }
        if (!runtime_list_add_unique_noempty(ctx, &resolved_dirs, dir)) return true;
    }

    SV_List queue = {0};
    SV_List processed = {0};
    SV_List resolved = {0};
    SV_List unresolved = {0};

    for (size_t i = 0; i < rd.executables.count; i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx, node, o, rd.executables.items[i], eval_file_current_src_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE, &p)) return true;
        if (!runtime_list_add_unique_noempty(ctx, &queue, p)) return true;
    }
    for (size_t i = 0; i < rd.libraries.count; i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx, node, o, rd.libraries.items[i], eval_file_current_src_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE, &p)) return true;
        if (!runtime_list_add_unique_noempty(ctx, &queue, p)) return true;
    }
    for (size_t i = 0; i < rd.modules.count; i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx, node, o, rd.modules.items[i], eval_file_current_src_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE, &p)) return true;
        if (!runtime_list_add_unique_noempty(ctx, &queue, p)) return true;
    }

    for (size_t qi = 0; qi < queue.count; qi++) {
        String_View seed = queue.items[qi];
        if (!runtime_collect_ldd_deps(ctx, node, o, seed, &rd, resolved_dirs, &queue, &processed, &resolved, &unresolved)) {
            return true;
        }
    }

    if (resolved.count > 1) qsort(resolved.items, resolved.count, sizeof(String_View), runtime_sv_cmp_qsort);
    if (unresolved.count > 1) qsort(unresolved.items, unresolved.count, sizeof(String_View), runtime_sv_cmp_qsort);

    if (rd.resolved_var.count > 0) {
        String_View joined = (resolved.count > 0) ? eval_sv_join_semi_temp(ctx, resolved.items, resolved.count)
                                                  : nob_sv_from_cstr("");
        (void)eval_var_set(ctx, rd.resolved_var, joined);
    }
    if (rd.unresolved_var.count > 0) {
        String_View joined = (unresolved.count > 0) ? eval_sv_join_semi_temp(ctx, unresolved.items, unresolved.count)
                                                    : nob_sv_from_cstr("");
        (void)eval_var_set(ctx, rd.unresolved_var, joined);
    }

    if (rd.conflicts_prefix.count > 0) {
        SV_List conflict_names = {0};
        for (size_t i = 0; i < resolved.count; i++) {
            String_View bi = runtime_sv_basename(resolved.items[i]);
            SV_List paths_for_name = {0};
            for (size_t j = 0; j < resolved.count; j++) {
                if (runtime_sv_eq(runtime_sv_basename(resolved.items[j]), bi)) {
                    if (!runtime_list_add_unique_noempty(ctx, &paths_for_name, resolved.items[j])) return true;
                }
            }
            if (paths_for_name.count > 1) {
                if (!runtime_list_add_unique_noempty(ctx, &conflict_names, bi)) return true;

                if (paths_for_name.count > 1) qsort(paths_for_name.items, paths_for_name.count, sizeof(String_View), runtime_sv_cmp_qsort);
                String_View val = eval_sv_join_semi_temp(ctx, paths_for_name.items, paths_for_name.count);

                Nob_String_Builder key = {0};
                nob_sb_append_buf(&key, rd.conflicts_prefix.data, rd.conflicts_prefix.count);
                nob_sb_append(&key, '_');
                nob_sb_append_buf(&key, bi.data, bi.count);
                nob_sb_append_null(&key);
                (void)eval_var_set(ctx, nob_sv_from_cstr(key.items), val);
                nob_sb_free(key);
            }
        }

        if (conflict_names.count > 1) qsort(conflict_names.items, conflict_names.count, sizeof(String_View), runtime_sv_cmp_qsort);
        Nob_String_Builder key = {0};
        nob_sb_append_buf(&key, rd.conflicts_prefix.data, rd.conflicts_prefix.count);
        nob_sb_append_cstr(&key, "_FILENAMES");
        nob_sb_append_null(&key);
        String_View joined = (conflict_names.count > 0)
                                 ? eval_sv_join_semi_temp(ctx, conflict_names.items, conflict_names.count)
                                 : nob_sv_from_cstr("");
        (void)eval_var_set(ctx, nob_sv_from_cstr(key.items), joined);
        nob_sb_free(key);
    }
#endif
    return true;
}

bool eval_file_handle_extra(Evaluator_Context *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || args.count == 0) return false;

    if (eval_hash_is_supported_algo(args.items[0])) return handle_file_hash(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "CONFIGURE")) return handle_file_configure(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "COPY_FILE")) return handle_file_copy_file(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "TOUCH")) return handle_file_touch(ctx, node, args, false);
    if (eval_sv_eq_ci_lit(args.items[0], "TOUCH_NOCREATE")) return handle_file_touch(ctx, node, args, true);
    if (eval_sv_eq_ci_lit(args.items[0], "GET_RUNTIME_DEPENDENCIES")) {
        return handle_file_get_runtime_dependencies(ctx, node, args);
    }

    return false;
}
