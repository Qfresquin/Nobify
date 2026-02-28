#include "eval_file.h"
#include "eval_file_internal.h"
#include "evaluator_internal.h"
#include "eval_expr.h"
#include "eval_opt_parser.h"
#include "sv_utils.h"
#include "arena_dyn.h"
#include "tinydir.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include <pcre2posix.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <glob.h>
#include <unistd.h>
#endif

String_View eval_file_cmk_path_normalize_temp(Evaluator_Context *ctx, String_View input);

String_View eval_file_current_src_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    return v.count > 0 ? v : ctx->source_dir;
}

String_View eval_file_current_bin_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

static bool eval_var_truthy_or_default(Evaluator_Context *ctx, const char *key, bool default_value) {
    if (!ctx || !key) return default_value;
    String_View v = eval_var_get(ctx, nob_sv_from_cstr(key));
    if (v.count == 0) return default_value;
    return eval_truthy(ctx, v);
}

bool eval_file_parse_size_sv(String_View sv, size_t *out) {
    if (!out || sv.count == 0) return false;
    char tmp[64];
    if (sv.count >= sizeof(tmp)) return false;
    memcpy(tmp, sv.data, sv.count);
    tmp[sv.count] = '\0';
    char *end = NULL;
    unsigned long long v = strtoull(tmp, &end, 10);
    if (!end || *end != '\0') return false;
    *out = (size_t)v;
    return true;
}

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

static bool is_path_safe(String_View path) {
    if (eval_sv_eq_ci_lit(path, "..")) return false;
    for (size_t i = 0; i + 2 < path.count; i++) {
        if (path.data[i] == '.' && path.data[i + 1] == '.' && svu_is_path_sep(path.data[i + 2])) return false;
        if (svu_is_path_sep(path.data[i]) && path.data[i + 1] == '.' && path.data[i + 2] == '.') return false;
    }
    return true;
}

static bool scope_char_eq(char a, char b, bool ci) {
    if (a == '\\') a = '/';
    if (b == '\\') b = '/';
    if (ci) {
        a = (char)tolower((unsigned char)a);
        b = (char)tolower((unsigned char)b);
    }
    return a == b;
}

static bool scope_is_root_like(String_View p) {
    if (p.count == 1 && p.data[0] == '/') return true;
    if (p.count == 3 &&
        isalpha((unsigned char)p.data[0]) &&
        p.data[1] == ':' &&
        p.data[2] == '/') return true;
    return false;
}

static String_View scope_trim_trailing_seps(String_View p) {
    while (p.count > 0 && svu_is_path_sep(p.data[p.count - 1]) && !scope_is_root_like(p)) {
        p.count--;
    }
    return p;
}

static bool scope_path_has_prefix(String_View path, String_View prefix, bool ci) {
    path = scope_trim_trailing_seps(path);
    prefix = scope_trim_trailing_seps(prefix);
    if (prefix.count == 0) return false;
    if (path.count < prefix.count) return false;

    for (size_t i = 0; i < prefix.count; i++) {
        if (!scope_char_eq(path.data[i], prefix.data[i], ci)) return false;
    }

    if (path.count == prefix.count) return true;
    if (prefix.data[prefix.count - 1] == '/' || prefix.data[prefix.count - 1] == '\\') return true;
    return svu_is_path_sep(path.data[prefix.count]);
}

static void scope_normalize_slashes_in_place(char *s) {
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\\') s[i] = '/';
    }
}

static bool scope_canonicalize_existing_cstr_temp(Evaluator_Context *ctx,
                                                  const char *path_c,
                                                  char **out_canon_cstr) {
    if (!ctx || !path_c || !out_canon_cstr) return false;
    *out_canon_cstr = NULL;

#if defined(_WIN32)
    HANDLE h = CreateFileA(path_c,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD need = GetFinalPathNameByHandleA(h, NULL, 0, FILE_NAME_NORMALIZED);
    if (need == 0) {
        CloseHandle(h);
        return false;
    }

    DWORD cap = need + 1;
    char *raw = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)cap);
    EVAL_OOM_RETURN_IF_NULL(ctx, raw, false);

    DWORD wrote = GetFinalPathNameByHandleA(h, raw, cap, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (wrote == 0 || wrote >= cap) return false;

    const char *view = raw;
    char *unc_fixed = NULL;
    if (strncmp(view, "\\\\?\\UNC\\", 8) == 0) {
        size_t rest = strlen(view + 8);
        unc_fixed = (char*)arena_alloc(eval_temp_arena(ctx), rest + 3);
        EVAL_OOM_RETURN_IF_NULL(ctx, unc_fixed, false);
        unc_fixed[0] = '/';
        unc_fixed[1] = '/';
        memcpy(unc_fixed + 2, view + 8, rest + 1);
        view = unc_fixed;
    } else if (strncmp(view, "\\\\?\\", 4) == 0) {
        view += 4;
    }

    size_t len = strlen(view);
    char *norm = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, norm, false);
    memcpy(norm, view, len + 1);
    scope_normalize_slashes_in_place(norm);
    *out_canon_cstr = norm;
    return true;
#else
    char *real = realpath(path_c, NULL);
    if (!real) return false;
    size_t len = strlen(real);
    char *norm = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, norm, false);
    memcpy(norm, real, len + 1);
    free(real);
    *out_canon_cstr = norm;
    return true;
#endif
}

static bool scope_canonicalize_existing_or_parent_temp(Evaluator_Context *ctx,
                                                       String_View path,
                                                       String_View *out) {
    if (!ctx || !out || path.count == 0) return false;

    char *probe = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, probe, false);
    scope_normalize_slashes_in_place(probe);

    for (;;) {
        char *canon = NULL;
        if (scope_canonicalize_existing_cstr_temp(ctx, probe, &canon)) {
            *out = nob_sv_from_cstr(canon);
            return true;
        }

        size_t len = strlen(probe);
        while (len > 0 && probe[len - 1] == '/') {
            if (len == 1) break;
            if (len == 3 && isalpha((unsigned char)probe[0]) && probe[1] == ':') break;
            probe[--len] = '\0';
        }

        char *last = strrchr(probe, '/');
        if (!last) return false;

        if (last == probe) {
            probe[1] = '\0';
            continue;
        }
        if (last == probe + 2 && isalpha((unsigned char)probe[0]) && probe[1] == ':') {
            probe[3] = '\0';
            continue;
        }
        *last = '\0';
    }

    return false;
}

