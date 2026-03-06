#include "eval_cmake_path.h"
#include "eval_cmake_path_internal.h"

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
    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr(cause), hint);
}

static bool cmk_path_set_result(Evaluator_Context *ctx,
                                String_View path_var,
                                String_View out_var,
                                String_View value) {
    String_View dst = out_var.count > 0 ? out_var : path_var;
    return eval_var_set_current(ctx, dst, value);
}

static bool handle_set(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (arena_arr_len(a) < 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(SET) requires <path-var> and <input>",
                       nob_sv_from_cstr("Usage: cmake_path(SET <path-var> [NORMALIZE] <input>)"));
        return true;
    }

    String_View path_var = a[1];
    bool normalize = false;
    String_View input = nob_sv_from_cstr("");

    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "NORMALIZE")) {
            normalize = true;
            continue;
        }
        if (input.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(SET) received unexpected argument",
                           a[i]);
            return true;
        }
        input = a[i];
    }

    if (input.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(SET) requires an input path",
                       nob_sv_from_cstr("Usage: cmake_path(SET <path-var> [NORMALIZE] <input>)"));
        return true;
    }

    String_View out = normalize ? cmk_path_normalize_temp(ctx, input) : input;
    (void)eval_var_set_current(ctx, path_var, out);
    return true;
}

static bool handle_get(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (arena_arr_len(a) < 4) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(GET) requires <path-var> <component> <out-var>",
                       nob_sv_from_cstr("Usage: cmake_path(GET <path-var> <component> [LAST_ONLY] <out-var>)"));
        return true;
    }

    String_View path_var = a[1];
    String_View component = a[2];
    bool supports_last_only = cmk_path_is_component_supports_last_only(component);

    bool last_only = false;
    String_View out_var = nob_sv_from_cstr("");

    if (supports_last_only) {
        if (arena_arr_len(a) == 4) {
            out_var = a[3];
        } else if (arena_arr_len(a) == 5 && eval_sv_eq_ci_lit(a[3], "LAST_ONLY")) {
            last_only = true;
            out_var = a[4];
        } else {
            cmk_path_error(ctx, node, o,
                           "cmake_path(GET) invalid argument combination for component",
                           component);
            return true;
        }
    } else {
        if (arena_arr_len(a) != 4) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(GET) received unexpected argument",
                           a[4]);
            return true;
        }
        out_var = a[3];
    }

    String_View input = eval_var_get_visible(ctx, path_var);
    bool ok = false;
    String_View result = cmk_path_component_get_temp(ctx, input, component, last_only, &ok);
    if (!ok) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(GET) unsupported component",
                       component);
        return true;
    }

    (void)eval_var_set_current(ctx, out_var, result);
    return true;
}

static bool handle_append_like(Evaluator_Context *ctx,
                               const Node *node,
                               Cmake_Event_Origin o,
                               SV_List a,
                               bool string_mode) {
    if (arena_arr_len(a) < 2) {
        cmk_path_error(ctx, node, o,
                       string_mode
                           ? "cmake_path(APPEND_STRING) requires <path-var>"
                           : "cmake_path(APPEND) requires <path-var>",
                       string_mode
                           ? nob_sv_from_cstr("Usage: cmake_path(APPEND_STRING <path-var> [<input>...] [OUTPUT_VARIABLE <out-var>])")
                           : nob_sv_from_cstr("Usage: cmake_path(APPEND <path-var> [<input>...] [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a[1];
    String_View out_var = nob_sv_from_cstr("");
    String_View current = eval_var_get_visible(ctx, path_var);

    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(a)) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(APPEND*) OUTPUT_VARIABLE requires a variable name",
                               a[i]);
                return true;
            }
            out_var = a[++i];
            continue;
        }

        if (string_mode) {
            String_View parts[2] = {current, a[i]};
            current = svu_join_no_sep_temp(ctx, parts, 2);
        } else {
            if (current.count == 0) current = a[i];
            else current = eval_sv_path_join(eval_temp_arena(ctx), current, a[i]);
        }
    }

    (void)cmk_path_set_result(ctx, path_var, out_var, current);
    return true;
}

