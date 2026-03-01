#include "eval_file_internal.h"
#include "eval_hash.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>

static bool file_read_bytes(Evaluator_Context *ctx, String_View path, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);
    return nob_read_entire_file(p, out);
}

static bool file_write_bytes(Evaluator_Context *ctx, String_View path, const char *data, size_t len) {
    if (!ctx) return false;
    if (!eval_file_mkdir_p(ctx, svu_dirname(path))) return false;
    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);
    return nob_write_entire_file(p, data, len);
}

static bool file_same_content(Evaluator_Context *ctx, String_View path, String_View content, bool *out_same) {
    if (!ctx || !out_same) return false;
    *out_same = false;

    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(p, &sb)) return true;
    String_View cur = nob_sv_from_parts(sb.items, sb.count);
    *out_same = nob_sv_eq(cur, content);
    nob_sb_free(sb);
    return true;
}

static bool handle_file_hash(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count != 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(<HASH>) requires filename and output variable"),
                       nob_sv_from_cstr("Usage: file(<HASH> <filename> <out-var>)"));
        return true;
    }

    String_View in_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args.items[1],
                                eval_file_current_src_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &in_path)) {
        return true;
    }

    Nob_String_Builder data = {0};
    if (!file_read_bytes(ctx, in_path, &data)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(<HASH>) failed to read file"), in_path);
        return true;
    }

    String_View digest = nob_sv_from_cstr("");
    String_View payload = nob_sv_from_parts(data.items, data.count);
    bool ok = eval_hash_compute_hex_temp(ctx, args.items[0], payload, &digest);
    nob_sb_free(data);
    if (!ok) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("Unsupported hash algorithm"), args.items[0]);
        return true;
    }

    (void)eval_var_set(ctx, args.items[2], digest);
    return true;
}

static String_View file_expand_configure_once(Evaluator_Context *ctx,
                                              String_View input,
                                              bool at_only,
                                              bool escape_quotes) {
    if (!ctx) return nob_sv_from_cstr("");
    Nob_String_Builder out = {0};

    for (size_t i = 0; i < input.count;) {
        if (!at_only && i + 3 < input.count && input.data[i] == '$' && input.data[i + 1] == '{') {
            size_t j = i + 2;
            while (j < input.count && input.data[j] != '}') j++;
            if (j < input.count && input.data[j] == '}') {
                String_View key = nob_sv_from_parts(input.data + i + 2, j - (i + 2));
                String_View val = eval_var_get(ctx, key);
                if (escape_quotes) {
                    for (size_t k = 0; k < val.count; k++) {
                        if (val.data[k] == '"') nob_sb_append(&out, '\\');
                        nob_sb_append(&out, val.data[k]);
                    }
                } else {
                    nob_sb_append_buf(&out, val.data, val.count);
                }
                i = j + 1;
                continue;
            }
        }
        if (i + 2 < input.count && input.data[i] == '@') {
            size_t j = i + 1;
            while (j < input.count && input.data[j] != '@') j++;
            if (j < input.count && input.data[j] == '@' && j > i + 1) {
                String_View key = nob_sv_from_parts(input.data + i + 1, j - (i + 1));
                String_View val = eval_var_get(ctx, key);
                if (escape_quotes) {
                    for (size_t k = 0; k < val.count; k++) {
                        if (val.data[k] == '"') nob_sb_append(&out, '\\');
                        nob_sb_append(&out, val.data[k]);
                    }
                } else {
                    nob_sb_append_buf(&out, val.data, val.count);
                }
                i = j + 1;
                continue;
            }
        }

        nob_sb_append(&out, input.data[i]);
        i++;
    }

    nob_sb_append_null(&out);
    String_View r = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(out.items, out.count - 1));
    nob_sb_free(out);
    return r;
}

