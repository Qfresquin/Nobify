#include "eval_dispatcher.h"
#include "evaluator_internal.h"
#include "eval_file.h"
#include "eval_stdlib.h"
#include "eval_vars.h"
#include "eval_diag.h"
#include "eval_flow.h"
#include "eval_expr.h"
#include "eval_opt_parser.h"
#include "eval_cpack.h"
#include "eval_package.h"
#include "eval_try_compile.h"
#include "arena_dyn.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool emit_dir_push_event(Evaluator_Context *ctx,
                                Cmake_Event_Origin origin,
                                String_View source_dir,
                                String_View binary_dir) {
    Cmake_Event ev = {0};
    ev.kind = EV_DIR_PUSH;
    ev.origin = origin;
    ev.as.dir_push.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.dir_push.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    return emit_event(ctx, ev);
}

static bool emit_dir_pop_event(Evaluator_Context *ctx, Cmake_Event_Origin origin) {
    Cmake_Event ev = {0};
    ev.kind = EV_DIR_POP;
    ev.origin = origin;
    return emit_event(ctx, ev);
}

static String_View sv_concat_suffix_temp(Evaluator_Context *ctx, String_View base, const char *suffix) {
    if (!ctx || !suffix) return nob_sv_from_cstr("");
    size_t suffix_len = strlen(suffix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), base.count + suffix_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (base.count) memcpy(buf, base.data, base.count);
    if (suffix_len) memcpy(buf + base.count, suffix, suffix_len);
    buf[base.count + suffix_len] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool append_list_var(Evaluator_Context *ctx, String_View var, String_View item) {
    String_View current = eval_var_get(ctx, var);
    if (current.count == 0) {
        return eval_var_set(ctx, var, item);
    }

    size_t total = current.count + 1 + item.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    memcpy(buf, current.data, current.count);
    buf[current.count] = ';';
    memcpy(buf + current.count + 1, item.data, item.count);
    buf[total] = '\0';
    return eval_var_set(ctx, var, nob_sv_from_cstr(buf));
}

static bool emit_items_from_list(Evaluator_Context *ctx,
                                 Cmake_Event_Origin o,
                                 String_View target_name,
                                 String_View list_text,
                                 Cmake_Event_Kind kind) {
    const char *p = list_text.data;
    const char *end = list_text.data + list_text.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View item = nob_sv_from_parts(p, (size_t)(q - p));
        if (item.count > 0) {
            Cmake_Event ev = {0};
            ev.kind = kind;
            ev.origin = o;
            if (kind == EV_TARGET_COMPILE_DEFINITIONS) {
                ev.as.target_compile_definitions.target_name = sv_copy_to_event_arena(ctx, target_name);
                ev.as.target_compile_definitions.visibility = EV_VISIBILITY_UNSPECIFIED;
                ev.as.target_compile_definitions.item = sv_copy_to_event_arena(ctx, item);
            } else if (kind == EV_TARGET_COMPILE_OPTIONS) {
                ev.as.target_compile_options.target_name = sv_copy_to_event_arena(ctx, target_name);
                ev.as.target_compile_options.visibility = EV_VISIBILITY_UNSPECIFIED;
                ev.as.target_compile_options.item = sv_copy_to_event_arena(ctx, item);
            } else {
                return false;
            }
            if (!emit_event(ctx, ev)) return false;
        }
        if (q >= end) break;
        p = q + 1;
    }
    return true;
}

static bool apply_global_compile_state_to_target(Evaluator_Context *ctx,
                                                 Cmake_Event_Origin o,
                                                 String_View target_name) {
    String_View defs = eval_var_get(ctx, nob_sv_from_cstr(k_global_defs_var));
    String_View opts = eval_var_get(ctx, nob_sv_from_cstr(k_global_opts_var));
    if (!emit_items_from_list(ctx, o, target_name, defs, EV_TARGET_COMPILE_DEFINITIONS)) return false;
    if (!emit_items_from_list(ctx, o, target_name, opts, EV_TARGET_COMPILE_OPTIONS)) return false;
    return true;
}

