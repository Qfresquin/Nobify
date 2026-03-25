#include "eval_file_internal.h"
#include "eval_expr.h"
#include "eval_hash.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <ctype.h>
#if !defined(_WIN32)
#include <regex.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "stb_ds.h"

static String_View file_apply_newline_style(EvalExecContext *ctx, String_View in, String_View style);

static bool file_read_bytes(EvalExecContext *ctx, String_View path, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);
    return nob_read_entire_file(p, out);
}

static bool file_write_bytes(EvalExecContext *ctx, String_View path, const char *data, size_t len) {
    if (!ctx) return false;
    if (!eval_file_mkdir_p(ctx, svu_dirname(path))) return false;
    char *p = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, p, false);
    return nob_write_entire_file(p, data, len);
}

static bool file_same_content(EvalExecContext *ctx, String_View path, String_View content, bool *out_same) {
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

static String_View file_cache_var_get(EvalExecContext *ctx, String_View key) {
    if (!ctx || !ctx->scope_state.cache_entries || key.count == 0 || !key.data) return nob_sv_from_cstr("");
    Eval_Cache_Entry *entry = stbds_shgetp_null(ctx->scope_state.cache_entries, nob_temp_sv_to_cstr(key));
    return entry ? entry->value.data : nob_sv_from_cstr("");
}

static void file_append_configure_value(Nob_String_Builder *out, String_View val, bool escape_quotes) {
    if (!out || val.count == 0) return;
    if (!escape_quotes) {
        nob_sb_append_buf(out, val.data, val.count);
        return;
    }
    for (size_t i = 0; i < val.count; i++) {
        if (val.data[i] == '"') nob_sb_append(out, '\\');
        nob_sb_append(out, val.data[i]);
    }
}

static bool handle_file_hash(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) != 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(<HASH>) requires filename and output variable"), nob_sv_from_cstr("Usage: file(<HASH> <filename> <out-var>)"));
        return true;
    }

    String_View in_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args[1],
                                eval_file_current_src_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &in_path)) {
        return true;
    }

    Nob_String_Builder data = {0};
    if (!file_read_bytes(ctx, in_path, &data)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(<HASH>) failed to read file"), in_path);
        return true;
    }

    String_View digest = nob_sv_from_cstr("");
    String_View payload = nob_sv_from_parts(data.items, data.count);
    bool ok = eval_hash_compute_hex_temp(ctx, args[0], payload, &digest);
    nob_sb_free(data);
    if (!ok) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "eval_file", nob_sv_from_cstr("Unsupported hash algorithm"), args[0]);
        return true;
    }

    (void)eval_var_set_current(ctx, args[2], digest);
    return true;
}

