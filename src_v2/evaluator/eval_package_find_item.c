#include "eval_package_internal.h"

typedef enum {
    FIND_ITEM_PROGRAM = 0,
    FIND_ITEM_FILE,
    FIND_ITEM_PATH,
    FIND_ITEM_LIBRARY,
} Find_Item_Kind;

typedef enum {
    FIND_ROOT_MODE_BOTH = 0,
    FIND_ROOT_MODE_ONLY,
    FIND_ROOT_MODE_NONE,
} Find_Root_Mode;

typedef struct {
    String_View out_var;
    SV_List names;
    SV_List hints;
    SV_List paths;
    SV_List path_suffixes;
    bool required;
    bool no_cache;
    bool names_per_dir;
    bool no_default_path;
    bool no_package_root_path;
    bool no_cmake_path;
    bool no_cmake_environment_path;
    bool no_system_environment_path;
    bool no_cmake_system_path;
    bool no_cmake_install_prefix;
    bool has_validator;
    String_View validator;
    bool has_registry_view;
    String_View registry_view;
    Find_Root_Mode root_mode;
} Find_Item_Options;

typedef enum {
    FIND_ITEM_OPT_NAMES = 0,
    FIND_ITEM_OPT_HINTS,
    FIND_ITEM_OPT_PATHS,
    FIND_ITEM_OPT_PATH_SUFFIXES,
    FIND_ITEM_OPT_DOC,
    FIND_ITEM_OPT_NO_CACHE,
    FIND_ITEM_OPT_REQUIRED,
    FIND_ITEM_OPT_NO_DEFAULT_PATH,
    FIND_ITEM_OPT_NO_PACKAGE_ROOT_PATH,
    FIND_ITEM_OPT_NO_CMAKE_PATH,
    FIND_ITEM_OPT_NO_CMAKE_ENVIRONMENT_PATH,
    FIND_ITEM_OPT_NO_SYSTEM_ENVIRONMENT_PATH,
    FIND_ITEM_OPT_NO_CMAKE_SYSTEM_PATH,
    FIND_ITEM_OPT_NO_CMAKE_INSTALL_PREFIX,
    FIND_ITEM_OPT_NO_CMAKE_FIND_ROOT_PATH,
    FIND_ITEM_OPT_ONLY_CMAKE_FIND_ROOT_PATH,
    FIND_ITEM_OPT_CMAKE_FIND_ROOT_PATH_BOTH,
    FIND_ITEM_OPT_NAMES_PER_DIR,
    FIND_ITEM_OPT_REGISTRY_VIEW,
    FIND_ITEM_OPT_VALIDATOR,
} Find_Item_Option_Id;

typedef struct {
    const Node *node;
    Find_Item_Options *out_opt;
    Cmake_Event_Origin origin;
    bool keyword_seen;
    SV_List args;
} Find_Item_Option_Parse_State;

static bool find_item_keyword_eq(String_View value, const char *lit) {
    return eval_sv_eq_ci_lit(value, lit);
}

static bool find_item_is_keyword(String_View value) {
    return find_item_keyword_eq(value, "NAMES") ||
           find_item_keyword_eq(value, "HINTS") ||
           find_item_keyword_eq(value, "PATHS") ||
           find_item_keyword_eq(value, "PATH_SUFFIXES") ||
           find_item_keyword_eq(value, "DOC") ||
           find_item_keyword_eq(value, "NO_CACHE") ||
           find_item_keyword_eq(value, "REQUIRED") ||
           find_item_keyword_eq(value, "NO_DEFAULT_PATH") ||
           find_item_keyword_eq(value, "NO_PACKAGE_ROOT_PATH") ||
           find_item_keyword_eq(value, "NO_CMAKE_PATH") ||
           find_item_keyword_eq(value, "NO_CMAKE_ENVIRONMENT_PATH") ||
           find_item_keyword_eq(value, "NO_SYSTEM_ENVIRONMENT_PATH") ||
           find_item_keyword_eq(value, "NO_CMAKE_SYSTEM_PATH") ||
           find_item_keyword_eq(value, "NO_CMAKE_INSTALL_PREFIX") ||
           find_item_keyword_eq(value, "NO_CMAKE_FIND_ROOT_PATH") ||
           find_item_keyword_eq(value, "ONLY_CMAKE_FIND_ROOT_PATH") ||
           find_item_keyword_eq(value, "CMAKE_FIND_ROOT_PATH_BOTH") ||
           find_item_keyword_eq(value, "NAMES_PER_DIR") ||
           find_item_keyword_eq(value, "REGISTRY_VIEW") ||
           find_item_keyword_eq(value, "VALIDATOR");
}

static bool find_item_list_append(Evaluator_Context *ctx, SV_List *list, String_View item) {
    if (!ctx || !list) return false;
    if (item.count == 0) return true;
    if (!svu_list_push_temp(ctx, list, item)) return false;
    return true;
}