bool eval_file_resolve_project_scoped_path(Evaluator_Context *ctx,
                                        const Node *node,
                                        Cmake_Event_Origin origin,
                                        String_View input_path,
                                        String_View relative_base,
                                        String_View *out_path) {
    if (!ctx || !node || !out_path) return false;

    if (!is_path_safe(input_path)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       origin,
                       nob_sv_from_cstr("Security Violation: Path traversal (..) is not allowed"),
                       input_path);
        return false;
    }

    String_View path = input_path;
    if (!eval_sv_is_abs_path(path)) {
        path = eval_sv_path_join(eval_temp_arena(ctx), relative_base, path);
    }
    path = eval_file_cmk_path_normalize_temp(ctx, path);
    if (eval_should_stop(ctx) || path.count == 0) return false;

    bool ci = false;
#if defined(_WIN32)
    ci = true;
#endif

    if (!scope_path_has_prefix(path, ctx->binary_dir, ci) &&
        !scope_path_has_prefix(path, ctx->source_dir, ci)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       origin,
                       nob_sv_from_cstr("Security Violation: Absolute path outside project scope"),
                       path);
        return false;
    }

    String_View resolved_probe = nob_sv_from_cstr("");
    if (scope_canonicalize_existing_or_parent_temp(ctx, path, &resolved_probe)) {
        String_View source_scope = ctx->source_dir;
        String_View binary_scope = ctx->binary_dir;

        String_View source_scope_canon = nob_sv_from_cstr("");
        if (scope_canonicalize_existing_or_parent_temp(ctx, ctx->source_dir, &source_scope_canon)) {
            source_scope = source_scope_canon;
        }

        String_View binary_scope_canon = nob_sv_from_cstr("");
        if (scope_canonicalize_existing_or_parent_temp(ctx, ctx->binary_dir, &binary_scope_canon)) {
            binary_scope = binary_scope_canon;
        }

        if (!scope_path_has_prefix(resolved_probe, binary_scope, ci) &&
            !scope_path_has_prefix(resolved_probe, source_scope, ci)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("eval_file"),
                           node->as.cmd.name,
                           origin,
                           nob_sv_from_cstr("Security Violation: Absolute path outside project scope"),
                           input_path);
            return false;
        }
    }

    *out_path = path;
    return true;
}

String_View eval_file_cmk_path_normalize_temp(Evaluator_Context *ctx, String_View input) {
    if (!ctx) return nob_sv_from_cstr("");
    if (input.count == 0) return nob_sv_from_cstr("");

    bool is_unc = input.count >= 2 && svu_is_path_sep(input.data[0]) && svu_is_path_sep(input.data[1]);
    bool has_drive = input.count >= 2 &&
                     isalpha((unsigned char)input.data[0]) &&
                     input.data[1] == ':';
    bool absolute = false;
    size_t pos = 0;

    if (is_unc) {
        pos = 2;
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

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 3);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;

    if (is_unc) {
        buf[off++] = '/';
        buf[off++] = '/';
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
    } else if (has_drive) {
        buf[off++] = input.data[0];
        buf[off++] = ':';
        if (absolute) buf[off++] = '/';
    } else if (absolute) {
        buf[off++] = '/';
    }

    bool prev_sep = (off > 0 && buf[off - 1] == '/');
    for (; pos < input.count; pos++) {
        char c = input.data[pos];
        if (svu_is_path_sep(c)) {
            if (!prev_sep) {
                buf[off++] = '/';
                prev_sep = true;
            }
            continue;
        }
        buf[off++] = c;
        prev_sep = false;
    }

    size_t min_len = 0;
    if (is_unc) min_len = 2;
    else if (has_drive && absolute) min_len = 3;
    else if (has_drive) min_len = 2;
    else if (absolute) min_len = 1;

    while (off > min_len && buf[off - 1] == '/') off--;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static size_t mkdir_root_prefix_len(const char *path) {
    if (!path) return 0;
    size_t len = strlen(path);
    if (len == 0) return 0;

    if (len >= 3 && isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == '/') {
        return 3;
    }
    if (len >= 2 && isalpha((unsigned char)path[0]) && path[1] == ':') {
        return 2;
    }
    if (len >= 2 && path[0] == '/' && path[1] == '/') {
        size_t i = 2;
        while (i < len && path[i] == '/') i++;
        while (i < len && path[i] != '/') i++;
        while (i < len && path[i] == '/') i++;
        while (i < len && path[i] != '/') i++;
        return i;
    }
    if (path[0] == '/') return 1;
    return 0;
}

bool eval_file_mkdir_p(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return false;

    String_View normalized = eval_file_cmk_path_normalize_temp(ctx, path);
    if (eval_should_stop(ctx) || normalized.count == 0) return false;

    char *path_c = eval_sv_to_cstr_temp(ctx, normalized);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    size_t len0 = strlen(path_c);
    char *tmp = (char*)arena_alloc(eval_temp_arena(ctx), len0 + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, tmp, false);
    memcpy(tmp, path_c, len0 + 1);

    for (size_t i = 0; i < len0; i++) {
        if (tmp[i] == '\\') tmp[i] = '/';
    }

    size_t len = strlen(tmp);
    size_t prefix_len = mkdir_root_prefix_len(tmp);
    while (len > prefix_len && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }

    size_t start = prefix_len;
    while (start < len && tmp[start] == '/') start++;
    if (start >= len) {
        return true;
    }

    for (size_t i = start; i < len; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        if (!nob_mkdir_if_not_exists(tmp)) {
            return false;
        }
        tmp[i] = '/';
    }

    return nob_mkdir_if_not_exists(tmp);
}

static bool glob_match_sv(String_View pat, String_View str, bool ci) {
    size_t pi = 0, si = 0;
    size_t star_pi = (size_t)-1, star_si = (size_t)-1;

    while (si < str.count) {
        if (pi < pat.count) {
            char pc = pat.data[pi];
            char sc = str.data[si];

            if (pc == '*') {
                star_pi = pi++;
                star_si = si;
                continue;
            }

            if (pc == '?') {
                if (!svu_is_path_sep(sc)) {
                    pi++;
                    si++;
                    continue;
                }
            }

            if (pc == '[') {
                if (!svu_is_path_sep(sc)) {
                    size_t j = pi + 1;
                    while (j < pat.count && pat.data[j] != ']') j++;

                    // If class is not closed, treat '[' as a regular char.
                    if (j < pat.count) {
                        bool neg = false;
                        size_t k = pi + 1;
                        if (k < j && (pat.data[k] == '!' || pat.data[k] == '^')) {
                            neg = true;
                            k++;
                        }

                        bool matched = false;
                        char sc_cmp = ci ? (char)tolower((unsigned char)sc) : sc;

                        while (k < j) {
                            char a = pat.data[k];
                            char a_cmp = ci ? (char)tolower((unsigned char)a) : a;

                            if (k + 2 < j && pat.data[k + 1] == '-') {
                                char b = pat.data[k + 2];
                                char b_cmp = ci ? (char)tolower((unsigned char)b) : b;
                                char lo = a_cmp < b_cmp ? a_cmp : b_cmp;
                                char hi = a_cmp < b_cmp ? b_cmp : a_cmp;
                                if (sc_cmp >= lo && sc_cmp <= hi) matched = true;
                                k += 3;
                            } else {
                                if (sc_cmp == a_cmp) matched = true;
                                k++;
                            }
                        }

                        if (neg) matched = !matched;
                        if (matched) {
                            pi = j + 1;
                            si++;
                            continue;
                        }
                    }
                }
            }

            char a = ci ? (char)tolower((unsigned char)pc) : pc;
            char b = ci ? (char)tolower((unsigned char)sc) : sc;
            if (a == b) {
                pi++;
                si++;
                continue;
            }
        }

        if (star_pi != (size_t)-1) {
            if (star_si < str.count && svu_is_path_sep(str.data[star_si])) {
                star_pi = (size_t)-1;
            } else {
                pi = star_pi + 1;
                si = ++star_si;
                continue;
            }
        }

        return false;
    }

    while (pi < pat.count && pat.data[pi] == '*') pi++;
    return pi == pat.count;
}

static int sv_lex_cmp_qsort(const void *a, const void *b) {
    const String_View *aa = (const String_View*)a;
    const String_View *bb = (const String_View*)b;

    size_t n = aa->count < bb->count ? aa->count : bb->count;
    int c = memcmp(aa->data, bb->data, n);
    if (c != 0) return c;
    return (aa->count < bb->count) ? -1 : (aa->count > bb->count ? 1 : 0);
}

#if !defined(_WIN32)
static bool posix_glob_collect(Evaluator_Context *ctx,
                               String_View pat,
                               bool list_dirs,
                               String_View **io_items,
                               size_t *io_count,
                               size_t *io_cap) {
    char *pat_c = eval_sv_to_cstr_temp(ctx, pat);
    EVAL_OOM_RETURN_IF_NULL(ctx, pat_c, false);

    glob_t g = {0};
    int rc = glob(pat_c, 0, NULL, &g);
    if (rc == GLOB_NOMATCH) {
        globfree(&g);
        return true;
    }
    if (rc != 0) {
        globfree(&g);
        return false;
    }

    for (size_t i = 0; i < g.gl_pathc; i++) {
        const char *entry = g.gl_pathv[i];
        if (!entry) continue;
        Nob_File_Type t = nob_get_file_type(entry);
        if (!list_dirs && t == NOB_FILE_DIRECTORY) continue;

        String_View sv = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(entry));
        if (ctx->oom) {
            globfree(&g);
            return false;
        }
        if (!arena_da_reserve(eval_temp_arena(ctx), (void**)io_items, io_cap, sizeof(String_View), *io_count + 1)) {
            globfree(&g);
            ctx_oom(ctx);
            return false;
        }
        (*io_items)[(*io_count)++] = sv;
    }

    globfree(&g);
    return true;
}
#endif

