#include "eval_vars.h"

#include "evaluator_internal.h"
#include "eval_expr.h"
#include "arena_dyn.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "stb_ds.h"

static bool load_cache_emit_diag(Evaluator_Context *ctx,
                                 const Node *node,
                                 Cmake_Diag_Severity severity,
                                 String_View cause,
                                 String_View hint) {
    return eval_emit_diag(ctx,
                          severity,
                          nob_sv_from_cstr("load_cache"),
                          node->as.cmd.name,
                          eval_origin_from_node(ctx, node),
                          cause,
                          hint);
}

static bool load_cache_name_in_list(String_View name, const SV_List *list) {
    if (!list) return false;
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        if (nob_sv_eq(name, (*list)[i])) return true;
    }
    return false;
}

static bool load_cache_resolve_path(Evaluator_Context *ctx, String_View raw_path, String_View *out_path) {
    if (!ctx || !out_path) return false;
    String_View path = raw_path;
    if (!eval_sv_is_abs_path(path)) {
        String_View base = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
        if (base.count == 0) base = ctx->binary_dir;
        path = eval_sv_path_join(eval_temp_arena(ctx), base, path);
        if (eval_should_stop(ctx)) return false;
    }
    if (!nob_sv_end_with(path, "CMakeCache.txt")) {
        path = eval_sv_path_join(eval_temp_arena(ctx), path, nob_sv_from_cstr("CMakeCache.txt"));
        if (eval_should_stop(ctx)) return false;
    }
    *out_path = path;
    return true;
}

static bool load_cache_parse_line(String_View line,
                                  String_View *out_key,
                                  String_View *out_type,
                                  String_View *out_value) {
    if (!out_key || !out_type || !out_value) return false;
    *out_key = nob_sv_from_cstr("");
    *out_type = nob_sv_from_cstr("");
    *out_value = nob_sv_from_cstr("");
    if (line.count == 0) return false;
    if (line.data[0] == '#' || line.data[0] == '/') return false;

    size_t colon = (size_t)-1;
    size_t eq = (size_t)-1;
    for (size_t i = 0; i < line.count; i++) {
        if (line.data[i] == ':' && colon == (size_t)-1) {
            colon = i;
            continue;
        }
        if (line.data[i] == '=' && colon != (size_t)-1) {
            eq = i;
            break;
        }
    }
    if (colon == (size_t)-1 || eq == (size_t)-1 || colon == 0 || eq <= colon + 1) return false;
    *out_key = nob_sv_from_parts(line.data, colon);
    *out_type = nob_sv_from_parts(line.data + colon + 1, eq - colon - 1);
    *out_value = nob_sv_from_parts(line.data + eq + 1, line.count - eq - 1);
    return out_key->count > 0 && out_type->count > 0;
}

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
    return eval_emit_diag(ctx,
                          EV_DIAG_WARNING,
                          nob_sv_from_cstr("cmake_parse_arguments"),
                          node->as.cmd.name,
                          eval_origin_from_node(ctx, node),
                          nob_sv_from_cstr("cmake_parse_arguments() keyword appears more than once across keyword lists"),
                          keyword);
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

    String_View argc_sv = eval_var_get(ctx, nob_sv_from_cstr("ARGC"));
    size_t argc = 0;
    if (!parse_nonnegative_index(ctx, argc_sv, &argc)) return false;

    for (size_t i = start_index; i < argc; i++) {
        char key_buf[64];
        int n = snprintf(key_buf, sizeof(key_buf), "ARGV%zu", i);
        if (n <= 0 || (size_t)n >= sizeof(key_buf)) return ctx_oom(ctx);
        if (!eval_sv_arr_push_temp(ctx, out, eval_var_get(ctx, nob_sv_from_cstr(key_buf)))) return false;
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
                if (!eval_var_set(ctx, var, specs[i].option_present ? nob_sv_from_cstr("TRUE")
                                                                    : nob_sv_from_cstr("FALSE"))) {
                    return false;
                }
                break;
            case PARSE_KEYWORD_ONE:
                if (specs[i].one_defined) {
                    if (!eval_var_set(ctx, var, specs[i].one_value)) return false;
                } else {
                    if (!eval_var_unset(ctx, var)) return false;
                }
                break;
            case PARSE_KEYWORD_MULTI:
                if (specs[i].multi_defined) {
                    if (!eval_var_set(ctx,
                                      var,
                                      eval_sv_join_semi_temp(ctx, specs[i].multi_values, arena_arr_len(specs[i].multi_values)))) {
                        return false;
                    }
                } else {
                    if (!eval_var_unset(ctx, var)) return false;
                }
                break;
        }
    }

    String_View unparsed_var = {0};
    if (!parse_build_prefix_var_name(ctx, prefix, "_UNPARSED_ARGUMENTS", &unparsed_var)) return false;
    if (unparsed && arena_arr_len(*unparsed) > 0) {
        if (!eval_var_set(ctx, unparsed_var, eval_sv_join_semi_temp(ctx, *unparsed, arena_arr_len(*unparsed)))) return false;
    } else {
        if (!eval_var_unset(ctx, unparsed_var)) return false;
    }

    String_View missing_var = {0};
    if (!parse_build_prefix_var_name(ctx, prefix, "_KEYWORDS_MISSING_VALUES", &missing_var)) return false;
    if (missing && arena_arr_len(*missing) > 0) {
        if (!eval_var_set(ctx, missing_var, eval_sv_join_semi_temp(ctx, *missing, arena_arr_len(*missing)))) return false;
    } else {
        if (!eval_var_unset(ctx, missing_var)) return false;
    }

    return true;
}