static bool sv_list_push_temp(Evaluator_Context *ctx, SV_List *list, String_View sv) {
    if (!ctx || !list) return false;
    if (!arena_da_reserve(eval_temp_arena(ctx), (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) {
        return ctx_oom(ctx);
    }
    list->items[list->count++] = sv;
    return true;
}

static String_View sv_join_no_sep_temp(Evaluator_Context *ctx, const String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += items[i].count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].count == 0) continue;
        memcpy(buf + off, items[i].data, items[i].count);
        off += items[i].count;
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View sv_join_space_temp(Evaluator_Context *ctx, const String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        total += items[i].count;
        if (i + 1 < count) total += 1;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].count > 0) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
        if (i + 1 < count) {
            buf[off++] = ' ';
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool emit_target_prop_set(Evaluator_Context *ctx,
                                 Cmake_Event_Origin o,
                                 String_View target_name,
                                 String_View key,
                                 String_View value,
                                 Cmake_Target_Property_Op op) {
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_PROP_SET;
    ev.origin = o;
    ev.as.target_prop_set.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_prop_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.target_prop_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.target_prop_set.op = op;
    return emit_event(ctx, ev);
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

static bool is_cmp_policy_id(String_View sv) {
    if (sv.count != 7) return false;
    if (!sv_has_prefix_ci_lit(sv, "CMP")) return false;
    for (size_t i = 3; i < 7; i++) {
        if (!isdigit((unsigned char)sv.data[i])) return false;
    }
    return true;
}

static String_View policy_key_from_id(Evaluator_Context *ctx, String_View policy_id) {
    return sv_concat_suffix_temp(ctx, nob_sv_from_cstr("CMAKE_POLICY_"), eval_sv_to_cstr_temp(ctx, policy_id));
}

static void emit_policy_partial_warning_once(Evaluator_Context *ctx,
                                             Cmake_Event_Origin o,
                                             String_View command_name) {
    String_View warned_key = nob_sv_from_cstr("NOBIFY_POLICY_PARTIAL_WARNED");
    if (eval_var_defined(ctx, warned_key)) return;
    (void)eval_var_set(ctx, warned_key, nob_sv_from_cstr("1"));
    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   command_name,
                   o,
                   nob_sv_from_cstr("Policy semantics are only partially modeled in evaluator v2"),
                   nob_sv_from_cstr("Commands parse and store basic policy state, but policy-driven behavior changes are not fully applied"));
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

static String_View sv_join_and_temp(Evaluator_Context *ctx, const String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        total += items[i].count;
        if (i + 1 < count) total += 4;
    }
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].count > 0) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
        if (i + 1 < count) {
            memcpy(buf + off, " && ", 4);
            off += 4;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

enum {
    CUSTOM_TARGET_OPT_DEPENDS = 1,
    CUSTOM_TARGET_OPT_BYPRODUCTS,
    CUSTOM_TARGET_OPT_SOURCES,
    CUSTOM_TARGET_OPT_WORKING_DIRECTORY,
    CUSTOM_TARGET_OPT_COMMENT,
    CUSTOM_TARGET_OPT_VERBATIM,
    CUSTOM_TARGET_OPT_USES_TERMINAL,
    CUSTOM_TARGET_OPT_COMMAND_EXPAND_LISTS,
    CUSTOM_TARGET_OPT_COMMAND,
};

typedef struct {
    String_View working_dir;
    String_View comment;
    bool verbatim;
    bool uses_terminal;
    bool command_expand_lists;
    SV_List depends;
    SV_List byproducts;
    SV_List commands;
    SV_List sources;
} Add_Custom_Target_Opts;

static bool add_custom_target_on_option(Evaluator_Context *ctx,
                                        void *userdata,
                                        int id,
                                        SV_List values,
                                        size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Custom_Target_Opts *st = (Add_Custom_Target_Opts*)userdata;
    switch (id) {
    case CUSTOM_TARGET_OPT_DEPENDS:
        for (size_t i = 0; i < values.count; i++) {
            if (!sv_list_push_temp(ctx, &st->depends, values.items[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_BYPRODUCTS:
        for (size_t i = 0; i < values.count; i++) {
            if (!sv_list_push_temp(ctx, &st->byproducts, values.items[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_SOURCES:
        for (size_t i = 0; i < values.count; i++) {
            if (!sv_list_push_temp(ctx, &st->sources, values.items[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_WORKING_DIRECTORY:
        if (values.count > 0) st->working_dir = values.items[0];
        return true;
    case CUSTOM_TARGET_OPT_COMMENT:
        if (values.count > 0) st->comment = values.items[0];
        return true;
    case CUSTOM_TARGET_OPT_VERBATIM:
        st->verbatim = true;
        return true;
    case CUSTOM_TARGET_OPT_USES_TERMINAL:
        st->uses_terminal = true;
        return true;
    case CUSTOM_TARGET_OPT_COMMAND_EXPAND_LISTS:
        st->command_expand_lists = true;
        return true;
    case CUSTOM_TARGET_OPT_COMMAND: {
        size_t start = 0;
        if (values.count > 0 && eval_sv_eq_ci_lit(values.items[0], "ARGS")) start = 1;
        if (start < values.count) {
            String_View cmd = sv_join_space_temp(ctx, &values.items[start], values.count - start);
            if (!sv_list_push_temp(ctx, &st->commands, cmd)) return false;
        }
        return true;
    }
    default:
        return true;
    }
}

static bool add_custom_noop_positional(Evaluator_Context *ctx,
                                       void *userdata,
                                       String_View value,
                                       size_t token_index) {
    (void)ctx;
    (void)userdata;
    (void)value;
    (void)token_index;
    return true;
}

enum {
    CUSTOM_CMD_OPT_OUTPUT = 1,
    CUSTOM_CMD_OPT_PRE_BUILD,
    CUSTOM_CMD_OPT_PRE_LINK,
    CUSTOM_CMD_OPT_POST_BUILD,
    CUSTOM_CMD_OPT_COMMAND,
    CUSTOM_CMD_OPT_DEPENDS,
    CUSTOM_CMD_OPT_BYPRODUCTS,
    CUSTOM_CMD_OPT_MAIN_DEPENDENCY,
    CUSTOM_CMD_OPT_IMPLICIT_DEPENDS,
    CUSTOM_CMD_OPT_DEPFILE,
    CUSTOM_CMD_OPT_WORKING_DIRECTORY,
    CUSTOM_CMD_OPT_COMMENT,
    CUSTOM_CMD_OPT_APPEND,
    CUSTOM_CMD_OPT_VERBATIM,
    CUSTOM_CMD_OPT_USES_TERMINAL,
    CUSTOM_CMD_OPT_COMMAND_EXPAND_LISTS,
    CUSTOM_CMD_OPT_DEPENDS_EXPLICIT_ONLY,
    CUSTOM_CMD_OPT_CODEGEN,
    CUSTOM_CMD_OPT_JOB_POOL,
    CUSTOM_CMD_OPT_JOB_SERVER_AWARE,
};

typedef struct {
    bool pre_build;
    bool got_stage;
    bool append;
    bool verbatim;
    bool uses_terminal;
    bool command_expand_lists;
    bool depends_explicit_only;
    bool codegen;
    String_View working_dir;
    String_View comment;
    String_View main_dependency;
    String_View depfile;
    SV_List outputs;
    SV_List byproducts;
    SV_List depends;
    SV_List commands;
} Add_Custom_Command_Opts;

static bool add_custom_command_on_option(Evaluator_Context *ctx,
                                         void *userdata,
                                         int id,
                                         SV_List values,
                                         size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Custom_Command_Opts *st = (Add_Custom_Command_Opts*)userdata;
    switch (id) {
    case CUSTOM_CMD_OPT_OUTPUT:
        for (size_t i = 0; i < values.count; i++) {
            if (!sv_list_push_temp(ctx, &st->outputs, values.items[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_PRE_BUILD:
    case CUSTOM_CMD_OPT_PRE_LINK:
        st->got_stage = true;
        st->pre_build = true;
        return true;
    case CUSTOM_CMD_OPT_POST_BUILD:
        st->got_stage = true;
        st->pre_build = false;
        return true;
    case CUSTOM_CMD_OPT_COMMAND: {
        size_t start = 0;
        if (values.count > 0 && eval_sv_eq_ci_lit(values.items[0], "ARGS")) start = 1;
        if (start < values.count) {
            String_View cmd = sv_join_space_temp(ctx, &values.items[start], values.count - start);
            if (!sv_list_push_temp(ctx, &st->commands, cmd)) return false;
        }
        return true;
    }
    case CUSTOM_CMD_OPT_DEPENDS:
        for (size_t i = 0; i < values.count; i++) {
            if (!sv_list_push_temp(ctx, &st->depends, values.items[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_BYPRODUCTS:
        for (size_t i = 0; i < values.count; i++) {
            if (!sv_list_push_temp(ctx, &st->byproducts, values.items[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_MAIN_DEPENDENCY:
        if (values.count > 0) st->main_dependency = values.items[0];
        return true;
    case CUSTOM_CMD_OPT_IMPLICIT_DEPENDS:
        for (size_t i = 1; i < values.count; i += 2) {
            if (!sv_list_push_temp(ctx, &st->depends, values.items[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_DEPFILE:
        if (values.count > 0) st->depfile = values.items[0];
        return true;
    case CUSTOM_CMD_OPT_WORKING_DIRECTORY:
        if (values.count > 0) st->working_dir = values.items[0];
        return true;
    case CUSTOM_CMD_OPT_COMMENT:
        if (values.count > 0) st->comment = values.items[0];
        return true;
    case CUSTOM_CMD_OPT_APPEND:
        st->append = true;
        return true;
    case CUSTOM_CMD_OPT_VERBATIM:
        st->verbatim = true;
        return true;
    case CUSTOM_CMD_OPT_USES_TERMINAL:
        st->uses_terminal = true;
        return true;
    case CUSTOM_CMD_OPT_COMMAND_EXPAND_LISTS:
        st->command_expand_lists = true;
        return true;
    case CUSTOM_CMD_OPT_DEPENDS_EXPLICIT_ONLY:
        st->depends_explicit_only = true;
        return true;
    case CUSTOM_CMD_OPT_CODEGEN:
        st->codegen = true;
        return true;
    case CUSTOM_CMD_OPT_JOB_POOL:
    case CUSTOM_CMD_OPT_JOB_SERVER_AWARE:
        return true;
    default:
        return true;
    }
}

static bool h_cmake_minimum_required(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2 || !eval_sv_eq_ci_lit(a.items[0], "VERSION")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_minimum_required() expects VERSION"),
                       nob_sv_from_cstr("Usage: cmake_minimum_required(VERSION <min>[...<max>] [FATAL_ERROR])"));
        return !eval_should_stop(ctx);
    }

    String_View version = a.items[1];
    String_View min_version = version;
    String_View policy_version = version;
    for (size_t i = 0; i + 2 < version.count; i++) {
        if (version.data[i] == '.' && version.data[i + 1] == '.' && version.data[i + 2] == '.') {
            min_version = nob_sv_from_parts(version.data, i);
            policy_version = nob_sv_from_parts(version.data + i + 3, version.count - (i + 3));
            break;
        }
    }
    if (min_version.count == 0) min_version = version;
    if (policy_version.count == 0) policy_version = min_version;

    (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_MINIMUM_REQUIRED_VERSION"), min_version);
    (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), policy_version);

    emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
    return !eval_should_stop(ctx);
}

static bool h_cmake_policy(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_policy() missing subcommand"),
                       nob_sv_from_cstr("Expected one of: VERSION, SET, GET, PUSH, POP"));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "VERSION")) {
        if (a.count < 2) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(VERSION ...) missing version"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), a.items[1]);
        emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "SET")) {
        if (a.count < 3 || !is_cmp_policy_id(a.items[1])) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(SET ...) expects CMP<NNNN> and value"),
                           nob_sv_from_cstr("Usage: cmake_policy(SET CMP0077 NEW)"));
            return !eval_should_stop(ctx);
        }
        String_View value = a.items[2];
        if (!(eval_sv_eq_ci_lit(value, "OLD") || eval_sv_eq_ci_lit(value, "NEW"))) {
            eval_emit_diag(ctx,
                           EV_DIAG_WARNING,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(SET ...) supports only OLD/NEW in v2"),
                           value);
        }
        String_View key = policy_key_from_id(ctx, a.items[1]);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        (void)eval_var_set(ctx, key, value);
        emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "GET")) {
        if (a.count < 3 || !is_cmp_policy_id(a.items[1])) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("cmake_policy(GET ...) expects CMP<NNNN> and output variable"),
                           nob_sv_from_cstr("Usage: cmake_policy(GET CMP0077 out_var)"));
            return !eval_should_stop(ctx);
        }
        String_View key = policy_key_from_id(ctx, a.items[1]);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        String_View val = eval_var_get(ctx, key);
        (void)eval_var_set(ctx, a.items[2], val);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "PUSH") || eval_sv_eq_ci_lit(a.items[0], "POP")) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cmake_policy(PUSH/POP) is not implemented in evaluator v2"),
                       nob_sv_from_cstr("Policy stack semantics are currently ignored"));
        emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("Unknown cmake_policy() subcommand"),
                   a.items[0]);
    emit_policy_partial_warning_once(ctx, o, node->as.cmd.name);
    return !eval_should_stop(ctx);
}

static bool h_project(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("project() missing name"),
                       nob_sv_from_cstr("Usage: project(<name> [VERSION v] ...)"));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    String_View version = nob_sv_from_cstr("");
    String_View desc = nob_sv_from_cstr("");

    String_View langs_items[32];
    size_t langs_n = 0;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "VERSION") && i + 1 < a.count) {
            version = a.items[++i];
        } else if (eval_sv_eq_ci_lit(a.items[i], "DESCRIPTION") && i + 1 < a.count) {
            desc = a.items[++i];
        } else if (eval_sv_eq_ci_lit(a.items[i], "LANGUAGES")) {
            for (size_t j = i + 1; j < a.count && langs_n < 32; j++) {
                langs_items[langs_n++] = a.items[j];
            }
            break;
        }
    }

    String_View langs = eval_sv_join_semi_temp(ctx, langs_items, langs_n);

    String_View project_src_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (project_src_dir.count == 0) project_src_dir = ctx->source_dir;
    String_View project_bin_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (project_bin_dir.count == 0) project_bin_dir = ctx->binary_dir;

    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_NAME"), name);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_VERSION"), version);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_SOURCE_DIR"), project_src_dir);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_BINARY_DIR"), project_bin_dir);
    (void)eval_var_set(ctx, nob_sv_from_cstr("PROJECT_DESCRIPTION"), desc);

    String_View cmake_project_name = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_PROJECT_NAME"));
    if (cmake_project_name.count == 0) {
        (void)eval_var_set(ctx, nob_sv_from_cstr("CMAKE_PROJECT_NAME"), name);
    }

    String_View key_src = sv_concat_suffix_temp(ctx, name, "_SOURCE_DIR");
    String_View key_bin = sv_concat_suffix_temp(ctx, name, "_BINARY_DIR");
    String_View key_ver = sv_concat_suffix_temp(ctx, name, "_VERSION");
    (void)eval_var_set(ctx, key_src, project_src_dir);
    (void)eval_var_set(ctx, key_bin, project_bin_dir);
    (void)eval_var_set(ctx, key_ver, version);

    Cmake_Event ev = {0};
    ev.kind = EV_PROJECT_DECLARE;
    ev.origin = o;
    ev.as.project_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.project_declare.version = sv_copy_to_event_arena(ctx, version);
    ev.as.project_declare.description = sv_copy_to_event_arena(ctx, desc);
    ev.as.project_declare.languages = sv_copy_to_event_arena(ctx, langs);
    (void)emit_event(ctx, ev);
    return !eval_should_stop(ctx);
}

static bool h_add_executable(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    String_View name = a.items[0];
    (void)eval_target_register(ctx, name);

    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_DECLARE;
    ev.origin = o;
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.type = EV_TARGET_EXECUTABLE;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "WIN32") ||
            eval_sv_eq_ci_lit(a.items[i], "MACOSX_BUNDLE") ||
            eval_sv_eq_ci_lit(a.items[i], "EXCLUDE_FROM_ALL")) {
            continue;
        }

        Cmake_Event src_ev = {0};
        src_ev.kind = EV_TARGET_ADD_SOURCE;
        src_ev.origin = o;
        src_ev.as.target_add_source.target_name = ev.as.target_declare.name;
        src_ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, src_ev)) return !eval_should_stop(ctx);
    }

    (void)apply_global_compile_state_to_target(ctx, o, name);
    return !eval_should_stop(ctx);
}

static bool h_add_library(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    String_View name = a.items[0];
    (void)eval_target_register(ctx, name);

    Cmake_Target_Type ty = EV_TARGET_LIBRARY_UNKNOWN;
    size_t i = 1;
    if (i < a.count) {
        if (eval_sv_eq_ci_lit(a.items[i], "STATIC")) {
            ty = EV_TARGET_LIBRARY_STATIC;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "SHARED")) {
            ty = EV_TARGET_LIBRARY_SHARED;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "MODULE")) {
            ty = EV_TARGET_LIBRARY_MODULE;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            ty = EV_TARGET_LIBRARY_INTERFACE;
            i++;
        } else if (eval_sv_eq_ci_lit(a.items[i], "OBJECT")) {
            ty = EV_TARGET_LIBRARY_OBJECT;
            i++;
        }
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_DECLARE;
    ev.origin = o;
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.type = ty;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);

    for (; i < a.count; i++) {
        Cmake_Event src_ev = {0};
        src_ev.kind = EV_TARGET_ADD_SOURCE;
        src_ev.origin = o;
        src_ev.as.target_add_source.target_name = ev.as.target_declare.name;
        src_ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, src_ev)) return !eval_should_stop(ctx);
    }

    (void)apply_global_compile_state_to_target(ctx, o, name);
    return !eval_should_stop(ctx);
}

