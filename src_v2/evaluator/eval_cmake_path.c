#include "eval_cmake_path.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <ctype.h>
#include <string.h>

static void cmk_path_error(Evaluator_Context *ctx,
                           const Node *node,
                           Cmake_Event_Origin o,
                           const char *cause,
                           String_View hint) {
    eval_emit_diag(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr(cause),
                   hint);
}

static size_t path_last_separator_index(String_View path) {
    for (size_t i = path.count; i > 0; i--) {
        if (svu_is_path_sep(path.data[i - 1])) return i - 1;
    }
    return SIZE_MAX;
}

static String_View cmk_path_unc_root_name_sv(String_View path) {
    if (path.count < 2 || !svu_is_path_sep(path.data[0]) || !svu_is_path_sep(path.data[1])) {
        return nob_sv_from_cstr("");
    }

    size_t pos = 2;
    while (pos < path.count && svu_is_path_sep(path.data[pos])) pos++;

    size_t server_start = pos;
    while (pos < path.count && !svu_is_path_sep(path.data[pos])) pos++;
    if (pos == server_start) return nob_sv_from_cstr("");

    while (pos < path.count && svu_is_path_sep(path.data[pos])) pos++;
    size_t share_start = pos;
    while (pos < path.count && !svu_is_path_sep(path.data[pos])) pos++;
    if (pos == share_start) return nob_sv_from_cstr("");

    return nob_sv_from_parts(path.data, pos);
}

static String_View cmk_path_root_name_sv(String_View path) {
    String_View unc_root = cmk_path_unc_root_name_sv(path);
    if (unc_root.count > 0) return unc_root;
    if (path.count >= 2 && isalpha((unsigned char)path.data[0]) && path.data[1] == ':') {
        return nob_sv_from_parts(path.data, 2);
    }
    return nob_sv_from_cstr("");
}

static String_View cmk_path_root_directory_sv(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    if (cmk_path_unc_root_name_sv(path).count > 0) return nob_sv_from_cstr("/");
    if (svu_is_path_sep(path.data[0])) return nob_sv_from_cstr("/");
    if (path.count >= 3 &&
        isalpha((unsigned char)path.data[0]) &&
        path.data[1] == ':' &&
        svu_is_path_sep(path.data[2])) {
        return nob_sv_from_cstr("/");
    }
    return nob_sv_from_cstr("");
}

