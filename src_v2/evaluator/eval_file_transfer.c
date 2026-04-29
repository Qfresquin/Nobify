#include "eval_file_internal.h"
#include "eval_hash.h"
#include "eval_expr.h"
#include "sv_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static bool file_transfer_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        char ca = a.data[i];
        char cb = b.data[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

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
    String_View userpwd;
    String_View tls_cainfo;
    String_View netrc_file;
    bool has_tls_verify;
    bool tls_verify;
    bool show_progress;
    Eval_Transfer_Netrc_Mode netrc_mode;
    String_View http_headers[16];
    size_t http_headers_count;
} File_Transfer_Options;

static bool file_transfer_emit_replay_marker(EvalExecContext *ctx,
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

static bool file_transfer_emit_replay_local_download(EvalExecContext *ctx,
                                                     Cmake_Event_Origin origin,
                                                     String_View src_path,
                                                     String_View dst_path,
                                                     String_View hash_algo,
                                                     String_View hash_digest) {
    String_View action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_HOST_EFFECT,
                                  EVENT_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key) ||
        !eval_emit_replay_action_add_input(ctx, origin, action_key, src_path) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, dst_path) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, hash_algo) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 1, hash_digest)) {
        return false;
    }
    return true;
}

static bool file_transfer_is_known_option(String_View t) {
    return eval_sv_eq_ci_lit(t, "STATUS") ||
           eval_sv_eq_ci_lit(t, "LOG") ||
           eval_sv_eq_ci_lit(t, "RANGE_START") ||
           eval_sv_eq_ci_lit(t, "RANGE_END") ||
           eval_sv_eq_ci_lit(t, "TIMEOUT") ||
           eval_sv_eq_ci_lit(t, "INACTIVITY_TIMEOUT") ||
           eval_sv_eq_ci_lit(t, "EXPECTED_HASH") ||
           eval_sv_eq_ci_lit(t, "EXPECTED_MD5") ||
           eval_sv_eq_ci_lit(t, "USERPWD") ||
           eval_sv_eq_ci_lit(t, "TLS_CAINFO") ||
           eval_sv_eq_ci_lit(t, "TLS_VERIFY") ||
           eval_sv_eq_ci_lit(t, "HTTPHEADER") ||
           eval_sv_eq_ci_lit(t, "NETRC") ||
           eval_sv_eq_ci_lit(t, "NETRC_FILE") ||
           eval_sv_eq_ci_lit(t, "SHOW_PROGRESS");
}

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

static String_View file_transfer_local_path_temp(EvalExecContext *ctx, String_View in) {
    if (!ctx) return nob_sv_from_cstr("");
    if (in.count >= 7 && (memcmp(in.data, "file://", 7) == 0 || memcmp(in.data, "FILE://", 7) == 0)) {
        return nob_sv_from_parts(in.data + 7, in.count - 7);
    }
    return in;
}

static String_View file_transfer_trim_temp(EvalExecContext *ctx, String_View in) {
    if (!ctx) return nob_sv_from_cstr("");
    size_t begin = 0;
    size_t end = in.count;
    while (begin < end && isspace((unsigned char)in.data[begin])) begin++;
    while (end > begin && isspace((unsigned char)in.data[end - 1])) end--;
    return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(in.data + begin, end - begin));
}

static String_View file_transfer_first_line_temp(EvalExecContext *ctx, String_View in) {
    if (!ctx) return nob_sv_from_cstr("");
    size_t end = 0;
    while (end < in.count && in.data[end] != '\n' && in.data[end] != '\r') end++;
    return file_transfer_trim_temp(ctx, nob_sv_from_parts(in.data, end));
}

static void file_transfer_set_status_sv(EvalExecContext *ctx, String_View status_var, int code, String_View msg) {
    if (!ctx || status_var.count == 0) return;
    const int mlen = (msg.count > (size_t)INT32_MAX) ? INT32_MAX : (int)msg.count;
    (void)eval_var_set_current(ctx, status_var, nob_sv_from_cstr(nob_temp_sprintf("%d;%.*s", code, mlen, msg.data ? msg.data : "")));
}

static void file_transfer_set_log_sv(EvalExecContext *ctx, String_View log_var, String_View msg) {
    if (!ctx || log_var.count == 0) return;
    (void)eval_var_set_current(ctx, log_var, msg);
}

static void file_transfer_set_success(EvalExecContext *ctx, const File_Transfer_Options *opt, String_View log_msg) {
    if (!ctx || !opt) return;
    file_transfer_set_status_sv(ctx, opt->status_var, 0, nob_sv_from_cstr("No error"));
    file_transfer_set_log_sv(ctx, opt->log_var, log_msg);
}

