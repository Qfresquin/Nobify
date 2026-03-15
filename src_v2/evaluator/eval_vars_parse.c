#include "eval_vars.h"

#include "evaluator_internal.h"
#include "eval_expr.h"
#include "eval_opt_parser.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
typedef enum {
    PARSE_KEYWORD_OPTION = 0,
    PARSE_KEYWORD_ONE,
    PARSE_KEYWORD_MULTI,
} Parse_Keyword_Kind;

typedef struct {
    String_View name;
    Parse_Keyword_Kind kind;
    bool option_present;
    bool one_defined;
    String_View one_value;
    bool multi_defined;
    SV_List multi_values;
} Parse_Keyword_Spec;

static bool parse_sv_eq_exact(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

static bool parse_strip_bracket_arg(String_View in, String_View *out) {
    if (!out) return false;
    *out = in;
    if (in.count < 4 || !in.data || in.data[0] != '[') return false;

    size_t eq_count = 0;
    size_t i = 1;
    while (i < in.count && in.data[i] == '=') {
        eq_count++;
        i++;
    }
    if (i >= in.count || in.data[i] != '[') return false;
    size_t open_len = i + 1;
    if (in.count < open_len + 2 + eq_count) return false;

    size_t close_pos = in.count - (eq_count + 2);
    if (in.data[close_pos] != ']') return false;
    for (size_t k = 0; k < eq_count; k++) {
        if (in.data[close_pos + 1 + k] != '=') return false;
    }
    if (in.data[in.count - 1] != ']') return false;
    if (close_pos < open_len) return false;

    *out = nob_sv_from_parts(in.data + open_len, close_pos - open_len);
    return true;
}

static String_View parse_arg_flat(Evaluator_Context *ctx, const Arg *arg) {
    if (!ctx || !arg || arena_arr_len(arg->items) == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) total += arg->items[i].text.count;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) {
        String_View text = arg->items[i].text;
        if (text.count > 0) {
            memcpy(buf + off, text.data, text.count);
            off += text.count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static String_View parse_eval_arg_single(Evaluator_Context *ctx, const Arg *arg) {
    if (!ctx || !arg) return nob_sv_from_cstr("");

    String_View flat = parse_arg_flat(ctx, arg);
    String_View value = eval_expand_vars(ctx, flat);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    if (arg->kind == ARG_QUOTED) {
        if (value.count >= 2 && value.data[0] == '"' && value.data[value.count - 1] == '"') {
            return nob_sv_from_parts(value.data + 1, value.count - 2);
        }
        return value;
    }
    if (arg->kind == ARG_BRACKET) {
        String_View stripped = value;
        (void)parse_strip_bracket_arg(value, &stripped);
        return stripped;
    }
    return value;
}

static bool parse_resolve_arg_range(Evaluator_Context *ctx,
                                    const Args *raw_args,
                                    size_t begin,
                                    SV_List *out) {
    if (!ctx || !raw_args || !out) return false;
    *out = (SV_List){0};

    for (size_t i = begin; i < arena_arr_len(*raw_args); i++) {
        const Arg *arg = &(*raw_args)[i];
        String_View value = parse_eval_arg_single(ctx, arg);
        if (eval_should_stop(ctx)) return false;

        if (arg->kind == ARG_QUOTED || arg->kind == ARG_BRACKET) {
            if (!eval_sv_arr_push_temp(ctx, out, value)) return false;
            continue;
        }

        if (value.count == 0) continue;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), value, out)) {
            return ctx_oom(ctx);
        }
    }

    return true;
}

static bool parse_keyword_name_valid(String_View keyword) {
    return keyword.count > 0;
}

static Parse_Keyword_Spec *parse_find_keyword(Parse_Keyword_Spec *specs, size_t count, String_View keyword) {
    if (!specs || keyword.count == 0) return NULL;
    for (size_t i = 0; i < count; i++) {
        if (parse_sv_eq_exact(specs[i].name, keyword)) return &specs[i];
    }
    return NULL;
}