static String_View cmk_path_root_path_temp(Evaluator_Context *ctx, String_View path) {
    String_View root_name = cmk_path_root_name_sv(path);
    String_View root_dir = cmk_path_root_directory_sv(path);
    if (root_name.count == 0) return root_dir;
    if (root_dir.count == 0) return root_name;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), root_name.count + root_dir.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, root_name.data, root_name.count);
    memcpy(buf + root_name.count, root_dir.data, root_dir.count);
    buf[root_name.count + root_dir.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View cmk_path_filename_sv(String_View path) {
    size_t sep = path_last_separator_index(path);
    if (sep == SIZE_MAX) return path;
    return nob_sv_from_parts(path.data + sep + 1, path.count - sep - 1);
}

static ssize_t cmk_path_dot_index(String_View name, bool last_only) {
    ssize_t dot = -1;
    if (name.count == 0) return -1;

    if (last_only) {
        for (size_t i = name.count; i > 0; i--) {
            if (name.data[i - 1] == '.') {
                dot = (ssize_t)(i - 1);
                break;
            }
        }
    } else {
        for (size_t i = 0; i < name.count; i++) {
            if (name.data[i] == '.') {
                dot = (ssize_t)i;
                break;
            }
        }
    }

    if (dot <= 0) return -1;
    return dot;
}

static String_View cmk_path_extension_from_name_sv(String_View name, bool last_only) {
    ssize_t dot = cmk_path_dot_index(name, last_only);
    if (dot < 0) return nob_sv_from_cstr("");
    return nob_sv_from_parts(name.data + dot, name.count - (size_t)dot);
}

static String_View cmk_path_stem_from_name_sv(String_View name, bool last_only) {
    ssize_t dot = cmk_path_dot_index(name, last_only);
    if (dot < 0) return name;
    return nob_sv_from_parts(name.data, (size_t)dot);
}

static String_View cmk_path_relative_part_temp(Evaluator_Context *ctx, String_View path) {
    String_View root = cmk_path_root_path_temp(ctx, path);
    if (root.count == 0) return path;
    if (path.count <= root.count) return nob_sv_from_cstr("");
    String_View rel = nob_sv_from_parts(path.data + root.count, path.count - root.count);
    while (rel.count > 0 && svu_is_path_sep(rel.data[0])) {
        rel = nob_sv_from_parts(rel.data + 1, rel.count - 1);
    }
    return rel;
}

static String_View cmk_path_parent_part_sv(String_View path) {
    size_t sep = path_last_separator_index(path);
    if (sep == SIZE_MAX) return nob_sv_from_cstr("");
    if (sep == 0) return nob_sv_from_cstr("/");
    if (sep == 2 && path.count >= 3 && path.data[1] == ':') {
        return nob_sv_from_parts(path.data, 3);
    }
    return nob_sv_from_parts(path.data, sep);
}

static String_View cmk_path_normalize_temp(Evaluator_Context *ctx, String_View input) {
    return eval_sv_path_normalize_temp(ctx, input);
}

static String_View cmk_path_current_source_dir(Evaluator_Context *ctx) {
    String_View cur = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (cur.count == 0) cur = ctx->source_dir;
    return cur;
}

static String_View cmk_path_make_absolute_temp(Evaluator_Context *ctx, String_View value, String_View base_dir) {
    if (!ctx) return nob_sv_from_cstr("");
    if (eval_sv_is_abs_path(value)) return value;

    String_View base = base_dir;
    if (base.count == 0) base = cmk_path_current_source_dir(ctx);
    if (!eval_sv_is_abs_path(base)) {
        base = eval_sv_path_join(eval_temp_arena(ctx), cmk_path_current_source_dir(ctx), base);
    }
    base = cmk_path_normalize_temp(ctx, base);

    String_View abs = eval_sv_path_join(eval_temp_arena(ctx), base, value);
    return cmk_path_normalize_temp(ctx, abs);
}

static void cmk_path_collect_segments_after_root(Evaluator_Context *ctx,
                                                 String_View path,
                                                 String_View root,
                                                 SV_List *out) {
    if (!ctx || !out) return;
    size_t pos = root.count;
    while (pos < path.count && svu_is_path_sep(path.data[pos])) pos++;
    while (pos < path.count) {
        size_t start = pos;
        while (pos < path.count && !svu_is_path_sep(path.data[pos])) pos++;
        if (pos > start) (void)svu_list_push_temp(ctx, out, nob_sv_from_parts(path.data + start, pos - start));
        while (pos < path.count && svu_is_path_sep(path.data[pos])) pos++;
    }
}

static String_View cmk_path_relativize_temp(Evaluator_Context *ctx, String_View path, String_View base_dir) {
    String_View a = cmk_path_normalize_temp(ctx, path);
    String_View b = cmk_path_normalize_temp(ctx, base_dir);
    if (ctx->oom) return nob_sv_from_cstr("");

    String_View root_a = cmk_path_root_path_temp(ctx, a);
    String_View root_b = cmk_path_root_path_temp(ctx, b);
    if (!svu_eq_ci_sv(root_a, root_b)) return a;

    SV_List seg_a = {0};
    SV_List seg_b = {0};
    cmk_path_collect_segments_after_root(ctx, a, root_a, &seg_a);
    cmk_path_collect_segments_after_root(ctx, b, root_b, &seg_b);
    if (ctx->oom) return nob_sv_from_cstr("");

    size_t common = 0;
    while (common < seg_a.count && common < seg_b.count && svu_eq_ci_sv(seg_a.items[common], seg_b.items[common])) {
        common++;
    }

    size_t total = 0;
    size_t count = 0;
    for (size_t i = common; i < seg_b.count; i++) {
        total += 2;
        if (count > 0) total += 1;
        count++;
    }
    for (size_t i = common; i < seg_a.count; i++) {
        total += seg_a.items[i].count;
        if (count > 0) total += 1;
        count++;
    }
    if (count == 0) total = 1;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;
    size_t emitted = 0;
    for (size_t i = common; i < seg_b.count; i++) {
        if (emitted > 0) buf[off++] = '/';
        buf[off++] = '.';
        buf[off++] = '.';
        emitted++;
    }
    for (size_t i = common; i < seg_a.count; i++) {
        if (emitted > 0) buf[off++] = '/';
        memcpy(buf + off, seg_a.items[i].data, seg_a.items[i].count);
        off += seg_a.items[i].count;
        emitted++;
    }
    if (emitted == 0) buf[off++] = '.';
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View cmk_path_to_cmake_seps_temp(Evaluator_Context *ctx, String_View in) {
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) {
        buf[i] = svu_is_path_sep(in.data[i]) ? '/' : in.data[i];
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View cmk_path_to_native_seps_temp(Evaluator_Context *ctx, String_View in) {
    char sep = '/';
#if defined(_WIN32)
    sep = '\\';
#endif
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) {
        buf[i] = svu_is_path_sep(in.data[i]) ? sep : in.data[i];
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool cmk_path_split_char_list_temp(Evaluator_Context *ctx, String_View in, char sep, SV_List *out) {
    if (!ctx || !out) return false;
    size_t start = 0;
    for (size_t i = 0; i <= in.count; i++) {
        if (i < in.count && in.data[i] != sep) continue;
        String_View part = nob_sv_from_parts(in.data + start, i - start);
        if (!svu_list_push_temp(ctx, out, part)) return false;
        start = i + 1;
    }
    return true;
}

static String_View cmk_path_join_char_list_temp(Evaluator_Context *ctx, SV_List list, char sep) {
    if (!ctx) return nob_sv_from_cstr("");
    if (list.count == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < list.count; i++) {
        total += list.items[i].count;
        if (i + 1 < list.count) total += 1;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < list.count; i++) {
        if (list.items[i].count > 0) {
            memcpy(buf + off, list.items[i].data, list.items[i].count);
            off += list.items[i].count;
        }
        if (i + 1 < list.count) buf[off++] = sep;
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View cmk_path_component_get_temp(Evaluator_Context *ctx,
                                               String_View input,
                                               String_View component,
                                               bool last_only,
                                               bool *ok) {
    if (ok) *ok = true;

    if (eval_sv_eq_ci_lit(component, "ROOT_NAME")) return cmk_path_root_name_sv(input);
    if (eval_sv_eq_ci_lit(component, "ROOT_DIRECTORY")) return cmk_path_root_directory_sv(input);
    if (eval_sv_eq_ci_lit(component, "ROOT_PATH")) return cmk_path_root_path_temp(ctx, input);
    if (eval_sv_eq_ci_lit(component, "FILENAME")) return cmk_path_filename_sv(input);
    if (eval_sv_eq_ci_lit(component, "RELATIVE_PART")) return cmk_path_relative_part_temp(ctx, input);
    if (eval_sv_eq_ci_lit(component, "PARENT_PATH")) return cmk_path_parent_part_sv(input);

    if (eval_sv_eq_ci_lit(component, "EXTENSION")) {
        return cmk_path_extension_from_name_sv(cmk_path_filename_sv(input), last_only);
    }
    if (eval_sv_eq_ci_lit(component, "STEM")) {
        return cmk_path_stem_from_name_sv(cmk_path_filename_sv(input), last_only);
    }

    if (ok) *ok = false;
    return nob_sv_from_cstr("");
}

static bool cmk_path_set_result(Evaluator_Context *ctx,
                                String_View path_var,
                                String_View out_var,
                                String_View value) {
    String_View dst = out_var.count > 0 ? out_var : path_var;
    return eval_var_set(ctx, dst, value);
}

static bool cmk_path_is_component_supports_last_only(String_View component) {
    return eval_sv_eq_ci_lit(component, "EXTENSION") || eval_sv_eq_ci_lit(component, "STEM");
}

static String_View cmk_path_compare_canonical_temp(Evaluator_Context *ctx, String_View in) {
    String_View cmk = cmk_path_to_cmake_seps_temp(ctx, in);
    if (ctx->oom) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), cmk.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;
    bool last_was_sep = false;
    for (size_t i = 0; i < cmk.count; i++) {
        char c = cmk.data[i];
        if (c == '/') {
            if (!last_was_sep) buf[off++] = c;
            last_was_sep = true;
            continue;
        }
        last_was_sep = false;
        buf[off++] = c;
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool handle_set(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(SET) requires <path-var> and <input>",
                       nob_sv_from_cstr("Usage: cmake_path(SET <path-var> [NORMALIZE] <input>)"));
        return true;
    }

    String_View path_var = a.items[1];
    bool normalize = false;
    String_View input = nob_sv_from_cstr("");

    for (size_t i = 2; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "NORMALIZE")) {
            normalize = true;
            continue;
        }
        if (input.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(SET) received unexpected argument",
                           a.items[i]);
            return true;
        }
        input = a.items[i];
    }

    if (input.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(SET) requires an input path",
                       nob_sv_from_cstr("Usage: cmake_path(SET <path-var> [NORMALIZE] <input>)"));
        return true;
    }

    String_View out = normalize ? cmk_path_normalize_temp(ctx, input) : input;
    (void)eval_var_set(ctx, path_var, out);
    return true;
}

static bool handle_get(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 4) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(GET) requires <path-var> <component> <out-var>",
                       nob_sv_from_cstr("Usage: cmake_path(GET <path-var> <component> [LAST_ONLY] <out-var>)"));
        return true;
    }

    String_View path_var = a.items[1];
    String_View component = a.items[2];
    bool supports_last_only = cmk_path_is_component_supports_last_only(component);

    bool last_only = false;
    String_View out_var = nob_sv_from_cstr("");

    if (supports_last_only) {
        if (a.count == 4) {
            out_var = a.items[3];
        } else if (a.count == 5 && eval_sv_eq_ci_lit(a.items[3], "LAST_ONLY")) {
            last_only = true;
            out_var = a.items[4];
        } else {
            cmk_path_error(ctx, node, o,
                           "cmake_path(GET) invalid argument combination for component",
                           component);
            return true;
        }
    } else {
        if (a.count != 4) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(GET) received unexpected argument",
                           a.items[4]);
            return true;
        }
        out_var = a.items[3];
    }

    String_View input = eval_var_get(ctx, path_var);
    bool ok = false;
    String_View result = cmk_path_component_get_temp(ctx, input, component, last_only, &ok);
    if (!ok) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(GET) unsupported component",
                       component);
        return true;
    }

    (void)eval_var_set(ctx, out_var, result);
    return true;
}

