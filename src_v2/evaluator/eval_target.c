#include "eval_target_internal.h"

static bool define_property_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "PROPERTY") ||
           eval_sv_eq_ci_lit(tok, "INHERITED") ||
           eval_sv_eq_ci_lit(tok, "BRIEF_DOCS") ||
           eval_sv_eq_ci_lit(tok, "FULL_DOCS") ||
           eval_sv_eq_ci_lit(tok, "INITIALIZE_FROM_VARIABLE");
}

static bool eval_property_write_current_directory_shadow(EvalExecContext *ctx,
                                                         Event_Origin origin,
                                                         String_View scope_upper,
                                                         String_View object_name,
                                                         String_View property_name,
                                                         String_View value,
                                                         Cmake_Target_Property_Op op,
                                                         bool emit_var_event) {
    if (!ctx) return false;
    String_View current_dir = eval_current_source_dir_for_paths(ctx);
    String_View scoped_object =
        eval_property_scoped_object_id_temp(ctx, "DIRECTORY", current_dir, object_name);
    if (eval_should_stop(ctx)) return false;
    return eval_property_write(
        ctx, origin, scope_upper, scoped_object, property_name, value, op, emit_var_event);
}

Eval_Result eval_handle_set_directory_properties(EvalExecContext *ctx, const Node *node) {
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

Eval_Result eval_handle_set_source_files_properties(EvalExecContext *ctx, const Node *node) {
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
        if (!eval_target_visible(ctx, target_dirs[ti])) {
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
            String_View declared_dir = nob_sv_from_cstr("");
            if (!eval_target_declared_dir(ctx, target_dirs[ti], &declared_dir)) {
                return eval_result_from_ctx(ctx);
            }
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

            String_View object_id =
                eval_property_scoped_object_id_temp(ctx, "DIRECTORY", declared_dir, files[fi]);
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

Eval_Result eval_handle_set_tests_properties(EvalExecContext *ctx, const Node *node) {
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
            if (!saw_directory_clause &&
                !eval_property_write_current_directory_shadow(ctx,
                                                              o,
                                                              scope_upper,
                                                              tests[ti],
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


Eval_Result eval_handle_define_property(EvalExecContext *ctx, const Node *node) {
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

Eval_Result eval_handle_set_target_properties(EvalExecContext *ctx, const Node *node) {
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

Eval_Result eval_handle_set_property(EvalExecContext *ctx, const Node *node) {
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
        if (is_dir_scope) {
            for (size_t oi = 0; oi < arena_arr_len(objects); oi++) {
                String_View resolved_dir =
                    eval_path_resolve_for_cmake_arg(ctx, objects[oi], cur_src, true);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

                String_View known_source_dir = eval_directory_known_source_dir_temp(ctx, resolved_dir);
                if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
                if (known_source_dir.count == 0) {
                    property_diag_unknown_directory(ctx,
                                                    node,
                                                    nob_sv_from_cstr("set_property(DIRECTORY ...) directory is not known"),
                                                    resolved_dir);
                    return eval_result_from_ctx(ctx);
                }

                objects[oi] = known_source_dir;
            }
        }
        for (size_t di = 0; di < arena_arr_len(source_dirs); di++) {
            String_View resolved_dir =
                eval_path_resolve_for_cmake_arg(ctx, source_dirs[di], cur_src, true);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

            source_dirs[di] = eval_directory_known_source_dir_temp(ctx, resolved_dir);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            if (source_dirs[di].count == 0) {
                property_diag_unknown_directory(ctx,
                                                node,
                                                nob_sv_from_cstr("set_property(SOURCE DIRECTORY ...) directory is not known"),
                                                resolved_dir);
                return eval_result_from_ctx(ctx);
            }
        }
        for (size_t di = 0; di < arena_arr_len(test_dirs); di++) {
            String_View resolved_dir =
                eval_path_resolve_for_cmake_arg(ctx, test_dirs[di], cur_src, true);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

            test_dirs[di] = eval_directory_known_source_dir_temp(ctx, resolved_dir);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            if (test_dirs[di].count == 0) {
                property_diag_unknown_directory(ctx,
                                                node,
                                                nob_sv_from_cstr("set_property(TEST DIRECTORY ...) directory is not known"),
                                                resolved_dir);
                return eval_result_from_ctx(ctx);
            }
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
            if (eval_target_visible(ctx, source_target_dirs[ti])) continue;
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
                    String_View declared_dir = nob_sv_from_cstr("");
                    if (!eval_target_declared_dir(ctx, source_target_dirs[ti], &declared_dir)) {
                        return eval_result_from_ctx(ctx);
                    }
                    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

                    String_View object_id = eval_property_scoped_object_id_temp(
                        ctx, "DIRECTORY", declared_dir, objects[oi]);
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
            if (!eval_property_write_current_directory_shadow(ctx,
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
