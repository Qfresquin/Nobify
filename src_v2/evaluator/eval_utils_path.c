#include "evaluator_internal.h"

#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <string.h>

bool eval_split_shell_like_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
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

static bool eval_split_windows_command_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;

    size_t i = 0;
    while (i < input.count) {
        while (i < input.count && isspace((unsigned char)input.data[i])) i++;
        if (i >= input.count) break;

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

        size_t off = 0;
        bool in_quotes = false;
        while (i < input.count) {
            char c = input.data[i];
            if (!in_quotes && isspace((unsigned char)c)) break;

            if (c == '\\') {
                size_t slash_count = 0;
                while (i + slash_count < input.count && input.data[i + slash_count] == '\\') slash_count++;

                bool next_is_quote = (i + slash_count < input.count && input.data[i + slash_count] == '"');
                if (next_is_quote) {
                    size_t literal_slashes = slash_count / 2;
                    for (size_t si = 0; si < literal_slashes; si++) buf[off++] = '\\';
                    if ((slash_count % 2) == 0) {
                        in_quotes = !in_quotes;
                    } else {
                        buf[off++] = '"';
                    }
                    i += slash_count + 1;
                    continue;
                }

                for (size_t si = 0; si < slash_count; si++) buf[off++] = '\\';
                i += slash_count;
                continue;
            }

            if (c == '"') {
                if (in_quotes && i + 1 < input.count && input.data[i + 1] == '"') {
                    buf[off++] = '"';
                    i += 2;
                    continue;
                }
                in_quotes = !in_quotes;
                i++;
                continue;
            }

            buf[off++] = c;
            i++;
        }

        buf[off] = '\0';
        if (!svu_list_push_temp(ctx, out, nob_sv_from_parts(buf, off))) return false;
    }

    return true;
}

static Eval_Cmdline_Mode eval_cmdline_mode_normalized(Eval_Cmdline_Mode mode) {
    if (mode == EVAL_CMDLINE_NATIVE) {
#if defined(_WIN32)
        return EVAL_CMDLINE_WINDOWS;
#else
        return EVAL_CMDLINE_UNIX;
#endif
    }
    return mode;
}

static bool eval_split_unix_program_from_args_temp(Evaluator_Context *ctx,
                                                   String_View input,
                                                   String_View *out_program,
                                                   String_View *out_args) {
    if (!ctx || !out_program || !out_args) return false;
    *out_program = nob_sv_from_cstr("");
    *out_args = nob_sv_from_cstr("");

    size_t i = 0;
    while (i < input.count && isspace((unsigned char)input.data[i])) i++;
    if (i >= input.count) return true;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t off = 0;
    char quote = '\0';
    while (i < input.count) {
        char c = input.data[i];
        if (quote != '\0') {
            if (c == quote) {
                quote = '\0';
                i++;
                continue;
            }
            if (c == '\\' && quote == '"' && i + 1 < input.count) {
                buf[off++] = input.data[i + 1];
                i += 2;
                continue;
            }
            buf[off++] = c;
            i++;
            continue;
        }

        if (isspace((unsigned char)c)) break;
        if (c == '"' || c == '\'') {
            quote = c;
            i++;
            continue;
        }
        if (c == '\\' && i + 1 < input.count) {
            buf[off++] = input.data[i + 1];
            i += 2;
            continue;
        }
        buf[off++] = c;
        i++;
    }

    buf[off] = '\0';
    *out_program = nob_sv_from_parts(buf, off);
    if (i < input.count) *out_args = nob_sv_from_parts(input.data + i, input.count - i);
    return true;
}

static bool eval_split_windows_program_from_args_temp(Evaluator_Context *ctx,
                                                      String_View input,
                                                      String_View *out_program,
                                                      String_View *out_args) {
    if (!ctx || !out_program || !out_args) return false;
    *out_program = nob_sv_from_cstr("");
    *out_args = nob_sv_from_cstr("");

    size_t i = 0;
    while (i < input.count && isspace((unsigned char)input.data[i])) i++;
    if (i >= input.count) return true;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t off = 0;
    bool in_quotes = false;
    while (i < input.count) {
        char c = input.data[i];
        if (!in_quotes && isspace((unsigned char)c)) break;

        if (c == '\\') {
            size_t slash_count = 0;
            while (i + slash_count < input.count && input.data[i + slash_count] == '\\') slash_count++;

            bool next_is_quote = (i + slash_count < input.count && input.data[i + slash_count] == '"');
            if (next_is_quote) {
                size_t literal_slashes = slash_count / 2;
                for (size_t si = 0; si < literal_slashes; si++) buf[off++] = '\\';
                if ((slash_count % 2) == 0) {
                    in_quotes = !in_quotes;
                } else {
                    buf[off++] = '"';
                }
                i += slash_count + 1;
                continue;
            }

            for (size_t si = 0; si < slash_count; si++) buf[off++] = '\\';
            i += slash_count;
            continue;
        }

        if (c == '"') {
            if (in_quotes && i + 1 < input.count && input.data[i + 1] == '"') {
                buf[off++] = '"';
                i += 2;
                continue;
            }
            in_quotes = !in_quotes;
            i++;
            continue;
        }

        buf[off++] = c;
        i++;
    }

    buf[off] = '\0';
    *out_program = nob_sv_from_parts(buf, off);
    if (i < input.count) *out_args = nob_sv_from_parts(input.data + i, input.count - i);
    return true;
}

