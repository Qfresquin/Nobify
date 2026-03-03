#include "eval_package.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "eval_expr.h"
#include "eval_opt_parser.h"
#include "arena_dyn.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

static bool file_exists_sv(Evaluator_Context *ctx, String_View path) {
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_file_exists(path_c) != 0;
}

static String_View sv_to_lower_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) {
        buf[i] = (char)tolower((unsigned char)in.data[i]);
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool find_package_split_semicolon_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;
    *out = NULL;
    if (input.count == 0) return true;

    const char *p = input.data;
    const char *end = input.data + input.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        if (!svu_list_push_temp(ctx, out, nob_sv_from_parts(p, (size_t)(q - p)))) return false;
        if (q >= end) break;
        p = q + 1;
    }
    return true;
}

static void find_package_push_env_list(Evaluator_Context *ctx,
                                       String_View *items,
                                       size_t *io_count,
                                       size_t cap,
                                       const char *env_name) {
    if (!ctx || !items || !io_count || !env_name) return;
    const char *raw = eval_getenv_temp(ctx, env_name);
    if (!raw || raw[0] == '\0') return;

    String_View sv = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(raw));
    if (eval_should_stop(ctx) || sv.count == 0) return;

#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif

    const char *p = sv.data;
    const char *end = sv.data + sv.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != sep) q++;
        String_View item = nob_sv_from_parts(p, (size_t)(q - p));
        if (item.count > 0 && *io_count < cap) {
            items[(*io_count)++] = item;
        }
        if (q >= end) break;
        p = q + 1;
    }
}

static void find_package_push_prefix(String_View *items, size_t *io_count, size_t cap, String_View v);

static bool find_package_try_module(Evaluator_Context *ctx,
                                    String_View pkg,
                                    String_View name_overrides,
                                    String_View extra_paths,
                                    bool no_default_path,
                                    bool no_cmake_path,
                                    bool no_cmake_environment_path,
                                    String_View *out_path) {
    String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;
    String_View module_paths[64] = {0};
    size_t module_count = 0;
    find_package_push_prefix(module_paths, &module_count, NOB_ARRAY_LEN(module_paths), extra_paths);
    if (!no_default_path) {
        if (!no_cmake_path) {
            String_View var_module_paths = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_MODULE_PATH"));
            find_package_push_prefix(module_paths, &module_count, NOB_ARRAY_LEN(module_paths), var_module_paths);
        }
        if (!no_cmake_environment_path) {
            find_package_push_env_list(ctx,
                                       module_paths,
                                       &module_count,
                                       NOB_ARRAY_LEN(module_paths),
                                       "CMAKE_MODULE_PATH");
        }
    }

    String_View module_candidates[32] = {0};
    size_t module_candidate_count = 0;
    SV_List name_items = NULL;
    if (!find_package_split_semicolon_temp(ctx, name_overrides, &name_items)) return false;
    if (arena_arr_len(name_items) > 0) {
        for (size_t i = 0; i < arena_arr_len(name_items) && module_candidate_count < NOB_ARRAY_LEN(module_candidates); i++) {
            String_View item = name_items[i];
            if (item.count == 0) continue;
            module_candidates[module_candidate_count++] = item;
        }
    }
    if (module_candidate_count == 0) module_candidates[module_candidate_count++] = pkg;

    if (!no_default_path && !no_cmake_path) {
        String_View fallback = eval_sv_path_join(eval_temp_arena(ctx), current_src, nob_sv_from_cstr("CMake"));
        find_package_push_prefix(module_paths, &module_count, NOB_ARRAY_LEN(module_paths), fallback);
    }
    String_View search = eval_sv_join_semi_temp(ctx, module_paths, module_count);
    if (eval_should_stop(ctx)) return false;

    const char *p = search.data;
    const char *end = search.data + search.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View dir = nob_sv_from_parts(p, (size_t)(q - p));
        if (dir.count > 0) {
            if (!eval_sv_is_abs_path(dir)) {
                dir = eval_sv_path_join(eval_temp_arena(ctx), current_src, dir);
            }
            for (size_t ni = 0; ni < module_candidate_count; ni++) {
                String_View module_name = nob_sv_from_cstr("");
                {
                    String_View tmp = svu_concat_suffix_temp(ctx, module_candidates[ni], ".cmake");
                    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), tmp.count + 5);
                    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
                    memcpy(buf, "Find", 4);
                    memcpy(buf + 4, tmp.data, tmp.count);
                    buf[tmp.count + 4] = '\0';
                    module_name = nob_sv_from_cstr(buf);
                }
                String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dir, module_name);
                if (file_exists_sv(ctx, candidate)) {
                    *out_path = candidate;
                    return true;
                }
            }
        }
        if (q >= end) break;
        p = q + 1;
    }
    return false;
}

static void find_package_push_prefix(String_View *items, size_t *io_count, size_t cap, String_View v) {
    if (!items || !io_count || *io_count >= cap) return;
    if (v.count == 0) return;
    items[(*io_count)++] = v;
}

static void find_package_push_prefix_variants(Evaluator_Context *ctx,
                                              String_View *items,
                                              size_t *io_count,
                                              size_t cap,
                                              String_View root) {
    if (!ctx || root.count == 0) return;
    find_package_push_prefix(items, io_count, cap, root);
    find_package_push_prefix(items, io_count, cap, eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("lib/cmake")));
    find_package_push_prefix(items, io_count, cap, eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("lib64/cmake")));
    find_package_push_prefix(items, io_count, cap, eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("share/cmake")));
}

static void find_package_push_env_prefix_variants(Evaluator_Context *ctx,
                                                  String_View *items,
                                                  size_t *io_count,
                                                  size_t cap,
                                                  const char *env_name) {
    if (!ctx || !env_name) return;
    const char *raw = eval_getenv_temp(ctx, env_name);
    if (!raw || raw[0] == '\0') return;
    String_View root = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(raw));
    if (eval_should_stop(ctx)) return;
    find_package_push_prefix_variants(ctx, items, io_count, cap, root);
}

