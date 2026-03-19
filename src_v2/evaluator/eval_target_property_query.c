#include "eval_target_internal.h"

typedef enum {
    GET_PROPERTY_SCOPE_GLOBAL = 0,
    GET_PROPERTY_SCOPE_DIRECTORY,
    GET_PROPERTY_SCOPE_TARGET,
    GET_PROPERTY_SCOPE_SOURCE,
    GET_PROPERTY_SCOPE_INSTALL,
    GET_PROPERTY_SCOPE_TEST,
    GET_PROPERTY_SCOPE_CACHE,
    GET_PROPERTY_SCOPE_VARIABLE,
} Get_Property_Scope;

typedef struct {
    String_View out_var;
    String_View scope_upper;
    String_View object_id;
    String_View validation_object;
    String_View property_name;
    String_View inherit_dir;
    Eval_Property_Query_Mode mode;
    bool validate_object;
    bool missing_as_notfound;
    bool unset_as_empty;
} Property_Query_Request;

typedef struct {
    String_View out_var;
    Get_Property_Scope scope;
    String_View scope_upper;
    String_View object_token;
    String_View object_id;
    String_View property_name;
    String_View inherit_dir;
    Eval_Property_Query_Mode mode;
} Get_Property_Request;

typedef struct {
    String_View out_var;
    String_View property_name;
} Get_Cmake_Property_Request;

typedef enum {
    GET_DIRECTORY_PROPERTY_QUERY_VALUE = 0,
    GET_DIRECTORY_PROPERTY_QUERY_DEFINITION,
} Get_Directory_Property_Query_Kind;

typedef struct {
    String_View out_var;
    String_View directory;
    Get_Directory_Property_Query_Kind kind;
    String_View query_name;
} Get_Directory_Property_Request;

typedef struct {
    String_View out_var;
    String_View object_name;
    String_View property_name;
} Property_Triplet_Request;

static bool target_get_declared_dir_temp(EvalExecContext *ctx,
                                         String_View target_name,
                                         String_View *out_dir) {
    if (!ctx || !out_dir) return false;
    return eval_target_declared_dir(ctx, target_name, out_dir);
}

static bool property_query_request_execute(EvalExecContext *ctx,
                                           const Node *node,
                                           Cmake_Event_Origin origin,
                                           const Property_Query_Request *req) {
    if (!ctx || !node || !req) return false;

    if (!eval_property_query(ctx,
                             node,
                             origin,
                             req->out_var,
                             req->scope_upper,
                             req->object_id,
                             req->validation_object,
                             req->property_name,
                             req->mode,
                             req->inherit_dir,
                             req->validate_object,
                             req->missing_as_notfound)) {
        return false;
    }
    if (req->unset_as_empty && !eval_var_defined_visible(ctx, req->out_var)) {
        return eval_var_set_current(ctx, req->out_var, nob_sv_from_cstr(""));
    }
    return true;
}

static bool get_property_scope_from_upper(String_View scope_upper,
                                          Get_Property_Scope *out_scope) {
    if (!out_scope) return false;
    if (eval_sv_eq_ci_lit(scope_upper, "GLOBAL")) *out_scope = GET_PROPERTY_SCOPE_GLOBAL;
    else if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) *out_scope = GET_PROPERTY_SCOPE_DIRECTORY;
    else if (eval_sv_eq_ci_lit(scope_upper, "TARGET")) *out_scope = GET_PROPERTY_SCOPE_TARGET;
    else if (eval_sv_eq_ci_lit(scope_upper, "SOURCE")) *out_scope = GET_PROPERTY_SCOPE_SOURCE;
    else if (eval_sv_eq_ci_lit(scope_upper, "INSTALL")) *out_scope = GET_PROPERTY_SCOPE_INSTALL;
    else if (eval_sv_eq_ci_lit(scope_upper, "TEST")) *out_scope = GET_PROPERTY_SCOPE_TEST;
    else if (eval_sv_eq_ci_lit(scope_upper, "CACHE")) *out_scope = GET_PROPERTY_SCOPE_CACHE;
    else if (eval_sv_eq_ci_lit(scope_upper, "VARIABLE")) *out_scope = GET_PROPERTY_SCOPE_VARIABLE;
    else return false;
    return true;
}