static bool parse_warn_duplicate_keyword(Evaluator_Context *ctx, const Node *node, String_View keyword) {
    return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("cmake_parse_arguments"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("cmake_parse_arguments() keyword appears more than once across keyword lists"), keyword);
}

static bool parse_add_keyword_list(Evaluator_Context *ctx,
                                   const Node *node,
                                   const Arg *arg,
                                   Parse_Keyword_Kind kind,
                                   Parse_Keyword_Spec *specs,
                                   size_t *inout_count,
                                   size_t max_count) {
    if (!ctx || !node || !arg || !inout_count) return false;

    String_View raw = parse_eval_arg_single(ctx, arg);
    if (eval_should_stop(ctx)) return false;
    if (raw.count == 0) return true;

    SV_List items = NULL;
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), raw, &items)) return ctx_oom(ctx);

    for (size_t i = 0; i < arena_arr_len(items); i++) {
        String_View keyword = items[i];
        if (!parse_keyword_name_valid(keyword)) continue;
        if (parse_find_keyword(specs, *inout_count, keyword)) {
            if (!parse_warn_duplicate_keyword(ctx, node, keyword)) return false;
            continue;
        }
        if (*inout_count >= max_count) return ctx_oom(ctx);
        specs[*inout_count].name = sv_copy_to_event_arena(ctx, keyword);
        specs[*inout_count].kind = kind;
        if (eval_should_stop(ctx)) return false;
        (*inout_count)++;
    }

    return true;
}