static void find_package_push_package_root_prefixes(Evaluator_Context *ctx,
                                                    String_View pkg,
                                                    String_View names_csv,
                                                    bool no_default_path,
                                                    bool no_package_root_path,
                                                    bool no_cmake_environment_path,
                                                    String_View *items,
                                                    size_t *io_count,
                                                    size_t cap) {
    if (!ctx || pkg.count == 0 || !items || !io_count) return;
    if (no_default_path || no_package_root_path) return;

    // CMP0074 controls whether find_package() honors <Pkg>_ROOT variables.
    if (!eval_sv_eq_ci_lit(eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0074")), "NEW")) return;

    String_View names[16] = {0};
    size_t name_count = 0;
    SV_List name_items = NULL;
    if (!find_package_split_semicolon_temp(ctx, names_csv, &name_items)) return;
    if (arena_arr_len(name_items) > 0) {
        for (size_t i = 0; i < arena_arr_len(name_items) && name_count < NOB_ARRAY_LEN(names); i++) {
            if (name_items[i].count == 0) continue;
            names[name_count++] = name_items[i];
        }
    }
    if (name_count == 0) names[name_count++] = pkg;

    for (size_t i = 0; i < name_count; i++) {
        String_View root_var = svu_concat_suffix_temp(ctx, names[i], "_ROOT");
        if (eval_should_stop(ctx)) return;

        String_View root_val = eval_var_get(ctx, root_var);
        if (root_val.count > 0) {
            find_package_push_prefix_variants(ctx, items, io_count, cap, root_val);
        }

        if (!no_cmake_environment_path) {
            char *env_name = (char*)arena_alloc(eval_temp_arena(ctx), root_var.count + 1);
            EVAL_OOM_RETURN_VOID_IF_NULL(ctx, env_name);
            memcpy(env_name, root_var.data, root_var.count);
            env_name[root_var.count] = '\0';
            find_package_push_env_prefix_variants(ctx, items, io_count, cap, env_name);
        }
    }
}

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

static String_View sv_to_upper_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) buf[i] = (char)toupper((unsigned char)in.data[i]);
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
}

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
                                    const SV_List *args,
                                    size_t *io_index,
                                    SV_List *out_values) {
    if (!ctx || !args || !io_index || !out_values) return false;
    while (*io_index < arena_arr_len(*args)) {
        String_View value = (*args)[*io_index];
        if (find_item_is_keyword(value)) break;
        if (find_item_keyword_eq(value, "ENV")) {
            (*io_index)++;
            if (*io_index >= arena_arr_len(*args)) return ctx_oom(ctx);
            if (find_item_is_keyword((*args)[*io_index])) return false;
            if (!find_item_list_append_env(ctx, out_values, (*args)[*io_index])) return false;
            (*io_index)++;
            continue;
        }
        if (!find_item_list_append(ctx, out_values, value)) return false;
        (*io_index)++;
    }
    return true;
}