static bool get_property_parse_scope(EvalExecContext *ctx,
                                     const Node *node,
                                     Cmake_Event_Origin origin,
                                     String_View token,
                                     Get_Property_Scope *out_scope,
                                     String_View *out_scope_upper) {
    String_View scope_upper = nob_sv_from_cstr("");
    if (!eval_property_scope_upper_temp(ctx, token, &scope_upper)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                       "dispatcher",
                                       nob_sv_from_cstr("get_property() unknown scope"),
                                       token);
        return false;
    }
    if (!get_property_scope_from_upper(scope_upper, out_scope)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                       "dispatcher",
                                       nob_sv_from_cstr("get_property() unknown scope"),
                                       token);
        return false;
    }
    *out_scope_upper = scope_upper;
    return true;
}

static bool get_property_parse_property_clause(EvalExecContext *ctx,
                                               const Node *node,
                                               Cmake_Event_Origin origin,
                                               SV_List args,
                                               size_t *io_index,
                                               Get_Property_Request *out_req) {
    size_t i = *io_index;
    if (i >= arena_arr_len(args) || !eval_sv_eq_ci_lit(args[i], "PROPERTY")) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("get_property() missing PROPERTY keyword"),
                          nob_sv_from_cstr(""));
        return false;
    }
    i++;
    if (i >= arena_arr_len(args)) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("get_property() missing property name"),
                          nob_sv_from_cstr(""));
        return false;
    }

    out_req->property_name = args[i++];
    out_req->mode = EVAL_PROP_QUERY_VALUE;
    if (i < arena_arr_len(args)) {
        if (!eval_property_query_mode_parse(ctx, node, origin, args[i++], &out_req->mode)) {
            return false;
        }
    }
    if (i != arena_arr_len(args)) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("get_property() received unexpected trailing arguments"),
                          args[i]);
        return false;
    }

    *io_index = i;
    return true;
}

static bool get_property_parse_directory_scope(EvalExecContext *ctx,
                                               const Node *node,
                                               Get_Property_Request *out_req) {
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    if (out_req->object_token.count == 0) out_req->object_id = cur_src;
    else out_req->object_id =
             eval_path_resolve_for_cmake_arg(ctx, out_req->object_token, cur_src, true);
    if (eval_should_stop(ctx)) return false;

    if (out_req->object_token.count > 0 && !eval_directory_is_known(ctx, out_req->object_id)) {
        property_diag_unknown_directory(ctx,
                                        node,
                                        nob_sv_from_cstr("get_property(DIRECTORY ...) directory is not known"),
                                        out_req->object_id);
        return false;
    }

    out_req->inherit_dir = out_req->object_id;
    return true;
}