static bool find_item_list_append_env(Evaluator_Context *ctx, SV_List *list, String_View env_name) {
    if (!ctx || !list || env_name.count == 0) return false;
    char *env_key = eval_sv_to_cstr_temp(ctx, env_name);
    EVAL_OOM_RETURN_IF_NULL(ctx, env_key, false);

    const char *raw = eval_getenv_temp(ctx, env_key);
    if (!raw || raw[0] == '\0') return true;

    String_View value = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(raw));
    if (eval_should_stop(ctx)) return false;

#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif

    const char *p = value.data;
    const char *end = value.data + value.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != sep) q++;
        if (!find_item_list_append(ctx, list, nob_sv_from_parts(p, (size_t)(q - p)))) return false;
        if (q >= end) break;
        p = q + 1;
    }
    return true;
}

static bool find_item_collect_multi(Evaluator_Context *ctx,
                                    const Node *node,
                                    const SV_List *args,
                                    size_t *io_index,
                                    SV_List *out_values) {
    if (!ctx || !node || !args || !io_index || !out_values) return false;
    while (*io_index < arena_arr_len(*args)) {
        String_View value = (*args)[*io_index];
        if (find_item_is_keyword(value)) break;
        if (find_item_keyword_eq(value, "ENV")) {
            (*io_index)++;
            if (*io_index >= arena_arr_len(*args) || find_item_is_keyword((*args)[*io_index])) {
                Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
                (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                                     node,
                                                     o,
                                                     EV_DIAG_ERROR,
                                                     EVAL_DIAG_MISSING_REQUIRED,
                                                     "dispatcher",
                                                     nob_sv_from_cstr("find_*(ENV) requires an environment variable name"),
                                                     nob_sv_from_cstr("Usage: find_<cmd>(... <section> ENV <env-var> ...)"));
                return false;
            }
            if (!find_item_list_append_env(ctx, out_values, (*args)[*io_index])) return false;
            (*io_index)++;
            continue;
        }
        if (!find_item_list_append(ctx, out_values, value)) return false;
        (*io_index)++;
    }
    return true;
}

static bool find_item_validate_out_var(Evaluator_Context *ctx,
                                       const Node *node,
                                       const SV_List args,
                                       Find_Item_Options *out_opt) {
    if (!ctx || !node || !out_opt) return false;
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);

    if (arena_arr_len(args) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                              node,
                                              o,
                                              EV_DIAG_ERROR,
                                              EVAL_DIAG_MISSING_REQUIRED,
                                              "dispatcher",
                                              nob_sv_from_cstr("find_*() requires an output variable"),
                                              nob_sv_from_cstr("Usage: find_<cmd>(<out-var> name1 [name2 ...] [NAMES ...] [HINTS ...] [PATHS ...])"));
        return false;
    }

    out_opt->out_var = args[0];
    if (out_opt->out_var.count == 0 || find_item_is_keyword(out_opt->out_var)) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                              node,
                                              o,
                                              EV_DIAG_ERROR,
                                              EVAL_DIAG_MISSING_REQUIRED,
                                              "dispatcher",
                                              nob_sv_from_cstr("find_*() requires an output variable"),
                                              nob_sv_from_cstr("Usage: find_<cmd>(<out-var> name1 [name2 ...] [NAMES ...] [HINTS ...] [PATHS ...])"));
        return false;
    }

    SV_List raw_args = eval_resolve_args_literal(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(raw_args) == 0 || !nob_sv_eq(raw_args[0], out_opt->out_var)) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                              node,
                                              o,
                                              EV_DIAG_ERROR,
                                              EVAL_DIAG_MISSING_REQUIRED,
                                              "dispatcher",
                                              nob_sv_from_cstr("find_*() requires output variable as a single token"),
                                              nob_sv_from_cstr("Usage: find_<cmd>(<out-var> name1 [name2 ...] [NAMES ...] [HINTS ...] [PATHS ...])"));
        return false;
    }

    if (arena_arr_len(args) < 2) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                              node,
                                              o,
                                              EV_DIAG_ERROR,
                                              EVAL_DIAG_MISSING_REQUIRED,
                                              "dispatcher",
                                              nob_sv_from_cstr("find_*() requires at least one search name"),
                                              nob_sv_from_cstr("Usage: find_<cmd>(<out-var> name1 [name2 ...] [NAMES ...] [HINTS ...] [PATHS ...])"));
        return false;
    }
    return true;
}

static bool find_item_parse_positional(Evaluator_Context *ctx,
                                       void *userdata,
                                       String_View value,
                                       size_t token_index) {
    (void)token_index;
    Find_Item_Option_Parse_State *state = (Find_Item_Option_Parse_State *)userdata;
    if (!state || !state->out_opt || !state->node) return false;
    if (state->keyword_seen) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                              state->node,
                                              state->origin,
                                              EV_DIAG_ERROR,
                                              EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                              "dispatcher",
                                              nob_sv_from_cstr("find_*() received unknown option"),
                                              nob_sv_from_cstr("Usage: find_<cmd>(<out-var> NAMES ... [HINTS ...] [PATHS ...])"));
        return false;
    }
    return find_item_list_append(ctx, &state->out_opt->names, value);
}

