#include "eval_custom.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "eval_opt_parser.h"
#include "arena_dyn.h"

#include <string.h>

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool emit_target_prop_set(Evaluator_Context *ctx,
                                 Cmake_Event_Origin o,
                                 String_View target_name,
                                 String_View key,
                                 String_View value,
                                 Cmake_Target_Property_Op op) {
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_PROP_SET;
    ev.origin = o;
    ev.as.target_prop_set.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_prop_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.target_prop_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.target_prop_set.op = op;
    return emit_event(ctx, ev);
}

static String_View *sv_list_copy_to_event_arena(Evaluator_Context *ctx, const SV_List *list) {
    if (!ctx || !list || list->count == 0) return NULL;
    String_View *items = arena_alloc_array(eval_event_arena(ctx), String_View, list->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, items, NULL);
    for (size_t i = 0; i < list->count; i++) {
        items[i] = sv_copy_to_event_arena(ctx, list->items[i]);
    }
    return items;
}

enum {
    CUSTOM_TARGET_OPT_DEPENDS = 1,
    CUSTOM_TARGET_OPT_BYPRODUCTS,
    CUSTOM_TARGET_OPT_SOURCES,
    CUSTOM_TARGET_OPT_WORKING_DIRECTORY,
    CUSTOM_TARGET_OPT_COMMENT,
    CUSTOM_TARGET_OPT_VERBATIM,
    CUSTOM_TARGET_OPT_USES_TERMINAL,
    CUSTOM_TARGET_OPT_COMMAND_EXPAND_LISTS,
    CUSTOM_TARGET_OPT_COMMAND,
    CUSTOM_TARGET_OPT_JOB_POOL,
    CUSTOM_TARGET_OPT_JOB_SERVER_AWARE,
};

typedef struct {
    String_View working_dir;
    String_View comment;
    bool verbatim;
    bool uses_terminal;
    bool command_expand_lists;
    bool has_job_pool;
    String_View job_pool;
    bool has_job_server_aware;
    String_View job_server_aware;
    SV_List depends;
    SV_List byproducts;
    SV_List commands;
    SV_List sources;
} Add_Custom_Target_Opts;

