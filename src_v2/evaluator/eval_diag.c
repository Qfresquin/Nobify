#include "eval_diag.h"

#include "evaluator_internal.h"
#include "arena_dyn.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef enum {
    MSG_MODE_NOTICE = 0,
    MSG_MODE_STATUS,
    MSG_MODE_VERBOSE,
    MSG_MODE_DEBUG,
    MSG_MODE_TRACE,
    MSG_MODE_WARNING,
    MSG_MODE_AUTHOR_WARNING,
    MSG_MODE_DEPRECATION,
    MSG_MODE_SEND_ERROR,
    MSG_MODE_FATAL_ERROR,
    MSG_MODE_CHECK_START,
    MSG_MODE_CHECK_PASS,
    MSG_MODE_CHECK_FAIL,
    MSG_MODE_CONFIGURE_LOG,
    MSG_MODE_PLAIN,
} Eval_Message_Mode;

static bool msg_join_no_sep_temp(EvalExecContext *ctx, String_View *items, size_t count, String_View *out) {
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!items || count == 0) return true;

    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += items[i].count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].count == 0) continue;
        memcpy(buf + off, items[i].data, items[i].count);
        off += items[i].count;
    }
    buf[off] = '\0';
    *out = nob_sv_from_parts(buf, off);
    return true;
}

static Eval_Message_Mode msg_parse_mode(String_View head, bool *out_is_mode_token) {
    if (out_is_mode_token) *out_is_mode_token = true;
    if (eval_sv_eq_ci_lit(head, "NOTICE")) return MSG_MODE_NOTICE;
    if (eval_sv_eq_ci_lit(head, "STATUS")) return MSG_MODE_STATUS;
    if (eval_sv_eq_ci_lit(head, "VERBOSE")) return MSG_MODE_VERBOSE;
    if (eval_sv_eq_ci_lit(head, "DEBUG")) return MSG_MODE_DEBUG;
    if (eval_sv_eq_ci_lit(head, "TRACE")) return MSG_MODE_TRACE;
    if (eval_sv_eq_ci_lit(head, "WARNING")) return MSG_MODE_WARNING;
    if (eval_sv_eq_ci_lit(head, "AUTHOR_WARNING")) return MSG_MODE_AUTHOR_WARNING;
    if (eval_sv_eq_ci_lit(head, "DEPRECATION")) return MSG_MODE_DEPRECATION;
    if (eval_sv_eq_ci_lit(head, "SEND_ERROR")) return MSG_MODE_SEND_ERROR;
    if (eval_sv_eq_ci_lit(head, "FATAL_ERROR")) return MSG_MODE_FATAL_ERROR;
    if (eval_sv_eq_ci_lit(head, "CHECK_START")) return MSG_MODE_CHECK_START;
    if (eval_sv_eq_ci_lit(head, "CHECK_PASS")) return MSG_MODE_CHECK_PASS;
    if (eval_sv_eq_ci_lit(head, "CHECK_FAIL")) return MSG_MODE_CHECK_FAIL;
    if (eval_sv_eq_ci_lit(head, "CONFIGURE_LOG")) return MSG_MODE_CONFIGURE_LOG;
    if (out_is_mode_token) *out_is_mode_token = false;
    return MSG_MODE_PLAIN;
}

static bool msg_check_stack_push(EvalExecContext *ctx, String_View msg) {
    if (!ctx) return false;
    msg = sv_copy_to_event_arena(ctx, msg);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->message_check_stack, msg);
}

static bool msg_check_stack_pop(EvalExecContext *ctx, String_View *out_msg) {
    if (!ctx || !out_msg) return false;
    if (arena_arr_len(ctx->message_check_stack) == 0) return false;
    *out_msg = ctx->message_check_stack[arena_arr_len(ctx->message_check_stack) - 1];
    arena_arr_set_len(ctx->message_check_stack, arena_arr_len(ctx->message_check_stack) - 1);
    return true;
}

