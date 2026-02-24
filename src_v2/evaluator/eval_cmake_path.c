#include "eval_cmake_path.h"

#include "evaluator_internal.h"
#include "arena_dyn.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static bool sv_list_push_temp(Evaluator_Context *ctx, SV_List *list, String_View sv) {
    if (!ctx || !list) return false;
    if (!arena_da_reserve(eval_temp_arena(ctx), (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) {
        return ctx_oom(ctx);
    }
    list->items[list->count++] = sv;
    return true;
}

static bool sv_has_prefix_ci_lit(String_View sv, const char *lit) {
    if (!lit || sv.count == 0) return false;
    size_t n = strlen(lit);
    if (sv.count < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = (char)tolower((unsigned char)sv.data[i]);
        char b = (char)tolower((unsigned char)lit[i]);
        if (a != b) return false;
    }
    return true;
}
static bool sv_eq_ci_sv(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        char ca = (char)tolower((unsigned char)a.data[i]);
        char cb = (char)tolower((unsigned char)b.data[i]);
        if (ca != cb) return false;
    }
    return true;
}

static bool ch_is_sep(char c) {
    return c == '/' || c == '\\';
}

static size_t path_last_separator_index(String_View path) {
    for (size_t i = path.count; i > 0; i--) {
        if (ch_is_sep(path.data[i - 1])) return i - 1;
    }
    return SIZE_MAX;
}

static String_View cmk_path_root_name_sv(String_View path) {
    if (path.count >= 2 && isalpha((unsigned char)path.data[0]) && path.data[1] == ':') {
        return nob_sv_from_parts(path.data, 2);
    }
    return nob_sv_from_cstr("");
}

