#include "eval_cpack.h"

#include "evaluator_internal.h"
#include "eval_opt_parser.h"

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
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
    if (id == CPACK_INSTALL_TYPE_OPT_DISPLAY_NAME && values.count > 0) st->display_name = values.items[0];
    return true;
}

static bool cpack_install_type_on_positional(Evaluator_Context *ctx,
                                             void *userdata,
                                             String_View value,
                                             size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Cpack_Install_Type_Opts *st = (Cpack_Install_Type_Opts*)userdata;
    eval_emit_diag(ctx,
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
        if (values.count > 0) st->display_name = values.items[0];
        return true;
    case CPACK_GROUP_OPT_DESCRIPTION:
        if (values.count > 0) st->description = values.items[0];
        return true;
    case CPACK_GROUP_OPT_PARENT_GROUP:
        if (values.count > 0) st->parent_group = values.items[0];
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
    eval_emit_diag(ctx,
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
        if (values.count > 0) st->display_name = values.items[0];
        return true;
    case CPACK_COMPONENT_OPT_DESCRIPTION:
        if (values.count > 0) st->description = values.items[0];
        return true;
    case CPACK_COMPONENT_OPT_GROUP:
        if (values.count > 0) st->group = values.items[0];
        return true;
    case CPACK_COMPONENT_OPT_DEPENDS:
        st->depends = values.count > 0 ? eval_sv_join_semi_temp(ctx, values.items, values.count) : nob_sv_from_cstr("");
        return true;
    case CPACK_COMPONENT_OPT_INSTALL_TYPES:
        st->install_types = values.count > 0 ? eval_sv_join_semi_temp(ctx, values.items, values.count) : nob_sv_from_cstr("");
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
    eval_emit_diag(ctx,
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
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_install_type() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
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

    Cmake_Event ev = {0};
    ev.kind = EV_CPACK_ADD_INSTALL_TYPE;
    ev.origin = o;
    ev.as.cpack_add_install_type.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_install_type.display_name = sv_copy_to_event_arena(ctx, opt.display_name);
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

bool eval_handle_cpack_add_component_group(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_component_group() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
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

    Cmake_Event ev = {0};
    ev.kind = EV_CPACK_ADD_COMPONENT_GROUP;
    ev.origin = o;
    ev.as.cpack_add_component_group.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_component_group.display_name = sv_copy_to_event_arena(ctx, opt.display_name);
    ev.as.cpack_add_component_group.description = sv_copy_to_event_arena(ctx, opt.description);
    ev.as.cpack_add_component_group.parent_group = sv_copy_to_event_arena(ctx, opt.parent_group);
    ev.as.cpack_add_component_group.expanded = opt.expanded;
    ev.as.cpack_add_component_group.bold_title = opt.bold_title;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

bool eval_handle_cpack_add_component(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("cpack_add_component() missing name"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    Cpack_Component_Opts opt = {
        .origin = o,
        .command_name = node->as.cmd.name,
        .display_name = nob_sv_from_cstr(""),
        .description = nob_sv_from_cstr(""),
        .group = nob_sv_from_cstr(""),
        .depends = nob_sv_from_cstr(""),
        .install_types = nob_sv_from_cstr(""),
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

    Cmake_Event ev = {0};
    ev.kind = EV_CPACK_ADD_COMPONENT;
    ev.origin = o;
    ev.as.cpack_add_component.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_component.display_name = sv_copy_to_event_arena(ctx, opt.display_name);
    ev.as.cpack_add_component.description = sv_copy_to_event_arena(ctx, opt.description);
    ev.as.cpack_add_component.group = sv_copy_to_event_arena(ctx, opt.group);
    ev.as.cpack_add_component.depends = sv_copy_to_event_arena(ctx, opt.depends);
    ev.as.cpack_add_component.install_types = sv_copy_to_event_arena(ctx, opt.install_types);
    ev.as.cpack_add_component.required = opt.required;
    ev.as.cpack_add_component.hidden = opt.hidden;
    ev.as.cpack_add_component.disabled = opt.disabled;
    ev.as.cpack_add_component.downloaded = opt.downloaded;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}
