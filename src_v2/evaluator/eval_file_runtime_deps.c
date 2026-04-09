#include "eval_file_internal.h"

#include "sv_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <regex.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

static bool runtime_push_temp(EvalExecContext *ctx, SV_List *list, String_View v) {
    return svu_list_push_temp(ctx, list, v);
}

static bool runtime_parse_lists(EvalExecContext *ctx,
                                SV_List args,
                                size_t *idx,
                                SV_List *out,
                                const char **stop_tokens,
                                size_t stop_count) {
    while (*idx < arena_arr_len(args)) {
        bool stop = false;
        for (size_t s = 0; s < stop_count; s++) {
            if (eval_sv_eq_ci_lit(args[*idx], stop_tokens[s])) {
                stop = true;
                break;
            }
        }
        if (stop) break;
        if (!runtime_push_temp(ctx, out, args[*idx])) return false;
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
    for (size_t i = 0; i < arena_arr_len(list); i++) {
        if (runtime_sv_eq(list[i], v)) return true;
    }
    return false;
}

static bool runtime_list_add_unique_noempty(EvalExecContext *ctx, SV_List *list, String_View v) {
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
static bool runtime_run_capture(EvalExecContext *ctx, char *const argv[], int *out_status_code, String_View *out_log) {
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

static bool runtime_match_any_regex(EvalExecContext *ctx,
                                    const Node *node,
                                    Cmake_Event_Origin o,
                                    SV_List regexes,
                                    String_View value,
                                    bool *out_match) {
    if (!ctx || !out_match) return false;
    *out_match = false;
    if (arena_arr_len(regexes) == 0) return true;

    char *val_c = eval_sv_to_cstr_temp(ctx, value);
    EVAL_OOM_RETURN_IF_NULL(ctx, val_c, false);
    for (size_t i = 0; i < arena_arr_len(regexes); i++) {
        char *rx_c = eval_sv_to_cstr_temp(ctx, regexes[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, rx_c, false);
        regex_t re = {0};
        int rc = regcomp(&re, rx_c, REG_EXTENDED);
        if (rc != 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(GET_RUNTIME_DEPENDENCIES) invalid regex"), regexes[i]);
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

static bool runtime_allow_pre(EvalExecContext *ctx,
                              const Node *node,
                              Cmake_Event_Origin o,
                              const Runtime_Deps_Args *rd,
                              String_View dep_name,
                              bool *out_allow) {
    if (!ctx || !rd || !out_allow) return false;
    *out_allow = true;

    if (arena_arr_len(rd->pre_include_regexes) > 0) {
        bool m = false;
        if (!runtime_match_any_regex(ctx, node, o, rd->pre_include_regexes, dep_name, &m)) return false;
        if (!m) {
            *out_allow = false;
            return true;
        }
    }

    if (arena_arr_len(rd->pre_exclude_regexes) > 0) {
        bool m = false;
        if (!runtime_match_any_regex(ctx, node, o, rd->pre_exclude_regexes, dep_name, &m)) return false;
        if (m) {
            *out_allow = false;
            return true;
        }
    }
    return true;
}

static bool runtime_allow_post(EvalExecContext *ctx,
                               const Node *node,
                               Cmake_Event_Origin o,
                               const Runtime_Deps_Args *rd,
                               String_View resolved_path,
                               bool *out_allow) {
    if (!ctx || !rd || !out_allow) return false;
    *out_allow = true;

    bool include_gate = (arena_arr_len(rd->post_include_regexes) > 0 || arena_arr_len(rd->post_include_files) > 0);
    if (include_gate) {
        bool matched = false;
        if (runtime_list_contains(rd->post_include_files, resolved_path)) matched = true;
        if (!matched && arena_arr_len(rd->post_include_regexes) > 0) {
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
    if (arena_arr_len(rd->post_exclude_regexes) > 0) {
        bool m = false;
        if (!runtime_match_any_regex(ctx, node, o, rd->post_exclude_regexes, resolved_path, &m)) return false;
        if (m) {
            *out_allow = false;
            return true;
        }
    }
    return true;
}

static bool runtime_try_resolve_in_dirs(EvalExecContext *ctx, String_View name, SV_List dirs, String_View *out_path) {
    if (!ctx || !out_path) return false;
    *out_path = nob_sv_from_cstr("");
    if (name.count == 0) return true;
    for (size_t i = 0; i < arena_arr_len(dirs); i++) {
        String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dirs[i], name);
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

static bool runtime_collect_ldd_deps(EvalExecContext *ctx,
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

bool eval_file_handle_runtime_dependencies(EvalExecContext *ctx, const Node *node, SV_List args) {
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

    for (size_t i = 1; i < arena_arr_len(args);) {
        if (eval_sv_eq_ci_lit(args[i], "RESOLVED_DEPENDENCIES_VAR") && i + 1 < arena_arr_len(args)) {
            rd.resolved_var = args[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "UNRESOLVED_DEPENDENCIES_VAR") && i + 1 < arena_arr_len(args)) {
            rd.unresolved_var = args[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "CONFLICTING_DEPENDENCIES_PREFIX") && i + 1 < arena_arr_len(args)) {
            rd.conflicts_prefix = args[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "EXECUTABLES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.executables, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "LIBRARIES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.libraries, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "MODULES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.modules, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "DIRECTORIES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.directories, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "PRE_INCLUDE_REGEXES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.pre_include_regexes, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "PRE_EXCLUDE_REGEXES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.pre_exclude_regexes, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "POST_INCLUDE_REGEXES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_include_regexes, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "POST_EXCLUDE_REGEXES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_exclude_regexes, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "POST_INCLUDE_FILES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_include_files, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "POST_EXCLUDE_FILES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_exclude_files, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }

        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_file", nob_sv_from_cstr("file(GET_RUNTIME_DEPENDENCIES) received unexpected argument"), args[i]);
        return true;
    }

    if (rd.resolved_var.count == 0 && rd.unresolved_var.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(GET_RUNTIME_DEPENDENCIES) requires RESOLVED_DEPENDENCIES_VAR or UNRESOLVED_DEPENDENCIES_VAR"), nob_sv_from_cstr("Provide at least one output variable"));
        return true;
    }

    {
        String_View replay_key = nob_sv_from_cstr("");
        (void)eval_begin_replay_action(ctx,
                                       o,
                                       EVENT_REPLAY_ACTION_PROBE,
                                       EVENT_REPLAY_OPCODE_NONE,
                                       EVENT_REPLAY_PHASE_CONFIGURE,
                                       eval_current_binary_dir(ctx),
                                       &replay_key);
    }

#if defined(_WIN32)
    if (rd.resolved_var.count > 0) (void)eval_var_set_current(ctx, rd.resolved_var, nob_sv_from_cstr(""));
    if (rd.unresolved_var.count > 0) (void)eval_var_set_current(ctx, rd.unresolved_var, nob_sv_from_cstr(""));
    if (rd.conflicts_prefix.count > 0) {
        Nob_String_Builder key = {0};
        nob_sb_append_buf(&key, rd.conflicts_prefix.data, rd.conflicts_prefix.count);
        nob_sb_append_cstr(&key, "_FILENAMES");
        nob_sb_append_null(&key);
        (void)eval_var_set_current(ctx, nob_sv_from_cstr(key.items), nob_sv_from_cstr(""));
        nob_sb_free(key);
    }
    return true;
#else
    SV_List resolved_dirs = NULL;
    for (size_t i = 0; i < arena_arr_len(rd.directories); i++) {
        String_View dir = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx,
                                    node,
                                    o,
                                    rd.directories[i],
                                    eval_file_current_src_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE,
                                    &dir)) {
            return true;
        }
        if (!runtime_list_add_unique_noempty(ctx, &resolved_dirs, dir)) return true;
    }

    SV_List queue = NULL;
    SV_List processed = NULL;
    SV_List resolved = NULL;
    SV_List unresolved = NULL;

    for (size_t i = 0; i < arena_arr_len(rd.executables); i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx, node, o, rd.executables[i], eval_file_current_src_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE, &p)) return true;
        if (!runtime_list_add_unique_noempty(ctx, &queue, p)) return true;
    }
    for (size_t i = 0; i < arena_arr_len(rd.libraries); i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx, node, o, rd.libraries[i], eval_file_current_src_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE, &p)) return true;
        if (!runtime_list_add_unique_noempty(ctx, &queue, p)) return true;
    }
    for (size_t i = 0; i < arena_arr_len(rd.modules); i++) {
        String_View p = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx, node, o, rd.modules[i], eval_file_current_src_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE, &p)) return true;
        if (!runtime_list_add_unique_noempty(ctx, &queue, p)) return true;
    }

    for (size_t qi = 0; qi < arena_arr_len(queue); qi++) {
        String_View seed = queue[qi];
        if (!runtime_collect_ldd_deps(ctx, node, o, seed, &rd, resolved_dirs, &queue, &processed, &resolved, &unresolved)) {
            return true;
        }
    }

    if (arena_arr_len(resolved) > 1) qsort(resolved, arena_arr_len(resolved), sizeof(String_View), runtime_sv_cmp_qsort);
    if (arena_arr_len(unresolved) > 1) qsort(unresolved, arena_arr_len(unresolved), sizeof(String_View), runtime_sv_cmp_qsort);

    if (rd.resolved_var.count > 0) {
        String_View joined = (arena_arr_len(resolved) > 0) ? eval_sv_join_semi_temp(ctx, resolved, arena_arr_len(resolved))
                                                           : nob_sv_from_cstr("");
        (void)eval_var_set_current(ctx, rd.resolved_var, joined);
    }
    if (rd.unresolved_var.count > 0) {
        String_View joined = (arena_arr_len(unresolved) > 0) ? eval_sv_join_semi_temp(ctx, unresolved, arena_arr_len(unresolved))
                                                             : nob_sv_from_cstr("");
        (void)eval_var_set_current(ctx, rd.unresolved_var, joined);
    }

    if (rd.conflicts_prefix.count > 0) {
        SV_List conflict_names = NULL;
        for (size_t i = 0; i < arena_arr_len(resolved); i++) {
            String_View bi = runtime_sv_basename(resolved[i]);
            SV_List paths_for_name = NULL;
            for (size_t j = 0; j < arena_arr_len(resolved); j++) {
                if (runtime_sv_eq(runtime_sv_basename(resolved[j]), bi)) {
                    if (!runtime_list_add_unique_noempty(ctx, &paths_for_name, resolved[j])) return true;
                }
            }
            if (arena_arr_len(paths_for_name) > 1) {
                if (!runtime_list_add_unique_noempty(ctx, &conflict_names, bi)) return true;

                if (arena_arr_len(paths_for_name) > 1) qsort(paths_for_name, arena_arr_len(paths_for_name), sizeof(String_View), runtime_sv_cmp_qsort);
                String_View val = eval_sv_join_semi_temp(ctx, paths_for_name, arena_arr_len(paths_for_name));

                Nob_String_Builder key = {0};
                nob_sb_append_buf(&key, rd.conflicts_prefix.data, rd.conflicts_prefix.count);
                nob_sb_append(&key, '_');
                nob_sb_append_buf(&key, bi.data, bi.count);
                nob_sb_append_null(&key);
                (void)eval_var_set_current(ctx, nob_sv_from_cstr(key.items), val);
                nob_sb_free(key);
            }
        }

        if (arena_arr_len(conflict_names) > 1) qsort(conflict_names, arena_arr_len(conflict_names), sizeof(String_View), runtime_sv_cmp_qsort);
        Nob_String_Builder key = {0};
        nob_sb_append_buf(&key, rd.conflicts_prefix.data, rd.conflicts_prefix.count);
        nob_sb_append_cstr(&key, "_FILENAMES");
        nob_sb_append_null(&key);
        String_View joined = (arena_arr_len(conflict_names) > 0)
                                 ? eval_sv_join_semi_temp(ctx, conflict_names, arena_arr_len(conflict_names))
                                 : nob_sv_from_cstr("");
        (void)eval_var_set_current(ctx, nob_sv_from_cstr(key.items), joined);
        nob_sb_free(key);
    }
#endif
    return true;
}