static bool h_add_custom_target(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_custom_target() missing target name"),
                       nob_sv_from_cstr("Usage: add_custom_target(<name> [ALL] [COMMAND ...])"));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    bool all = false;
    size_t parse_start = 1;
    if (parse_start < a.count && eval_sv_eq_ci_lit(a.items[parse_start], "ALL")) {
        all = true;
        parse_start++;
    }

    static const Eval_Opt_Spec k_custom_target_specs[] = {
        {CUSTOM_TARGET_OPT_DEPENDS, "DEPENDS", EVAL_OPT_MULTI},
        {CUSTOM_TARGET_OPT_BYPRODUCTS, "BYPRODUCTS", EVAL_OPT_MULTI},
        {CUSTOM_TARGET_OPT_SOURCES, "SOURCES", EVAL_OPT_MULTI},
        {CUSTOM_TARGET_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE},
        {CUSTOM_TARGET_OPT_COMMENT, "COMMENT", EVAL_OPT_SINGLE},
        {CUSTOM_TARGET_OPT_VERBATIM, "VERBATIM", EVAL_OPT_FLAG},
        {CUSTOM_TARGET_OPT_USES_TERMINAL, "USES_TERMINAL", EVAL_OPT_FLAG},
        {CUSTOM_TARGET_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG},
        {CUSTOM_TARGET_OPT_COMMAND, "COMMAND", EVAL_OPT_MULTI},
    };
    Add_Custom_Target_Opts opt = {0};
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             a,
                             parse_start,
                             k_custom_target_specs,
                             NOB_ARRAY_LEN(k_custom_target_specs),
                             cfg,
                             add_custom_target_on_option,
                             add_custom_noop_positional,
                             &opt)) {
        return !eval_should_stop(ctx);
    }

    (void)eval_target_register(ctx, name);

    Cmake_Event decl = {0};
    decl.kind = EV_TARGET_DECLARE;
    decl.origin = o;
    decl.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    decl.as.target_declare.type = EV_TARGET_LIBRARY_UNKNOWN;
    if (!emit_event(ctx, decl)) return !eval_should_stop(ctx);

    if (!emit_target_prop_set(ctx,
                              o,
                              name,
                              nob_sv_from_cstr("EXCLUDE_FROM_ALL"),
                              all ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"),
                              EV_PROP_SET)) {
        return !eval_should_stop(ctx);
    }

    for (size_t s = 0; s < opt.sources.count; s++) {
        Cmake_Event src_ev = {0};
        src_ev.kind = EV_TARGET_ADD_SOURCE;
        src_ev.origin = o;
        src_ev.as.target_add_source.target_name = sv_copy_to_event_arena(ctx, name);
        src_ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, opt.sources.items[s]);
        if (!emit_event(ctx, src_ev)) return !eval_should_stop(ctx);
    }

    for (size_t d = 0; d < opt.depends.count; d++) {
        Cmake_Event dep_ev = {0};
        dep_ev.kind = EV_TARGET_LINK_LIBRARIES;
        dep_ev.origin = o;
        dep_ev.as.target_link_libraries.target_name = sv_copy_to_event_arena(ctx, name);
        dep_ev.as.target_link_libraries.visibility = EV_VISIBILITY_PRIVATE;
        dep_ev.as.target_link_libraries.item = sv_copy_to_event_arena(ctx, opt.depends.items[d]);
        if (!emit_event(ctx, dep_ev)) return !eval_should_stop(ctx);
    }

    if (opt.commands.count > 0 || opt.byproducts.count > 0) {
        Cmake_Event cmd_ev = {0};
        cmd_ev.kind = EV_CUSTOM_COMMAND_TARGET;
        cmd_ev.origin = o;
        cmd_ev.as.custom_command_target.target_name = sv_copy_to_event_arena(ctx, name);
        cmd_ev.as.custom_command_target.pre_build = true;
        cmd_ev.as.custom_command_target.command = sv_copy_to_event_arena(ctx, sv_join_and_temp(ctx, opt.commands.items, opt.commands.count));
        cmd_ev.as.custom_command_target.working_dir = sv_copy_to_event_arena(ctx, opt.working_dir);
        cmd_ev.as.custom_command_target.comment = sv_copy_to_event_arena(ctx, opt.comment);
        cmd_ev.as.custom_command_target.outputs = sv_copy_to_event_arena(ctx, nob_sv_from_cstr(""));
        cmd_ev.as.custom_command_target.byproducts = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.byproducts.items, opt.byproducts.count));
        cmd_ev.as.custom_command_target.depends = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.depends.items, opt.depends.count));
        cmd_ev.as.custom_command_target.main_dependency = sv_copy_to_event_arena(ctx, nob_sv_from_cstr(""));
        cmd_ev.as.custom_command_target.depfile = sv_copy_to_event_arena(ctx, nob_sv_from_cstr(""));
        cmd_ev.as.custom_command_target.append = false;
        cmd_ev.as.custom_command_target.verbatim = opt.verbatim;
        cmd_ev.as.custom_command_target.uses_terminal = opt.uses_terminal;
        cmd_ev.as.custom_command_target.command_expand_lists = opt.command_expand_lists;
        cmd_ev.as.custom_command_target.depends_explicit_only = false;
        cmd_ev.as.custom_command_target.codegen = false;
        if (!emit_event(ctx, cmd_ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

static bool h_add_custom_command(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_custom_command() requires TARGET or OUTPUT signature"),
                       nob_sv_from_cstr("Usage: add_custom_command(TARGET <tgt> ... ) or add_custom_command(OUTPUT <files...> ...)"));
        return !eval_should_stop(ctx);
    }

    bool mode_target = eval_sv_eq_ci_lit(a.items[0], "TARGET");
    bool mode_output = eval_sv_eq_ci_lit(a.items[0], "OUTPUT");
    if (!mode_target && !mode_output) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Unsupported add_custom_command() signature"),
                       nob_sv_from_cstr("Use TARGET or OUTPUT signatures"));
        return !eval_should_stop(ctx);
    }

    String_View target_name = nob_sv_from_cstr("");
    size_t parse_start = 0;
    if (mode_target) {
        target_name = a.items[1];
        parse_start = 2;
    }

    static const Eval_Opt_Spec k_custom_command_specs[] = {
        {CUSTOM_CMD_OPT_OUTPUT, "OUTPUT", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_PRE_BUILD, "PRE_BUILD", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_PRE_LINK, "PRE_LINK", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_POST_BUILD, "POST_BUILD", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_COMMAND, "COMMAND", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_DEPENDS, "DEPENDS", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_BYPRODUCTS, "BYPRODUCTS", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_MAIN_DEPENDENCY, "MAIN_DEPENDENCY", EVAL_OPT_SINGLE},
        {CUSTOM_CMD_OPT_IMPLICIT_DEPENDS, "IMPLICIT_DEPENDS", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_DEPFILE, "DEPFILE", EVAL_OPT_SINGLE},
        {CUSTOM_CMD_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE},
        {CUSTOM_CMD_OPT_COMMENT, "COMMENT", EVAL_OPT_SINGLE},
        {CUSTOM_CMD_OPT_APPEND, "APPEND", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_VERBATIM, "VERBATIM", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_USES_TERMINAL, "USES_TERMINAL", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_DEPENDS_EXPLICIT_ONLY, "DEPENDS_EXPLICIT_ONLY", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_CODEGEN, "CODEGEN", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_JOB_POOL, "JOB_POOL", EVAL_OPT_OPTIONAL_SINGLE},
        {CUSTOM_CMD_OPT_JOB_SERVER_AWARE, "JOB_SERVER_AWARE", EVAL_OPT_OPTIONAL_SINGLE},
    };
    Add_Custom_Command_Opts opt = {0};
    opt.pre_build = true;
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             a,
                             parse_start,
                             k_custom_command_specs,
                             NOB_ARRAY_LEN(k_custom_command_specs),
                             cfg,
                             add_custom_command_on_option,
                             add_custom_noop_positional,
                             &opt)) {
        return !eval_should_stop(ctx);
    }

    if (mode_target && !opt.got_stage) opt.pre_build = true;

    if (opt.commands.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_custom_command() has no COMMAND entries"),
                       nob_sv_from_cstr("Command was ignored"));
        return !eval_should_stop(ctx);
    }
    if (mode_output && opt.outputs.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_custom_command(OUTPUT ...) requires at least one output"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    if (opt.main_dependency.count > 0) (void)sv_list_push_temp(ctx, &opt.depends, opt.main_dependency);
    if (opt.depfile.count > 0) (void)sv_list_push_temp(ctx, &opt.byproducts, opt.depfile);

    if (mode_target) {
        Cmake_Event ev = {0};
        ev.kind = EV_CUSTOM_COMMAND_TARGET;
        ev.origin = o;
        ev.as.custom_command_target.target_name = sv_copy_to_event_arena(ctx, target_name);
        ev.as.custom_command_target.pre_build = opt.pre_build;
        ev.as.custom_command_target.command = sv_copy_to_event_arena(ctx, sv_join_and_temp(ctx, opt.commands.items, opt.commands.count));
        ev.as.custom_command_target.working_dir = sv_copy_to_event_arena(ctx, opt.working_dir);
        ev.as.custom_command_target.comment = sv_copy_to_event_arena(ctx, opt.comment);
        ev.as.custom_command_target.outputs = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.outputs.items, opt.outputs.count));
        ev.as.custom_command_target.byproducts = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.byproducts.items, opt.byproducts.count));
        ev.as.custom_command_target.depends = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.depends.items, opt.depends.count));
        ev.as.custom_command_target.main_dependency = sv_copy_to_event_arena(ctx, opt.main_dependency);
        ev.as.custom_command_target.depfile = sv_copy_to_event_arena(ctx, opt.depfile);
        ev.as.custom_command_target.append = opt.append;
        ev.as.custom_command_target.verbatim = opt.verbatim;
        ev.as.custom_command_target.uses_terminal = opt.uses_terminal;
        ev.as.custom_command_target.command_expand_lists = opt.command_expand_lists;
        ev.as.custom_command_target.depends_explicit_only = opt.depends_explicit_only;
        ev.as.custom_command_target.codegen = opt.codegen;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    } else {
        Cmake_Event ev = {0};
        ev.kind = EV_CUSTOM_COMMAND_OUTPUT;
        ev.origin = o;
        ev.as.custom_command_output.command = sv_copy_to_event_arena(ctx, sv_join_and_temp(ctx, opt.commands.items, opt.commands.count));
        ev.as.custom_command_output.working_dir = sv_copy_to_event_arena(ctx, opt.working_dir);
        ev.as.custom_command_output.comment = sv_copy_to_event_arena(ctx, opt.comment);
        ev.as.custom_command_output.outputs = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.outputs.items, opt.outputs.count));
        ev.as.custom_command_output.byproducts = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.byproducts.items, opt.byproducts.count));
        ev.as.custom_command_output.depends = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.depends.items, opt.depends.count));
        ev.as.custom_command_output.main_dependency = sv_copy_to_event_arena(ctx, opt.main_dependency);
        ev.as.custom_command_output.depfile = sv_copy_to_event_arena(ctx, opt.depfile);
        ev.as.custom_command_output.append = opt.append;
        ev.as.custom_command_output.verbatim = opt.verbatim;
        ev.as.custom_command_output.uses_terminal = opt.uses_terminal;
        ev.as.custom_command_output.command_expand_lists = opt.command_expand_lists;
        ev.as.custom_command_output.depends_explicit_only = opt.depends_explicit_only;
        ev.as.custom_command_output.codegen = opt.codegen;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

