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
#include "eval_custom.h"
#include "eval_directory.h"
#include "eval_install.h"
#include "eval_package.h"
#include "eval_target.h"
#include "eval_test.h"
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

// add_custom_*() foi movido para eval_custom.c (eval_handle_add_custom_target/eval_handle_add_custom_command).

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

// target_*()/set_target_properties()/set_property() foi movido para eval_target.c.

// add_*_options()/add_definitions()/link_libraries()/include_directories()/link_directories() foi movido para eval_directory.c.

// enable_testing()/add_test() foi movido para eval_test.c (eval_handle_enable_testing/eval_handle_add_test).

// install() foi movido para eval_install.c (eval_handle_install).

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
    {"add_link_options", eval_handle_add_link_options},
    {"add_compile_options", eval_handle_add_compile_options},
    {"add_custom_command", eval_handle_add_custom_command},
    {"add_custom_target", eval_handle_add_custom_target},
    {"add_definitions", eval_handle_add_definitions},
    {"add_test", eval_handle_add_test},
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
    {"enable_testing", eval_handle_enable_testing},
    {"file", h_file},
    {"find_package", eval_handle_find_package},
    {"include_guard", h_include_guard},
    {"include", h_include},
    {"include_directories", eval_handle_include_directories},
    {"install", eval_handle_install},
    {"list", h_list},
    {"link_directories", eval_handle_link_directories},
    {"link_libraries", eval_handle_link_libraries},
    {"math", h_math},
    {"message", h_message},
    {"project", h_project},
    {"return", h_return},
    {"set", h_set},
    {"set_property", eval_handle_set_property},
    {"set_target_properties", eval_handle_set_target_properties},
    {"string", h_string},
    {"target_compile_definitions", eval_handle_target_compile_definitions},
    {"target_compile_options", eval_handle_target_compile_options},
    {"target_include_directories", eval_handle_target_include_directories},
    {"target_link_directories", eval_handle_target_link_directories},
    {"target_link_libraries", eval_handle_target_link_libraries},
    {"target_link_options", eval_handle_target_link_options},
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

