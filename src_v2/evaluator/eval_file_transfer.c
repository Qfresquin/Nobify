#include "eval_file_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef struct {
    String_View status_var;
    String_View log_var;
    bool has_range_start;
    bool has_range_end;
    size_t range_start;
    size_t range_end;
    bool has_timeout;
    bool has_inactivity_timeout;
    size_t timeout_sec;
    size_t inactivity_timeout_sec;
    String_View expected_hash;
    String_View expected_md5;
} File_Transfer_Options;

static bool file_transfer_is_remote_url(String_View in) {
    if (in.count == 0) return false;
    for (size_t i = 0; i + 2 < in.count; i++) {
        if (in.data[i] == ':' && in.data[i + 1] == '/' && in.data[i + 2] == '/') {
            String_View scheme = nob_sv_from_parts(in.data, i);
            if (eval_sv_eq_ci_lit(scheme, "file")) return false;
            return true;
        }
    }
    return false;
}

static String_View file_transfer_local_path_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx) return nob_sv_from_cstr("");
    if (in.count >= 7 && (memcmp(in.data, "file://", 7) == 0 || memcmp(in.data, "FILE://", 7) == 0)) {
        return nob_sv_from_parts(in.data + 7, in.count - 7);
    }
    return in;
}

static String_View file_transfer_trim_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx) return nob_sv_from_cstr("");
    size_t begin = 0;
    size_t end = in.count;
    while (begin < end && isspace((unsigned char)in.data[begin])) begin++;
    while (end > begin && isspace((unsigned char)in.data[end - 1])) end--;
    return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(in.data + begin, end - begin));
}

static String_View file_transfer_first_line_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx) return nob_sv_from_cstr("");
    size_t end = 0;
    while (end < in.count && in.data[end] != '\n' && in.data[end] != '\r') end++;
    return file_transfer_trim_temp(ctx, nob_sv_from_parts(in.data, end));
}

static void file_transfer_set_status_sv(Evaluator_Context *ctx, String_View status_var, int code, String_View msg) {
    if (!ctx || status_var.count == 0) return;
    const int mlen = (msg.count > (size_t)INT32_MAX) ? INT32_MAX : (int)msg.count;
    (void)eval_var_set(ctx, status_var, nob_sv_from_cstr(nob_temp_sprintf("%d;%.*s", code, mlen, msg.data ? msg.data : "")));
}

static void file_transfer_set_log_sv(Evaluator_Context *ctx, String_View log_var, String_View msg) {
    if (!ctx || log_var.count == 0) return;
    (void)eval_var_set(ctx, log_var, msg);
}

static void file_transfer_set_success(Evaluator_Context *ctx, const File_Transfer_Options *opt, String_View log_msg) {
    if (!ctx || !opt) return;
    file_transfer_set_status_sv(ctx, opt->status_var, 0, nob_sv_from_cstr("No error"));
    file_transfer_set_log_sv(ctx, opt->log_var, log_msg);
}

static void file_transfer_fail(Evaluator_Context *ctx,
                               const Node *node,
                               Cmake_Event_Origin o,
                               const File_Transfer_Options *opt,
                               int status_code,
                               String_View status_message,
                               String_View log_message,
                               String_View cause,
                               String_View hint) {
    if (!ctx || !node || !opt) return;
    String_View msg = status_message;
    if (msg.count == 0) msg = (status_code == 0) ? nob_sv_from_cstr("No error") : nob_sv_from_cstr("transfer failed");
    file_transfer_set_status_sv(ctx, opt->status_var, status_code, msg);
    file_transfer_set_log_sv(ctx, opt->log_var, log_message);
    if (opt->status_var.count > 0) return;

    eval_emit_diag(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("eval_file"),
                   node->as.cmd.name,
                   o,
                   cause,
                   hint.count > 0 ? hint : msg);
}