// try_compile() foi movido para eval_try_compile.c (eval_handle_try_compile).

static bool h_include_guard(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    String_View mode = nob_sv_from_cstr("DIRECTORY");
    if (a.count > 0) {
        mode = a.items[0];
        if (!(eval_sv_eq_ci_lit(mode, "DIRECTORY") || eval_sv_eq_ci_lit(mode, "GLOBAL"))) {
            eval_emit_diag(ctx,
                           EV_DIAG_WARNING,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("include_guard() unsupported mode"),
                           mode);
            mode = nob_sv_from_cstr("DIRECTORY");
        }
    }

    String_View current_file = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_FILE"));
    if (current_file.count == 0 && ctx->current_file) {
        current_file = nob_sv_from_cstr(ctx->current_file);
    }
    String_View current_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"));

    String_View key = nob_sv_from_cstr("");
    if (eval_sv_eq_ci_lit(mode, "GLOBAL")) {
        String_View parts[2] = {nob_sv_from_cstr("NOBIFY_INCLUDE_GUARD_GLOBAL::"), current_file};
        key = sv_join_no_sep_temp(ctx, parts, 2);
    } else {
        String_View parts[4] = {
            nob_sv_from_cstr("NOBIFY_INCLUDE_GUARD_DIR::"),
            current_dir,
            nob_sv_from_cstr("::"),
            current_file
        };
        key = sv_join_no_sep_temp(ctx, parts, 4);
    }
    if (ctx->oom) return !eval_should_stop(ctx);

    if (eval_var_defined(ctx, key)) {
        ctx->return_requested = true;
        return !eval_should_stop(ctx);
    }
    (void)eval_var_set(ctx, key, nob_sv_from_cstr("1"));
    return !eval_should_stop(ctx);
}

