#include "eval_directory.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <string.h>

static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";
static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_link_opts_var = "NOBIFY_GLOBAL_LINK_OPTIONS";

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool sv_eq_exact(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

static bool sv_list_contains_exact(const SV_List *list, String_View item) {
    if (!list) return false;
    for (size_t i = 0; i < list->count; i++) {
        if (sv_eq_exact(list->items[i], item)) return true;
    }
    return false;
}

static bool semicolon_list_contains_exact(String_View list, String_View item) {
    if (list.count == 0) return false;
    const char *p = list.data;
    const char *end = list.data + list.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View cur = nob_sv_from_parts(p, (size_t)(q - p));
        if (sv_eq_exact(cur, item)) return true;
        if (q >= end) break;
        p = q + 1;
    }
    return false;
}

static bool append_list_var_unique(Evaluator_Context *ctx, String_View var, String_View item, bool *out_added) {
    if (out_added) *out_added = false;
    String_View current = eval_var_get(ctx, var);
    if (current.count == 0) {
        if (!eval_var_set(ctx, var, item)) return false;
        if (out_added) *out_added = true;
        return true;
    }
    if (semicolon_list_contains_exact(current, item)) {
        return true;
    }

    size_t total = current.count + 1 + item.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    memcpy(buf, current.data, current.count);
    buf[current.count] = ';';
    memcpy(buf + current.count + 1, item.data, item.count);
    buf[total] = '\0';
    if (!eval_var_set(ctx, var, nob_sv_from_cstr(buf))) return false;
    if (out_added) *out_added = true;
    return true;
}

static bool split_definition_flag(String_View item, String_View *out_definition) {
    if (!out_definition) return false;
    *out_definition = nob_sv_from_cstr("");
    if (item.count < 2 || !item.data) return false;
    bool is_dash_d = item.data[0] == '-' && (item.data[1] == 'D' || item.data[1] == 'd');
    bool is_slash_d = item.data[0] == '/' && (item.data[1] == 'D' || item.data[1] == 'd');
    if (!is_dash_d && !is_slash_d) return false;
    *out_definition = nob_sv_from_parts(item.data + 2, item.count - 2);
    return true;
}

static bool split_shell_like_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;

    size_t i = 0;
    while (i < input.count) {
        while (i < input.count && isspace((unsigned char)input.data[i])) i++;
        if (i >= input.count) break;

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

        size_t off = 0;
        bool touched = false;
        char quote = '\0';
        while (i < input.count) {
            char c = input.data[i];
            if (quote != '\0') {
                if (c == quote) {
                    quote = '\0';
                    touched = true;
                    i++;
                    continue;
                }
                if (c == '\\' && quote == '"' && i + 1 < input.count) {
                    buf[off++] = input.data[i + 1];
                    touched = true;
                    i += 2;
                    continue;
                }
                buf[off++] = c;
                touched = true;
                i++;
                continue;
            }

            if (isspace((unsigned char)c)) break;
            if (c == '"' || c == '\'') {
                quote = c;
                touched = true;
                i++;
                continue;
            }
            if (c == '\\' && i + 1 < input.count) {
                buf[off++] = input.data[i + 1];
                touched = true;
                i += 2;
                continue;
            }
            buf[off++] = c;
            touched = true;
            i++;
        }

        buf[off] = '\0';
        if (touched) {
            if (!svu_list_push_temp(ctx, out, nob_sv_from_cstr(buf))) return false;
        }
    }
    return true;
}

static bool split_comma_list_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;
    if (input.count == 0) return true;

    size_t start = 0;
    for (size_t i = 0; i <= input.count; i++) {
        if (i < input.count && input.data[i] != ',') continue;
        String_View part = nob_sv_from_parts(input.data + start, i - start);
        while (part.count > 0 && isspace((unsigned char)part.data[0])) {
            part = nob_sv_from_parts(part.data + 1, part.count - 1);
        }
        while (part.count > 0 && isspace((unsigned char)part.data[part.count - 1])) {
            part = nob_sv_from_parts(part.data, part.count - 1);
        }
        if (part.count > 0) {
            if (!svu_list_push_temp(ctx, out, part)) return false;
        }
        start = i + 1;
    }
    return true;
}

static bool expand_compile_option_token(Evaluator_Context *ctx, String_View tok, SV_List *out) {
    if (!ctx || !out) return false;
    if (tok.count == 0) return true;

    if (svu_has_prefix_ci_lit(tok, "SHELL:")) {
        String_View payload = (tok.count > 6) ? nob_sv_from_parts(tok.data + 6, tok.count - 6) : nob_sv_from_cstr("");
        return split_shell_like_temp(ctx, payload, out);
    }
    return svu_list_push_temp(ctx, out, tok);
}