static String_View glob_base_dir(String_View pattern_abs) {
    size_t first_meta = pattern_abs.count;
    for (size_t i = 0; i < pattern_abs.count; i++) {
        char c = pattern_abs.data[i];
        if (c == '*' || c == '?' || c == '[') {
            first_meta = i;
            break;
        }
    }
    if (first_meta == pattern_abs.count) return svu_dirname(pattern_abs);
    if (first_meta == 0) return nob_sv_from_cstr(".");
    return svu_dirname(nob_sv_from_parts(pattern_abs.data, first_meta));
}

static bool sv_path_prefix_eq(String_View path, String_View prefix, bool ci) {
    if (prefix.count > path.count) return false;
    for (size_t i = 0; i < prefix.count; i++) {
        char a = path.data[i];
        char b = prefix.data[i];
        if (svu_is_path_sep(a) && svu_is_path_sep(b)) continue;
        if (ci) {
            a = (char)tolower((unsigned char)a);
            b = (char)tolower((unsigned char)b);
        }
        if (a != b) return false;
    }
    return true;
}

static String_View sv_make_relative(String_View path, String_View base, bool ci) {
    if (base.count == 0) return path;
    if (!sv_path_prefix_eq(path, base, ci)) return path;
    size_t off = base.count;
    if (off < path.count && svu_is_path_sep(path.data[off])) off++;
    return nob_sv_from_parts(path.data + off, path.count - off);
}

static void file_glob_walk(Evaluator_Context *ctx,
                           const Node *node,
                           Cmake_Event_Origin origin,
                           String_View dir_full,
                           String_View pat,
                           bool recurse,
                           bool list_dirs,
                           bool ci,
                           bool strict_failures,
                           size_t *io_open_failures,
                           String_View **io_items,
                           size_t *io_count,
                           size_t *io_cap) {
    if (ctx->oom) return;

    if (dir_full.count == 0) return;
    char *dir_c = (char*)arena_alloc(eval_temp_arena(ctx), dir_full.count + 1);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, dir_c);
    memcpy(dir_c, dir_full.data, dir_full.count);
    dir_c[dir_full.count] = 0;

    tinydir_dir d;
    if (tinydir_open(&d, dir_c) != 0) {
        const char *err = strerror(errno);
        String_View cause = nob_sv_from_cstr("file(GLOB) failed to open directory");
        if (err && err[0] != '\0') {
            cause = nob_sv_from_cstr(nob_temp_sprintf("file(GLOB) failed to open directory: %s", err));
        }
        if (io_open_failures) (*io_open_failures)++;
        eval_emit_diag(ctx,
                       strict_failures ? EV_DIAG_ERROR : EV_DIAG_WARNING,
                       nob_sv_from_cstr("eval_file"),
                       node ? node->as.cmd.name : nob_sv_from_cstr("file"),
                       origin,
                       cause,
                       dir_full);
        return;
    }

    while (d.has_next) {
        tinydir_file f;
        if (tinydir_readfile(&d, &f) != 0) break;
        tinydir_next(&d);

        if (strcmp(f.name, ".") == 0 || strcmp(f.name, "..") == 0) continue;

        bool is_dir = f.is_dir != 0;
        String_View name = nob_sv_from_cstr(f.name);
        String_View full = eval_sv_path_join(eval_temp_arena(ctx), dir_full, name);

        if (glob_match_sv(pat, full, ci)) {
            if (list_dirs || !is_dir) {
                if (arena_da_reserve(eval_temp_arena(ctx),
                                     (void**)io_items,
                                     io_cap,
                                     sizeof(String_View),
                                     *io_count + 1)) {
                    (*io_items)[(*io_count)++] = full;
                } else {
                    ctx_oom(ctx);
                    break;
                }
            }
        }

        if (recurse && is_dir) {
            file_glob_walk(ctx, node, origin, full, pat, recurse, list_dirs, ci, strict_failures, io_open_failures, io_items, io_count, io_cap);
            if (ctx->oom) break;
        }
    }

    tinydir_close(&d);
}