static void file_transfer_fail(EvalExecContext *ctx,
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

    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", cause, hint.count > 0 ? hint : msg);
}

static bool file_transfer_parse_options(EvalExecContext *ctx,
                                        const Node *node,
                                        SV_List args,
                                        size_t start,
                                        File_Transfer_Options *out) {
    if (!ctx || !node || !out) return false;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    memset(out, 0, sizeof(*out));

    for (size_t i = start; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "STATUS")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() STATUS requires an output variable"), args[i]);
                return false;
            }
            out->status_var = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "LOG")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() LOG requires an output variable"), args[i]);
                return false;
            }
            out->log_var = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "RANGE_START")) {
            if (i + 1 >= arena_arr_len(args) || !eval_file_parse_size_sv(args[i + 1], &out->range_start)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(DOWNLOAD) invalid RANGE_START"), (i + 1 < arena_arr_len(args)) ? args[i + 1] : args[i]);
                return false;
            }
            out->has_range_start = true;
            i++;
        } else if (eval_sv_eq_ci_lit(args[i], "RANGE_END")) {
            if (i + 1 >= arena_arr_len(args) || !eval_file_parse_size_sv(args[i + 1], &out->range_end)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(DOWNLOAD) invalid RANGE_END"), (i + 1 < arena_arr_len(args)) ? args[i + 1] : args[i]);
                return false;
            }
            out->has_range_end = true;
            i++;
        } else if (eval_sv_eq_ci_lit(args[i], "TIMEOUT")) {
            if (i + 1 >= arena_arr_len(args) || !eval_file_parse_size_sv(args[i + 1], &out->timeout_sec)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file() invalid TIMEOUT"), (i + 1 < arena_arr_len(args)) ? args[i + 1] : args[i]);
                return false;
            }
            out->has_timeout = true;
            i++;
        } else if (eval_sv_eq_ci_lit(args[i], "INACTIVITY_TIMEOUT")) {
            if (i + 1 >= arena_arr_len(args) || !eval_file_parse_size_sv(args[i + 1], &out->inactivity_timeout_sec)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file() invalid INACTIVITY_TIMEOUT"), (i + 1 < arena_arr_len(args)) ? args[i + 1] : args[i]);
                return false;
            }
            out->has_inactivity_timeout = true;
            i++;
        } else if (eval_sv_eq_ci_lit(args[i], "EXPECTED_HASH")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(DOWNLOAD) EXPECTED_HASH requires a value"), args[i]);
                return false;
            }
            out->expected_hash = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "EXPECTED_MD5")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(DOWNLOAD) EXPECTED_MD5 requires a value"), args[i]);
                return false;
            }
            out->expected_md5 = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "USERPWD")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() USERPWD requires a value"), args[i]);
                return false;
            }
            out->userpwd = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "TLS_CAINFO")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() TLS_CAINFO requires a value"), args[i]);
                return false;
            }
            out->tls_cainfo = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "TLS_VERIFY")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() TLS_VERIFY requires a value"), args[i]);
                return false;
            }
            out->has_tls_verify = true;
            out->tls_verify = eval_truthy(ctx, args[++i]);
        } else if (eval_sv_eq_ci_lit(args[i], "HTTPHEADER")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() HTTPHEADER requires a value"), args[i]);
                return false;
            }
            if (out->http_headers_count < NOB_ARRAY_LEN(out->http_headers)) {
                out->http_headers[out->http_headers_count++] = args[++i];
            } else {
                i++;
            }
        } else if (eval_sv_eq_ci_lit(args[i], "NETRC")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() NETRC requires one of IGNORED/OPTIONAL/REQUIRED"), args[i]);
                return false;
            }
            String_View mode = args[++i];
            if (eval_sv_eq_ci_lit(mode, "IGNORED")) out->netrc_mode = EVAL_TRANSFER_NETRC_IGNORED;
            else if (eval_sv_eq_ci_lit(mode, "OPTIONAL")) out->netrc_mode = EVAL_TRANSFER_NETRC_OPTIONAL;
            else if (eval_sv_eq_ci_lit(mode, "REQUIRED")) out->netrc_mode = EVAL_TRANSFER_NETRC_REQUIRED;
            else {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() NETRC requires IGNORED/OPTIONAL/REQUIRED"), mode);
                return false;
            }
        } else if (eval_sv_eq_ci_lit(args[i], "NETRC_FILE")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file() NETRC_FILE requires a value"), args[i]);
                return false;
            }
            out->netrc_file = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "SHOW_PROGRESS")) {
            out->show_progress = true;
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_file", nob_sv_from_cstr("file() received unknown transfer option"), args[i]);
            return false;
        }
    }
    return true;
}

