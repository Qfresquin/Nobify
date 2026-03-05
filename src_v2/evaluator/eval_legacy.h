#ifndef EVAL_LEGACY_H_
#define EVAL_LEGACY_H_

#include "parser.h"
#include "evaluator.h"

struct Evaluator_Context;

Eval_Result eval_handle_export_library_dependencies(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_install_files(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_install_programs(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_install_targets(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_load_command(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_make_directory(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_output_required_files(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_qt_wrap_cpp(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_qt_wrap_ui(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_remove(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_subdir_depends(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_subdirs(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_use_mangled_mesa(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_utility_source(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_variable_requires(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_variable_watch(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_fltk_wrap_ui(struct Evaluator_Context *ctx, const Node *node);
Eval_Result eval_handle_write_file(struct Evaluator_Context *ctx, const Node *node);

#endif // EVAL_LEGACY_H_