static bool get_property_parse_source_scope(EvalExecContext *ctx,
                                            const Node *node,
                                            SV_List args,
                                            size_t *io_index,
                                            Get_Property_Request *out_req) {
    size_t i = *io_index;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    bool saw_source_dir_clause = false;
    bool saw_source_target_dir_clause = false;

    while (i < arena_arr_len(args) && !eval_sv_eq_ci_lit(args[i], "PROPERTY")) {
        if (eval_sv_eq_ci_lit(args[i], "DIRECTORY")) {
            if (saw_source_dir_clause || saw_source_target_dir_clause || i + 1 >= arena_arr_len(args)) {
                target_diag_error(
                    ctx,
                    node,
                    nob_sv_from_cstr("get_property(SOURCE DIRECTORY ...) requires exactly one directory"),
                    nob_sv_from_cstr(""));
                return false;
            }

            out_req->inherit_dir =
                eval_path_resolve_for_cmake_arg(ctx, args[i + 1], cur_src, true);
            if (eval_should_stop(ctx)) return false;
            if (!eval_directory_is_known(ctx, out_req->inherit_dir)) {
                property_diag_unknown_directory(
                    ctx,
                    node,
                    nob_sv_from_cstr("get_property(SOURCE DIRECTORY ...) directory is not known"),
                    out_req->inherit_dir);
                return false;
            }

            saw_source_dir_clause = true;
            i += 2;
            continue;
        }

        if (eval_sv_eq_ci_lit(args[i], "TARGET_DIRECTORY")) {
            if (saw_source_dir_clause || saw_source_target_dir_clause || i + 1 >= arena_arr_len(args)) {
                target_diag_error(
                    ctx,
                    node,
                    nob_sv_from_cstr("get_property(SOURCE TARGET_DIRECTORY ...) requires exactly one target"),
                    nob_sv_from_cstr(""));
                return false;
            }

            String_View target_name = args[i + 1];
            if (!eval_target_known(ctx, target_name)) {
                target_diag_error(
                    ctx,
                    node,
                    nob_sv_from_cstr("get_property(SOURCE TARGET_DIRECTORY ...) target was not declared"),
                    target_name);
                return false;
            }
            if (!target_get_declared_dir_temp(ctx, target_name, &out_req->inherit_dir)) return false;
            if (eval_should_stop(ctx)) return false;

            out_req->object_id =
                eval_property_scoped_object_id_temp(ctx, "TARGET_DIRECTORY", target_name, out_req->object_token);
            if (eval_should_stop(ctx)) return false;

            saw_source_target_dir_clause = true;
            i += 2;
            continue;
        }

        target_diag_error(
            ctx,
            node,
            nob_sv_from_cstr("get_property(SOURCE ...) received unexpected argument before PROPERTY"),
            args[i]);
        return false;
    }

    if (!saw_source_target_dir_clause) {
        if (saw_source_dir_clause) {
            out_req->object_id = eval_property_scoped_object_id_temp(
                ctx, "DIRECTORY", out_req->inherit_dir, out_req->object_token);
        } else {
            out_req->object_id = out_req->object_token;
            out_req->inherit_dir = cur_src;
        }
        if (eval_should_stop(ctx)) return false;
    }

    *io_index = i;
    return true;
}

static bool get_property_parse_test_scope(EvalExecContext *ctx,
                                          const Node *node,
                                          SV_List args,
                                          size_t *io_index,
                                          Get_Property_Request *out_req) {
    size_t i = *io_index;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    bool saw_test_dir_clause = false;

    while (i < arena_arr_len(args) && !eval_sv_eq_ci_lit(args[i], "PROPERTY")) {
        if (!eval_sv_eq_ci_lit(args[i], "DIRECTORY") || saw_test_dir_clause || i + 1 >= arena_arr_len(args)) {
            target_diag_error(ctx,
                              node,
                              nob_sv_from_cstr("get_property(TEST ...) received invalid DIRECTORY clause"),
                              nob_sv_from_cstr(""));
            return false;
        }

        out_req->inherit_dir =
            eval_path_resolve_for_cmake_arg(ctx, args[i + 1], cur_src, true);
        if (eval_should_stop(ctx)) return false;
        if (!eval_directory_is_known(ctx, out_req->inherit_dir)) {
            property_diag_unknown_directory(
                ctx,
                node,
                nob_sv_from_cstr("get_property(TEST DIRECTORY ...) directory is not known"),
                out_req->inherit_dir);
            return false;
        }

        saw_test_dir_clause = true;
        i += 2;
    }

    if (saw_test_dir_clause) {
        out_req->object_id = eval_property_scoped_object_id_temp(
            ctx, "DIRECTORY", out_req->inherit_dir, out_req->object_token);
    } else {
        out_req->object_id = out_req->object_token;
        out_req->inherit_dir = cur_src;
    }
    if (eval_should_stop(ctx)) return false;

    *io_index = i;
    return true;
}