static bool handle_remove_filename(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (arena_arr_len(a) < 2) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(REMOVE_FILENAME) requires <path-var>",
                       nob_sv_from_cstr("Usage: cmake_path(REMOVE_FILENAME <path-var> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a[1];
    String_View out_var = nob_sv_from_cstr("");
    if (arena_arr_len(a) > 2) {
        if (arena_arr_len(a) == 4 && eval_sv_eq_ci_lit(a[2], "OUTPUT_VARIABLE")) {
            out_var = a[3];
        } else {
            cmk_path_error(ctx, node, o,
                           "cmake_path(REMOVE_FILENAME) received unexpected argument",
                           a[2]);
            return true;
        }
    }

    String_View value = eval_var_get_visible(ctx, path_var);
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
    if (arena_arr_len(a) < 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(REPLACE_FILENAME) requires <path-var> and <input>",
                       nob_sv_from_cstr("Usage: cmake_path(REPLACE_FILENAME <path-var> <input> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a[1];
    String_View input = nob_sv_from_cstr("");
    String_View out_var = nob_sv_from_cstr("");

    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(a)) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(REPLACE_FILENAME) OUTPUT_VARIABLE requires a variable name",
                               a[i]);
                return true;
            }
            out_var = a[++i];
            continue;
        }
        if (input.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(REPLACE_FILENAME) received unexpected argument",
                           a[i]);
            return true;
        }
        input = a[i];
    }

    if (input.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(REPLACE_FILENAME) requires replacement input",
                       nob_sv_from_cstr("Usage: cmake_path(REPLACE_FILENAME <path-var> <input> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View value = eval_var_get_visible(ctx, path_var);
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
    if (arena_arr_len(a) < 2) {
        cmk_path_error(ctx, node, o,
                       replace_mode
                           ? "cmake_path(REPLACE_EXTENSION) requires <path-var>"
                           : "cmake_path(REMOVE_EXTENSION) requires <path-var>",
                       replace_mode
                           ? nob_sv_from_cstr("Usage: cmake_path(REPLACE_EXTENSION <path-var> [LAST_ONLY] <input> [OUTPUT_VARIABLE <out-var>])")
                           : nob_sv_from_cstr("Usage: cmake_path(REMOVE_EXTENSION <path-var> [LAST_ONLY] [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a[1];
    bool last_only = false;
    String_View replacement = nob_sv_from_cstr("");
    String_View out_var = nob_sv_from_cstr("");

    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "LAST_ONLY")) {
            last_only = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(a)) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(*_EXTENSION) OUTPUT_VARIABLE requires a variable name",
                               a[i]);
                return true;
            }
            out_var = a[++i];
            continue;
        }

        if (!replace_mode) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(REMOVE_EXTENSION) received unexpected argument",
                           a[i]);
            return true;
        }

        if (replacement.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(REPLACE_EXTENSION) received unexpected argument",
                           a[i]);
            return true;
        }
        replacement = a[i];
    }

    if (replace_mode && replacement.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(REPLACE_EXTENSION) requires replacement input",
                       nob_sv_from_cstr("Usage: cmake_path(REPLACE_EXTENSION <path-var> [LAST_ONLY] <input> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View value = eval_var_get_visible(ctx, path_var);
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
    if (arena_arr_len(a) < 2) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(NORMAL_PATH) requires <path-var>",
                       nob_sv_from_cstr("Usage: cmake_path(NORMAL_PATH <path-var> [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a[1];
    String_View out_var = nob_sv_from_cstr("");

    if (arena_arr_len(a) > 2) {
        if (arena_arr_len(a) == 4 && eval_sv_eq_ci_lit(a[2], "OUTPUT_VARIABLE")) {
            out_var = a[3];
        } else {
            cmk_path_error(ctx, node, o,
                           "cmake_path(NORMAL_PATH) received unexpected argument",
                           a[2]);
            return true;
        }
    }

    String_View value = eval_var_get_visible(ctx, path_var);
    (void)cmk_path_set_result(ctx, path_var, out_var, cmk_path_normalize_temp(ctx, value));
    return true;
}

static bool handle_relative_path(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (arena_arr_len(a) < 2) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(RELATIVE_PATH) requires <path-var>",
                       nob_sv_from_cstr("Usage: cmake_path(RELATIVE_PATH <path-var> [BASE_DIRECTORY <dir>] [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a[1];
    String_View out_var = nob_sv_from_cstr("");
    String_View base_dir = cmk_path_current_source_dir(ctx);

    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BASE_DIRECTORY")) {
            if (i + 1 >= arena_arr_len(a)) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(RELATIVE_PATH) BASE_DIRECTORY requires a value",
                               a[i]);
                return true;
            }
            base_dir = a[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(a)) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(RELATIVE_PATH) OUTPUT_VARIABLE requires a variable name",
                               a[i]);
                return true;
            }
            out_var = a[++i];
            continue;
        }

        cmk_path_error(ctx, node, o,
                       "cmake_path(RELATIVE_PATH) received unexpected argument",
                       a[i]);
        return true;
    }

    String_View value = eval_var_get_visible(ctx, path_var);
    String_View abs_value = cmk_path_make_absolute_temp(ctx, value, base_dir);
    String_View abs_base = cmk_path_make_absolute_temp(ctx, base_dir, cmk_path_current_source_dir(ctx));
    String_View rel = cmk_path_relativize_temp(ctx, abs_value, abs_base);
    (void)cmk_path_set_result(ctx, path_var, out_var, rel);
    return true;
}

