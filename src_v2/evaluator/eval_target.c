#include "eval_target_internal.h"

static bool define_property_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "PROPERTY") ||
           eval_sv_eq_ci_lit(tok, "INHERITED") ||
           eval_sv_eq_ci_lit(tok, "BRIEF_DOCS") ||
           eval_sv_eq_ci_lit(tok, "FULL_DOCS") ||
           eval_sv_eq_ci_lit(tok, "INITIALIZE_FROM_VARIABLE");
}

static bool target_get_declared_dir_temp(Evaluator_Context *ctx,
                                         String_View target_name,
                                         String_View *out_dir) {
    if (!ctx || !out_dir) return false;
    (void)target_name;
    *out_dir = eval_current_source_dir_for_paths(ctx);
    return true;
}

Eval_Result eval_handle_get_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 4) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_property() requires output variable, scope, PROPERTY and property name"), nob_sv_from_cstr("Usage: get_property(<var> <GLOBAL|DIRECTORY|TARGET|SOURCE|INSTALL|TEST|CACHE|VARIABLE> ... PROPERTY <name> [SET|DEFINED|BRIEF_DOCS|FULL_DOCS])"));
        return eval_result_from_ctx(ctx);
    }

    String_View out_var = a[0];
    String_View scope_upper = nob_sv_from_cstr("");
    if (!eval_property_scope_upper_temp(ctx, a[1], &scope_upper)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_property() unknown scope"), a[1]);
        return eval_result_from_ctx(ctx);
    }

    size_t i = 2;
    String_View object_token = nob_sv_from_cstr("");
    String_View object_id = nob_sv_from_cstr("");
    String_View inherit_dir = nob_sv_from_cstr("");
    bool saw_source_dir_clause = false;
    bool saw_source_target_dir_clause = false;
    bool saw_test_dir_clause = false;

    if (!(eval_sv_eq_ci_lit(scope_upper, "GLOBAL") || eval_sv_eq_ci_lit(scope_upper, "VARIABLE"))) {
        if (i >= arena_arr_len(a)) {
            target_diag_error(ctx,
                              node,
                              nob_sv_from_cstr("get_property() missing scope object"),
                              nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }

        if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) {
            if (!eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
                object_token = a[i++];
            }
        } else {
            object_token = a[i++];
        }
    }

    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        if (object_token.count == 0) object_id = cur_src;
        else object_id = eval_path_resolve_for_cmake_arg(ctx, object_token, cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        inherit_dir = object_id;
    } else if (eval_sv_eq_ci_lit(scope_upper, "SOURCE")) {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
            if (eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
                if (saw_source_dir_clause || saw_source_target_dir_clause || i + 1 >= arena_arr_len(a)) {
                    target_diag_error(ctx,
                                      node,
                                      nob_sv_from_cstr("get_property(SOURCE DIRECTORY ...) requires exactly one directory"),
                                      nob_sv_from_cstr(""));
                    return eval_result_from_ctx(ctx);
                }
                inherit_dir = eval_path_resolve_for_cmake_arg(ctx, a[++i], cur_src, true);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
                saw_source_dir_clause = true;
                i++;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "TARGET_DIRECTORY")) {
                if (saw_source_dir_clause || saw_source_target_dir_clause || i + 1 >= arena_arr_len(a)) {
                    target_diag_error(ctx,
                                      node,
                                      nob_sv_from_cstr("get_property(SOURCE TARGET_DIRECTORY ...) requires exactly one target"),
                                      nob_sv_from_cstr(""));
                    return eval_result_from_ctx(ctx);
                }
                String_View target_name = a[++i];
                if (!eval_target_known(ctx, target_name)) {
                    target_diag_error(ctx,
                                      node,
                                      nob_sv_from_cstr("get_property(SOURCE TARGET_DIRECTORY ...) target was not declared"),
                                      target_name);
                    return eval_result_from_ctx(ctx);
                }
                if (!target_get_declared_dir_temp(ctx, target_name, &inherit_dir)) return eval_result_from_ctx(ctx);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
                object_id = eval_property_scoped_object_id_temp(ctx, "TARGET_DIRECTORY", target_name, object_token);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
                saw_source_target_dir_clause = true;
                i++;
                continue;
            }
            target_diag_error(ctx,
                              node,
                              nob_sv_from_cstr("get_property(SOURCE ...) received unexpected argument before PROPERTY"),
                              a[i]);
            return eval_result_from_ctx(ctx);
        }
        if (!saw_source_target_dir_clause) {
            if (saw_source_dir_clause) object_id = eval_property_scoped_object_id_temp(ctx, "DIRECTORY", inherit_dir, object_token);
            else {
                object_id = object_token;
                inherit_dir = cur_src;
            }
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        }
    } else if (eval_sv_eq_ci_lit(scope_upper, "TEST")) {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
            if (!eval_sv_eq_ci_lit(a[i], "DIRECTORY") || saw_test_dir_clause || i + 1 >= arena_arr_len(a)) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("get_property(TEST ...) received invalid DIRECTORY clause"),
                                  nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            inherit_dir = eval_path_resolve_for_cmake_arg(ctx, a[++i], cur_src, true);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            saw_test_dir_clause = true;
            i++;
        }
        if (saw_test_dir_clause) object_id = eval_property_scoped_object_id_temp(ctx, "DIRECTORY", inherit_dir, object_token);
        else {
            object_id = object_token;
            inherit_dir = cur_src;
        }
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    } else if (eval_sv_eq_ci_lit(scope_upper, "TARGET") ||
               eval_sv_eq_ci_lit(scope_upper, "INSTALL") ||
               eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
        object_id = object_token;
    }

    if (i >= arena_arr_len(a) || !eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("get_property() missing PROPERTY keyword"),
                          nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    i++;
    if (i >= arena_arr_len(a)) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("get_property() missing property name"),
                          nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    String_View prop_name = a[i++];

    Eval_Property_Query_Mode mode = EVAL_PROP_QUERY_VALUE;
    if (i < arena_arr_len(a)) {
        if (!eval_property_query_mode_parse(ctx, node, o, a[i++], &mode)) {
            return eval_result_from_ctx(ctx);
        }
    }
    if (i != arena_arr_len(a)) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("get_property() received unexpected trailing arguments"),
                          a[i]);
        return eval_result_from_ctx(ctx);
    }

    if (!eval_property_query(ctx,
                             node,
                             o,
                             out_var,
                             scope_upper,
                             object_id,
                             object_token,
                             prop_name,
                             mode,
                             inherit_dir,
                             true,
                             false)) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_cmake_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) != 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_cmake_property() expects output variable and property name"), nob_sv_from_cstr("Usage: get_cmake_property(<var> <property>)"));
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_bool(eval_property_query_cmake(ctx, node, o, a[0], a[1]));
}