static bool find_item_parse_option(Evaluator_Context *ctx,
                                   void *userdata,
                                   int id,
                                   SV_List values,
                                   size_t token_index) {
    Find_Item_Option_Parse_State *state = (Find_Item_Option_Parse_State *)userdata;
    if (!state || !state->out_opt || !state->node) return false;
    state->keyword_seen = true;

    switch (id) {
        case FIND_ITEM_OPT_NAMES: {
            size_t i = token_index + 1;
            if (!find_item_collect_multi(ctx, state->node, &state->args, &i, &state->out_opt->names)) return false;
            return true;
        }
        case FIND_ITEM_OPT_HINTS: {
            size_t i = token_index + 1;
            if (!find_item_collect_multi(ctx, state->node, &state->args, &i, &state->out_opt->hints)) return false;
            return true;
        }
        case FIND_ITEM_OPT_PATHS: {
            size_t i = token_index + 1;
            if (!find_item_collect_multi(ctx, state->node, &state->args, &i, &state->out_opt->paths)) return false;
            return true;
        }
        case FIND_ITEM_OPT_PATH_SUFFIXES: {
            size_t i = token_index + 1;
            if (!find_item_collect_multi(ctx, state->node, &state->args, &i, &state->out_opt->path_suffixes)) return false;
            return true;
        }
        case FIND_ITEM_OPT_DOC:
            return true;
        case FIND_ITEM_OPT_NO_CACHE:
            state->out_opt->no_cache = true;
            return true;
        case FIND_ITEM_OPT_REQUIRED:
            state->out_opt->required = true;
            return true;
        case FIND_ITEM_OPT_NO_DEFAULT_PATH:
            state->out_opt->no_default_path = true;
            return true;
        case FIND_ITEM_OPT_NO_PACKAGE_ROOT_PATH:
            state->out_opt->no_package_root_path = true;
            return true;
        case FIND_ITEM_OPT_NO_CMAKE_PATH:
            state->out_opt->no_cmake_path = true;
            return true;
        case FIND_ITEM_OPT_NO_CMAKE_ENVIRONMENT_PATH:
            state->out_opt->no_cmake_environment_path = true;
            return true;
        case FIND_ITEM_OPT_NO_SYSTEM_ENVIRONMENT_PATH:
            state->out_opt->no_system_environment_path = true;
            return true;
        case FIND_ITEM_OPT_NO_CMAKE_SYSTEM_PATH:
            state->out_opt->no_cmake_system_path = true;
            return true;
        case FIND_ITEM_OPT_NO_CMAKE_INSTALL_PREFIX:
            state->out_opt->no_cmake_install_prefix = true;
            return true;
        case FIND_ITEM_OPT_NO_CMAKE_FIND_ROOT_PATH:
            state->out_opt->root_mode = FIND_ROOT_MODE_NONE;
            return true;
        case FIND_ITEM_OPT_ONLY_CMAKE_FIND_ROOT_PATH:
            state->out_opt->root_mode = FIND_ROOT_MODE_ONLY;
            return true;
        case FIND_ITEM_OPT_CMAKE_FIND_ROOT_PATH_BOTH:
            state->out_opt->root_mode = FIND_ROOT_MODE_BOTH;
            return true;
        case FIND_ITEM_OPT_NAMES_PER_DIR:
            state->out_opt->names_per_dir = true;
            return true;
        case FIND_ITEM_OPT_REGISTRY_VIEW:
            if (arena_arr_len(values) == 0 || find_item_is_keyword(values[0])) {
                (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                                      state->node,
                                                      state->origin,
                                                      EV_DIAG_ERROR,
                                                      EVAL_DIAG_MISSING_REQUIRED,
                                                      "dispatcher",
                                                      nob_sv_from_cstr("find_*(REGISTRY_VIEW) requires a value"),
                                                      nob_sv_from_cstr("Usage: find_*(<out-var> ... REGISTRY_VIEW <view> ...)"));
                return false;
            }
            state->out_opt->has_registry_view = true;
            state->out_opt->registry_view = values[0];
            return true;
        case FIND_ITEM_OPT_VALIDATOR:
            if (arena_arr_len(values) == 0 || find_item_is_keyword(values[0])) {
                (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                                      state->node,
                                                      state->origin,
                                                      EV_DIAG_ERROR,
                                                      EVAL_DIAG_MISSING_REQUIRED,
                                                      "dispatcher",
                                                      nob_sv_from_cstr("find_*(VALIDATOR) requires a function name"),
                                                      nob_sv_from_cstr("Usage: find_*(<out-var> ... VALIDATOR <function> ...)"));
                return false;
            }
            state->out_opt->has_validator = true;
            state->out_opt->validator = values[0];
            return true;
        default:
            return false;
    }
}