static bool msg_sv_eq_ci(String_View a, const char *lit) {
    if (!lit) return false;
    String_View b = nob_sv_from_cstr(lit);
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

static bool msg_sv_ends_with_ci(String_View a, const char *suffix) {
    if (!suffix) return false;
    String_View s = nob_sv_from_cstr(suffix);
    if (a.count < s.count) return false;
    size_t off = a.count - s.count;
    for (size_t i = 0; i < s.count; i++) {
        if (toupper((unsigned char)a.data[off + i]) != toupper((unsigned char)s.data[i])) return false;
    }
    return true;
}

static bool msg_is_cmake_false(String_View v) {
    if (v.count == 0) return true;
    if (msg_sv_eq_ci(v, "0")) return true;
    if (msg_sv_eq_ci(v, "OFF")) return true;
    if (msg_sv_eq_ci(v, "NO")) return true;
    if (msg_sv_eq_ci(v, "FALSE")) return true;
    if (msg_sv_eq_ci(v, "N")) return true;
    if (msg_sv_eq_ci(v, "IGNORE")) return true;
    if (msg_sv_eq_ci(v, "NOTFOUND")) return true;
    if (msg_sv_ends_with_ci(v, "-NOTFOUND")) return true;
    return false;
}

bool eval_append_configure_log(EvalExecContext *ctx, const Node *node, String_View msg) {
    if (!ctx) return false;

    String_View bin = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"));
    if (bin.count == 0) bin = ctx->binary_dir;

    String_View cmakefiles_dir = eval_sv_path_join(eval_temp_arena(ctx), bin, nob_sv_from_cstr("CMakeFiles"));
    if (eval_should_stop(ctx)) return false;
    if (!eval_service_mkdir(ctx, cmakefiles_dir)) return false;

    String_View log_path = eval_sv_path_join(eval_temp_arena(ctx), cmakefiles_dir, nob_sv_from_cstr("CMakeConfigureLog.yaml"));
    if (eval_should_stop(ctx)) return false;
    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "---\n");
    nob_sb_append_cstr(&sb, "events:\n");
    nob_sb_append_cstr(&sb, "  - kind: \"message-v1\"\n");
    nob_sb_appendf(&sb,
                   "    backtrace:\n      - \"%s:%zu (message)\"\n",
                   ctx->current_file ? ctx->current_file : "CMakeLists.txt",
                   node ? node->line : 0u);
    if (arena_arr_len(ctx->message_check_stack) > 0) {
        nob_sb_append_cstr(&sb, "    checks:\n");
        for (size_t i = arena_arr_len(ctx->message_check_stack); i-- > 0;) {
            String_View check = ctx->message_check_stack[i];
            nob_sb_append_cstr(&sb, "      - \"");
            if (check.count > 0) nob_sb_append_buf(&sb, check.data, check.count);
            nob_sb_append_cstr(&sb, "\"\n");
        }
    }
    nob_sb_append_cstr(&sb, "    message: |\n      ");
    if (msg.count > 0) nob_sb_append_buf(&sb, msg.data, msg.count);
    nob_sb_append_cstr(&sb, "\n...\n");
    bool ok = eval_service_write_file(ctx, log_path, nob_sv_from_parts(sb.items ? sb.items : "", sb.count), true);
    nob_sb_free(sb);
    return ok;
}

Eval_Result eval_handle_message(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_fatal();

    Eval_Message_Mode mode = MSG_MODE_PLAIN;
    size_t msg_begin = 0;
    if (arena_arr_len(a) > 0) {
        bool is_mode = false;
        mode = msg_parse_mode(a[0], &is_mode);
        if (is_mode) msg_begin = 1;
    }

    String_View msg = {0};
    if (!msg_join_no_sep_temp(ctx, a ? &a[msg_begin] : NULL, arena_arr_len(a) > msg_begin ? (arena_arr_len(a) - msg_begin) : 0, &msg)) {
        return eval_result_from_ctx(ctx);
    }

    if (mode == MSG_MODE_CHECK_START) {
        if (!msg_check_stack_push(ctx, msg)) return eval_result_from_ctx(ctx);
    } else if (mode == MSG_MODE_CHECK_PASS || mode == MSG_MODE_CHECK_FAIL) {
        String_View start_msg = {0};
        if (!msg_check_stack_pop(ctx, &start_msg)) {
            if (!EVAL_NODE_ORIGIN_DIAG_BOOL_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_SCRIPT_ERROR, "message", nob_sv_from_cstr("message(CHECK_PASS/CHECK_FAIL) requires a preceding CHECK_START"), nob_sv_from_cstr("Use message(CHECK_START ...) before CHECK_PASS/CHECK_FAIL"))) {
                return eval_result_fatal();
            }
            return eval_result_from_ctx(ctx);
        }

        // CMake check reporting uses the latest CHECK_START text as a prefix.
        String_View parts[3] = {start_msg, nob_sv_from_cstr(" - "), msg};
        if (!msg_join_no_sep_temp(ctx, parts, 3, &msg)) return eval_result_from_ctx(ctx);
    }

    if (mode == MSG_MODE_DEPRECATION) {
        String_View error_dep = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_ERROR_DEPRECATED"));
        bool error_enabled = (error_dep.count > 0) && !msg_is_cmake_false(error_dep);
        String_View warn_dep = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_WARN_DEPRECATED"));
        bool warn_enabled = (warn_dep.count == 0) || !msg_is_cmake_false(warn_dep);
        if (error_enabled) {
            mode = MSG_MODE_SEND_ERROR;
        } else if (!warn_enabled) {
            return eval_result_from_ctx(ctx);
        } else {
            mode = MSG_MODE_WARNING;
        }
    }

    if (mode == MSG_MODE_CONFIGURE_LOG) {
        if (!eval_append_configure_log(ctx, node, msg)) {
            if (ctx_oom(ctx)) return eval_result_fatal();
            return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    if (mode == MSG_MODE_FATAL_ERROR || mode == MSG_MODE_SEND_ERROR) {
        fprintf(stderr, "CMake ERROR: %.*s\n", (int)msg.count, msg.data ? msg.data : "");
    } else {
        fprintf(stdout, "CMake: %.*s\n", (int)msg.count, msg.data ? msg.data : "");
    }

    if (mode == MSG_MODE_FATAL_ERROR || mode == MSG_MODE_SEND_ERROR) {
        if (!EVAL_NODE_ORIGIN_DIAG_BOOL_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_SCRIPT_ERROR, "message", msg, nob_sv_from_cstr(""))) {
            return eval_result_fatal();
        }
        return eval_result_from_ctx(ctx);
    }
    if (mode == MSG_MODE_WARNING || mode == MSG_MODE_AUTHOR_WARNING || mode == MSG_MODE_DEPRECATION) {
        if (!EVAL_NODE_ORIGIN_DIAG_BOOL_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_SCRIPT_WARNING, "message", msg, nob_sv_from_cstr(""))) {
            return eval_result_fatal();
        }
    }

    return eval_result_from_ctx(ctx);
}