static bool handle_absolute_path(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (arena_arr_len(a) < 2) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(ABSOLUTE_PATH) requires <path-var>",
                       nob_sv_from_cstr("Usage: cmake_path(ABSOLUTE_PATH <path-var> [BASE_DIRECTORY <dir>] [NORMALIZE] [OUTPUT_VARIABLE <out-var>])"));
        return true;
    }

    String_View path_var = a[1];
    String_View out_var = nob_sv_from_cstr("");
    String_View base_dir = cmk_path_current_source_dir(ctx);
    bool normalize = false;

    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BASE_DIRECTORY")) {
            if (i + 1 >= arena_arr_len(a)) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(ABSOLUTE_PATH) BASE_DIRECTORY requires a value",
                               a[i]);
                return true;
            }
            base_dir = a[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "NORMALIZE")) {
            normalize = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(a)) {
                cmk_path_error(ctx, node, o,
                               "cmake_path(ABSOLUTE_PATH) OUTPUT_VARIABLE requires a variable name",
                               a[i]);
                return true;
            }
            out_var = a[++i];
            continue;
        }

        cmk_path_error(ctx, node, o,
                       "cmake_path(ABSOLUTE_PATH) received unexpected argument",
                       a[i]);
        return true;
    }

    String_View value = eval_var_get_visible(ctx, path_var);
    String_View abs = cmk_path_make_absolute_temp(ctx, value, base_dir);
    if (normalize) abs = cmk_path_normalize_temp(ctx, abs);
    (void)cmk_path_set_result(ctx, path_var, out_var, abs);
    return true;
}

static bool handle_native_path(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (arena_arr_len(a) < 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(NATIVE_PATH) requires <path-var> and output variable",
                       nob_sv_from_cstr("Usage: cmake_path(NATIVE_PATH <path-var> [NORMALIZE] <out-var>)"));
        return true;
    }

    String_View path_var = a[1];
    bool normalize = false;
    String_View out_var = nob_sv_from_cstr("");

    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "NORMALIZE")) {
            normalize = true;
            continue;
        }
        if (out_var.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(NATIVE_PATH) received unexpected argument",
                           a[i]);
            return true;
        }
        out_var = a[i];
    }

    if (out_var.count == 0) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(NATIVE_PATH) requires output variable",
                       nob_sv_from_cstr("Usage: cmake_path(NATIVE_PATH <path-var> [NORMALIZE] <out-var>)"));
        return true;
    }

    String_View value = eval_var_get_visible(ctx, path_var);
    if (normalize) value = cmk_path_normalize_temp(ctx, value);
    value = cmk_path_to_native_seps_temp(ctx, value);
    (void)eval_var_set_current(ctx, out_var, value);
    return true;
}