static String_View file_expand_configure_once(EvalExecContext *ctx,
                                              String_View input,
                                              bool at_only,
                                              bool escape_quotes) {
    if (!ctx) return nob_sv_from_cstr("");
    Nob_String_Builder out = {0};

    for (size_t i = 0; i < input.count;) {
        if (!at_only && input.data[i] == '$') {
            if (i + 2 < input.count && input.data[i + 1] == '{') {
                size_t j = i + 2;
                while (j < input.count && input.data[j] != '}') j++;
                if (j < input.count && input.data[j] == '}') {
                    String_View key = nob_sv_from_parts(input.data + i + 2, j - (i + 2));
                    file_append_configure_value(&out, eval_var_get_visible(ctx, key), escape_quotes);
                    i = j + 1;
                    continue;
                }
            }
            if (i + 5 < input.count &&
                input.data[i + 1] == 'E' &&
                input.data[i + 2] == 'N' &&
                input.data[i + 3] == 'V' &&
                input.data[i + 4] == '{') {
                size_t j = i + 5;
                while (j < input.count && input.data[j] != '}') j++;
                if (j < input.count && input.data[j] == '}') {
                    String_View key = nob_sv_from_parts(input.data + i + 5, j - (i + 5));
                    const char *env_c = eval_getenv_temp(ctx, eval_sv_to_cstr_temp(ctx, key));
                    String_View env = env_c ? nob_sv_from_cstr(env_c) : nob_sv_from_cstr("");
                    file_append_configure_value(&out, env, escape_quotes);
                    i = j + 1;
                    continue;
                }
            }
            if (i + 7 < input.count &&
                input.data[i + 1] == 'C' &&
                input.data[i + 2] == 'A' &&
                input.data[i + 3] == 'C' &&
                input.data[i + 4] == 'H' &&
                input.data[i + 5] == 'E' &&
                input.data[i + 6] == '{') {
                size_t j = i + 7;
                while (j < input.count && input.data[j] != '}') j++;
                if (j < input.count && input.data[j] == '}') {
                    String_View key = nob_sv_from_parts(input.data + i + 7, j - (i + 7));
                    file_append_configure_value(&out, file_cache_var_get(ctx, key), escape_quotes);
                    i = j + 1;
                    continue;
                }
            }
        }
        if (i + 2 < input.count && input.data[i] == '@') {
            size_t j = i + 1;
            while (j < input.count && input.data[j] != '@') j++;
            if (j < input.count && input.data[j] == '@' && j > i + 1) {
                String_View key = nob_sv_from_parts(input.data + i + 1, j - (i + 1));
                file_append_configure_value(&out, eval_var_get_visible(ctx, key), escape_quotes);
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

static bool configure_file_perm_add_token(mode_t *mode, String_View token) {
    if (!mode) return false;
    if (eval_sv_eq_ci_lit(token, "OWNER_READ")) {
        *mode |= S_IRUSR;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "OWNER_WRITE")) {
        *mode |= S_IWUSR;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "OWNER_EXECUTE")) {
        *mode |= S_IXUSR;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_READ")) {
        *mode |= S_IRGRP;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_WRITE")) {
        *mode |= S_IWGRP;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_EXECUTE")) {
        *mode |= S_IXGRP;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_READ")) {
        *mode |= S_IROTH;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_WRITE")) {
        *mode |= S_IWOTH;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_EXECUTE")) {
        *mode |= S_IXOTH;
        return true;
    }
    return false;
}

static bool configure_file_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "COPYONLY") ||
           eval_sv_eq_ci_lit(tok, "@ONLY") ||
           eval_sv_eq_ci_lit(tok, "ESCAPE_QUOTES") ||
           eval_sv_eq_ci_lit(tok, "NEWLINE_STYLE") ||
           eval_sv_eq_ci_lit(tok, "NO_SOURCE_PERMISSIONS") ||
           eval_sv_eq_ci_lit(tok, "USE_SOURCE_PERMISSIONS") ||
           eval_sv_eq_ci_lit(tok, "FILE_PERMISSIONS");
}

static bool configure_file_valid_newline_style(String_View style) {
    return eval_sv_eq_ci_lit(style, "UNIX") ||
           eval_sv_eq_ci_lit(style, "LF") ||
           eval_sv_eq_ci_lit(style, "DOS") ||
           eval_sv_eq_ci_lit(style, "WIN32") ||
           eval_sv_eq_ci_lit(style, "CRLF");
}

static String_View configure_file_basename(String_View path) {
    size_t base = 0;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') base = i + 1;
    }
    return nob_sv_from_parts(path.data + base, path.count - base);
}

static bool configure_file_apply_permissions(String_View path, mode_t mode) {
    const char *path_c = nob_temp_sv_to_cstr(path);
    return path_c && chmod(path_c, mode) == 0;
}

