#include "eval_target_internal.h"

static bool source_group_emit_assignment(EvalExecContext *ctx,
                                         Cmake_Event_Origin o,
                                         String_View file_path,
                                         String_View group_name) {
    if (!ctx) return false;
    String_View key_parts[3] = {
        nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::"),
        file_path,
        nob_sv_from_cstr(""),
    };
    String_View key = svu_join_no_sep_temp(ctx, key_parts, 3);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set_current(ctx, key, group_name)) return false;
    return eval_emit_var_set_current(ctx, o, key, group_name);
}

static String_View source_group_dirname(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    for (size_t i = path.count; i > 0; i--) {
        if (!svu_is_path_sep(path.data[i - 1])) continue;
        if (i == 1) return nob_sv_from_cstr("");
        return nob_sv_from_parts(path.data, i - 1);
    }
    return nob_sv_from_cstr("");
}

static bool source_group_path_relative_to_root(EvalExecContext *ctx,
                                               String_View root,
                                               String_View file_path,
                                               String_View *out_relative) {
    if (!out_relative) return false;
    *out_relative = nob_sv_from_cstr("");
    if (!ctx) return false;

    String_View root_norm = eval_sv_path_normalize_temp(ctx, root);
    if (eval_should_stop(ctx)) return false;
    String_View file_norm = eval_sv_path_normalize_temp(ctx, file_path);
    if (eval_should_stop(ctx)) return false;

    if (file_norm.count < root_norm.count) return false;
    if (root_norm.count > 0 && !svu_eq_ci_sv(nob_sv_from_parts(file_norm.data, root_norm.count), root_norm)) {
        return false;
    }
    if (file_norm.count == root_norm.count) {
        *out_relative = nob_sv_from_cstr("");
        return true;
    }
    if (!svu_is_path_sep(file_norm.data[root_norm.count])) return false;
    *out_relative = nob_sv_from_parts(file_norm.data + root_norm.count + 1,
                                      file_norm.count - root_norm.count - 1);
    return true;
}

static String_View source_group_join_tree_name_temp(EvalExecContext *ctx,
                                                    String_View prefix,
                                                    String_View relative_dir) {
    if (!ctx) return nob_sv_from_cstr("");
    if (prefix.count == 0 && relative_dir.count == 0) return nob_sv_from_cstr("");
    if (relative_dir.count == 0) return prefix;
    if (prefix.count == 0) return relative_dir;

    size_t total = prefix.count + 1 + relative_dir.count;
    char *buf = (char *)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix.data, prefix.count);
    off += prefix.count;
    buf[off++] = '\\';
    memcpy(buf + off, relative_dir.data, relative_dir.count);
    off += relative_dir.count;
    buf[off] = '\0';

    for (size_t i = 0; i < off; i++) {
        if (svu_is_path_sep(buf[i])) buf[i] = '\\';
    }

    return nob_sv_from_parts(buf, off);
}

static bool source_group_emit_regex_rule(EvalExecContext *ctx,
                                         Cmake_Event_Origin o,
                                         String_View group_name,
                                         String_View regex_value) {
    if (!ctx) return false;
    String_View line_sv = nob_sv_from_cstr(nob_temp_sprintf("%zu", o.line));
    String_View col_sv = nob_sv_from_cstr(nob_temp_sprintf("%zu", o.col));
    String_View key_parts[5] = {
        nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::"),
        line_sv,
        nob_sv_from_cstr(":"),
        col_sv,
        nob_sv_from_cstr(""),
    };
    String_View key = svu_join_no_sep_temp(ctx, key_parts, 5);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set_current(ctx, key, regex_value)) return false;
    if (!eval_emit_var_set_current(ctx, o, key, regex_value)) return false;

    String_View name_key_parts[3] = {
        key,
        nob_sv_from_cstr("::NAME"),
        nob_sv_from_cstr(""),
    };
    String_View name_key = svu_join_no_sep_temp(ctx, name_key_parts, 3);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set_current(ctx, name_key, group_name)) return false;
    return eval_emit_var_set_current(ctx, o, name_key, group_name);
}

