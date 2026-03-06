#include "eval_cmake_path_internal.h"

#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

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

String_View cmk_path_root_path_temp(Evaluator_Context *ctx, String_View path) {
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

String_View cmk_path_filename_sv(String_View path) {
    size_t sep = path_last_separator_index(path);
    if (sep == SIZE_MAX) return path;
    return nob_sv_from_parts(path.data + sep + 1, path.count - sep - 1);
}

static ptrdiff_t cmk_path_dot_index(String_View name, bool last_only) {
    ptrdiff_t dot = -1;
    if (name.count == 0) return -1;

    if (last_only) {
        for (size_t i = name.count; i > 0; i--) {
            if (name.data[i - 1] == '.') {
                dot = (ptrdiff_t)(i - 1);
                break;
            }
        }
    } else {
        for (size_t i = 0; i < name.count; i++) {
            if (name.data[i] == '.') {
                dot = (ptrdiff_t)i;
                break;
            }
        }
    }

    if (dot <= 0) return -1;
    return dot;
}

String_View cmk_path_extension_from_name_sv(String_View name, bool last_only) {
    ptrdiff_t dot = cmk_path_dot_index(name, last_only);
    if (dot < 0) return nob_sv_from_cstr("");
    return nob_sv_from_parts(name.data + dot, name.count - (size_t)dot);
}

String_View cmk_path_stem_from_name_sv(String_View name, bool last_only) {
    ptrdiff_t dot = cmk_path_dot_index(name, last_only);
    if (dot < 0) return name;
    return nob_sv_from_parts(name.data, (size_t)dot);
}

String_View cmk_path_relative_part_temp(Evaluator_Context *ctx, String_View path) {
    String_View root = cmk_path_root_path_temp(ctx, path);
    if (root.count == 0) return path;
    if (path.count <= root.count) return nob_sv_from_cstr("");
    String_View rel = nob_sv_from_parts(path.data + root.count, path.count - root.count);
    while (rel.count > 0 && svu_is_path_sep(rel.data[0])) {
        rel = nob_sv_from_parts(rel.data + 1, rel.count - 1);
    }
    return rel;
}

String_View cmk_path_parent_part_sv(String_View path) {
    size_t sep = path_last_separator_index(path);
    if (sep == SIZE_MAX) return nob_sv_from_cstr("");
    if (sep == 0) return nob_sv_from_cstr("/");
    if (sep == 2 && path.count >= 3 && path.data[1] == ':') {
        return nob_sv_from_parts(path.data, 3);
    }
    return nob_sv_from_parts(path.data, sep);
}

String_View cmk_path_normalize_temp(Evaluator_Context *ctx, String_View input) {
    return eval_sv_path_normalize_temp(ctx, input);
}

String_View cmk_path_current_source_dir(Evaluator_Context *ctx) {
    return eval_current_source_dir(ctx);
}

String_View cmk_path_make_absolute_temp(Evaluator_Context *ctx, String_View value, String_View base_dir) {
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

String_View cmk_path_relativize_temp(Evaluator_Context *ctx, String_View path, String_View base_dir) {
    String_View a = cmk_path_normalize_temp(ctx, path);
    String_View b = cmk_path_normalize_temp(ctx, base_dir);
    if (ctx->oom) return nob_sv_from_cstr("");

    String_View root_a = cmk_path_root_path_temp(ctx, a);
    String_View root_b = cmk_path_root_path_temp(ctx, b);
    if (!svu_eq_ci_sv(root_a, root_b)) return a;

    SV_List seg_a = NULL;
    SV_List seg_b = NULL;
    cmk_path_collect_segments_after_root(ctx, a, root_a, &seg_a);
    cmk_path_collect_segments_after_root(ctx, b, root_b, &seg_b);
    if (ctx->oom) return nob_sv_from_cstr("");

    size_t common = 0;
    while (common < arena_arr_len(seg_a) && common < arena_arr_len(seg_b) && svu_eq_ci_sv(seg_a[common], seg_b[common])) {
        common++;
    }

    size_t total = 0;
    size_t count = 0;
    for (size_t i = common; i < arena_arr_len(seg_b); i++) {
        total += 2;
        if (count > 0) total += 1;
        count++;
    }
    for (size_t i = common; i < arena_arr_len(seg_a); i++) {
        total += seg_a[i].count;
        if (count > 0) total += 1;
        count++;
    }
    if (count == 0) total = 1;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;
    size_t emitted = 0;
    for (size_t i = common; i < arena_arr_len(seg_b); i++) {
        if (emitted > 0) buf[off++] = '/';
        buf[off++] = '.';
        buf[off++] = '.';
        emitted++;
    }
    for (size_t i = common; i < arena_arr_len(seg_a); i++) {
        if (emitted > 0) buf[off++] = '/';
        memcpy(buf + off, seg_a[i].data, seg_a[i].count);
        off += seg_a[i].count;
        emitted++;
    }
    if (emitted == 0) buf[off++] = '.';
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View cmk_path_to_cmake_seps_temp(Evaluator_Context *ctx, String_View in) {
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) {
        buf[i] = svu_is_path_sep(in.data[i]) ? '/' : in.data[i];
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View cmk_path_to_native_seps_temp(Evaluator_Context *ctx, String_View in) {
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

bool cmk_path_split_char_list_temp(Evaluator_Context *ctx, String_View in, char sep, SV_List *out) {
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

String_View cmk_path_join_char_list_temp(Evaluator_Context *ctx, SV_List list, char sep) {
    if (!ctx) return nob_sv_from_cstr("");
    if (arena_arr_len(list) == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < arena_arr_len(list); i++) {
        total += list[i].count;
        if (i + 1 < arena_arr_len(list)) total += 1;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arena_arr_len(list); i++) {
        if (list[i].count > 0) {
            memcpy(buf + off, list[i].data, list[i].count);
            off += list[i].count;
        }
        if (i + 1 < arena_arr_len(list)) buf[off++] = sep;
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View cmk_path_component_get_temp(Evaluator_Context *ctx,
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

String_View cmk_path_remove_filename_temp(Evaluator_Context *ctx, String_View value) {
    (void)ctx;
    size_t sep = path_last_separator_index(value);
    if (sep == SIZE_MAX) return nob_sv_from_cstr("");
    if (sep == 0) return nob_sv_from_cstr("/");
    return nob_sv_from_parts(value.data, sep + 1);
}

String_View cmk_path_replace_filename_temp(Evaluator_Context *ctx, String_View value, String_View input) {
    if (!ctx) return input;
    size_t sep = path_last_separator_index(value);
    if (sep == SIZE_MAX) return input;

    String_View parent_with_sep = nob_sv_from_parts(value.data, sep + 1);
    String_View parts[2] = {parent_with_sep, input};
    return svu_join_no_sep_temp(ctx, parts, 2);
}

String_View cmk_path_transform_extension_temp(Evaluator_Context *ctx,
                                              String_View value,
                                              bool last_only,
                                              bool replace_mode,
                                              String_View replacement) {
    if (!ctx) return value;

    size_t sep = path_last_separator_index(value);
    String_View head = (sep == SIZE_MAX) ? nob_sv_from_cstr("") : nob_sv_from_parts(value.data, sep + 1);
    String_View name = (sep == SIZE_MAX) ? value : nob_sv_from_parts(value.data + sep + 1, value.count - sep - 1);

    ptrdiff_t dot = cmk_path_dot_index(name, last_only);
    String_View stem = (dot < 0) ? name : nob_sv_from_parts(name.data, (size_t)dot);

    String_View out_name = stem;
    if (replace_mode) {
        if (replacement.count > 0 && replacement.data[0] != '.') {
            char *buf = (char*)arena_alloc(eval_temp_arena(ctx), replacement.count + 2);
            EVAL_OOM_RETURN_IF_NULL(ctx, buf, value);
            buf[0] = '.';
            memcpy(buf + 1, replacement.data, replacement.count);
            buf[replacement.count + 1] = '\0';
            replacement = nob_sv_from_cstr(buf);
        }
        String_View parts[2] = {stem, replacement};
        out_name = svu_join_no_sep_temp(ctx, parts, 2);
    }

    if (head.count == 0) return out_name;
    String_View parts[2] = {head, out_name};
    return svu_join_no_sep_temp(ctx, parts, 2);
}

bool cmk_path_is_component_supports_last_only(String_View component) {
    return eval_sv_eq_ci_lit(component, "EXTENSION") || eval_sv_eq_ci_lit(component, "STEM");
}

String_View cmk_path_compare_canonical_temp(Evaluator_Context *ctx, String_View in) {
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