static bool h_cmake_path(Evaluator_Context *ctx, const Node *node) {
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

static bool h_target_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 2) return !eval_should_stop(ctx);

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_LIBRARIES;
        ev.origin = o;
        ev.as.target_link_libraries.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_libraries.visibility = vis;
        ev.as.target_link_libraries.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

static bool h_target_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_link_options() requires target and items"),
                       nob_sv_from_cstr("Usage: target_link_options(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_OPTIONS;
        ev.origin = o;
        ev.as.target_link_options.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_options.visibility = vis;
        ev.as.target_link_options.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

static bool h_target_link_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_link_directories() requires target and items"),
                       nob_sv_from_cstr("Usage: target_link_directories(<tgt> <PUBLIC|PRIVATE|INTERFACE> <dirs...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_LINK_DIRECTORIES;
        ev.origin = o;
        ev.as.target_link_directories.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_link_directories.visibility = vis;
        ev.as.target_link_directories.path = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

static bool h_target_include_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_include_directories() requires target and items"),
                       nob_sv_from_cstr("Usage: target_include_directories(<tgt> [SYSTEM] [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_system = false;
    bool is_before = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "SYSTEM")) {
            is_system = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "AFTER")) {
            is_before = false;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_INCLUDE_DIRECTORIES;
        ev.origin = o;
        ev.as.target_include_directories.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_include_directories.visibility = vis;
        ev.as.target_include_directories.path = sv_copy_to_event_arena(ctx, a.items[i]);
        ev.as.target_include_directories.is_system = is_system;
        ev.as.target_include_directories.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

static bool h_target_compile_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_compile_definitions() requires target and items"),
                       nob_sv_from_cstr("Usage: target_compile_definitions(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_COMPILE_DEFINITIONS;
        ev.origin = o;
        ev.as.target_compile_definitions.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_compile_definitions.visibility = vis;
        ev.as.target_compile_definitions.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

static bool h_target_compile_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("target_compile_options() requires target and items"),
                       nob_sv_from_cstr("Usage: target_compile_options(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return !eval_should_stop(ctx);
    }

    String_View tgt = a.items[0];
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_TARGET_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.target_compile_options.target_name = sv_copy_to_event_arena(ctx, tgt);
        ev.as.target_compile_options.visibility = vis;
        ev.as.target_compile_options.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

static bool h_set_target_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() requires targets and PROPERTIES key/value pairs"),
                       nob_sv_from_cstr("Usage: set_target_properties(<t1> [<t2> ...] PROPERTIES <k1> <v1> ...)"));
        return !eval_should_stop(ctx);
    }

    size_t props_i = a.count;
    for (size_t i = 0; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PROPERTIES")) {
            props_i = i;
            break;
        }
    }

    if (props_i == 0 || props_i >= a.count - 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() missing PROPERTIES section"),
                       nob_sv_from_cstr("Expected: set_target_properties(<targets...> PROPERTIES <key> <value> ...)"));
        return !eval_should_stop(ctx);
    }

    size_t kv_start = props_i + 1;
    size_t kv_count = a.count - kv_start;
    if (kv_count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() missing property key/value"),
                       nob_sv_from_cstr("Provide at least one <key> <value> pair"));
        return !eval_should_stop(ctx);
    }

    if ((kv_count % 2) != 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_target_properties() has dangling property key without value"),
                       nob_sv_from_cstr("Ignoring the last unmatched key"));
    }

    for (size_t ti = 0; ti < props_i; ti++) {
        String_View tgt = a.items[ti];
        for (size_t i = kv_start; i + 1 < a.count; i += 2) {
            if (!emit_target_prop_set(ctx, o, tgt, a.items[i], a.items[i + 1], EV_PROP_SET)) {
                return !eval_should_stop(ctx);
            }
        }
    }

    return !eval_should_stop(ctx);
}