static void handle_file_glob(Evaluator_Context *ctx, const Node *node, SV_List args, bool recurse) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("file(GLOB) requires <var> and patterns"),
                       nob_sv_from_cstr(""));
        return;
    }

    String_View out_var = args.items[1];
    bool list_dirs = true;
    bool has_relative = false;
    String_View relative_base = nob_sv_from_cstr("");
    size_t pat_idx = 2;

    while (pat_idx < args.count &&
           (eval_sv_eq_ci_lit(args.items[pat_idx], "CONFIGURE_DEPENDS") ||
            eval_sv_eq_ci_lit(args.items[pat_idx], "LIST_DIRECTORIES") ||
            eval_sv_eq_ci_lit(args.items[pat_idx], "RELATIVE"))) {
        if (eval_sv_eq_ci_lit(args.items[pat_idx], "LIST_DIRECTORIES")) {
            if (pat_idx + 1 < args.count) list_dirs = eval_truthy(ctx, args.items[++pat_idx]);
        } else if (eval_sv_eq_ci_lit(args.items[pat_idx], "RELATIVE")) {
            if (pat_idx + 1 < args.count) {
                relative_base = args.items[++pat_idx];
                has_relative = true;
            }
        }
        pat_idx++;
    }

    String_View *matches = NULL;
    size_t mcount = 0, mcap = 0;

    bool ci = false;
#if defined(_WIN32) || defined(__APPLE__)
    ci = true;
#endif
    bool glob_strict = eval_var_truthy_or_default(ctx, "CMAKE_NOBIFY_FILE_GLOB_STRICT", false);
    size_t open_failures = 0;

    String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;
    if (has_relative && !eval_sv_is_abs_path(relative_base)) {
        relative_base = eval_sv_path_join(eval_temp_arena(ctx), current_src, relative_base);
    }

    for (size_t i = pat_idx; i < args.count; ++i) {
        String_View pat = args.items[i];
        if (!eval_sv_is_abs_path(pat)) {
            pat = eval_sv_path_join(eval_temp_arena(ctx), current_src, pat);
        }

#if !defined(_WIN32)
        if (!recurse) {
            if (posix_glob_collect(ctx, pat, list_dirs, &matches, &mcount, &mcap)) {
                if (ctx->oom) return;
                continue;
            }
        }
#endif

        String_View base_dir = glob_base_dir(pat);
        file_glob_walk(ctx, node, o, base_dir, pat, recurse, list_dirs, ci, glob_strict, &open_failures, &matches, &mcount, &mcap);
        if (ctx->oom) return;
        if (eval_should_stop(ctx)) return;
    }

    if (open_failures > 0) {
        eval_emit_diag(ctx,
                       glob_strict ? EV_DIAG_ERROR : EV_DIAG_WARNING,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr(nob_temp_sprintf("file(GLOB) completed with %zu directory open failure(s); matches may be incomplete", open_failures)),
                       glob_strict
                           ? nob_sv_from_cstr("Unset CMAKE_NOBIFY_FILE_GLOB_STRICT to treat as warning")
                           : nob_sv_from_cstr("Set CMAKE_NOBIFY_FILE_GLOB_STRICT=ON to treat as error"));
        if (eval_should_stop(ctx)) return;
    }

    if (mcount > 1) {
        qsort(matches, mcount, sizeof(String_View), sv_lex_cmp_qsort);
    }

    String_View joined = nob_sv_from_cstr("");
    if (mcount > 0) {
        size_t total = 0;
        for (size_t i = 0; i < mcount; i++) total += matches[i].count;
        total += (mcount - 1);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, buf);

        size_t off = 0;
        for (size_t i = 0; i < mcount; i++) {
            String_View out_item = matches[i];
            if (has_relative) {
                out_item = sv_make_relative(out_item, relative_base, ci);
            }
            if (i) buf[off++] = ';';
            memcpy(buf + off, out_item.data, out_item.count);
            off += out_item.count;
        }
        buf[off] = '\0';
        joined = nob_sv_from_cstr(buf);
    }

    (void)eval_var_set(ctx, out_var, joined);
}

static void handle_file_write(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("file(WRITE) requires <path> and <content>"),
                       nob_sv_from_cstr(""));
        return;
    }

    String_View path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], ctx->binary_dir, &path)) return;

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
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("eval_file"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("file(WRITE) failed to create parent directory"),
                           dir);
            return;
        }
    }

    FILE *f = fopen(path_c, "wb");
    if (!f) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("file(WRITE) failed to open/create file"),
                       path);
        return;
    }

    for (size_t i = 2; i < args.count; i++) {
        fwrite(args.items[i].data, 1, args.items[i].count, f);
    }
    fclose(f);
}

static void handle_file_make_directory(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("file(MAKE_DIRECTORY) requires at least one path"),
                       nob_sv_from_cstr("Usage: file(MAKE_DIRECTORY <dir>...)"));
        return;
    }

    for (size_t i = 1; i < args.count; i++) {
        String_View path = nob_sv_from_cstr("");
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[i], eval_file_current_bin_dir(ctx), &path)) return;

        if (!eval_file_mkdir_p(ctx, path)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("eval_file"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("file(MAKE_DIRECTORY) failed to create directory"),
                           path);
            return;
        }
    }
}

static void handle_file_read(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(READ) requires <path> and <out-var>"),
                       nob_sv_from_cstr("Usage: file(READ <path> <out-var> [OFFSET n] [LIMIT n] [HEX])"));
        return;
    }

    String_View path = nob_sv_from_cstr("");
    String_View out_var = args.items[2];
    size_t offset = 0;
    bool has_limit = false;
    size_t limit = 0;
    bool hex = false;

    for (size_t i = 3; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "OFFSET") && i + 1 < args.count) {
            (void)eval_file_parse_size_sv(args.items[++i], &offset);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "LIMIT") && i + 1 < args.count) {
            has_limit = eval_file_parse_size_sv(args.items[++i], &limit);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "HEX")) {
            hex = true;
        }
    }

    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_src_dir(ctx), &path)) return;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, path_c);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
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
        (void)eval_var_set(ctx, out_var, content);
        nob_sb_free(sb);
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
    (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(hex_buf));
    nob_sb_free(sb);
}

