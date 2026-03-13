#include "eval_target_internal.h"

static bool target_usage_store_target_property(Evaluator_Context *ctx,
                                               Cmake_Event_Origin origin,
                                               String_View target_name,
                                               String_View key,
                                               String_View value,
                                               Cmake_Target_Property_Op op) {
    return eval_property_write(ctx,
                               origin,
                               nob_sv_from_cstr("TARGET"),
                               target_name,
                               key,
                               value,
                               op,
                               false);
}

static bool target_usage_store_and_emit_target_property(Evaluator_Context *ctx,
                                                        Cmake_Event_Origin origin,
                                                        String_View target_name,
                                                        String_View key,
                                                        String_View value,
                                                        Cmake_Target_Property_Op op) {
    if (!target_usage_store_target_property(ctx, origin, target_name, key, value, op)) return false;
    return eval_emit_target_prop_set(ctx, origin, target_name, key, value, op);
}

typedef struct {
    String_View name;
    String_View type;
    int kind;
    SV_List base_dirs;
    SV_List files;
    size_t next_index;
} Target_File_Set_Parse;

typedef enum {
    TARGET_FILE_SET_HEADERS = 0,
    TARGET_FILE_SET_CXX_MODULES,
} Target_File_Set_Kind;

static bool target_sources_is_file_set_clause_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "TYPE") ||
           eval_sv_eq_ci_lit(tok, "BASE_DIRS") ||
           eval_sv_eq_ci_lit(tok, "FILES");
}

static bool target_sources_is_group_boundary(String_View tok) {
    return target_usage_parse_visibility(tok, &(Cmake_Visibility){0}) ||
           eval_sv_eq_ci_lit(tok, "FILE_SET");
}

static bool target_sources_file_set_kind_from_type(String_View type, Target_File_Set_Kind *out_kind) {
    if (!out_kind) return false;
    if (eval_sv_eq_ci_lit(type, "HEADERS")) {
        *out_kind = TARGET_FILE_SET_HEADERS;
        return true;
    }
    if (eval_sv_eq_ci_lit(type, "CXX_MODULES")) {
        *out_kind = TARGET_FILE_SET_CXX_MODULES;
        return true;
    }
    return false;
}

static bool target_sources_append_file_set_items(Evaluator_Context *ctx,
                                                 SV_List args,
                                                 size_t *io_index,
                                                 String_View current_src_dir,
                                                 SV_List *out_items) {
    if (!ctx || !io_index || !out_items) return false;
    while (*io_index < arena_arr_len(args)) {
        String_View tok = args[*io_index];
        if (target_sources_is_group_boundary(tok) || target_sources_is_file_set_clause_keyword(tok)) break;
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, tok, current_src_dir, true);
        if (eval_should_stop(ctx)) return false;
        if (!svu_list_push_temp(ctx, out_items, resolved)) return false;
        (*io_index)++;
    }
    return true;
}

