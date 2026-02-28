#include "eval_file_internal.h"
#include "sv_utils.h"

#include <string.h>

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

static void file_transfer_set_status(Evaluator_Context *ctx, String_View status_var, int code, const char *msg) {
    if (!ctx || status_var.count == 0) return;
    (void)eval_var_set(ctx, status_var, nob_sv_from_cstr(nob_temp_sprintf("%d;%s", code, msg ? msg : "")));
}

static void file_transfer_set_log(Evaluator_Context *ctx, String_View log_var, const char *msg) {
    if (!ctx || log_var.count == 0) return;
    (void)eval_var_set(ctx, log_var, nob_sv_from_cstr(msg ? msg : ""));
}

static void file_transfer_parse_options(Evaluator_Context *ctx, SV_List args, size_t start, File_Transfer_Options *out) {
    if (!ctx || !out) return;
    memset(out, 0, sizeof(*out));
    for (size_t i = start; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "STATUS") && i + 1 < args.count) {
            out->status_var = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "LOG") && i + 1 < args.count) {
            out->log_var = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "RANGE_START") && i + 1 < args.count) {
            out->has_range_start = eval_file_parse_size_sv(args.items[++i], &out->range_start);
        } else if (eval_sv_eq_ci_lit(args.items[i], "RANGE_END") && i + 1 < args.count) {
            out->has_range_end = eval_file_parse_size_sv(args.items[++i], &out->range_end);
        } else if (eval_sv_eq_ci_lit(args.items[i], "TIMEOUT") && i + 1 < args.count) {
            out->has_timeout = eval_file_parse_size_sv(args.items[++i], &out->timeout_sec);
        } else if (eval_sv_eq_ci_lit(args.items[i], "INACTIVITY_TIMEOUT") && i + 1 < args.count) {
            out->has_inactivity_timeout = eval_file_parse_size_sv(args.items[++i], &out->inactivity_timeout_sec);
        } else if (eval_sv_eq_ci_lit(args.items[i], "EXPECTED_HASH") && i + 1 < args.count) {
            out->expected_hash = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "EXPECTED_MD5") && i + 1 < args.count) {
            out->expected_md5 = args.items[++i];
        }
    }
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
    file_transfer_parse_options(ctx, args, 3, &opt);

    if (opt.expected_hash.count > 0 || opt.expected_md5.count > 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(DOWNLOAD) EXPECTED_HASH/EXPECTED_MD5 is not supported in local backend"),
                       nob_sv_from_cstr("Use local backend without hash options for now"));
        file_transfer_set_status(ctx, opt.status_var, 1, "EXPECTED_HASH unsupported");
        file_transfer_set_log(ctx, opt.log_var, "EXPECTED_HASH unsupported");
        return true;
    }

    String_View src_input = file_transfer_local_path_temp(ctx, args.items[1]);
    if (file_transfer_is_remote_url(args.items[1])) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(DOWNLOAD) remote URL is not supported without external backend"),
                       args.items[1]);
        file_transfer_set_status(ctx, opt.status_var, 1, "remote URL unsupported");
        file_transfer_set_log(ctx, opt.log_var, "remote URL unsupported");
        return true;
    }

    String_View src = nob_sv_from_cstr("");
    String_View dst = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, src_input, eval_file_current_src_dir(ctx), &src)) return true;
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[2], eval_file_current_bin_dir(ctx), &dst)) return true;

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, true);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, true);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(src_c, &sb)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(DOWNLOAD) failed to read source"), src);
        file_transfer_set_status(ctx, opt.status_var, 1, "read source failed");
        file_transfer_set_log(ctx, opt.log_var, "read source failed");
        return true;
    }

    size_t begin = 0;
    size_t end = sb.count;
    if (opt.has_range_start && opt.range_start < sb.count) begin = opt.range_start;
    if (opt.has_range_end && opt.range_end + 1 < end) end = opt.range_end + 1;
    if (begin > end) begin = end;

    if (!eval_file_mkdir_p(ctx, svu_dirname(dst))) {
        nob_sb_free(sb);
        file_transfer_set_status(ctx, opt.status_var, 1, "mkdir failed");
        file_transfer_set_log(ctx, opt.log_var, "mkdir failed");
        return true;
    }
    bool ok = nob_write_entire_file(dst_c, sb.items + begin, end - begin);
    nob_sb_free(sb);
    if (!ok) {
        file_transfer_set_status(ctx, opt.status_var, 1, "write destination failed");
        file_transfer_set_log(ctx, opt.log_var, "write destination failed");
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(DOWNLOAD) failed to write destination"), dst);
        return true;
    }

    file_transfer_set_status(ctx, opt.status_var, 0, "OK");
    file_transfer_set_log(ctx, opt.log_var, "local download completed");
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
    file_transfer_parse_options(ctx, args, 3, &opt);

    String_View dst_input = file_transfer_local_path_temp(ctx, args.items[2]);
    if (file_transfer_is_remote_url(args.items[2])) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(UPLOAD) remote URL is not supported without external backend"),
                       args.items[2]);
        file_transfer_set_status(ctx, opt.status_var, 1, "remote URL unsupported");
        file_transfer_set_log(ctx, opt.log_var, "remote URL unsupported");
        return true;
    }

    String_View src = nob_sv_from_cstr("");
    String_View dst = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_src_dir(ctx), &src)) return true;
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, dst_input, eval_file_current_bin_dir(ctx), &dst)) return true;

    if (!eval_file_mkdir_p(ctx, svu_dirname(dst))) {
        file_transfer_set_status(ctx, opt.status_var, 1, "mkdir failed");
        file_transfer_set_log(ctx, opt.log_var, "mkdir failed");
        return true;
    }

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, true);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, true);

    if (!nob_copy_file(src_c, dst_c)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(UPLOAD) failed to copy source to destination"), dst);
        file_transfer_set_status(ctx, opt.status_var, 1, "copy failed");
        file_transfer_set_log(ctx, opt.log_var, "copy failed");
        return true;
    }

    file_transfer_set_status(ctx, opt.status_var, 0, "OK");
    file_transfer_set_log(ctx, opt.log_var, "local upload completed");
    return true;
}

bool eval_file_handle_transfer(Evaluator_Context *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || args.count == 0) return false;
    if (eval_sv_eq_ci_lit(args.items[0], "DOWNLOAD")) return handle_file_download(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "UPLOAD")) return handle_file_upload(ctx, node, args);
    return false;
}