static bool h_set_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() missing scope"),
                       nob_sv_from_cstr("Usage: set_property(TARGET <t...> [APPEND|APPEND_STRING] PROPERTY <k> [v...])"));
        return !eval_should_stop(ctx);
    }

    if (!eval_sv_eq_ci_lit(a.items[0], "TARGET")) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() V1 supports only TARGET scope"),
                       nob_sv_from_cstr("GLOBAL/DIRECTORY/SOURCE/INSTALL/TEST/CACHE scopes are ignored in v2"));
        return !eval_should_stop(ctx);
    }

    bool append = false;
    bool append_string = false;
    SV_List targets = {0};

    size_t i = 1;
    for (; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "PROPERTY")) break;
        if (eval_sv_eq_ci_lit(a.items[i], "APPEND")) {
            append = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "APPEND_STRING")) {
            append_string = true;
            continue;
        }
        if (!sv_list_push_temp(ctx, &targets, a.items[i])) return !eval_should_stop(ctx);
    }

    if (targets.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(TARGET ...) requires at least one target"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    if (i >= a.count || !eval_sv_eq_ci_lit(a.items[i], "PROPERTY")) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(TARGET ...) missing PROPERTY keyword"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    i++;
    if (i >= a.count) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property(TARGET ...) missing property key"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View key = a.items[i++];
    String_View value = nob_sv_from_cstr("");
    if (i < a.count) {
        if (append_string) value = sv_join_no_sep_temp(ctx, &a.items[i], a.count - i);
        else value = eval_sv_join_semi_temp(ctx, &a.items[i], a.count - i);
    }

    Cmake_Target_Property_Op op = EV_PROP_SET;
    if (append_string) op = EV_PROP_APPEND_STRING;
    else if (append) op = EV_PROP_APPEND_LIST;

    if (append && append_string) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("set_property() received both APPEND and APPEND_STRING"),
                       nob_sv_from_cstr("Using APPEND_STRING behavior"));
    }

    for (size_t ti = 0; ti < targets.count; ti++) {
        if (!emit_target_prop_set(ctx, o, targets.items[ti], key, value, op)) {
            return !eval_should_stop(ctx);
        }
    }
    return !eval_should_stop(ctx);
}

static bool h_add_compile_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        if (!append_list_var(ctx, nob_sv_from_cstr(k_global_opts_var), a.items[i])) return false;

        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.global_compile_options.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return false;
    }
    return !eval_should_stop(ctx);
}

static bool h_add_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        String_View item = a.items[i];
        if (item.count == 0) continue;

        // Keep add_definitions() semantics close to CMake: treat arguments as raw flags.
        if (!append_list_var(ctx, nob_sv_from_cstr(k_global_opts_var), item)) return false;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_COMPILE_OPTIONS;
        ev.origin = o;
        ev.as.global_compile_options.item = sv_copy_to_event_arena(ctx, item);
        if (!emit_event(ctx, ev)) return false;
    }
    return !eval_should_stop(ctx);
}