static void handle_file_strings(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
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
    String_View out_var = args.items[2];

    File_Strings_Options opt = {0};
    String_View unsupported_items[64] = {0};
    size_t unsupported_count = 0;

    for (size_t i = 3; i < args.count; i++) {
        String_View t = args.items[i];

        if (eval_sv_eq_ci_lit(t, "LENGTH_MINIMUM") && i + 1 < args.count) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args.items[++i], &v)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(STRINGS) invalid LENGTH_MINIMUM value"),
                               args.items[i]);
                return;
            }
            opt.has_len_min = true;
            opt.len_min = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "LENGTH_MAXIMUM") && i + 1 < args.count) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args.items[++i], &v)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(STRINGS) invalid LENGTH_MAXIMUM value"),
                               args.items[i]);
                return;
            }
            opt.has_len_max = true;
            opt.len_max = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "LIMIT_COUNT") && i + 1 < args.count) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args.items[++i], &v)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(STRINGS) invalid LIMIT_COUNT value"),
                               args.items[i]);
                return;
            }
            opt.has_limit_count = true;
            opt.limit_count = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "LIMIT_INPUT") && i + 1 < args.count) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args.items[++i], &v)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(STRINGS) invalid LIMIT_INPUT value"),
                               args.items[i]);
                return;
            }
            opt.has_limit_input = true;
            opt.limit_input = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "LIMIT_OUTPUT") && i + 1 < args.count) {
            size_t v = 0;
            if (!eval_file_parse_size_sv(args.items[++i], &v)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(STRINGS) invalid LIMIT_OUTPUT value"),
                               args.items[i]);
                return;
            }
            opt.has_limit_output = true;
            opt.limit_output = v;
            continue;
        }
        if (eval_sv_eq_ci_lit(t, "REGEX") && i + 1 < args.count) {
            opt.has_regex = true;
            opt.regex = args.items[++i];
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
            if (i + 1 >= args.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(STRINGS) ENCODING requires a value"),
                               t);
                return;
            }
            String_View encoding_sv = args.items[++i];
            File_Strings_Encoding enc = file_strings_parse_encoding_sv(encoding_sv);
            if (enc == FILE_STRINGS_ENCODING_INVALID) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(STRINGS) invalid ENCODING value"),
                               encoding_sv);
                return;
            }
            opt.has_encoding = true;
            opt.encoding = enc;
            continue;
        }

        if (unsupported_count < 64) unsupported_items[unsupported_count++] = t;
    }

    if (opt.has_len_min && opt.has_len_max && opt.len_min > opt.len_max) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(STRINGS) LENGTH_MINIMUM cannot be greater than LENGTH_MAXIMUM"),
                       nob_sv_from_cstr(""));
        return;
    }

    if (unsupported_count > 0) {
        opt.unsupported = eval_sv_join_semi_temp(ctx, unsupported_items, unsupported_count);
        eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(STRINGS) has unsupported options"),
                       opt.unsupported);
        if (eval_should_stop(ctx)) return;
    }

    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_src_dir(ctx), &path)) return;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, path_c);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(STRINGS) failed to read file"),
                       path);
        return;
    }

    size_t input_n = sb.count;
    if (opt.has_limit_input && opt.limit_input < input_n) input_n = opt.limit_input;
    String_View decoded_input = nob_sv_from_parts(sb.items, input_n);
    File_Strings_Encoding requested_encoding = opt.has_encoding ? opt.encoding : FILE_STRINGS_ENCODING_AUTO;
    if (!file_strings_decode_to_utf8_temp(ctx, sb.items, input_n, requested_encoding, &decoded_input)) {
        nob_sb_free(sb);
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(STRINGS) failed to decode input with requested ENCODING"),
                       path);
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
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(STRINGS) invalid REGEX"),
                           opt.regex);
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
    (void)eval_var_set(ctx, out_var, out_sv);
    nob_sb_free(sb);
}

typedef struct {
    bool is_regex;
    String_View expr;
    bool exclude;
    bool regex_ready;
    regex_t regex;
} Copy_Filter;

typedef struct {
    bool has_permissions;
    bool has_file_permissions;
    bool has_directory_permissions;
    mode_t permissions_mode;
    mode_t file_permissions_mode;
    mode_t directory_permissions_mode;
    bool saw_use_source_permissions;
    bool saw_no_source_permissions;
} Copy_Permissions;

enum {
    COPY_KEY_DESTINATION = 0,
    COPY_KEY_FILES_MATCHING,
    COPY_KEY_PATTERN,
    COPY_KEY_REGEX,
    COPY_KEY_EXCLUDE,
    COPY_KEY_FOLLOW_SYMLINK_CHAIN,
    COPY_KEY_PERMISSIONS,
    COPY_KEY_FILE_PERMISSIONS,
    COPY_KEY_DIRECTORY_PERMISSIONS,
    COPY_KEY_USE_SOURCE_PERMISSIONS,
    COPY_KEY_NO_SOURCE_PERMISSIONS,
    COPY_KEY_UNKNOWN,
};

static bool copy_permission_add_token(mode_t *mode, String_View token) {
    if (!mode) return false;
    if (eval_sv_eq_ci_lit(token, "OWNER_READ")) {
#ifdef S_IRUSR
        *mode |= S_IRUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "OWNER_WRITE")) {
#ifdef S_IWUSR
        *mode |= S_IWUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "OWNER_EXECUTE")) {
#ifdef S_IXUSR
        *mode |= S_IXUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_READ")) {
#ifdef S_IRGRP
        *mode |= S_IRGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_WRITE")) {
#ifdef S_IWGRP
        *mode |= S_IWGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_EXECUTE")) {
#ifdef S_IXGRP
        *mode |= S_IXGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_READ")) {
#ifdef S_IROTH
        *mode |= S_IROTH;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_WRITE")) {
#ifdef S_IWOTH
        *mode |= S_IWOTH;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_EXECUTE")) {
#ifdef S_IXOTH
        *mode |= S_IXOTH;
#endif
        return true;
    }
    return false;
}

static bool copy_permissions_pick_mode(const Copy_Permissions *perms, bool is_dir, mode_t *out_mode) {
    if (!perms || !out_mode) return false;
    if (is_dir && perms->has_directory_permissions) {
        *out_mode = perms->directory_permissions_mode;
        return true;
    }
    if (!is_dir && perms->has_file_permissions) {
        *out_mode = perms->file_permissions_mode;
        return true;
    }
    if (perms->has_permissions) {
        *out_mode = perms->permissions_mode;
        return true;
    }
    return false;
}

static bool copy_apply_permissions(const char *path, mode_t mode) {
    if (!path) return false;
#if defined(_WIN32)
    (void)mode;
    return true;
#else
    return chmod(path, mode) == 0;
#endif
}