static bool find_item_parse_options(Evaluator_Context *ctx,
                                    const Node *node,
                                    SV_List args,
                                    Find_Item_Options *out_opt) {
    if (!ctx || !node || !out_opt) return false;
    memset(out_opt, 0, sizeof(*out_opt));
    out_opt->root_mode = FIND_ROOT_MODE_BOTH;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 2) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("dispatcher"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("find_*() requires an output variable and at least one name"),
                             nob_sv_from_cstr("Usage: find_*(<VAR> name1 [name2 ...] [NAMES ...] [HINTS ...] [PATHS ...])"));
        return !eval_should_stop(ctx);
    }

    out_opt->out_var = args[0];
    size_t i = 1;
    while (i < arena_arr_len(args) && !find_item_is_keyword(args[i])) {
        if (!find_item_list_append(ctx, &out_opt->names, args[i])) return false;
        i++;
    }

    while (i < arena_arr_len(args)) {
        String_View key = args[i++];
        if (find_item_keyword_eq(key, "NAMES")) {
            if (!find_item_collect_multi(ctx, &args, &i, &out_opt->names)) return false;
            continue;
        }
        if (find_item_keyword_eq(key, "HINTS")) {
            if (!find_item_collect_multi(ctx, &args, &i, &out_opt->hints)) return false;
            continue;
        }
        if (find_item_keyword_eq(key, "PATHS")) {
            if (!find_item_collect_multi(ctx, &args, &i, &out_opt->paths)) return false;
            continue;
        }
        if (find_item_keyword_eq(key, "PATH_SUFFIXES")) {
            if (!find_item_collect_multi(ctx, &args, &i, &out_opt->path_suffixes)) return false;
            continue;
        }
        if (find_item_keyword_eq(key, "DOC")) {
            if (i < arena_arr_len(args) && !find_item_is_keyword(args[i])) i++;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_CACHE")) {
            out_opt->no_cache = true;
            continue;
        }
        if (find_item_keyword_eq(key, "REQUIRED")) {
            out_opt->required = true;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_DEFAULT_PATH")) {
            out_opt->no_default_path = true;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_PACKAGE_ROOT_PATH")) {
            out_opt->no_package_root_path = true;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_CMAKE_PATH")) {
            out_opt->no_cmake_path = true;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_CMAKE_ENVIRONMENT_PATH")) {
            out_opt->no_cmake_environment_path = true;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_SYSTEM_ENVIRONMENT_PATH")) {
            out_opt->no_system_environment_path = true;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_CMAKE_SYSTEM_PATH")) {
            out_opt->no_cmake_system_path = true;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_CMAKE_INSTALL_PREFIX")) {
            out_opt->no_cmake_install_prefix = true;
            continue;
        }
        if (find_item_keyword_eq(key, "NO_CMAKE_FIND_ROOT_PATH")) {
            out_opt->root_mode = FIND_ROOT_MODE_NONE;
            continue;
        }
        if (find_item_keyword_eq(key, "ONLY_CMAKE_FIND_ROOT_PATH")) {
            out_opt->root_mode = FIND_ROOT_MODE_ONLY;
            continue;
        }
        if (find_item_keyword_eq(key, "CMAKE_FIND_ROOT_PATH_BOTH")) {
            out_opt->root_mode = FIND_ROOT_MODE_BOTH;
            continue;
        }
        if (find_item_keyword_eq(key, "NAMES_PER_DIR")) {
            out_opt->names_per_dir = true;
            continue;
        }
        if (find_item_keyword_eq(key, "REGISTRY_VIEW")) {
            if (i >= arena_arr_len(args) || find_item_is_keyword(args[i])) {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("dispatcher"),
                                     node->as.cmd.name,
                                     o,
                                     nob_sv_from_cstr("find_*(REGISTRY_VIEW) requires a value"),
                                     nob_sv_from_cstr("Usage: find_*(... REGISTRY_VIEW <view>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_registry_view = true;
            out_opt->registry_view = args[i++];
            continue;
        }
        if (find_item_keyword_eq(key, "VALIDATOR")) {
            if (i >= arena_arr_len(args) || find_item_is_keyword(args[i])) {
                (void)eval_emit_diag(ctx,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("dispatcher"),
                                     node->as.cmd.name,
                                     o,
                                     nob_sv_from_cstr("find_*(VALIDATOR) requires a function name"),
                                     nob_sv_from_cstr("Usage: find_*(... VALIDATOR <function>)"));
                return !eval_should_stop(ctx);
            }
            out_opt->has_validator = true;
            out_opt->validator = args[i++];
            continue;
        }
    }

    if (arena_arr_len(out_opt->names) == 0) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("dispatcher"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("find_*() requires at least one search name"),
                             nob_sv_from_cstr("Provide a legacy name after <VAR> or use NAMES <name>..."));
        return !eval_should_stop(ctx);
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
    if (opt->no_default_path || opt->no_package_root_path || arena_arr_len(ctx->active_find_packages) == 0) return true;
    if (!eval_sv_eq_ci_lit(eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0074")), "NEW")) return true;

    String_View pkg = ctx->active_find_packages[arena_arr_len(ctx->active_find_packages) - 1];
    String_View mixed_root = svu_concat_suffix_temp(ctx, pkg, "_ROOT");
    bool cmp0144_new = eval_sv_eq_ci_lit(eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0144")), "NEW");
    String_View upper_root = cmp0144_new ? svu_concat_suffix_temp(ctx, sv_to_upper_temp(ctx, pkg), "_ROOT") : nob_sv_from_cstr("");

    String_View roots[4] = {0};
    size_t root_count = 0;
    String_View mixed_val = eval_var_get(ctx, mixed_root);
    if (mixed_val.count > 0) roots[root_count++] = mixed_val;
    if (cmp0144_new) {
        String_View upper_val = eval_var_get(ctx, upper_root);
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
    if (!find_package_split_semicolon_temp(ctx, eval_var_get(ctx, nob_sv_from_cstr(var_name)), &values)) return false;
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
    String_View prefix = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_INSTALL_PREFIX"));
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
        if (!find_package_split_semicolon_temp(ctx, eval_var_get(ctx, nob_sv_from_cstr("CMAKE_FIND_ROOT_PATH")), &roots)) return false;
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
    int n = snprintf(key_buf, sizeof(key_buf), "_NOBIFY_FIND_VALID_%zu", arena_arr_len(ctx->active_find_packages) + ctx->scope_depth);
    if (n <= 0 || (size_t)n >= sizeof(key_buf)) return ctx_oom(ctx);
    String_View result_var = nob_sv_from_cstr(key_buf);
    if (!eval_var_set(ctx, result_var, nob_sv_from_cstr("TRUE"))) return false;

    String_View call_args_storage[2] = {result_var, candidate};
    SV_List call_args = call_args_storage;
    Cmake_Event_Origin o = {0};
    o.file_path = ctx->current_file ? nob_sv_from_cstr(ctx->current_file) : nob_sv_from_cstr("<input>");
    if (!eval_user_cmd_invoke(ctx, opt->validator, &call_args, o)) return !eval_should_stop(ctx);
    if (eval_should_stop(ctx)) return false;

    String_View final = eval_var_get(ctx, result_var);
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
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
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
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
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

    String_View current = eval_var_get(ctx, opt->out_var);
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
                dir = eval_path_resolve_for_cmake_arg(ctx, dir, eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR")), false);
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
    if (found_value.count > 0) return eval_var_set(ctx, opt->out_var, found_value);

    String_View notfound = svu_concat_suffix_temp(ctx, opt->out_var, "-NOTFOUND");
    if (!eval_var_set(ctx, opt->out_var, notfound)) return false;
    if (opt->required) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("dispatcher"),
                             node->as.cmd.name,
                             eval_origin_from_node(ctx, node),
                             nob_sv_from_cstr("Required item not found"),
                             opt->out_var);
        eval_request_stop_on_error(ctx);
    }
    return !eval_should_stop(ctx);
}

static bool find_item_handle(Evaluator_Context *ctx,
                             const Node *node,
                             Find_Item_Kind kind) {
    if (!ctx || !node || eval_should_stop(ctx)) return false;
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    Find_Item_Options opt = {0};
    if (!find_item_parse_options(ctx, node, args, &opt)) return !eval_should_stop(ctx);

    String_View found = nob_sv_from_cstr("");
    if (!find_item_search(ctx, &opt, kind, &found)) return !eval_should_stop(ctx);
    return find_item_set_result(ctx, node, &opt, found);
}

static bool find_package_try_config_in_prefixes(Evaluator_Context *ctx,
                                                String_View current_src,
                                                String_View default_name,
                                                String_View names_csv,
                                                String_View *config_names,
                                                size_t config_name_count,
                                                String_View path_suffixes_csv,
                                                String_View prefixes,
                                                String_View *out_path) {
    String_View name_items[16] = {0};
    size_t name_count = 0;
    SV_List parsed_names = NULL;
    if (!find_package_split_semicolon_temp(ctx, names_csv, &parsed_names)) return false;
    for (size_t i = 0; i < arena_arr_len(parsed_names) && name_count < NOB_ARRAY_LEN(name_items); i++) {
        if (parsed_names[i].count == 0) continue;
        name_items[name_count++] = parsed_names[i];
    }
    if (name_count == 0 && default_name.count > 0) {
        name_items[name_count++] = default_name;
    }

    String_View suffix_items[32] = {0};
    size_t suffix_count = 0;
    suffix_items[suffix_count++] = nob_sv_from_cstr("");
    SV_List parsed_suffixes = NULL;
    if (!find_package_split_semicolon_temp(ctx, path_suffixes_csv, &parsed_suffixes)) return false;
    for (size_t i = 0; i < arena_arr_len(parsed_suffixes) && suffix_count < NOB_ARRAY_LEN(suffix_items); i++) {
        if (parsed_suffixes[i].count == 0) continue;
        suffix_items[suffix_count++] = parsed_suffixes[i];
    }

    const char *p = prefixes.data;
    const char *end = prefixes.data + prefixes.count;
    while (p <= end) {
        const char *q = p;
        while (q < end && *q != ';') q++;
        String_View prefix = nob_sv_from_parts(p, (size_t)(q - p));
        if (prefix.count > 0) {
            if (!eval_sv_is_abs_path(prefix)) {
                prefix = eval_sv_path_join(eval_temp_arena(ctx), current_src, prefix);
            }
            for (size_t si = 0; si < suffix_count; si++) {
                String_View base = prefix;
                if (suffix_items[si].count > 0) {
                    base = eval_sv_path_join(eval_temp_arena(ctx), prefix, suffix_items[si]);
                }
                for (size_t ni = 0; ni < config_name_count; ni++) {
                    String_View config_name = config_names[ni];
                    String_View c1 = eval_sv_path_join(eval_temp_arena(ctx), base, config_name);
                    if (file_exists_sv(ctx, c1)) {
                        *out_path = c1;
                        return true;
                    }
                    for (size_t pn = 0; pn < name_count; pn++) {
                        String_View pkg_subdir = eval_sv_path_join(eval_temp_arena(ctx), base, name_items[pn]);
                        String_View c2 = eval_sv_path_join(eval_temp_arena(ctx), pkg_subdir, config_name);
                        if (file_exists_sv(ctx, c2)) {
                            *out_path = c2;
                            return true;
                        }
                    }
                }
            }
        }
        if (q >= end) break;
        p = q + 1;
    }
    return false;
}

static bool find_package_try_config(Evaluator_Context *ctx,
                                    String_View pkg,
                                    String_View names_csv,
                                    String_View configs_csv,
                                    String_View path_suffixes_csv,
                                    String_View extra_prefixes,
                                    bool no_default_path,
                                    bool no_package_root_path,
                                    bool no_cmake_path,
                                    bool no_cmake_environment_path,
                                    bool no_system_environment_path,
                                    bool no_cmake_system_path,
                                    bool no_cmake_install_prefix,
                                    String_View *out_path) {
    String_View current_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current_src.count == 0) current_src = ctx->source_dir;

    String_View names[16] = {0};
    size_t name_count = 0;
    SV_List parsed_names = NULL;
    if (!find_package_split_semicolon_temp(ctx, names_csv, &parsed_names)) return false;
    if (arena_arr_len(parsed_names) > 0) {
        for (size_t i = 0; i < arena_arr_len(parsed_names) && name_count < NOB_ARRAY_LEN(names); i++) {
            if (parsed_names[i].count == 0) continue;
            names[name_count++] = parsed_names[i];
        }
    }
    if (name_count == 0) names[name_count++] = pkg;

    String_View config_names[32] = {0};
    size_t config_name_count = 0;
    SV_List parsed_configs = NULL;
    if (!find_package_split_semicolon_temp(ctx, configs_csv, &parsed_configs)) return false;
    if (arena_arr_len(parsed_configs) > 0) {
        for (size_t i = 0; i < arena_arr_len(parsed_configs) && config_name_count < NOB_ARRAY_LEN(config_names); i++) {
            if (parsed_configs[i].count == 0) continue;
            config_names[config_name_count++] = parsed_configs[i];
        }
    } else {
        for (size_t i = 0; i < name_count && config_name_count + 2 <= NOB_ARRAY_LEN(config_names); i++) {
            config_names[config_name_count++] = svu_concat_suffix_temp(ctx, names[i], "Config.cmake");
            String_View lower_name = sv_to_lower_temp(ctx, names[i]);
            if (lower_name.count > 0) {
                config_names[config_name_count++] = svu_concat_suffix_temp(ctx, lower_name, "-config.cmake");
            }
        }
    }

    String_View dir_var = svu_concat_suffix_temp(ctx, pkg, "_DIR");
    String_View pkg_dir = eval_var_get(ctx, dir_var);
    if (pkg_dir.count > 0) {
        String_View dir = pkg_dir;
        if (!eval_sv_is_abs_path(dir)) {
            dir = eval_sv_path_join(eval_temp_arena(ctx), current_src, dir);
        }
        for (size_t ni = 0; ni < config_name_count; ni++) {
            String_View candidate = eval_sv_path_join(eval_temp_arena(ctx), dir, config_names[ni]);
            if (file_exists_sv(ctx, candidate)) {
                *out_path = candidate;
                return true;
            }
        }
    }

    String_View merged_prefixes[64] = {0};
    size_t merged_count = 0;
    find_package_push_prefix(merged_prefixes, &merged_count, 64, extra_prefixes);
    if (!no_default_path) {
        find_package_push_package_root_prefixes(ctx,
                                                pkg,
                                                names_csv,
                                                no_default_path,
                                                no_package_root_path,
                                                no_cmake_environment_path,
                                                merged_prefixes,
                                                &merged_count,
                                                NOB_ARRAY_LEN(merged_prefixes));

        if (!no_cmake_path) {
            String_View prefixes = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_PREFIX_PATH"));
            find_package_push_prefix(merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), prefixes);
        }
        if (!no_cmake_environment_path) {
            find_package_push_env_list(ctx,
                                       merged_prefixes,
                                       &merged_count,
                                       NOB_ARRAY_LEN(merged_prefixes),
                                       "CMAKE_PREFIX_PATH");
        }
        if (!no_cmake_install_prefix) {
            String_View install_prefix = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_INSTALL_PREFIX"));
            if (install_prefix.count > 0) {
                find_package_push_prefix_variants(ctx,
                                                  merged_prefixes,
                                                  &merged_count,
                                                  NOB_ARRAY_LEN(merged_prefixes),
                                                  install_prefix);
            }
        }
    }

    if (!no_default_path && !no_system_environment_path) {
#if defined(_WIN32)
        find_package_push_env_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), "ProgramFiles");
        find_package_push_env_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), "ProgramFiles(x86)");
        find_package_push_env_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), "ProgramW6432");
        find_package_push_env_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), "VCPKG_ROOT");