static String_View file_apply_newline_style(Evaluator_Context *ctx, String_View in, String_View style) {
    if (!ctx || style.count == 0) return in;
    const char *nl = "\n";
    if (eval_sv_eq_ci_lit(style, "DOS") || eval_sv_eq_ci_lit(style, "WIN32") || eval_sv_eq_ci_lit(style, "CRLF")) {
        nl = "\r\n";
    }

    Nob_String_Builder out = {0};
    for (size_t i = 0; i < in.count; i++) {
        if (in.data[i] == '\r') {
            if (i + 1 < in.count && in.data[i + 1] == '\n') i++;
            nob_sb_append_cstr(&out, nl);
            continue;
        }
        if (in.data[i] == '\n') {
            nob_sb_append_cstr(&out, nl);
            continue;
        }
        nob_sb_append(&out, in.data[i]);
    }
    nob_sb_append_null(&out);
    String_View r = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(out.items, out.count - 1));
    nob_sb_free(out);
    return r;
}

static bool handle_file_configure(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    String_View output = nob_sv_from_cstr("");
    String_View content = nob_sv_from_cstr("");
    String_View newline_style = nob_sv_from_cstr("");
    bool at_only = false;
    bool escape_quotes = false;

    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "OUTPUT") && i + 1 < args.count) {
            output = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "CONTENT") && i + 1 < args.count) {
            content = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "NEWLINE_STYLE") && i + 1 < args.count) {
            newline_style = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "@ONLY")) {
            at_only = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "ESCAPE_QUOTES")) {
            escape_quotes = true;
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(CONFIGURE) received unexpected argument"), args.items[i]);
            return true;
        }
    }

    if (output.count == 0 || content.count == 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CONFIGURE) requires OUTPUT and CONTENT"),
                       nob_sv_from_cstr("Usage: file(CONFIGURE OUTPUT <out> CONTENT <text> [@ONLY] [ESCAPE_QUOTES] [NEWLINE_STYLE <style>])"));
        return true;
    }

    String_View out_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                output,
                                eval_file_current_bin_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &out_path)) {
        return true;
    }

    String_View expanded = file_expand_configure_once(ctx, content, at_only, escape_quotes);
    expanded = file_apply_newline_style(ctx, expanded, newline_style);

    bool same = false;
    if (!file_same_content(ctx, out_path, expanded, &same)) return true;
    if (same) return true;

    if (!file_write_bytes(ctx, out_path, expanded.data, expanded.count)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CONFIGURE) failed to write OUTPUT"), out_path);
    }
    return true;
}

static bool file_copy_file_do(Evaluator_Context *ctx, String_View src, String_View dst, bool only_if_different) {
    if (!ctx) return false;
    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);

    if (only_if_different) {
        Nob_String_Builder a = {0};
        Nob_String_Builder b = {0};
        bool ra = nob_read_entire_file(src_c, &a);
        bool rb = nob_read_entire_file(dst_c, &b);
        if (ra && rb && a.count == b.count && (a.count == 0 || memcmp(a.items, b.items, a.count) == 0)) {
            nob_sb_free(a);
            nob_sb_free(b);
            return true;
        }
        nob_sb_free(a);
        nob_sb_free(b);
    }

    if (!eval_file_mkdir_p(ctx, svu_dirname(dst))) return false;
    return nob_copy_file(src_c, dst_c);
}

static bool handle_file_copy_file(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY_FILE) requires source and destination"),
                       nob_sv_from_cstr("Usage: file(COPY_FILE <old> <new> [RESULT <var>] [ONLY_IF_DIFFERENT] [INPUT_MAY_BE_RECENT])"));
        return true;
    }

    bool only_if_different = false;
    String_View result_var = nob_sv_from_cstr("");
    for (size_t i = 3; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "ONLY_IF_DIFFERENT")) {
            only_if_different = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "INPUT_MAY_BE_RECENT")) {
            // Accepted for parity; no extra behavior needed in evaluator backend.
        } else if (eval_sv_eq_ci_lit(args.items[i], "RESULT") && i + 1 < args.count) {
            result_var = args.items[++i];
        } else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(COPY_FILE) received unexpected argument"), args.items[i]);
            return true;
        }
    }

    String_View src = nob_sv_from_cstr("");
    String_View dst = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args.items[1],
                                eval_file_current_src_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &src)) {
        return true;
    }
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args.items[2],
                                eval_file_current_bin_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &dst)) {
        return true;
    }

    bool ok = file_copy_file_do(ctx, src, dst, only_if_different);
    if (result_var.count > 0) {
        (void)eval_var_set(ctx, result_var, ok ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"));
        return true;
    }

    if (!ok) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY_FILE) failed"), src);
    }
    return true;
}