static String_View copy_basename_sv(String_View path) {
    size_t base = 0;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') base = i + 1;
    }
    return nob_sv_from_parts(path.data + base, path.count - base);
}

static bool copy_copy_entry_raw(const char *src_c, const char *dst_c, bool *out_src_is_dir) {
    if (!src_c || !dst_c) return false;
    bool src_is_dir = false;
    bool ok = true;
    tinydir_file tf = {0};
    if (tinydir_file_open(&tf, src_c) == 0) {
        src_is_dir = tf.is_dir != 0;
        ok = tf.is_dir ? nob_copy_directory_recursively(src_c, dst_c)
                       : nob_copy_file(src_c, dst_c);
    } else {
        ok = nob_copy_file(src_c, dst_c);
    }
    if (out_src_is_dir) *out_src_is_dir = src_is_dir;
    return ok;
}

static bool copy_path_is_symlink(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#elif defined(S_ISLNK)
    struct stat st = {0};
    if (lstat(path, &st) != 0) return false;
    return S_ISLNK(st.st_mode);
#else
    return false;
#endif
}

static bool copy_read_symlink_target_temp(Evaluator_Context *ctx, const char *path, String_View *out_target) {
    if (!ctx || !path || !out_target) return false;
#if defined(_WIN32)
    HANDLE h = CreateFileA(path,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD need = GetFinalPathNameByHandleA(h, NULL, 0, FILE_NAME_NORMALIZED);
    if (need == 0) {
        CloseHandle(h);
        return false;
    }

    DWORD cap = need + 1;
    char *raw = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)cap);
    EVAL_OOM_RETURN_IF_NULL(ctx, raw, false);

    DWORD wrote = GetFinalPathNameByHandleA(h, raw, cap, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (wrote == 0 || wrote >= cap) return false;

    const char *view = raw;
    char *unc_fixed = NULL;
    if (strncmp(view, "\\\\?\\UNC\\", 8) == 0) {
        size_t rest = strlen(view + 8);
        unc_fixed = (char*)arena_alloc(eval_temp_arena(ctx), rest + 3);
        EVAL_OOM_RETURN_IF_NULL(ctx, unc_fixed, false);
        unc_fixed[0] = '/';
        unc_fixed[1] = '/';
        memcpy(unc_fixed + 2, view + 8, rest + 1);
        view = unc_fixed;
    } else if (strncmp(view, "\\\\?\\", 4) == 0) {
        view += 4;
    }

    size_t len = strlen(view);
    char *norm = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, norm, false);
    memcpy(norm, view, len + 1);
    for (size_t i = 0; i < len; i++) {
        if (norm[i] == '\\') norm[i] = '/';
    }
    *out_target = nob_sv_from_cstr(norm);
    return true;
#else
    char buf[4096];
    ssize_t n = readlink(path, buf, sizeof(buf) - 1);
    if (n < 0) return false;
    buf[n] = '\0';
    *out_target = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
    return !eval_should_stop(ctx);
#endif
}

#if !defined(_WIN32)
static bool copy_remove_existing_leaf(const char *path) {
    if (!path || path[0] == '\0') return false;
    struct stat st = {0};
    if (lstat(path, &st) != 0) return errno == ENOENT;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) return false;
    return remove(path) == 0;
}

static bool copy_create_symlink_like(const char *target, const char *link_path) {
    if (!target || !link_path) return false;
    if (!copy_remove_existing_leaf(link_path)) return false;
    return symlink(target, link_path) == 0;
}
#endif

static bool copy_follow_symlink_chain(Evaluator_Context *ctx,
                                      const Node *node,
                                      Cmake_Event_Origin o,
                                      String_View src,
                                      String_View dest_dir,
                                      const Copy_Permissions *perms,
                                      bool *io_applied_any_permissions) {
    if (!ctx || !node) return false;
    String_View current = src;

    for (size_t depth = 0; depth < 64; depth++) {
        char *current_c = eval_sv_to_cstr_temp(ctx, current);
        EVAL_OOM_RETURN_IF_NULL(ctx, current_c, false);

        if (!copy_path_is_symlink(current_c)) {
            String_View base = copy_basename_sv(current);
            String_View dst = eval_sv_path_join(eval_temp_arena(ctx), dest_dir, base);
            char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
            EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);

            bool src_is_dir = false;
            if (!copy_copy_entry_raw(current_c, dst_c, &src_is_dir)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(COPY) failed to copy entry"),
                               current);
                return false;
            }

#if defined(_WIN32)
            // Windows fallback: materialize symlink aliases as copied entries.
            for (size_t i = 0; i < link_names.count; i++) {
                if (eval_sv_key_eq(link_names.items[i], base)) continue;
                String_View alias_dst = eval_sv_path_join(eval_temp_arena(ctx), dest_dir, link_names.items[i]);
                char *alias_dst_c = eval_sv_to_cstr_temp(ctx, alias_dst);
                EVAL_OOM_RETURN_IF_NULL(ctx, alias_dst_c, false);

                bool alias_ok = src_is_dir ? nob_copy_directory_recursively(dst_c, alias_dst_c)
                                           : nob_copy_file(dst_c, alias_dst_c);
                if (!alias_ok) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("file(COPY) failed to materialize FOLLOW_SYMLINK_CHAIN alias on Windows"),
                                   alias_dst);
                    return false;
                }
            }
#endif

            mode_t mode = 0;
            if (perms && copy_permissions_pick_mode(perms, src_is_dir, &mode)) {
                if (!copy_apply_permissions(dst_c, mode)) {
                    eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("file(COPY) copied entry but failed to apply permissions"),
                                   dst);
                } else if (io_applied_any_permissions) {
                    *io_applied_any_permissions = true;
                }
            }

            return true;
        }

        String_View target = nob_sv_from_cstr("");
        if (!copy_read_symlink_target_temp(ctx, current_c, &target) || target.count == 0) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(COPY) failed to resolve symlink target in FOLLOW_SYMLINK_CHAIN"),
                           current);
            return false;
        }

        String_View link_name = copy_basename_sv(current);
        String_View dst_link = eval_sv_path_join(eval_temp_arena(ctx), dest_dir, link_name);
        char *dst_link_c = eval_sv_to_cstr_temp(ctx, dst_link);
        EVAL_OOM_RETURN_IF_NULL(ctx, dst_link_c, false);

#if !defined(_WIN32)
        char *target_c = eval_sv_to_cstr_temp(ctx, target);
        EVAL_OOM_RETURN_IF_NULL(ctx, target_c, false);
        if (!copy_create_symlink_like(target_c, dst_link_c)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(COPY) failed to recreate symlink in FOLLOW_SYMLINK_CHAIN"),
                           dst_link);
            return false;
        }
