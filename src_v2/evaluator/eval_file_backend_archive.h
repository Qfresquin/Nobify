#ifndef EVAL_FILE_BACKEND_ARCHIVE_H_
#define EVAL_FILE_BACKEND_ARCHIVE_H_

#include "evaluator_internal.h"

typedef struct {
    String_View output;
    SV_List paths;
    String_View format;
    String_View compression;
    bool has_compression_level;
    long compression_level;
    bool has_mtime;
    long long mtime_epoch;
    bool verbose;
} Eval_File_Archive_Create_Options;

typedef struct {
    String_View input;
    String_View destination;
    SV_List patterns;
    bool list_only;
    bool verbose;
    bool touch;
} Eval_File_Archive_Extract_Options;

bool eval_file_backend_archive_create(Evaluator_Context *ctx,
                                      const Eval_File_Archive_Create_Options *opt,
                                      int *out_status_code,
                                      String_View *out_log);

bool eval_file_backend_archive_extract(Evaluator_Context *ctx,
                                       const Eval_File_Archive_Extract_Options *opt,
                                       int *out_status_code,
                                       String_View *out_log);

#endif // EVAL_FILE_BACKEND_ARCHIVE_H_