bool eval_handle_cmake_parse_arguments(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    const Args *raw = &node->as.cmd.args;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(*raw) < 4) {
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments() requires at least four arguments"),
                             nob_sv_from_cstr("Usage: cmake_parse_arguments(<prefix> <options> <one_value_keywords> <multi_value_keywords> <args>...)"));
        return !eval_should_stop(ctx);
    }

    bool use_parse_argv = false;
    size_t spec_index = 0;
    String_View first = parse_eval_arg_single(ctx, &(*raw)[0]);
    if (eval_should_stop(ctx)) return false;
    if (eval_sv_eq_ci_lit(first, "PARSE_ARGV")) {
        use_parse_argv = true;
    }

    String_View prefix = nob_sv_from_cstr("");
    SV_List source_args = NULL;
    if (use_parse_argv) {
        if (arena_arr_len(*raw) < 6) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) requires index, prefix and three keyword lists"),
                                 nob_sv_from_cstr("Usage: cmake_parse_arguments(PARSE_ARGV <N> <prefix> <options> <one_value_keywords> <multi_value_keywords>)"));
            return !eval_should_stop(ctx);
        }
        if (ctx->function_eval_depth == 0 || arena_arr_len(ctx->macro_frames) > 0) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) may only be used in function() scope"),
                                 nob_sv_from_cstr("Use the direct signature in macro() or top-level scope"));
            return !eval_should_stop(ctx);
        }

        size_t start_index = 0;
        String_View index_sv = parse_eval_arg_single(ctx, &(*raw)[1]);
        if (!parse_nonnegative_index(ctx, index_sv, &start_index)) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) requires a non-negative integer index"),
                                 index_sv);
            return !eval_should_stop(ctx);
        }

        prefix = parse_eval_arg_single(ctx, &(*raw)[2]);
        if (eval_should_stop(ctx)) return false;
        spec_index = 3;
        if (!parse_collect_parse_argv_source(ctx, start_index, &source_args)) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "cmake_parse_arguments", nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) could not read ARGV values"),
                                 nob_sv_from_cstr("Ensure the command is called from function() scope"));
            return !eval_should_stop(ctx);
        }
    } else {
        prefix = first;
        spec_index = 1;
        if (!parse_resolve_arg_range(ctx, raw, 4, &source_args)) return !eval_should_stop(ctx);
    }

    size_t max_specs = 0;
    for (size_t i = spec_index; i < spec_index + 3 && i < arena_arr_len(*raw); i++) {
        String_View raw_list = parse_eval_arg_single(ctx, &(*raw)[i]);
        if (eval_should_stop(ctx)) return false;
        if (raw_list.count == 0) continue;

        SV_List split = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), raw_list, &split)) return !eval_should_stop(ctx);
        max_specs += arena_arr_len(split);
    }

    Parse_Keyword_Spec *specs = NULL;
    if (max_specs > 0) {
        specs = arena_alloc_array_zero(eval_temp_arena(ctx), Parse_Keyword_Spec, max_specs);
        EVAL_OOM_RETURN_IF_NULL(ctx, specs, false);
    }
    size_t spec_count = 0;

    if (!parse_add_keyword_list(ctx, node, &(*raw)[spec_index + 0], PARSE_KEYWORD_OPTION, specs, &spec_count, max_specs)) {
        return !eval_should_stop(ctx);
    }
    if (!parse_add_keyword_list(ctx, node, &(*raw)[spec_index + 1], PARSE_KEYWORD_ONE, specs, &spec_count, max_specs)) {
        return !eval_should_stop(ctx);
    }
    if (!parse_add_keyword_list(ctx, node, &(*raw)[spec_index + 2], PARSE_KEYWORD_MULTI, specs, &spec_count, max_specs)) {
        return !eval_should_stop(ctx);
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
                if (!eval_sv_arr_push_temp(ctx, &missing, active->name)) return false;
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
                if (!eval_sv_arr_push_temp(ctx, &active->multi_values, token)) return false;
                active->multi_defined = true;
                active_has_value = true;
                continue;
            }
        }

        if (!eval_sv_arr_push_temp(ctx, &unparsed, token)) return false;
    }

    if (active && !active_has_value) {
        if (!eval_sv_arr_push_temp(ctx, &missing, active->name)) return false;
    }

    if (!parse_assign_results(ctx, prefix, specs, spec_count, &unparsed, &missing)) return false;
    return !eval_should_stop(ctx);
}