static bool file_transfer_parse_options(Evaluator_Context *ctx,
                                        const Node *node,
                                        SV_List args,
                                        size_t start,
                                        File_Transfer_Options *out) {
    if (!ctx || !node || !out) return false;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    memset(out, 0, sizeof(*out));

    for (size_t i = start; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "STATUS")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file() STATUS requires an output variable"), args.items[i]);
                return false;
            }
            out->status_var = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "LOG")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file() LOG requires an output variable"), args.items[i]);
                return false;
            }
            out->log_var = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "RANGE_START")) {
            if (i + 1 >= args.count || !eval_file_parse_size_sv(args.items[i + 1], &out->range_start)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(DOWNLOAD) invalid RANGE_START"),
                               (i + 1 < args.count) ? args.items[i + 1] : args.items[i]);
                return false;
            }
            out->has_range_start = true;
            i++;
        } else if (eval_sv_eq_ci_lit(args.items[i], "RANGE_END")) {
            if (i + 1 >= args.count || !eval_file_parse_size_sv(args.items[i + 1], &out->range_end)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(DOWNLOAD) invalid RANGE_END"),
                               (i + 1 < args.count) ? args.items[i + 1] : args.items[i]);
                return false;
            }
            out->has_range_end = true;
            i++;
        } else if (eval_sv_eq_ci_lit(args.items[i], "TIMEOUT")) {
            if (i + 1 >= args.count || !eval_file_parse_size_sv(args.items[i + 1], &out->timeout_sec)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file() invalid TIMEOUT"),
                               (i + 1 < args.count) ? args.items[i + 1] : args.items[i]);
                return false;
            }
            out->has_timeout = true;
            i++;
        } else if (eval_sv_eq_ci_lit(args.items[i], "INACTIVITY_TIMEOUT")) {
            if (i + 1 >= args.count || !eval_file_parse_size_sv(args.items[i + 1], &out->inactivity_timeout_sec)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file() invalid INACTIVITY_TIMEOUT"),
                               (i + 1 < args.count) ? args.items[i + 1] : args.items[i]);
                return false;
            }
            out->has_inactivity_timeout = true;
            i++;
        } else if (eval_sv_eq_ci_lit(args.items[i], "EXPECTED_HASH")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(DOWNLOAD) EXPECTED_HASH requires a value"), args.items[i]);
                return false;
            }
            out->expected_hash = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "EXPECTED_MD5")) {
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(DOWNLOAD) EXPECTED_MD5 requires a value"), args.items[i]);
                return false;
            }
            out->expected_md5 = args.items[++i];
        }
    }
    return true;
}

#if !defined(_WIN32)
static bool file_transfer_run_capture(Evaluator_Context *ctx,
                                      char *const argv[],
                                      int *out_status_code,
                                      String_View *out_log) {
    if (!ctx || !argv || !out_status_code || !out_log) return false;
    *out_status_code = 1;
    *out_log = nob_sv_from_cstr("");

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        *out_log = nob_sv_from_cstr("failed to create pipe for curl backend");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        *out_log = nob_sv_from_cstr("failed to fork curl backend process");
        return false;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        dprintf(STDERR_FILENO, "failed to execute curl backend: %s\n", strerror(errno));
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

    int wait_status = 0;
    if (waitpid(pid, &wait_status, 0) < 0) {
        nob_sb_free(sb);
        *out_log = nob_sv_from_cstr("failed to wait curl backend process");
        return false;
    }

    nob_sb_append_null(&sb);

    String_View raw_log = nob_sv_from_parts(sb.items, sb.count > 0 ? sb.count - 1 : 0);
    *out_log = sv_copy_to_temp_arena(ctx, raw_log);
    nob_sb_free(sb);
    if (ctx->oom) return false;

    if (WIFEXITED(wait_status)) {
        *out_status_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        *out_status_code = 128 + WTERMSIG(wait_status);
    } else {
        *out_status_code = 1;
    }
    return true;
}
#else
static bool file_transfer_run_capture(Evaluator_Context *ctx,
                                      char *const argv[],
                                      int *out_status_code,
                                      String_View *out_log) {
    (void)ctx;
    (void)argv;
    if (!out_status_code || !out_log) return false;
    *out_status_code = 1;
    *out_log = nob_sv_from_cstr("remote transfer backend via curl is not implemented on Windows");
    return false;
}
#endif

