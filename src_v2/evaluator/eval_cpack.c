#include "eval_cpack.h"

#include "evaluator_internal.h"
#include "eval_opt_parser.h"

static bool require_cpack_component_module(EvalExecContext *ctx,
                                           String_View command,
                                           Cmake_Event_Origin origin) {
    if (!ctx) return false;
    if (ctx->cpack_component_module_loaded) return true;
    EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNKNOWN_COMMAND, nob_sv_from_cstr("dispatcher"), command, origin, nob_sv_from_cstr("Unknown command"), nob_sv_from_cstr("include(CPackComponent) must be called before using this command"));
    return false;
}

enum {
    CPACK_INSTALL_TYPE_OPT_DISPLAY_NAME = 1,
};

typedef struct {
    String_View name;
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View display_name;
} Cpack_Install_Type_Request;

static bool cpack_install_type_on_option(EvalExecContext *ctx,
                                         void *userdata,
                                         int id,
                                         SV_List values,
                                         size_t token_index) {
    (void)ctx;
    (void)token_index;
    if (!userdata) return false;
    Cpack_Install_Type_Request *st = (Cpack_Install_Type_Request*)userdata;
    if (id == CPACK_INSTALL_TYPE_OPT_DISPLAY_NAME && arena_arr_len(values) > 0) st->display_name = values[0];
    return true;
}

static bool cpack_install_type_on_positional(EvalExecContext *ctx,
                                             void *userdata,
                                             String_View value,
                                             size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Install_Type_Request *st = (Cpack_Install_Type_Request*)userdata;
    EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_UNEXPECTED_ARGUMENT, nob_sv_from_cstr("dispatcher"), st->command_name, st->origin, nob_sv_from_cstr("cpack_add_install_type() unexpected argument"), value);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

enum {
    CPACK_GROUP_OPT_DISPLAY_NAME = 1,
    CPACK_GROUP_OPT_DESCRIPTION,
    CPACK_GROUP_OPT_PARENT_GROUP,
    CPACK_GROUP_OPT_EXPANDED,
    CPACK_GROUP_OPT_BOLD_TITLE,
};

typedef struct {
    String_View name;
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View display_name;
    String_View description;
    String_View parent_group;
    bool expanded;
    bool bold_title;
} Cpack_Component_Group_Request;

static bool cpack_group_on_option(EvalExecContext *ctx,
                                  void *userdata,
                                  int id,
                                  SV_List values,
                                  size_t token_index) {
    (void)ctx;
    (void)token_index;
    if (!userdata) return false;
    Cpack_Component_Group_Request *st = (Cpack_Component_Group_Request*)userdata;
    switch (id) {
    case CPACK_GROUP_OPT_DISPLAY_NAME:
        if (arena_arr_len(values) > 0) st->display_name = values[0];
        return true;
    case CPACK_GROUP_OPT_DESCRIPTION:
        if (arena_arr_len(values) > 0) st->description = values[0];
        return true;
    case CPACK_GROUP_OPT_PARENT_GROUP:
        if (arena_arr_len(values) > 0) st->parent_group = values[0];
        return true;
    case CPACK_GROUP_OPT_EXPANDED:
        st->expanded = true;
        return true;
    case CPACK_GROUP_OPT_BOLD_TITLE:
        st->bold_title = true;
        return true;
    default:
        return true;
    }
}

static bool cpack_group_on_positional(EvalExecContext *ctx,
                                      void *userdata,
                                      String_View value,
                                      size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Component_Group_Request *st = (Cpack_Component_Group_Request*)userdata;
    EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_UNEXPECTED_ARGUMENT, nob_sv_from_cstr("dispatcher"), st->command_name, st->origin, nob_sv_from_cstr("cpack_add_component_group() unexpected argument"), value);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

enum {
    CPACK_COMPONENT_OPT_DISPLAY_NAME = 1,
    CPACK_COMPONENT_OPT_DESCRIPTION,
    CPACK_COMPONENT_OPT_GROUP,
    CPACK_COMPONENT_OPT_DEPENDS,
    CPACK_COMPONENT_OPT_INSTALL_TYPES,
    CPACK_COMPONENT_OPT_ARCHIVE_FILE,
    CPACK_COMPONENT_OPT_PLIST,
    CPACK_COMPONENT_OPT_REQUIRED,
    CPACK_COMPONENT_OPT_HIDDEN,
    CPACK_COMPONENT_OPT_DISABLED,
    CPACK_COMPONENT_OPT_DOWNLOADED,
};