static bool parse_env_var_name(String_View token, String_View *out_name) {
    if (!out_name) return false;
    *out_name = (String_View){0};
    if (token.count < 6) return false; // ENV{a}
    if (memcmp(token.data, "ENV{", 4) != 0) return false;
    if (token.data[token.count - 1] != '}') return false;
    *out_name = nob_sv_from_parts(token.data + 4, token.count - 5);
    return true;
}

static bool parse_cache_type(String_View type, String_View *out_upper_type, bool *out_is_internal) {
    if (!out_upper_type || !out_is_internal) return false;
    *out_upper_type = nob_sv_from_cstr("");
    *out_is_internal = false;
    if (type.count == 0) return false;

    char tmp[32];
    if (type.count >= sizeof(tmp)) return false;
    for (size_t i = 0; i < type.count; i++) tmp[i] = (char)toupper((unsigned char)type.data[i]);
    tmp[type.count] = '\0';

    if (strcmp(tmp, "BOOL") == 0) {
        *out_upper_type = nob_sv_from_cstr("BOOL");
        return true;
    }
    if (strcmp(tmp, "FILEPATH") == 0) {
        *out_upper_type = nob_sv_from_cstr("FILEPATH");
        return true;
    }
    if (strcmp(tmp, "PATH") == 0) {
        *out_upper_type = nob_sv_from_cstr("PATH");
        return true;
    }
    if (strcmp(tmp, "STRING") == 0) {
        *out_upper_type = nob_sv_from_cstr("STRING");
        return true;
    }
    if (strcmp(tmp, "INTERNAL") == 0) {
        *out_upper_type = nob_sv_from_cstr("INTERNAL");
        *out_is_internal = true;
        return true;
    }
    return false;
}

static bool cache_type_is_path_like(String_View cache_type) {
    return eval_sv_eq_ci_lit(cache_type, "PATH") || eval_sv_eq_ci_lit(cache_type, "FILEPATH");
}

static bool cache_promote_untyped_path_value_if_needed(Evaluator_Context *ctx,
                                                        Eval_Cache_Entry *entry,
                                                        String_View cache_type) {
    if (!ctx || !entry) return false;
    if (entry->value.type.count != 0) return true;
    if (!cache_type_is_path_like(cache_type)) return true;
    if (entry->value.data.count == 0) return true;
    if (eval_sv_is_abs_path(entry->value.data)) return true;

    const char *cwd_c = nob_get_current_dir_temp();
    if (!cwd_c || cwd_c[0] == '\0') return true;

    String_View cwd = nob_sv_from_cstr(cwd_c);
    String_View abs = eval_sv_path_join(eval_event_arena(ctx), cwd, entry->value.data);
    if (entry->value.data.count > 0 && abs.count == 0) return ctx_oom(ctx);
    entry->value.data = abs;
    return !eval_should_stop(ctx);
}

static Eval_Cache_Entry *cache_find(Evaluator_Context *ctx, String_View key) {
    if (!ctx || !ctx->cache_entries) return NULL;
    return stbds_shgetp_null(ctx->cache_entries, nob_temp_sv_to_cstr(key));
}

static bool cache_remove(Evaluator_Context *ctx, String_View key) {
    if (!ctx || !ctx->cache_entries) return true;
    (void)stbds_shdel(ctx->cache_entries, nob_temp_sv_to_cstr(key));
    return true;
}

static char *cache_copy_key_cstr(Evaluator_Context *ctx, String_View key) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_event_arena(ctx), key.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (key.count > 0) memcpy(buf, key.data, key.count);
    buf[key.count] = '\0';
    return buf;
}

