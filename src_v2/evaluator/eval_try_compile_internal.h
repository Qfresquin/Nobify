#ifndef EVAL_TRY_COMPILE_INTERNAL_H_
#define EVAL_TRY_COMPILE_INTERNAL_H_

#include "eval_try_compile.h"

#include "arena_dyn.h"
#include "evaluator_internal.h"
#include "stb_ds.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

void nob__cmd_append(Nob_Cmd *cmd, size_t n, ...);

typedef enum {
    TRY_COMPILE_SIGNATURE_SOURCE = 0,
    TRY_COMPILE_SIGNATURE_PROJECT,
} Try_Compile_Signature;

typedef enum {
    TRY_COMPILE_LANG_AUTO = 0,
    TRY_COMPILE_LANG_C,
    TRY_COMPILE_LANG_CXX,
    TRY_COMPILE_LANG_HEADERS,
} Try_Compile_Language;

typedef enum {
    TRY_COMPILE_BUILD_EXECUTABLE = 0,
    TRY_COMPILE_BUILD_STATIC_LIBRARY,
    TRY_COMPILE_BUILD_SHARED_LIBRARY,
    TRY_COMPILE_BUILD_OBJECTS,
} Try_Compile_Build_Kind;

typedef struct {
    String_View path;
    Try_Compile_Language language;
} Try_Compile_Source_Item;

typedef struct {
    Try_Compile_Source_Item *items;
    size_t count;
    size_t capacity;
} Try_Compile_Source_List;

typedef struct {
    bool has_value;
    String_View standard;
    bool standard_required;
    bool extensions;
    bool extensions_set;
} Try_Compile_Lang_Props;

typedef struct {
    Try_Compile_Signature signature;
    String_View result_var;
    String_View binary_dir;
    String_View current_src_dir;
    String_View current_bin_dir;
    String_View output_var;
    String_View copy_file_path;
    String_View copy_file_error_var;
    String_View log_description;
    String_View source_dir;
    String_View project_name;
    String_View target_name;
    String_View linker_language;
    SV_List compile_definitions;
    SV_List link_options;
    SV_List link_libraries;
    SV_List cmake_flags;
    Try_Compile_Source_List source_items;
    Try_Compile_Lang_Props c_lang;
    Try_Compile_Lang_Props cxx_lang;
    bool no_cache;
    bool no_log;
} Try_Compile_Request;

typedef struct {
    bool ok;
    String_View output;
    String_View artifact_path;
} Try_Compile_Execution_Result;

typedef struct {
    Try_Compile_Request compile_req;
    String_View run_result_var;
    String_View compile_output_var;
    String_View run_output_var;
    String_View run_stdout_var;
    String_View run_stderr_var;
    String_View legacy_output_var;
    String_View working_directory;
    SV_List run_args;
    bool allow_legacy_output_variable;
} Try_Run_Request;

typedef struct {
    bool compile_ok;
    bool run_invoked;
    int run_exit_code;
    String_View compile_output;
    String_View run_stdout;
    String_View run_stderr;
} Try_Run_Result;

typedef struct {
    String_View key;
    String_View value;
} Try_Compile_Target_Artifact;

bool try_compile_file_exists_sv(EvalExecContext *ctx, String_View path);
bool try_compile_mkdir_p_local(EvalExecContext *ctx, const char *path);
String_View try_compile_current_src_dir(EvalExecContext *ctx);
String_View try_compile_current_bin_dir(EvalExecContext *ctx);
String_View try_compile_concat_prefix_temp(EvalExecContext *ctx, const char *prefix, String_View tail);
String_View try_compile_basename(String_View path);
bool try_compile_is_false(String_View v);
bool try_compile_source_push(EvalExecContext *ctx,
                             Try_Compile_Source_List *list,
                             Try_Compile_Source_Item item);
bool try_compile_keyword_is_standard(String_View tok);
bool try_compile_is_keyword(String_View tok);
Try_Compile_Language try_compile_language_from_sources_type(String_View tok);
Try_Compile_Language try_compile_detect_language(String_View path);
String_View try_compile_make_scratch_dir(EvalExecContext *ctx, String_View current_bin);
String_View try_compile_resolve_in_dir(EvalExecContext *ctx, String_View path, String_View base_dir);
bool try_compile_append_file_to_log(EvalExecContext *ctx,
                                    const char *path,
                                    Nob_String_Builder *log);
bool try_compile_run_command_captured(EvalExecContext *ctx,
                                      Nob_Cmd *cmd,
                                      String_View bindir,
                                      Nob_String_Builder *log,
                                      bool *out_ok);
String_View try_compile_finish_log(EvalExecContext *ctx, Nob_String_Builder *log);

bool try_compile_parse_request(EvalExecContext *ctx,
                               const Node *node,
                               const SV_List *args,
                               Try_Compile_Request *out_req);
bool try_run_parse_request(EvalExecContext *ctx,
                           const Node *node,
                           const SV_List *args,
                           Try_Run_Request *out_req);

bool try_compile_execute_source_request(EvalExecContext *ctx,
                                        const Try_Compile_Request *req,
                                        Try_Compile_Execution_Result *out_res);
bool try_compile_execute_project_request(EvalExecContext *ctx,
                                         const Node *node,
                                         const Try_Compile_Request *req,
                                         Try_Compile_Execution_Result *out_res);
Eval_Result try_compile_execute_and_publish(EvalExecContext *ctx,
                                            const Node *node,
                                            const Try_Compile_Request *req);

#endif // EVAL_TRY_COMPILE_INTERNAL_H_
