#include "eval_file.h"
#include "evaluator_internal.h"
#include "eval_expr.h"
#include "arena_dyn.h"
#include "tinydir.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#if !defined(_WIN32)
#include <glob.h>
#endif

static bool ch_is_sep(char c) { return c == '/' || c == '\\'; }

static String_View current_src_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    return v.count > 0 ? v : ctx->source_dir;
}

static String_View current_bin_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

static char *sv_to_cstr_temp(Evaluator_Context *ctx, String_View sv) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sv.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (sv.count) memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    return buf;
}

static bool parse_size_sv(String_View sv, size_t *out) {
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

static bool is_path_safe(String_View path) {
    if (eval_sv_eq_ci_lit(path, "..")) return false;
    for (size_t i = 0; i + 2 < path.count; i++) {
        if (path.data[i] == '.' && path.data[i + 1] == '.' && ch_is_sep(path.data[i + 2])) return false;
        if (ch_is_sep(path.data[i]) && path.data[i + 1] == '.' && path.data[i + 2] == '.') return false;
    }
    return true;
}

static bool mkdir_p(const char *path) {
    if (!path) return false;
    size_t len0 = strlen(path);
    char *tmp = (char*)malloc(len0 + 1);
    if (!tmp) return false;
    memcpy(tmp, path, len0 + 1);
    for (size_t i = 0; i < len0; i++) {
        if (tmp[i] == '\\') tmp[i] = '/';
    }

    size_t len = strlen(tmp);
    while (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }
    if (len == 0) {
        free(tmp);
        return false;
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            // Keep "C:/" intact before creating nested directories.
            if ((p == tmp + 2) && isalpha((unsigned char)tmp[0]) && tmp[1] == ':') continue;
            *p = 0;
            nob_mkdir_if_not_exists(tmp);
            *p = '/';
        }
    }
    bool ok = nob_mkdir_if_not_exists(tmp);
    free(tmp);
    return ok;
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
                if (!ch_is_sep(sc)) {
                    pi++;
                    si++;
                    continue;
                }
            }

            if (pc == '[') {
                if (!ch_is_sep(sc)) {
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
            if (star_si < str.count && ch_is_sep(str.data[star_si])) {
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
    char *pat_c = sv_to_cstr_temp(ctx, pat);
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

static String_View sv_dirname(String_View path) {
    for (size_t i = path.count; i-- > 0;) {
        if (ch_is_sep(path.data[i])) {
            if (i == 0) return nob_sv_from_parts(path.data, 1); // "/" root
            return nob_sv_from_parts(path.data, i);
        }
    }
    return nob_sv_from_cstr(".");
}

static bool sv_has_glob_meta(String_View sv) {
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '*' || c == '?' || c == '[') return true;
    }
    return false;
}

static String_View glob_base_dir(String_View pattern_abs) {
    size_t first_meta = pattern_abs.count;
    for (size_t i = 0; i < pattern_abs.count; i++) {
        char c = pattern_abs.data[i];
        if (c == '*' || c == '?' || c == '[') {
            first_meta = i;
            break;
        }
    }
    if (first_meta == pattern_abs.count) return sv_dirname(pattern_abs);
    if (first_meta == 0) return nob_sv_from_cstr(".");
    return sv_dirname(nob_sv_from_parts(pattern_abs.data, first_meta));
}

static bool sv_path_prefix_eq(String_View path, String_View prefix, bool ci) {
    if (prefix.count > path.count) return false;
    for (size_t i = 0; i < prefix.count; i++) {
        char a = path.data[i];
        char b = prefix.data[i];
        if (ch_is_sep(a) && ch_is_sep(b)) continue;
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
    if (off < path.count && ch_is_sep(path.data[off])) off++;
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
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
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
            file_glob_walk(ctx, node, origin, full, pat, recurse, list_dirs, ci, io_items, io_count, io_cap);
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
        file_glob_walk(ctx, node, o, base_dir, pat, recurse, list_dirs, ci, &matches, &mcount, &mcap);
        if (ctx->oom) return;
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

    String_View path = args.items[1];

    if (!is_path_safe(path)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("eval_file"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Security Violation: Path traversal (..) is not allowed"),
                       path);
        return;
    }

    if (!eval_sv_is_abs_path(path)) {
        path = eval_sv_path_join(eval_temp_arena(ctx), ctx->binary_dir, path);
    } else {
        if (!sv_starts_with(path, ctx->binary_dir) && !sv_starts_with(path, ctx->source_dir)) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("eval_file"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("Security Violation: Absolute path outside project scope"),
                           path);
            return;
        }
    }

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
        mkdir_p(dir_c);
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

static void handle_file_read(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(READ) requires <path> and <out-var>"),
                       nob_sv_from_cstr("Usage: file(READ <path> <out-var> [OFFSET n] [LIMIT n] [HEX])"));
        return;
    }

    String_View path = args.items[1];
    String_View out_var = args.items[2];
    size_t offset = 0;
    bool has_limit = false;
    size_t limit = 0;
    bool hex = false;

    for (size_t i = 3; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "OFFSET") && i + 1 < args.count) {
            (void)parse_size_sv(args.items[++i], &offset);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "LIMIT") && i + 1 < args.count) {
            has_limit = parse_size_sv(args.items[++i], &limit);
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "HEX")) {
            hex = true;
        }
    }

    if (!eval_sv_is_abs_path(path)) {
        path = eval_sv_path_join(eval_temp_arena(ctx), current_src_dir(ctx), path);
    }

    char *path_c = sv_to_cstr_temp(ctx, path);
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
        ctx_oom(ctx);
        return;
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

    String_View path = args.items[1];
    String_View out_var = args.items[2];
    if (!eval_sv_is_abs_path(path)) {
        path = eval_sv_path_join(eval_temp_arena(ctx), current_src_dir(ctx), path);
    }
    char *path_c = sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, path_c);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(STRINGS) failed to read file"),
                       path);
        return;
    }

    // Simplified behavior: each text line is one list item.
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
    if (!buf) {
        nob_sb_free(sb);
        ctx_oom(ctx);
        return;
    }

    size_t off = 0;
    size_t line_start = 0;
    bool first = true;
    while (line_start <= sb.count) {
        size_t line_end = line_start;
        while (line_end < sb.count && sb.items[line_end] != '\n') line_end++;

        size_t len = line_end - line_start;
        if (len > 0 && sb.items[line_start + len - 1] == '\r') len--;
        if (len > 0) {
            if (!first) buf[off++] = ';';
            memcpy(buf + off, sb.items + line_start, len);
            off += len;
            first = false;
        }

        if (line_end >= sb.count) break;
        line_start = line_end + 1;
    }
    buf[off] = '\0';
    (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(buf));
    nob_sb_free(sb);
}