#endif
    }

    if (!no_default_path && !no_cmake_system_path) {
#if defined(_WIN32)
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), nob_sv_from_cstr("C:/Program Files"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), nob_sv_from_cstr("C:/Program Files (x86)"));
#else
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), nob_sv_from_cstr("/usr/local"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), nob_sv_from_cstr("/usr"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), nob_sv_from_cstr("/opt/local"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), nob_sv_from_cstr("/opt/homebrew"));
        find_package_push_prefix_variants(ctx, merged_prefixes, &merged_count, NOB_ARRAY_LEN(merged_prefixes), nob_sv_from_cstr("/opt"));
#endif
    }

    String_View all_prefixes = eval_sv_join_semi_temp(ctx, merged_prefixes, merged_count);
    if (eval_should_stop(ctx)) return false;
    if (find_package_try_config_in_prefixes(ctx,
                                            current_src,
                                            pkg,
                                            names_csv,
                                            config_names,
                                            config_name_count,
                                            path_suffixes_csv,
                                            all_prefixes,
                                            out_path)) {
        return true;
    }

    return false;
}

typedef struct {
    String_View pkg;
    String_View mode; // MODULE / CONFIG / AUTO
    bool required;
    bool quiet;
    String_View requested_version;
    bool exact_version;
    String_View components;
    String_View optional_components;
    String_View names;
    String_View configs;
    String_View path_suffixes;
    String_View registry_view_value;
    String_View extra_prefixes;
    bool no_default_path;
    bool no_package_root_path;
    bool no_cmake_path;
    bool no_cmake_environment_path;
    bool no_system_environment_path;
    bool no_cmake_system_path;
    bool no_cmake_install_prefix;
    bool no_cmake_package_registry;
    bool no_cmake_system_package_registry;
    bool global_targets;
    bool no_policy_scope;
    bool bypass_provider;
    bool unwind_include;
    bool no_module;
} Find_Package_Options;

