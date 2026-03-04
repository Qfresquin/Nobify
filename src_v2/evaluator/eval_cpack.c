#include "eval_cpack.h"

#include "evaluator_internal.h"
#include "eval_opt_parser.h"

static bool require_cpack_component_module(Evaluator_Context *ctx,
                                           String_View command,
                                           Cmake_Event_Origin origin) {
    if (!ctx) return false;
    if (ctx->cpack_component_module_loaded) return true;
    EVAL_DIAG(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("dispatcher"),
                   command,
                   origin,
                   nob_sv_from_cstr("Unknown command"),
                   nob_sv_from_cstr("include(CPackComponent) must be called before using this command"));
    return false;
}

enum {
    CPACK_INSTALL_TYPE_OPT_DISPLAY_NAME = 1,
};

typedef struct {
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View display_name;
} Cpack_Install_Type_Opts;

static bool cpack_install_type_on_option(Evaluator_Context *ctx,
                                         void *userdata,
                                         int id,
                                         SV_List values,
                                         size_t token_index) {
    (void)ctx;
    (void)token_index;
    if (!userdata) return false;
    Cpack_Install_Type_Opts *st = (Cpack_Install_Type_Opts*)userdata;
    if (id == CPACK_INSTALL_TYPE_OPT_DISPLAY_NAME && arena_arr_len(values) > 0) st->display_name = values[0];
    return true;
}

static bool cpack_install_type_on_positional(Evaluator_Context *ctx,
                                             void *userdata,
                                             String_View value,
                                             size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Install_Type_Opts *st = (Cpack_Install_Type_Opts*)userdata;
    EVAL_DIAG(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   st->command_name,
                   st->origin,
                   nob_sv_from_cstr("cpack_add_install_type() unexpected argument"),
                   value);
    return !eval_should_stop(ctx);
}

enum {
    CPACK_GROUP_OPT_DISPLAY_NAME = 1,
    CPACK_GROUP_OPT_DESCRIPTION,
    CPACK_GROUP_OPT_PARENT_GROUP,
    CPACK_GROUP_OPT_EXPANDED,
    CPACK_GROUP_OPT_BOLD_TITLE,
};

typedef struct {
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View display_name;
    String_View description;
    String_View parent_group;
    bool expanded;
    bool bold_title;
} Cpack_Group_Opts;

