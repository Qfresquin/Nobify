#include "eval_install.h"

#include "evaluator_internal.h"

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}
bool eval_handle_install(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() requires rule type, items and DESTINATION"),
                       nob_sv_from_cstr("Usage: install(TARGETS|FILES|PROGRAMS|DIRECTORY <items...> DESTINATION <dir>)"));
        return !eval_should_stop(ctx);
    }

    Cmake_Install_Rule_Type rule_type = EV_INSTALL_RULE_TARGET;
    if (eval_sv_eq_ci_lit(a.items[0], "TARGETS")) rule_type = EV_INSTALL_RULE_TARGET;
    else if (eval_sv_eq_ci_lit(a.items[0], "FILES")) rule_type = EV_INSTALL_RULE_FILE;
    else if (eval_sv_eq_ci_lit(a.items[0], "PROGRAMS")) rule_type = EV_INSTALL_RULE_PROGRAM;
    else if (eval_sv_eq_ci_lit(a.items[0], "DIRECTORY")) rule_type = EV_INSTALL_RULE_DIRECTORY;
    else {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() unsupported rule type in v2"),
                       a.items[0]);
        return !eval_should_stop(ctx);
    }

    size_t dest_i = a.count;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "DESTINATION")) {
            dest_i = i;
            break;
        }
    }
    if (dest_i == a.count || dest_i + 1 >= a.count) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() missing DESTINATION"),
                       nob_sv_from_cstr("Usage: install(TARGETS|FILES|PROGRAMS|DIRECTORY <items...> DESTINATION <dir>)"));
        return !eval_should_stop(ctx);
    }

    if (dest_i <= 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() has no items before DESTINATION"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View destination = a.items[dest_i + 1];
    for (size_t i = 1; i < dest_i; i++) {
        Cmake_Event ev = {0};
        ev.kind = EV_INSTALL_ADD_RULE;
        ev.origin = o;
        ev.as.install_add_rule.rule_type = rule_type;
        ev.as.install_add_rule.item = sv_copy_to_event_arena(ctx, a.items[i]);
        ev.as.install_add_rule.destination = sv_copy_to_event_arena(ctx, destination);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