enum {
    FIND_PKG_OPT_REQUIRED = 1,
    FIND_PKG_OPT_QUIET,
    FIND_PKG_OPT_MODULE,
    FIND_PKG_OPT_CONFIG,
    FIND_PKG_OPT_EXACT,
    FIND_PKG_OPT_NO_MODULE,
    FIND_PKG_OPT_NAMES,
    FIND_PKG_OPT_CONFIGS,
    FIND_PKG_OPT_PATH_SUFFIXES,
    FIND_PKG_OPT_COMPONENTS,
    FIND_PKG_OPT_OPTIONAL_COMPONENTS,
    FIND_PKG_OPT_HINTS,
    FIND_PKG_OPT_PATHS,
    FIND_PKG_OPT_NO_DEFAULT_PATH,
    FIND_PKG_OPT_NO_PACKAGE_ROOT_PATH,
    FIND_PKG_OPT_NO_CMAKE_PATH,
    FIND_PKG_OPT_NO_CMAKE_ENVIRONMENT_PATH,
    FIND_PKG_OPT_NO_SYSTEM_ENVIRONMENT_PATH,
    FIND_PKG_OPT_NO_CMAKE_SYSTEM_PATH,
    FIND_PKG_OPT_NO_CMAKE_INSTALL_PREFIX,
    FIND_PKG_OPT_NO_CMAKE_PACKAGE_REGISTRY,
    FIND_PKG_OPT_NO_CMAKE_SYSTEM_PACKAGE_REGISTRY,
    FIND_PKG_OPT_REGISTRY_VIEW,
    FIND_PKG_OPT_GLOBAL,
    FIND_PKG_OPT_NO_POLICY_SCOPE,
    FIND_PKG_OPT_BYPASS_PROVIDER,
    FIND_PKG_OPT_UNWIND_INCLUDE,
};

typedef struct {
    Find_Package_Options *out;
    bool module_mode;
    bool config_mode;
    bool force_config_mode;
    SV_List components;
    SV_List optional_components;
    SV_List names;
    SV_List configs;
    SV_List path_suffixes;
    SV_List prefixes;
} Find_Package_Parse_State;

static const Eval_Opt_Spec k_find_pkg_specs[] = {
    {FIND_PKG_OPT_REQUIRED, "REQUIRED", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_QUIET, "QUIET", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_MODULE, "MODULE", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_CONFIG, "CONFIG", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_MODULE, "NO_MODULE", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_EXACT, "EXACT", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NAMES, "NAMES", EVAL_OPT_MULTI},
    {FIND_PKG_OPT_CONFIGS, "CONFIGS", EVAL_OPT_MULTI},
    {FIND_PKG_OPT_PATH_SUFFIXES, "PATH_SUFFIXES", EVAL_OPT_MULTI},
    {FIND_PKG_OPT_COMPONENTS, "COMPONENTS", EVAL_OPT_MULTI},
    {FIND_PKG_OPT_OPTIONAL_COMPONENTS, "OPTIONAL_COMPONENTS", EVAL_OPT_MULTI},
    {FIND_PKG_OPT_HINTS, "HINTS", EVAL_OPT_MULTI},
    {FIND_PKG_OPT_PATHS, "PATHS", EVAL_OPT_MULTI},
    {FIND_PKG_OPT_NO_DEFAULT_PATH, "NO_DEFAULT_PATH", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_PACKAGE_ROOT_PATH, "NO_PACKAGE_ROOT_PATH", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_CMAKE_PATH, "NO_CMAKE_PATH", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_CMAKE_ENVIRONMENT_PATH, "NO_CMAKE_ENVIRONMENT_PATH", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_SYSTEM_ENVIRONMENT_PATH, "NO_SYSTEM_ENVIRONMENT_PATH", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_CMAKE_SYSTEM_PATH, "NO_CMAKE_SYSTEM_PATH", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_CMAKE_INSTALL_PREFIX, "NO_CMAKE_INSTALL_PREFIX", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_CMAKE_PACKAGE_REGISTRY, "NO_CMAKE_PACKAGE_REGISTRY", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_CMAKE_SYSTEM_PACKAGE_REGISTRY, "NO_CMAKE_SYSTEM_PACKAGE_REGISTRY", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_REGISTRY_VIEW, "REGISTRY_VIEW", EVAL_OPT_OPTIONAL_SINGLE},
    {FIND_PKG_OPT_GLOBAL, "GLOBAL", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_NO_POLICY_SCOPE, "NO_POLICY_SCOPE", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_BYPASS_PROVIDER, "BYPASS_PROVIDER", EVAL_OPT_FLAG},
    {FIND_PKG_OPT_UNWIND_INCLUDE, "UNWIND_INCLUDE", EVAL_OPT_FLAG},
};

static bool find_package_looks_like_version(String_View t) {
    if (t.count == 0) return false;
    bool saw_digit = false;
    for (size_t i = 0; i < t.count; i++) {
        char c = t.data[i];
        if (isdigit((unsigned char)c)) {
            saw_digit = true;
            continue;
        }
        if (c == '.' || c == '_' || c == '-' || isalpha((unsigned char)c)) continue;
        return false;
    }
    return saw_digit;
}

static int find_package_version_part_cmp(String_View a, String_View b) {
    bool da = true;
    bool db = true;
    for (size_t i = 0; i < a.count; i++) if (!isdigit((unsigned char)a.data[i])) da = false;
    for (size_t i = 0; i < b.count; i++) if (!isdigit((unsigned char)b.data[i])) db = false;

    if (da && db) {
        size_t ia = 0, ib = 0;
        while (ia < a.count && a.data[ia] == '0') ia++;
        while (ib < b.count && b.data[ib] == '0') ib++;
        size_t na = a.count - ia;
        size_t nb = b.count - ib;
        if (na < nb) return -1;
        if (na > nb) return 1;
        if (na == 0) return 0;
        int c = memcmp(a.data + ia, b.data + ib, na);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }

    size_t n = a.count < b.count ? a.count : b.count;
    int c = memcmp(a.data, b.data, n);
    if (c != 0) return c < 0 ? -1 : 1;
    if (a.count < b.count) return -1;
    if (a.count > b.count) return 1;
    return 0;
}