static bool add_custom_target_on_option(Evaluator_Context *ctx,
                                        void *userdata,
                                        int id,
                                        SV_List values,
                                        size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Custom_Target_Opts *st = (Add_Custom_Target_Opts*)userdata;
    switch (id) {
    case CUSTOM_TARGET_OPT_DEPENDS:
        for (size_t i = 0; i < values.count; i++) {
            if (!svu_list_push_temp(ctx, &st->depends, values.items[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_BYPRODUCTS:
        for (size_t i = 0; i < values.count; i++) {
            if (!svu_list_push_temp(ctx, &st->byproducts, values.items[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_SOURCES:
        for (size_t i = 0; i < values.count; i++) {
            if (!svu_list_push_temp(ctx, &st->sources, values.items[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_WORKING_DIRECTORY:
        if (values.count > 0) st->working_dir = values.items[0];
        return true;
    case CUSTOM_TARGET_OPT_COMMENT:
        if (values.count > 0) st->comment = values.items[0];
        return true;
    case CUSTOM_TARGET_OPT_VERBATIM:
        st->verbatim = true;
        return true;
    case CUSTOM_TARGET_OPT_USES_TERMINAL:
        st->uses_terminal = true;
        return true;
    case CUSTOM_TARGET_OPT_COMMAND_EXPAND_LISTS:
        st->command_expand_lists = true;
        return true;
    case CUSTOM_TARGET_OPT_COMMAND: {
        size_t start = 0;
        if (values.count > 0 && eval_sv_eq_ci_lit(values.items[0], "ARGS")) start = 1;
        if (start < values.count) {
            String_View cmd = svu_join_space_temp(ctx, &values.items[start], values.count - start);
            if (!svu_list_push_temp(ctx, &st->commands, cmd)) return false;
        }
        return true;
    }
    case CUSTOM_TARGET_OPT_JOB_POOL:
        st->has_job_pool = true;
        if (values.count > 0) st->job_pool = values.items[0];
        return true;
    case CUSTOM_TARGET_OPT_JOB_SERVER_AWARE:
        st->has_job_server_aware = true;
        st->job_server_aware = (values.count > 0) ? values.items[0] : nob_sv_from_cstr("1");
        return true;
    default:
        return true;
    }
}

static bool add_custom_noop_positional(Evaluator_Context *ctx,
                                       void *userdata,
                                       String_View value,
                                       size_t token_index) {
    (void)ctx;
    (void)userdata;
    (void)value;
    (void)token_index;
    return true;
}

static bool apply_subdir_system_default_to_target(Evaluator_Context *ctx,
                                                  Cmake_Event_Origin o,
                                                  String_View target_name) {
    String_View raw = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"));
    if (raw.count == 0) return true;
    if (eval_sv_eq_ci_lit(raw, "0") || eval_sv_eq_ci_lit(raw, "FALSE") || eval_sv_eq_ci_lit(raw, "OFF")) {
        return true;
    }
    return emit_target_prop_set(ctx,
                                o,
                                target_name,
                                nob_sv_from_cstr("SYSTEM"),
                                nob_sv_from_cstr("1"),
                                EV_PROP_SET);
}

enum {
    CUSTOM_CMD_OPT_OUTPUT = 1,
    CUSTOM_CMD_OPT_PRE_BUILD,
    CUSTOM_CMD_OPT_PRE_LINK,
    CUSTOM_CMD_OPT_POST_BUILD,
    CUSTOM_CMD_OPT_COMMAND,
    CUSTOM_CMD_OPT_DEPENDS,
    CUSTOM_CMD_OPT_BYPRODUCTS,
    CUSTOM_CMD_OPT_MAIN_DEPENDENCY,
    CUSTOM_CMD_OPT_IMPLICIT_DEPENDS,
    CUSTOM_CMD_OPT_DEPFILE,
    CUSTOM_CMD_OPT_WORKING_DIRECTORY,
    CUSTOM_CMD_OPT_COMMENT,
    CUSTOM_CMD_OPT_APPEND,
    CUSTOM_CMD_OPT_VERBATIM,
    CUSTOM_CMD_OPT_USES_TERMINAL,
    CUSTOM_CMD_OPT_COMMAND_EXPAND_LISTS,
    CUSTOM_CMD_OPT_DEPENDS_EXPLICIT_ONLY,
    CUSTOM_CMD_OPT_CODEGEN,
    CUSTOM_CMD_OPT_JOB_POOL,
    CUSTOM_CMD_OPT_JOB_SERVER_AWARE,
};

typedef struct {
    bool pre_build;
    bool got_stage;
    bool append;
    bool verbatim;
    bool uses_terminal;
    bool command_expand_lists;
    bool depends_explicit_only;
    bool codegen;
    String_View working_dir;
    String_View comment;
    String_View main_dependency;
    String_View depfile;
    SV_List outputs;
    SV_List byproducts;
    SV_List depends;
    SV_List commands;
} Add_Custom_Command_Opts;

static bool add_custom_command_on_option(Evaluator_Context *ctx,
                                         void *userdata,
                                         int id,
                                         SV_List values,
                                         size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Custom_Command_Opts *st = (Add_Custom_Command_Opts*)userdata;
    switch (id) {
    case CUSTOM_CMD_OPT_OUTPUT:
        for (size_t i = 0; i < values.count; i++) {
            if (!svu_list_push_temp(ctx, &st->outputs, values.items[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_PRE_BUILD:
    case CUSTOM_CMD_OPT_PRE_LINK:
        st->got_stage = true;
        st->pre_build = true;
        return true;
    case CUSTOM_CMD_OPT_POST_BUILD:
        st->got_stage = true;
        st->pre_build = false;
        return true;
    case CUSTOM_CMD_OPT_COMMAND: {
        size_t start = 0;
        if (values.count > 0 && eval_sv_eq_ci_lit(values.items[0], "ARGS")) start = 1;
        if (start < values.count) {
            String_View cmd = svu_join_space_temp(ctx, &values.items[start], values.count - start);
            if (!svu_list_push_temp(ctx, &st->commands, cmd)) return false;
        }
        return true;
    }
    case CUSTOM_CMD_OPT_DEPENDS:
        for (size_t i = 0; i < values.count; i++) {
            if (!svu_list_push_temp(ctx, &st->depends, values.items[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_BYPRODUCTS:
        for (size_t i = 0; i < values.count; i++) {
            if (!svu_list_push_temp(ctx, &st->byproducts, values.items[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_MAIN_DEPENDENCY:
        if (values.count > 0) st->main_dependency = values.items[0];
        return true;
    case CUSTOM_CMD_OPT_IMPLICIT_DEPENDS:
        for (size_t i = 1; i < values.count; i += 2) {
            if (!svu_list_push_temp(ctx, &st->depends, values.items[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_DEPFILE:
        if (values.count > 0) st->depfile = values.items[0];
        return true;
    case CUSTOM_CMD_OPT_WORKING_DIRECTORY:
        if (values.count > 0) st->working_dir = values.items[0];
        return true;
    case CUSTOM_CMD_OPT_COMMENT:
        if (values.count > 0) st->comment = values.items[0];
        return true;
    case CUSTOM_CMD_OPT_APPEND:
        st->append = true;
        return true;
    case CUSTOM_CMD_OPT_VERBATIM:
        st->verbatim = true;
        return true;
    case CUSTOM_CMD_OPT_USES_TERMINAL:
        st->uses_terminal = true;
        return true;
    case CUSTOM_CMD_OPT_COMMAND_EXPAND_LISTS:
        st->command_expand_lists = true;
        return true;
    case CUSTOM_CMD_OPT_DEPENDS_EXPLICIT_ONLY:
        st->depends_explicit_only = true;
        return true;
    case CUSTOM_CMD_OPT_CODEGEN:
        st->codegen = true;
        return true;
    case CUSTOM_CMD_OPT_JOB_POOL:
    case CUSTOM_CMD_OPT_JOB_SERVER_AWARE:
        return true;
    default:
        return true;
    }
}

bool eval_handle_add_custom_target(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 1) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_custom_target() missing target name"),
                       nob_sv_from_cstr("Usage: add_custom_target(<name> [ALL] [COMMAND ...])"));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    bool all = false;
    size_t parse_start = 1;
    if (parse_start < a.count && eval_sv_eq_ci_lit(a.items[parse_start], "ALL")) {
        all = true;
        parse_start++;
    }

    static const Eval_Opt_Spec k_custom_target_specs[] = {
        {CUSTOM_TARGET_OPT_DEPENDS, "DEPENDS", EVAL_OPT_MULTI},
        {CUSTOM_TARGET_OPT_BYPRODUCTS, "BYPRODUCTS", EVAL_OPT_MULTI},
        {CUSTOM_TARGET_OPT_SOURCES, "SOURCES", EVAL_OPT_MULTI},
        {CUSTOM_TARGET_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE},
        {CUSTOM_TARGET_OPT_COMMENT, "COMMENT", EVAL_OPT_SINGLE},
        {CUSTOM_TARGET_OPT_VERBATIM, "VERBATIM", EVAL_OPT_FLAG},
        {CUSTOM_TARGET_OPT_USES_TERMINAL, "USES_TERMINAL", EVAL_OPT_FLAG},
        {CUSTOM_TARGET_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG},
        {CUSTOM_TARGET_OPT_JOB_POOL, "JOB_POOL", EVAL_OPT_SINGLE},
        {CUSTOM_TARGET_OPT_JOB_SERVER_AWARE, "JOB_SERVER_AWARE", EVAL_OPT_OPTIONAL_SINGLE},
        {CUSTOM_TARGET_OPT_COMMAND, "COMMAND", EVAL_OPT_MULTI},
    };
    Add_Custom_Target_Opts opt = {0};
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             a,
                             parse_start,
                             k_custom_target_specs,
                             NOB_ARRAY_LEN(k_custom_target_specs),
                             cfg,
                             add_custom_target_on_option,
                             add_custom_noop_positional,
                             &opt)) {
        return !eval_should_stop(ctx);
    }

    (void)eval_target_register(ctx, name);

    Cmake_Event decl = {0};
    decl.kind = EV_TARGET_DECLARE;
    decl.origin = o;
    decl.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    decl.as.target_declare.type = EV_TARGET_LIBRARY_UNKNOWN;
    if (!emit_event(ctx, decl)) return !eval_should_stop(ctx);
    if (!apply_subdir_system_default_to_target(ctx, o, name)) return !eval_should_stop(ctx);

    if (!emit_target_prop_set(ctx,
                              o,
                              name,
                              nob_sv_from_cstr("EXCLUDE_FROM_ALL"),
                              all ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"),
                              EV_PROP_SET)) {
        return !eval_should_stop(ctx);
    }
    if (opt.has_job_pool && opt.job_pool.count > 0) {
        if (!emit_target_prop_set(ctx,
                                  o,
                                  name,
                                  nob_sv_from_cstr("JOB_POOL"),
                                  opt.job_pool,
                                  EV_PROP_SET)) {
            return !eval_should_stop(ctx);
        }
    }
    if (opt.has_job_server_aware) {
        String_View aware = opt.job_server_aware.count > 0 ? opt.job_server_aware : nob_sv_from_cstr("1");
        if (!emit_target_prop_set(ctx,
                                  o,
                                  name,
                                  nob_sv_from_cstr("JOB_SERVER_AWARE"),
                                  aware,
                                  EV_PROP_SET)) {
            return !eval_should_stop(ctx);
        }
    }

    for (size_t s = 0; s < opt.sources.count; s++) {
        Cmake_Event src_ev = {0};
        src_ev.kind = EV_TARGET_ADD_SOURCE;
        src_ev.origin = o;
        src_ev.as.target_add_source.target_name = sv_copy_to_event_arena(ctx, name);
        src_ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, opt.sources.items[s]);
        if (!emit_event(ctx, src_ev)) return !eval_should_stop(ctx);
    }

    for (size_t d = 0; d < opt.depends.count; d++) {
        Cmake_Event dep_ev = {0};
        dep_ev.kind = EV_TARGET_LINK_LIBRARIES;
        dep_ev.origin = o;
        dep_ev.as.target_link_libraries.target_name = sv_copy_to_event_arena(ctx, name);
        dep_ev.as.target_link_libraries.visibility = EV_VISIBILITY_PRIVATE;
        dep_ev.as.target_link_libraries.item = sv_copy_to_event_arena(ctx, opt.depends.items[d]);
        if (!emit_event(ctx, dep_ev)) return !eval_should_stop(ctx);
    }

    if (opt.commands.count > 0 || opt.byproducts.count > 0) {
        Cmake_Event cmd_ev = {0};
        cmd_ev.kind = EV_CUSTOM_COMMAND_TARGET;
        cmd_ev.origin = o;
        cmd_ev.as.custom_command_target.target_name = sv_copy_to_event_arena(ctx, name);
        cmd_ev.as.custom_command_target.pre_build = true;
        cmd_ev.as.custom_command_target.commands = sv_list_copy_to_event_arena(ctx, &opt.commands);
        cmd_ev.as.custom_command_target.command_count = opt.commands.count;
        if (opt.commands.count > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, cmd_ev.as.custom_command_target.commands, !eval_should_stop(ctx));
        }
        cmd_ev.as.custom_command_target.working_dir = sv_copy_to_event_arena(ctx, opt.working_dir);
        cmd_ev.as.custom_command_target.comment = sv_copy_to_event_arena(ctx, opt.comment);
        cmd_ev.as.custom_command_target.outputs = sv_copy_to_event_arena(ctx, nob_sv_from_cstr(""));
        cmd_ev.as.custom_command_target.byproducts = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.byproducts.items, opt.byproducts.count));
        cmd_ev.as.custom_command_target.depends = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.depends.items, opt.depends.count));
        cmd_ev.as.custom_command_target.main_dependency = sv_copy_to_event_arena(ctx, nob_sv_from_cstr(""));
        cmd_ev.as.custom_command_target.depfile = sv_copy_to_event_arena(ctx, nob_sv_from_cstr(""));
        cmd_ev.as.custom_command_target.append = false;
        cmd_ev.as.custom_command_target.verbatim = opt.verbatim;
        cmd_ev.as.custom_command_target.uses_terminal = opt.uses_terminal;
        cmd_ev.as.custom_command_target.command_expand_lists = opt.command_expand_lists;
        cmd_ev.as.custom_command_target.depends_explicit_only = false;
        cmd_ev.as.custom_command_target.codegen = false;
        if (!emit_event(ctx, cmd_ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

bool eval_handle_add_custom_command(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_custom_command() requires TARGET or OUTPUT signature"),
                       nob_sv_from_cstr("Usage: add_custom_command(TARGET <tgt> ... ) or add_custom_command(OUTPUT <files...> ...)"));
        return !eval_should_stop(ctx);
    }

    bool mode_target = eval_sv_eq_ci_lit(a.items[0], "TARGET");
    bool mode_output = eval_sv_eq_ci_lit(a.items[0], "OUTPUT");
    if (!mode_target && !mode_output) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("Unsupported add_custom_command() signature"),
                       nob_sv_from_cstr("Use TARGET or OUTPUT signatures"));
        return !eval_should_stop(ctx);
    }

    String_View target_name = nob_sv_from_cstr("");
    size_t parse_start = 0;
    if (mode_target) {
        target_name = a.items[1];
        parse_start = 2;
    }

    static const Eval_Opt_Spec k_custom_command_specs[] = {
        {CUSTOM_CMD_OPT_OUTPUT, "OUTPUT", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_PRE_BUILD, "PRE_BUILD", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_PRE_LINK, "PRE_LINK", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_POST_BUILD, "POST_BUILD", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_COMMAND, "COMMAND", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_DEPENDS, "DEPENDS", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_BYPRODUCTS, "BYPRODUCTS", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_MAIN_DEPENDENCY, "MAIN_DEPENDENCY", EVAL_OPT_SINGLE},
        {CUSTOM_CMD_OPT_IMPLICIT_DEPENDS, "IMPLICIT_DEPENDS", EVAL_OPT_MULTI},
        {CUSTOM_CMD_OPT_DEPFILE, "DEPFILE", EVAL_OPT_SINGLE},
        {CUSTOM_CMD_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE},
        {CUSTOM_CMD_OPT_COMMENT, "COMMENT", EVAL_OPT_SINGLE},
        {CUSTOM_CMD_OPT_APPEND, "APPEND", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_VERBATIM, "VERBATIM", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_USES_TERMINAL, "USES_TERMINAL", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_DEPENDS_EXPLICIT_ONLY, "DEPENDS_EXPLICIT_ONLY", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_CODEGEN, "CODEGEN", EVAL_OPT_FLAG},
        {CUSTOM_CMD_OPT_JOB_POOL, "JOB_POOL", EVAL_OPT_OPTIONAL_SINGLE},
        {CUSTOM_CMD_OPT_JOB_SERVER_AWARE, "JOB_SERVER_AWARE", EVAL_OPT_OPTIONAL_SINGLE},
    };
    Add_Custom_Command_Opts opt = {0};
    opt.pre_build = true;
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             a,
                             parse_start,
                             k_custom_command_specs,
                             NOB_ARRAY_LEN(k_custom_command_specs),
                             cfg,
                             add_custom_command_on_option,
                             add_custom_noop_positional,
                             &opt)) {
        return !eval_should_stop(ctx);
    }

    if (mode_target && !opt.got_stage) opt.pre_build = true;

    if (opt.commands.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_custom_command() has no COMMAND entries"),
                       nob_sv_from_cstr("Command was ignored"));
        return !eval_should_stop(ctx);
    }
    if (mode_output && opt.outputs.count == 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_custom_command(OUTPUT ...) requires at least one output"),
                       nob_sv_from_cstr(""));
        return !eval_should_stop(ctx);
    }
    if (opt.main_dependency.count > 0) (void)svu_list_push_temp(ctx, &opt.depends, opt.main_dependency);
    if (opt.depfile.count > 0) (void)svu_list_push_temp(ctx, &opt.byproducts, opt.depfile);

    if (mode_target) {
        Cmake_Event ev = {0};
        ev.kind = EV_CUSTOM_COMMAND_TARGET;
        ev.origin = o;
        ev.as.custom_command_target.target_name = sv_copy_to_event_arena(ctx, target_name);
        ev.as.custom_command_target.pre_build = opt.pre_build;
        ev.as.custom_command_target.commands = sv_list_copy_to_event_arena(ctx, &opt.commands);
        ev.as.custom_command_target.command_count = opt.commands.count;
        if (opt.commands.count > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, ev.as.custom_command_target.commands, !eval_should_stop(ctx));
        }
        ev.as.custom_command_target.working_dir = sv_copy_to_event_arena(ctx, opt.working_dir);
        ev.as.custom_command_target.comment = sv_copy_to_event_arena(ctx, opt.comment);
        ev.as.custom_command_target.outputs = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.outputs.items, opt.outputs.count));
        ev.as.custom_command_target.byproducts = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.byproducts.items, opt.byproducts.count));
        ev.as.custom_command_target.depends = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.depends.items, opt.depends.count));
        ev.as.custom_command_target.main_dependency = sv_copy_to_event_arena(ctx, opt.main_dependency);
        ev.as.custom_command_target.depfile = sv_copy_to_event_arena(ctx, opt.depfile);
        ev.as.custom_command_target.append = opt.append;
        ev.as.custom_command_target.verbatim = opt.verbatim;
        ev.as.custom_command_target.uses_terminal = opt.uses_terminal;
        ev.as.custom_command_target.command_expand_lists = opt.command_expand_lists;
        ev.as.custom_command_target.depends_explicit_only = opt.depends_explicit_only;
        ev.as.custom_command_target.codegen = opt.codegen;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    } else {
        Cmake_Event ev = {0};
        ev.kind = EV_CUSTOM_COMMAND_OUTPUT;
        ev.origin = o;
        ev.as.custom_command_output.commands = sv_list_copy_to_event_arena(ctx, &opt.commands);
        ev.as.custom_command_output.command_count = opt.commands.count;
        if (opt.commands.count > 0) {
            EVAL_OOM_RETURN_IF_NULL(ctx, ev.as.custom_command_output.commands, !eval_should_stop(ctx));
        }
        ev.as.custom_command_output.working_dir = sv_copy_to_event_arena(ctx, opt.working_dir);
        ev.as.custom_command_output.comment = sv_copy_to_event_arena(ctx, opt.comment);
        ev.as.custom_command_output.outputs = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.outputs.items, opt.outputs.count));
        ev.as.custom_command_output.byproducts = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.byproducts.items, opt.byproducts.count));
        ev.as.custom_command_output.depends = sv_copy_to_event_arena(ctx, eval_sv_join_semi_temp(ctx, opt.depends.items, opt.depends.count));
        ev.as.custom_command_output.main_dependency = sv_copy_to_event_arena(ctx, opt.main_dependency);
        ev.as.custom_command_output.depfile = sv_copy_to_event_arena(ctx, opt.depfile);
        ev.as.custom_command_output.append = opt.append;
        ev.as.custom_command_output.verbatim = opt.verbatim;
        ev.as.custom_command_output.uses_terminal = opt.uses_terminal;
        ev.as.custom_command_output.command_expand_lists = opt.command_expand_lists;
        ev.as.custom_command_output.depends_explicit_only = opt.depends_explicit_only;
        ev.as.custom_command_output.codegen = opt.codegen;
        if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    }

    return !eval_should_stop(ctx);
}