static bool handle_convert(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (arena_arr_len(a) < 4) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(CONVERT) requires input, mode and output variable",
                       nob_sv_from_cstr("Usage: cmake_path(CONVERT <input> TO_CMAKE_PATH_LIST|TO_NATIVE_PATH_LIST <out-var> [NORMALIZE])"));
        return true;
    }

    String_View input = a[1];
    String_View mode = a[2];
    bool normalize = false;
    String_View out_var = nob_sv_from_cstr("");

    for (size_t i = 3; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "NORMALIZE")) {
            normalize = true;
            continue;
        }
        if (out_var.count > 0) {
            cmk_path_error(ctx, node, o,
                           "cmake_path(CONVERT) received unexpected argument",
                           a[i]);
            return true;
        }
        out_var = a[i];
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
        SV_List parts = NULL;
        if (!cmk_path_split_char_list_temp(ctx, input, native_list_sep, &parts)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));

        SV_List converted = NULL;
        for (size_t i = 0; i < arena_arr_len(parts); i++) {
            String_View p = cmk_path_to_cmake_seps_temp(ctx, parts[i]);
            if (normalize) p = cmk_path_normalize_temp(ctx, p);
            if (!svu_list_push_temp(ctx, &converted, p)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
        String_View out = eval_sv_join_semi_temp(ctx, converted, arena_arr_len(converted));
        (void)eval_var_set_current(ctx, out_var, out);
        return true;
    }

    if (eval_sv_eq_ci_lit(mode, "TO_NATIVE_PATH_LIST")) {
        SV_List parts = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), input, &parts)) {
            return ctx_oom(ctx);
        }

#if defined(_WIN32)
        const char native_list_sep = ';';
#else
        const char native_list_sep = ':';
#endif

        SV_List converted = NULL;
        for (size_t i = 0; i < arena_arr_len(parts); i++) {
            String_View p = parts[i];
            if (normalize) p = cmk_path_normalize_temp(ctx, p);
            p = cmk_path_to_native_seps_temp(ctx, p);
            if (!svu_list_push_temp(ctx, &converted, p)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }

        String_View out = cmk_path_join_char_list_temp(ctx, converted, native_list_sep);
        (void)eval_var_set_current(ctx, out_var, out);
        return true;
    }

    cmk_path_error(ctx, node, o,
                   "cmake_path(CONVERT) unsupported conversion mode",
                   mode);
    return true;
}

static bool handle_compare(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (arena_arr_len(a) != 5) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(COMPARE) requires exactly 4 arguments",
                       nob_sv_from_cstr("Usage: cmake_path(COMPARE <input1> EQUAL|NOT_EQUAL <input2> <out-var>)"));
        return true;
    }

    String_View lhs = cmk_path_compare_canonical_temp(ctx, a[1]);
    String_View op = a[2];
    String_View rhs = cmk_path_compare_canonical_temp(ctx, a[3]);
    String_View out_var = a[4];

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

    (void)eval_var_set_current(ctx, out_var, result ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
    return true;
}

static bool handle_has_component(Evaluator_Context *ctx,
                                 const Node *node,
                                 Cmake_Event_Origin o,
                                 SV_List a,
                                 String_View mode) {
    if (arena_arr_len(a) != 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(HAS_*) requires <path-var> and <out-var>",
                       nob_sv_from_cstr("Usage: cmake_path(HAS_<component> <path-var> <out-var>)"));
        return true;
    }

    String_View path_var = a[1];
    String_View out_var = a[2];
    String_View value = eval_var_get_visible(ctx, path_var);
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
    (void)eval_var_set_current(ctx, out_var, has ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
    return true;
}