static String_View configure_file_process_line(EvalExecContext *ctx,
                                               String_View line,
                                               bool at_only,
                                               bool escape_quotes) {
    if (!ctx || line.count == 0) return file_expand_configure_once(ctx, line, at_only, escape_quotes);

    size_t i = 0;
    while (i < line.count && (line.data[i] == ' ' || line.data[i] == '\t')) i++;
    if (i >= line.count || line.data[i] != '#') return file_expand_configure_once(ctx, line, at_only, escape_quotes);

    size_t hash = i;
    i++;
    size_t pad_start = i;
    while (i < line.count && (line.data[i] == ' ' || line.data[i] == '\t')) i++;
    String_View pad = nob_sv_from_parts(line.data + pad_start, i - pad_start);

    bool is_define01 = false;
    const char *kw = "cmakedefine";
    size_t kw_len = strlen(kw);
    if (i + kw_len <= line.count && memcmp(line.data + i, kw, kw_len) == 0) {
        i += kw_len;
        if (i + 2 <= line.count && line.data[i] == '0' && line.data[i + 1] == '1') {
            is_define01 = true;
            i += 2;
        }
    } else {
        return file_expand_configure_once(ctx, line, at_only, escape_quotes);
    }

    if (i < line.count && !(line.data[i] == ' ' || line.data[i] == '\t')) {
        return file_expand_configure_once(ctx, line, at_only, escape_quotes);
    }
    while (i < line.count && (line.data[i] == ' ' || line.data[i] == '\t')) i++;
    if (i >= line.count) return file_expand_configure_once(ctx, line, at_only, escape_quotes);

    size_t var_start = i;
    while (i < line.count && !isspace((unsigned char)line.data[i])) i++;
    String_View key = nob_sv_from_parts(line.data + var_start, i - var_start);
    while (i < line.count && (line.data[i] == ' ' || line.data[i] == '\t')) i++;
    String_View suffix = nob_sv_from_parts(line.data + i, line.count - i);

    String_View val = eval_var_get_visible(ctx, key);
    bool truthy = eval_truthy(ctx, val);

    Nob_String_Builder sb = {0};
    if (is_define01) {
        if (hash + 1 > 0) nob_sb_append_buf(&sb, line.data, hash + 1);
        if (pad.count > 0) nob_sb_append_buf(&sb, pad.data, pad.count);
        nob_sb_append_cstr(&sb, "define ");
        if (key.count > 0) nob_sb_append_buf(&sb, key.data, key.count);
        nob_sb_append_cstr(&sb, truthy ? " 1" : " 0");
    } else if (truthy) {
        if (hash + 1 > 0) nob_sb_append_buf(&sb, line.data, hash + 1);
        if (pad.count > 0) nob_sb_append_buf(&sb, pad.data, pad.count);
        nob_sb_append_cstr(&sb, "define ");
        if (key.count > 0) nob_sb_append_buf(&sb, key.data, key.count);
        if (suffix.count > 0) {
            nob_sb_append(&sb, ' ');
            String_View expanded = file_expand_configure_once(ctx, suffix, at_only, escape_quotes);
            file_append_configure_value(&sb, expanded, false);
        }
    } else {
        if (hash > 0) nob_sb_append_buf(&sb, line.data, hash);
        nob_sb_append_cstr(&sb, "/* #undef ");
        if (key.count > 0) nob_sb_append_buf(&sb, key.data, key.count);
        nob_sb_append_cstr(&sb, " */");
    }
    nob_sb_append_null(&sb);
    String_View out = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items, sb.count - 1));
    nob_sb_free(sb);
    return out;
}

static String_View configure_file_expand_content(EvalExecContext *ctx,
                                                String_View input,
                                                bool at_only,
                                                bool escape_quotes) {
    if (!ctx) return nob_sv_from_cstr("");

    Nob_String_Builder sb = {0};
    size_t start = 0;
    while (start <= input.count) {
        size_t end = start;
        while (end < input.count && input.data[end] != '\n' && input.data[end] != '\r') end++;

        String_View line = nob_sv_from_parts(input.data + start, end - start);
        String_View processed = configure_file_process_line(ctx, line, at_only, escape_quotes);
        nob_sb_append_buf(&sb, processed.data, processed.count);

        if (end >= input.count) break;
        if (input.data[end] == '\r' && end + 1 < input.count && input.data[end + 1] == '\n') {
            nob_sb_append_cstr(&sb, "\r\n");
            start = end + 2;
        } else {
            nob_sb_append(&sb, input.data[end]);
            start = end + 1;
        }
    }

    nob_sb_append_null(&sb);
    String_View out = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items, sb.count - 1));
    nob_sb_free(sb);
    return out;
}