static bool file_touch_one(Evaluator_Context *ctx, String_View path, bool create) {
    if (!ctx) return false;
    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);

    struct stat st = {0};
    if (stat(p, &st) != 0) {
        if (!create) return true;
        if (!file_write_bytes(ctx, path, "", 0)) return false;
        if (stat(p, &st) != 0) return false;
    }

    struct utimbuf tb = {0};
    tb.actime = st.st_atime;
    tb.modtime = time(NULL);
    return utime(p, &tb) == 0;
}

static bool handle_file_touch(Evaluator_Context *ctx, const Node *node, SV_List args, bool nocreate) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 2) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nocreate ? nob_sv_from_cstr("file(TOUCH_NOCREATE) requires at least one file")
                                : nob_sv_from_cstr("file(TOUCH) requires at least one file"),
                       nocreate ? nob_sv_from_cstr("Usage: file(TOUCH_NOCREATE <file>...)")
                                : nob_sv_from_cstr("Usage: file(TOUCH <file>...)"));
        return true;
    }

    for (size_t i = 1; i < args.count; i++) {
        String_View path = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx,
                                    node,
                                    o,
                                    args.items[i],
                                    eval_file_current_bin_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE,
                                    &path)) {
            return true;
        }
        if (!file_touch_one(ctx, path, !nocreate)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nocreate ? nob_sv_from_cstr("file(TOUCH_NOCREATE) failed")
                                    : nob_sv_from_cstr("file(TOUCH) failed"),
                           path);
            return true;
        }
    }
    return true;
}

typedef struct {
    String_View resolved_var;
    String_View unresolved_var;
    String_View conflicts_prefix;
    SV_List executables;
    SV_List libraries;
    SV_List modules;
    SV_List directories;
    SV_List post_include_files;
    SV_List post_exclude_files;
} Runtime_Deps_Args;

static bool runtime_push_temp(Evaluator_Context *ctx, SV_List *list, String_View v) {
    return svu_list_push_temp(ctx, list, v);
}

static bool runtime_parse_lists(Evaluator_Context *ctx,
                                SV_List args,
                                size_t *idx,
                                SV_List *out,
                                const char **stop_tokens,
                                size_t stop_count) {
    while (*idx < args.count) {
        bool stop = false;
        for (size_t s = 0; s < stop_count; s++) {
            if (eval_sv_eq_ci_lit(args.items[*idx], stop_tokens[s])) {
                stop = true;
                break;
            }
        }
        if (stop) break;
        if (!runtime_push_temp(ctx, out, args.items[*idx])) return false;
        (*idx)++;
    }
    return true;
}