static bool file_transfer_remote_download(Evaluator_Context *ctx,
                                          const Node *node,
                                          Cmake_Event_Origin o,
                                          String_View url,
                                          String_View dst,
                                          const File_Transfer_Options *opt,
                                          int *out_status_code,
                                          String_View *out_log) {
    (void)node;
    (void)o;
    if (!ctx || !opt || !out_status_code || !out_log) return false;

    char *url_c = eval_sv_to_cstr_temp(ctx, url);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, url_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);

    char timeout_buf[64] = {0};
    char inactivity_buf[64] = {0};
    char range_buf[96] = {0};

    char *argv[24] = {0};
    size_t argc = 0;
    argv[argc++] = "curl";
    argv[argc++] = "--silent";
    argv[argc++] = "--show-error";
    argv[argc++] = "--location";
    argv[argc++] = "--output";
    argv[argc++] = dst_c;

    if (opt->has_timeout) {
        snprintf(timeout_buf, sizeof(timeout_buf), "%zu", opt->timeout_sec);
        argv[argc++] = "--max-time";
        argv[argc++] = timeout_buf;
    }
    if (opt->has_inactivity_timeout) {
        snprintf(inactivity_buf, sizeof(inactivity_buf), "%zu", opt->inactivity_timeout_sec);
        argv[argc++] = "--speed-time";
        argv[argc++] = inactivity_buf;
        argv[argc++] = "--speed-limit";
        argv[argc++] = "1";
    }
    if (opt->has_range_start || opt->has_range_end) {
        if (opt->has_range_start && opt->has_range_end) {
            snprintf(range_buf, sizeof(range_buf), "%zu-%zu", opt->range_start, opt->range_end);
        } else if (opt->has_range_start) {
            snprintf(range_buf, sizeof(range_buf), "%zu-", opt->range_start);
        } else {
            snprintf(range_buf, sizeof(range_buf), "0-%zu", opt->range_end);
        }
        argv[argc++] = "--range";
        argv[argc++] = range_buf;
    }

    argv[argc++] = url_c;
    argv[argc] = NULL;

    return file_transfer_run_capture(ctx, argv, out_status_code, out_log);
}

static bool file_transfer_remote_upload(Evaluator_Context *ctx,
                                        const Node *node,
                                        Cmake_Event_Origin o,
                                        String_View src,
                                        String_View url,
                                        const File_Transfer_Options *opt,
                                        int *out_status_code,
                                        String_View *out_log) {
    (void)node;
    (void)o;
    if (!ctx || !opt || !out_status_code || !out_log) return false;

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *url_c = eval_sv_to_cstr_temp(ctx, url);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, url_c, false);

    char timeout_buf[64] = {0};
    char inactivity_buf[64] = {0};

    char *argv[24] = {0};
    size_t argc = 0;
    argv[argc++] = "curl";
    argv[argc++] = "--silent";
    argv[argc++] = "--show-error";
    argv[argc++] = "--location";
    argv[argc++] = "--upload-file";
    argv[argc++] = src_c;

    if (opt->has_timeout) {
        snprintf(timeout_buf, sizeof(timeout_buf), "%zu", opt->timeout_sec);
        argv[argc++] = "--max-time";
        argv[argc++] = timeout_buf;
    }
    if (opt->has_inactivity_timeout) {
        snprintf(inactivity_buf, sizeof(inactivity_buf), "%zu", opt->inactivity_timeout_sec);
        argv[argc++] = "--speed-time";
        argv[argc++] = inactivity_buf;
        argv[argc++] = "--speed-limit";
        argv[argc++] = "1";
    }

    argv[argc++] = url_c;
    argv[argc] = NULL;

    return file_transfer_run_capture(ctx, argv, out_status_code, out_log);
}