Eval_Result eval_handle_source_group(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("source_group() requires a group descriptor and mapping input"), nob_sv_from_cstr("Usage: source_group(<name> [FILES <src>...]) or source_group(TREE <root> [PREFIX <prefix>] FILES <src>...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    if (eval_sv_eq_ci_lit(a[0], "TREE")) {
        if (arena_arr_len(a) < 4) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("source_group(TREE ...) requires root and FILES list"), nob_sv_from_cstr("Usage: source_group(TREE <root> [PREFIX <prefix>] FILES <src>...)"));
            return eval_result_from_ctx(ctx);
        }

        String_View root = eval_path_resolve_for_cmake_arg(ctx, a[1], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        String_View prefix = nob_sv_from_cstr("");
        size_t files_index = 2;
        if (files_index < arena_arr_len(a) && eval_sv_eq_ci_lit(a[files_index], "PREFIX")) {
            if (files_index + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("source_group(TREE ... PREFIX) requires a value"), nob_sv_from_cstr("Usage: source_group(TREE <root> PREFIX <prefix> FILES <src>...)"));
                return eval_result_from_ctx(ctx);
            }
            prefix = a[files_index + 1];
            files_index += 2;
        }
        if (files_index >= arena_arr_len(a) || !eval_sv_eq_ci_lit(a[files_index], "FILES") || files_index + 1 >= arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("source_group(TREE ...) requires FILES followed by at least one source"), nob_sv_from_cstr("Usage: source_group(TREE <root> [PREFIX <prefix>] FILES <src>...)"));
            return eval_result_from_ctx(ctx);
        }

        for (size_t i = files_index + 1; i < arena_arr_len(a); i++) {
            String_View file_path = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            String_View relative = nob_sv_from_cstr("");
            if (!source_group_path_relative_to_root(ctx, root, file_path, &relative)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("source_group(TREE ...) file is outside the declared tree root"), file_path);
                return eval_result_from_ctx(ctx);
            }
            String_View group_name = source_group_join_tree_name_temp(ctx, prefix, source_group_dirname(relative));
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            if (!source_group_emit_assignment(ctx, o, file_path, group_name)) return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    String_View group_name = a[0];
    bool have_files = false;
    bool have_regex = false;
    String_View regex_value = nob_sv_from_cstr("");
    SV_List files = NULL;

    if (arena_arr_len(a) == 2 &&
        !eval_sv_eq_ci_lit(a[1], "FILES") &&
        !eval_sv_eq_ci_lit(a[1], "REGULAR_EXPRESSION")) {
        have_regex = true;
        regex_value = a[1];
    } else {
        size_t i = 1;
        while (i < arena_arr_len(a)) {
            if (eval_sv_eq_ci_lit(a[i], "FILES")) {
                i++;
                if (i >= arena_arr_len(a) || eval_sv_eq_ci_lit(a[i], "REGULAR_EXPRESSION")) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("source_group(FILES) requires at least one source"), nob_sv_from_cstr("Usage: source_group(<name> [FILES <src>...] [REGULAR_EXPRESSION <regex>])"));
                    return eval_result_from_ctx(ctx);
                }
                while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "REGULAR_EXPRESSION")) {
                    if (!svu_list_push_temp(ctx, &files, a[i])) return eval_result_from_ctx(ctx);
                    i++;
                }
                have_files = arena_arr_len(files) > 0;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "REGULAR_EXPRESSION")) {
                if (have_regex || i + 1 >= arena_arr_len(a)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("source_group(REGULAR_EXPRESSION) requires exactly one regex"), nob_sv_from_cstr("Usage: source_group(<name> [FILES <src>...] [REGULAR_EXPRESSION <regex>])"));
                    return eval_result_from_ctx(ctx);
                }
                have_regex = true;
                regex_value = a[i + 1];
                i += 2;
                continue;
            }

            if (have_files) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("source_group() received unexpected argument"), a[i]);
                return eval_result_from_ctx(ctx);
            }

            if (!svu_list_push_temp(ctx, &files, a[i])) return eval_result_from_ctx(ctx);
            have_files = arena_arr_len(files) > 0;
            i++;
        }
    }

    if (!have_files && !have_regex) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("source_group() requires source files and/or a regular expression"), nob_sv_from_cstr("Usage: source_group(<name> [FILES <src>...] [REGULAR_EXPRESSION <regex>])"));
        return eval_result_from_ctx(ctx);
    }

    if (have_regex) {
        if (!source_group_emit_regex_rule(ctx, o, group_name, regex_value)) return eval_result_from_ctx(ctx);
    }

    for (size_t i = 0; i < arena_arr_len(files); i++) {
        String_View file_path = eval_path_resolve_for_cmake_arg(ctx, files[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (!source_group_emit_assignment(ctx, o, file_path, group_name)) return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}
