#ifndef EVAL_TARGET_INTERNAL_H_
#define EVAL_TARGET_INTERNAL_H_

#include "eval_target.h"

#include "arena_dyn.h"
#include "evaluator_internal.h"
#include "stb_ds.h"
#include "sv_utils.h"

#include <string.h>

static inline bool target_diag(Evaluator_Context *ctx,
                               const Node *node,
                               Cmake_Diag_Severity severity,
                               String_View cause,
                               String_View hint) {
    if (!ctx || !node) return false;
    return EVAL_DIAG_BOOL_SEV(ctx,
                              severity,
                              EVAL_DIAG_INVALID_STATE,
                              nob_sv_from_cstr("dispatcher"),
                              node->as.cmd.name,
                              eval_origin_from_node(ctx, node),
                              cause,
                              hint);
}

static inline bool target_diag_error(Evaluator_Context *ctx,
                                     const Node *node,
                                     String_View cause,
                                     String_View hint) {
    return target_diag(ctx, node, EV_DIAG_ERROR, cause, hint);
}

static inline bool property_diag_unknown_directory(Evaluator_Context *ctx,
                                                   const Node *node,
                                                   String_View cause,
                                                   String_View dir) {
    if (!ctx || !node) return false;
    return EVAL_DIAG_BOOL_SEV(ctx,
                              EV_DIAG_ERROR,
                              EVAL_DIAG_NOT_FOUND,
                              nob_sv_from_cstr("dispatcher"),
                              node->as.cmd.name,
                              eval_origin_from_node(ctx, node),
                              cause,
                              dir);
}

static inline bool target_usage_validate_target(Evaluator_Context *ctx,
                                                const Node *node,
                                                String_View target_name) {
    if (!ctx || !node) return false;
    char *cmd_c = eval_sv_to_cstr_temp(ctx, node->as.cmd.name);
    EVAL_OOM_RETURN_IF_NULL(ctx, cmd_c, false);

    if (!eval_target_known(ctx, target_name)) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr(nob_temp_sprintf("%s() target was not declared", cmd_c)),
                          target_name);
        return false;
    }
    if (eval_target_alias_known(ctx, target_name)) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr(nob_temp_sprintf("%s() cannot be used on ALIAS targets", cmd_c)),
                          target_name);
        return false;
    }
    return true;
}

static inline bool target_usage_parse_visibility(String_View tok, Cmake_Visibility *io_vis) {
    if (!io_vis) return false;
    if (eval_sv_eq_ci_lit(tok, "PRIVATE")) {
        *io_vis = EV_VISIBILITY_PRIVATE;
        return true;
    }
    if (eval_sv_eq_ci_lit(tok, "PUBLIC")) {
        *io_vis = EV_VISIBILITY_PUBLIC;
        return true;
    }
    if (eval_sv_eq_ci_lit(tok, "INTERFACE")) {
        *io_vis = EV_VISIBILITY_INTERFACE;
        return true;
    }
    return false;
}

static inline bool target_usage_require_visibility(Evaluator_Context *ctx,
                                                   const Node *node) {
    if (!ctx || !node) return false;
    target_diag_error(ctx,
                      node,
                      nob_sv_from_cstr("target command requires PUBLIC, PRIVATE or INTERFACE before items"),
                      nob_sv_from_cstr("Start each item group with PUBLIC, PRIVATE or INTERFACE"));
    return false;
}

static inline String_View target_pch_item_normalize_temp(Evaluator_Context *ctx, String_View item) {
    if (!ctx) return item;
    if (item.count == 0) return item;
    if (item.count >= 2 && item.data[0] == '$' && item.data[1] == '<') return item;
    if (item.data[0] == '<' && item.data[item.count - 1] == '>') return item;
    return eval_path_resolve_for_cmake_arg(ctx, item, eval_current_source_dir_for_paths(ctx), true);
}

static inline String_View wrap_link_item_with_config_genex_temp(Evaluator_Context *ctx,
                                                                String_View item,
                                                                String_View cond_prefix) {
    if (!ctx || item.count == 0 || cond_prefix.count == 0) return item;
    String_View parts[3] = {
        cond_prefix,
        item,
        nob_sv_from_cstr(">"),
    };
    return svu_join_no_sep_temp(ctx, parts, 3);
}
#endif // EVAL_TARGET_INTERNAL_H_