static bool handle_append_like(Evaluator_Context *ctx,
                               const Node *node,
                               Cmake_Event_Origin o,
                               SV_List a,
                               bool string_mode) {
    if (a.count < 2) {
        cmk_path_error(ctx, node, o,
                       string_mode
                           ? "cmake_path(APPEND_STRING) requires <path-var>"
                           : "cmake_path(APPEND) requires <path-var>",
                       string_mode
                           ? nob_sv_from_cstr("Usage: cmake_path(APPEND_STRING <path-var> [<input>...] [OUTPUT_VARIABLE <out-var>])")
                           : nob_sv_from_cstr("Usage: cmake_path(APPEND <path-var> [<input>...] [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a.items[1];
    String_View out_var = nob_sv_from_cstr("");
    String_View current = eval_var_get(ctx, path_var);

    for (size_t i = 2; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= a.count) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(APPEND*) OUTPUT_VARIABLE requires a variable name",
                               a.items[i]);
                return true;
            }
            out_var = a.items[++i];
            continue;
        }

        if (string_mode) {
            String_View parts[2] = {current, a.items[i]};
            current = svu_join_no_sep_temp(ctx, parts, 2);
        } else {
            if (current.count == 0) current = a.items[i];
            else current = eval_sv_path_join(eval_temp_arena(ctx), current, a.items[i]);
        }
    }

    (void)cmk_path_set_result(ctx, path_var, out_var, current);
    return true;
}

