#include "eval_file_internal.h"
#include "arena_dyn.h"

#include <pcre2posix.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    FILE_STRINGS_ENCODING_AUTO = 0,
    FILE_STRINGS_ENCODING_UTF8,
    FILE_STRINGS_ENCODING_UTF16,
    FILE_STRINGS_ENCODING_UTF16LE,
    FILE_STRINGS_ENCODING_UTF16BE,
    FILE_STRINGS_ENCODING_UTF32,
    FILE_STRINGS_ENCODING_UTF32LE,
    FILE_STRINGS_ENCODING_UTF32BE,
    FILE_STRINGS_ENCODING_INVALID,
} File_Strings_Encoding;

static File_Strings_Encoding file_strings_parse_encoding_sv(String_View sv) {
    if (eval_sv_eq_ci_lit(sv, "UTF-8") || eval_sv_eq_ci_lit(sv, "UTF8")) return FILE_STRINGS_ENCODING_UTF8;

    if (eval_sv_eq_ci_lit(sv, "UTF-16") || eval_sv_eq_ci_lit(sv, "UTF16")) return FILE_STRINGS_ENCODING_UTF16;
    if (eval_sv_eq_ci_lit(sv, "UTF-16LE") || eval_sv_eq_ci_lit(sv, "UTF16LE")) return FILE_STRINGS_ENCODING_UTF16LE;
    if (eval_sv_eq_ci_lit(sv, "UTF-16BE") || eval_sv_eq_ci_lit(sv, "UTF16BE")) return FILE_STRINGS_ENCODING_UTF16BE;

    if (eval_sv_eq_ci_lit(sv, "UTF-32") || eval_sv_eq_ci_lit(sv, "UTF32")) return FILE_STRINGS_ENCODING_UTF32;
    if (eval_sv_eq_ci_lit(sv, "UTF-32LE") || eval_sv_eq_ci_lit(sv, "UTF32LE")) return FILE_STRINGS_ENCODING_UTF32LE;
    if (eval_sv_eq_ci_lit(sv, "UTF-32BE") || eval_sv_eq_ci_lit(sv, "UTF32BE")) return FILE_STRINGS_ENCODING_UTF32BE;

    return FILE_STRINGS_ENCODING_INVALID;
}

static File_Strings_Encoding file_strings_detect_bom(const unsigned char *bytes, size_t n, size_t *out_skip) {
    if (out_skip) *out_skip = 0;
    if (!bytes || n == 0) return FILE_STRINGS_ENCODING_AUTO;

    if (n >= 4 && bytes[0] == 0xFF && bytes[1] == 0xFE && bytes[2] == 0x00 && bytes[3] == 0x00) {
        if (out_skip) *out_skip = 4;
        return FILE_STRINGS_ENCODING_UTF32LE;
    }
    if (n >= 4 && bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0xFE && bytes[3] == 0xFF) {
        if (out_skip) *out_skip = 4;
        return FILE_STRINGS_ENCODING_UTF32BE;
    }
    if (n >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        if (out_skip) *out_skip = 3;
        return FILE_STRINGS_ENCODING_UTF8;
    }
    if (n >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        if (out_skip) *out_skip = 2;
        return FILE_STRINGS_ENCODING_UTF16LE;
    }
    if (n >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        if (out_skip) *out_skip = 2;
        return FILE_STRINGS_ENCODING_UTF16BE;
    }
    return FILE_STRINGS_ENCODING_AUTO;
}