static void handle_file_copy(Evaluator_Context *ctx, const Node *node, SV_List args) {
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
        dest = eval_sv_path_join(eval_temp_arena(ctx), current_bin_dir(ctx), dest);
    }
    char *dest_c = sv_to_cstr_temp(ctx, dest);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, dest_c);
    if (!mkdir_p(dest_c)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(COPY) failed to create destination"),
                       dest);
        return;
    }

    for (size_t i = 1; i < dest_idx; i++) {
        String_View src = args.items[i];
        if (!eval_sv_is_abs_path(src)) {
            src = eval_sv_path_join(eval_temp_arena(ctx), current_src_dir(ctx), src);
        }

        char *src_c = sv_to_cstr_temp(ctx, src);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, src_c);

        const char *base = src_c;
        for (const char *p = src_c; *p; p++) {
            if (*p == '/' || *p == '\\') base = p + 1;
        }
        String_View dst_sv = eval_sv_path_join(eval_temp_arena(ctx), dest, nob_sv_from_cstr(base));
        char *dst_c = sv_to_cstr_temp(ctx, dst_sv);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, dst_c);

        bool ok = true;
        tinydir_file tf = {0};
        if (tinydir_file_open(&tf, src_c) == 0) {
            ok = tf.is_dir ? nob_copy_directory_recursively(src_c, dst_c)
                           : nob_copy_file(src_c, dst_c);
        } else {
            ok = nob_copy_file(src_c, dst_c);
        }

        if (!ok) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           nob_sv_from_cstr("file(COPY) failed to copy entry"),
                           src);
            return;
        }
    }
}

bool h_file(Evaluator_Context *ctx, const Node *node) {
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
        handle_file_copy(ctx, node, args);
    } else if (eval_sv_eq_ci_lit(subcmd, "WRITE")) {
        handle_file_write(ctx, node, args);
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