static bool h_add_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        if (a.items[i].count == 0) continue;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_LINK_OPTIONS;
        ev.origin = o;
        ev.as.global_link_options.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

static bool h_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    for (size_t i = 0; i < a.count; i++) {
        if (a.items[i].count == 0) continue;
        Cmake_Event ev = {0};
        ev.kind = EV_GLOBAL_LINK_LIBRARIES;
        ev.origin = o;
        ev.as.global_link_libraries.item = sv_copy_to_event_arena(ctx, a.items[i]);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

static bool h_include_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    bool is_system = false;
    bool is_before = false;
    for (size_t i = 0; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "SYSTEM")) {
            is_system = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "AFTER")) {
            is_before = false;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_DIRECTORY_INCLUDE_DIRECTORIES;
        ev.origin = o;
        ev.as.directory_include_directories.path = sv_copy_to_event_arena(ctx, a.items[i]);
        ev.as.directory_include_directories.is_system = is_system;
        ev.as.directory_include_directories.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

static bool h_link_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    bool is_before = false;
    for (size_t i = 0; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "AFTER")) {
            is_before = false;
            continue;
        }

        Cmake_Event ev = {0};
        ev.kind = EV_DIRECTORY_LINK_DIRECTORIES;
        ev.origin = o;
        ev.as.directory_link_directories.path = sv_copy_to_event_arena(ctx, a.items[i]);
        ev.as.directory_link_directories.is_before = is_before;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

static bool h_enable_testing(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count > 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("enable_testing() does not expect arguments"),
                       nob_sv_from_cstr("Extra arguments are ignored"));
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TESTING_ENABLE;
    ev.origin = o;
    ev.as.testing_enable.enabled = true;
    (void)eval_var_set(ctx, nob_sv_from_cstr("BUILD_TESTING"), nob_sv_from_cstr("1"));
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

enum {
    ADD_TEST_OPT_WORKING_DIRECTORY = 1,
    ADD_TEST_OPT_COMMAND_EXPAND_LISTS,
};

typedef struct {
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View working_dir;
    bool command_expand_lists;
} Add_Test_Option_State;

static bool add_test_on_option(Evaluator_Context *ctx,
                               void *userdata,
                               int id,
                               SV_List values,
                               size_t token_index) {
    (void)token_index;
    (void)ctx;
    if (!userdata) return false;
    Add_Test_Option_State *st = (Add_Test_Option_State*)userdata;
    if (id == ADD_TEST_OPT_WORKING_DIRECTORY) {
        if (values.count > 0) st->working_dir = values.items[0];
        return true;
    }
    if (id == ADD_TEST_OPT_COMMAND_EXPAND_LISTS) {
        st->command_expand_lists = true;
        return true;
    }
    return true;
}

static bool add_test_on_positional(Evaluator_Context *ctx,
                                   void *userdata,
                                   String_View value,
                                   size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Test_Option_State *st = (Add_Test_Option_State*)userdata;
    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   st->command_name,
                   st->origin,
                   nob_sv_from_cstr("add_test() has unsupported/extra argument"),
                   value);
    return !eval_should_stop(ctx);
}

static bool h_add_test(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_test() requires at least test name and command"),
                       nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>) or add_test(<name> <cmd...>)"));
        return !eval_should_stop(ctx);
    }

    String_View name = nob_sv_from_cstr("");
    String_View command = nob_sv_from_cstr("");
    String_View working_dir = nob_sv_from_cstr("");
    bool command_expand_lists = false;

    if (eval_sv_eq_ci_lit(a.items[0], "NAME")) {
        if (a.count < 4) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) requires COMMAND clause"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>)"));
            return !eval_should_stop(ctx);
        }
        name = a.items[1];

        size_t cmd_i = 2;
        if (!eval_sv_eq_ci_lit(a.items[cmd_i], "COMMAND")) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) missing COMMAND"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>)"));
            return !eval_should_stop(ctx);
        }
        cmd_i++;
        size_t cmd_start = cmd_i;
        const Eval_Opt_Spec add_test_specs[] = {
            {ADD_TEST_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE},
            {ADD_TEST_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG},
        };
        size_t cmd_end = cmd_i;
        while (cmd_end < a.count &&
               !eval_opt_token_is_keyword(a.items[cmd_end], add_test_specs, NOB_ARRAY_LEN(add_test_specs))) {
            cmd_end++;
        }
        if (cmd_end <= cmd_start) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) has empty COMMAND"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        command = sv_join_space_temp(ctx, &a.items[cmd_start], cmd_end - cmd_start);

        Add_Test_Option_State st = {
            .origin = o,
            .command_name = node->as.cmd.name,
            .working_dir = working_dir,
            .command_expand_lists = command_expand_lists,
        };
        Eval_Opt_Parse_Config cfg = {
            .origin = o,
            .component = nob_sv_from_cstr("dispatcher"),
            .command = node->as.cmd.name,
            .unknown_as_positional = true,
            .warn_unknown = false,
        };
        if (!eval_opt_parse_walk(ctx,
                                 a,
                                 cmd_end,
                                 add_test_specs,
                                 NOB_ARRAY_LEN(add_test_specs),
                                 cfg,
                                 add_test_on_option,
                                 add_test_on_positional,
                                 &st)) {
            return !eval_should_stop(ctx);
        }
        working_dir = st.working_dir;
        command_expand_lists = st.command_expand_lists;
    } else {
        name = a.items[0];
        command = sv_join_space_temp(ctx, &a.items[1], a.count - 1);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TEST_ADD;
    ev.origin = o;
    ev.as.test_add.name = sv_copy_to_event_arena(ctx, name);
    ev.as.test_add.command = sv_copy_to_event_arena(ctx, command);
    ev.as.test_add.working_dir = sv_copy_to_event_arena(ctx, working_dir);
    ev.as.test_add.command_expand_lists = command_expand_lists;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

static bool h_install(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 4) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() requires rule type, items and DESTINATION"),
                       nob_sv_from_cstr("Usage: install(TARGETS|FILES|PROGRAMS|DIRECTORY <items...> DESTINATION <dir>)"));
        return !eval_should_stop(ctx);
    }

    Cmake_Install_Rule_Type rule_type = EV_INSTALL_RULE_TARGET;
    if (eval_sv_eq_ci_lit(a.items[0], "TARGETS")) rule_type = EV_INSTALL_RULE_TARGET;
    else if (eval_sv_eq_ci_lit(a.items[0], "FILES")) rule_type = EV_INSTALL_RULE_FILE;
    else if (eval_sv_eq_ci_lit(a.items[0], "PROGRAMS")) rule_type = EV_INSTALL_RULE_PROGRAM;
    else if (eval_sv_eq_ci_lit(a.items[0], "DIRECTORY")) rule_type = EV_INSTALL_RULE_DIRECTORY;
    else {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() unsupported rule type in v2"),
                       a.items[0]);
        return !eval_should_stop(ctx);
    }

    size_t dest_i = a.count;
    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "DESTINATION")) {
            dest_i = i;
            break;
        }
    }
    if (dest_i == a.count || dest_i + 1 >= a.count) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() missing DESTINATION"),
                       nob_sv_from_cstr("Usage: install(TARGETS|FILES|PROGRAMS|DIRECTORY <items...> DESTINATION <dir>)"));
        return !eval_should_stop(ctx);
    }

    if (dest_i <= 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("install() has no items before DESTINATION"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View destination = a.items[dest_i + 1];
    for (size_t i = 1; i < dest_i; i++) {
        Cmake_Event ev = {0};
        ev.kind = EV_INSTALL_ADD_RULE;
        ev.origin = o;
        ev.as.install_add_rule.rule_type = rule_type;
        ev.as.install_add_rule.item = sv_copy_to_event_arena(ctx, a.items[i]);
        ev.as.install_add_rule.destination = sv_copy_to_event_arena(ctx, destination);
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

static bool h_include(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    String_View file_path = a.items[0];
    bool optional = false;

    for (size_t i = 1; i < a.count; i++) {
        if (eval_sv_eq_ci_lit(a.items[i], "OPTIONAL")) {
            optional = true;
            break;
        }
    }

    if (!eval_sv_is_abs_path(file_path)) {
        String_View current_dir = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_DIR"));
        if (current_dir.count == 0) current_dir = ctx->source_dir;
        file_path = eval_sv_path_join(eval_temp_arena(ctx), current_dir, file_path);
    }

    String_View scope_source = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (scope_source.count == 0) scope_source = ctx->source_dir;
    String_View scope_binary = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (scope_binary.count == 0) scope_binary = ctx->source_dir;

    if (!emit_dir_push_event(ctx, o, scope_source, scope_binary)) return !eval_should_stop(ctx);
    bool success = eval_execute_file(ctx, file_path, false, nob_sv_from_cstr(""));
    if (!emit_dir_pop_event(ctx, o)) return !eval_should_stop(ctx);
    if (!success && !optional && !eval_should_stop(ctx)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("include() failed to read or evaluate file"),
                       file_path);
    }
    return !eval_should_stop(ctx);
}

static bool h_add_subdirectory(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_subdirectory() missing source_dir"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View source_dir = a.items[0];
    String_View binary_dir = nob_sv_from_cstr("");

    if (a.count >= 2 && !eval_sv_eq_ci_lit(a.items[1], "EXCLUDE_FROM_ALL")) {
        binary_dir = a.items[1];
    }

    if (!eval_sv_is_abs_path(source_dir)) {
        String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
        source_dir = eval_sv_path_join(eval_temp_arena(ctx), current_src, source_dir);
    }

    String_View full_path = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr("CMakeLists.txt"));

    if (binary_dir.count > 0 && !eval_sv_is_abs_path(binary_dir)) {
        String_View current_bin = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
        binary_dir = eval_sv_path_join(eval_temp_arena(ctx), current_bin, binary_dir);
    }

    String_View scope_binary = binary_dir.count > 0 ? binary_dir : source_dir;
    if (!emit_dir_push_event(ctx, o, source_dir, scope_binary)) return !eval_should_stop(ctx);
    bool success = eval_execute_file(ctx, full_path, true, binary_dir);
    if (!emit_dir_pop_event(ctx, o)) return !eval_should_stop(ctx);
    if (!success && !eval_should_stop(ctx)) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_subdirectory() failed to read or evaluate CMakeLists.txt"),
                       full_path);
    }
    return !eval_should_stop(ctx);
}