static bool find_item_parse_options(Evaluator_Context *ctx,
                                    const Node *node,
                                    SV_List args,
                                    Find_Item_Options *out_opt) {
    if (!ctx || !node || !out_opt) return false;
    memset(out_opt, 0, sizeof(*out_opt));
    out_opt->root_mode = FIND_ROOT_MODE_BOTH;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (!find_item_validate_out_var(ctx, node, args, out_opt)) {
        return false;
    }

    static const Eval_Opt_Spec k_find_item_specs[] = {
        {FIND_ITEM_OPT_NAMES, "NAMES", EVAL_OPT_MULTI},
        {FIND_ITEM_OPT_HINTS, "HINTS", EVAL_OPT_MULTI},
        {FIND_ITEM_OPT_PATHS, "PATHS", EVAL_OPT_MULTI},
        {FIND_ITEM_OPT_PATH_SUFFIXES, "PATH_SUFFIXES", EVAL_OPT_MULTI},
        {FIND_ITEM_OPT_DOC, "DOC", EVAL_OPT_OPTIONAL_SINGLE},
        {FIND_ITEM_OPT_NO_CACHE, "NO_CACHE", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_REQUIRED, "REQUIRED", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NO_DEFAULT_PATH, "NO_DEFAULT_PATH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NO_PACKAGE_ROOT_PATH, "NO_PACKAGE_ROOT_PATH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NO_CMAKE_PATH, "NO_CMAKE_PATH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NO_CMAKE_ENVIRONMENT_PATH, "NO_CMAKE_ENVIRONMENT_PATH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NO_SYSTEM_ENVIRONMENT_PATH, "NO_SYSTEM_ENVIRONMENT_PATH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NO_CMAKE_SYSTEM_PATH, "NO_CMAKE_SYSTEM_PATH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NO_CMAKE_INSTALL_PREFIX, "NO_CMAKE_INSTALL_PREFIX", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NO_CMAKE_FIND_ROOT_PATH, "NO_CMAKE_FIND_ROOT_PATH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_ONLY_CMAKE_FIND_ROOT_PATH, "ONLY_CMAKE_FIND_ROOT_PATH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_CMAKE_FIND_ROOT_PATH_BOTH, "CMAKE_FIND_ROOT_PATH_BOTH", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_NAMES_PER_DIR, "NAMES_PER_DIR", EVAL_OPT_FLAG},
        {FIND_ITEM_OPT_REGISTRY_VIEW, "REGISTRY_VIEW", EVAL_OPT_OPTIONAL_SINGLE},
        {FIND_ITEM_OPT_VALIDATOR, "VALIDATOR", EVAL_OPT_OPTIONAL_SINGLE},
    };

    Find_Item_Option_Parse_State state = {
        .node = node,
        .out_opt = out_opt,
        .origin = o,
        .keyword_seen = false,
        .args = args,
    };
    Eval_Opt_Parse_Config cfg = {
        .origin = o,
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    if (!eval_opt_parse_walk(ctx,
                             args,
                             1,
                             k_find_item_specs,
                             NOB_ARRAY_LEN(k_find_item_specs),
                             cfg,
                             find_item_parse_option,
                             find_item_parse_positional,
                             &state)) {
        return false;
    }

    if (arena_arr_len(out_opt->names) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                              node,
                                              o,
                                              EV_DIAG_ERROR,
                                              EVAL_DIAG_MISSING_REQUIRED,
                                              "dispatcher",
                                              nob_sv_from_cstr("find_*() requires at least one search name"),
                                              nob_sv_from_cstr("Usage: find_<cmd>(<out-var> name1 [name2 ...] [NAMES ...] [HINTS ...] [PATHS ...])"));
        return false;
    }
    return true;
}

static bool find_item_candidate_exists(Evaluator_Context *ctx,
                                       Find_Item_Kind kind,
                                       String_View candidate,
                                       String_View *out_value) {
    if (!ctx || !out_value) return false;
    *out_value = nob_sv_from_cstr("");
    if (candidate.count == 0) return false;

    char *path_c = eval_sv_to_cstr_temp(ctx, candidate);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    if (!nob_file_exists(path_c)) return false;

    Nob_File_Type type = nob_get_file_type(path_c);
    if (kind == FIND_ITEM_PROGRAM || kind == FIND_ITEM_FILE || kind == FIND_ITEM_LIBRARY) {
        if (type != NOB_FILE_REGULAR && type != NOB_FILE_SYMLINK) return false;
        *out_value = candidate;
        return true;
    }
    if (kind == FIND_ITEM_PATH) {
        if (type != NOB_FILE_REGULAR && type != NOB_FILE_SYMLINK && type != NOB_FILE_DIRECTORY) return false;
        *out_value = svu_dirname(candidate);
        return true;
    }
    return false;
}

static bool find_item_append_prefixed_root(Evaluator_Context *ctx,
                                           SV_List *dirs,
                                           String_View root,
                                           String_View dir) {
    if (!ctx || !dirs || root.count == 0 || dir.count == 0) return false;

    String_View relative = dir;
    if (eval_sv_is_abs_path(relative)) {
#if defined(_WIN32)
        if (relative.count >= 2 && relative.data[1] == ':') {
            relative = relative.count > 2 ? nob_sv_from_parts(relative.data + 2, relative.count - 2) : nob_sv_from_cstr("");
        }
#endif
        while (relative.count > 0 && (relative.data[0] == '/' || relative.data[0] == '\\')) {
            relative = nob_sv_from_parts(relative.data + 1, relative.count - 1);
        }
    }
    return find_item_list_append(ctx, dirs, eval_sv_path_join(eval_temp_arena(ctx), root, relative));
}