typedef struct {
    String_View name;
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View display_name;
    String_View description;
    String_View group;
    String_View depends;
    String_View install_types;
    String_View archive_file;
    String_View plist;
    bool required;
    bool hidden;
    bool disabled;
    bool downloaded;
} Cpack_Component_Request;

static bool cpack_component_on_option(EvalExecContext *ctx,
                                      void *userdata,
                                      int id,
                                      SV_List values,
                                      size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Component_Request *st = (Cpack_Component_Request*)userdata;
    switch (id) {
    case CPACK_COMPONENT_OPT_DISPLAY_NAME:
        if (arena_arr_len(values) > 0) st->display_name = values[0];
        return true;
    case CPACK_COMPONENT_OPT_DESCRIPTION:
        if (arena_arr_len(values) > 0) st->description = values[0];
        return true;
    case CPACK_COMPONENT_OPT_GROUP:
        if (arena_arr_len(values) > 0) st->group = values[0];
        return true;
    case CPACK_COMPONENT_OPT_DEPENDS:
        st->depends = arena_arr_len(values) > 0 ? eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)) : nob_sv_from_cstr("");
        return true;
    case CPACK_COMPONENT_OPT_INSTALL_TYPES:
        st->install_types = arena_arr_len(values) > 0 ? eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)) : nob_sv_from_cstr("");
        return true;
    case CPACK_COMPONENT_OPT_ARCHIVE_FILE:
        if (arena_arr_len(values) > 0) st->archive_file = values[0];
        return true;
    case CPACK_COMPONENT_OPT_PLIST:
        if (arena_arr_len(values) > 0) st->plist = values[0];
        return true;
    case CPACK_COMPONENT_OPT_REQUIRED:
        st->required = true;
        return true;
    case CPACK_COMPONENT_OPT_HIDDEN:
        st->hidden = true;
        return true;
    case CPACK_COMPONENT_OPT_DISABLED:
        st->disabled = true;
        return true;
    case CPACK_COMPONENT_OPT_DOWNLOADED:
        st->downloaded = true;
        return true;
    default:
        return true;
    }
}

static bool cpack_component_on_positional(EvalExecContext *ctx,
                                          void *userdata,
                                          String_View value,
                                          size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Component_Request *st = (Cpack_Component_Request*)userdata;
    EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("dispatcher"), st->command_name, st->origin, nob_sv_from_cstr("cpack_add_component() unsupported/extra argument"), value);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool cpack_parse_install_type_request(EvalExecContext *ctx,
                                             const Node *node,
                                             Cmake_Event_Origin origin,
                                             Cpack_Install_Type_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (!require_cpack_component_module(ctx, node->as.cmd.name, origin)) return false;

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(args) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cpack_add_install_type() missing name"), nob_sv_from_cstr(""));
        return false;
    }

    *out_req = (Cpack_Install_Type_Request){
        .name = args[0],
        .origin = origin,
        .command_name = node->as.cmd.name,
        .display_name = nob_sv_from_cstr(""),
    };
    static const Eval_Opt_Spec k_specs[] = {
        EVAL_OPT_SPEC(CPACK_INSTALL_TYPE_OPT_DISPLAY_NAME, "DISPLAY_NAME", EVAL_OPT_SINGLE),
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = origin;
    return eval_opt_parse_walk(ctx,
                               args,
                               1,
                               k_specs,
                               NOB_ARRAY_LEN(k_specs),
                               cfg,
                               cpack_install_type_on_option,
                               cpack_install_type_on_positional,
                               out_req);
}

static bool cpack_execute_install_type_request(EvalExecContext *ctx,
                                               const Cpack_Install_Type_Request *req) {
    if (!ctx || !req) return false;
    return eval_emit_cpack_add_install_type(ctx, req->origin, req->name, req->display_name);
}