static bool target_sources_parse_file_set(Evaluator_Context *ctx,
                                          const Node *node,
                                          Cmake_Event_Origin origin,
                                          SV_List args,
                                          size_t start,
                                          Target_File_Set_Parse *out_parse) {
    if (!ctx || !node || !out_parse) return false;
    *out_parse = (Target_File_Set_Parse){0};

    if (start >= arena_arr_len(args) || target_sources_is_group_boundary(args[start])) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) requires a set name"),
                          nob_sv_from_cstr("Usage: target_sources(<tgt> <vis> FILE_SET <name> [TYPE HEADERS|CXX_MODULES] [BASE_DIRS <dir>...] FILES <file>...)"));
        return false;
    }

    out_parse->name = args[start++];
    if (eval_sv_eq_ci_lit(out_parse->name, "HEADERS")) {
        out_parse->type = nob_sv_from_cstr("HEADERS");
    } else if (eval_sv_eq_ci_lit(out_parse->name, "CXX_MODULES")) {
        out_parse->type = nob_sv_from_cstr("CXX_MODULES");
    }

    bool saw_base_dirs = false;
    bool saw_files = false;
    String_View current_src_dir = eval_current_source_dir_for_paths(ctx);

    while (start < arena_arr_len(args)) {
        String_View tok = args[start];
        if (target_sources_is_group_boundary(tok)) break;

        if (eval_sv_eq_ci_lit(tok, "TYPE")) {
            if (start + 1 >= arena_arr_len(args) ||
                target_sources_is_group_boundary(args[start + 1]) ||
                target_sources_is_file_set_clause_keyword(args[start + 1])) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("target_sources(FILE_SET TYPE) requires a file-set type"),
                                  nob_sv_from_cstr("Supported types in this batch: HEADERS, CXX_MODULES"));
                return false;
            }
            out_parse->type = args[start + 1];
            start += 2;
            continue;
        }

        if (eval_sv_eq_ci_lit(tok, "BASE_DIRS")) {
            start++;
            size_t before = arena_arr_len(out_parse->base_dirs);
            if (!target_sources_append_file_set_items(ctx,
                                                      args,
                                                      &start,
                                                      current_src_dir,
                                                      &out_parse->base_dirs)) {
                return false;
            }
            if (arena_arr_len(out_parse->base_dirs) == before) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("target_sources(FILE_SET BASE_DIRS) requires at least one base directory"),
                                  nob_sv_from_cstr(""));
                return false;
            }
            saw_base_dirs = true;
            continue;
        }

        if (eval_sv_eq_ci_lit(tok, "FILES")) {
            start++;
            size_t before = arena_arr_len(out_parse->files);
            if (!target_sources_append_file_set_items(ctx,
                                                      args,
                                                      &start,
                                                      current_src_dir,
                                                      &out_parse->files)) {
                return false;
            }
            if (arena_arr_len(out_parse->files) == before) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("target_sources(FILE_SET FILES) requires at least one file"),
                                  nob_sv_from_cstr(""));
                return false;
            }
            saw_files = true;
            continue;
        }

        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) received an unexpected argument"),
                          tok);
        return false;
    }

    if (out_parse->type.count == 0) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) requires TYPE for non-default file-set names"),
                          nob_sv_from_cstr("Use FILE_SET HEADERS ..., FILE_SET CXX_MODULES ..., or FILE_SET <name> TYPE HEADERS|CXX_MODULES ..."));
        return false;
    }
    Target_File_Set_Kind kind = TARGET_FILE_SET_HEADERS;
    if (!target_sources_file_set_kind_from_type(out_parse->type, &kind)) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) currently supports only TYPE HEADERS or TYPE CXX_MODULES"),
                          out_parse->type);
        return false;
    }
    out_parse->kind = (int)kind;
    if (!saw_base_dirs) {
        if (!svu_list_push_temp(ctx, &out_parse->base_dirs, current_src_dir)) return false;
    }
    if (!saw_files) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) requires FILES"),
                          nob_sv_from_cstr(""));
        return false;
    }

    out_parse->next_index = start;
    (void)origin;
    return true;
}

static bool target_sources_store_file_set(Evaluator_Context *ctx,
                                          Cmake_Event_Origin origin,
                                          String_View target_name,
                                          Cmake_Visibility vis,
                                          const Target_File_Set_Parse *file_set) {
    if (!ctx || !file_set) return false;
    String_View set_name_upper = eval_property_upper_name_temp(ctx, file_set->name);
    if (eval_should_stop(ctx)) return false;

    bool is_headers = file_set->kind == TARGET_FILE_SET_HEADERS;
    bool is_default_set =
        (is_headers && eval_sv_eq_ci_lit(file_set->name, "HEADERS")) ||
        (!is_headers && eval_sv_eq_ci_lit(file_set->name, "CXX_MODULES"));
    const char *sets_prop_name = is_headers ? "HEADER_SETS" : "CXX_MODULE_SETS";
    const char *interface_sets_prop_name = is_headers ? "INTERFACE_HEADER_SETS" : "INTERFACE_CXX_MODULE_SETS";
    const char *set_prop_prefix = is_headers ? "HEADER_SET_" : "CXX_MODULE_SET_";
    const char *dirs_prop_prefix = is_headers ? "HEADER_DIRS_" : "CXX_MODULE_DIRS_";
    const char *set_prop_default = is_headers ? "HEADER_SET" : "CXX_MODULE_SET";
    const char *dirs_prop_default = is_headers ? "HEADER_DIRS" : "CXX_MODULE_DIRS";

    String_View set_prop = nob_sv_from_cstr(nob_temp_sprintf("%s%.*s",
                                                             set_prop_prefix,
                                                             (int)set_name_upper.count,
                                                             set_name_upper.data));
    String_View dirs_prop = nob_sv_from_cstr(nob_temp_sprintf("%s%.*s",
                                                              dirs_prop_prefix,
                                                              (int)set_name_upper.count,
                                                              set_name_upper.data));
    if (vis != EV_VISIBILITY_INTERFACE) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         nob_sv_from_cstr(sets_prop_name),
                                                         file_set->name,
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
    }
    if (vis != EV_VISIBILITY_PRIVATE) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         nob_sv_from_cstr(interface_sets_prop_name),
                                                         file_set->name,
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(file_set->base_dirs); i++) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         dirs_prop,
                                                         file_set->base_dirs[i],
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
        if (is_default_set &&
            !target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         nob_sv_from_cstr(dirs_prop_default),
                                                         file_set->base_dirs[i],
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(file_set->files); i++) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         set_prop,
                                                         file_set->files[i],
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
        if (is_default_set &&
            !target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         nob_sv_from_cstr(set_prop_default),
                                                         file_set->files[i],
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
    }
    return true;
}