static bool cpack_group_on_option(Evaluator_Context *ctx,
                                  void *userdata,
                                  int id,
                                  SV_List values,
                                  size_t token_index) {
    (void)ctx;
    (void)token_index;
    if (!userdata) return false;
    Cpack_Group_Opts *st = (Cpack_Group_Opts*)userdata;
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

static bool cpack_group_on_positional(Evaluator_Context *ctx,
                                      void *userdata,
                                      String_View value,
                                      size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Group_Opts *st = (Cpack_Group_Opts*)userdata;
    EVAL_DIAG(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   st->command_name,
                   st->origin,
                   nob_sv_from_cstr("cpack_add_component_group() unexpected argument"),
                   value);
    return !eval_should_stop(ctx);
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
} Cpack_Component_Opts;

static bool cpack_component_on_option(Evaluator_Context *ctx,
                                      void *userdata,
                                      int id,
                                      SV_List values,
                                      size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Component_Opts *st = (Cpack_Component_Opts*)userdata;
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

static bool cpack_component_on_positional(Evaluator_Context *ctx,
                                          void *userdata,
                                          String_View value,
                                          size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Component_Opts *st = (Cpack_Component_Opts*)userdata;
    EVAL_DIAG(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   st->command_name,
                   st->origin,
                   nob_sv_from_cstr("cpack_add_component() unsupported/extra argument"),
                   value);
    return !eval_should_stop(ctx);
}

bool eval_handle_cpack_add_install_type(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (!require_cpack_component_module(ctx, node->as.cmd.name, o)) return !eval_should_stop(ctx);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "dispatcher", nob_sv_from_cstr("cpack_add_install_type() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a[0];
    Cpack_Install_Type_Opts opt = {
        .origin = o,
        .command_name = node->as.cmd.name,
        .display_name = nob_sv_from_cstr(""),
    };
    static const Eval_Opt_Spec k_specs[] = {
        {CPACK_INSTALL_TYPE_OPT_DISPLAY_NAME, "DISPLAY_NAME", EVAL_OPT_SINGLE},
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             a,
                             1,
                             k_specs,
                             NOB_ARRAY_LEN(k_specs),
                             cfg,
                             cpack_install_type_on_option,
                             cpack_install_type_on_positional,
                             &opt)) {
        return !eval_should_stop(ctx);
    }

    if (!eval_emit_cpack_add_install_type(ctx, o, name, opt.display_name)) return false;
    return !eval_should_stop(ctx);
}

bool eval_handle_cpack_add_component_group(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (!require_cpack_component_module(ctx, node->as.cmd.name, o)) return !eval_should_stop(ctx);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "dispatcher", nob_sv_from_cstr("cpack_add_component_group() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a[0];
    Cpack_Group_Opts opt = {
        .origin = o,
        .command_name = node->as.cmd.name,
        .display_name = nob_sv_from_cstr(""),
        .description = nob_sv_from_cstr(""),
        .parent_group = nob_sv_from_cstr(""),
        .expanded = false,
        .bold_title = false,
    };
    static const Eval_Opt_Spec k_specs[] = {
        {CPACK_GROUP_OPT_DISPLAY_NAME, "DISPLAY_NAME", EVAL_OPT_SINGLE},
        {CPACK_GROUP_OPT_DESCRIPTION, "DESCRIPTION", EVAL_OPT_SINGLE},
        {CPACK_GROUP_OPT_PARENT_GROUP, "PARENT_GROUP", EVAL_OPT_SINGLE},
        {CPACK_GROUP_OPT_EXPANDED, "EXPANDED", EVAL_OPT_FLAG},
        {CPACK_GROUP_OPT_BOLD_TITLE, "BOLD_TITLE", EVAL_OPT_FLAG},
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             a,
                             1,
                             k_specs,
                             NOB_ARRAY_LEN(k_specs),
                             cfg,
                             cpack_group_on_option,
                             cpack_group_on_positional,
                             &opt)) {
        return !eval_should_stop(ctx);
    }

    if (!eval_emit_cpack_add_component_group(ctx,
                                             o,
                                             name,
                                             opt.display_name,
                                             opt.description,
                                             opt.parent_group,
                                             opt.expanded,
                                             opt.bold_title)) {
        return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_cpack_add_component(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (!require_cpack_component_module(ctx, node->as.cmd.name, o)) return !eval_should_stop(ctx);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "dispatcher", nob_sv_from_cstr("cpack_add_component() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a[0];
    Cpack_Component_Opts opt = {
        .origin = o,
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
        {CPACK_COMPONENT_OPT_DISPLAY_NAME, "DISPLAY_NAME", EVAL_OPT_SINGLE},
        {CPACK_COMPONENT_OPT_DESCRIPTION, "DESCRIPTION", EVAL_OPT_SINGLE},
        {CPACK_COMPONENT_OPT_GROUP, "GROUP", EVAL_OPT_SINGLE},
        {CPACK_COMPONENT_OPT_DEPENDS, "DEPENDS", EVAL_OPT_MULTI},
        {CPACK_COMPONENT_OPT_INSTALL_TYPES, "INSTALL_TYPES", EVAL_OPT_MULTI},
        {CPACK_COMPONENT_OPT_ARCHIVE_FILE, "ARCHIVE_FILE", EVAL_OPT_SINGLE},
        {CPACK_COMPONENT_OPT_PLIST, "PLIST", EVAL_OPT_SINGLE},
        {CPACK_COMPONENT_OPT_REQUIRED, "REQUIRED", EVAL_OPT_FLAG},
        {CPACK_COMPONENT_OPT_HIDDEN, "HIDDEN", EVAL_OPT_FLAG},
        {CPACK_COMPONENT_OPT_DISABLED, "DISABLED", EVAL_OPT_FLAG},
        {CPACK_COMPONENT_OPT_DOWNLOADED, "DOWNLOADED", EVAL_OPT_FLAG},
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             a,
                             1,
                             k_specs,
                             NOB_ARRAY_LEN(k_specs),
                             cfg,
                             cpack_component_on_option,
                             cpack_component_on_positional,
                             &opt)) {
        return !eval_should_stop(ctx);
    }

    if (!eval_emit_cpack_add_component(ctx,
                                       o,
                                       name,
                                       opt.display_name,
                                       opt.description,
                                       opt.group,
                                       opt.depends,
                                       opt.install_types,
                                       opt.archive_file,
                                       opt.plist,
                                       opt.required,
                                       opt.hidden,
                                       opt.disabled,
                                       opt.downloaded)) {
        return false;
    }
    return !eval_should_stop(ctx);
}
