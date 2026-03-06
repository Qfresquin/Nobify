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

bool eval_split_command_line_temp(Evaluator_Context *ctx,
                                  Eval_Cmdline_Mode mode,
                                  String_View input,
                                  SV_List *out_tokens) {
    if (!ctx || !out_tokens) return false;

    if (mode == EVAL_CMDLINE_NATIVE) {
#if defined(_WIN32)
        mode = EVAL_CMDLINE_WINDOWS;
#else
        mode = EVAL_CMDLINE_UNIX;
#endif
    }

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
