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

static bool msg_join_no_sep_temp(Evaluator_Context *ctx, String_View *items, size_t count, String_View *out) {
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

static bool msg_check_stack_push(Evaluator_Context *ctx, String_View msg) {
    if (!ctx) return false;
    msg = sv_copy_to_event_arena(ctx, msg);
    if (eval_should_stop(ctx)) return false;
    if (!arena_da_try_append(ctx->event_arena, &ctx->message_check_stack, msg)) return ctx_oom(ctx);
    return true;
}

static bool msg_check_stack_pop(Evaluator_Context *ctx, String_View *out_msg) {
    if (!ctx || !out_msg) return false;
    if (ctx->message_check_stack.count == 0) return false;
    *out_msg = ctx->message_check_stack.items[ctx->message_check_stack.count - 1];
    ctx->message_check_stack.count--;
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

static bool msg_write_all(FILE *f, const char *text) {
    if (!f || !text) return false;
    size_t n = strlen(text);
    return fwrite(text, 1, n, f) == n;
}

bool eval_append_configure_log(Evaluator_Context *ctx, const Node *node, String_View msg) {
    if (!ctx) return false;

    String_View bin = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"));
    if (bin.count == 0) bin = ctx->binary_dir;

    String_View cmakefiles_dir = eval_sv_path_join(eval_temp_arena(ctx), bin, nob_sv_from_cstr("CMakeFiles"));
    if (eval_should_stop(ctx)) return false;
    char *cmakefiles_dir_c = eval_sv_to_cstr_temp(ctx, cmakefiles_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, cmakefiles_dir_c, false);
    if (!nob_mkdir_if_not_exists(cmakefiles_dir_c)) return false;

    String_View log_path = eval_sv_path_join(eval_temp_arena(ctx), cmakefiles_dir, nob_sv_from_cstr("CMakeConfigureLog.yaml"));
    if (eval_should_stop(ctx)) return false;
    char *log_path_c = eval_sv_to_cstr_temp(ctx, log_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, log_path_c, false);

    FILE *f = fopen(log_path_c, "ab");
    if (!f) return false;

    bool ok = true;
    ok = ok && msg_write_all(f, "---\n");
    ok = ok && msg_write_all(f, "events:\n");
    ok = ok && msg_write_all(f, "  - kind: \"message-v1\"\n");

    char bt[1024];
    snprintf(bt, sizeof(bt), "    backtrace:\n      - \"%s:%zu (message)\"\n",
             ctx->current_file ? ctx->current_file : "CMakeLists.txt",
             node ? node->line : 0u);
    ok = ok && msg_write_all(f, bt);

    if (ctx->message_check_stack.count > 0) {
        ok = ok && msg_write_all(f, "    checks:\n");
        for (size_t i = ctx->message_check_stack.count; i-- > 0;) {
            String_View check = ctx->message_check_stack.items[i];
            ok = ok && msg_write_all(f, "      - \"");
            if (check.count > 0) ok = ok && (fwrite(check.data, 1, check.count, f) == check.count);
            ok = ok && msg_write_all(f, "\"\n");
        }
    }

    ok = ok && msg_write_all(f, "    message: |\n      ");
    if (msg.count > 0) ok = ok && (fwrite(msg.data, 1, msg.count, f) == msg.count);
    ok = ok && msg_write_all(f, "\n...\n");

    fclose(f);
    return ok;
}

bool eval_handle_message(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    Eval_Message_Mode mode = MSG_MODE_PLAIN;
    size_t msg_begin = 0;
    if (a.count > 0) {
        bool is_mode = false;
        mode = msg_parse_mode(a.items[0], &is_mode);
        if (is_mode) msg_begin = 1;
    }

    String_View msg = {0};
    if (!msg_join_no_sep_temp(ctx, &a.items[msg_begin], a.count > msg_begin ? (a.count - msg_begin) : 0, &msg)) {
        return !eval_should_stop(ctx);
    }

    if (mode == MSG_MODE_CHECK_START) {
        if (!msg_check_stack_push(ctx, msg)) return !eval_should_stop(ctx);
    } else if (mode == MSG_MODE_CHECK_PASS || mode == MSG_MODE_CHECK_FAIL) {
        String_View start_msg = {0};
        if (!msg_check_stack_pop(ctx, &start_msg)) {
            if (!eval_emit_diag(ctx,
                                EV_DIAG_ERROR,
                                nob_sv_from_cstr("message"),
                                node->as.cmd.name,
                                o,
                                nob_sv_from_cstr("message(CHECK_PASS/CHECK_FAIL) requires a preceding CHECK_START"),
                                nob_sv_from_cstr("Use message(CHECK_START ...) before CHECK_PASS/CHECK_FAIL"))) {
                return false;
            }
            return !eval_should_stop(ctx);
        }

        // CMake check reporting uses the latest CHECK_START text as a prefix.
        String_View parts[3] = {start_msg, nob_sv_from_cstr(" - "), msg};
        if (!msg_join_no_sep_temp(ctx, parts, 3, &msg)) return !eval_should_stop(ctx);
    }

    if (mode == MSG_MODE_DEPRECATION) {
        String_View error_dep = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_ERROR_DEPRECATED"));
        bool error_enabled = (error_dep.count > 0) && !msg_is_cmake_false(error_dep);
        String_View warn_dep = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_WARN_DEPRECATED"));
        bool warn_enabled = (warn_dep.count == 0) || !msg_is_cmake_false(warn_dep);
        if (error_enabled) {
            mode = MSG_MODE_SEND_ERROR;
        } else if (!warn_enabled) {
            return !eval_should_stop(ctx);
        } else {
            mode = MSG_MODE_WARNING;
        }
    }

    if (mode == MSG_MODE_CONFIGURE_LOG) {
        if (!eval_append_configure_log(ctx, node, msg)) {
            return ctx_oom(ctx) ? false : !eval_should_stop(ctx);
        }
        return !eval_should_stop(ctx);
    }

    if (mode == MSG_MODE_FATAL_ERROR || mode == MSG_MODE_SEND_ERROR) {
        fprintf(stderr, "CMake ERROR: %.*s\n", (int)msg.count, msg.data ? msg.data : "");
    } else {
        fprintf(stdout, "CMake: %.*s\n", (int)msg.count, msg.data ? msg.data : "");
    }

    if (mode == MSG_MODE_FATAL_ERROR || mode == MSG_MODE_SEND_ERROR) {
        if (!eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("message"), node->as.cmd.name, o, msg, nob_sv_from_cstr(""))) {
            return false;
        }
        return !eval_should_stop(ctx);
    }
    if (mode == MSG_MODE_WARNING || mode == MSG_MODE_AUTHOR_WARNING || mode == MSG_MODE_DEPRECATION) {
        if (!eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("message"), node->as.cmd.name, o, msg, nob_sv_from_cstr(""))) {
            return false;
        }
    }

    return !eval_should_stop(ctx);
}
