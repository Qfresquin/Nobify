#ifndef EVAL_FLOW_INTERNAL_H_
#define EVAL_FLOW_INTERNAL_H_

#include "eval_flow.h"

#include "arena_dyn.h"
#include "eval_expr.h"
#include "evaluator_internal.h"
#include "subprocess.h"

bool flow_require_no_args(Evaluator_Context *ctx, const Node *node, String_View usage_hint);
bool flow_parse_inline_script(Evaluator_Context *ctx, String_View script, Ast_Root *out_ast);
bool flow_is_valid_command_name(String_View name);
bool flow_is_call_disallowed(String_View name);
bool flow_append_sv(Nob_String_Builder *sb, String_View sv);
bool flow_build_call_script(Evaluator_Context *ctx,
                            String_View command_name,
                            const SV_List *args,
                            String_View *out_script);
bool flow_sv_eq_exact(String_View a, String_View b);
bool flow_arg_exact_ci(String_View value, const char *lit);
String_View flow_current_binary_dir(Evaluator_Context *ctx);
String_View flow_current_source_dir(Evaluator_Context *ctx);
String_View flow_resolve_binary_relative_path(Evaluator_Context *ctx, String_View path);
double flow_now_seconds(void);
String_View flow_sb_to_temp_sv(Evaluator_Context *ctx, Nob_String_Builder *sb);
String_View flow_trim_trailing_ascii_ws(String_View sv);
String_View flow_eval_arg_single(Evaluator_Context *ctx, const Arg *arg, bool expand_vars);
bool flow_clone_args_to_event_range(Evaluator_Context *ctx,
                                    const Args *src,
                                    size_t begin,
                                    Args *dst);

#endif // EVAL_FLOW_INTERNAL_H_
