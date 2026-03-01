#include "eval_file_backend_curl.h"

#include <stdio.h>
#include <string.h>

#if defined(EVAL_HAVE_LIBCURL)
#include <curl/curl.h>
#endif

static void curl_append_log_line(Nob_String_Builder *sb, const char *text) {
    if (!sb || !text || text[0] == '\0') return;
    if (sb->count > 0 && sb->items[sb->count - 1] != '\n') nob_sb_append(sb, '\n');
    nob_sb_append_cstr(sb, text);
}

static bool curl_finalize_log(Evaluator_Context *ctx, Nob_String_Builder *sb, String_View *out_log) {
    if (!ctx || !sb || !out_log) return false;
    if (sb->count == 0) {
        *out_log = nob_sv_from_cstr("");
        return true;
    }
    String_View raw = nob_sv_from_parts(sb->items, sb->count);
    *out_log = sv_copy_to_temp_arena(ctx, raw);
    return !ctx->oom;
}

#if defined(EVAL_HAVE_LIBCURL)
typedef struct {
    Nob_String_Builder *log_sb;
    FILE *dst_file;
    bool discard_payload;
} Curl_Callback_State;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    Curl_Callback_State *st = (Curl_Callback_State*)userdata;
    if (!st) return total;
    if (st->discard_payload) return total;
    if (st->dst_file) {
        size_t wrote = fwrite(ptr, 1, total, st->dst_file);
        return wrote;
    }
    return total;
}

static int curl_xferinfo_cb(void *clientp,
                            curl_off_t dltotal,
                            curl_off_t dlnow,
                            curl_off_t ultotal,
                            curl_off_t ulnow) {
    (void)clientp;
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    return 0;
}

static bool curl_apply_common_opts(Evaluator_Context *ctx,
                                   CURL *h,
                                   const Eval_File_Curl_Options *opt,
                                   Curl_Callback_State *cb_state,
                                   Nob_String_Builder *log_sb,
                                   struct curl_slist **out_headers,
                                   char errbuf[CURL_ERROR_SIZE]) {
    if (!ctx || !h || !opt || !cb_state || !log_sb || !out_headers || !errbuf) return false;

    *out_headers = NULL;
    errbuf[0] = '\0';

    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, cb_state);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, curl_write_cb);

    if (opt->has_timeout) curl_easy_setopt(h, CURLOPT_TIMEOUT, opt->timeout_sec);
    if (opt->has_inactivity_timeout) {
        curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, opt->inactivity_timeout_sec);
        curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
    }

    if (opt->userpwd.count > 0) {
        char *userpwd_c = eval_sv_to_cstr_temp(ctx, opt->userpwd);
        EVAL_OOM_RETURN_IF_NULL(ctx, userpwd_c, false);
        curl_easy_setopt(h, CURLOPT_USERPWD, userpwd_c);
    }

    if (opt->has_tls_verify) {
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, opt->tls_verify ? 1L : 0L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, opt->tls_verify ? 2L : 0L);
    }

    if (opt->tls_cainfo.count > 0) {
        char *cainfo_c = eval_sv_to_cstr_temp(ctx, opt->tls_cainfo);
        EVAL_OOM_RETURN_IF_NULL(ctx, cainfo_c, false);
        curl_easy_setopt(h, CURLOPT_CAINFO, cainfo_c);
    }

    if (opt->netrc_mode != EVAL_FILE_NETRC_DEFAULT) {
        long mode = CURL_NETRC_IGNORED;
        if (opt->netrc_mode == EVAL_FILE_NETRC_OPTIONAL) mode = CURL_NETRC_OPTIONAL;
        if (opt->netrc_mode == EVAL_FILE_NETRC_REQUIRED) mode = CURL_NETRC_REQUIRED;
        curl_easy_setopt(h, CURLOPT_NETRC, mode);
    }

    if (opt->netrc_file.count > 0) {
        char *netrc_c = eval_sv_to_cstr_temp(ctx, opt->netrc_file);
        EVAL_OOM_RETURN_IF_NULL(ctx, netrc_c, false);
        curl_easy_setopt(h, CURLOPT_NETRC_FILE, netrc_c);
    }

    for (size_t i = 0; i < opt->http_headers.count; i++) {
        char *hdr_c = eval_sv_to_cstr_temp(ctx, opt->http_headers.items[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, hdr_c, false);
        *out_headers = curl_slist_append(*out_headers, hdr_c);
        if (!*out_headers) {
            if (ctx) ctx_oom(ctx);
            return false;
        }
    }
    if (*out_headers) curl_easy_setopt(h, CURLOPT_HTTPHEADER, *out_headers);

    if (opt->has_range_start || opt->has_range_end) {
        char range_buf[96] = {0};
        if (opt->has_range_start && opt->has_range_end) {
            snprintf(range_buf, sizeof(range_buf), "%zu-%zu", opt->range_start, opt->range_end);
        } else if (opt->has_range_start) {
            snprintf(range_buf, sizeof(range_buf), "%zu-", opt->range_start);
        } else {
            snprintf(range_buf, sizeof(range_buf), "0-%zu", opt->range_end);
        }
        curl_easy_setopt(h, CURLOPT_RANGE, range_buf);
    }

    if (opt->show_progress) {
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, curl_xferinfo_cb);
    } else {
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 1L);
    }

    curl_append_log_line(log_sb, "libcurl backend active");
    return true;
}