static void file_strings_append_utf8_codepoint(Nob_String_Builder *sb, uint32_t cp) {
    if (!sb) return;
    if (cp == 0) return;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;

    if (cp <= 0x7F) {
        nob_sb_append(sb, (char)cp);
        return;
    }
    if (cp <= 0x7FF) {
        nob_sb_append(sb, (char)(0xC0 | ((cp >> 6) & 0x1F)));
        nob_sb_append(sb, (char)(0x80 | (cp & 0x3F)));
        return;
    }
    if (cp <= 0xFFFF) {
        nob_sb_append(sb, (char)(0xE0 | ((cp >> 12) & 0x0F)));
        nob_sb_append(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        nob_sb_append(sb, (char)(0x80 | (cp & 0x3F)));
        return;
    }

    nob_sb_append(sb, (char)(0xF0 | ((cp >> 18) & 0x07)));
    nob_sb_append(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
    nob_sb_append(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
    nob_sb_append(sb, (char)(0x80 | (cp & 0x3F)));
}

static bool file_strings_decode_to_utf8_temp(Evaluator_Context *ctx,
                                             const char *raw,
                                             size_t raw_n,
                                             File_Strings_Encoding requested,
                                             String_View *out_decoded) {
    if (!ctx || !out_decoded) return false;
    *out_decoded = nob_sv_from_cstr("");
    if (!raw || raw_n == 0) return true;

    const unsigned char *bytes = (const unsigned char*)raw;
    size_t bom_skip = 0;
    File_Strings_Encoding bom_enc = file_strings_detect_bom(bytes, raw_n, &bom_skip);
    File_Strings_Encoding effective = requested;
    size_t start = 0;

    if (requested == FILE_STRINGS_ENCODING_AUTO) {
        if (bom_enc != FILE_STRINGS_ENCODING_AUTO) {
            effective = bom_enc;
            start = bom_skip;
        } else {
            effective = FILE_STRINGS_ENCODING_UTF8;
        }
    } else if (requested == FILE_STRINGS_ENCODING_UTF16) {
        if (bom_enc == FILE_STRINGS_ENCODING_UTF16LE || bom_enc == FILE_STRINGS_ENCODING_UTF16BE) {
            effective = bom_enc;
            start = bom_skip;
        } else {
            effective = FILE_STRINGS_ENCODING_UTF16LE;
        }
    } else if (requested == FILE_STRINGS_ENCODING_UTF32) {
        if (bom_enc == FILE_STRINGS_ENCODING_UTF32LE || bom_enc == FILE_STRINGS_ENCODING_UTF32BE) {
            effective = bom_enc;
            start = bom_skip;
        } else {
            effective = FILE_STRINGS_ENCODING_UTF32LE;
        }
    } else {
        effective = requested;
        if ((requested == FILE_STRINGS_ENCODING_UTF8 && bom_enc == FILE_STRINGS_ENCODING_UTF8) ||
            (requested == FILE_STRINGS_ENCODING_UTF16LE && bom_enc == FILE_STRINGS_ENCODING_UTF16LE) ||
            (requested == FILE_STRINGS_ENCODING_UTF16BE && bom_enc == FILE_STRINGS_ENCODING_UTF16BE) ||
            (requested == FILE_STRINGS_ENCODING_UTF32LE && bom_enc == FILE_STRINGS_ENCODING_UTF32LE) ||
            (requested == FILE_STRINGS_ENCODING_UTF32BE && bom_enc == FILE_STRINGS_ENCODING_UTF32BE)) {
            start = bom_skip;
        }
    }

    if (effective == FILE_STRINGS_ENCODING_UTF8) {
        *out_decoded = nob_sv_from_parts(raw + start, raw_n - start);
        return true;
    }

    Nob_String_Builder decoded = {0};
    size_t i = start;

    if (effective == FILE_STRINGS_ENCODING_UTF16LE || effective == FILE_STRINGS_ENCODING_UTF16BE) {
        bool little = effective == FILE_STRINGS_ENCODING_UTF16LE;
        while (i + 1 < raw_n) {
            uint16_t w1 = little
                ? (uint16_t)((uint16_t)bytes[i] | ((uint16_t)bytes[i + 1] << 8))
                : (uint16_t)(((uint16_t)bytes[i] << 8) | (uint16_t)bytes[i + 1]);
            i += 2;

            uint32_t cp = w1;
            if (w1 >= 0xD800 && w1 <= 0xDBFF) {
                cp = 0xFFFD;
                if (i + 1 < raw_n) {
                    uint16_t w2 = little
                        ? (uint16_t)((uint16_t)bytes[i] | ((uint16_t)bytes[i + 1] << 8))
                        : (uint16_t)(((uint16_t)bytes[i] << 8) | (uint16_t)bytes[i + 1]);
                    if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                        cp = 0x10000u + (((uint32_t)(w1 - 0xD800u) << 10) | (uint32_t)(w2 - 0xDC00u));
                        i += 2;
                    }
                }
            } else if (w1 >= 0xDC00 && w1 <= 0xDFFF) {
                cp = 0xFFFD;
            }
            file_strings_append_utf8_codepoint(&decoded, cp);
        }
    } else if (effective == FILE_STRINGS_ENCODING_UTF32LE || effective == FILE_STRINGS_ENCODING_UTF32BE) {
        bool little = effective == FILE_STRINGS_ENCODING_UTF32LE;
        while (i + 3 < raw_n) {
            uint32_t cp = little
                ? ((uint32_t)bytes[i]) |
                  ((uint32_t)bytes[i + 1] << 8) |
                  ((uint32_t)bytes[i + 2] << 16) |
                  ((uint32_t)bytes[i + 3] << 24)
                : ((uint32_t)bytes[i] << 24) |
                  ((uint32_t)bytes[i + 1] << 16) |
                  ((uint32_t)bytes[i + 2] << 8) |
                  (uint32_t)bytes[i + 3];
            i += 4;
            file_strings_append_utf8_codepoint(&decoded, cp);
        }
    } else {
        return false;
    }

    nob_sb_append_null(&decoded);
    char *decoded_c = (char*)arena_alloc(eval_temp_arena(ctx), decoded.count);
    if (!decoded_c) {
        nob_sb_free(decoded);
        EVAL_OOM_RETURN_IF_NULL(ctx, decoded_c, false);
    }
    memcpy(decoded_c, decoded.items, decoded.count);
    *out_decoded = nob_sv_from_parts(decoded_c, decoded.count - 1);
    nob_sb_free(decoded);
    return true;
}

void eval_file_handle_write(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 3) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_MISSING_REQUIRED,
                             o,
                             nob_sv_from_cstr("file(WRITE) requires <path> and <content>"),
                             nob_sv_from_cstr(""));
        return;
    }

    String_View path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args[1], ctx->binary_dir, &path)) return;

    char *path_c = (char*)arena_alloc(eval_temp_arena(ctx), path.count + 1);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, path_c);
    memcpy(path_c, path.data, path.count);
    path_c[path.count] = '\0';

    char *dir_c = (char*)arena_alloc(eval_temp_arena(ctx), path.count + 1);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, dir_c);
    memcpy(dir_c, path_c, path.count + 1);
    char *last_slash = strrchr(dir_c, '/');
    if (!last_slash) last_slash = strrchr(dir_c, '\\');
    if (last_slash) {
        *last_slash = '\0';
        String_View dir = nob_sv_from_cstr(dir_c);
        if (dir.count > 0 && !eval_file_mkdir_p(ctx, dir)) {
            eval_file_diag_error(ctx,
                                 node,
                                 EVAL_DIAG_IO_FAILURE,
                                 o,
                                 nob_sv_from_cstr("file(WRITE) failed to create parent directory"),
                                 dir);
            return;
        }
    }

    FILE *f = fopen(path_c, "wb");
    if (!f) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_IO_FAILURE,
                             o,
                             nob_sv_from_cstr("file(WRITE) failed to open/create file"),
                             path);
        return;
    }

    for (size_t i = 2; i < arena_arr_len(args); i++) {
        fwrite(args[i].data, 1, args[i].count, f);
    }
    fclose(f);
    (void)eval_emit_fs_write_file(ctx, o, path);
}