static bool parse_nonnegative_index(Evaluator_Context *ctx, String_View token, size_t *out_value) {
    if (!ctx || !out_value) return false;
    *out_value = 0;
    if (token.count == 0) return false;

    char *buf = eval_sv_to_cstr_temp(ctx, token);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    char *end = NULL;
    unsigned long long v = strtoull(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out_value = (size_t)v;
    return true;
}

static bool parse_build_prefix_var_name(Evaluator_Context *ctx,
                                        String_View prefix,
                                        const char *suffix,
                                        String_View *out_name) {
    if (!ctx || !out_name || !suffix) return false;
    size_t suffix_len = strlen(suffix);
    size_t total = prefix.count + suffix_len;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    if (prefix.count > 0) memcpy(buf, prefix.data, prefix.count);
    memcpy(buf + prefix.count, suffix, suffix_len);
    buf[total] = '\0';
    *out_name = nob_sv_from_parts(buf, total);
    return true;
}

static bool parse_build_prefixed_var_name(Evaluator_Context *ctx,
                                          String_View prefix,
                                          String_View keyword,
                                          String_View *out_name) {
    if (!ctx || !out_name) return false;
    size_t total = prefix.count + 1 + keyword.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    if (prefix.count > 0) memcpy(buf, prefix.data, prefix.count);
    buf[prefix.count] = '_';
    if (keyword.count > 0) memcpy(buf + prefix.count + 1, keyword.data, keyword.count);
    buf[total] = '\0';
    *out_name = nob_sv_from_parts(buf, total);
    return true;
}

static bool parse_single_empty_value_defines_var(Evaluator_Context *ctx) {
    if (!ctx) return false;
    if (!eval_policy_is_known(nob_sv_from_cstr(EVAL_POLICY_CMP0174))) return false;
    return eval_policy_is_new(ctx, EVAL_POLICY_CMP0174);
}

static bool parse_collect_parse_argv_source(Evaluator_Context *ctx, size_t start_index, SV_List *out) {
    if (!ctx || !out) return false;
    *out = NULL;

    String_View argc_sv = eval_var_get_visible(ctx, nob_sv_from_cstr("ARGC"));
    size_t argc = 0;
    if (!parse_nonnegative_index(ctx, argc_sv, &argc)) return false;

    for (size_t i = start_index; i < argc; i++) {
        char key_buf[64];
        int n = snprintf(key_buf, sizeof(key_buf), "ARGV%zu", i);
        if (n <= 0 || (size_t)n >= sizeof(key_buf)) return ctx_oom(ctx);
        if (!eval_sv_arr_push_temp(ctx, out, eval_var_get_visible(ctx, nob_sv_from_cstr(key_buf)))) return false;
    }

    return true;
}

static bool parse_assign_results(Evaluator_Context *ctx,
                                 String_View prefix,
                                 Parse_Keyword_Spec *specs,
                                 size_t spec_count,
                                 const SV_List *unparsed,
                                 const SV_List *missing) {
    if (!ctx) return false;

    for (size_t i = 0; i < spec_count; i++) {
        String_View var = {0};
        if (!parse_build_prefixed_var_name(ctx, prefix, specs[i].name, &var)) return false;

        switch (specs[i].kind) {
            case PARSE_KEYWORD_OPTION:
                if (!eval_var_set_current(ctx, var, specs[i].option_present ? nob_sv_from_cstr("TRUE")
                                                                    : nob_sv_from_cstr("FALSE"))) {
                    return false;
                }
                break;
            case PARSE_KEYWORD_ONE:
                if (specs[i].one_defined) {
                    if (!eval_var_set_current(ctx, var, specs[i].one_value)) return false;
                } else {
                    if (!eval_var_unset_current(ctx, var)) return false;
                }
                break;
            case PARSE_KEYWORD_MULTI:
                if (specs[i].multi_defined) {
                    if (!eval_var_set_current(ctx,
                                      var,
                                      eval_sv_join_semi_temp(ctx, specs[i].multi_values, arena_arr_len(specs[i].multi_values)))) {
                        return false;
                    }
                } else {
                    if (!eval_var_unset_current(ctx, var)) return false;
                }
                break;
        }
    }

    String_View unparsed_var = {0};
    if (!parse_build_prefix_var_name(ctx, prefix, "_UNPARSED_ARGUMENTS", &unparsed_var)) return false;
    if (unparsed && arena_arr_len(*unparsed) > 0) {
        if (!eval_var_set_current(ctx, unparsed_var, eval_sv_join_semi_temp(ctx, *unparsed, arena_arr_len(*unparsed)))) return false;
    } else {
        if (!eval_var_unset_current(ctx, unparsed_var)) return false;
    }

    String_View missing_var = {0};
    if (!parse_build_prefix_var_name(ctx, prefix, "_KEYWORDS_MISSING_VALUES", &missing_var)) return false;
    if (missing && arena_arr_len(*missing) > 0) {
        if (!eval_var_set_current(ctx, missing_var, eval_sv_join_semi_temp(ctx, *missing, arena_arr_len(*missing)))) return false;
    } else {
        if (!eval_var_unset_current(ctx, missing_var)) return false;
    }

    return true;
}

Eval_Result eval_handle_cmake_parse_arguments(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    const Args *raw = &node->as.cmd.args;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(*raw) < 4) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments() requires at least four arguments"), nob_sv_from_cstr("Usage: cmake_parse_arguments(<prefix> <options> <one_value_keywords> <multi_value_keywords> <args>...)"));
        return eval_result_from_ctx(ctx);
    }

    bool use_parse_argv = false;
    size_t spec_index = 0;
    String_View first = parse_eval_arg_single(ctx, &(*raw)[0]);
    if (eval_should_stop(ctx)) return eval_result_fatal();
    if (eval_sv_eq_ci_lit(first, "PARSE_ARGV")) {
        use_parse_argv = true;
    }

    String_View prefix = nob_sv_from_cstr("");
    SV_List source_args = NULL;
    if (use_parse_argv) {
        if (arena_arr_len(*raw) < 6) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) requires index, prefix and three keyword lists"), nob_sv_from_cstr("Usage: cmake_parse_arguments(PARSE_ARGV <N> <prefix> <options> <one_value_keywords> <multi_value_keywords>)"));
            return eval_result_from_ctx(ctx);
        }
        if (ctx->function_eval_depth == 0 || eval_exec_has_active_kind(ctx, EVAL_EXEC_CTX_MACRO)) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_CONTEXT, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) may only be used in function() scope"), nob_sv_from_cstr("Use the direct signature in macro() or top-level scope"));
            return eval_result_from_ctx(ctx);
        }

        size_t start_index = 0;
        String_View index_sv = parse_eval_arg_single(ctx, &(*raw)[1]);
        if (!parse_nonnegative_index(ctx, index_sv, &start_index)) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) requires a non-negative integer index"), index_sv);
            return eval_result_from_ctx(ctx);
        }

        prefix = parse_eval_arg_single(ctx, &(*raw)[2]);
        if (eval_should_stop(ctx)) return eval_result_fatal();
        spec_index = 3;
        if (!parse_collect_parse_argv_source(ctx, start_index, &source_args)) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) could not read ARGV values"), nob_sv_from_cstr("Ensure the command is called from function() scope"));
            return eval_result_from_ctx(ctx);
        }
    } else {
        prefix = first;
        spec_index = 1;
        if (!parse_resolve_arg_range(ctx, raw, 4, &source_args)) return eval_result_from_ctx(ctx);
    }

    size_t max_specs = 0;
    for (size_t i = spec_index; i < spec_index + 3 && i < arena_arr_len(*raw); i++) {
        String_View raw_list = parse_eval_arg_single(ctx, &(*raw)[i]);
        if (eval_should_stop(ctx)) return eval_result_fatal();
        if (raw_list.count == 0) continue;

        SV_List split = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), raw_list, &split)) return eval_result_from_ctx(ctx);
        max_specs += arena_arr_len(split);
    }

    Parse_Keyword_Spec *specs = NULL;
    if (max_specs > 0) {
        specs = arena_alloc_array_zero(eval_temp_arena(ctx), Parse_Keyword_Spec, max_specs);
        EVAL_OOM_RETURN_IF_NULL(ctx, specs, eval_result_fatal());
    }
    size_t spec_count = 0;

    if (!parse_add_keyword_list(ctx, node, &(*raw)[spec_index + 0], PARSE_KEYWORD_OPTION, specs, &spec_count, max_specs)) {
        return eval_result_from_ctx(ctx);
    }
    if (!parse_add_keyword_list(ctx, node, &(*raw)[spec_index + 1], PARSE_KEYWORD_ONE, specs, &spec_count, max_specs)) {
        return eval_result_from_ctx(ctx);
    }
    if (!parse_add_keyword_list(ctx, node, &(*raw)[spec_index + 2], PARSE_KEYWORD_MULTI, specs, &spec_count, max_specs)) {
        return eval_result_from_ctx(ctx);
    }

    SV_List unparsed = NULL;
    SV_List missing = NULL;
    Parse_Keyword_Spec *active = NULL;
    bool active_has_value = false;
    bool single_empty_defines = parse_single_empty_value_defines_var(ctx);

    for (size_t i = 0; i < arena_arr_len(source_args); i++) {
        String_View token = source_args[i];
        Parse_Keyword_Spec *matched = parse_find_keyword(specs, spec_count, token);
        if (matched) {
            if (active && !active_has_value) {
                if (!eval_sv_arr_push_temp(ctx, &missing, active->name)) return eval_result_fatal();
            }

            active = matched;
            active_has_value = false;
            if (matched->kind == PARSE_KEYWORD_OPTION) {
                matched->option_present = true;
                active = NULL;
                active_has_value = true;
            } else if (matched->kind == PARSE_KEYWORD_ONE) {
                matched->one_defined = false;
                matched->one_value = nob_sv_from_cstr("");
            } else {
                matched->multi_defined = false;
                matched->multi_values = NULL;
            }
            continue;
        }

        if (active) {
            if (active->kind == PARSE_KEYWORD_ONE) {
                active->one_value = token;
                active->one_defined = single_empty_defines || token.count > 0;
                active_has_value = true;
                active = NULL;
                continue;
            }
            if (active->kind == PARSE_KEYWORD_MULTI) {
                if (!eval_sv_arr_push_temp(ctx, &active->multi_values, token)) return eval_result_fatal();
                active->multi_defined = true;
                active_has_value = true;
                continue;
            }
        }

        if (!eval_sv_arr_push_temp(ctx, &unparsed, token)) return eval_result_fatal();
    }

    if (active && !active_has_value) {
        if (!eval_sv_arr_push_temp(ctx, &missing, active->name)) return eval_result_fatal();
    }

    if (!parse_assign_results(ctx, prefix, specs, spec_count, &unparsed, &missing)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

static bool separate_arguments_parse_tokens(Evaluator_Context *ctx,
                                            Eval_Cmdline_Mode mode,
                                            String_View input,
                                            SV_List *out) {
    return eval_split_command_line_temp(ctx, mode, input, out);
}

static String_View separate_arguments_replace_spaces_temp(Evaluator_Context *ctx, String_View input) {
    if (!ctx || input.count == 0) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < input.count; i++) {
        buf[i] = (input.data[i] == ' ') ? ';' : input.data[i];
    }
    buf[input.count] = '\0';
    return nob_sv_from_parts(buf, input.count);
}