static bool handle_remove_filename(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 2) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(REMOVE_FILENAME) requires <path-var>",
                       nob_sv_from_cstr("Usage: cmake_path(REMOVE_FILENAME <path-var> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a.items[1];
    String_View out_var = nob_sv_from_cstr("");
    if (a.count > 2) {
        if (a.count == 4 && eval_sv_eq_ci_lit(a.items[2], "OUTPUT_VARIABLE")) {
            out_var = a.items[3];
        } else {
            cmk_path_error(ctx, node, o,
                           "cmake_path(REMOVE_FILENAME) received unexpected argument",
                           a.items[2]);
            return true;
        }
    }

    String_View value = eval_var_get(ctx, path_var);
    size_t sep = path_last_separator_index(value);
    String_View out = nob_sv_from_cstr("");
    if (sep != SIZE_MAX) {
        if (sep == 0) out = nob_sv_from_cstr("/");
        else out = nob_sv_from_parts(value.data, sep + 1);
    }

    (void)cmk_path_set_result(ctx, path_var, out_var, out);
    return true;
}

static bool handle_replace_filename(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(REPLACE_FILENAME) requires <path-var> and <input>",
                       nob_sv_from_cstr("Usage: cmake_path(REPLACE_FILENAME <path-var> <input> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a.items[1];
    String_View input = nob_sv_from_cstr("");
    String_View out_var = nob_sv_from_cstr("");

    for (size_t i = 2; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= a.count) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(REPLACE_FILENAME) OUTPUT_VARIABLE requires a variable name",
                               a.items[i]);
                return true;
            }
            out_var = a.items[++i];
            continue;
        }
        if (input.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(REPLACE_FILENAME) received unexpected argument",
                           a.items[i]);
            return true;
        }
        input = a.items[i];
    }

    if (input.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(REPLACE_FILENAME) requires replacement input",
                       nob_sv_from_cstr("Usage: cmake_path(REPLACE_FILENAME <path-var> <input> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View value = eval_var_get(ctx, path_var);
    size_t sep = path_last_separator_index(value);
    String_View out = input;
    if (sep != SIZE_MAX) {
        String_View parent_with_sep = nob_sv_from_parts(value.data, sep + 1);
        String_View parts[2] = {parent_with_sep, input};
        out = svu_join_no_sep_temp(ctx, parts, 2);
    }

    (void)cmk_path_set_result(ctx, path_var, out_var, out);
    return true;
}

static bool handle_extension_common(Evaluator_Context *ctx,
                                    const Node *node,
                                    Cmake_Event_Origin o,
                                    SV_List a,
                                    bool replace_mode) {
    if (a.count < 2) {
        cmk_path_error(ctx, node, o,
                       replace_mode
                           ? "cmake_path(REPLACE_EXTENSION) requires <path-var>"
                           : "cmake_path(REMOVE_EXTENSION) requires <path-var>",
                       replace_mode
                           ? nob_sv_from_cstr("Usage: cmake_path(REPLACE_EXTENSION <path-var> [LAST_ONLY] <input> [OUTPUT_VARIABLE <out-var>])")
                           : nob_sv_from_cstr("Usage: cmake_path(REMOVE_EXTENSION <path-var> [LAST_ONLY] [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a.items[1];
    bool last_only = false;
    String_View replacement = nob_sv_from_cstr("");
    String_View out_var = nob_sv_from_cstr("");

    for (size_t i = 2; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "LAST_ONLY")) {
            last_only = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= a.count) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(*_EXTENSION) OUTPUT_VARIABLE requires a variable name",
                               a.items[i]);
                return true;
            }
            out_var = a.items[++i];
            continue;
        }

        if (!replace_mode) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(REMOVE_EXTENSION) received unexpected argument",
                           a.items[i]);
            return true;
        }

        if (replacement.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(REPLACE_EXTENSION) received unexpected argument",
                           a.items[i]);
            return true;
        }
        replacement = a.items[i];
    }

    if (replace_mode && replacement.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(REPLACE_EXTENSION) requires replacement input",
                       nob_sv_from_cstr("Usage: cmake_path(REPLACE_EXTENSION <path-var> [LAST_ONLY] <input> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View value = eval_var_get(ctx, path_var);
    size_t sep = path_last_separator_index(value);
    String_View head = (sep == SIZE_MAX) ? nob_sv_from_cstr("") : nob_sv_from_parts(value.data, sep + 1);
    String_View name = (sep == SIZE_MAX) ? value : nob_sv_from_parts(value.data + sep + 1, value.count - sep - 1);

    ssize_t dot = cmk_path_dot_index(name, last_only);
    String_View stem = (dot < 0) ? name : nob_sv_from_parts(name.data, (size_t)dot);

    String_View out_name = stem;
    if (replace_mode) {
        if (replacement.count > 0 && replacement.data[0] != '.') {
            char *buf = (char*)arena_alloc(eval_temp_arena(ctx), replacement.count + 2);
            EVAL_OOM_RETURN_IF_NULL(ctx, buf, true);
            buf[0] = '.';
            memcpy(buf + 1, replacement.data, replacement.count);
            buf[replacement.count + 1] = '\0';
            replacement = nob_sv_from_cstr(buf);
        }
        String_View parts[2] = {stem, replacement};
        out_name = svu_join_no_sep_temp(ctx, parts, 2);
    }

    String_View out = out_name;
    if (head.count > 0) {
        String_View parts[2] = {head, out_name};
        out = svu_join_no_sep_temp(ctx, parts, 2);
    }

    (void)cmk_path_set_result(ctx, path_var, out_var, out);
    return true;
}

static bool handle_normal_path(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 2) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(NORMAL_PATH) requires <path-var>",
                       nob_sv_from_cstr("Usage: cmake_path(NORMAL_PATH <path-var> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a.items[1];
    String_View out_var = nob_sv_from_cstr("");

    if (a.count > 2) {
        if (a.count == 4 && eval_sv_eq_ci_lit(a.items[2], "OUTPUT_VARIABLE")) {
            out_var = a.items[3];
        } else {
            cmk_path_error(ctx, node, o,
                           "cmake_path(NORMAL_PATH) received unexpected argument",
                           a.items[2]);
            return true;
        }
    }

    String_View value = eval_var_get(ctx, path_var);
    (void)cmk_path_set_result(ctx, path_var, out_var, cmk_path_normalize_temp(ctx, value));
    return true;
}

static bool handle_relative_path(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 2) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(RELATIVE_PATH) requires <path-var>",
                       nob_sv_from_cstr("Usage: cmake_path(RELATIVE_PATH <path-var> [BASE_DIRECTORY <dir>] [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a.items[1];
    String_View out_var = nob_sv_from_cstr("");
    String_View base_dir = cmk_path_current_source_dir(ctx);

    for (size_t i = 2; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "BASE_DIRECTORY")) {
            if (i + 1 >= a.count) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(RELATIVE_PATH) BASE_DIRECTORY requires a value",
                               a.items[i]);
                return true;
            }
            base_dir = a.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= a.count) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(RELATIVE_PATH) OUTPUT_VARIABLE requires a variable name",
                               a.items[i]);
                return true;
            }
            out_var = a.items[++i];
            continue;
        }

        cmk_path_error(ctx, node, o,
                       "cmake_path(RELATIVE_PATH) received unexpected argument",
                       a.items[i]);
        return true;
    }

    String_View value = eval_var_get(ctx, path_var);
    String_View abs_value = cmk_path_make_absolute_temp(ctx, value, base_dir);
    String_View abs_base = cmk_path_make_absolute_temp(ctx, base_dir, cmk_path_current_source_dir(ctx));
    String_View rel = cmk_path_relativize_temp(ctx, abs_value, abs_base);
    (void)cmk_path_set_result(ctx, path_var, out_var, rel);
    return true;
}

