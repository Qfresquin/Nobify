#include "eval_file.h"
#include "eval_file_internal.h"
#include "eval_expr.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool eval_var_truthy_or_default(EvalExecContext *ctx, const char *key, bool default_value) {
    if (!ctx || !key) return default_value;
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(key));
    if (v.count == 0) return default_value;
    return eval_truthy(ctx, v);
}

Eval_Result eval_handle_aux_source_directory(EvalExecContext *ctx, const Node *node) {
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

static int sv_lex_cmp_qsort(const void *a, const void *b) {
    const String_View *aa = (const String_View*)a;
    const String_View *bb = (const String_View*)b;

    size_t n = aa->count < bb->count ? aa->count : bb->count;
    int c = memcmp(aa->data, bb->data, n);
    if (c != 0) return c;
    return (aa->count < bb->count) ? -1 : (aa->count > bb->count ? 1 : 0);
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

void eval_file_handle_glob(EvalExecContext *ctx, const Node *node, SV_List args, bool recurse) {
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

    // TODO(file-parity): Add CMake 3.28 oracle cases for CONFIGURE_DEPENDS,
    // LIST_DIRECTORIES, recursive symlink traversal/FOLLOW_SYMLINKS policy
    // behavior, relative output spelling with multiple patterns, ordering, and
    // inaccessible-directory diagnostics. Current coverage proves basic
    // GLOB/GLOB_RECURSE/RELATIVE/REAL_PATH behavior only.
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

    SV_List patterns = NULL;

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
        if (!svu_list_push_temp(ctx, &patterns, pat)) return;
    }

    Eval_Glob_Request req = {
        .patterns = patterns,
        .pattern_count = arena_arr_len(patterns),
        .recursive = recurse,
        .list_directories = list_dirs,
        .case_insensitive = ci,
        .strict_failures = glob_strict,
    };
    Eval_Glob_Result glob_result = {0};
    if (!eval_service_glob(ctx, &req, &glob_result)) {
        eval_file_diag_error(ctx,
                             node,
                             EVAL_DIAG_IO_FAILURE,
                             o,
                             nob_sv_from_cstr("file(GLOB) failed to enumerate patterns"),
                             nob_sv_from_cstr(""));
        return;
    }
    if (eval_should_stop(ctx)) return;

    String_View *matches = glob_result.matches;
    size_t mcount = glob_result.match_count;
    open_failures = glob_result.open_failure_count;

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
    String_View base_dir = arena_arr_len(patterns) > 0 ? glob_base_dir(patterns[0]) : eval_current_source_dir(ctx);
    (void)eval_emit_fs_glob(ctx, o, out_var, base_dir, recurse);
}