bool eval_split_program_from_command_line_temp(Evaluator_Context *ctx,
                                               Eval_Cmdline_Mode mode,
                                               String_View input,
                                               String_View *out_program,
                                               String_View *out_args) {
    if (!ctx || !out_program || !out_args) return false;

    mode = eval_cmdline_mode_normalized(mode);
    if (mode == EVAL_CMDLINE_WINDOWS) {
        return eval_split_windows_program_from_args_temp(ctx, input, out_program, out_args);
    }
    return eval_split_unix_program_from_args_temp(ctx, input, out_program, out_args);
}

static bool eval_program_token_contains_path_sep(String_View value) {
    if (value.count == 0) return false;
    for (size_t i = 0; i < value.count; i++) {
        if (svu_is_path_sep(value.data[i])) return true;
    }
    return false;
}

static bool eval_program_candidate_is_file(Evaluator_Context *ctx, String_View candidate) {
    if (!ctx || candidate.count == 0) return false;
    char *path_c = eval_sv_to_cstr_temp(ctx, candidate);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    if (!nob_file_exists(path_c)) return false;
    Nob_File_Type kind = nob_get_file_type(path_c);
    return kind == NOB_FILE_REGULAR || kind == NOB_FILE_SYMLINK;
}

static String_View eval_trim_whitespace_view(String_View input) {
    return svu_trim_ascii_ws(input);
}

bool eval_find_program_full_path_temp(Evaluator_Context *ctx,
                                      String_View token,
                                      String_View *out_program,
                                      bool *out_found) {
    if (!ctx || !out_program) return false;
    *out_program = nob_sv_from_cstr("");
    if (out_found) *out_found = false;

    String_View trimmed = eval_trim_whitespace_view(token);
    if (trimmed.count == 0) return true;

    String_View current_src = eval_current_source_dir(ctx);
    if (eval_sv_is_abs_path(trimmed) || eval_program_token_contains_path_sep(trimmed)) {
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, trimmed, current_src, false);
        if (eval_should_stop(ctx)) return false;
        *out_program = resolved;
        if (eval_program_candidate_is_file(ctx, resolved) && out_found) *out_found = true;
        return true;
    }

    const char *path_env = eval_getenv_temp(ctx, "PATH");
    if (!path_env || path_env[0] == '\0') return true;

    String_View raw_path = nob_sv_from_cstr(path_env);
    const char path_sep =
#if defined(_WIN32)
        ';';
#else
        ':';
#endif

#if defined(_WIN32)
    static const char *const k_exts[] = {"", ".exe", ".cmd", ".bat", ".com"};
#else
    static const char *const k_exts[] = {""};
#endif

    const char *p = raw_path.data;
    const char *end = raw_path.data + raw_path.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != path_sep) q++;
        String_View dir = nob_sv_from_parts(p, (size_t)(q - p));
        if (dir.count > 0) {
            for (size_t ei = 0; ei < NOB_ARRAY_LEN(k_exts); ei++) {
                String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dir, trimmed);
                if (eval_should_stop(ctx)) return false;
                if (k_exts[ei][0] != '\0') {
                    candidate = svu_concat_suffix_temp(ctx, candidate, k_exts[ei]);
                    if (eval_should_stop(ctx)) return false;
                }
                if (!eval_program_candidate_is_file(ctx, candidate)) continue;
                *out_program = eval_sv_path_normalize_temp(ctx, candidate);
                if (eval_should_stop(ctx)) return false;
                if (out_found) *out_found = true;
                return true;
            }
        }
        if (q >= end) break;
        p = q + 1;
    }

    return true;
}

bool eval_split_command_line_temp(Evaluator_Context *ctx,
                                  Eval_Cmdline_Mode mode,
                                  String_View input,
                                  SV_List *out_tokens) {
    if (!ctx || !out_tokens) return false;

    mode = eval_cmdline_mode_normalized(mode);

    if (mode == EVAL_CMDLINE_WINDOWS) return eval_split_windows_command_temp(ctx, input, out_tokens);
    return eval_split_shell_like_temp(ctx, input, out_tokens);
}

bool eval_sv_is_abs_path(String_View p) {
    if (p.count == 0) return false;
    if ((p.count >= 2) &&
        (p.data[0] == '/' || p.data[0] == '\\') &&
        (p.data[1] == '/' || p.data[1] == '\\')) {
        return true;
    }
    if (p.count > 1 && p.data[1] == ':') return true;
    if (p.data[0] == '/' || p.data[0] == '\\') return true;
    return false;
}

