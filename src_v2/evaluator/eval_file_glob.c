#include "eval_file.h"
#include "eval_file_internal.h"
#include "eval_expr.h"
#include "sv_utils.h"
#include "arena_dyn.h"
#include "tinydir.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <glob.h>
#endif

static bool eval_var_truthy_or_default(Evaluator_Context *ctx, const char *key, bool default_value) {
    if (!ctx || !key) return default_value;
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(key));
    if (v.count == 0) return default_value;
    return eval_truthy(ctx, v);
}

Eval_Result eval_handle_aux_source_directory(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) != 2) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_MISSING_REQUIRED,
                             o,
                             nob_sv_from_cstr("aux_source_directory() requires a directory and an output variable"),
                             nob_sv_from_cstr("Usage: aux_source_directory(<dir> <var>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View dir = a[0];
    if (!eval_sv_is_abs_path(dir)) {
        dir = eval_sv_path_join(eval_temp_arena(ctx), eval_current_source_dir(ctx), dir);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    }

    SV_List sources = {0};
    if (!eval_list_dir_sources_sorted_temp(ctx, dir, &sources)) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_IO_FAILURE,
                             o,
                             nob_sv_from_cstr("aux_source_directory() failed to enumerate the source directory"),
                             dir);
        return eval_result_from_ctx(ctx);
    }

    if (!eval_var_set_current(ctx, a[1], eval_sv_join_semi_temp(ctx, sources, arena_arr_len(sources)))) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

bool eval_file_glob_match_sv(String_View pat, String_View str, bool ci) {
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
        if (!EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), *io_items, sv)) {
            globfree(&g);
            return false;
        }
        *io_count = arena_arr_len(*io_items);
        *io_cap = arena_arr_cap(*io_items);
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
        EVAL_DIAG_EMIT_SEV(ctx, strict_failures ? EV_DIAG_ERROR : EV_DIAG_WARNING, EVAL_DIAG_IO_FAILURE, nob_sv_from_cstr("eval_file"), node ? node->as.cmd.name : nob_sv_from_cstr("file"), origin, cause, dir_full);
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

        if (eval_file_glob_match_sv(pat, full, ci)) {
            if (list_dirs || !is_dir) {
                if (EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), *io_items, full)) {
                    *io_count = arena_arr_len(*io_items);
                    *io_cap = arena_arr_cap(*io_items);
                } else {
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

void eval_file_handle_glob(Evaluator_Context *ctx, const Node *node, SV_List args, bool recurse) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 3) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_MISSING_REQUIRED,
                             o,
                             nob_sv_from_cstr("file(GLOB) requires <var> and patterns"),
                             nob_sv_from_cstr(""));
        return;
    }

    String_View out_var = args[1];
    bool list_dirs = true;
    bool has_relative = false;
    String_View relative_base = nob_sv_from_cstr("");
    size_t pat_idx = 2;

    while (pat_idx < arena_arr_len(args) &&
           (eval_sv_eq_ci_lit(args[pat_idx], "CONFIGURE_DEPENDS") ||
            eval_sv_eq_ci_lit(args[pat_idx], "LIST_DIRECTORIES") ||
            eval_sv_eq_ci_lit(args[pat_idx], "RELATIVE"))) {
        if (eval_sv_eq_ci_lit(args[pat_idx], "LIST_DIRECTORIES")) {
            if (pat_idx + 1 < arena_arr_len(args)) list_dirs = eval_truthy(ctx, args[++pat_idx]);
        } else if (eval_sv_eq_ci_lit(args[pat_idx], "RELATIVE")) {
            if (pat_idx + 1 < arena_arr_len(args)) {
                relative_base = args[++pat_idx];
                has_relative = true;
            }
        }
        pat_idx++;
    }

    String_View *matches = NULL;
    size_t mcount = 0;
    size_t mcap = 0;

    bool ci = false;
#if defined(_WIN32) || defined(__APPLE__)
    ci = true;
#endif
    bool glob_strict = eval_var_truthy_or_default(ctx, EVAL_VAR_NOBIFY_FILE_GLOB_STRICT, false);
    size_t open_failures = 0;

    String_View current_src = eval_current_source_dir(ctx);
    if (has_relative && !eval_sv_is_abs_path(relative_base)) {
        relative_base = eval_sv_path_join(eval_temp_arena(ctx), current_src, relative_base);
    }

    for (size_t i = pat_idx; i < arena_arr_len(args); ++i) {
        String_View pat = args[i];
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
        eval_file_diag(ctx,
                       node,
                       glob_strict ? EV_DIAG_ERROR : EV_DIAG_WARNING,
                       EVAL_DIAG_IO_FAILURE,
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

    (void)eval_var_set_current(ctx, out_var, joined);
    String_View base_dir = pat_idx < arena_arr_len(args) ? glob_base_dir(args[pat_idx]) : eval_current_source_dir(ctx);
    (void)eval_emit_fs_glob(ctx, o, out_var, base_dir, recurse);
}