static bool handle_file_download(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(DOWNLOAD) requires URL/path and destination"),
                       nob_sv_from_cstr("Usage: file(DOWNLOAD <url> <file> [STATUS var] [LOG var])"));
        return true;
    }

    File_Transfer_Options opt = {0};
    if (!file_transfer_parse_options(ctx, node, args, 3, &opt)) return true;

    if (opt.expected_hash.count > 0 || opt.expected_md5.count > 0) {
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           nob_sv_from_cstr("EXPECTED_HASH unsupported"),
                           nob_sv_from_cstr("EXPECTED_HASH/EXPECTED_MD5 is not implemented yet"),
                           nob_sv_from_cstr("file(DOWNLOAD) EXPECTED_HASH/EXPECTED_MD5 is not implemented"),
                           nob_sv_from_cstr("Use file(DOWNLOAD ...) without EXPECTED_HASH for now"));
        return true;
    }

    String_View dst = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[2], eval_file_current_bin_dir(ctx), &dst)) return true;

    if (file_transfer_is_remote_url(args.items[1])) {
        if (!eval_file_mkdir_p(ctx, svu_dirname(dst))) {
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               1,
                               nob_sv_from_cstr("mkdir failed"),
                               nob_sv_from_cstr("failed to create destination directory"),
                               nob_sv_from_cstr("file(DOWNLOAD) failed to create destination directory"),
                               dst);
            return true;
        }

        int rc = 1;
        String_View log_sv = nob_sv_from_cstr("");
        if (!file_transfer_remote_download(ctx, node, o, args.items[1], dst, &opt, &rc, &log_sv)) {
            if (ctx->oom) return true;
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               1,
                               nob_sv_from_cstr("curl backend failed"),
                               file_transfer_trim_temp(ctx, log_sv),
                               nob_sv_from_cstr("file(DOWNLOAD) remote backend failure"),
                               args.items[1]);
            return true;
        }

        String_View log_trim = file_transfer_trim_temp(ctx, log_sv);
        String_View first = file_transfer_first_line_temp(ctx, log_trim);
        if (rc != 0) {
            if (first.count == 0) first = nob_sv_from_cstr("curl transfer failed");
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               rc,
                               first,
                               log_trim,
                               nob_sv_from_cstr("file(DOWNLOAD) failed to fetch remote URL"),
                               args.items[1]);
            return true;
        }

        file_transfer_set_success(ctx, &opt, log_trim.count > 0 ? log_trim : nob_sv_from_cstr("remote download completed"));
        return true;
    }

    String_View src_input = file_transfer_local_path_temp(ctx, args.items[1]);
    String_View src = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, src_input, eval_file_current_src_dir(ctx), &src)) return true;

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, true);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, true);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(src_c, &sb)) {
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           nob_sv_from_cstr("read source failed"),
                           nob_sv_from_cstr("read source failed"),
                           nob_sv_from_cstr("file(DOWNLOAD) failed to read source"),
                           src);
        return true;
    }

    size_t begin = 0;
    size_t end = sb.count;
    if (opt.has_range_start && opt.range_start < sb.count) begin = opt.range_start;
    if (opt.has_range_end && opt.range_end + 1 < end) end = opt.range_end + 1;
    if (begin > end) begin = end;

    if (!eval_file_mkdir_p(ctx, svu_dirname(dst))) {
        nob_sb_free(sb);
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           nob_sv_from_cstr("mkdir failed"),
                           nob_sv_from_cstr("failed to create destination directory"),
                           nob_sv_from_cstr("file(DOWNLOAD) failed to create destination directory"),
                           dst);
        return true;
    }

    bool ok = nob_write_entire_file(dst_c, sb.items + begin, end - begin);
    nob_sb_free(sb);
    if (!ok) {
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           nob_sv_from_cstr("write destination failed"),
                           nob_sv_from_cstr("write destination failed"),
                           nob_sv_from_cstr("file(DOWNLOAD) failed to write destination"),
                           dst);
        return true;
    }

    file_transfer_set_success(ctx, &opt, nob_sv_from_cstr("local download completed"));
    return true;
}

