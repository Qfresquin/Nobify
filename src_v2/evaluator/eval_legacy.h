#ifndef EVAL_LEGACY_H_
#define EVAL_LEGACY_H_

#include "parser.h"
#include "evaluator.h"

struct EvalExecContext;

Eval_Result eval_handle_export_library_dependencies(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_install_files(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_install_programs(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_install_targets(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_load_command(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_make_directory(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_output_required_files(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_qt_wrap_cpp(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_qt_wrap_ui(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_remove(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_subdir_depends(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_subdirs(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_use_mangled_mesa(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_utility_source(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_variable_requires(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_variable_watch(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_fltk_wrap_ui(struct EvalExecContext *ctx, const Node *node);
Eval_Result eval_handle_write_file(struct EvalExecContext *ctx, const Node *node);

#endif // EVAL_LEGACY_H_
