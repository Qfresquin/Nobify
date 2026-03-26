#include "eval_flow_internal.h"

#include <string.h>
#include <time.h>

bool flow_require_no_args(EvalExecContext *ctx, const Node *node, String_View usage_hint) {
    if (!ctx || !node) return false;

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(args) == 0) return true;

    (void)EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, nob_sv_from_cstr("flow"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("Command does not accept arguments"), usage_hint);
    return false;
}

static bool flow_token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, token);
}

bool flow_parse_inline_script(EvalExecContext *ctx, String_View script, Ast_Root *out_ast) {
    if (!ctx || !out_ast) return false;
    *out_ast = NULL;

    Lexer lx = lexer_init(script);
    Token_List toks = NULL;
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!flow_token_list_append(ctx->arena, &toks, t)) return ctx_oom(ctx);
    }

    *out_ast = parse_tokens(ctx->arena, toks);
    return true;
}

bool flow_is_valid_command_name(String_View name) {
    if (name.count == 0 || !name.data) return false;
    char c0 = name.data[0];
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_')) return false;
    for (size_t i = 1; i < name.count; i++) {
        char c = name.data[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') continue;
        return false;
    }
    return true;
}

bool flow_is_call_disallowed(String_View name) {
    return eval_sv_eq_ci_lit(name, "if") ||
           eval_sv_eq_ci_lit(name, "elseif") ||
           eval_sv_eq_ci_lit(name, "else") ||
           eval_sv_eq_ci_lit(name, "endif") ||
           eval_sv_eq_ci_lit(name, "foreach") ||
           eval_sv_eq_ci_lit(name, "endforeach") ||
           eval_sv_eq_ci_lit(name, "while") ||
           eval_sv_eq_ci_lit(name, "endwhile") ||
           eval_sv_eq_ci_lit(name, "function") ||
           eval_sv_eq_ci_lit(name, "endfunction") ||
           eval_sv_eq_ci_lit(name, "macro") ||
           eval_sv_eq_ci_lit(name, "endmacro") ||
           eval_sv_eq_ci_lit(name, "block") ||
           eval_sv_eq_ci_lit(name, "endblock");
}

static size_t flow_bracket_eq_count(String_View sv) {
    for (size_t eqs = 0; eqs < 8; eqs++) {
        bool conflict = false;
        for (size_t i = 0; i + eqs + 1 < sv.count; i++) {
            if (sv.data[i] != ']') continue;
            size_t j = 0;
            while (j < eqs && (i + 1 + j) < sv.count && sv.data[i + 1 + j] == '=') j++;
            if (j != eqs) continue;
            if ((i + 1 + eqs) < sv.count && sv.data[i + 1 + eqs] == ']') {
                conflict = true;
                break;
            }
        }
        if (!conflict) return eqs;
    }
    return 8;
}

bool flow_append_sv(Nob_String_Builder *sb, String_View sv) {
    if (!sb) return false;
    for (size_t i = 0; i < sv.count; i++) nob_sb_append(sb, sv.data[i]);
    return true;
}

bool flow_build_call_script(EvalExecContext *ctx,
                            String_View command_name,
                            const SV_List *args,
                            String_View *out_script) {
    if (!ctx || !args || !out_script) return false;
    *out_script = nob_sv_from_cstr("");

    Nob_String_Builder sb = {0};
    if (!flow_append_sv(&sb, command_name)) {
        nob_sb_free(sb);
        return ctx_oom(ctx);
    }
    nob_sb_append(&sb, '(');
    for (size_t i = 0; i < arena_arr_len(*args); i++) {
        nob_sb_append(&sb, ' ');
        size_t eqs = flow_bracket_eq_count((*args)[i]);
        nob_sb_append(&sb, '[');
        for (size_t j = 0; j < eqs; j++) nob_sb_append(&sb, '=');
        nob_sb_append(&sb, '[');
        if (!flow_append_sv(&sb, (*args)[i])) {
            nob_sb_free(sb);
            return ctx_oom(ctx);
        }
        nob_sb_append(&sb, ']');
        for (size_t j = 0; j < eqs; j++) nob_sb_append(&sb, '=');
        nob_sb_append(&sb, ']');
    }
    nob_sb_append_cstr(&sb, ")\n");

    char *copy = arena_strndup(ctx->arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, false);
    *out_script = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool flow_sv_eq_exact(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

bool flow_arg_exact_ci(String_View value, const char *lit) {
    return eval_sv_eq_ci_lit(value, lit);
}

String_View flow_current_binary_dir(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View dir = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (dir.count == 0) dir = ctx->binary_dir;
    return dir;
}

String_View flow_current_source_dir(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View dir = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (dir.count == 0) dir = ctx->source_dir;
    return dir;
}

String_View flow_resolve_binary_relative_path(EvalExecContext *ctx, String_View path) {
    if (!ctx || path.count == 0) return nob_sv_from_cstr("");
    return eval_path_resolve_for_cmake_arg(ctx, path, flow_current_binary_dir(ctx), false);
}

double flow_now_seconds(void) {
    struct timespec ts = {0};
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

String_View flow_sb_to_temp_sv(EvalExecContext *ctx, Nob_String_Builder *sb) {
    if (!ctx || !sb) return nob_sv_from_cstr("");
    if (sb->count == 0) return nob_sv_from_cstr("");
    char *copy = arena_strndup(ctx->arena, sb->items, sb->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, nob_sv_from_cstr(""));
    return nob_sv_from_parts(copy, sb->count);
}

String_View flow_trim_trailing_ascii_ws(String_View sv) {
    while (sv.count > 0) {
        unsigned char c = (unsigned char)sv.data[sv.count - 1];
        if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')) break;
        sv.count--;
    }
    return sv;
}

static bool flow_strip_bracket_arg(String_View in, String_View *out) {
    if (!out) return false;
    *out = in;
    if (in.count < 4 || !in.data || in.data[0] != '[') return false;

    size_t eq_count = 0;
    size_t i = 1;
    while (i < in.count && in.data[i] == '=') {
        eq_count++;
        i++;
    }
    if (i >= in.count || in.data[i] != '[') return false;
    size_t open_len = i + 1;
    if (in.count < open_len + 2 + eq_count) return false;

    size_t close_pos = in.count - (eq_count + 2);
    if (in.data[close_pos] != ']') return false;
    for (size_t k = 0; k < eq_count; k++) {
        if (in.data[close_pos + 1 + k] != '=') return false;
    }
    if (in.data[in.count - 1] != ']') return false;
    if (close_pos < open_len) return false;

    *out = nob_sv_from_parts(in.data + open_len, close_pos - open_len);
    return true;
}

static String_View flow_arg_flat(EvalExecContext *ctx, const Arg *arg) {
    if (!ctx || !arg || arena_arr_len(arg->items) == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) total += arg->items[i].text.count;

    char *buf = (char*)arena_alloc(ctx->arena, total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) {
        String_View text = arg->items[i].text;
        if (text.count > 0) {
            memcpy(buf + off, text.data, text.count);
            off += text.count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

String_View flow_eval_arg_single(EvalExecContext *ctx, const Arg *arg, bool expand_vars) {
    if (!ctx || !arg) return nob_sv_from_cstr("");

    String_View flat = flow_arg_flat(ctx, arg);
    String_View value = expand_vars ? eval_expand_vars(ctx, flat) : flat;
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    if (arg->kind == ARG_QUOTED) {
        if (value.count >= 2 && value.data[0] == '"' && value.data[value.count - 1] == '"') {
            return nob_sv_from_parts(value.data + 1, value.count - 2);
        }
        return value;
    }
    if (arg->kind == ARG_BRACKET) {
        String_View stripped = value;
        (void)flow_strip_bracket_arg(value, &stripped);
        return stripped;
    }
    return value;
}

bool flow_clone_args_to_event_range(EvalExecContext *ctx,
                                    const Args *src,
                                    size_t begin,
                                    Args *dst) {
    if (!ctx || !src || !dst) return false;
    *dst = NULL;
    if (begin >= arena_arr_len(*src)) return true;

    for (size_t i = begin; i < arena_arr_len(*src); i++) {
        const Arg *in = &(*src)[i];
        Arg out = {0};
        out.kind = in->kind;
        for (size_t k = 0; k < arena_arr_len(in->items); k++) {
            Token t = in->items[k];
            t.text = sv_copy_to_event_arena(ctx, t.text);
            if (eval_should_stop(ctx)) return false;
            if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, out.items, t)) return false;
        }
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, *dst, out)) return false;
    }
    return true;
}
