#ifndef EVAL_FILE_INTERNAL_H_
#define EVAL_FILE_INTERNAL_H_

#include "evaluator_internal.h"

String_View eval_file_current_src_dir(Evaluator_Context *ctx);
String_View eval_file_current_bin_dir(Evaluator_Context *ctx);
bool eval_file_parse_size_sv(String_View sv, size_t *out);
String_View eval_file_cmk_path_normalize_temp(Evaluator_Context *ctx, String_View input);
bool eval_file_resolve_project_scoped_path(Evaluator_Context *ctx,
                                           const Node *node,
                                           Cmake_Event_Origin origin,
                                           String_View input_path,
                                           String_View relative_base,
                                           String_View *out_path);
bool eval_file_mkdir_p(Evaluator_Context *ctx, String_View path);
void eval_file_handle_copy(Evaluator_Context *ctx, const Node *node, SV_List args);

bool eval_file_handle_fsops(Evaluator_Context *ctx, const Node *node, SV_List args);
bool eval_file_handle_transfer(Evaluator_Context *ctx, const Node *node, SV_List args);
bool eval_file_handle_generate_lock_archive(Evaluator_Context *ctx, const Node *node, SV_List args);
void eval_file_lock_cleanup(Evaluator_Context *ctx);

#endif // EVAL_FILE_INTERNAL_H_