static bool find_item_append_package_roots(Evaluator_Context *ctx,
                                           const Find_Item_Options *opt,
                                           Find_Item_Kind kind,
                                           SV_List *out_dirs) {
    if (!ctx || !opt || !out_dirs) return false;
    if (opt->no_default_path || opt->no_package_root_path || arena_arr_len(ctx->command_state.active_find_packages) == 0) return true;
    if (!eval_policy_is_new(ctx, EVAL_POLICY_CMP0074)) return true;

    String_View pkg = ctx->command_state.active_find_packages[arena_arr_len(ctx->command_state.active_find_packages) - 1];
    String_View mixed_root = svu_concat_suffix_temp(ctx, pkg, "_ROOT");
    bool cmp0144_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0144);
    String_View upper_root = cmp0144_new ? svu_concat_suffix_temp(ctx, sv_to_upper_temp(ctx, pkg), "_ROOT") : nob_sv_from_cstr("");

    String_View roots[4] = {0};
    size_t root_count = 0;
    String_View mixed_val = eval_var_get_visible(ctx, mixed_root);
    if (mixed_val.count > 0) roots[root_count++] = mixed_val;
    if (cmp0144_new) {
        String_View upper_val = eval_var_get_visible(ctx, upper_root);
        if (upper_val.count > 0) roots[root_count++] = upper_val;
    }

    if (!opt->no_cmake_environment_path) {
        const char *mixed_env = eval_getenv_temp(ctx, eval_sv_to_cstr_temp(ctx, mixed_root));
        if (mixed_env && mixed_env[0] != '\0') roots[root_count++] = nob_sv_from_cstr(mixed_env);
        if (cmp0144_new) {
            const char *upper_env = eval_getenv_temp(ctx, eval_sv_to_cstr_temp(ctx, upper_root));
            if (upper_env && upper_env[0] != '\0') roots[root_count++] = nob_sv_from_cstr(upper_env);
        }
    }

    for (size_t i = 0; i < root_count; i++) {
        if (!find_item_list_append(ctx, out_dirs, roots[i])) return false;
        if (kind == FIND_ITEM_PROGRAM) {
            if (!find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), roots[i], nob_sv_from_cstr("bin")))) return false;
            if (!find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), roots[i], nob_sv_from_cstr("sbin")))) return false;
        } else if (kind == FIND_ITEM_LIBRARY) {
            if (!find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), roots[i], nob_sv_from_cstr("lib")))) return false;
            if (!find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), roots[i], nob_sv_from_cstr("lib64")))) return false;
        } else {
            if (!find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), roots[i], nob_sv_from_cstr("include")))) return false;
            if (!find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), roots[i], nob_sv_from_cstr("share")))) return false;
        }
    }
    return true;
}

static bool find_item_append_cmake_var_paths(Evaluator_Context *ctx,
                                             const Find_Item_Options *opt,
                                             Find_Item_Kind kind,
                                             SV_List *out_dirs) {
    if (!ctx || !opt || !out_dirs || opt->no_default_path || opt->no_cmake_path) return true;
    const char *var_name = NULL;
    switch (kind) {
    case FIND_ITEM_PROGRAM: var_name = "CMAKE_PROGRAM_PATH"; break;
    case FIND_ITEM_LIBRARY: var_name = "CMAKE_LIBRARY_PATH"; break;
    case FIND_ITEM_FILE:
    case FIND_ITEM_PATH: var_name = "CMAKE_INCLUDE_PATH"; break;
    }
    if (!var_name) return true;

    SV_List values = NULL;
    if (!find_package_split_semicolon_temp(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr(var_name)), &values)) return false;
    for (size_t i = 0; i < arena_arr_len(values); i++) {
        if (!find_item_list_append(ctx, out_dirs, values[i])) return false;
    }
    return true;
}

static bool find_item_append_env_default_paths(Evaluator_Context *ctx,
                                               const Find_Item_Options *opt,
                                               Find_Item_Kind kind,
                                               SV_List *out_dirs) {
    if (!ctx || !opt || !out_dirs || opt->no_default_path || opt->no_cmake_environment_path) return true;
    const char *env_name = NULL;
    switch (kind) {
    case FIND_ITEM_PROGRAM: env_name = "CMAKE_PROGRAM_PATH"; break;
    case FIND_ITEM_LIBRARY: env_name = "CMAKE_LIBRARY_PATH"; break;
    case FIND_ITEM_FILE:
    case FIND_ITEM_PATH: env_name = "CMAKE_INCLUDE_PATH"; break;
    }
    if (!env_name) return true;
    return find_item_list_append_env(ctx, out_dirs, nob_sv_from_cstr(env_name));
}

