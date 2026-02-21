#include "eval_diag.h"

#include "evaluator_internal.h"

#include <stdio.h>

bool h_message(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    Cmake_Diag_Severity sev = EV_DIAG_WARNING;
    size_t i = 0;

    if (a.count > 0) {
        if (eval_sv_eq_ci_lit(a.items[0], "FATAL_ERROR")) {
            sev = EV_DIAG_ERROR;
            i = 1;
        } else if (eval_sv_eq_ci_lit(a.items[0], "WARNING") || eval_sv_eq_ci_lit(a.items[0], "STATUS")) {
            i = 1;
        }
    }

    String_View msg = nob_sv_from_cstr("");
    if (i < a.count) {
        msg = eval_sv_join_semi_temp(ctx, &a.items[i], a.count - i);
    }

    if (sev == EV_DIAG_ERROR) {
        fprintf(stderr, "CMake FATAL_ERROR: %.*s\n", (int)msg.count, msg.data ? msg.data : "");
    } else {
        fprintf(stdout, "CMake: %.*s\n", (int)msg.count, msg.data ? msg.data : "");
    }

    if (i == 1 && (sev == EV_DIAG_ERROR || eval_sv_eq_ci_lit(a.items[0], "WARNING"))) {
        if (!eval_emit_diag(ctx, sev, nob_sv_from_cstr("message"), node->as.cmd.name, o, msg, nob_sv_from_cstr(""))) {
            return false;
        }
    }
    return !eval_should_stop(ctx);
}
