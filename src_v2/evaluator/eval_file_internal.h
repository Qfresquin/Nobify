#ifndef EVAL_FILE_INTERNAL_H_
#define EVAL_FILE_INTERNAL_H_

#include "evaluator_internal.h"

static inline bool eval_file_diag(EvalExecContext *ctx,
                                  const Node *node,
                                  Cmake_Diag_Severity severity,
                                  Eval_Diag_Code code,
                                  Cmake_Event_Origin origin,
                                  String_View cause,
                                  String_View hint) {
    if (!ctx || !node) return false;
    return EVAL_DIAG_BOOL_SEV(
        ctx, severity, code, nob_sv_from_cstr("eval_file"), node->as.cmd.name, origin, cause, hint);
}

static inline bool eval_file_diag_error(EvalExecContext *ctx,
                                        const Node *node,
                                        Eval_Diag_Code code,
                                        Cmake_Event_Origin origin,
                                        String_View cause,
                                        String_View hint) {
    return eval_file_diag(ctx, node, EV_DIAG_ERROR, code, origin, cause, hint);
}

String_View eval_file_current_src_dir(EvalExecContext *ctx);
String_View eval_file_current_bin_dir(EvalExecContext *ctx);
bool eval_file_parse_size_sv(String_View sv, size_t *out);
String_View eval_file_cmk_path_normalize_temp(EvalExecContext *ctx, String_View input);
bool eval_file_canonicalize_existing_path_temp(EvalExecContext *ctx, String_View path, String_View *out_path);
typedef enum {
    EVAL_FILE_PATH_MODE_CMAKE = 0,
    EVAL_FILE_PATH_MODE_PROJECT_SCOPED,
} Eval_File_Path_Mode;

bool eval_file_resolve_path(EvalExecContext *ctx,
                            const Node *node,
                            Cmake_Event_Origin origin,
                            String_View input_path,
                            String_View relative_base,
                            Eval_File_Path_Mode mode,
                            String_View *out_path);
bool eval_file_resolve_project_scoped_path(EvalExecContext *ctx,
                                           const Node *node,
                                           Cmake_Event_Origin origin,
                                           String_View input_path,
                                           String_View relative_base,
                                           String_View *out_path);
bool eval_file_mkdir_p(EvalExecContext *ctx, String_View path);
bool eval_file_glob_match_sv(String_View pat, String_View str, bool ci);
void eval_file_handle_glob(EvalExecContext *ctx, const Node *node, SV_List args, bool recurse);
void eval_file_handle_write(EvalExecContext *ctx, const Node *node, SV_List args);
void eval_file_handle_make_directory(EvalExecContext *ctx, const Node *node, SV_List args);
void eval_file_handle_read(EvalExecContext *ctx, const Node *node, SV_List args);
void eval_file_handle_strings(EvalExecContext *ctx, const Node *node, SV_List args);
void eval_file_handle_copy(EvalExecContext *ctx, const Node *node, SV_List args);
bool eval_file_handle_runtime_dependencies(EvalExecContext *ctx, const Node *node, SV_List args);

bool eval_file_handle_fsops(EvalExecContext *ctx, const Node *node, SV_List args);
bool eval_file_handle_transfer(EvalExecContext *ctx, const Node *node, SV_List args);
bool eval_file_handle_generate_lock_archive(EvalExecContext *ctx, const Node *node, SV_List args);
bool eval_file_handle_extra(EvalExecContext *ctx, const Node *node, SV_List args);
bool eval_file_generate_flush(EvalExecContext *ctx);
void eval_file_lock_cleanup(EvalExecContext *ctx);
void eval_file_lock_release_file_scope(EvalExecContext *ctx, size_t owner_depth);
void eval_file_lock_release_function_scope(EvalExecContext *ctx, size_t owner_depth);

#endif // EVAL_FILE_INTERNAL_H_