static String_View cmk_path_root_directory_sv(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    if (ch_is_sep(path.data[0])) return nob_sv_from_cstr("/");
    if (path.count >= 3 &&
        isalpha((unsigned char)path.data[0]) &&
        path.data[1] == ':' &&
        ch_is_sep(path.data[2])) {
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

static String_View cmk_path_basename_sv(String_View path) {
    size_t sep = path_last_separator_index(path);
    if (sep == SIZE_MAX) return path;
    return nob_sv_from_parts(path.data + sep + 1, path.count - sep - 1);
}

static String_View cmk_path_extension_sv(String_View name) {
    size_t dot = SIZE_MAX;
    for (size_t i = 0; i < name.count; i++) {
        if (name.data[i] == '.') dot = i;
    }
    if (dot == SIZE_MAX || dot == 0) return nob_sv_from_cstr("");
    return nob_sv_from_parts(name.data + dot, name.count - dot);
}

static String_View cmk_path_stem_sv(String_View name) {
    size_t dot = SIZE_MAX;
    for (size_t i = 0; i < name.count; i++) {
        if (name.data[i] == '.') dot = i;
    }
    if (dot == SIZE_MAX || dot == 0) return name;
    return nob_sv_from_parts(name.data, dot);
}

static String_View cmk_path_relative_part_temp(Evaluator_Context *ctx, String_View path) {
    String_View root = cmk_path_root_path_temp(ctx, path);
    if (root.count == 0) return path;
    if (path.count <= root.count) return nob_sv_from_cstr("");
    String_View rel = nob_sv_from_parts(path.data + root.count, path.count - root.count);
    while (rel.count > 0 && ch_is_sep(rel.data[0])) {
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
    if (!ctx) return nob_sv_from_cstr("");
    if (input.count == 0) return nob_sv_from_cstr(".");

    bool has_drive = input.count >= 2 &&
                     isalpha((unsigned char)input.data[0]) &&
                     input.data[1] == ':';
    bool absolute = false;
    size_t pos = 0;
    if (has_drive) {
        pos = 2;
        if (pos < input.count && ch_is_sep(input.data[pos])) {
            absolute = true;
            while (pos < input.count && ch_is_sep(input.data[pos])) pos++;
        }
    } else if (ch_is_sep(input.data[0])) {
        absolute = true;
        while (pos < input.count && ch_is_sep(input.data[pos])) pos++;
    }

    SV_List segments = {0};
    while (pos < input.count) {
        size_t start = pos;
        while (pos < input.count && !ch_is_sep(input.data[pos])) pos++;
        String_View seg = nob_sv_from_parts(input.data + start, pos - start);
        while (pos < input.count && ch_is_sep(input.data[pos])) pos++;

        if (seg.count == 0 || nob_sv_eq(seg, nob_sv_from_cstr("."))) continue;
        if (nob_sv_eq(seg, nob_sv_from_cstr(".."))) {
            if (segments.count > 0 && !nob_sv_eq(segments.items[segments.count - 1], nob_sv_from_cstr(".."))) {
                segments.count--;
            } else if (!absolute) {
                if (!sv_list_push_temp(ctx, &segments, seg)) return nob_sv_from_cstr("");
            }
            continue;
        }
        if (!sv_list_push_temp(ctx, &segments, seg)) return nob_sv_from_cstr("");
    }

    size_t total = 0;
    if (has_drive) total += 2;
    if (absolute) total += 1;
    for (size_t i = 0; i < segments.count; i++) {
        total += segments.items[i].count;
        if (i > 0) total += 1;
    }
    if (segments.count == 0) {
        if (has_drive && absolute) total += 1;
        else if (!has_drive && !absolute) total += 1;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;

    if (has_drive) {
        buf[off++] = input.data[0];
        buf[off++] = ':';
    }
    if (absolute) {
        buf[off++] = '/';
    }

    for (size_t i = 0; i < segments.count; i++) {
        if ((absolute || has_drive) && off > 0 && buf[off - 1] != '/') buf[off++] = '/';
        if (!(absolute || has_drive) && i > 0) buf[off++] = '/';
        memcpy(buf + off, segments.items[i].data, segments.items[i].count);
        off += segments.items[i].count;
    }

    if (segments.count == 0) {
        if (has_drive && absolute) {
            if (off == 2) buf[off++] = '/';
        } else if (!has_drive && !absolute) {
            buf[off++] = '.';
        }
    }

    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static void cmk_path_collect_segments_after_root(Evaluator_Context *ctx,
                                                 String_View path,
                                                 String_View root,
                                                 SV_List *out) {
    if (!ctx || !out) return;
    size_t pos = root.count;
    while (pos < path.count && ch_is_sep(path.data[pos])) pos++;
    while (pos < path.count) {
        size_t start = pos;
        while (pos < path.count && !ch_is_sep(path.data[pos])) pos++;
        if (pos > start) (void)sv_list_push_temp(ctx, out, nob_sv_from_parts(path.data + start, pos - start));
        while (pos < path.count && ch_is_sep(path.data[pos])) pos++;
    }
}

static String_View cmk_path_relativize_temp(Evaluator_Context *ctx, String_View path, String_View base_dir) {
    String_View a = cmk_path_normalize_temp(ctx, path);
    String_View b = cmk_path_normalize_temp(ctx, base_dir);
    if (ctx->oom) return nob_sv_from_cstr("");

    String_View root_a = cmk_path_root_path_temp(ctx, a);
    String_View root_b = cmk_path_root_path_temp(ctx, b);
    if (!sv_eq_ci_sv(root_a, root_b)) return a;

    SV_List seg_a = {0};
    SV_List seg_b = {0};
    cmk_path_collect_segments_after_root(ctx, a, root_a, &seg_a);
    cmk_path_collect_segments_after_root(ctx, b, root_b, &seg_b);
    if (ctx->oom) return nob_sv_from_cstr("");

    size_t common = 0;
    while (common < seg_a.count && common < seg_b.count && sv_eq_ci_sv(seg_a.items[common], seg_b.items[common])) {
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

static String_View cmk_path_get_component_temp(Evaluator_Context *ctx,
                                               String_View input,
                                               String_View component,
                                               bool *supported) {
    if (supported) *supported = true;
    if (eval_sv_eq_ci_lit(component, "ROOT_NAME")) return cmk_path_root_name_sv(input);
    if (eval_sv_eq_ci_lit(component, "ROOT_DIRECTORY")) return cmk_path_root_directory_sv(input);
    if (eval_sv_eq_ci_lit(component, "ROOT_PATH")) return cmk_path_root_path_temp(ctx, input);
    if (eval_sv_eq_ci_lit(component, "FILENAME")) return cmk_path_basename_sv(input);
    if (eval_sv_eq_ci_lit(component, "STEM")) return cmk_path_stem_sv(cmk_path_basename_sv(input));
    if (eval_sv_eq_ci_lit(component, "EXTENSION")) return cmk_path_extension_sv(cmk_path_basename_sv(input));
    if (eval_sv_eq_ci_lit(component, "RELATIVE_PART")) return cmk_path_relative_part_temp(ctx, input);
    if (eval_sv_eq_ci_lit(component, "PARENT_PATH")) return cmk_path_parent_part_sv(input);
    if (supported) *supported = false;
    return nob_sv_from_cstr("");
}

bool eval_handle_cmake_path(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) return !eval_should_stop(ctx);

    String_View mode = a.items[0];
    if (eval_sv_eq_ci_lit(mode, "SET") && a.count >= 2) {
        String_View out_var = a.items[1];
        bool normalize = false;
        String_View value = nob_sv_from_cstr("");
        for (size_t i = 2; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "NORMALIZE")) {
                normalize = true;
                continue;
            }
            value = a.items[i];
            break;
        }
        if (normalize) value = cmk_path_normalize_temp(ctx, value);
        (void)eval_var_set(ctx, out_var, value);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(mode, "GET") && a.count >= 4) {
        String_View input_var = a.items[1];
        String_View input = eval_var_get(ctx, input_var);
        if (input.count == 0) input = input_var;
        input = cmk_path_normalize_temp(ctx, input);
        String_View component = a.items[2];
        String_View out_var = a.items[3];
        bool supported = false;
        String_View result = cmk_path_get_component_temp(ctx, input, component, &supported);
        if (!supported) {
            eval_emit_diag(ctx,
                           EV_DIAG_WARNING,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_path(GET ...) unsupported component"),
                           component);
        }
        (void)eval_var_set(ctx, out_var, result);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(mode, "APPEND") && a.count >= 2) {
        String_View path_var = a.items[1];
        String_View current = eval_var_get(ctx, path_var);
        if (current.count == 0) current = path_var;

        bool normalize = false;
        String_View out_var = path_var;
        for (size_t i = 2; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "NORMALIZE")) {
                normalize = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "OUTPUT_VARIABLE") && i + 1 < a.count) {
                out_var = a.items[++i];
                continue;
            }
            current = eval_sv_path_join(eval_temp_arena(ctx), current, a.items[i]);
        }
        if (normalize) current = cmk_path_normalize_temp(ctx, current);
        (void)eval_var_set(ctx, out_var, current);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(mode, "NORMAL_PATH") && a.count >= 2) {
        String_View path_var = a.items[1];
        String_View value = eval_var_get(ctx, path_var);
        if (value.count == 0) value = path_var;
        String_View out_var = path_var;
        for (size_t i = 2; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "OUTPUT_VARIABLE") && i + 1 < a.count) {
                out_var = a.items[++i];
            }
        }
        (void)eval_var_set(ctx, out_var, cmk_path_normalize_temp(ctx, value));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(mode, "RELATIVE_PATH") && a.count >= 2) {
        String_View path_var = a.items[1];
        String_View value = eval_var_get(ctx, path_var);
        if (value.count == 0) value = path_var;
        String_View base_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"));
        if (base_dir.count == 0) base_dir = ctx->source_dir;
        String_View out_var = path_var;
        for (size_t i = 2; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "BASE_DIRECTORY") && i + 1 < a.count) {
                base_dir = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "OUTPUT_VARIABLE") && i + 1 < a.count) {
                out_var = a.items[++i];
            }
        }
        (void)eval_var_set(ctx, out_var, cmk_path_relativize_temp(ctx, value, base_dir));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(mode, "COMPARE") && a.count >= 5) {
        String_View lhs = cmk_path_normalize_temp(ctx, a.items[1]);
        String_View op = a.items[2];
        String_View rhs = cmk_path_normalize_temp(ctx, a.items[3]);
        String_View out_var = a.items[4];

        char *lhs_c = eval_sv_to_cstr_temp(ctx, lhs);
        char *rhs_c = eval_sv_to_cstr_temp(ctx, rhs);
        EVAL_OOM_RETURN_IF_NULL(ctx, lhs_c, !eval_should_stop(ctx));
        EVAL_OOM_RETURN_IF_NULL(ctx, rhs_c, !eval_should_stop(ctx));
        int cmp = strcmp(lhs_c, rhs_c);
        bool ok = false;
        if (eval_sv_eq_ci_lit(op, "EQUAL")) ok = cmp == 0;
        else if (eval_sv_eq_ci_lit(op, "NOT_EQUAL")) ok = cmp != 0;
        else if (eval_sv_eq_ci_lit(op, "LESS")) ok = cmp < 0;
        else if (eval_sv_eq_ci_lit(op, "LESS_EQUAL")) ok = cmp <= 0;
        else if (eval_sv_eq_ci_lit(op, "GREATER")) ok = cmp > 0;
        else if (eval_sv_eq_ci_lit(op, "GREATER_EQUAL")) ok = cmp >= 0;
        (void)eval_var_set(ctx, out_var, ok ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
        return !eval_should_stop(ctx);
    }

    if (sv_has_prefix_ci_lit(mode, "HAS_") && a.count >= 3) {
        String_View path_var = a.items[1];
        String_View value = eval_var_get(ctx, path_var);
        if (value.count == 0) value = path_var;
        String_View input = cmk_path_normalize_temp(ctx, value);
        String_View component = nob_sv_from_parts(mode.data + 4, mode.count - 4);
        String_View out_var = a.items[2];
        bool supported = false;
        String_View comp = cmk_path_get_component_temp(ctx, input, component, &supported);
        bool has = supported && comp.count > 0;
        (void)eval_var_set(ctx, out_var, has ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
        return !eval_should_stop(ctx);
    }

    if (sv_has_prefix_ci_lit(mode, "IS_") && a.count >= 3) {
        String_View path_var = a.items[1];
        String_View value = eval_var_get(ctx, path_var);
        if (value.count == 0) value = path_var;
        value = cmk_path_normalize_temp(ctx, value);
        String_View out_var = a.items[2];
        bool result = false;
        if (eval_sv_eq_ci_lit(mode, "IS_ABSOLUTE")) {
            result = eval_sv_is_abs_path(value);
        } else if (eval_sv_eq_ci_lit(mode, "IS_RELATIVE")) {
            result = !eval_sv_is_abs_path(value);
        }
        (void)eval_var_set(ctx, out_var, result ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("cmake_path() subcommand is not implemented in evaluator v2"),
                   nob_sv_from_cstr("Implemented: SET, GET, APPEND, NORMAL_PATH, RELATIVE_PATH, COMPARE, HAS_*, IS_*"));
    return !eval_should_stop(ctx);
}