Eval_Result eval_handle_configure_file(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(args) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "configure_file", nob_sv_from_cstr("configure_file() requires input and output paths"), nob_sv_from_cstr("Usage: configure_file(<input> <output> [COPYONLY] [@ONLY] [ESCAPE_QUOTES] [NEWLINE_STYLE <style>] [NO_SOURCE_PERMISSIONS|USE_SOURCE_PERMISSIONS|FILE_PERMISSIONS <perms>...])"));
        return eval_result_from_ctx(ctx);
    }

    bool copyonly = false;
    bool at_only = false;
    bool escape_quotes = false;
    bool saw_use_source_permissions = false;
    bool saw_no_source_permissions = false;
    bool saw_file_permissions = false;
    String_View newline_style = nob_sv_from_cstr("");
    mode_t file_mode = 0;

    for (size_t i = 2; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "COPYONLY")) {
            copyonly = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "@ONLY")) {
            at_only = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "ESCAPE_QUOTES")) {
            escape_quotes = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "NEWLINE_STYLE")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "configure_file", nob_sv_from_cstr("configure_file(NEWLINE_STYLE) requires a value"), args[i]);
                return eval_result_from_ctx(ctx);
            }
            newline_style = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "USE_SOURCE_PERMISSIONS")) {
            saw_use_source_permissions = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "NO_SOURCE_PERMISSIONS")) {
            saw_no_source_permissions = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "FILE_PERMISSIONS")) {
            saw_file_permissions = true;
            for (i = i + 1; i < arena_arr_len(args) && !configure_file_keyword(args[i]); i++) {
                if (!configure_file_perm_add_token(&file_mode, args[i])) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "configure_file", nob_sv_from_cstr("configure_file() unknown FILE_PERMISSIONS token"), args[i]);
                    return eval_result_from_ctx(ctx);
                }
            }
            i--;
            continue;
        }

        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "configure_file", nob_sv_from_cstr("configure_file() received unexpected argument"), args[i]);
        return eval_result_from_ctx(ctx);
    }

    if (newline_style.count > 0 && !configure_file_valid_newline_style(newline_style)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "configure_file", nob_sv_from_cstr("configure_file(NEWLINE_STYLE) received invalid value"), newline_style);
        return eval_result_from_ctx(ctx);
    }

    if (copyonly && newline_style.count > 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "configure_file", nob_sv_from_cstr("configure_file(COPYONLY) cannot be combined with NEWLINE_STYLE"), nob_sv_from_cstr("COPYONLY preserves the input bytes and therefore cannot rewrite line endings"));
        return eval_result_from_ctx(ctx);
    }
    if ((saw_use_source_permissions ? 1 : 0) + (saw_no_source_permissions ? 1 : 0) + (saw_file_permissions ? 1 : 0) > 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_CONFLICTING_OPTIONS, "configure_file", nob_sv_from_cstr("configure_file() received conflicting permission options"), nob_sv_from_cstr("Use only one of USE_SOURCE_PERMISSIONS, NO_SOURCE_PERMISSIONS, or FILE_PERMISSIONS"));
        return eval_result_from_ctx(ctx);
    }
    if (saw_file_permissions && file_mode == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "configure_file", nob_sv_from_cstr("configure_file(FILE_PERMISSIONS) requires at least one valid permission token"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    String_View in_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx, node, o, args[0], eval_file_current_src_dir(ctx), EVAL_FILE_PATH_MODE_CMAKE, &in_path)) {
        return eval_result_from_ctx(ctx);
    }
    String_View out_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx, node, o, args[1], eval_file_current_bin_dir(ctx), EVAL_FILE_PATH_MODE_CMAKE, &out_path)) {
        return eval_result_from_ctx(ctx);
    }

    char *in_c = eval_sv_to_cstr_temp(ctx, in_path);
    char *out_c = eval_sv_to_cstr_temp(ctx, out_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, in_c, eval_result_fatal());
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, eval_result_fatal());

    struct stat src_st = {0};
    if (stat(in_c, &src_st) != 0 || !S_ISREG(src_st.st_mode)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "configure_file", nob_sv_from_cstr("configure_file() input must name an existing regular file"), in_path);
        return eval_result_from_ctx(ctx);
    }

    struct stat out_st = {0};
    if (stat(out_c, &out_st) == 0 && S_ISDIR(out_st.st_mode)) {
        out_path = eval_sv_path_join(eval_temp_arena(ctx), out_path, configure_file_basename(in_path));
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    }

    Nob_String_Builder src_buf = {0};
    if (!file_read_bytes(ctx, in_path, &src_buf)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "configure_file", nob_sv_from_cstr("configure_file() failed to read input file"), in_path);
        return eval_result_from_ctx(ctx);
    }

    String_View src_text = nob_sv_from_parts(src_buf.items, src_buf.count);
    String_View out_text = copyonly ? sv_copy_to_temp_arena(ctx, src_text)
                                    : configure_file_expand_content(ctx, src_text, at_only, escape_quotes);
    nob_sb_free(src_buf);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    out_text = file_apply_newline_style(ctx, out_text, newline_style);

    bool same = false;
    if (!file_same_content(ctx, out_path, out_text, &same)) return eval_result_from_ctx(ctx);
    if (!same) {
        if (!file_write_bytes(ctx, out_path, out_text.data, out_text.count)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "configure_file", nob_sv_from_cstr("configure_file() failed to write output file"), out_path);
            return eval_result_from_ctx(ctx);
        }
    }

    mode_t mode = 0;
    if (saw_file_permissions) {
        mode = file_mode;
    } else if (saw_no_source_permissions) {
        mode = 0644;
    } else {
        mode = src_st.st_mode & 0777;
    }
    (void)configure_file_apply_permissions(out_path, mode);
    return eval_result_from_ctx(ctx);
}