static bool curl_exec(Evaluator_Context *ctx,
                      CURL *h,
                      Nob_String_Builder *log_sb,
                      char errbuf[CURL_ERROR_SIZE],
                      int *out_status_code,
                      String_View *out_log) {
    if (!ctx || !h || !log_sb || !errbuf || !out_status_code || !out_log) return false;

    CURLcode rc = curl_easy_perform(h);
    *out_status_code = (int)rc;

    long http_code = 0;
    (void)curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &http_code);

    if (rc != CURLE_OK) {
        if (errbuf[0] != '\0') {
            curl_append_log_line(log_sb, errbuf);
        } else {
            curl_append_log_line(log_sb, curl_easy_strerror(rc));
        }
    }

    char info[128] = {0};
    snprintf(info, sizeof(info), "curl_code=%d http_code=%ld", (int)rc, http_code);
    curl_append_log_line(log_sb, info);

    return curl_finalize_log(ctx, log_sb, out_log);
}

bool eval_file_backend_curl_download(Evaluator_Context *ctx,
                                     String_View url,
                                     String_View dst_path,
                                     bool has_dst_path,
                                     const Eval_File_Curl_Options *opt,
                                     int *out_status_code,
                                     String_View *out_log) {
    if (!ctx || !opt || !out_status_code || !out_log) return false;
    *out_status_code = 1;
    *out_log = nob_sv_from_cstr("");

    char *url_c = eval_sv_to_cstr_temp(ctx, url);
    EVAL_OOM_RETURN_IF_NULL(ctx, url_c, false);

    CURL *h = curl_easy_init();
    if (!h) {
        *out_status_code = 1;
        *out_log = nob_sv_from_cstr("libcurl: curl_easy_init failed");
        return true;
    }

    FILE *dst_file = NULL;
    if (has_dst_path) {
        char *dst_c = eval_sv_to_cstr_temp(ctx, dst_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);
        dst_file = fopen(dst_c, "wb");
        if (!dst_file) {
            curl_easy_cleanup(h);
            *out_status_code = 1;
            *out_log = nob_sv_from_cstr("libcurl: failed to open destination file");
            return true;
        }
    }

    Nob_String_Builder log_sb = {0};
    Curl_Callback_State cb_state = {
        .log_sb = &log_sb,
        .dst_file = dst_file,
        .discard_payload = !has_dst_path,
    };

    struct curl_slist *headers = NULL;
    char errbuf[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(h, CURLOPT_URL, url_c);
    if (!curl_apply_common_opts(ctx, h, opt, &cb_state, &log_sb, &headers, errbuf)) {
        if (headers) curl_slist_free_all(headers);
        if (dst_file) fclose(dst_file);
        curl_easy_cleanup(h);
        nob_sb_free(log_sb);
        return false;
    }

    bool ok = curl_exec(ctx, h, &log_sb, errbuf, out_status_code, out_log);

    if (headers) curl_slist_free_all(headers);
    if (dst_file) fclose(dst_file);
    curl_easy_cleanup(h);
    nob_sb_free(log_sb);
    return ok;
}

bool eval_file_backend_curl_upload(Evaluator_Context *ctx,
                                   String_View src_path,
                                   String_View url,
                                   const Eval_File_Curl_Options *opt,
                                   int *out_status_code,
                                   String_View *out_log) {
    if (!ctx || !opt || !out_status_code || !out_log) return false;
    *out_status_code = 1;
    *out_log = nob_sv_from_cstr("");

    char *url_c = eval_sv_to_cstr_temp(ctx, url);
    char *src_c = eval_sv_to_cstr_temp(ctx, src_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, url_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);

    FILE *src_file = fopen(src_c, "rb");
    if (!src_file) {
        *out_status_code = 1;
        *out_log = nob_sv_from_cstr("libcurl: failed to open upload source file");
        return true;
    }

    CURL *h = curl_easy_init();
    if (!h) {
        fclose(src_file);
        *out_status_code = 1;
        *out_log = nob_sv_from_cstr("libcurl: curl_easy_init failed");
        return true;
    }

    Nob_String_Builder log_sb = {0};
    Curl_Callback_State cb_state = {
        .log_sb = &log_sb,
        .dst_file = NULL,
        .discard_payload = true,
    };

    struct curl_slist *headers = NULL;
    char errbuf[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(h, CURLOPT_URL, url_c);
    curl_easy_setopt(h, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(h, CURLOPT_READDATA, src_file);
    if (!curl_apply_common_opts(ctx, h, opt, &cb_state, &log_sb, &headers, errbuf)) {
        if (headers) curl_slist_free_all(headers);
        fclose(src_file);
        curl_easy_cleanup(h);
        nob_sb_free(log_sb);
        return false;
    }

    bool ok = curl_exec(ctx, h, &log_sb, errbuf, out_status_code, out_log);

    if (headers) curl_slist_free_all(headers);
    fclose(src_file);
    curl_easy_cleanup(h);
    nob_sb_free(log_sb);
    return ok;
}

#else

bool eval_file_backend_curl_download(Evaluator_Context *ctx,
                                     String_View url,
                                     String_View dst_path,
                                     bool has_dst_path,
                                     const Eval_File_Curl_Options *opt,
                                     int *out_status_code,
                                     String_View *out_log) {
    (void)ctx;
    (void)url;
    (void)dst_path;
    (void)has_dst_path;
    (void)opt;
    if (out_status_code) *out_status_code = 1;
    if (out_log) *out_log = nob_sv_from_cstr("libcurl backend not compiled (define EVAL_HAVE_LIBCURL=1)");
    return false;
}

bool eval_file_backend_curl_upload(Evaluator_Context *ctx,
                                   String_View src_path,
                                   String_View url,
                                   const Eval_File_Curl_Options *opt,
                                   int *out_status_code,
                                   String_View *out_log) {
    (void)ctx;
    (void)src_path;
    (void)url;
    (void)opt;
    if (out_status_code) *out_status_code = 1;
    if (out_log) *out_log = nob_sv_from_cstr("libcurl backend not compiled (define EVAL_HAVE_LIBCURL=1)");
    return false;
}

#endif