void eval_file_handle_make_directory(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 2) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_MISSING_REQUIRED,
                             o,
                             nob_sv_from_cstr("file(MAKE_DIRECTORY) requires at least one path"),
                             nob_sv_from_cstr("Usage: file(MAKE_DIRECTORY <dir>...)"));
        return;
    }

    for (size_t i = 1; i < arena_arr_len(args); i++) {
        String_View path = nob_sv_from_cstr("");
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, args[i], eval_current_binary_dir(ctx), &path)) return;

        if (!eval_file_mkdir_p(ctx, path)) {
            eval_file_diag_error(ctx,
                                 node,
                                 EVAL_DIAG_IO_FAILURE,
                                 o,
                                 nob_sv_from_cstr("file(MAKE_DIRECTORY) failed to create directory"),
                                 path);
            return;
        }
        (void)eval_emit_fs_mkdir(ctx, o, path);
    }
}

void eval_file_handle_read(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 3) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_MISSING_REQUIRED,
                             o,
                             nob_sv_from_cstr("file(READ) requires <path> and <out-var>"),
                             nob_sv_from_cstr("Usage: file(READ <path> <out-var> [OFFSET n] [LIMIT n] [HEX])"));
        return;
    }

    String_View path = nob_sv_from_cstr("");
    String_View out_var = args[2];
    size_t offset = 0;
    bool has_limit = false;
    size_t limit = 0;
    bool hex = false;

    for (size_t i = 3; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "OFFSET") && i + 1 < arena_arr_len(args)) {
            (void)eval_file_parse_size_sv(args[++i], &offset);
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "LIMIT") && i + 1 < arena_arr_len(args)) {
            has_limit = eval_file_parse_size_sv(args[++i], &limit);
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "HEX")) {
            hex = true;
        }
    }

    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args[1], eval_current_source_dir(ctx), &path)) return;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, path_c);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_IO_FAILURE,
                             o,
                             nob_sv_from_cstr("file(READ) failed to read file"),
                             path);
        return;
    }

    size_t begin = offset < sb.count ? offset : sb.count;
    size_t end = sb.count;
    if (has_limit && begin + limit < end) end = begin + limit;
    size_t n = end - begin;

    if (!hex) {
        String_View content = nob_sv_from_parts(sb.items + begin, n);
        (void)eval_var_set_current(ctx, out_var, content);
        nob_sb_free(sb);
        (void)eval_emit_fs_read_file(ctx, o, path, out_var);
        return;
    }

    char *hex_buf = (char*)arena_alloc(eval_temp_arena(ctx), (n * 2) + 1);
    if (!hex_buf) {
        nob_sb_free(sb);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, hex_buf);
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char b = (unsigned char)sb.items[begin + i];
        static const char *lut = "0123456789abcdef";
        hex_buf[(i * 2) + 0] = lut[(b >> 4) & 0xF];
        hex_buf[(i * 2) + 1] = lut[b & 0xF];
    }
    hex_buf[n * 2] = '\0';
    (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(hex_buf));
    nob_sb_free(sb);
    (void)eval_emit_fs_read_file(ctx, o, path, out_var);
}