Eval_Result eval_handle_add_dependencies(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_dependencies() requires target and at least one dependency"), nob_sv_from_cstr("Usage: add_dependencies(<target> <target-dependency>...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View target_name = a[0];
    if (!eval_target_known(ctx, target_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("add_dependencies() target was not declared"), target_name);
        return eval_result_from_ctx(ctx);
    }
    if (eval_target_alias_known(ctx, target_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_dependencies() cannot be used on ALIAS targets"), target_name);
        return eval_result_from_ctx(ctx);
    }

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        String_View dep = a[i];
        if (dep.count == 0) continue;

        if (!eval_target_known(ctx, dep)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("add_dependencies() dependency target was not declared"), dep);
            return eval_result_from_ctx(ctx);
        }
        if (eval_target_alias_known(ctx, dep)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_dependencies() cannot depend on ALIAS targets"), dep);
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_dependency(ctx, o, target_name, dep)) return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || arena_arr_len(a) < 2) return eval_result_from_ctx(ctx);

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View qualifier = nob_sv_from_cstr("");

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "DEBUG") ||
            eval_sv_eq_ci_lit(a[i], "OPTIMIZED") ||
            eval_sv_eq_ci_lit(a[i], "GENERAL")) {
            qualifier = a[i];
            continue;
        }

        String_View item = a[i];
        if (eval_sv_eq_ci_lit(qualifier, "DEBUG")) {
            item = wrap_link_item_with_config_genex_temp(ctx,
                                                         item,
                                                         nob_sv_from_cstr("$<$<CONFIG:Debug>:"));
        } else if (eval_sv_eq_ci_lit(qualifier, "OPTIMIZED")) {
            item = wrap_link_item_with_config_genex_temp(ctx,
                                                         item,
                                                         nob_sv_from_cstr("$<$<NOT:$<CONFIG:Debug>>:"));
        }

        (void)vis;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("LINK_LIBRARIES"),
                                 item,
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_link_libraries(ctx, o, tgt, vis, item)) return eval_result_from_ctx(ctx);
        qualifier = nob_sv_from_cstr("");
    }

    if (qualifier.count > 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("target_link_libraries() qualifier without following item"), qualifier);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_link_options() requires target and items"), nob_sv_from_cstr("Usage: target_link_options(<tgt> [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_before = false;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        (void)vis;
        (void)is_before;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("LINK_OPTIONS"),
                                 a[i],
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_link_options(ctx, o, tgt, vis, a[i], is_before)) return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_link_directories() requires target and items"), nob_sv_from_cstr("Usage: target_link_directories(<tgt> <PUBLIC|PRIVATE|INTERFACE> <dirs...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        (void)vis;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("LINK_DIRECTORIES"),
                                 resolved,
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_link_directories(ctx, o, tgt, vis, resolved)) return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_include_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_include_directories() requires target and items"), nob_sv_from_cstr("Usage: target_include_directories(<tgt> [SYSTEM] [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_system = false;
    bool is_before = false;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "SYSTEM")) {
            is_system = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "AFTER")) {
            is_before = false;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        (void)vis;
        (void)is_system;
        (void)is_before;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("INCLUDE_DIRECTORIES"),
                                 resolved,
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_include_directories(ctx, o, tgt, vis, resolved, is_system, is_before)) {
            return eval_result_from_ctx(ctx);
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_compile_definitions() requires target and items"), nob_sv_from_cstr("Usage: target_compile_definitions(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        String_View item = eval_normalize_compile_definition_item(a[i]);
        if (item.count == 0) continue;

        (void)vis;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("COMPILE_DEFINITIONS"),
                                 item,
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_compile_definitions(ctx, o, tgt, vis, item)) return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_compile_options() requires target and items"), nob_sv_from_cstr("Usage: target_compile_options(<tgt> [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_before = false;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        (void)vis;
        (void)is_before;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("COMPILE_OPTIONS"),
                                 a[i],
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_compile_options(ctx, o, tgt, vis, a[i], is_before)) return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_sources(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_sources() requires target, visibility and items"), nob_sv_from_cstr("Usage: target_sources(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (eval_sv_eq_ci_lit(a[i], "FILE_SET")) {
            if (vis == EV_VISIBILITY_UNSPECIFIED) {
                (void)target_usage_require_visibility(ctx, node);
                return eval_result_from_ctx(ctx);
            }
            Target_File_Set_Parse file_set = {0};
            if (!target_sources_parse_file_set(ctx, node, o, a, i + 1, &file_set)) {
                return eval_result_from_ctx(ctx);
            }
            if (file_set.kind == TARGET_FILE_SET_CXX_MODULES &&
                vis == EV_VISIBILITY_INTERFACE &&
                !eval_target_is_imported(ctx, tgt)) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("target_sources(FILE_SET TYPE CXX_MODULES) may not use INTERFACE scope on non-IMPORTED targets"),
                                  tgt);
                return eval_result_from_ctx(ctx);
            }
            if (!target_sources_store_file_set(ctx, o, tgt, vis, &file_set)) {
                return eval_result_from_ctx(ctx);
            }
            i = file_set.next_index - 1;
            continue;
        }
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return eval_result_from_ctx(ctx);
        }

        String_View item = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!target_usage_store_target_property(ctx,
                                                    o,
                                                    tgt,
                                                    nob_sv_from_cstr("SOURCES"),
                                                    item,
                                                    EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
            if (!eval_emit_target_add_source(ctx, o, tgt, item)) return eval_result_from_ctx(ctx);
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!target_usage_store_and_emit_target_property(ctx,
                                                             o,
                                                             tgt,
                                                             nob_sv_from_cstr("INTERFACE_SOURCES"),
                                                             item,
                                                             EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_features(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_compile_features() requires target, visibility and features"), nob_sv_from_cstr("Usage: target_compile_features(<tgt> <PUBLIC|PRIVATE|INTERFACE> <features...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return eval_result_from_ctx(ctx);
        }
        if (a[i].count == 0) continue;

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!target_usage_store_and_emit_target_property(ctx,
                                                             o,
                                                             tgt,
                                                             nob_sv_from_cstr("COMPILE_FEATURES"),
                                                             a[i],
                                                             EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!target_usage_store_and_emit_target_property(ctx,
                                                             o,
                                                             tgt,
                                                             nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES"),
                                                             a[i],
                                                             EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_precompile_headers(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_precompile_headers() requires target and arguments"), nob_sv_from_cstr("Usage: target_precompile_headers(<tgt> <PUBLIC|PRIVATE|INTERFACE> <headers...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);

    if (eval_sv_eq_ci_lit(a[1], "REUSE_FROM")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_precompile_headers(REUSE_FROM) expects exactly one donor target"), nob_sv_from_cstr("Usage: target_precompile_headers(<tgt> REUSE_FROM <other-target>)"));
            return eval_result_from_ctx(ctx);
        }
        if (!target_usage_validate_target(ctx, node, a[2])) return eval_result_from_ctx(ctx);
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         o,
                                                         tgt,
                                                         nob_sv_from_cstr("PRECOMPILE_HEADERS_REUSE_FROM"),
                                                         a[2],
                                                         EV_PROP_SET)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_sv_key_eq(tgt, a[2])) {
            if (!eval_emit_target_dependency(ctx, o, tgt, a[2])) return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return eval_result_from_ctx(ctx);
        }

        String_View item = target_pch_item_normalize_temp(ctx, a[i]);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (item.count == 0) continue;

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!target_usage_store_and_emit_target_property(ctx,
                                                             o,
                                                             tgt,
                                                             nob_sv_from_cstr("PRECOMPILE_HEADERS"),
                                                             item,
                                                             EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!target_usage_store_and_emit_target_property(ctx,
                                                             o,
                                                             tgt,
                                                             nob_sv_from_cstr("INTERFACE_PRECOMPILE_HEADERS"),
                                                             item,
                                                             EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
    }

    return eval_result_from_ctx(ctx);
}