static bool handle_absolute_path(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 2) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(ABSOLUTE_PATH) requires <path-var>",
                       nob_sv_from_cstr("Usage: cmake_path(ABSOLUTE_PATH <path-var> [BASE_DIRECTORY <dir>] [NORMALIZE] [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a.items[1];
    String_View out_var = nob_sv_from_cstr("");
    String_View base_dir = cmk_path_current_source_dir(ctx);
    bool normalize = false;

    for (size_t i = 2; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "BASE_DIRECTORY")) {
            if (i + 1 >= a.count) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(ABSOLUTE_PATH) BASE_DIRECTORY requires a value",
                               a.items[i]);
                return true;
            }
            base_dir = a.items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "NORMALIZE")) {
            normalize = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= a.count) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(ABSOLUTE_PATH) OUTPUT_VARIABLE requires a variable name",
                               a.items[i]);
                return true;
            }
            out_var = a.items[++i];
            continue;
        }

        cmk_path_error(ctx, node, o,
                       "cmake_path(ABSOLUTE_PATH) received unexpected argument",
                       a.items[i]);
        return true;
    }

    String_View value = eval_var_get(ctx, path_var);
    String_View abs = cmk_path_make_absolute_temp(ctx, value, base_dir);
    if (normalize) abs = cmk_path_normalize_temp(ctx, abs);
    (void)cmk_path_set_result(ctx, path_var, out_var, abs);
    return true;
}