static bool find_item_append_system_env_paths(Evaluator_Context *ctx,
                                              const Find_Item_Options *opt,
                                              Find_Item_Kind kind,
                                              SV_List *out_dirs) {
    if (!ctx || !opt || !out_dirs || opt->no_default_path || opt->no_system_environment_path) return true;
    if (kind == FIND_ITEM_PROGRAM) {
        return find_item_list_append_env(ctx, out_dirs, nob_sv_from_cstr("PATH"));
    }
    return true;
}

static bool find_item_append_install_prefix(Evaluator_Context *ctx,
                                            const Find_Item_Options *opt,
                                            Find_Item_Kind kind,
                                            SV_List *out_dirs) {
    if (!ctx || !opt || !out_dirs || opt->no_default_path || opt->no_cmake_install_prefix) return true;
    String_View prefix = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_INSTALL_PREFIX"));
    if (prefix.count == 0) return true;
    if (!find_item_list_append(ctx, out_dirs, prefix)) return false;
    if (kind == FIND_ITEM_PROGRAM) return find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), prefix, nob_sv_from_cstr("bin")));
    if (kind == FIND_ITEM_LIBRARY) {
        if (!find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), prefix, nob_sv_from_cstr("lib")))) return false;
        return find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), prefix, nob_sv_from_cstr("lib64")));
    }
    return find_item_list_append(ctx, out_dirs, eval_sv_path_join(eval_temp_arena(ctx), prefix, nob_sv_from_cstr("include")));
}

static bool find_item_append_system_defaults(Evaluator_Context *ctx,
                                             const Find_Item_Options *opt,
                                             Find_Item_Kind kind,
                                             SV_List *out_dirs) {
    if (!ctx || !opt || !out_dirs || opt->no_default_path || opt->no_cmake_system_path) return true;
#if defined(_WIN32)
    if (kind == FIND_ITEM_PROGRAM) {
        if (!find_item_list_append_env(ctx, out_dirs, nob_sv_from_cstr("ProgramFiles"))) return false;
        if (!find_item_list_append_env(ctx, out_dirs, nob_sv_from_cstr("ProgramFiles(x86)"))) return false;
    }
    if (kind == FIND_ITEM_LIBRARY || kind == FIND_ITEM_FILE || kind == FIND_ITEM_PATH) {
        if (!find_item_list_append_env(ctx, out_dirs, nob_sv_from_cstr("ProgramFiles"))) return false;
        if (!find_item_list_append_env(ctx, out_dirs, nob_sv_from_cstr("ProgramFiles(x86)"))) return false;
    }
#else
    if (kind == FIND_ITEM_PROGRAM) {
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/local/bin"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/bin"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/bin"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/local/sbin"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/sbin"))) return false;
    } else if (kind == FIND_ITEM_LIBRARY) {
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/local/lib"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/local/lib64"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/lib"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/lib64"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/lib"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/lib64"))) return false;
    } else {
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/local/include"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/usr/include"))) return false;
        if (!find_item_list_append(ctx, out_dirs, nob_sv_from_cstr("/opt/include"))) return false;
    }
#endif
    return true;
}

static bool find_item_build_search_dirs(Evaluator_Context *ctx,
                                        const Find_Item_Options *opt,
                                        Find_Item_Kind kind,
                                        SV_List *out_dirs) {
    if (!ctx || !opt || !out_dirs) return false;
    *out_dirs = NULL;

    if (!find_item_append_package_roots(ctx, opt, kind, out_dirs)) return false;
    for (size_t i = 0; i < arena_arr_len(opt->hints); i++) {
        if (!find_item_list_append(ctx, out_dirs, opt->hints[i])) return false;
    }
    if (!find_item_append_cmake_var_paths(ctx, opt, kind, out_dirs)) return false;
    if (!find_item_append_env_default_paths(ctx, opt, kind, out_dirs)) return false;
    for (size_t i = 0; i < arena_arr_len(opt->paths); i++) {
        if (!find_item_list_append(ctx, out_dirs, opt->paths[i])) return false;
    }
    if (!find_item_append_system_env_paths(ctx, opt, kind, out_dirs)) return false;
    if (!find_item_append_install_prefix(ctx, opt, kind, out_dirs)) return false;
    if (!find_item_append_system_defaults(ctx, opt, kind, out_dirs)) return false;
    return true;
}