// find_package() foi movido para eval_package.c (eval_handle_find_package).

typedef bool (*Cmd_Handler)(Evaluator_Context *ctx, const Node *node);

typedef struct {
    const char *name;
    Cmd_Handler fn;
} Command_Entry;

static const Command_Entry DISPATCH[] = {
    {"add_link_options", h_add_link_options},
    {"add_compile_options", h_add_compile_options},
    {"add_custom_command", h_add_custom_command},
    {"add_custom_target", h_add_custom_target},
    {"add_definitions", h_add_definitions},
    {"add_test", h_add_test},
    {"add_executable", h_add_executable},
    {"add_library", h_add_library},
    {"add_subdirectory", h_add_subdirectory},
    {"break", h_break},
    {"cmake_minimum_required", h_cmake_minimum_required},
    {"cmake_policy", h_cmake_policy},
    {"cpack_add_component", eval_handle_cpack_add_component},
    {"cpack_add_component_group", eval_handle_cpack_add_component_group},
    {"cpack_add_install_type", eval_handle_cpack_add_install_type},
    {"continue", h_continue},
    {"cmake_path", h_cmake_path},
    {"enable_testing", h_enable_testing},
    {"file", h_file},
    {"find_package", eval_handle_find_package},
    {"include_guard", h_include_guard},
    {"include", h_include},
    {"include_directories", h_include_directories},
    {"install", h_install},
    {"list", h_list},
    {"link_directories", h_link_directories},
    {"link_libraries", h_link_libraries},
    {"math", h_math},
    {"message", h_message},
    {"project", h_project},
    {"return", h_return},
    {"set", h_set},
    {"set_property", h_set_property},
    {"set_target_properties", h_set_target_properties},
    {"string", h_string},
    {"target_compile_definitions", h_target_compile_definitions},
    {"target_compile_options", h_target_compile_options},
    {"target_include_directories", h_target_include_directories},
    {"target_link_directories", h_target_link_directories},
    {"target_link_libraries", h_target_link_libraries},
    {"target_link_options", h_target_link_options},
    {"try_compile", eval_handle_try_compile},
};
static const size_t DISPATCH_COUNT = sizeof(DISPATCH) / sizeof(DISPATCH[0]);

bool eval_dispatcher_is_known_command(String_View name) {
    for (size_t i = 0; i < DISPATCH_COUNT; i++) {
        if (eval_sv_eq_ci_lit(name, DISPATCH[i].name)) return true;
    }
    return false;
}

bool eval_dispatch_command(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node || node->kind != NODE_COMMAND) return false;

    for (size_t i = 0; i < DISPATCH_COUNT; i++) {
        if (eval_sv_eq_ci_lit(node->as.cmd.name, DISPATCH[i].name)) {
            if (!DISPATCH[i].fn(ctx, node)) return false;
            return !eval_should_stop(ctx);
        }
    }

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    User_Command *user = eval_user_cmd_find(ctx, node->as.cmd.name);
    if (user) {
        SV_List args = (user->kind == USER_CMD_MACRO)
            ? eval_resolve_args_literal(ctx, &node->as.cmd.args)
            : eval_resolve_args(ctx, &node->as.cmd.args);
        if (eval_should_stop(ctx)) return false;
        if (eval_user_cmd_invoke(ctx, node->as.cmd.name, &args, o)) {
            return true;
        }
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("Unknown command"),
                   nob_sv_from_cstr("Ignored during evaluation"));
    return true;
}