String_View eval_sv_path_join(Arena *arena, String_View a, String_View b) {
    if (!arena) return nob_sv_from_cstr("");
    if (a.count == 0) return sv_copy_to_arena(arena, b);
    if (b.count == 0) return sv_copy_to_arena(arena, a);

    bool need_slash = !svu_is_path_sep(a.data[a.count - 1]);
    size_t total = a.count + (need_slash ? 1 : 0) + b.count;

    char *buf = (char*)arena_alloc(arena, total + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t off = 0;
    memcpy(buf + off, a.data, a.count);
    off += a.count;
    if (need_slash) buf[off++] = '/';
    memcpy(buf + off, b.data, b.count);
    off += b.count;
    buf[off] = '\0';

    return nob_sv_from_cstr(buf);
}

String_View eval_sv_path_normalize_temp(Evaluator_Context *ctx, String_View input) {
    if (!ctx) return nob_sv_from_cstr("");
    if (input.count == 0) return nob_sv_from_cstr(".");

    bool is_unc = input.count >= 2 && svu_is_path_sep(input.data[0]) && svu_is_path_sep(input.data[1]);
    bool has_drive = input.count >= 2 &&
                     isalpha((unsigned char)input.data[0]) &&
                     input.data[1] == ':';
    bool absolute = false;
    size_t pos = 0;

    if (is_unc) {
        pos = 2;
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
    } else if (has_drive) {
        pos = 2;
        if (pos < input.count && svu_is_path_sep(input.data[pos])) {
            absolute = true;
            while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
        }
    } else if (svu_is_path_sep(input.data[0])) {
        absolute = true;
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
    }

    SV_List segments = {0};
    size_t unc_root_segments = 0;
    while (pos < input.count) {
        size_t start = pos;
        while (pos < input.count && !svu_is_path_sep(input.data[pos])) pos++;
        String_View seg = nob_sv_from_parts(input.data + start, pos - start);
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;

        if (seg.count == 0 || nob_sv_eq(seg, nob_sv_from_cstr("."))) continue;
        if (nob_sv_eq(seg, nob_sv_from_cstr(".."))) {
            if (arena_arr_len(segments) > 0 &&
                !nob_sv_eq(segments[arena_arr_len(segments) - 1], nob_sv_from_cstr("..")) &&
                (!is_unc || arena_arr_len(segments) > unc_root_segments)) {
                arena_arr_set_len(segments, arena_arr_len(segments) - 1);
                continue;
            }
            if (!absolute) {
                if (!svu_list_push_temp(ctx, &segments, seg)) return nob_sv_from_cstr("");
            }
            continue;
        }

        if (!svu_list_push_temp(ctx, &segments, seg)) return nob_sv_from_cstr("");
        if (is_unc && unc_root_segments < 2) unc_root_segments++;
    }

    size_t total = 0;
    if (is_unc) total += 2;
    else if (has_drive) total += 2;
    if (absolute && !is_unc && !has_drive) total += 1;
    if (absolute && has_drive) total += 1;

    for (size_t i = 0; i < arena_arr_len(segments); i++) {
        if (i > 0 || is_unc || absolute || (has_drive && absolute)) total += 1;
        total += segments[i].count;
    }

    if (arena_arr_len(segments) == 0) {
        if (is_unc) total += 0;
        else if (has_drive && absolute) {
            if (total == 2) total += 1;
        } else if (!has_drive && !absolute) {
            total += 1;
        }
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;

    if (is_unc) {
        buf[off++] = '/';
        buf[off++] = '/';
    } else if (has_drive) {
        buf[off++] = input.data[0];
        buf[off++] = ':';
        if (absolute) buf[off++] = '/';
    } else if (absolute) {
        buf[off++] = '/';
    }

    for (size_t i = 0; i < arena_arr_len(segments); i++) {
        if (off > 0 && buf[off - 1] != '/') buf[off++] = '/';
        memcpy(buf + off, segments[i].data, segments[i].count);
        off += segments[i].count;
    }

    if (arena_arr_len(segments) == 0) {
        if (has_drive && absolute) {
            if (off == 2) buf[off++] = '/';
        } else if (!is_unc && !has_drive && !absolute) {
            buf[off++] = '.';
        }
    }

    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View eval_path_resolve_for_cmake_arg(Evaluator_Context *ctx,
                                            String_View raw_path,
                                            String_View base_dir,
                                            bool preserve_generator_expressions) {
    if (!ctx) return nob_sv_from_cstr("");
    if (raw_path.count == 0) return nob_sv_from_cstr("");

    if (preserve_generator_expressions &&
        raw_path.count >= 2 &&
        raw_path.data[0] == '$' &&
        raw_path.data[1] == '<') {
        return raw_path;
    }

    String_View resolved = raw_path;
    if (!eval_sv_is_abs_path(resolved)) {
        resolved = eval_sv_path_join(eval_temp_arena(ctx), base_dir, resolved);
    }
    return eval_sv_path_normalize_temp(ctx, resolved);
}