static bool cache_upsert(Evaluator_Context *ctx,
                         String_View key,
                         String_View value,
                         String_View type,
                         String_View doc) {
    if (!ctx) return false;

    Eval_Cache_Entry *entry = cache_find(ctx, key);
    if (entry) {
        entry->value.data = sv_copy_to_event_arena(ctx, value);
        entry->value.type = sv_copy_to_event_arena(ctx, type);
        entry->value.doc = sv_copy_to_event_arena(ctx, doc);
        return !eval_should_stop(ctx);
    }

    char *stable_key = cache_copy_key_cstr(ctx, key);
    EVAL_OOM_RETURN_IF_NULL(ctx, stable_key, false);
    Eval_Cache_Value cv = {0};
    cv.data = sv_copy_to_event_arena(ctx, value);
    cv.type = sv_copy_to_event_arena(ctx, type);
    cv.doc = sv_copy_to_event_arena(ctx, doc);
    if (eval_should_stop(ctx)) return false;

    Eval_Cache_Entry *entries = ctx->cache_entries;
    stbds_shput(entries, stable_key, cv);
    ctx->cache_entries = entries;
    return true;
}

static bool scope_has_normal_binding(Evaluator_Context *ctx, String_View key) {
    if (eval_scope_visible_depth(ctx) == 0 || key.count == 0) return false;
    for (size_t depth = eval_scope_visible_depth(ctx); depth-- > 0;) {
        Var_Scope *scope = &ctx->scopes[depth];
        if (!scope->vars) continue;
        if (stbds_shgetp_null(scope->vars, nob_temp_sv_to_cstr(key)) != NULL) return true;
    }
    return false;
}

static bool unset_visible_normal_binding(Evaluator_Context *ctx, String_View key) {
    if (eval_scope_visible_depth(ctx) == 0 || key.count == 0) return false;
    for (size_t depth = eval_scope_visible_depth(ctx); depth-- > 0;) {
        Var_Scope *scope = &ctx->scopes[depth];
        if (!scope->vars) continue;
        if (stbds_shgetp_null(scope->vars, nob_temp_sv_to_cstr(key)) == NULL) continue;
        (void)stbds_shdel(scope->vars, nob_temp_sv_to_cstr(key));
        return true;
    }
    return true;
}

static bool emit_cache_entry_write(Evaluator_Context *ctx,
                                   Cmake_Event_Origin origin,
                                   String_View key,
                                   String_View value) {
    if (!ctx) return false;
    Cmake_Event ev = {0};
    ev.kind = EV_SET_CACHE_ENTRY;
    ev.origin = origin;
    ev.as.cache_entry.key = sv_copy_to_event_arena(ctx, key);
    ev.as.cache_entry.value = sv_copy_to_event_arena(ctx, value);
    return emit_event(ctx, ev);
}

static bool option_cache_write(Evaluator_Context *ctx,
                               Cmake_Event_Origin origin,
                               String_View key,
                               String_View value,
                               String_View doc) {
    if (!cache_upsert(ctx, key, value, nob_sv_from_cstr("BOOL"), doc)) return false;
    return emit_cache_entry_write(ctx, origin, key, value);
}

static bool mark_as_advanced_apply(Evaluator_Context *ctx,
                                   String_View var_name,
                                   bool clear_mode) {
    if (!ctx || var_name.count == 0) return false;
    String_View prop_key = eval_property_store_key_temp(ctx,
                                                        nob_sv_from_cstr("CACHE"),
                                                        var_name,
                                                        nob_sv_from_cstr("ADVANCED"));
    if (eval_should_stop(ctx)) return false;
    return eval_var_set(ctx, prop_key, clear_mode ? nob_sv_from_cstr("0")
                                                  : nob_sv_from_cstr("1"));
}

static bool join_sv_with_spaces_temp(Evaluator_Context *ctx,
                                     String_View *items,
                                     size_t count,
                                     String_View *out_value) {
    if (!out_value) return false;
    *out_value = nob_sv_from_cstr("");
    if (!ctx) return false;
    if (count == 0) return true;

    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += items[i].count;
    if (count > 1) total += count - 1;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0) buf[off++] = ' ';
        if (items[i].count > 0) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
    }
    buf[off] = '\0';
    *out_value = nob_sv_from_parts(buf, off);
    return true;
}

static bool separate_arguments_parse_tokens(Evaluator_Context *ctx,
                                            bool windows_mode,
                                            String_View input,
                                            SV_List *out) {
    return eval_split_command_line_temp(ctx,
                                        windows_mode ? EVAL_CMDLINE_WINDOWS : EVAL_CMDLINE_UNIX,
                                        input,
                                        out);
}

static bool set_process_env(Evaluator_Context *ctx, String_View name, String_View value) {
    if (!ctx) return false;
    char *name_c = eval_sv_to_cstr_temp(ctx, name);
    EVAL_OOM_RETURN_IF_NULL(ctx, name_c, false);
    char *value_c = eval_sv_to_cstr_temp(ctx, value);
    EVAL_OOM_RETURN_IF_NULL(ctx, value_c, false);

#if defined(_WIN32)
    return _putenv_s(name_c, value_c) == 0;
#else
    return setenv(name_c, value_c, 1) == 0;
#endif
}