static bool separate_arguments_set_program_pair(Evaluator_Context *ctx,
                                                String_View out_var,
                                                String_View program,
                                                String_View args_string) {
    if (!ctx) return false;

    size_t total = program.count + 1 + args_string.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t off = 0;
    if (program.count > 0) {
        memcpy(buf + off, program.data, program.count);
        off += program.count;
    }
    buf[off++] = ';';
    if (args_string.count > 0) {
        memcpy(buf + off, args_string.data, args_string.count);
        off += args_string.count;
    }
    buf[off] = '\0';
    return eval_var_set_current(ctx, out_var, nob_sv_from_parts(buf, off));
}

static size_t separate_arguments_mode_count(bool unix_mode, bool windows_mode, bool native_mode) {
    return (unix_mode ? 1u : 0u) + (windows_mode ? 1u : 0u) + (native_mode ? 1u : 0u);
}

static Eval_Cmdline_Mode separate_arguments_selected_mode(bool unix_mode,
                                                          bool windows_mode,
                                                          bool native_mode) {
    if (unix_mode) return EVAL_CMDLINE_UNIX;
    if (windows_mode) return EVAL_CMDLINE_WINDOWS;
    if (native_mode) return EVAL_CMDLINE_NATIVE;
    return EVAL_CMDLINE_NATIVE;
}

