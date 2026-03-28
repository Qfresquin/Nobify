#ifndef EVAL_PACKAGE_INTERNAL_H_
#define EVAL_PACKAGE_INTERNAL_H_

#include "eval_package.h"

#include "arena_dyn.h"
#include "eval_expr.h"
#include "eval_opt_parser.h"
#include "evaluator_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool file_exists_sv(EvalExecContext *ctx, String_View path);
bool find_package_diag_error(EvalExecContext *ctx,
                             const Node *node,
                             String_View cause,
                             String_View hint);
String_View sv_to_lower_temp(EvalExecContext *ctx, String_View in);
bool find_package_split_semicolon_temp(EvalExecContext *ctx, String_View input, SV_List *out);
void find_package_push_env_list(EvalExecContext *ctx,
                                String_View *items,
                                size_t *io_count,
                                size_t cap,
                                const char *env_name);
void find_package_push_prefix(String_View *items, size_t *io_count, size_t cap, String_View v);
void find_package_push_prefix_variants(EvalExecContext *ctx,
                                       String_View *items,
                                       size_t *io_count,
                                       size_t cap,
                                       String_View root);
void find_package_push_package_root_prefixes(EvalExecContext *ctx,
                                             String_View pkg,
                                             String_View names_csv,
                                             bool no_default_path,
                                             bool no_package_root_path,
                                             bool no_cmake_environment_path,
                                             String_View *items,
                                             size_t *io_count,
                                             size_t cap);
String_View sv_to_upper_temp(EvalExecContext *ctx, String_View in);
bool eval_package_registry_add(EvalExecContext *ctx,
                               String_View package_name,
                               String_View prefix);
void find_package_push_registry_prefixes(EvalExecContext *ctx,
                                         String_View pkg,
                                         String_View names_csv,
                                         bool no_default_path,
                                         bool no_cmake_package_registry,
                                         String_View *items,
                                         size_t *io_count,
                                         size_t cap);

#endif // EVAL_PACKAGE_INTERNAL_H_