static bool handle_native_path(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(NATIVE_PATH) requires <path-var> and output variable",
                       nob_sv_from_cstr("Usage: cmake_path(NATIVE_PATH <path-var> [NORMALIZE] <out-var>)"));
        return true;
    }

    String_View path_var = a.items[1];
    bool normalize = false;
    String_View out_var = nob_sv_from_cstr("");

    for (size_t i = 2; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "NORMALIZE")) {
            normalize = true;
            continue;
        }
        if (out_var.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(NATIVE_PATH) received unexpected argument",
                           a.items[i]);
            return true;
        }
        out_var = a.items[i];
    }

    if (out_var.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(NATIVE_PATH) requires output variable",
                       nob_sv_from_cstr("Usage: cmake_path(NATIVE_PATH <path-var> [NORMALIZE] <out-var>)"));
        return true;
    }

    String_View value = eval_var_get(ctx, path_var);
    if (normalize) value = cmk_path_normalize_temp(ctx, value);
    value = cmk_path_to_native_seps_temp(ctx, value);
    (void)eval_var_set(ctx, out_var, value);
    return true;
}

static bool handle_convert(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count < 4) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(CONVERT) requires input, mode and output variable",
                       nob_sv_from_cstr("Usage: cmake_path(CONVERT <input> TO_CMAKE_PATH_LIST|TO_NATIVE_PATH_LIST <out-var> [NORMALIZE])"));
        return true;
    }

    String_View input = a.items[1];
    String_View mode = a.items[2];
    bool normalize = false;
    String_View out_var = nob_sv_from_cstr("");

    for (size_t i = 3; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "NORMALIZE")) {
            normalize = true;
            continue;
        }
        if (out_var.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(CONVERT) received unexpected argument",
                           a.items[i]);
            return true;
        }
        out_var = a.items[i];
    }

    if (out_var.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(CONVERT) requires output variable",
                       nob_sv_from_cstr("Usage: cmake_path(CONVERT <input> TO_CMAKE_PATH_LIST|TO_NATIVE_PATH_LIST <out-var> [NORMALIZE])"));
        return true;
    }

    if (eval_sv_eq_ci_lit(mode, "TO_CMAKE_PATH_LIST")) {
#if defined(_WIN32)
        const char native_list_sep = ';';
#else
        const char native_list_sep = ':';
#endif
        SV_List parts = {0};
        if (!cmk_path_split_char_list_temp(ctx, input, native_list_sep, &parts)) return !eval_should_stop(ctx);

        SV_List converted = {0};
        for (size_t i = 0; i < parts.count; i++) {
            String_View p = cmk_path_to_cmake_seps_temp(ctx, parts.items[i]);
            if (normalize) p = cmk_path_normalize_temp(ctx, p);
            if (!svu_list_push_temp(ctx, &converted, p)) return !eval_should_stop(ctx);
        }
        String_View out = eval_sv_join_semi_temp(ctx, converted.items, converted.count);
        (void)eval_var_set(ctx, out_var, out);
        return true;
    }

    if (eval_sv_eq_ci_lit(mode, "TO_NATIVE_PATH_LIST")) {
        SV_List parts = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), input, &parts)) {
            return ctx_oom(ctx);
        }