static bool handle_file_get_runtime_dependencies(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    Runtime_Deps_Args rd = {0};

    static const char *k_stops[] = {
        "RESOLVED_DEPENDENCIES_VAR",
        "UNRESOLVED_DEPENDENCIES_VAR",
        "CONFLICTING_DEPENDENCIES_PREFIX",
        "EXECUTABLES",
        "LIBRARIES",
        "MODULES",
        "DIRECTORIES",
        "PRE_INCLUDE_REGEXES",
        "PRE_EXCLUDE_REGEXES",
        "POST_INCLUDE_REGEXES",
        "POST_EXCLUDE_REGEXES",
        "POST_INCLUDE_FILES",
        "POST_EXCLUDE_FILES",
    };

    for (size_t i = 1; i < args.count;) {
        if (eval_sv_eq_ci_lit(args.items[i], "RESOLVED_DEPENDENCIES_VAR") && i + 1 < args.count) {
            rd.resolved_var = args.items[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "UNRESOLVED_DEPENDENCIES_VAR") && i + 1 < args.count) {
            rd.unresolved_var = args.items[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "CONFLICTING_DEPENDENCIES_PREFIX") && i + 1 < args.count) {
            rd.conflicts_prefix = args.items[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "EXECUTABLES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.executables, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "LIBRARIES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.libraries, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "MODULES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.modules, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "DIRECTORIES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.directories, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "POST_INCLUDE_FILES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_include_files, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "POST_EXCLUDE_FILES")) {
            i++;
            if (!runtime_parse_lists(ctx, args, &i, &rd.post_exclude_files, k_stops, NOB_ARRAY_LEN(k_stops))) return true;
            continue;
        }

        // Accepted but not yet used in this backend (regex filters).
        if (eval_sv_eq_ci_lit(args.items[i], "PRE_INCLUDE_REGEXES") ||
            eval_sv_eq_ci_lit(args.items[i], "PRE_EXCLUDE_REGEXES") ||
            eval_sv_eq_ci_lit(args.items[i], "POST_INCLUDE_REGEXES") ||
            eval_sv_eq_ci_lit(args.items[i], "POST_EXCLUDE_REGEXES")) {
            i++;
            while (i < args.count) {
                bool is_kw = false;
                for (size_t s = 0; s < NOB_ARRAY_LEN(k_stops); s++) {
                    if (eval_sv_eq_ci_lit(args.items[i], k_stops[s])) {
                        is_kw = true;
                        break;
                    }
                }
                if (is_kw) break;
                i++;
            }
            continue;
        }

        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GET_RUNTIME_DEPENDENCIES) received unexpected argument"),
                       args.items[i]);
        return true;
    }

    if (rd.resolved_var.count == 0 && rd.unresolved_var.count == 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(GET_RUNTIME_DEPENDENCIES) requires RESOLVED_DEPENDENCIES_VAR or UNRESOLVED_DEPENDENCIES_VAR"),
                       nob_sv_from_cstr("Provide at least one output variable"));
        return true;
    }

    // Pragmatic Linux backend: keep deterministic outputs; deep ELF traversal is handled in a follow-up phase.
    if (rd.resolved_var.count > 0) (void)eval_var_set(ctx, rd.resolved_var, nob_sv_from_cstr(""));
    if (rd.unresolved_var.count > 0) (void)eval_var_set(ctx, rd.unresolved_var, nob_sv_from_cstr(""));
    if (rd.conflicts_prefix.count > 0) {
        Nob_String_Builder key = {0};
        nob_sb_append_buf(&key, rd.conflicts_prefix.data, rd.conflicts_prefix.count);
        nob_sb_append_cstr(&key, "_FILENAMES");
        nob_sb_append_null(&key);
        (void)eval_var_set(ctx, nob_sv_from_cstr(key.items), nob_sv_from_cstr(""));
        nob_sb_free(key);
    }
    return true;
}

bool eval_file_handle_extra(Evaluator_Context *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || args.count == 0) return false;

    if (eval_hash_is_supported_algo(args.items[0])) return handle_file_hash(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "CONFIGURE")) return handle_file_configure(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "COPY_FILE")) return handle_file_copy_file(ctx, node, args);
    if (eval_sv_eq_ci_lit(args.items[0], "TOUCH")) return handle_file_touch(ctx, node, args, false);
    if (eval_sv_eq_ci_lit(args.items[0], "TOUCH_NOCREATE")) return handle_file_touch(ctx, node, args, true);
    if (eval_sv_eq_ci_lit(args.items[0], "GET_RUNTIME_DEPENDENCIES")) {
        return handle_file_get_runtime_dependencies(ctx, node, args);
    }

    return false;
}