static bool file_transfer_parse_expected_hash(String_View in, String_View *out_algo, String_View *out_hex) {
    if (!out_algo || !out_hex) return false;
    *out_algo = nob_sv_from_cstr("");
    *out_hex = nob_sv_from_cstr("");
    for (size_t i = 0; i < in.count; i++) {
        if (in.data[i] != '=') continue;
        *out_algo = nob_sv_from_parts(in.data, i);
        *out_hex = nob_sv_from_parts(in.data + i + 1, in.count - i - 1);
        return out_algo->count > 0 && out_hex->count > 0;
    }
    return false;
}

static bool file_transfer_path_exists(EvalExecContext *ctx, String_View path) {
    if (!ctx || path.count == 0) return false;
    Eval_Fs_Stat st = {0};
    return eval_service_stat(ctx, path, true, &st) && st.exists;
}

static bool file_transfer_verify_download_hash(EvalExecContext *ctx,
                                               String_View dst,
                                               const File_Transfer_Options *opt,
                                               String_View *out_status) {
    if (!ctx || !opt || !out_status) return false;
    *out_status = nob_sv_from_cstr("");

    String_View algo = nob_sv_from_cstr("");
    String_View expected = nob_sv_from_cstr("");
    if (opt->expected_hash.count > 0) {
        if (!file_transfer_parse_expected_hash(opt->expected_hash, &algo, &expected)) {
            *out_status = nob_sv_from_cstr("EXPECTED_HASH must be <ALGO>=<VALUE>");
            return false;
        }
    } else if (opt->expected_md5.count > 0) {
        algo = nob_sv_from_cstr("MD5");
        expected = opt->expected_md5;
    } else {
        return true;
    }

    String_View payload = nob_sv_from_cstr("");
    bool found = false;
    if (!eval_service_read_file(ctx, dst, &payload, &found) || !found) {
        *out_status = nob_sv_from_cstr("failed to read downloaded file for hash verification");
        return false;
    }
    String_View actual = nob_sv_from_cstr("");
    bool ok = eval_hash_compute_hex_temp(ctx, algo, payload, &actual);
    if (!ok) {
        *out_status = nob_sv_from_cstr("unsupported EXPECTED_HASH algorithm");
        return false;
    }

    if (!file_transfer_sv_eq_ci(actual, expected)) {
        *out_status = nob_sv_from_cstr("download hash mismatch");
        return false;
    }
    return true;
}

static Eval_Transfer_Options file_transfer_service_options(const File_Transfer_Options *opt) {
    Eval_Transfer_Options out = {0};
    if (!opt) return out;
    out.has_timeout = opt->has_timeout;
    out.has_inactivity_timeout = opt->has_inactivity_timeout;
    out.timeout_sec = (long)opt->timeout_sec;
    out.inactivity_timeout_sec = (long)opt->inactivity_timeout_sec;
    out.has_range_start = opt->has_range_start;
    out.has_range_end = opt->has_range_end;
    out.range_start = opt->range_start;
    out.range_end = opt->range_end;
    out.has_tls_verify = opt->has_tls_verify;
    out.tls_verify = opt->tls_verify;
    out.show_progress = opt->show_progress;
    out.userpwd = opt->userpwd;
    out.tls_cainfo = opt->tls_cainfo;
    out.netrc_file = opt->netrc_file;
    out.netrc_mode = opt->netrc_mode;
    out.http_headers = opt->http_headers;
    out.http_headers_count = opt->http_headers_count;
    return out;
}