static int find_package_version_cmp(String_View a, String_View b) {
    size_t pa = 0, pb = 0;
    for (;;) {
        String_View sa = nob_sv_from_cstr("");
        String_View sb = nob_sv_from_cstr("");
        if (pa < a.count) {
            size_t s = pa;
            while (pa < a.count && a.data[pa] != '.') pa++;
            sa = nob_sv_from_parts(a.data + s, pa - s);
            if (pa < a.count) pa++;
        }
        if (pb < b.count) {
            size_t s = pb;
            while (pb < b.count && b.data[pb] != '.') pb++;
            sb = nob_sv_from_parts(b.data + s, pb - s);
            if (pb < b.count) pb++;
        }
        if (sa.count == 0 && sb.count == 0) return 0;
        int c = find_package_version_part_cmp(sa, sb);
        if (c != 0) return c;
    }
}

static bool find_package_parse_on_option(Evaluator_Context *ctx,
                                         void *userdata,
                                         int id,
                                         SV_List values,
                                         size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Find_Package_Parse_State *st = (Find_Package_Parse_State*)userdata;
    switch (id) {
    case FIND_PKG_OPT_REQUIRED:
        st->out->required = true;
        return true;
    case FIND_PKG_OPT_QUIET:
        st->out->quiet = true;
        return true;
    case FIND_PKG_OPT_MODULE:
        st->module_mode = true;
        return true;
    case FIND_PKG_OPT_CONFIG:
        st->config_mode = true;
        return true;
    case FIND_PKG_OPT_NO_MODULE:
        st->out->no_module = true;
        st->config_mode = true;
        return true;
    case FIND_PKG_OPT_EXACT:
        st->out->exact_version = true;
        return true;
    case FIND_PKG_OPT_NAMES:
        st->force_config_mode = true;
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->names, values[i])) return false;
        }
        return true;
    case FIND_PKG_OPT_CONFIGS:
        st->force_config_mode = true;
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->configs, values[i])) return false;
        }
        return true;
    case FIND_PKG_OPT_PATH_SUFFIXES:
        st->force_config_mode = true;
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->path_suffixes, values[i])) return false;
        }
        return true;
    case FIND_PKG_OPT_COMPONENTS:
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->components, values[i])) return false;
        }
        return true;
    case FIND_PKG_OPT_OPTIONAL_COMPONENTS:
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->optional_components, values[i])) return false;
        }
        return true;
    case FIND_PKG_OPT_HINTS:
    case FIND_PKG_OPT_PATHS:
        st->force_config_mode = true;
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->prefixes, values[i])) return false;
        }
        return true;
    case FIND_PKG_OPT_NO_DEFAULT_PATH:
        st->force_config_mode = true;
        st->out->no_default_path = true;
        return true;
    case FIND_PKG_OPT_NO_PACKAGE_ROOT_PATH:
        st->force_config_mode = true;
        st->out->no_package_root_path = true;
        return true;
    case FIND_PKG_OPT_NO_CMAKE_PATH:
        st->force_config_mode = true;
        st->out->no_cmake_path = true;
        return true;
    case FIND_PKG_OPT_NO_CMAKE_ENVIRONMENT_PATH:
        st->force_config_mode = true;
        st->out->no_cmake_environment_path = true;
        return true;
    case FIND_PKG_OPT_NO_SYSTEM_ENVIRONMENT_PATH:
        st->force_config_mode = true;
        st->out->no_system_environment_path = true;
        return true;
    case FIND_PKG_OPT_NO_CMAKE_SYSTEM_PATH:
        st->force_config_mode = true;
        st->out->no_cmake_system_path = true;
        return true;
    case FIND_PKG_OPT_NO_CMAKE_INSTALL_PREFIX:
        st->force_config_mode = true;
        st->out->no_cmake_install_prefix = true;
        return true;
    case FIND_PKG_OPT_NO_CMAKE_PACKAGE_REGISTRY:
        st->force_config_mode = true;
        st->out->no_cmake_package_registry = true;
        return true;
    case FIND_PKG_OPT_NO_CMAKE_SYSTEM_PACKAGE_REGISTRY:
        st->force_config_mode = true;
        st->out->no_cmake_system_package_registry = true;
        return true;
    case FIND_PKG_OPT_REGISTRY_VIEW:
        if (arena_arr_len(values) > 0) st->out->registry_view_value = values[0];
        return true;
    case FIND_PKG_OPT_GLOBAL:
        st->out->global_targets = true;
        return true;
    case FIND_PKG_OPT_NO_POLICY_SCOPE:
        st->out->no_policy_scope = true;
        return true;
    case FIND_PKG_OPT_BYPASS_PROVIDER:
        st->out->bypass_provider = true;
        return true;
    case FIND_PKG_OPT_UNWIND_INCLUDE:
        st->out->unwind_include = true;
        return true;
    default:
        return true;
    }
}

static bool find_package_parse_on_positional(Evaluator_Context *ctx,
                                             void *userdata,
                                             String_View value,
                                             size_t token_index) {
    (void)ctx;
    (void)token_index;
    if (!userdata) return false;
    Find_Package_Parse_State *st = (Find_Package_Parse_State*)userdata;
    if (st->out->requested_version.count == 0 && find_package_looks_like_version(value)) {
        st->out->requested_version = value;
    }
    return true;
}