static bool unset_process_env(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return false;
    char *name_c = eval_sv_to_cstr_temp(ctx, name);
    EVAL_OOM_RETURN_IF_NULL(ctx, name_c, false);

#if defined(_WIN32)
    return _putenv_s(name_c, "") == 0;
#else
    return unsetenv(name_c) == 0;
#endif
}

bool eval_handle_set(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || arena_arr_len(a) == 0) return !eval_should_stop(ctx);

    String_View var = a[0];
    String_View env_name = {0};
    if (parse_env_var_name(var, &env_name)) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);

        // CMake: set(ENV{var} [value]) uses only first value arg and warns on extras.
        String_View env_value = (arena_arr_len(a) >= 2) ? a[1] : nob_sv_from_cstr("");
        if (arena_arr_len(a) > 2) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_WARNING, "set", nob_sv_from_cstr("set(ENV{...}) ignores extra arguments after value"),
                                 nob_sv_from_cstr("Only the first value argument is used"));
        }

        if (!set_process_env(ctx, env_name, env_value)) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "set", nob_sv_from_cstr("Failed to set environment variable"),
                                 env_name);
        }
        return !eval_should_stop(ctx);
    }

    size_t cache_idx = arena_arr_len(a);
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "CACHE")) {
            cache_idx = i;
            break;
        }
    }

    if (cache_idx < arena_arr_len(a)) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        if (cache_idx + 2 >= arena_arr_len(a)) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "set", nob_sv_from_cstr("set(... CACHE ...) requires <type> and <docstring>"),
                                 nob_sv_from_cstr("Usage: set(<var> <value>... CACHE <type> <doc> [FORCE])"));
            return !eval_should_stop(ctx);
        }

        String_View cache_type_raw = a[cache_idx + 1];
        String_View cache_type = {0};
        bool is_internal = false;
        if (!parse_cache_type(cache_type_raw, &cache_type, &is_internal)) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "set", nob_sv_from_cstr("set(... CACHE ...) received invalid cache type"),
                                 cache_type_raw);
            return !eval_should_stop(ctx);
        }

        String_View cache_doc = a[cache_idx + 2];
        bool force = false;
        if (cache_idx + 3 < arena_arr_len(a)) {
            if (cache_idx + 3 == arena_arr_len(a) - 1 && eval_sv_eq_ci_lit(a[cache_idx + 3], "FORCE")) {
                force = true;
            } else {
                (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "set", nob_sv_from_cstr("set(... CACHE ...) received unsupported trailing arguments"),
                                     nob_sv_from_cstr("Only optional FORCE is accepted after <docstring>"));
                return !eval_should_stop(ctx);
            }
        }
        if (is_internal) force = true;

        String_View value = nob_sv_from_cstr("");
        if (cache_idx > 1) value = eval_sv_join_semi_temp(ctx, &a[1], cache_idx - 1);

        Eval_Cache_Entry *existing = cache_find(ctx, var);
        bool should_write_cache = (existing == NULL) || force;
        bool cmp0126_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0126);
        bool remove_local_binding_old = (existing == NULL) || (existing && existing->value.type.count == 0) || force || is_internal;

        if (!cmp0126_new && remove_local_binding_old) (void)eval_var_unset(ctx, var);

        if (should_write_cache) {
            if (!cache_upsert(ctx, var, value, cache_type, cache_doc)) return false;

            Cmake_Event ce = {0};
            ce.kind = EV_SET_CACHE_ENTRY;
            ce.origin = o;
            ce.as.cache_entry.key = sv_copy_to_event_arena(ctx, var);
            ce.as.cache_entry.value = sv_copy_to_event_arena(ctx, value);
            if (!emit_event(ctx, ce)) return false;
        } else if (existing->value.type.count == 0) {
            if (!cache_promote_untyped_path_value_if_needed(ctx, existing, cache_type)) return false;
            existing->value.type = sv_copy_to_event_arena(ctx, cache_type);
            existing->value.doc = sv_copy_to_event_arena(ctx, cache_doc);
        }

        return !eval_should_stop(ctx);
    }

    if (arena_arr_len(a) == 1) {
        (void)eval_var_unset(ctx, var);
        return !eval_should_stop(ctx);
    }

    bool parent_scope = false;

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PARENT_SCOPE")) {
            parent_scope = true;
            break;
        }
    }

    size_t val_end = arena_arr_len(a);
    if (parent_scope) val_end--;

    String_View value = nob_sv_from_cstr("");
    if (val_end > 1) value = eval_sv_join_semi_temp(ctx, &a[1], val_end - 1);

    if (parent_scope) {
        if (eval_scope_visible_depth(ctx) > 1) {
            size_t saved_depth = 0;
            if (!eval_scope_enter_parent(ctx, &saved_depth)) return false;
            bool ok = eval_var_set(ctx, var, value);
            eval_scope_leave(ctx, saved_depth);
            if (!ok) return false;
        }
        else {
            Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
            if (!EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "set", nob_sv_from_cstr("PARENT_SCOPE used without a parent scope"),
                                nob_sv_from_cstr("Use PARENT_SCOPE only inside function/subscope"))) {
                return false;
            }
        }
    } else {
        (void)eval_var_set(ctx, var, value);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_unset(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(a) == 0) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "unset", nob_sv_from_cstr("unset() requires variable name"),
                             nob_sv_from_cstr("Usage: unset(<var> [CACHE|PARENT_SCOPE])"));
        return !eval_should_stop(ctx);
    }
    if (arena_arr_len(a) > 2) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "unset", nob_sv_from_cstr("unset() accepts at most one option"),
                             nob_sv_from_cstr("Supported options: CACHE, PARENT_SCOPE"));
        return !eval_should_stop(ctx);
    }

    String_View var = a[0];
    String_View env_name = {0};
    if (parse_env_var_name(var, &env_name)) {
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        if (arena_arr_len(a) > 1) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "unset", nob_sv_from_cstr("unset(ENV{...}) does not accept options"),
                                 nob_sv_from_cstr("Usage: unset(ENV{<var>})"));
            return !eval_should_stop(ctx);
        }
        if (!unset_process_env(ctx, env_name)) {
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "unset", nob_sv_from_cstr("Failed to unset environment variable"),
                                 env_name);
        }
        return !eval_should_stop(ctx);
    }

    bool cache_mode = false;
    bool parent_scope = false;
    if (arena_arr_len(a) == 2) {
        if (eval_sv_eq_ci_lit(a[1], "CACHE")) {
            cache_mode = true;
        } else if (eval_sv_eq_ci_lit(a[1], "PARENT_SCOPE")) {
            parent_scope = true;
        } else {
            Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "unset", nob_sv_from_cstr("unset() received unsupported option"),
                                 a[1]);
            return !eval_should_stop(ctx);
        }
    }

    if (cache_mode) {
        // Pragmatic cache behavior: emit a cache-entry mutation to empty value.
        Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
        (void)cache_remove(ctx, var);
        Cmake_Event ce = {0};
        ce.kind = EV_SET_CACHE_ENTRY;
        ce.origin = o;
        ce.as.cache_entry.key = sv_copy_to_event_arena(ctx, var);
        ce.as.cache_entry.value = sv_copy_to_event_arena(ctx, nob_sv_from_cstr(""));
        if (!emit_event(ctx, ce)) return false;
        return !eval_should_stop(ctx);
    }

    if (parent_scope) {
        if (eval_scope_visible_depth(ctx) <= 1) {
            Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
            (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "unset", nob_sv_from_cstr("PARENT_SCOPE used without a parent scope"),
                                 nob_sv_from_cstr("Use PARENT_SCOPE only inside function/subscope"));
            return !eval_should_stop(ctx);
        }

        size_t saved_depth = 0;
        if (!eval_scope_enter_parent(ctx, &saved_depth)) return false;
        bool ok = eval_var_unset(ctx, var);
        eval_scope_leave(ctx, saved_depth);
        if (!ok) return false;
        return !eval_should_stop(ctx);
    }

    (void)eval_var_unset(ctx, var);
    return !eval_should_stop(ctx);
}