#if defined(_WIN32)
        const char native_list_sep = ';';
#else
        const char native_list_sep = ':';
#endif

        SV_List converted = {0};
        for (size_t i = 0; i < parts.count; i++) {
            String_View p = parts.items[i];
            if (normalize) p = cmk_path_normalize_temp(ctx, p);
            p = cmk_path_to_native_seps_temp(ctx, p);
            if (!svu_list_push_temp(ctx, &converted, p)) return !eval_should_stop(ctx);
        }

        String_View out = cmk_path_join_char_list_temp(ctx, converted, native_list_sep);
        (void)eval_var_set(ctx, out_var, out);
        return true;
    }

    cmk_path_error(ctx, node, o,
                   "cmake_path(CONVERT) unsupported conversion mode",
                   mode);
    return true;
}

static bool handle_compare(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (a.count != 5) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(COMPARE) requires exactly 4 arguments",
                       nob_sv_from_cstr("Usage: cmake_path(COMPARE <input1> EQUAL|NOT_EQUAL <input2> <out-var>)"));
        return true;
    }

    String_View lhs = cmk_path_compare_canonical_temp(ctx, a.items[1]);
    String_View op = a.items[2];
    String_View rhs = cmk_path_compare_canonical_temp(ctx, a.items[3]);
    String_View out_var = a.items[4];

    bool eq = lhs.count == rhs.count && (lhs.count == 0 || memcmp(lhs.data, rhs.data, lhs.count) == 0);
    bool result = false;
    if (eval_sv_eq_ci_lit(op, "EQUAL")) result = eq;
    else if (eval_sv_eq_ci_lit(op, "NOT_EQUAL")) result = !eq;
    else {
        cmk_path_error(ctx, node, o,
                       "cmake_path(COMPARE) unsupported operator",
                       op);
        return true;
    }

    (void)eval_var_set(ctx, out_var, result ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
    return true;
}