static bool cpack_parse_component_group_request(EvalExecContext *ctx,
                                                const Node *node,
                                                Cmake_Event_Origin origin,
                                                Cpack_Component_Group_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (!require_cpack_component_module(ctx, node->as.cmd.name, origin)) return false;

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(args) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cpack_add_component_group() missing name"), nob_sv_from_cstr(""));
        return false;
    }

    *out_req = (Cpack_Component_Group_Request){
        .name = args[0],
        .origin = origin,
        .command_name = node->as.cmd.name,
        .display_name = nob_sv_from_cstr(""),
        .description = nob_sv_from_cstr(""),
        .parent_group = nob_sv_from_cstr(""),
        .expanded = false,
        .bold_title = false,
    };
    static const Eval_Opt_Spec k_specs[] = {
        EVAL_OPT_SPEC(CPACK_GROUP_OPT_DISPLAY_NAME, "DISPLAY_NAME", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CPACK_GROUP_OPT_DESCRIPTION, "DESCRIPTION", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CPACK_GROUP_OPT_PARENT_GROUP, "PARENT_GROUP", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CPACK_GROUP_OPT_EXPANDED, "EXPANDED", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CPACK_GROUP_OPT_BOLD_TITLE, "BOLD_TITLE", EVAL_OPT_FLAG),
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = origin;
    return eval_opt_parse_walk(ctx,
                               args,
                               1,
                               k_specs,
                               NOB_ARRAY_LEN(k_specs),
                               cfg,
                               cpack_group_on_option,
                               cpack_group_on_positional,
                               out_req);
}

static bool cpack_execute_component_group_request(EvalExecContext *ctx,
                                                  const Cpack_Component_Group_Request *req) {
    if (!ctx || !req) return false;
    return eval_emit_cpack_add_component_group(ctx,
                                               req->origin,
                                               req->name,
                                               req->display_name,
                                               req->description,
                                               req->parent_group,
                                               req->expanded,
                                               req->bold_title);
}

static bool cpack_parse_component_request(EvalExecContext *ctx,
                                          const Node *node,
                                          Cmake_Event_Origin origin,
                                          Cpack_Component_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (!require_cpack_component_module(ctx, node->as.cmd.name, origin)) return false;

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(args) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("cpack_add_component() missing name"), nob_sv_from_cstr(""));
        return false;
    }

    *out_req = (Cpack_Component_Request){
        .name = args[0],
        .origin = origin,
        .command_name = node->as.cmd.name,
        .display_name = nob_sv_from_cstr(""),
        .description = nob_sv_from_cstr(""),
        .group = nob_sv_from_cstr(""),
        .depends = nob_sv_from_cstr(""),
        .install_types = nob_sv_from_cstr(""),
        .archive_file = nob_sv_from_cstr(""),
        .plist = nob_sv_from_cstr(""),
        .required = false,
        .hidden = false,
        .disabled = false,
        .downloaded = false,
    };
    static const Eval_Opt_Spec k_specs[] = {
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_DISPLAY_NAME, "DISPLAY_NAME", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_DESCRIPTION, "DESCRIPTION", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_GROUP, "GROUP", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_DEPENDS, "DEPENDS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_INSTALL_TYPES, "INSTALL_TYPES", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_ARCHIVE_FILE, "ARCHIVE_FILE", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_PLIST, "PLIST", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_REQUIRED, "REQUIRED", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_HIDDEN, "HIDDEN", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_DISABLED, "DISABLED", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CPACK_COMPONENT_OPT_DOWNLOADED, "DOWNLOADED", EVAL_OPT_FLAG),
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = origin;
    return eval_opt_parse_walk(ctx,
                               args,
                               1,
                               k_specs,
                               NOB_ARRAY_LEN(k_specs),
                               cfg,
                               cpack_component_on_option,
                               cpack_component_on_positional,
                               out_req);
}

static bool cpack_execute_component_request(EvalExecContext *ctx,
                                            const Cpack_Component_Request *req) {
    if (!ctx || !req) return false;
    return eval_emit_cpack_add_component(ctx,
                                         req->origin,
                                         req->name,
                                         req->display_name,
                                         req->description,
                                         req->group,
                                         req->depends,
                                         req->install_types,
                                         req->archive_file,
                                         req->plist,
                                         req->required,
                                         req->hidden,
                                         req->disabled,
                                         req->downloaded);
}

Eval_Result eval_handle_cpack_add_install_type(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Cpack_Install_Type_Request req = {0};
    if (!cpack_parse_install_type_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    if (!cpack_execute_install_type_request(ctx, &req)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_cpack_add_component_group(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Cpack_Component_Group_Request req = {0};
    if (!cpack_parse_component_group_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    if (!cpack_execute_component_group_request(ctx, &req)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_cpack_add_component(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    Cpack_Component_Request req = {0};
    if (!cpack_parse_component_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    if (!cpack_execute_component_request(ctx, &req)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}