static String_View file_apply_newline_style(EvalExecContext *ctx, String_View in, String_View style) {
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

static bool handle_file_configure(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    String_View output = nob_sv_from_cstr("");
    String_View content = nob_sv_from_cstr("");
    String_View newline_style = nob_sv_from_cstr("");
    bool at_only = false;
    bool escape_quotes = false;

    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "OUTPUT") && i + 1 < arena_arr_len(args)) {
            output = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "CONTENT") && i + 1 < arena_arr_len(args)) {
            content = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "NEWLINE_STYLE") && i + 1 < arena_arr_len(args)) {
            newline_style = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "@ONLY")) {
            at_only = true;
        } else if (eval_sv_eq_ci_lit(args[i], "ESCAPE_QUOTES")) {
            escape_quotes = true;
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_file", nob_sv_from_cstr("file(CONFIGURE) received unexpected argument"), args[i]);
            return true;
        }
    }

    if (output.count == 0 || content.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(CONFIGURE) requires OUTPUT and CONTENT"), nob_sv_from_cstr("Usage: file(CONFIGURE OUTPUT <out> CONTENT <text> [@ONLY] [ESCAPE_QUOTES] [NEWLINE_STYLE <style>])"));
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
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(CONFIGURE) failed to write OUTPUT"), out_path);
    }
    return true;
}

