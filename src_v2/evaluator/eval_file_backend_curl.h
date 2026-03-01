#ifndef EVAL_FILE_BACKEND_CURL_H_
#define EVAL_FILE_BACKEND_CURL_H_

#include "evaluator_internal.h"

typedef enum {
    EVAL_FILE_NETRC_DEFAULT = 0,
    EVAL_FILE_NETRC_IGNORED,
    EVAL_FILE_NETRC_OPTIONAL,
    EVAL_FILE_NETRC_REQUIRED,
} Eval_File_Netrc_Mode;

typedef struct {
    bool has_timeout;
    bool has_inactivity_timeout;
    long timeout_sec;
    long inactivity_timeout_sec;
    bool has_range_start;
    bool has_range_end;
    size_t range_start;
    size_t range_end;
    bool has_tls_verify;
    bool tls_verify;
    bool show_progress;
    String_View userpwd;
    String_View tls_cainfo;
    String_View netrc_file;
    Eval_File_Netrc_Mode netrc_mode;
    SV_List http_headers;
} Eval_File_Curl_Options;

bool eval_file_backend_curl_download(Evaluator_Context *ctx,
                                     String_View url,
                                     String_View dst_path,
                                     bool has_dst_path,
                                     const Eval_File_Curl_Options *opt,
                                     int *out_status_code,
                                     String_View *out_log);

bool eval_file_backend_curl_upload(Evaluator_Context *ctx,
                                   String_View src_path,
                                   String_View url,
                                   const Eval_File_Curl_Options *opt,
                                   int *out_status_code,
                                   String_View *out_log);

#endif // EVAL_FILE_BACKEND_CURL_H_