#else
        if (!svu_list_push_temp(ctx, &link_names, link_name)) return false;
#endif

        if (eval_sv_is_abs_path(target)) {
            current = eval_file_cmk_path_normalize_temp(ctx, target);
        } else {
            String_View parent = svu_dirname(current);
            current = eval_file_cmk_path_normalize_temp(ctx, eval_sv_path_join(eval_temp_arena(ctx), parent, target));
        }
        if (eval_should_stop(ctx) || current.count == 0) return false;
    }

    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                   nob_sv_from_cstr("file(COPY) FOLLOW_SYMLINK_CHAIN exceeded maximum link depth"),
                   src);
    return false;
}

static bool copy_filter_matches(Evaluator_Context *ctx, Copy_Filter *f, String_View src, String_View base) {
    if (!ctx || !f) return false;
    if (!f->is_regex) {
        return glob_match_sv(f->expr, base, false) || glob_match_sv(f->expr, src, false);
    }
    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    if (!src_c) return false;
    return regexec(&f->regex, src_c, 0, NULL, 0) == 0;
}

static bool copy_should_include(Evaluator_Context *ctx,
                                Copy_Filter *filters,
                                size_t filter_count,
                                bool files_matching,
                                String_View src,
                                String_View base) {
    bool include = !files_matching;
    for (size_t i = 0; i < filter_count; i++) {
        Copy_Filter *f = &filters[i];
        if (!copy_filter_matches(ctx, f, src, base)) continue;
        if (f->exclude) include = false;
        else include = true;
    }
    return include;
}

typedef struct {
    Cmake_Event_Origin origin;
    String_View command_name;
    SV_List args;
    bool files_matching;
    bool follow_symlink_chain;
    bool saw_follow_symlink_option;
    bool saw_unknown_permission_token;
    Copy_Filter filters[64];
    size_t filter_count;
    Copy_Permissions perms;
} Copy_Parse_State;

static bool copy_parse_on_option(Evaluator_Context *ctx,
                                 void *userdata,
                                 int id,
                                 SV_List values,
                                 size_t token_index) {
    if (!ctx || !userdata) return false;
    Copy_Parse_State *st = (Copy_Parse_State*)userdata;
    String_View token = nob_sv_from_cstr("");
    if (token_index < st->args.count) {
        token = st->args.items[token_index];
    }

    if (id == COPY_KEY_FILES_MATCHING) {
        st->files_matching = true;
        return true;
    }
    if (id == COPY_KEY_PATTERN || id == COPY_KEY_REGEX) {
        if (values.count == 0) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), st->command_name, st->origin,
                           nob_sv_from_cstr("file(COPY) missing argument after PATTERN/REGEX"),
                           token);
            return false;
        }
        if (st->filter_count >= NOB_ARRAY_LEN(st->filters)) {
            eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), st->command_name, st->origin,
                           nob_sv_from_cstr("file(COPY) filter limit reached; extra filters ignored"),
                           token);
            return true;
        }
        st->filters[st->filter_count].is_regex = (id == COPY_KEY_REGEX);
        st->filters[st->filter_count].expr = values.items[0];
        st->filter_count++;
        return true;
    }
    if (id == COPY_KEY_EXCLUDE) {
        if (st->filter_count == 0) {
            eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), st->command_name, st->origin,
                           nob_sv_from_cstr("file(COPY) EXCLUDE without a previous PATTERN/REGEX is ignored"),
                           token);
        } else {
            st->filters[st->filter_count - 1].exclude = true;
        }
        return true;
    }
    if (id == COPY_KEY_FOLLOW_SYMLINK_CHAIN) {
        st->follow_symlink_chain = true;
        st->saw_follow_symlink_option = true;
        return true;
    }
    if (id == COPY_KEY_USE_SOURCE_PERMISSIONS) {
        st->perms.saw_use_source_permissions = true;
        return true;
    }
    if (id == COPY_KEY_NO_SOURCE_PERMISSIONS) {
        st->perms.saw_no_source_permissions = true;
        return true;
    }
    if (id == COPY_KEY_PERMISSIONS ||
        id == COPY_KEY_FILE_PERMISSIONS ||
        id == COPY_KEY_DIRECTORY_PERMISSIONS) {
        mode_t parsed_mode = 0;
        bool has_any = false;
        for (size_t i = 0; i < values.count; i++) {
            if (copy_permission_add_token(&parsed_mode, values.items[i])) {
                has_any = true;
            } else {
                st->saw_unknown_permission_token = true;
                eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), st->command_name, st->origin,
                               nob_sv_from_cstr("file(COPY) unknown permission token"),
                               values.items[i]);
            }
        }
        if (!has_any) {
            eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), st->command_name, st->origin,
                           nob_sv_from_cstr("file(COPY) permission list has no valid tokens"),
                           token);
        } else if (id == COPY_KEY_PERMISSIONS) {
            st->perms.has_permissions = true;
            st->perms.permissions_mode = parsed_mode;
        } else if (id == COPY_KEY_FILE_PERMISSIONS) {
            st->perms.has_file_permissions = true;
            st->perms.file_permissions_mode = parsed_mode;
        } else if (id == COPY_KEY_DIRECTORY_PERMISSIONS) {
            st->perms.has_directory_permissions = true;
            st->perms.directory_permissions_mode = parsed_mode;
        }
        return true;
    }

    return true;
}

static bool copy_parse_on_positional(Evaluator_Context *ctx,
                                     void *userdata,
                                     String_View value,
                                     size_t token_index) {
    (void)ctx;
    (void)userdata;
    (void)value;
    (void)token_index;
    return true;
}

