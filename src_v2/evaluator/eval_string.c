#include "eval_stdlib.h"

#include "eval_string_internal.h"

static bool string_is_text_subcommand(String_View cmd) {
    return eval_sv_eq_ci_lit(cmd, "APPEND") ||
           eval_sv_eq_ci_lit(cmd, "PREPEND") ||
           eval_sv_eq_ci_lit(cmd, "CONCAT") ||
           eval_sv_eq_ci_lit(cmd, "JOIN") ||
           eval_sv_eq_ci_lit(cmd, "LENGTH") ||
           eval_sv_eq_ci_lit(cmd, "STRIP") ||
           eval_sv_eq_ci_lit(cmd, "FIND") ||
           eval_sv_eq_ci_lit(cmd, "COMPARE") ||
           eval_sv_eq_ci_lit(cmd, "ASCII") ||
           eval_sv_eq_ci_lit(cmd, "HEX") ||
           eval_sv_eq_ci_lit(cmd, "CONFIGURE") ||
           eval_sv_eq_ci_lit(cmd, "MAKE_C_IDENTIFIER") ||
           eval_sv_eq_ci_lit(cmd, "GENEX_STRIP") ||
           eval_sv_eq_ci_lit(cmd, "REPEAT") ||
           eval_sv_eq_ci_lit(cmd, "REPLACE") ||
           eval_sv_eq_ci_lit(cmd, "TOUPPER") ||
           eval_sv_eq_ci_lit(cmd, "TOLOWER") ||
           eval_sv_eq_ci_lit(cmd, "SUBSTRING");
}

static bool string_is_misc_subcommand(String_View cmd) {
    return eval_sv_eq_ci_lit(cmd, "RANDOM") ||
           eval_sv_eq_ci_lit(cmd, "TIMESTAMP") ||
           eval_sv_eq_ci_lit(cmd, "UUID") ||
           eval_string_is_hash_command(cmd);
}

Eval_Result eval_handle_string(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || arena_arr_len(a) < 1) return eval_result_from_ctx(ctx);

    if (string_is_text_subcommand(a[0])) {
        return eval_string_handle_text(ctx, node, o, a);
    }

    if (eval_sv_eq_ci_lit(a[0], "REGEX")) {
        return eval_string_handle_regex(ctx, node, o, a);
    }

    if (eval_sv_eq_ci_lit(a[0], "JSON")) {
        return eval_string_handle_json(ctx, node, o, a);
    }

    if (string_is_misc_subcommand(a[0])) {
        return eval_string_handle_misc(ctx, node, o, a);
    }

    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("Unsupported string() subcommand"), nob_sv_from_cstr("Implemented: APPEND, PREPEND, CONCAT, JOIN, LENGTH, STRIP, FIND, COMPARE, ASCII, HEX, CONFIGURE, MAKE_C_IDENTIFIER, GENEX_STRIP, REPEAT, RANDOM, TIMESTAMP, UUID, MD5, SHA1, SHA224, SHA256, SHA384, SHA512, SHA3_224, SHA3_256, SHA3_384, SHA3_512, REPLACE, TOUPPER, TOLOWER, SUBSTRING, REGEX MATCH, REGEX REPLACE, REGEX MATCHALL, JSON(GET|TYPE|MEMBER|LENGTH|REMOVE|SET|EQUAL)"));
    eval_request_stop_on_error(ctx);
    return eval_result_from_ctx(ctx);
}