static bool separate_arguments_trim_whitespace(String_View input, String_View *out_trimmed) {
    if (!out_trimmed) return false;
    *out_trimmed = svu_trim_ascii_ws(input);
    return true;
}

Eval_Result eval_handle_separate_arguments(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "separate_arguments", nob_sv_from_cstr("separate_arguments() requires an output variable"), nob_sv_from_cstr("Usage: separate_arguments(<var> [UNIX_COMMAND|WINDOWS_COMMAND|NATIVE_COMMAND] <args>...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View out_var = a[0];
    if (arena_arr_len(a) == 1) {
        String_View current = eval_var_get_visible(ctx, out_var);
        if (!eval_var_set_current(ctx, out_var, separate_arguments_replace_spaces_temp(ctx, current))) {
            return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    bool unix_mode = false;
    bool windows_mode = false;
    bool native_mode = false;
    bool program_mode = false;
    bool separate_args = false;
    String_View input = nob_sv_from_cstr("");
    size_t positional_count = 0;

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "UNIX_COMMAND")) {
            unix_mode = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "WINDOWS_COMMAND")) {
            windows_mode = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "NATIVE_COMMAND")) {
            native_mode = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PROGRAM")) {
            program_mode = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "SEPARATE_ARGS")) {
            separate_args = true;
            continue;
        }

        positional_count++;
        if (positional_count > 1) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                                 node,
                                                 o,
                                                 EV_DIAG_ERROR,
                                                 EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                                 "separate_arguments",
                                                 nob_sv_from_cstr("separate_arguments() given unexpected argument(s)"),
                                                 a[i]);
            return eval_result_from_ctx(ctx);
        }
        input = a[i];
    }

    if (separate_arguments_mode_count(unix_mode, windows_mode, native_mode) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                             node,
                                             o,
                                             EV_DIAG_ERROR,
                                             EVAL_DIAG_MISSING_REQUIRED,
                                             "separate_arguments",
                                             nob_sv_from_cstr("separate_arguments() missing required mode"),
                                             nob_sv_from_cstr("Use UNIX_COMMAND, WINDOWS_COMMAND, or NATIVE_COMMAND"));
        return eval_result_from_ctx(ctx);
    }

    if (separate_arguments_mode_count(unix_mode, windows_mode, native_mode) > 1) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                             node,
                                             o,
                                             EV_DIAG_ERROR,
                                             EVAL_DIAG_INVALID_VALUE,
                                             "separate_arguments",
                                             nob_sv_from_cstr("separate_arguments() modes are mutually exclusive"),
                                             nob_sv_from_cstr("Choose exactly one of UNIX_COMMAND, WINDOWS_COMMAND, or NATIVE_COMMAND"));
        return eval_result_from_ctx(ctx);
    }

    if (separate_args && !program_mode) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                             node,
                                             o,
                                             EV_DIAG_ERROR,
                                             EVAL_DIAG_INVALID_VALUE,
                                             "separate_arguments",
                                             nob_sv_from_cstr("separate_arguments(SEPARATE_ARGS) requires PROGRAM"),
                                             nob_sv_from_cstr("Add PROGRAM before SEPARATE_ARGS"));
        return eval_result_from_ctx(ctx);
    }

    if (positional_count == 0 || input.count == 0) {
        if (!eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""))) {
            return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    Eval_Cmdline_Mode mode = separate_arguments_selected_mode(unix_mode, windows_mode, native_mode);
    if (!program_mode) {
        SV_List tokens = NULL;
        if (!separate_arguments_parse_tokens(ctx, mode, input, &tokens)) return eval_result_from_ctx(ctx);
        if (!eval_var_set_current(ctx, out_var, eval_sv_join_semi_temp(ctx, tokens, arena_arr_len(tokens)))) {
            return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    if (separate_args) {
        SV_List tokens = NULL;
        if (!separate_arguments_parse_tokens(ctx, mode, input, &tokens)) return eval_result_from_ctx(ctx);
        if (arena_arr_len(tokens) == 0) {
            if (!eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""))) return eval_result_from_ctx(ctx);
            return eval_result_from_ctx(ctx);
        }

        String_View program_path = nob_sv_from_cstr("");
        bool found = false;
        if (!eval_find_program_full_path_temp(ctx, tokens[0], &program_path, &found)) return eval_result_from_ctx(ctx);
        if (!found) {
            if (!eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""))) return eval_result_from_ctx(ctx);
            return eval_result_from_ctx(ctx);
        }

        tokens[0] = program_path;
        if (!eval_var_set_current(ctx, out_var, eval_sv_join_semi_temp(ctx, tokens, arena_arr_len(tokens)))) {
            return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    String_View trimmed = nob_sv_from_cstr("");
    if (!separate_arguments_trim_whitespace(input, &trimmed)) return eval_result_fatal();

    String_View program_path = nob_sv_from_cstr("");
    bool found = false;
    if (trimmed.count > 0 &&
        !eval_find_program_full_path_temp(ctx, trimmed, &program_path, &found)) {
        return eval_result_from_ctx(ctx);
    }

    String_View program_args = nob_sv_from_cstr("");
    if (!found) {
        String_View program_token = nob_sv_from_cstr("");
        if (!eval_split_program_from_command_line_temp(ctx, mode, input, &program_token, &program_args)) {
            return eval_result_from_ctx(ctx);
        }
        if (program_token.count > 0 &&
            !eval_find_program_full_path_temp(ctx, program_token, &program_path, &found)) {
            return eval_result_from_ctx(ctx);
        }
        if (!found) program_args = nob_sv_from_cstr("");
    }

    if (!found) {
        if (!eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""))) return eval_result_from_ctx(ctx);
        return eval_result_from_ctx(ctx);
    }

    if (!separate_arguments_set_program_pair(ctx, out_var, program_path, program_args)) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}