static Find_Package_Options find_package_parse_options(Evaluator_Context *ctx, SV_List args) {
    Find_Package_Options out = {0};
    out.pkg = arena_arr_len(args) > 0 ? args[0] : nob_sv_from_cstr("");
    out.mode = nob_sv_from_cstr("AUTO");

    Find_Package_Parse_State st = {
        .out = &out,
        .module_mode = false,
        .config_mode = false,
        .force_config_mode = false,
        .components = NULL,
        .optional_components = NULL,
        .names = NULL,
        .configs = NULL,
        .path_suffixes = NULL,
        .prefixes = NULL,
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = nob_sv_from_cstr("find_package"),
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = (Cmake_Event_Origin){0};
    if (!eval_opt_parse_walk(ctx,
                             args,
                             1,
                             k_find_pkg_specs,
                             NOB_ARRAY_LEN(k_find_pkg_specs),
                             cfg,
                             find_package_parse_on_option,
                             find_package_parse_on_positional,
                             &st)) {
        return out;
    }

    if (st.module_mode && !st.config_mode && !st.force_config_mode && !out.no_module) {
        out.mode = nob_sv_from_cstr("MODULE");
    } else if (st.config_mode || st.force_config_mode || out.no_module) {
        out.mode = nob_sv_from_cstr("CONFIG");
    }
    if (arena_arr_len(st.components) > 0) out.components = eval_sv_join_semi_temp(ctx, st.components, arena_arr_len(st.components));
    if (arena_arr_len(st.optional_components) > 0) {
        out.optional_components = eval_sv_join_semi_temp(ctx, st.optional_components, arena_arr_len(st.optional_components));
    }
    if (arena_arr_len(st.names) > 0) out.names = eval_sv_join_semi_temp(ctx, st.names, arena_arr_len(st.names));
    if (arena_arr_len(st.configs) > 0) out.configs = eval_sv_join_semi_temp(ctx, st.configs, arena_arr_len(st.configs));
    if (arena_arr_len(st.path_suffixes) > 0) {
        out.path_suffixes = eval_sv_join_semi_temp(ctx, st.path_suffixes, arena_arr_len(st.path_suffixes));
    }
    if (arena_arr_len(st.prefixes) > 0) out.extra_prefixes = eval_sv_join_semi_temp(ctx, st.prefixes, arena_arr_len(st.prefixes));
    return out;
}

static bool find_package_resolve(Evaluator_Context *ctx,
                                 const Find_Package_Options *opt,
                                 String_View *out_found_path) {
    if (!ctx || !opt || !out_found_path) return false;
    *out_found_path = nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(opt->mode, "MODULE")) {
        return find_package_try_module(ctx,
                                       opt->pkg,
                                       opt->names,
                                       opt->extra_prefixes,
                                       opt->no_default_path,
                                       opt->no_cmake_path,
                                       opt->no_cmake_environment_path,
                                       out_found_path);
    }
    if (eval_sv_eq_ci_lit(opt->mode, "CONFIG")) {
        return find_package_try_config(ctx,
                                       opt->pkg,
                                       opt->names,
                                       opt->configs,
                                       opt->path_suffixes,
                                       opt->extra_prefixes,
                                       opt->no_default_path,
                                       opt->no_package_root_path,
                                       opt->no_cmake_path,
                                       opt->no_cmake_environment_path,
                                       opt->no_system_environment_path,
                                       opt->no_cmake_system_path,
                                       opt->no_cmake_install_prefix,
                                       out_found_path);
    }

    bool prefer_config = eval_truthy(ctx, eval_var_get(ctx, nob_sv_from_cstr("CMAKE_FIND_PACKAGE_PREFER_CONFIG")));
    bool found = false;
    if (prefer_config) {
        found = find_package_try_config(ctx,
                                        opt->pkg,
                                        opt->names,
                                        opt->configs,
                                        opt->path_suffixes,
                                        opt->extra_prefixes,
                                        opt->no_default_path,
                                        opt->no_package_root_path,
                                        opt->no_cmake_path,
                                        opt->no_cmake_environment_path,
                                        opt->no_system_environment_path,
                                        opt->no_cmake_system_path,
                                        opt->no_cmake_install_prefix,
                                        out_found_path);
        if (!found) {
            found = find_package_try_module(ctx,
                                            opt->pkg,
                                            opt->names,
                                            opt->extra_prefixes,
                                            opt->no_default_path,
                                            opt->no_cmake_path,
                                            opt->no_cmake_environment_path,
                                            out_found_path);
        }
    } else {
        found = find_package_try_module(ctx,
                                        opt->pkg,
                                        opt->names,
                                        opt->extra_prefixes,
                                        opt->no_default_path,
                                        opt->no_cmake_path,
                                        opt->no_cmake_environment_path,
                                        out_found_path);
        if (!found) {
            found = find_package_try_config(ctx,
                                            opt->pkg,
                                            opt->names,
                                            opt->configs,
                                            opt->path_suffixes,
                                            opt->extra_prefixes,
                                            opt->no_default_path,
                                            opt->no_package_root_path,
                                            opt->no_cmake_path,
                                            opt->no_cmake_environment_path,
                                            opt->no_system_environment_path,
                                            opt->no_cmake_system_path,
                                            opt->no_cmake_install_prefix,
                                            out_found_path);
        }
    }
    return found;
}

static String_View find_package_guess_version_path(Evaluator_Context *ctx,
                                                   String_View config_path) {
    if (!ctx || config_path.count == 0) return nob_sv_from_cstr("");
    String_View dir = svu_dirname(config_path);
    size_t name_off = 0;
    for (size_t i = config_path.count; i-- > 0;) {
        char c = config_path.data[i];
        if (c == '/' || c == '\\') {
            name_off = i + 1;
            break;
        }
    }
    size_t name_len = config_path.count - name_off;

    String_View base = nob_sv_from_parts(config_path.data + name_off, name_len);
    if (base.count >= 12 && eval_sv_eq_ci_lit(nob_sv_from_parts(base.data + base.count - 12, 12), "Config.cmake")) {
        String_View stem = nob_sv_from_parts(base.data, base.count - 12);
        String_View version_name = svu_concat_suffix_temp(ctx, stem, "ConfigVersion.cmake");
        return eval_sv_path_join(eval_temp_arena(ctx), dir, version_name);
    }
    if (base.count >= 13 && eval_sv_eq_ci_lit(nob_sv_from_parts(base.data + base.count - 13, 13), "-config.cmake")) {
        String_View stem = nob_sv_from_parts(base.data, base.count - 13);
        String_View version_name = svu_concat_suffix_temp(ctx, stem, "-config-version.cmake");
        return eval_sv_path_join(eval_temp_arena(ctx), dir, version_name);
    }
    return nob_sv_from_cstr("");
}

static bool find_package_requested_version_matches(const Find_Package_Options *opt,
                                                   String_View actual_version) {
    if (!opt || opt->requested_version.count == 0) return true;
    if (actual_version.count == 0) return false;
    int c = find_package_version_cmp(actual_version, opt->requested_version);
    if (opt->exact_version) return c == 0;
    return c >= 0;
}