static bool file_copy_file_do(EvalExecContext *ctx, String_View src, String_View dst, bool only_if_different) {
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

static bool handle_file_copy_file(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(COPY_FILE) requires source and destination"), nob_sv_from_cstr("Usage: file(COPY_FILE <old> <new> [RESULT <var>] [ONLY_IF_DIFFERENT] [INPUT_MAY_BE_RECENT])"));
        return true;
    }

    bool only_if_different = false;
    String_View result_var = nob_sv_from_cstr("");
    for (size_t i = 3; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "ONLY_IF_DIFFERENT")) {
            only_if_different = true;
        } else if (eval_sv_eq_ci_lit(args[i], "INPUT_MAY_BE_RECENT")) {
            // Accepted for parity; no extra behavior needed in evaluator backend.
        } else if (eval_sv_eq_ci_lit(args[i], "RESULT") && i + 1 < arena_arr_len(args)) {
            result_var = args[++i];
        } else {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "eval_file", nob_sv_from_cstr("file(COPY_FILE) received unexpected argument"), args[i]);
            return true;
        }
    }

    String_View src = nob_sv_from_cstr("");
    String_View dst = nob_sv_from_cstr("");
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args[1],
                                eval_file_current_src_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &src)) {
        return true;
    }
    if (!eval_file_resolve_path(ctx,
                                node,
                                o,
                                args[2],
                                eval_file_current_bin_dir(ctx),
                                EVAL_FILE_PATH_MODE_CMAKE,
                                &dst)) {
        return true;
    }

    bool ok = file_copy_file_do(ctx, src, dst, only_if_different);
    if (result_var.count > 0) {
        (void)eval_var_set_current(ctx, result_var, ok ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"));
        return true;
    }

    if (!ok) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY_FILE) failed"), src);
    }
    return true;
}

static bool file_touch_one(EvalExecContext *ctx, String_View path, bool create) {
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

static bool handle_file_touch(EvalExecContext *ctx, const Node *node, SV_List args, bool nocreate) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nocreate ? nob_sv_from_cstr("file(TOUCH_NOCREATE) requires at least one file")
                                : nob_sv_from_cstr("file(TOUCH) requires at least one file"), nocreate ? nob_sv_from_cstr("Usage: file(TOUCH_NOCREATE <file>...)")
                                : nob_sv_from_cstr("Usage: file(TOUCH <file>...)"));
        return true;
    }

    for (size_t i = 1; i < arena_arr_len(args); i++) {
        String_View path = nob_sv_from_cstr("");
        if (!eval_file_resolve_path(ctx,
                                    node,
                                    o,
                                    args[i],
                                    eval_file_current_bin_dir(ctx),
                                    EVAL_FILE_PATH_MODE_CMAKE,
                                    &path)) {
            return true;
        }
        if (!file_touch_one(ctx, path, !nocreate)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nocreate ? nob_sv_from_cstr("file(TOUCH_NOCREATE) failed")
                                    : nob_sv_from_cstr("file(TOUCH) failed"), path);
            return true;
        }
    }
    return true;
}

bool eval_file_handle_extra(EvalExecContext *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || arena_arr_len(args) == 0) return false;

    if (eval_hash_is_supported_algo(args[0])) return handle_file_hash(ctx, node, args);
    if (eval_sv_eq_ci_lit(args[0], "CONFIGURE")) return handle_file_configure(ctx, node, args);
    if (eval_sv_eq_ci_lit(args[0], "COPY_FILE")) return handle_file_copy_file(ctx, node, args);
    if (eval_sv_eq_ci_lit(args[0], "TOUCH")) return handle_file_touch(ctx, node, args, false);
    if (eval_sv_eq_ci_lit(args[0], "TOUCH_NOCREATE")) return handle_file_touch(ctx, node, args, true);
    if (eval_sv_eq_ci_lit(args[0], "GET_RUNTIME_DEPENDENCIES")) return eval_file_handle_runtime_dependencies(ctx, node, args);

    return false;
}