static bool handle_file_download(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(DOWNLOAD) requires at least URL/path"), nob_sv_from_cstr("Usage: file(DOWNLOAD <url> [<file>] [STATUS var] [LOG var])"));
        return true;
    }

    bool has_dst = false;
    String_View dst_arg = nob_sv_from_cstr("");
    size_t opt_start = 2;
    if (arena_arr_len(args) >= 3 && !file_transfer_is_known_option(args[2])) {
        has_dst = true;
        dst_arg = args[2];
        opt_start = 3;
    }

    File_Transfer_Options opt = {0};
    if (!file_transfer_parse_options(ctx, node, args, opt_start, &opt)) return true;

    String_View dst = nob_sv_from_cstr("");
    if (has_dst) {
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, dst_arg, eval_file_current_bin_dir(ctx), &dst)) return true;
    }

    if (!has_dst && (opt.expected_hash.count > 0 || opt.expected_md5.count > 0)) {
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           nob_sv_from_cstr("EXPECTED_HASH requires destination file"),
                           nob_sv_from_cstr("EXPECTED_HASH/EXPECTED_MD5 cannot be used in probe-only DOWNLOAD"),
                           nob_sv_from_cstr("file(DOWNLOAD) EXPECTED_HASH requires destination"),
                           nob_sv_from_cstr("Add destination path or remove EXPECTED_HASH/EXPECTED_MD5"));
        return true;
    }

    if (has_dst && !opt.has_range_start && !opt.has_range_end &&
        (opt.expected_hash.count > 0 || opt.expected_md5.count > 0) &&
        file_transfer_path_exists(ctx, dst)) {
        String_View hash_status = nob_sv_from_cstr("");
        if (file_transfer_verify_download_hash(ctx, dst, &opt, &hash_status)) {
            file_transfer_set_success(ctx, &opt, nob_sv_from_cstr("download skipped (destination already matches EXPECTED_HASH)"));
            return true;
        }
    }

    if (file_transfer_is_remote_url(args[1])) {
        if (has_dst && !eval_file_mkdir_p(ctx, svu_dirname(dst))) {
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

        Eval_Transfer_Download_Request transfer_req = {
            .url = args[1],
            .dst_path = dst,
            .has_dst_path = has_dst,
            .options = file_transfer_service_options(&opt),
        };
        Eval_Backend_Result transfer = {0};
        if (!eval_service_transfer_download(ctx, &transfer_req, &transfer)) {
            if (ctx->oom) return true;
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               1,
                               nob_sv_from_cstr("remote transfer backend failed"),
                               file_transfer_trim_temp(ctx, transfer.log),
                               nob_sv_from_cstr("file(DOWNLOAD) remote backend failure"),
                               args[1]);
            return true;
        }

        String_View log_trim = file_transfer_trim_temp(ctx, transfer.log);
        String_View first = file_transfer_first_line_temp(ctx, log_trim);
        if (transfer.status_code != 0) {
            if (first.count == 0) first = nob_sv_from_cstr("remote transfer failed");
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               transfer.status_code,
                               first,
                               log_trim,
                               nob_sv_from_cstr("file(DOWNLOAD) failed to fetch remote URL"),
                               args[1]);
            return true;
        }

        String_View hash_status = nob_sv_from_cstr("");
        if (has_dst && !file_transfer_verify_download_hash(ctx, dst, &opt, &hash_status)) {
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               1,
                               hash_status.count > 0 ? hash_status : nob_sv_from_cstr("hash verification failed"),
                               log_trim,
                               nob_sv_from_cstr("file(DOWNLOAD) hash verification failed"),
                               dst);
            return true;
        }

        file_transfer_set_success(ctx, &opt, log_trim.count > 0 ? log_trim : nob_sv_from_cstr("remote download completed"));
        (void)eval_emit_fs_transfer_download(ctx, o, args[1], has_dst ? dst : nob_sv_from_cstr(""));
        (void)file_transfer_emit_replay_marker(ctx,
                                               o,
                                               has_dst ? EVENT_REPLAY_ACTION_HOST_EFFECT
                                                       : EVENT_REPLAY_ACTION_PROBE);
        return true;
    }

    String_View src_input = file_transfer_local_path_temp(ctx, args[1]);
    String_View src = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, src_input, eval_file_current_src_dir(ctx), &src)) return true;

    String_View src_contents = nob_sv_from_cstr("");
    bool found = false;
    if (!eval_service_read_file(ctx, src, &src_contents, &found) || !found) {
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
    size_t end = src_contents.count;
    if (opt.has_range_start && opt.range_start < src_contents.count) begin = opt.range_start;
    if (opt.has_range_end && opt.range_end + 1 < end) end = opt.range_end + 1;
    if (begin > end) begin = end;

    if (!has_dst) {
        file_transfer_set_success(ctx, &opt, nob_sv_from_cstr("local download probe completed"));
        (void)file_transfer_emit_replay_marker(ctx, o, EVENT_REPLAY_ACTION_PROBE);
        return true;
    }

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

    if (opt.has_range_start && opt.range_start >= src_contents.count) {
        (void)eval_service_write_file(ctx, dst, nob_sv_from_cstr(""), false);
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           36,
                           nob_sv_from_cstr("Couldn't resume download"),
                           nob_sv_from_cstr("failed to resume file:// transfer"),
                           nob_sv_from_cstr("file(DOWNLOAD) range start is outside source"),
                           src);
        return true;
    }

    const char *range_data = src_contents.data ? src_contents.data + begin : "";
    bool ok = eval_service_write_file(ctx,
                                      dst,
                                      nob_sv_from_parts(range_data, end - begin),
                                      false);
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

    String_View hash_status = nob_sv_from_cstr("");
    if (!file_transfer_verify_download_hash(ctx, dst, &opt, &hash_status)) {
        file_transfer_fail(ctx,
                           node,
                           o,
                           &opt,
                           1,
                           hash_status.count > 0 ? hash_status : nob_sv_from_cstr("hash verification failed"),
                           hash_status,
                           nob_sv_from_cstr("file(DOWNLOAD) hash verification failed"),
                           dst);
        return true;
    }

    file_transfer_set_success(ctx, &opt, nob_sv_from_cstr("local download completed"));
    (void)eval_emit_fs_transfer_download(ctx, o, src, dst);
    {
        bool supported = !opt.has_range_start &&
                         !opt.has_range_end &&
                         !opt.has_timeout &&
                         !opt.has_inactivity_timeout &&
                         !opt.has_tls_verify &&
                         opt.http_headers_count == 0 &&
                         opt.userpwd.count == 0 &&
                         opt.tls_cainfo.count == 0 &&
                         opt.netrc_file.count == 0 &&
                         opt.netrc_mode == EVAL_TRANSFER_NETRC_DEFAULT &&
                         !opt.expected_md5.count;
        String_View hash_algo = nob_sv_from_cstr("");
        String_View hash_digest = nob_sv_from_cstr("");
        if (supported && opt.expected_hash.count > 0) {
            supported = file_transfer_parse_expected_hash(opt.expected_hash, &hash_algo, &hash_digest) &&
                        file_transfer_sv_eq_ci(hash_algo, nob_sv_from_cstr("SHA256"));
        }
        if (supported) {
            (void)file_transfer_emit_replay_local_download(ctx, o, src, dst, hash_algo, hash_digest);
        } else {
            (void)file_transfer_emit_replay_marker(ctx, o, EVENT_REPLAY_ACTION_HOST_EFFECT);
        }
    }
    return true;
}