static bool get_property_parse_request(EvalExecContext *ctx,
                                       const Node *node,
                                       Cmake_Event_Origin origin,
                                       SV_List args,
                                       Get_Property_Request *out_req) {
    if (arena_arr_len(args) < 4) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_MISSING_REQUIRED,
            "dispatcher",
            nob_sv_from_cstr("get_property() requires output variable, scope, PROPERTY and property name"),
            nob_sv_from_cstr(
                "Usage: get_property(<var> <GLOBAL|DIRECTORY|TARGET|SOURCE|INSTALL|TEST|CACHE|VARIABLE> ... PROPERTY <name> [SET|DEFINED|BRIEF_DOCS|FULL_DOCS])"));
        return false;
    }

    *out_req = (Get_Property_Request){0};
    out_req->out_var = args[0];
    if (!get_property_parse_scope(ctx, node, origin, args[1], &out_req->scope, &out_req->scope_upper)) {
        return false;
    }

    size_t i = 2;
    if (!(out_req->scope == GET_PROPERTY_SCOPE_GLOBAL || out_req->scope == GET_PROPERTY_SCOPE_VARIABLE)) {
        if (i >= arena_arr_len(args)) {
            target_diag_error(ctx,
                              node,
                              nob_sv_from_cstr("get_property() missing scope object"),
                              nob_sv_from_cstr(""));
            return false;
        }

        if (out_req->scope == GET_PROPERTY_SCOPE_DIRECTORY) {
            if (!eval_sv_eq_ci_lit(args[i], "PROPERTY")) out_req->object_token = args[i++];
        } else {
            out_req->object_token = args[i++];
        }
    }

    switch (out_req->scope) {
        case GET_PROPERTY_SCOPE_DIRECTORY:
            if (!get_property_parse_directory_scope(ctx, node, out_req)) return false;
            break;
        case GET_PROPERTY_SCOPE_SOURCE:
            if (!get_property_parse_source_scope(ctx, node, args, &i, out_req)) return false;
            break;
        case GET_PROPERTY_SCOPE_TEST:
            if (!get_property_parse_test_scope(ctx, node, args, &i, out_req)) return false;
            break;
        case GET_PROPERTY_SCOPE_TARGET:
        case GET_PROPERTY_SCOPE_INSTALL:
        case GET_PROPERTY_SCOPE_CACHE:
            out_req->object_id = out_req->object_token;
            break;
        case GET_PROPERTY_SCOPE_GLOBAL:
        case GET_PROPERTY_SCOPE_VARIABLE:
            break;
    }

    return get_property_parse_property_clause(ctx, node, origin, args, &i, out_req);
}

static bool get_cmake_property_parse_request(EvalExecContext *ctx,
                                             const Node *node,
                                             Cmake_Event_Origin origin,
                                             SV_List args,
                                             Get_Cmake_Property_Request *out_req) {
    if (arena_arr_len(args) != 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_MISSING_REQUIRED,
            "dispatcher",
            nob_sv_from_cstr("get_cmake_property() expects output variable and property name"),
            nob_sv_from_cstr("Usage: get_cmake_property(<var> <property>)"));
        return false;
    }

    *out_req = (Get_Cmake_Property_Request){
        .out_var = args[0],
        .property_name = args[1],
    };
    return true;
}