void eval_file_handle_copy(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 4) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY) requires sources and DESTINATION"),
                       nob_sv_from_cstr("Usage: file(COPY <src>... DESTINATION <dir>)"));
        return;
    }

    size_t dest_idx = SIZE_MAX;
    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "DESTINATION")) {
            dest_idx = i;
            break;
        }
    }
    if (dest_idx == SIZE_MAX || dest_idx + 1 >= args.count || dest_idx == 1) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY) missing DESTINATION or sources"),
                       nob_sv_from_cstr("Usage: file(COPY <src>... DESTINATION <dir>)"));
        return;
    }

    String_View dest = args.items[dest_idx + 1];
    if (!eval_sv_is_abs_path(dest)) {
        dest = eval_sv_path_join(eval_temp_arena(ctx), eval_file_current_bin_dir(ctx), dest);
    }

    static const Eval_Opt_Spec k_copy_specs[] = {
        {COPY_KEY_FILES_MATCHING, "FILES_MATCHING", EVAL_OPT_FLAG},
        {COPY_KEY_PATTERN, "PATTERN", EVAL_OPT_OPTIONAL_SINGLE},
        {COPY_KEY_REGEX, "REGEX", EVAL_OPT_OPTIONAL_SINGLE},
        {COPY_KEY_EXCLUDE, "EXCLUDE", EVAL_OPT_FLAG},
        {COPY_KEY_FOLLOW_SYMLINK_CHAIN, "FOLLOW_SYMLINK_CHAIN", EVAL_OPT_FLAG},
        {COPY_KEY_PERMISSIONS, "PERMISSIONS", EVAL_OPT_MULTI},
        {COPY_KEY_FILE_PERMISSIONS, "FILE_PERMISSIONS", EVAL_OPT_MULTI},
        {COPY_KEY_DIRECTORY_PERMISSIONS, "DIRECTORY_PERMISSIONS", EVAL_OPT_MULTI},
        {COPY_KEY_USE_SOURCE_PERMISSIONS, "USE_SOURCE_PERMISSIONS", EVAL_OPT_FLAG},
        {COPY_KEY_NO_SOURCE_PERMISSIONS, "NO_SOURCE_PERMISSIONS", EVAL_OPT_FLAG},
    };
    Copy_Parse_State parsed = {
        .origin = o,
        .command_name = node->as.cmd.name,
        .args = args,
        .files_matching = false,
        .follow_symlink_chain = false,
        .saw_follow_symlink_option = false,
        .saw_unknown_permission_token = false,
        .filter_count = 0,
        .perms = {0},
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("eval_file"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             args,
                             dest_idx + 2,
                             k_copy_specs,
                             NOB_ARRAY_LEN(k_copy_specs),
                             cfg,
                             copy_parse_on_option,
                             copy_parse_on_positional,
                             &parsed)) {
        return;
    }
    bool files_matching = parsed.files_matching;
    bool follow_symlink_chain = parsed.follow_symlink_chain;
    bool saw_unknown_permission_token = parsed.saw_unknown_permission_token;
    Copy_Filter *filters = parsed.filters;
    size_t filter_count = parsed.filter_count;
    Copy_Permissions perms = parsed.perms;

    for (size_t i = 0; i < filter_count; i++) {
        if (!filters[i].is_regex) continue;
        char *expr_c = eval_sv_to_cstr_temp(ctx, filters[i].expr);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, expr_c);
        if (regcomp(&filters[i].regex, expr_c, REG_EXTENDED) != 0) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(COPY) invalid REGEX filter"),
                           filters[i].expr);
            for (size_t j = 0; j < i; j++) {
                if (filters[j].is_regex && filters[j].regex_ready) regfree(&filters[j].regex);
            }
            return;
        }
        filters[i].regex_ready = true;
    }

    if (!eval_file_mkdir_p(ctx, dest)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY) failed to create destination"),
                       dest);
        return;
    }

    bool applied_any_permissions = false;
    for (size_t i = 1; i < dest_idx; i++) {
        String_View src = args.items[i];
        if (!eval_sv_is_abs_path(src)) {
            src = eval_sv_path_join(eval_temp_arena(ctx), eval_file_current_src_dir(ctx), src);
        }

        char *src_c = eval_sv_to_cstr_temp(ctx, src);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, src_c);

        const char *base = src_c;
        for (const char *p = src_c; *p; p++) {
            if (*p == '/' || *p == '\\') base = p + 1;
        }
        String_View base_sv = nob_sv_from_cstr(base);
        if (!copy_should_include(ctx, filters, filter_count, files_matching, src, base_sv)) {
            continue;
        }
        String_View dst_sv = eval_sv_path_join(eval_temp_arena(ctx), dest, nob_sv_from_cstr(base));
        char *dst_c = eval_sv_to_cstr_temp(ctx, dst_sv);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, dst_c);

        if (follow_symlink_chain && copy_path_is_symlink(src_c)) {
            if (!copy_follow_symlink_chain(ctx, node, o, src, dest, &perms, &applied_any_permissions)) {
                return;
            }
            continue;
        }

        bool src_is_dir = false;
        if (!copy_copy_entry_raw(src_c, dst_c, &src_is_dir)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(COPY) failed to copy entry"),
                           src);
            return;
        }

        mode_t mode = 0;
        if (copy_permissions_pick_mode(&perms, src_is_dir, &mode)) {
            if (!copy_apply_permissions(dst_c, mode)) {
                eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                               nob_sv_from_cstr("file(COPY) copied entry but failed to apply permissions"),
                               dst_sv);
            } else {
                applied_any_permissions = true;
            }
        }
    }

    if ((perms.has_permissions || perms.has_file_permissions || perms.has_directory_permissions) &&
        !applied_any_permissions &&
        !saw_unknown_permission_token) {
        eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY) permissions were requested but not applied"),
                       nob_sv_from_cstr("Check destination type and platform permission support"));
    }
    if (perms.saw_use_source_permissions || perms.saw_no_source_permissions) {
        eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY) USE_SOURCE_PERMISSIONS/NO_SOURCE_PERMISSIONS are not implemented"),
                       nob_sv_from_cstr("Use explicit PERMISSIONS/FILE_PERMISSIONS/DIRECTORY_PERMISSIONS"));
    }

    for (size_t i = 0; i < filter_count; i++) {
        if (filters[i].is_regex && filters[i].regex_ready) regfree(&filters[i].regex);
    }
}

bool eval_handle_file(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || args.count == 0) return !eval_should_stop(ctx);

    String_View subcmd = args.items[0];

    if (eval_sv_eq_ci_lit(subcmd, "GLOB")) {
        handle_file_glob(ctx, node, args, false);
    } else if (eval_sv_eq_ci_lit(subcmd, "GLOB_RECURSE")) {
        handle_file_glob(ctx, node, args, true);
    } else if (eval_sv_eq_ci_lit(subcmd, "READ")) {
        handle_file_read(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "STRINGS")) {
        handle_file_strings(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "COPY")) {
        eval_file_handle_copy(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "WRITE")) {
        handle_file_write(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "MAKE_DIRECTORY")) {
        handle_file_make_directory(ctx, node, args);
    } else if (eval_file_handle_fsops(ctx, node, args)) {
        // handled in eval_file_fsops.c
    } else if (eval_file_handle_transfer(ctx, node, args)) {
        // handled in eval_file_transfer.c
    } else if (eval_file_handle_generate_lock_archive(ctx, node, args)) {
        // handled in eval_file_generate_lock_archive.c
    } else {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Unsupported file() subcommand"),
                       subcmd);
    }
    return !eval_should_stop(ctx);
}