static bool handle_file_upload(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(UPLOAD) requires source file and URL/path"), nob_sv_from_cstr("Usage: file(UPLOAD <file> <url> [STATUS var] [LOG var])"));
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
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args[1], eval_file_current_src_dir(ctx), &src)) return true;

    if (file_transfer_is_remote_url(args[2])) {
        Eval_Transfer_Upload_Request transfer_req = {
            .src_path = src,
            .url = args[2],
            .options = file_transfer_service_options(&opt),
        };
        Eval_Backend_Result transfer = {0};
        if (!eval_service_transfer_upload(ctx, &transfer_req, &transfer)) {
            if (ctx->oom) return true;
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               1,
                               nob_sv_from_cstr("remote transfer backend failed"),
                               file_transfer_trim_temp(ctx, transfer.log),
                               nob_sv_from_cstr("file(UPLOAD) remote backend failure"),
                               args[2]);
            return true;
        }

        String_View log_trim = file_transfer_trim_temp(ctx, transfer.log);
        String_View first = file_transfer_first_line_temp(ctx, log_trim);
        if (transfer.status_code != 0) {
            if (first.count == 0) first = nob_sv_from_cstr("remote transfer failed");
            file_transfer_fail(ctx,
                               node,
                               o,
                               &opt,
                               transfer.status_code,
                               first,
                               log_trim,
                               nob_sv_from_cstr("file(UPLOAD) failed to send to remote URL"),
                               args[2]);
            return true;
        }

        file_transfer_set_success(ctx, &opt, log_trim.count > 0 ? log_trim : nob_sv_from_cstr("remote upload completed"));
        (void)eval_emit_fs_transfer_upload(ctx, o, src, args[2]);
        return true;
    }

    String_View dst_input = file_transfer_local_path_temp(ctx, args[2]);
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

    if (!eval_service_copy_file(ctx, src, dst)) {
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
    (void)eval_emit_fs_transfer_upload(ctx, o, src, dst);
    return true;
}

bool eval_file_handle_transfer(EvalExecContext *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || arena_arr_len(args) == 0) return false;
    if (eval_sv_eq_ci_lit(args[0], "DOWNLOAD")) return handle_file_download(ctx, node, args);
    if (eval_sv_eq_ci_lit(args[0], "UPLOAD")) return handle_file_upload(ctx, node, args);
    return false;
}