static bool handle_file_upload(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(UPLOAD) requires source file and URL/path"),
                       nob_sv_from_cstr("Usage: file(UPLOAD <file> <url> [STATUS var] [LOG var])"));
        return true;
    }

    File_Transfer_Options opt = {0};
    if (!file_transfer_parse_options(ctx, node, args, 3, &opt)) return true;

    if (opt.expected_hash.count > 0 || opt.expected_md5.count > 0) {
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           nob_sv_from_cstr("EXPECTED_HASH unsupported"),
                           nob_sv_from_cstr("EXPECTED_HASH/EXPECTED_MD5 is not valid for UPLOAD"),
                           nob_sv_from_cstr("file(UPLOAD) EXPECTED_HASH/EXPECTED_MD5 is invalid"),
                           nob_sv_from_cstr("Remove EXPECTED_HASH/EXPECTED_MD5 from file(UPLOAD)"));
        return true;
    }

    String_View src = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_src_dir(ctx), &src)) return true;

    if (file_transfer_is_remote_url(args.items[2])) {
        int rc = 1;
        String_View log_sv = nob_sv_from_cstr("");
        if (!file_transfer_remote_upload(ctx, node, o, src, args.items[2], &opt, &rc, &log_sv)) {
            if (ctx->oom) return true;
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               1,
                               nob_sv_from_cstr("curl backend failed"),
                               file_transfer_trim_temp(ctx, log_sv),
                               nob_sv_from_cstr("file(UPLOAD) remote backend failure"),
                               args.items[2]);
            return true;
        }

        String_View log_trim = file_transfer_trim_temp(ctx, log_sv);
        String_View first = file_transfer_first_line_temp(ctx, log_trim);
        if (rc != 0) {
            if (first.count == 0) first = nob_sv_from_cstr("curl transfer failed");
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               rc,
                               first,
                               log_trim,
                               nob_sv_from_cstr("file(UPLOAD) failed to send to remote URL"),
                               args.items[2]);
            return true;
        }

        file_transfer_set_success(ctx, &opt, log_trim.count > 0 ? log_trim : nob_sv_from_cstr("remote upload completed"));
        return true;
    }

    String_View dst_input = file_transfer_local_path_temp(ctx, args.items[2]);
    String_View dst = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, dst_input, eval_file_current_bin_dir(ctx), &dst)) return true;

    if (!eval_file_mkdir_p(ctx, svu_dirname(dst))) {
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           nob_sv_from_cstr("mkdir failed"),
                           nob_sv_from_cstr("failed to create destination directory"),
                           nob_sv_from_cstr("file(UPLOAD) failed to create destination directory"),
                           dst);
        return true;
    }

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, true);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, true);

    if (!nob_copy_file(src_c, dst_c)) {
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           nob_sv_from_cstr("copy failed"),
                           nob_sv_from_cstr("copy failed"),
                           nob_sv_from_cstr("file(UPLOAD) failed to copy source to destination"),
                           dst);
        return true;
    }

    file_transfer_set_success(ctx, &opt, nob_sv_from_cstr("local upload completed"));
    return true;
}

bool eval_file_handle_transfer(Evaluator_Context *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || args.count == 0) return false;
    if (eval_sv_eq_ci_lit(args.items[0], "DOWNLOAD")) return handle_file_download(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "UPLOAD")) return handle_file_upload(ctx, node, args);
    return false;
}