static bool handle_has_component(Evaluator_Context *ctx,
                                 const Node *node,
                                 Cmake_Event_Origin o,
                                 SV_List a,
                                 String_View mode) {
    if (a.count != 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(HAS_*) requires <path-var> and <out-var>",
                       nob_sv_from_cstr("Usage: cmake_path(HAS_<component> <path-var> <out-var>)"));
        return true;
    }

    String_View path_var = a.items[1];
    String_View out_var = a.items[2];
    String_View value = eval_var_get(ctx, path_var);
    String_View component = nob_sv_from_parts(mode.data + 4, mode.count - 4);

    bool ok = false;
    String_View comp = cmk_path_component_get_temp(ctx, value, component, false, &ok);
    if (!ok) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(HAS_*) unsupported component",
                       component);
        return true;
    }

    bool has = comp.count > 0;
    (void)eval_var_set(ctx, out_var, has ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
    return true;
}

static bool handle_is_absolute(Evaluator_Context *ctx,
                               const Node *node,
                               Cmake_Event_Origin o,
                               SV_List a,
                               String_View mode) {
    if (a.count != 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(IS_*) requires <path-var> and <out-var>",
                       nob_sv_from_cstr("Usage: cmake_path(IS_ABSOLUTE <path-var> <out-var>)"));
        return true;
    }

    String_View value = eval_var_get(ctx, a.items[1]);
    bool result = false;
    if (eval_sv_eq_ci_lit(mode, "IS_ABSOLUTE")) {
        result = eval_sv_is_abs_path(value);
    } else {
        cmk_path_error(ctx, node, o,
                       "cmake_path() unsupported IS_* mode",
                       mode);
        return true;
    }

    (void)eval_var_set(ctx, a.items[2], result ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
    return true;
}

bool eval_handle_cmake_path(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        cmk_path_error(ctx, node, o,
                       "cmake_path() requires a subcommand",
                       nob_sv_from_cstr("Usage: cmake_path(<mode> ...)"));
        return true;
    }

    String_View mode = a.items[0];

    if (eval_sv_eq_ci_lit(mode, "SET")) return handle_set(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "GET")) return handle_get(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "APPEND")) return handle_append_like(ctx, node, o, a, false);
    if (eval_sv_eq_ci_lit(mode, "APPEND_STRING")) return handle_append_like(ctx, node, o, a, true);
    if (eval_sv_eq_ci_lit(mode, "REMOVE_FILENAME")) return handle_remove_filename(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "REPLACE_FILENAME")) return handle_replace_filename(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "REMOVE_EXTENSION")) return handle_extension_common(ctx, node, o, a, false);
    if (eval_sv_eq_ci_lit(mode, "REPLACE_EXTENSION")) return handle_extension_common(ctx, node, o, a, true);
    if (eval_sv_eq_ci_lit(mode, "NORMAL_PATH")) return handle_normal_path(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "RELATIVE_PATH")) return handle_relative_path(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "ABSOLUTE_PATH")) return handle_absolute_path(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "NATIVE_PATH")) return handle_native_path(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "CONVERT")) return handle_convert(ctx, node, o, a);
    if (eval_sv_eq_ci_lit(mode, "COMPARE")) return handle_compare(ctx, node, o, a);

    if (svu_has_prefix_ci_lit(mode, "HAS_")) return handle_has_component(ctx, node, o, a, mode);
    if (svu_has_prefix_ci_lit(mode, "IS_")) return handle_is_absolute(ctx, node, o, a, mode);

    cmk_path_error(ctx, node, o,
                   "cmake_path() subcommand is not implemented",
                   mode);
    return true;
}