static bool get_directory_property_parse_request(EvalExecContext *ctx,
                                                 const Node *node,
                                                 Cmake_Event_Origin origin,
                                                 SV_List args,
                                                 Get_Directory_Property_Request *out_req) {
    if (arena_arr_len(args) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_MISSING_REQUIRED,
            "dispatcher",
            nob_sv_from_cstr("get_directory_property() requires output variable and query"),
            nob_sv_from_cstr(
                "Usage: get_directory_property(<var> [DIRECTORY <dir>] <prop-name>|DEFINITION <var-name>)"));
        return false;
    }

    *out_req = (Get_Directory_Property_Request){
        .out_var = args[0],
        .directory = eval_current_source_dir_for_paths(ctx),
        .kind = GET_DIRECTORY_PROPERTY_QUERY_VALUE,
    };

    size_t i = 1;
    if (eval_sv_eq_ci_lit(args[i], "DIRECTORY")) {
        if (i + 1 >= arena_arr_len(args)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
                ctx,
                node,
                origin,
                EV_DIAG_ERROR,
                EVAL_DIAG_MISSING_REQUIRED,
                "dispatcher",
                nob_sv_from_cstr("get_directory_property(DIRECTORY ...) requires a directory"),
                nob_sv_from_cstr(""));
            return false;
        }

        out_req->directory =
            eval_path_resolve_for_cmake_arg(ctx, args[i + 1], out_req->directory, true);
        if (eval_should_stop(ctx)) return false;
        if (!eval_directory_is_known(ctx, out_req->directory)) {
            property_diag_unknown_directory(
                ctx,
                node,
                nob_sv_from_cstr("get_directory_property(DIRECTORY ...) directory is not known"),
                out_req->directory);
            return false;
        }
        i += 2;
    }

    if (i >= arena_arr_len(args)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("get_directory_property() missing property query"),
                                       nob_sv_from_cstr(""));
        return false;
    }

    if (eval_sv_eq_ci_lit(args[i], "DEFINITION")) {
        if (i + 1 >= arena_arr_len(args)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
                ctx,
                node,
                origin,
                EV_DIAG_ERROR,
                EVAL_DIAG_MISSING_REQUIRED,
                "dispatcher",
                nob_sv_from_cstr("get_directory_property(DEFINITION ...) requires a variable name"),
                nob_sv_from_cstr(""));
            return false;
        }
        if (i + 2 != arena_arr_len(args)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
                ctx,
                node,
                origin,
                EV_DIAG_ERROR,
                EVAL_DIAG_MISSING_REQUIRED,
                "dispatcher",
                nob_sv_from_cstr("get_directory_property(DEFINITION ...) expects exactly one variable name"),
                nob_sv_from_cstr(""));
            return false;
        }

        out_req->kind = GET_DIRECTORY_PROPERTY_QUERY_DEFINITION;
        out_req->query_name = args[i + 1];
        return true;
    }

    if (i + 1 != arena_arr_len(args)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_UNEXPECTED_ARGUMENT,
            "dispatcher",
            nob_sv_from_cstr("get_directory_property() received unexpected trailing arguments"),
            args[i + 1]);
        return false;
    }

    out_req->query_name = args[i];
    return true;
}

static bool property_triplet_parse_request(EvalExecContext *ctx,
                                           const Node *node,
                                           Cmake_Event_Origin origin,
                                           SV_List args,
                                           String_View cause,
                                           String_View hint,
                                           Property_Triplet_Request *out_req) {
    if (arena_arr_len(args) != 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", cause, hint);
        return false;
    }

    *out_req = (Property_Triplet_Request){
        .out_var = args[0],
        .object_name = args[1],
        .property_name = args[2],
    };
    return true;
}