Eval_Result eval_handle_get_directory_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_directory_property() requires output variable and query"), nob_sv_from_cstr("Usage: get_directory_property(<var> [DIRECTORY <dir>] <prop-name>|DEFINITION <var-name>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View out_var = a[0];
    size_t i = 1;
    String_View dir = eval_current_source_dir_for_paths(ctx);
    if (eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
        if (i + 1 >= arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_directory_property(DIRECTORY ...) requires a directory"), nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }
        dir = eval_path_resolve_for_cmake_arg(ctx, a[i + 1], dir, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        i += 2;
    }
    if (i >= arena_arr_len(a)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_directory_property() missing property query"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[i], "DEFINITION")) {
        if (i + 1 >= arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_directory_property(DEFINITION ...) requires a variable name"), nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }
        if (i + 2 != arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_directory_property(DEFINITION ...) expects exactly one variable name"), nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }
        String_View value = eval_var_get_visible(ctx, a[i + 1]);
        return eval_result_from_bool(eval_var_set_current(ctx, out_var, value));
    }

    if (i + 1 != arena_arr_len(a)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("get_directory_property() received unexpected trailing arguments"), a[i + 1]);
        return eval_result_from_ctx(ctx);
    }

    if (!eval_property_query(ctx,
                             node,
                             o,
                             out_var,
                             nob_sv_from_cstr("DIRECTORY"),
                             dir,
                             dir,
                             a[i],
                             EVAL_PROP_QUERY_VALUE,
                             dir,
                             false,
                             false)) {
        return eval_result_from_ctx(ctx);
    }
    if (!eval_var_defined_visible(ctx, out_var)) {
        return eval_result_from_bool(eval_var_set_current(ctx, out_var, nob_sv_from_cstr("")));
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_source_file_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) != 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_source_file_property() expects output variable, source and property"), nob_sv_from_cstr("Usage: get_source_file_property(<var> <source> <property>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View inherit_dir = eval_current_source_dir_for_paths(ctx);
    if (!eval_property_query(ctx,
                             node,
                             o,
                             a[0],
                             nob_sv_from_cstr("SOURCE"),
                             a[1],
                             a[1],
                             a[2],
                             EVAL_PROP_QUERY_VALUE,
                             inherit_dir,
                             false,
                             true)) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_target_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) != 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_target_property() expects output variable, target and property"), nob_sv_from_cstr("Usage: get_target_property(<var> <target> <property>)"));
        return eval_result_from_ctx(ctx);
    }
    if (!eval_target_known(ctx, a[1])) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("get_target_property() target was not declared"), a[1]);
        return eval_result_from_ctx(ctx);
    }

    if (!eval_property_query(ctx,
                             node,
                             o,
                             a[0],
                             nob_sv_from_cstr("TARGET"),
                             a[1],
                             a[1],
                             a[2],
                             EVAL_PROP_QUERY_VALUE,
                             nob_sv_from_cstr(""),
                             false,
                             true)) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_test_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) != 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("get_test_property() expects output variable, test and property"), nob_sv_from_cstr("Usage: get_test_property(<var> <test> <property>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View test_dir = eval_current_source_dir_for_paths(ctx);
    if (!eval_test_exists_in_directory_scope(ctx, a[1], test_dir)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("get_test_property() test was not declared in current directory scope"), a[1]);
        return eval_result_from_ctx(ctx);
    }

    if (!eval_property_query(ctx,
                             node,
                             o,
                             a[0],
                             nob_sv_from_cstr("TEST"),
                             a[1],
                             a[1],
                             a[2],
                             EVAL_PROP_QUERY_VALUE,
                             test_dir,
                             false,
                             true)) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_set_directory_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3 || !eval_sv_eq_ci_lit(a[0], "PROPERTIES")) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_directory_properties() requires PROPERTIES key/value pairs"), nob_sv_from_cstr("Usage: set_directory_properties(PROPERTIES <k> <v> [<k> <v>...])"));
        return eval_result_from_ctx(ctx);
    }
    if (((arena_arr_len(a) - 1) % 2) != 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("set_directory_properties() has dangling property key without value"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    String_View current_dir = eval_current_source_dir_for_paths(ctx);
    String_View scope_upper = nob_sv_from_cstr("DIRECTORY");
    for (size_t i = 1; i + 1 < arena_arr_len(a); i += 2) {
        if (!eval_property_write(ctx,
                                 o,
                                 scope_upper,
                                 current_dir,
                                 a[i],
                                 a[i + 1],
                                 EV_PROP_SET,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_set_source_files_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 4) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_source_files_properties() requires files and PROPERTIES pairs"), nob_sv_from_cstr("Usage: set_source_files_properties(<file>... [DIRECTORY <dir>...] [TARGET_DIRECTORY <target>...] PROPERTIES <k> <v> [<k> <v>...])"));
        return eval_result_from_ctx(ctx);
    }

    SV_List files = NULL;
    SV_List dirs = NULL;
    SV_List target_dirs = NULL;
    enum {
        SF_PARSE_FILES = 0,
        SF_PARSE_DIRS,
        SF_PARSE_TARGET_DIRS,
    } mode = SF_PARSE_FILES;

    size_t i = 0;
    for (; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PROPERTIES")) break;
        if (eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
            mode = SF_PARSE_DIRS;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "TARGET_DIRECTORY")) {
            mode = SF_PARSE_TARGET_DIRS;
            continue;
        }

        if (mode == SF_PARSE_FILES) {
            if (!svu_list_push_temp(ctx, &files, a[i])) return eval_result_from_ctx(ctx);
        } else if (mode == SF_PARSE_DIRS) {
            if (!svu_list_push_temp(ctx, &dirs, a[i])) return eval_result_from_ctx(ctx);
        } else {
            if (!svu_list_push_temp(ctx, &target_dirs, a[i])) return eval_result_from_ctx(ctx);
        }
    }

    if (arena_arr_len(files) == 0 || i >= arena_arr_len(a)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_source_files_properties() requires at least one source file and PROPERTIES"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    i++;
    if (i >= arena_arr_len(a) || ((arena_arr_len(a) - i) % 2) != 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "dispatcher", nob_sv_from_cstr("set_source_files_properties() has invalid PROPERTIES key/value pairs"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    for (size_t di = 0; di < arena_arr_len(dirs); di++) {
        dirs[di] = eval_path_resolve_for_cmake_arg(ctx, dirs[di], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    }
    for (size_t ti = 0; ti < arena_arr_len(target_dirs); ti++) {
        if (!eval_target_known(ctx, target_dirs[ti])) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("set_source_files_properties(TARGET_DIRECTORY ...) target was not declared"), target_dirs[ti]);
            return eval_result_from_ctx(ctx);
        }
    }

    String_View scope_upper = nob_sv_from_cstr("SOURCE");
    for (size_t fi = 0; fi < arena_arr_len(files); fi++) {
        if (arena_arr_len(dirs) == 0 && arena_arr_len(target_dirs) == 0) {
            for (size_t pi = i; pi + 1 < arena_arr_len(a); pi += 2) {
                if (!eval_property_write(ctx,
                                         o,
                                         scope_upper,
                                         files[fi],
                                         a[pi],
                                         a[pi + 1],
                                         EV_PROP_SET,
                                         true)) {
                    return eval_result_from_ctx(ctx);
                }
            }
            continue;
        }

        for (size_t di = 0; di < arena_arr_len(dirs); di++) {
            String_View object_id = eval_property_scoped_object_id_temp(ctx, "DIRECTORY", dirs[di], files[fi]);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            for (size_t pi = i; pi + 1 < arena_arr_len(a); pi += 2) {
                if (!eval_property_write(ctx,
                                         o,
                                         scope_upper,
                                         object_id,
                                         a[pi],
                                         a[pi + 1],
                                         EV_PROP_SET,
                                         true)) {
                    return eval_result_from_ctx(ctx);
                }
            }
        }

        for (size_t ti = 0; ti < arena_arr_len(target_dirs); ti++) {
            String_View object_id = eval_property_scoped_object_id_temp(ctx,
                                                                        "TARGET_DIRECTORY",
                                                                        target_dirs[ti],
                                                                        files[fi]);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            for (size_t pi = i; pi + 1 < arena_arr_len(a); pi += 2) {
                if (!eval_property_write(ctx,
                                         o,
                                         scope_upper,
                                         object_id,
                                         a[pi],
                                         a[pi + 1],
                                         EV_PROP_SET,
                                         true)) {
                    return eval_result_from_ctx(ctx);
                }
            }
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_set_tests_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 4) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_tests_properties() requires tests and PROPERTIES pairs"), nob_sv_from_cstr("Usage: set_tests_properties(<test>... [DIRECTORY <dir>] PROPERTIES <k> <v> [<k> <v>...])"));
        return eval_result_from_ctx(ctx);
    }

    SV_List tests = NULL;
    size_t i = 0;
    while (i < arena_arr_len(a) && !eval_sv_eq_ci_lit(a[i], "DIRECTORY") && !eval_sv_eq_ci_lit(a[i], "PROPERTIES")) {
        if (!svu_list_push_temp(ctx, &tests, a[i])) return eval_result_from_ctx(ctx);
        i++;
    }

    if (arena_arr_len(tests) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_tests_properties() requires at least one test name"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    String_View test_dir = eval_current_source_dir_for_paths(ctx);
    bool saw_directory_clause = false;
    if (i < arena_arr_len(a) && eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
        if (i + 1 >= arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_tests_properties(DIRECTORY ...) requires a directory"), nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }
        test_dir = eval_path_resolve_for_cmake_arg(ctx, a[i + 1], test_dir, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        saw_directory_clause = true;
        i += 2;
    }

    if (i >= arena_arr_len(a) || !eval_sv_eq_ci_lit(a[i], "PROPERTIES")) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_tests_properties() missing PROPERTIES keyword"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    i++;
    if (i >= arena_arr_len(a) || ((arena_arr_len(a) - i) % 2) != 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "dispatcher", nob_sv_from_cstr("set_tests_properties() has invalid PROPERTIES key/value pairs"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    for (size_t ti = 0; ti < arena_arr_len(tests); ti++) {
        if (eval_test_exists_in_directory_scope(ctx, tests[ti], test_dir)) continue;
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("set_tests_properties() test was not declared in selected directory scope"), tests[ti]);
        return eval_result_from_ctx(ctx);
    }

    String_View scope_upper = nob_sv_from_cstr("TEST");
    for (size_t ti = 0; ti < arena_arr_len(tests); ti++) {
        String_View object_id = tests[ti];
        if (saw_directory_clause) {
            object_id = eval_property_scoped_object_id_temp(ctx, "DIRECTORY", test_dir, tests[ti]);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        }

        for (size_t pi = i; pi + 1 < arena_arr_len(a); pi += 2) {
            if (!eval_property_write(ctx,
                                     o,
                                     scope_upper,
                                     object_id,
                                     a[pi],
                                     a[pi + 1],
                                     EV_PROP_SET,
                                     true)) {
                return eval_result_from_ctx(ctx);
            }
        }
    }

    return eval_result_from_ctx(ctx);
}


Eval_Result eval_handle_define_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("define_property() requires scope and PROPERTY name"), nob_sv_from_cstr("Usage: define_property(<GLOBAL|DIRECTORY|TARGET|SOURCE|TEST|VARIABLE|CACHED_VARIABLE> PROPERTY <name> [INHERITED] [BRIEF_DOCS <docs>...] [FULL_DOCS <docs>...] [INITIALIZE_FROM_VARIABLE <var>])"));
        return eval_result_from_ctx(ctx);
    }

    String_View scope_upper = nob_sv_from_cstr("");
    if (!eval_property_scope_upper_temp(ctx, a[0], &scope_upper)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("define_property() unknown scope"), a[0]);
        return eval_result_from_ctx(ctx);
    }

    if (!eval_sv_eq_ci_lit(a[1], "PROPERTY")) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("define_property() missing PROPERTY keyword"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    Eval_Property_Definition def = {0};
    def.scope_upper = scope_upper;
    def.property_upper = eval_property_upper_name_temp(ctx, a[2]);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (def.property_upper.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("define_property() missing property name"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    size_t i = 3;
    while (i < arena_arr_len(a)) {
        if (eval_sv_eq_ci_lit(a[i], "INHERITED")) {
            def.inherited = true;
            i++;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "BRIEF_DOCS")) {
            size_t start = ++i;
            while (i < arena_arr_len(a) && !define_property_keyword(a[i])) i++;
            def.has_brief_docs = true;
            if (i > start) {
                def.brief_docs = eval_sv_join_semi_temp(ctx, &a[start], i - start);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "FULL_DOCS")) {
            size_t start = ++i;
            while (i < arena_arr_len(a) && !define_property_keyword(a[i])) i++;
            def.has_full_docs = true;
            if (i > start) {
                def.full_docs = eval_sv_join_semi_temp(ctx, &a[start], i - start);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INITIALIZE_FROM_VARIABLE")) {
            if (!eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_CONTEXT, "dispatcher", nob_sv_from_cstr("define_property(INITIALIZE_FROM_VARIABLE) is only valid for TARGET scope"), scope_upper);
                return eval_result_from_ctx(ctx);
            }
            if (def.has_initialize_from_variable) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_DUPLICATE_ARGUMENT, "dispatcher", nob_sv_from_cstr("define_property() received duplicate INITIALIZE_FROM_VARIABLE"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            if (i + 1 >= arena_arr_len(a) || define_property_keyword(a[i + 1])) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("define_property(INITIALIZE_FROM_VARIABLE) requires a variable name"), nob_sv_from_cstr(""));
                return eval_result_from_ctx(ctx);
            }
            def.has_initialize_from_variable = true;
            def.initialize_from_variable = a[i + 1];
            i += 2;
            continue;
        }

        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("define_property() received unexpected argument"), a[i]);
        return eval_result_from_ctx(ctx);
    }

    if (!eval_property_define(ctx, &def)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_set_target_properties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 4) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_target_properties() requires targets and PROPERTIES key/value pairs"), nob_sv_from_cstr("Usage: set_target_properties(<t1> [<t2> ...] PROPERTIES <k1> <v1> ...)"));
        return eval_result_from_ctx(ctx);
    }

    size_t props_i = arena_arr_len(a);
    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PROPERTIES")) {
            props_i = i;
            break;
        }
    }

    if (props_i == 0 || props_i >= arena_arr_len(a) - 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_target_properties() missing PROPERTIES section"), nob_sv_from_cstr("Expected: set_target_properties(<targets...> PROPERTIES <key> <value> ...)"));
        return eval_result_from_ctx(ctx);
    }

    size_t kv_start = props_i + 1;
    size_t kv_count = arena_arr_len(a) - kv_start;
    if (kv_count < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_target_properties() missing property key/value"), nob_sv_from_cstr("Provide at least one <key> <value> pair"));
        return eval_result_from_ctx(ctx);
    }

    if ((kv_count % 2) != 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("set_target_properties() has dangling property key without value"), nob_sv_from_cstr("Ignoring the last unmatched key"));
    }

    for (size_t ti = 0; ti < props_i; ti++) {
        String_View tgt = a[ti];
        if (!eval_target_known(ctx, tgt)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("set_target_properties() target was not declared"), tgt);
            continue;
        }
        if (eval_target_alias_known(ctx, tgt)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("set_target_properties() cannot be used on ALIAS targets"), tgt);
            continue;
        }
        for (size_t i = kv_start; i + 1 < arena_arr_len(a); i += 2) {
            if (!eval_property_write(ctx,
                                     o,
                                     nob_sv_from_cstr("TARGET"),
                                     tgt,
                                     a[i],
                                     a[i + 1],
                                     EV_PROP_SET,
                                     false)) {
                return eval_result_from_ctx(ctx);
            }
            if (!eval_emit_target_prop_set(ctx, o, tgt, a[i], a[i + 1], EV_PROP_SET)) {
                return eval_result_from_ctx(ctx);
            }
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_set_property(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_property() missing scope"), nob_sv_from_cstr("Usage: set_property(<GLOBAL|DIRECTORY|TARGET|SOURCE|INSTALL|TEST|CACHE> ... PROPERTY <k> [v...])"));
        return eval_result_from_ctx(ctx);
    }

    String_View scope = a[0];
    bool is_target_scope = eval_sv_eq_ci_lit(scope, "TARGET");
    bool is_global_scope = eval_sv_eq_ci_lit(scope, "GLOBAL");
    bool is_dir_scope = eval_sv_eq_ci_lit(scope, "DIRECTORY");
    bool is_source_scope = eval_sv_eq_ci_lit(scope, "SOURCE");
    bool is_install_scope = eval_sv_eq_ci_lit(scope, "INSTALL");
    bool is_test_scope = eval_sv_eq_ci_lit(scope, "TEST");
    bool is_cache_scope = eval_sv_eq_ci_lit(scope, "CACHE");
    if (!(is_target_scope || is_global_scope || is_dir_scope || is_source_scope || is_install_scope || is_test_scope || is_cache_scope)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("set_property() unknown scope"), scope);
        return eval_result_from_ctx(ctx);
    }

    bool append = false;
    bool append_string = false;
    SV_List objects = NULL;
    SV_List source_dirs = NULL;
    SV_List source_target_dirs = NULL;
    SV_List test_dirs = NULL;
    bool saw_source_directory_clause = false;
    bool saw_source_target_directory_clause = false;
    bool saw_test_directory_clause = false;

    enum {
        SP_PARSE_OBJECTS = 0,
        SP_PARSE_SOURCE_DIRS,
        SP_PARSE_SOURCE_TARGET_DIRS,
        SP_PARSE_TEST_DIRS,
    } parse_mode = SP_PARSE_OBJECTS;

    size_t i = 1;
    for (; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PROPERTY")) break;
        if (eval_sv_eq_ci_lit(a[i], "APPEND")) {
            append = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "APPEND_STRING")) {
            append_string = true;
            continue;
        }
        if (is_source_scope && eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
            saw_source_directory_clause = true;
            parse_mode = SP_PARSE_SOURCE_DIRS;
            continue;
        }
        if (is_source_scope && eval_sv_eq_ci_lit(a[i], "TARGET_DIRECTORY")) {
            saw_source_target_directory_clause = true;
            parse_mode = SP_PARSE_SOURCE_TARGET_DIRS;
            continue;
        }
        if (is_test_scope && eval_sv_eq_ci_lit(a[i], "DIRECTORY")) {
            saw_test_directory_clause = true;
            parse_mode = SP_PARSE_TEST_DIRS;
            continue;
        }

        if (parse_mode == SP_PARSE_SOURCE_DIRS) {
            if (!svu_list_push_temp(ctx, &source_dirs, a[i])) return eval_result_from_ctx(ctx);
            continue;
        }
        if (parse_mode == SP_PARSE_SOURCE_TARGET_DIRS) {
            if (!svu_list_push_temp(ctx, &source_target_dirs, a[i])) return eval_result_from_ctx(ctx);
            continue;
        }
        if (parse_mode == SP_PARSE_TEST_DIRS) {
            if (!svu_list_push_temp(ctx, &test_dirs, a[i])) return eval_result_from_ctx(ctx);
            continue;
        }
        if (!svu_list_push_temp(ctx, &objects, a[i])) return eval_result_from_ctx(ctx);
    }

    if (saw_source_directory_clause && arena_arr_len(source_dirs) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_property(SOURCE DIRECTORY ...) requires at least one directory"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    if (saw_source_target_directory_clause && arena_arr_len(source_target_dirs) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_property(SOURCE TARGET_DIRECTORY ...) requires at least one target"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    if (saw_test_directory_clause && arena_arr_len(test_dirs) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_property(TEST DIRECTORY ...) requires at least one directory"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    if (saw_test_directory_clause && arena_arr_len(test_dirs) > 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_property(TEST DIRECTORY ...) expects exactly one directory"), nob_sv_from_cstr("Use: set_property(TEST [<test>...] [DIRECTORY <dir>] PROPERTY <key> [value...])"));
        return eval_result_from_ctx(ctx);
    }

    if (is_global_scope && arena_arr_len(objects) > 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("set_property(GLOBAL ...) does not take object names"), nob_sv_from_cstr("Use: set_property(GLOBAL PROPERTY <key> [value...])"));
        return eval_result_from_ctx(ctx);
    }

    {
        String_View cur_src = eval_current_source_dir_for_paths(ctx);
        for (size_t di = 0; di < arena_arr_len(source_dirs); di++) {
            source_dirs[di] = eval_path_resolve_for_cmake_arg(ctx, source_dirs[di], cur_src, true);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        }
        for (size_t di = 0; di < arena_arr_len(test_dirs); di++) {
            test_dirs[di] = eval_path_resolve_for_cmake_arg(ctx, test_dirs[di], cur_src, true);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        }
    }

    if (i >= arena_arr_len(a) || !eval_sv_eq_ci_lit(a[i], "PROPERTY")) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_property() missing PROPERTY keyword"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    i++;
    if (i >= arena_arr_len(a)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("set_property() missing property key"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    String_View key = a[i++];
    String_View value = nob_sv_from_cstr("");
    if (i < arena_arr_len(a)) {
        if (append_string) value = svu_join_no_sep_temp(ctx, &a[i], arena_arr_len(a) - i);
        else value = eval_sv_join_semi_temp(ctx, &a[i], arena_arr_len(a) - i);
    }

    Cmake_Target_Property_Op op = EV_PROP_SET;
    if (append_string) op = EV_PROP_APPEND_STRING;
    else if (append) op = EV_PROP_APPEND_LIST;

    if (append && append_string) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_CONFLICTING_OPTIONS, "dispatcher", nob_sv_from_cstr("set_property() received both APPEND and APPEND_STRING"), nob_sv_from_cstr("Use only one of APPEND or APPEND_STRING"));
        return eval_result_from_ctx(ctx);
    }

    if (is_target_scope) {
        for (size_t ti = 0; ti < arena_arr_len(objects); ti++) {
            if (!eval_target_known(ctx, objects[ti])) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("set_property(TARGET ...) target was not declared"), objects[ti]);
                continue;
            }
            if (eval_target_alias_known(ctx, objects[ti])) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("set_property(TARGET ...) cannot be used on ALIAS targets"), objects[ti]);
                continue;
            }
            if (!eval_emit_target_prop_set(ctx, o, objects[ti], key, value, op)) {
                return eval_result_from_ctx(ctx);
            }
        }
        return eval_result_from_ctx(ctx);
    }

    String_View scope_upper = eval_property_upper_name_temp(ctx, scope);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (is_global_scope) {
        if (!eval_property_write(ctx,
                                 o,
                                 scope_upper,
                                 nob_sv_from_cstr(""),
                                 key,
                                 value,
                                 op,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    if (is_dir_scope && arena_arr_len(objects) == 0) {
        String_View current_dir = eval_current_source_dir(ctx);
        if (!eval_property_write(ctx,
                                 o,
                                 scope_upper,
                                 current_dir,
                                 key,
                                 value,
                                 op,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    if (is_source_scope) {
        for (size_t ti = 0; ti < arena_arr_len(source_target_dirs); ti++) {
            if (eval_target_known(ctx, source_target_dirs[ti])) continue;
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("set_property(SOURCE TARGET_DIRECTORY ...) target was not declared"), source_target_dirs[ti]);
            return eval_result_from_ctx(ctx);
        }

        if (saw_source_directory_clause || saw_source_target_directory_clause) {
            for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
                if (!saw_source_directory_clause && !saw_source_target_directory_clause) {
                    if (!eval_property_write(ctx,
                                             o,
                                             scope_upper,
                                             objects[oi],
                                             key,
                                             value,
                                             op,
                                             true)) {
                        return eval_result_from_ctx(ctx);
                    }
                    continue;
                }

                for (size_t di = 0; di < arena_arr_len(source_dirs); di++) {
                    String_View object_id = eval_property_scoped_object_id_temp(ctx,
                                                                                "DIRECTORY",
                                                                                source_dirs[di],
                                                                                objects[oi]);
                    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
                    if (!eval_property_write(ctx,
                                             o,
                                             scope_upper,
                                             object_id,
                                             key,
                                             value,
                                             op,
                                             true)) {
                        return eval_result_from_ctx(ctx);
                    }
                }
                for (size_t ti = 0; ti < arena_arr_len(source_target_dirs); ti++) {
                    String_View object_id = eval_property_scoped_object_id_temp(ctx,
                                                                                "TARGET_DIRECTORY",
                                                                                source_target_dirs[ti],
                                                                                objects[oi]);
                    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
                    if (!eval_property_write(ctx,
                                             o,
                                             scope_upper,
                                             object_id,
                                             key,
                                             value,
                                             op,
                                             true)) {
                        return eval_result_from_ctx(ctx);
                    }
                }
            }
            return eval_result_from_ctx(ctx);
        }
    }

    if (is_test_scope) {
        String_View test_scope_dir = eval_current_source_dir_for_paths(ctx);
        if (saw_test_directory_clause) test_scope_dir = test_dirs[0];

        for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
            if (eval_test_exists_in_directory_scope(ctx, objects[oi], test_scope_dir)) continue;
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("set_property(TEST ...) test was not declared in selected directory scope"), objects[oi]);
            return eval_result_from_ctx(ctx);
        }

        if (saw_test_directory_clause) {
            for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
                String_View object_id = eval_property_scoped_object_id_temp(ctx,
                                                                            "DIRECTORY",
                                                                            test_scope_dir,
                                                                            objects[oi]);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
                if (!eval_property_write(ctx,
                                         o,
                                         scope_upper,
                                         object_id,
                                         key,
                                         value,
                                         op,
                                         true)) {
                    return eval_result_from_ctx(ctx);
                }
            }
            return eval_result_from_ctx(ctx);
        }
    }

    if (is_cache_scope) {
        for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
            if (eval_cache_defined(ctx, objects[oi])) continue;
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("set_property(CACHE ...) cache entry does not exist"), objects[oi]);
            return eval_result_from_ctx(ctx);
        }
    }

    for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
        if (!eval_property_write(ctx,
                                 o,
                                 scope_upper,
                                 objects[oi],
                                 key,
                                 value,
                                 op,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
    }

    return eval_result_from_ctx(ctx);
}