static bool handle_is_absolute(Evaluator_Context *ctx,
                               const Node *node,
                               Cmake_Event_Origin o,
                               SV_List a,
                               String_View mode) {
    if (arena_arr_len(a) != 3) {
        cmk_path_error(ctx, node, o,
                       "cmake_path(IS_*) requires <path-var> and <out-var>",
                       nob_sv_from_cstr("Usage: cmake_path(IS_ABSOLUTE <path-var> <out-var>)"));
        return true;
    }

    String_View value = eval_var_get_visible(ctx, a[1]);
    bool result = false;
    if (eval_sv_eq_ci_lit(mode, "IS_ABSOLUTE")) {
        result = eval_sv_is_abs_path(value);
    } else {
        cmk_path_error(ctx, node, o,
                       "cmake_path() unsupported IS_* mode",
                       mode);
        return true;
    }

    (void)eval_var_set_current(ctx, a[2], result ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
    return true;
}

Eval_Result eval_handle_cmake_path(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 1) {
        cmk_path_error(ctx, node, o,
                       "cmake_path() requires a subcommand",
                       nob_sv_from_cstr("Usage: cmake_path(<mode> ...)"));
        return eval_result_ok();
    }

    String_View mode = a[0];

    if (eval_sv_eq_ci_lit(mode, "SET")) return eval_result_from_bool(handle_set(ctx, node, o, a));
    if (eval_sv_eq_ci_lit(mode, "GET")) return eval_result_from_bool(handle_get(ctx, node, o, a));
    if (eval_sv_eq_ci_lit(mode, "APPEND")) return eval_result_from_bool(handle_append_like(ctx, node, o, a, false));
    if (eval_sv_eq_ci_lit(mode, "APPEND_STRING")) return eval_result_from_bool(handle_append_like(ctx, node, o, a, true));
    if (eval_sv_eq_ci_lit(mode, "REMOVE_FILENAME")) return eval_result_from_bool(handle_remove_filename(ctx, node, o, a));
    if (eval_sv_eq_ci_lit(mode, "REPLACE_FILENAME")) return eval_result_from_bool(handle_replace_filename(ctx, node, o, a));
    if (eval_sv_eq_ci_lit(mode, "REMOVE_EXTENSION")) return eval_result_from_bool(handle_extension_common(ctx, node, o, a, false));
    if (eval_sv_eq_ci_lit(mode, "REPLACE_EXTENSION")) return eval_result_from_bool(handle_extension_common(ctx, node, o, a, true));
    if (eval_sv_eq_ci_lit(mode, "NORMAL_PATH")) {
        bool ok = handle_normal_path(ctx, node, o, a);
        if (ok && !eval_should_stop(ctx)) {
            String_View out_var = (arena_arr_len(a) == 4 && eval_sv_eq_ci_lit(a[2], "OUTPUT_VARIABLE")) ? a[3] : nob_sv_from_cstr("");
            if (!eval_emit_path_normalize(ctx, o, out_var)) return eval_result_fatal();
        }
        return eval_result_from_bool(ok);
    }
    if (eval_sv_eq_ci_lit(mode, "RELATIVE_PATH")) return eval_result_from_bool(handle_relative_path(ctx, node, o, a));
    if (eval_sv_eq_ci_lit(mode, "ABSOLUTE_PATH")) {
        bool ok = handle_absolute_path(ctx, node, o, a);
        if (ok && !eval_should_stop(ctx)) {
            String_View out_var = nob_sv_from_cstr("");
            for (size_t i = 2; i + 1 < arena_arr_len(a); ++i) {
                if (eval_sv_eq_ci_lit(a[i], "OUTPUT_VARIABLE")) {
                    out_var = a[i + 1];
                    break;
                }
            }
            if (!eval_emit_path_normalize(ctx, o, out_var)) return eval_result_fatal();
        }
        return eval_result_from_bool(ok);
    }
    if (eval_sv_eq_ci_lit(mode, "NATIVE_PATH")) {
        bool ok = handle_native_path(ctx, node, o, a);
        if (ok && !eval_should_stop(ctx)) {
            String_View out_var = arena_arr_len(a) >= 3 ? a[arena_arr_len(a) - 1] : nob_sv_from_cstr("");
            if (!eval_emit_path_normalize(ctx, o, out_var)) return eval_result_fatal();
        }
        return eval_result_from_bool(ok);
    }
    if (eval_sv_eq_ci_lit(mode, "CONVERT")) {
        bool ok = handle_convert(ctx, node, o, a);
        if (ok && !eval_should_stop(ctx)) {
            String_View out_var = nob_sv_from_cstr("");
            for (size_t i = 3; i < arena_arr_len(a); ++i) {
                if (eval_sv_eq_ci_lit(a[i], "NORMALIZE")) continue;
                out_var = a[i];
                break;
            }
            if (!eval_emit_path_convert(ctx, o, out_var)) return eval_result_fatal();
        }
        return eval_result_from_bool(ok);
    }
    if (eval_sv_eq_ci_lit(mode, "COMPARE")) {
        bool ok = handle_compare(ctx, node, o, a);
        if (ok && !eval_should_stop(ctx)) {
            String_View out_var = arena_arr_len(a) == 5 ? a[4] : nob_sv_from_cstr("");
            if (!eval_emit_path_compare(ctx, o, out_var)) return eval_result_fatal();
        }
        return eval_result_from_bool(ok);
    }

    if (svu_has_prefix_ci_lit(mode, "HAS_")) return eval_result_from_bool(handle_has_component(ctx, node, o, a, mode));
    if (svu_has_prefix_ci_lit(mode, "IS_")) return eval_result_from_bool(handle_is_absolute(ctx, node, o, a, mode));

    cmk_path_error(ctx, node, o,
                   "cmake_path() subcommand is not implemented",
                   mode);
    return eval_result_ok();
}