static bool find_item_apply_root_paths(Evaluator_Context *ctx,
                                       const Find_Item_Options *opt,
                                       const SV_List *base_dirs,
                                       SV_List *out_dirs) {
    if (!ctx || !opt || !base_dirs || !out_dirs) return false;
    *out_dirs = NULL;

    SV_List roots = NULL;
    if (opt->root_mode != FIND_ROOT_MODE_NONE) {
        if (!find_package_split_semicolon_temp(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_FIND_ROOT_PATH")), &roots)) return false;
    }

    if (opt->root_mode != FIND_ROOT_MODE_NONE && arena_arr_len(roots) > 0) {
        for (size_t ri = 0; ri < arena_arr_len(roots); ri++) {
            for (size_t di = 0; di < arena_arr_len(*base_dirs); di++) {
                if (!find_item_append_prefixed_root(ctx, out_dirs, roots[ri], (*base_dirs)[di])) return false;
            }
        }
    }

    if (opt->root_mode != FIND_ROOT_MODE_ONLY) {
        for (size_t di = 0; di < arena_arr_len(*base_dirs); di++) {
            if (!find_item_list_append(ctx, out_dirs, (*base_dirs)[di])) return false;
        }
    }
    return true;
}

static bool find_item_invoke_validator(Evaluator_Context *ctx,
                                       const Find_Item_Options *opt,
                                       String_View candidate,
                                       bool *out_accept) {
    if (!ctx || !opt || !out_accept) return false;
    *out_accept = true;
    if (!opt->has_validator) return true;

    User_Command *user = eval_user_cmd_find(ctx, opt->validator);
    if (!user || user->kind != USER_CMD_FUNCTION) {
        *out_accept = false;
        return true;
    }

    char key_buf[64];
    int n = snprintf(key_buf, sizeof(key_buf), "_NOBIFY_FIND_VALID_%zu", arena_arr_len(ctx->command_state.active_find_packages) + eval_scope_visible_depth(ctx));
    if (n <= 0 || (size_t)n >= sizeof(key_buf)) return ctx_oom(ctx);
    String_View result_var = nob_sv_from_cstr(key_buf);
    if (!eval_var_set_current(ctx, result_var, nob_sv_from_cstr("TRUE"))) return false;

    String_View call_args_storage[2] = {result_var, candidate};
    SV_List call_args = call_args_storage;
    Cmake_Event_Origin o = {0};
    o.file_path = ctx->current_file ? nob_sv_from_cstr(ctx->current_file) : nob_sv_from_cstr("<input>");
    if (!eval_user_cmd_invoke(ctx, opt->validator, &call_args, o)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    if (eval_should_stop(ctx)) return false;

    String_View final = eval_var_get_visible(ctx, result_var);
    *out_accept = final.count == 0 || eval_truthy(ctx, final);
    return true;
}

static bool find_item_make_library_name(Evaluator_Context *ctx,
                                        String_View base_name,
                                        size_t variant_index,
                                        String_View *out_name) {
    if (!ctx || !out_name) return false;
    *out_name = nob_sv_from_cstr("");
    if (variant_index == 0) {
        *out_name = base_name;
        return true;
    }

#if defined(_WIN32)
    const char *prefix = (variant_index == 1) ? "" : "lib";
    const char *suffix = ".lib";
    if (variant_index > 2) return false;
#else
    const char *prefix = (variant_index == 1 || variant_index == 3 || variant_index == 5) ? "lib" : "";
    const char *suffix = (variant_index <= 2) ? ".so" : ((variant_index <= 4) ? ".a" : ".dylib");
    if (variant_index > 6) return false;
#endif

    size_t total = strlen(prefix) + base_name.count + strlen(suffix);
    char *buf = (char *)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    size_t off = 0;
    memcpy(buf + off, prefix, strlen(prefix));
    off += strlen(prefix);
    memcpy(buf + off, base_name.data, base_name.count);
    off += base_name.count;
    memcpy(buf + off, suffix, strlen(suffix));
    off += strlen(suffix);
    buf[off] = '\0';
    *out_name = nob_sv_from_parts(buf, off);
    return true;
}

static bool find_item_make_program_name(Evaluator_Context *ctx,
                                        String_View base_name,
                                        size_t variant_index,
                                        String_View *out_name) {
    if (!ctx || !out_name) return false;
    *out_name = nob_sv_from_cstr("");
    if (variant_index == 0) {
        *out_name = base_name;
        return true;
    }
#if defined(_WIN32)
    static const char *const exts[] = {".exe", ".cmd", ".bat", ".com"};
    if (variant_index > NOB_ARRAY_LEN(exts)) return false;
    const char *suffix = exts[variant_index - 1];
    size_t total = base_name.count + strlen(suffix);
    char *buf = (char *)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memcpy(buf, base_name.data, base_name.count);
    memcpy(buf + base_name.count, suffix, strlen(suffix));
    buf[total] = '\0';
    *out_name = nob_sv_from_parts(buf, total);
    return true;
#else
    (void)variant_index;
    return false;
#endif
}

static bool find_item_search(Evaluator_Context *ctx,
                             const Find_Item_Options *opt,
                             Find_Item_Kind kind,
                             String_View *out_found) {
    if (!ctx || !opt || !out_found) return false;
    *out_found = nob_sv_from_cstr("");

    String_View current = eval_var_get_visible(ctx, opt->out_var);
    if (current.count > 0 && !nob_sv_end_with(current, "-NOTFOUND")) {
        String_View existing = nob_sv_from_cstr("");
        if (find_item_candidate_exists(ctx, kind, current, &existing)) {
            *out_found = current;
            return true;
        }
    }

    SV_List base_dirs = NULL;
    if (!find_item_build_search_dirs(ctx, opt, kind, &base_dirs)) return false;

    SV_List search_dirs = NULL;
    if (!find_item_apply_root_paths(ctx, opt, &base_dirs, &search_dirs)) return false;

    SV_List suffixes = NULL;
    if (!svu_list_push_temp(ctx, &suffixes, nob_sv_from_cstr(""))) return false;
    for (size_t i = 0; i < arena_arr_len(opt->path_suffixes); i++) {
        if (!find_item_list_append(ctx, &suffixes, opt->path_suffixes[i])) return false;
    }

    size_t max_name_variants = (kind == FIND_ITEM_LIBRARY)
#if defined(_WIN32)
        ? 3
#else
        ? 7
#endif
        : ((kind == FIND_ITEM_PROGRAM)
#if defined(_WIN32)
           ? 5
#else
           ? 1
#endif
           : 1);

    size_t outer_limit = opt->names_per_dir ? arena_arr_len(search_dirs) : arena_arr_len(opt->names);
    size_t inner_limit = opt->names_per_dir ? arena_arr_len(opt->names) : arena_arr_len(search_dirs);
    for (size_t outer = 0; outer < outer_limit; outer++) {
        for (size_t inner = 0; inner < inner_limit; inner++) {
            String_View dir = opt->names_per_dir ? search_dirs[outer] : search_dirs[inner];
            String_View name = opt->names_per_dir ? opt->names[inner] : opt->names[outer];
            if (dir.count == 0 || name.count == 0) continue;

            if (!eval_sv_is_abs_path(dir)) {
                dir = eval_path_resolve_for_cmake_arg(ctx, dir, eval_current_source_dir(ctx), false);
                if (eval_should_stop(ctx)) return false;
            }

            for (size_t si = 0; si < arena_arr_len(suffixes); si++) {
                String_View base = dir;
                if (suffixes[si].count > 0) {
                    base = eval_sv_path_join(eval_temp_arena(ctx), dir, suffixes[si]);
                }
                for (size_t nv = 0; nv < max_name_variants; nv++) {
                    String_View effective_name = nob_sv_from_cstr("");
                    bool ok_name = true;
                    if (kind == FIND_ITEM_LIBRARY) {
                        ok_name = find_item_make_library_name(ctx, name, nv, &effective_name);
                    } else if (kind == FIND_ITEM_PROGRAM) {
                        if (nv == 0) {
                            effective_name = name;
                        } else {
                            ok_name = find_item_make_program_name(ctx, name, nv, &effective_name);
                        }
                    } else {
                        effective_name = name;
                        ok_name = (nv == 0);
                    }
                    if (!ok_name || effective_name.count == 0) continue;

                    String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), base, effective_name);
                    String_View found_value = nob_sv_from_cstr("");
                    if (!find_item_candidate_exists(ctx, kind, candidate, &found_value)) continue;

                    bool accept = true;
                    if (!find_item_invoke_validator(ctx, opt, found_value, &accept)) return false;
                    if (!accept) continue;

                    *out_found = found_value;
                    return true;
                }
            }
        }
    }
    return true;
}

static bool find_item_set_result(Evaluator_Context *ctx,
                                 const Node *node,
                                 const Find_Item_Options *opt,
                                 String_View found_value) {
    if (!ctx || !node || !opt) return false;
    if (found_value.count > 0) return eval_var_set_current(ctx, opt->out_var, found_value);

    String_View notfound = svu_concat_suffix_temp(ctx, opt->out_var, "-NOTFOUND");
    if (!eval_var_set_current(ctx, opt->out_var, notfound)) return false;
    if (opt->required) {
        (void)find_package_diag_error(ctx,
                                      node,
                                      nob_sv_from_cstr("Required item not found"),
                                      opt->out_var);
        eval_request_stop_on_error(ctx);
    }
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static Eval_Result find_item_handle(Evaluator_Context *ctx,
                                    const Node *node,
                                    Find_Item_Kind kind) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Find_Item_Options opt = {0};
    if (!find_item_parse_options(ctx, node, args, &opt)) return eval_result_from_ctx(ctx);

    String_View found = nob_sv_from_cstr("");
    if (!find_item_search(ctx, &opt, kind, &found)) return eval_result_from_ctx(ctx);
    if (!find_item_set_result(ctx, node, &opt, found)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_find_program(Evaluator_Context *ctx, const Node *node) {
    return find_item_handle(ctx, node, FIND_ITEM_PROGRAM);
}

Eval_Result eval_handle_find_file(Evaluator_Context *ctx, const Node *node) {
    return find_item_handle(ctx, node, FIND_ITEM_FILE);
}

Eval_Result eval_handle_find_path(Evaluator_Context *ctx, const Node *node) {
    return find_item_handle(ctx, node, FIND_ITEM_PATH);
}

Eval_Result eval_handle_find_library(Evaluator_Context *ctx, const Node *node) {
    return find_item_handle(ctx, node, FIND_ITEM_LIBRARY);
}