static bool expand_link_option_token(Evaluator_Context *ctx, String_View tok, SV_List *out) {
    if (!ctx || !out) return false;
    if (tok.count == 0) return true;

    if (svu_has_prefix_ci_lit(tok, "SHELL:")) {
        String_View payload = (tok.count > 6) ? nob_sv_from_parts(tok.data + 6, tok.count - 6) : nob_sv_from_cstr("");
        return split_shell_like_temp(ctx, payload, out);
    }

    if (svu_has_prefix_ci_lit(tok, "LINKER:")) {
        String_View payload = (tok.count > 7) ? nob_sv_from_parts(tok.data + 7, tok.count - 7) : nob_sv_from_cstr("");
        SV_List linker_parts = {0};
        if (svu_has_prefix_ci_lit(payload, "SHELL:")) {
            String_View shell_payload = (payload.count > 6)
                ? nob_sv_from_parts(payload.data + 6, payload.count - 6)
                : nob_sv_from_cstr("");
            if (!split_shell_like_temp(ctx, shell_payload, &linker_parts)) return false;
        } else {
            if (!split_comma_list_temp(ctx, payload, &linker_parts)) return false;
        }

        for (size_t i = 0; i < linker_parts.count; i++) {
            String_View piece = linker_parts.items[i];
            size_t total = 7 + piece.count;
            char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
            memcpy(buf, "LINKER:", 7);
            if (piece.count > 0) memcpy(buf + 7, piece.data, piece.count);
            buf[total] = '\0';
            if (!svu_list_push_temp(ctx, out, nob_sv_from_cstr(buf))) return false;
        }
        return true;
    }

    return svu_list_push_temp(ctx, out, tok);
}

bool eval_handle_add_compile_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    SV_List expanded = {0};
    for (size_t i = 0; i < a.count; i++) {
        if (!expand_compile_option_token(ctx, a.items[i], &expanded)) return !eval_should_stop(ctx);
    }

    SV_List unique = {0};
    for (size_t i = 0; i < expanded.count; i++) {
        if (expanded.items[i].count == 0) continue;
        if (sv_list_contains_exact(&unique, expanded.items[i])) continue;
        if (!svu_list_push_temp(ctx, &unique, expanded.items[i])) return !eval_should_stop(ctx);
    }

    for (size_t i = 0; i < unique.count; i++) {
        bool added = false;
        if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_opts_var), unique.items[i], &added)) return false;
        if (!added) continue;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.global_compile_options.item = sv_copy_to_event_arena(ctx, unique.items[i]);
        if (!emit_event(ctx, ev)) return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_add_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        String_View item = a.items[i];
        if (item.count == 0) continue;

        String_View definition = nob_sv_from_cstr("");
        bool looks_like_definition = split_definition_flag(item, &definition);
        if (looks_like_definition && definition.count > 0) {
            bool added = false;
            if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_defs_var), definition, &added)) return false;
            if (!added) continue;
            Cmake_Event ev = {0};
            ev.kind = EV_GLOBAL_COMPILE_DEFINITIONS;
            ev.origin = o;
            ev.as.global_compile_definitions.item = sv_copy_to_event_arena(ctx, definition);
            if (!emit_event(ctx, ev)) return false;
            continue;
        }

        bool added = false;
        if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_opts_var), item, &added)) return false;
        if (!added) continue;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.global_compile_options.item = sv_copy_to_event_arena(ctx, item);
        if (!emit_event(ctx, ev)) return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_add_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    SV_List expanded = {0};
    for (size_t i = 0; i < a.count; i++) {
        if (!expand_link_option_token(ctx, a.items[i], &expanded)) return !eval_should_stop(ctx);
    }

    SV_List unique = {0};
    for (size_t i = 0; i < expanded.count; i++) {
        if (expanded.items[i].count == 0) continue;
        if (sv_list_contains_exact(&unique, expanded.items[i])) continue;
        if (!svu_list_push_temp(ctx, &unique, expanded.items[i])) return !eval_should_stop(ctx);
    }

    for (size_t i = 0; i < unique.count; i++) {
        bool added = false;
        if (!append_list_var_unique(ctx, nob_sv_from_cstr(k_global_link_opts_var), unique.items[i], &added)) return false;
        if (!added) continue;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_LINK_OPTIONS;
        ev.origin = o;
        ev.as.global_link_options.item = sv_copy_to_event_arena(ctx, unique.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        if (a.items[i].count == 0) continue;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_LINK_LIBRARIES;
        ev.origin = o;
        ev.as.global_link_libraries.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_include_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    String_View cur_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (cur_src.count == 0) cur_src = ctx->source_dir;

    bool is_system = false;
    bool is_before = false;
    for (size_t i = 0; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "SYSTEM")) {
            is_system = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "AFTER")) {
            is_before = false;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_DIRECTORY_INCLUDE_DIRECTORIES;
        ev.origin = o;
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a.items[i], cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        ev.as.directory_include_directories.path = sv_copy_to_event_arena(ctx, resolved);
        ev.as.directory_include_directories.is_system = is_system;
        ev.as.directory_include_directories.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_link_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    String_View cur_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (cur_src.count == 0) cur_src = ctx->source_dir;

    bool is_before = false;
    for (size_t i = 0; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "AFTER")) {
            is_before = false;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_DIRECTORY_LINK_DIRECTORIES;
        ev.origin = o;
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a.items[i], cur_src, true);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        ev.as.directory_link_directories.path = sv_copy_to_event_arena(ctx, resolved);
        ev.as.directory_link_directories.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