static void find_package_seed_find_context_vars(Evaluator_Context *ctx, const Find_Package_Options *opt) {
    if (!ctx || !opt) return;
    String_View key_required = svu_concat_suffix_temp(ctx, opt->pkg, "_FIND_REQUIRED");
    String_View key_quiet = svu_concat_suffix_temp(ctx, opt->pkg, "_FIND_QUIETLY");
    String_View key_ver = svu_concat_suffix_temp(ctx, opt->pkg, "_FIND_VERSION");
    String_View key_exact = svu_concat_suffix_temp(ctx, opt->pkg, "_FIND_VERSION_EXACT");
    String_View key_comps = svu_concat_suffix_temp(ctx, opt->pkg, "_FIND_COMPONENTS");
    String_View key_req_comps = svu_concat_suffix_temp(ctx, opt->pkg, "_FIND_REQUIRED_COMPONENTS");
    String_View key_opt_comps = svu_concat_suffix_temp(ctx, opt->pkg, "_FIND_OPTIONAL_COMPONENTS");
    String_View key_registry_view = svu_concat_suffix_temp(ctx, opt->pkg, "_FIND_REGISTRY_VIEW");

    (void)eval_var_set(ctx, key_required, opt->required ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    (void)eval_var_set(ctx, key_quiet, opt->quiet ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    if (opt->requested_version.count > 0) {
        (void)eval_var_set(ctx, key_ver, opt->requested_version);
        (void)eval_var_set(ctx, key_exact, opt->exact_version ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    }
    if (opt->components.count > 0) {
        (void)eval_var_set(ctx, key_comps, opt->components);
        (void)eval_var_set(ctx, key_req_comps, opt->components);
    }
    if (opt->optional_components.count > 0) (void)eval_var_set(ctx, key_opt_comps, opt->optional_components);
    if (opt->registry_view_value.count > 0) (void)eval_var_set(ctx, key_registry_view, opt->registry_view_value);
}

static void find_package_publish_vars(Evaluator_Context *ctx,
                                      const Find_Package_Options *opt,
                                      bool *io_found,
                                      String_View found_path) {
    String_View found_key = svu_concat_suffix_temp(ctx, opt->pkg, "_FOUND");
    String_View dir_key = svu_concat_suffix_temp(ctx, opt->pkg, "_DIR");
    String_View cfg_key = svu_concat_suffix_temp(ctx, opt->pkg, "_CONFIG");

    if (*io_found) {
        String_View dir = svu_dirname(found_path);
        (void)eval_var_set(ctx, dir_key, dir);
        (void)eval_var_set(ctx, cfg_key, found_path);

        find_package_seed_find_context_vars(ctx, opt);

        bool pushed_pkg = false;
        String_View active_pkg = sv_copy_to_event_arena(ctx, opt->pkg);
        if (!eval_should_stop(ctx)) {
            pushed_pkg = arena_arr_push(ctx->event_arena, ctx->active_find_packages, active_pkg);
            if (!pushed_pkg) ctx_oom(ctx);
        }

        bool version_ok = true;
        if (opt->requested_version.count > 0) {
            String_View version_path = find_package_guess_version_path(ctx, found_path);
            if (version_path.count > 0 && file_exists_sv(ctx, version_path)) {
                (void)eval_var_set(ctx, nob_sv_from_cstr("PACKAGE_VERSION"), nob_sv_from_cstr(""));
                (void)eval_var_set(ctx, nob_sv_from_cstr("PACKAGE_VERSION_EXACT"), nob_sv_from_cstr(""));
                (void)eval_var_set(ctx, nob_sv_from_cstr("PACKAGE_VERSION_COMPATIBLE"), nob_sv_from_cstr(""));
                if (!eval_execute_file(ctx, version_path, false, nob_sv_from_cstr(""))) {
                    version_ok = false;
                } else {
                    String_View exact = eval_var_get(ctx, nob_sv_from_cstr("PACKAGE_VERSION_EXACT"));
                    String_View compat = eval_var_get(ctx, nob_sv_from_cstr("PACKAGE_VERSION_COMPATIBLE"));
                    if (opt->exact_version && exact.count > 0) {
                        version_ok = eval_truthy(ctx, exact);
                    } else if (!opt->exact_version && compat.count > 0) {
                        version_ok = eval_truthy(ctx, compat);
                    } else {
                        version_ok = find_package_requested_version_matches(opt, eval_var_get(ctx, nob_sv_from_cstr("PACKAGE_VERSION")));
                    }
                }
            }
        }

        if (version_ok) {
            if (!eval_execute_file(ctx, found_path, false, nob_sv_from_cstr(""))) {
                *io_found = false;
            }
        } else {
            *io_found = false;
        }

        if (pushed_pkg && arena_arr_len(ctx->active_find_packages) > 0) {
            arena_arr_set_len(ctx->active_find_packages, arena_arr_len(ctx->active_find_packages) - 1);
        }

        if (*io_found && opt->requested_version.count > 0) {
            String_View pkg_ver_key = svu_concat_suffix_temp(ctx, opt->pkg, "_VERSION");
            String_View actual = eval_var_get(ctx, pkg_ver_key);
            if (actual.count == 0) actual = eval_var_get(ctx, nob_sv_from_cstr("PACKAGE_VERSION"));
            if (!find_package_requested_version_matches(opt, actual)) {
                *io_found = false;
            }
        }
    }

    if (*io_found) {
        if (eval_var_defined(ctx, found_key)) {
            *io_found = eval_truthy(ctx, eval_var_get(ctx, found_key));
        }
    }
    (void)eval_var_set(ctx, found_key, *io_found ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
}

static void find_package_emit_result(Evaluator_Context *ctx,
                                     const Node *node,
                                     Cmake_Event_Origin o,
                                     const Find_Package_Options *opt,
                                     bool found,
                                     String_View found_path) {
    if (!found && opt->required) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Required package not found"),
                       opt->pkg);
        eval_request_stop_on_error(ctx);
    } else if (!found && !opt->quiet) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Package not found"),
                       opt->pkg);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_FIND_PACKAGE;
    ev.origin = o;
    ev.as.find_package.package_name = sv_copy_to_event_arena(ctx, opt->pkg);
    ev.as.find_package.mode = sv_copy_to_event_arena(ctx, opt->mode);
    ev.as.find_package.required = opt->required;
    ev.as.find_package.found = found;
    ev.as.find_package.location = sv_copy_to_event_arena(ctx, found_path);
    (void)emit_event(ctx, ev);
}

bool eval_handle_find_package(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(a) < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("find_package() missing package name"),
                       nob_sv_from_cstr("Usage: find_package(<Pkg> [REQUIRED] [MODULE|CONFIG])"));
        return !eval_should_stop(ctx);
    }

    Find_Package_Options opt = find_package_parse_options(ctx, a);
    if (opt.exact_version && opt.requested_version.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("find_package() EXACT specified without version"),
                       nob_sv_from_cstr("EXACT is ignored when no version is requested"));
    }
    String_View found_path = nob_sv_from_cstr("");
    bool found = find_package_resolve(ctx, &opt, &found_path);
    find_package_publish_vars(ctx, &opt, &found, found_path);
    find_package_emit_result(ctx, node, o, &opt, found, found_path);
    return !eval_should_stop(ctx);
}

bool eval_handle_find_program(Evaluator_Context *ctx, const Node *node) {
    return find_item_handle(ctx, node, FIND_ITEM_PROGRAM);
}

bool eval_handle_find_file(Evaluator_Context *ctx, const Node *node) {
    return find_item_handle(ctx, node, FIND_ITEM_FILE);
}

bool eval_handle_find_path(Evaluator_Context *ctx, const Node *node) {
    return find_item_handle(ctx, node, FIND_ITEM_PATH);
}

bool eval_handle_find_library(Evaluator_Context *ctx, const Node *node) {
    return find_item_handle(ctx, node, FIND_ITEM_LIBRARY);
}
