#include "eval_target_internal.h"

static bool target_get_declared_dir_temp(Evaluator_Context *ctx,
                                         String_View target_name,
                                         String_View *out_dir) {
    if (!ctx || !out_dir) return false;
    return eval_target_declared_dir(ctx, target_name, out_dir);
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