bool eval_handle_option(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 2 || arena_arr_len(a) > 3) {
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "option", nob_sv_from_cstr("option() requires <variable> <help_text> [value]"),
                             nob_sv_from_cstr("Usage: option(<variable> <help_text> [value])"));
        return !eval_should_stop(ctx);
    }

    String_View var = a[0];
    if (var.count == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "option", nob_sv_from_cstr("option() requires a non-empty variable name"),
                             nob_sv_from_cstr("Provide a cache variable identifier"));
        return !eval_should_stop(ctx);
    }

    String_View doc = a[1];
    String_View value = (arena_arr_len(a) >= 3) ? a[2] : nob_sv_from_cstr("OFF");

    bool cmp0077_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0077);
    bool has_normal_binding = scope_has_normal_binding(ctx, var);
    Eval_Cache_Entry *existing = cache_find(ctx, var);
    bool has_typed_cache = existing && existing->value.type.count > 0;

    if (cmp0077_new && has_normal_binding) return !eval_should_stop(ctx);

    if (!cmp0077_new && has_normal_binding) {
        if (!unset_visible_normal_binding(ctx, var)) return false;
    }

    if (has_typed_cache) return !eval_should_stop(ctx);

    if (!option_cache_write(ctx, o, var, value, doc)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

bool eval_handle_mark_as_advanced(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    bool clear_mode = false;
    size_t start = 0;
    if (arena_arr_len(a) > 0 && eval_sv_eq_ci_lit(a[0], "CLEAR")) {
        clear_mode = true;
        start = 1;
    } else if (arena_arr_len(a) > 0 && eval_sv_eq_ci_lit(a[0], "FORCE")) {
        start = 1;
    }

    if (start >= arena_arr_len(a)) {
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "mark_as_advanced", nob_sv_from_cstr("mark_as_advanced() requires at least one variable name"),
                             nob_sv_from_cstr("Usage: mark_as_advanced([CLEAR|FORCE] <var>...)"));
        return !eval_should_stop(ctx);
    }

    bool cmp0102_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0102);
    for (size_t i = start; i < arena_arr_len(a); i++) {
        String_View var_name = a[i];
        if (var_name.count == 0) continue;

        if (!eval_cache_defined(ctx, var_name)) {
            if (cmp0102_new) continue;
            if (!cache_upsert(ctx,
                              var_name,
                              nob_sv_from_cstr(""),
                              nob_sv_from_cstr("UNINITIALIZED"),
                              nob_sv_from_cstr(""))) {
                return !eval_should_stop(ctx);
            }
            if (!emit_cache_entry_write(ctx, o, var_name, nob_sv_from_cstr(""))) return !eval_should_stop(ctx);
        }

        if (!mark_as_advanced_apply(ctx, var_name, clear_mode)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_separate_arguments(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "separate_arguments", nob_sv_from_cstr("separate_arguments() requires an output variable"),
                             nob_sv_from_cstr("Usage: separate_arguments(<var> [UNIX_COMMAND|WINDOWS_COMMAND|NATIVE_COMMAND] <args>...)"));
        return !eval_should_stop(ctx);
    }

    String_View out_var = a[0];
    bool windows_mode =
#if defined(_WIN32)
        true;
#else
        false;
#endif
    bool explicit_mode = false;
    size_t input_index = 1;

    if (arena_arr_len(a) > 1) {
        if (eval_sv_eq_ci_lit(a[1], "UNIX_COMMAND")) {
            windows_mode = false;
            explicit_mode = true;
            input_index = 2;
        } else if (eval_sv_eq_ci_lit(a[1], "WINDOWS_COMMAND")) {
            windows_mode = true;
            explicit_mode = true;
            input_index = 2;
        } else if (eval_sv_eq_ci_lit(a[1], "NATIVE_COMMAND")) {
#if defined(_WIN32)
            windows_mode = true;
#else
            windows_mode = false;
#endif
            explicit_mode = true;
            input_index = 2;
        }
    }

    if (explicit_mode && input_index >= arena_arr_len(a)) {
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "separate_arguments", nob_sv_from_cstr("separate_arguments() mode form requires an input command line"),
                             nob_sv_from_cstr("Add the command string after the parsing mode"));
        return !eval_should_stop(ctx);
    }

    if (explicit_mode && input_index < arena_arr_len(a) && eval_sv_eq_ci_lit(a[input_index], "PROGRAM")) {
        (void)EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "separate_arguments", nob_sv_from_cstr("separate_arguments(PROGRAM ...) is not implemented yet"),
                             nob_sv_from_cstr("Supported in this batch: UNIX_COMMAND, WINDOWS_COMMAND, NATIVE_COMMAND, and one-argument list form"));
        return !eval_should_stop(ctx);
    }

    String_View input = nob_sv_from_cstr("");
    if (arena_arr_len(a) == 1) {
        input = eval_var_get(ctx, out_var);
    } else {
        if (!join_sv_with_spaces_temp(ctx, &a[input_index], arena_arr_len(a) - input_index, &input)) {
            return !eval_should_stop(ctx);
        }
    }

    SV_List tokens = NULL;
    if (!separate_arguments_parse_tokens(ctx, windows_mode, input, &tokens)) return !eval_should_stop(ctx);
    if (!eval_var_set(ctx, out_var, eval_sv_join_semi_temp(ctx, tokens, arena_arr_len(tokens)))) {
        return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_load_cache(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (arena_arr_len(a) < 2) {
        (void)load_cache_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("load_cache() requires a path and a supported mode"),
                                   nob_sv_from_cstr("Usage: load_cache(<path> READ_WITH_PREFIX <prefix> <entry>...)"));
        return !eval_should_stop(ctx);
    }

    String_View cache_path = nob_sv_from_cstr("");
    if (!load_cache_resolve_path(ctx, a[0], &cache_path)) return !eval_should_stop(ctx);
    char *cache_path_c = eval_sv_to_cstr_temp(ctx, cache_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, cache_path_c, !eval_should_stop(ctx));

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(cache_path_c, &sb)) {
        (void)load_cache_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("load_cache() failed to read CMakeCache.txt"),
                                   cache_path);
        return !eval_should_stop(ctx);
    }

    bool read_with_prefix = false;
    String_View prefix = nob_sv_from_cstr("");
    SV_List requested = NULL;
    SV_List excludes = NULL;
    SV_List include_internals = NULL;

    size_t i = 1;
    if (eval_sv_eq_ci_lit(a[i], "READ_WITH_PREFIX")) {
        read_with_prefix = true;
        if (i + 2 >= arena_arr_len(a)) {
            (void)load_cache_emit_diag(ctx,
                                       node,
                                       EV_DIAG_ERROR,
                                       nob_sv_from_cstr("load_cache(READ_WITH_PREFIX ...) requires a prefix and at least one entry"),
                                       nob_sv_from_cstr("Usage: load_cache(<path> READ_WITH_PREFIX <prefix> <entry>...)"));
            nob_sb_free(sb);
            return !eval_should_stop(ctx);
        }
        prefix = a[i + 1];
        i += 2;
        for (; i < arena_arr_len(a); i++) {
            if (!eval_sv_arr_push_temp(ctx, &requested, a[i])) {
                nob_sb_free(sb);
                return !eval_should_stop(ctx);
            }
        }
    } else {
        while (i < arena_arr_len(a)) {
            if (eval_sv_eq_ci_lit(a[i], "EXCLUDE")) {
                i++;
                while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "INCLUDE_INTERNALS")) {
                    if (!eval_sv_arr_push_temp(ctx, &excludes, a[i])) {
                        nob_sb_free(sb);
                        return !eval_should_stop(ctx);
                    }
                    i++;
                }
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "INCLUDE_INTERNALS")) {
                i++;
                while (i < arena_arr_len(a)) {
                    if (!eval_sv_arr_push_temp(ctx, &include_internals, a[i])) {
                        nob_sb_free(sb);
                        return !eval_should_stop(ctx);
                    }
                    i++;
                }
                break;
            }
            (void)load_cache_emit_diag(ctx,
                                       node,
                                       EV_DIAG_ERROR,
                                       nob_sv_from_cstr("load_cache() received an unsupported argument"),
                                       a[i]);
            nob_sb_free(sb);
            return !eval_should_stop(ctx);
        }
    }

    const char *data = sb.items ? sb.items : "";
    size_t len = sb.count;
    size_t start = 0;
    while (start <= len) {
        size_t end = start;
        while (end < len && data[end] != '\n') end++;
        size_t line_len = end - start;
        if (line_len > 0 && data[start + line_len - 1] == '\r') line_len--;
        String_View line = nob_sv_from_parts(data + start, line_len);

        String_View key = {0};
        String_View type = {0};
        String_View value = {0};
        if (line.count > 0 && line.data[0] != '#' && line.data[0] != '/') {
            if (!load_cache_parse_line(line, &key, &type, &value)) {
                (void)load_cache_emit_diag(ctx,
                                           node,
                                           EV_DIAG_WARNING,
                                           nob_sv_from_cstr("load_cache() skipped a malformed cache entry"),
                                           line);
            } else {
                bool is_internal = eval_sv_eq_ci_lit(type, "INTERNAL");
                if (read_with_prefix) {
                    if (load_cache_name_in_list(key, &requested)) {
                        size_t total = prefix.count + key.count;
                        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
                        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));
                        if (prefix.count > 0) memcpy(buf, prefix.data, prefix.count);
                        memcpy(buf + prefix.count, key.data, key.count);
                        buf[total] = '\0';
                        if (!eval_var_set(ctx, nob_sv_from_parts(buf, total), value)) {
                            nob_sb_free(sb);
                            return !eval_should_stop(ctx);
                        }
                    }
                } else {
                    if (!is_internal || load_cache_name_in_list(key, &include_internals)) {
                        if (!load_cache_name_in_list(key, &excludes)) {
                            if (!eval_cache_set(ctx, key, value, nob_sv_from_cstr("INTERNAL"), nob_sv_from_cstr("loaded by load_cache"))) {
                                nob_sb_free(sb);
                                return !eval_should_stop(ctx);
                            }
                        }
                    }
                }
            }
        }

        if (end == len) break;
        start = end + 1;
    }

    nob_sb_free(sb);
    return !eval_should_stop(ctx);
}