void eval_file_handle_strings(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 3) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_MISSING_REQUIRED,
                             o,
                             nob_sv_from_cstr("file(STRINGS) requires <path> and <out-var>"),
                             nob_sv_from_cstr("Usage: file(STRINGS <path> <out-var>)"));
        return;
    }

    typedef struct {
        bool has_len_min;
        bool has_len_max;
        bool has_limit_count;
        bool has_limit_input;
        bool has_limit_output;
        size_t len_min;
        size_t len_max;
        size_t limit_count;
        size_t limit_input;
        size_t limit_output;
        bool has_regex;
        String_View regex;
        bool newline_consume;
        bool no_hex_conversion;
        bool has_encoding;
        File_Strings_Encoding encoding;
        String_View unsupported;
    } File_Strings_Options;

    String_View path = nob_sv_from_cstr("");
    String_View out_var = args[2];

    File_Strings_Options opt = {0};
    String_View unsupported_items[64] = {0};
    size_t unsupported_count = 0;

    for (size_t i = 3; i < arena_arr_len(args); i++) {
        String_View t = args[i];

        if (eval_sv_eq_ci_lit(t, "LENGTH_MINIMUM") && i + 1 < arena_arr_len(args)) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args[++i], &v)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(STRINGS) invalid LENGTH_MINIMUM value"), args[i]);
                return;
            }
            opt.has_len_min = true;
            opt.len_min = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "LENGTH_MAXIMUM") && i + 1 < arena_arr_len(args)) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args[++i], &v)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(STRINGS) invalid LENGTH_MAXIMUM value"), args[i]);
                return;
            }
            opt.has_len_max = true;
            opt.len_max = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "LIMIT_COUNT") && i + 1 < arena_arr_len(args)) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args[++i], &v)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(STRINGS) invalid LIMIT_COUNT value"), args[i]);
                return;
            }
            opt.has_limit_count = true;
            opt.limit_count = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "LIMIT_INPUT") && i + 1 < arena_arr_len(args)) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args[++i], &v)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(STRINGS) invalid LIMIT_INPUT value"), args[i]);
                return;
            }
            opt.has_limit_input = true;
            opt.limit_input = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "LIMIT_OUTPUT") && i + 1 < arena_arr_len(args)) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args[++i], &v)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(STRINGS) invalid LIMIT_OUTPUT value"), args[i]);
                return;
            }
            opt.has_limit_output = true;
            opt.limit_output = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "REGEX") && i + 1 < arena_arr_len(args)) {
            opt.has_regex = true;
            opt.regex = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "NEWLINE_CONSUME")) {
            opt.newline_consume = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "NO_HEX_CONVERSION")) {
            opt.no_hex_conversion = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "ENCODING")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(STRINGS) ENCODING requires a value"), t);
                return;
            }
            String_View encoding_sv = args[++i];
            File_Strings_Encoding enc = file_strings_parse_encoding_sv(encoding_sv);
            if (enc == FILE_STRINGS_ENCODING_INVALID) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(STRINGS) invalid ENCODING value"), encoding_sv);
                return;
            }
            opt.has_encoding = true;
            opt.encoding = enc;
            continue;
        }

        if (unsupported_count < 64) unsupported_items[unsupported_count++] = t;
    }

    if (opt.has_len_min && opt.has_len_max && opt.len_min > opt.len_max) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_CONFLICTING_OPTIONS, "eval_file", nob_sv_from_cstr("file(STRINGS) LENGTH_MINIMUM cannot be greater than LENGTH_MAXIMUM"), nob_sv_from_cstr(""));
        return;
    }

    if (unsupported_count > 0) {
        opt.unsupported = eval_sv_join_semi_temp(ctx, unsupported_items, unsupported_count);
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_UNSUPPORTED_OPERATION, "eval_file", nob_sv_from_cstr("file(STRINGS) has unsupported options"), opt.unsupported);
        if (eval_should_stop(ctx)) return;
    }

    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args[1], eval_current_source_dir(ctx), &path)) return;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, path_c);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(STRINGS) failed to read file"), path);
        return;
    }

    size_t input_n = sb.count;
    if (opt.has_limit_input && opt.limit_input < input_n) input_n = opt.limit_input;
    String_View decoded_input = nob_sv_from_parts(sb.items, input_n);
    File_Strings_Encoding requested_encoding = opt.has_encoding ? opt.encoding : FILE_STRINGS_ENCODING_AUTO;
    if (!file_strings_decode_to_utf8_temp(ctx, sb.items, input_n, requested_encoding, &decoded_input)) {
        nob_sb_free(sb);
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(STRINGS) failed to decode input with requested ENCODING"), path);
        return;
    }
    const char *input_data = decoded_input.data ? decoded_input.data : "";
    input_n = decoded_input.count;

    regex_t re = {0};
    bool re_compiled = false;
    if (opt.has_regex) {
        char *regex_c = eval_sv_to_cstr_temp(ctx, opt.regex);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, regex_c);
        if (regcomp(&re, regex_c, REG_EXTENDED) != 0) {
            nob_sb_free(sb);
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(STRINGS) invalid REGEX"), opt.regex);
            return;
        }
        re_compiled = true;
    }

    Nob_String_Builder out = {0};
    size_t emitted_count = 0;
    size_t out_bytes = 0;
    size_t line_start = 0;
    while (line_start < input_n) {
        size_t line_end = line_start;
        if (opt.newline_consume) {
            while (line_end < input_n && input_data[line_end] != '\0') line_end++;
        } else {
            while (line_end < input_n && input_data[line_end] != '\n') line_end++;
        }

        const char *line_ptr = input_data + line_start;
        size_t len = line_end - line_start;
        bool has_cr = false;
        for (size_t k = 0; k < len; k++) {
            if (line_ptr[k] == '\r') {
                has_cr = true;
                break;
            }
        }

        if (has_cr) {
            char *filtered = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
            if (!filtered) {
                if (re_compiled) regfree(&re);
                nob_sb_free(out);
                nob_sb_free(sb);
                EVAL_OOM_RETURN_VOID_IF_NULL(ctx, filtered);
            }
            size_t filtered_n = 0;
            for (size_t k = 0; k < len; k++) {
                if (line_ptr[k] != '\r') filtered[filtered_n++] = line_ptr[k];
            }
            filtered[filtered_n] = '\0';
            line_ptr = filtered;
            len = filtered_n;
        }

        bool keep = len > 0;
        if (keep && opt.has_len_min && len < opt.len_min) keep = false;
        if (keep && opt.has_len_max && len > opt.len_max) keep = false;

        if (keep && re_compiled) {
            char *line_c = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
            if (!line_c) {
                if (re_compiled) regfree(&re);
                nob_sb_free(out);
                nob_sb_free(sb);
                EVAL_OOM_RETURN_VOID_IF_NULL(ctx, line_c);
            }
            memcpy(line_c, line_ptr, len);
            line_c[len] = '\0';
            keep = regexec(&re, line_c, 0, NULL, 0) == 0;
        }

        if (keep) {
            size_t add = len + ((emitted_count > 0) ? 1 : 0);
            if (opt.has_limit_output && (out_bytes + add > opt.limit_output)) break;
            if (emitted_count > 0) nob_sb_append(&out, ';');
            if (len > 0) nob_sb_append_buf(&out, line_ptr, len);
            emitted_count++;
            out_bytes += add;
            if (opt.has_limit_count && emitted_count >= opt.limit_count) break;
        }

        if (line_end >= input_n) break;
        line_start = line_end + 1;
    }

    nob_sb_append_null(&out);
    String_View out_sv = out.items ? nob_sv_from_parts(out.items, out.count - 1) : nob_sv_from_cstr("");
    if (re_compiled) regfree(&re);
    nob_sb_free(out);
    (void)eval_var_set_current(ctx, out_var, out_sv);
    nob_sb_free(sb);
}