Eval_Result eval_handle_get_property(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Get_Property_Request req = {0};
    if (!get_property_parse_request(ctx, node, origin, args, &req)) {
        return eval_result_from_ctx(ctx);
    }

    Property_Query_Request query = {
        .out_var = req.out_var,
        .scope_upper = req.scope_upper,
        .object_id = req.object_id,
        .validation_object = req.object_token,
        .property_name = req.property_name,
        .inherit_dir = req.inherit_dir,
        .mode = req.mode,
        .validate_object = true,
        .missing_as_notfound = false,
        .unset_as_empty = false,
    };
    if (!property_query_request_execute(ctx, node, origin, &query)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_cmake_property(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Get_Cmake_Property_Request req = {0};
    if (!get_cmake_property_parse_request(ctx, node, origin, args, &req)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_bool(
        eval_property_query_cmake(ctx, node, origin, req.out_var, req.property_name));
}

Eval_Result eval_handle_get_directory_property(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Get_Directory_Property_Request req = {0};
    if (!get_directory_property_parse_request(ctx, node, origin, args, &req)) {
        return eval_result_from_ctx(ctx);
    }

    if (req.kind == GET_DIRECTORY_PROPERTY_QUERY_DEFINITION) {
        String_View value = eval_var_get_visible(ctx, req.query_name);
        return eval_result_from_bool(eval_var_set_current(ctx, req.out_var, value));
    }

    Property_Query_Request query = {
        .out_var = req.out_var,
        .scope_upper = nob_sv_from_cstr("DIRECTORY"),
        .object_id = req.directory,
        .validation_object = req.directory,
        .property_name = req.query_name,
        .inherit_dir = req.directory,
        .mode = EVAL_PROP_QUERY_VALUE,
        .validate_object = false,
        .missing_as_notfound = false,
        .unset_as_empty = true,
    };
    if (!property_query_request_execute(ctx, node, origin, &query)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_source_file_property(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Property_Triplet_Request req = {0};
    if (!property_triplet_parse_request(
            ctx,
            node,
            origin,
            args,
            nob_sv_from_cstr("get_source_file_property() expects output variable, source and property"),
            nob_sv_from_cstr("Usage: get_source_file_property(<var> <source> <property>)"),
            &req)) {
        return eval_result_from_ctx(ctx);
    }

    Property_Query_Request query = {
        .out_var = req.out_var,
        .scope_upper = nob_sv_from_cstr("SOURCE"),
        .object_id = req.object_name,
        .validation_object = req.object_name,
        .property_name = req.property_name,
        .inherit_dir = eval_current_source_dir_for_paths(ctx),
        .mode = EVAL_PROP_QUERY_VALUE,
        .validate_object = false,
        .missing_as_notfound = true,
        .unset_as_empty = false,
    };
    if (!property_query_request_execute(ctx, node, origin, &query)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_target_property(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Property_Triplet_Request req = {0};
    if (!property_triplet_parse_request(
            ctx,
            node,
            origin,
            args,
            nob_sv_from_cstr("get_target_property() expects output variable, target and property"),
            nob_sv_from_cstr("Usage: get_target_property(<var> <target> <property>)"),
            &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!eval_target_known(ctx, req.object_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_NOT_FOUND,
                                       "dispatcher",
                                       nob_sv_from_cstr("get_target_property() target was not declared"),
                                       req.object_name);
        return eval_result_from_ctx(ctx);
    }

    Property_Query_Request query = {
        .out_var = req.out_var,
        .scope_upper = nob_sv_from_cstr("TARGET"),
        .object_id = req.object_name,
        .validation_object = req.object_name,
        .property_name = req.property_name,
        .inherit_dir = nob_sv_from_cstr(""),
        .mode = EVAL_PROP_QUERY_VALUE,
        .validate_object = false,
        .missing_as_notfound = true,
        .unset_as_empty = false,
    };
    if (!property_query_request_execute(ctx, node, origin, &query)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_get_test_property(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Property_Triplet_Request req = {0};
    if (!property_triplet_parse_request(
            ctx,
            node,
            origin,
            args,
            nob_sv_from_cstr("get_test_property() expects output variable, test and property"),
            nob_sv_from_cstr("Usage: get_test_property(<var> <test> <property>)"),
            &req)) {
        return eval_result_from_ctx(ctx);
    }

    String_View test_dir = eval_current_source_dir_for_paths(ctx);
    if (!eval_test_exists_in_directory_scope(ctx, req.object_name, test_dir)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_NOT_FOUND,
            "dispatcher",
            nob_sv_from_cstr("get_test_property() test was not declared in current directory scope"),
            req.object_name);
        return eval_result_from_ctx(ctx);
    }

    Property_Query_Request query = {
        .out_var = req.out_var,
        .scope_upper = nob_sv_from_cstr("TEST"),
        .object_id = req.object_name,
        .validation_object = req.object_name,
        .property_name = req.property_name,
        .inherit_dir = test_dir,
        .mode = EVAL_PROP_QUERY_VALUE,
        .validate_object = false,
        .missing_as_notfound = true,
        .unset_as_empty = false,
    };
    if (!property_query_request_execute(ctx, node, origin, &query)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}
