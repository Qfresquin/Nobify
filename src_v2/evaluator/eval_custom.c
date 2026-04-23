#include "eval_custom.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "eval_opt_parser.h"
#include "arena_dyn.h"

#include <string.h>

static bool emit_target_prop_set(EvalExecContext *ctx,
                                 Cmake_Event_Origin o,
                                 String_View target_name,
                                 String_View key,
                                 String_View value,
                                 Cmake_Target_Property_Op op) {
    return eval_emit_target_prop_set(ctx, o, target_name, key, value, op);
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
    SV_List argv;
} Eval_Custom_Command_Record;

typedef Eval_Custom_Command_Record *Eval_Custom_Command_Record_List;

static bool custom_command_record_push(EvalExecContext *ctx,
                                       Eval_Custom_Command_Record_List *commands,
                                       SV_List values) {
    Eval_Custom_Command_Record record = {0};
    size_t start = 0;
    if (!ctx || !commands) return false;
    if (arena_arr_len(values) > 0 && eval_sv_eq_ci_lit(values[0], "ARGS")) start = 1;
    if (start >= arena_arr_len(values)) return true;
    for (size_t i = start; i < arena_arr_len(values); ++i) {
        if (!svu_list_push_temp(ctx, &record.argv, values[i])) return false;
    }
    return arena_arr_push(eval_temp_arena(ctx), *commands, record);
}

static bool custom_expand_command_argv(EvalExecContext *ctx,
                                       const Eval_Custom_Command_Record *record,
                                       bool command_expand_lists,
                                       SV_List *out) {
    if (!ctx || !record || !out) return false;
    *out = NULL;
    for (size_t i = 0; i < arena_arr_len(record->argv); ++i) {
        if (!command_expand_lists) {
            if (!svu_list_push_temp(ctx, out, record->argv[i])) return false;
            continue;
        }

        SV_List expanded = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), record->argv[i], &expanded)) return false;
        if (arena_arr_len(expanded) == 0) {
            if (!svu_list_push_temp(ctx, out, record->argv[i])) return false;
            continue;
        }
        for (size_t j = 0; j < arena_arr_len(expanded); ++j) {
            if (!svu_list_push_temp(ctx, out, expanded[j])) return false;
        }
    }
    return true;
}

static bool custom_emit_generated_mark(EvalExecContext *ctx,
                                       Cmake_Event_Origin origin,
                                       String_View raw_path) {
    String_View directory_source_dir = {0};
    String_View directory_binary_dir = {0};
    String_View resolved_path = {0};
    if (!ctx) return false;
    directory_source_dir = eval_current_source_dir_for_paths(ctx);
    directory_binary_dir = eval_current_binary_dir(ctx);
    resolved_path = eval_path_resolve_for_cmake_arg(ctx, raw_path, directory_binary_dir, true);
    if (eval_should_stop(ctx)) return false;
    return eval_emit_source_mark_generated(ctx,
                                           origin,
                                           resolved_path,
                                           directory_source_dir,
                                           directory_binary_dir,
                                           true);
}

static bool custom_command_effective_first_output(EvalExecContext *ctx,
                                                  const SV_List *outputs,
                                                  String_View *out) {
    String_View directory_binary_dir = {0};
    if (out) *out = nob_sv_from_cstr("");
    if (!ctx || !outputs || !out || arena_arr_len(*outputs) == 0) return false;
    directory_binary_dir = eval_current_binary_dir(ctx);
    *out = eval_path_resolve_for_cmake_arg(ctx, (*outputs)[0], directory_binary_dir, true);
    return !eval_should_stop(ctx);
}

static const Eval_Custom_Command_Output_Step *custom_command_find_output_step(EvalExecContext *ctx,
                                                                              String_View effective_output) {
    if (!ctx || effective_output.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->file_state.custom_command_output_steps); ++i) {
        const Eval_Custom_Command_Output_Step *record = &ctx->file_state.custom_command_output_steps[i];
        if (nob_sv_eq(record->effective_first_output, effective_output)) return record;
    }
    return NULL;
}

static bool custom_command_register_output_step(EvalExecContext *ctx,
                                                String_View effective_output,
                                                String_View step_key) {
    Eval_Custom_Command_Output_Step record = {0};
    if (!ctx || effective_output.count == 0 || step_key.count == 0) return false;
    if (custom_command_find_output_step(ctx, effective_output)) return true;
    record.effective_first_output = sv_copy_to_event_arena(ctx, effective_output);
    record.step_key = sv_copy_to_event_arena(ctx, step_key);
    if (eval_should_stop(ctx)) return false;
    return arena_arr_push(eval_event_arena(ctx), ctx->file_state.custom_command_output_steps, record);
}

static bool custom_emit_build_step_payload(EvalExecContext *ctx,
                                           Cmake_Event_Origin origin,
                                           String_View step_key,
                                           bool command_expand_lists,
                                           const SV_List *depends,
                                           const Eval_Custom_Command_Record_List *commands) {
    if (!ctx) return false;
    if (depends) {
        for (size_t i = 0; i < arena_arr_len(*depends); ++i) {
            if (!eval_emit_build_step_add_dependency(ctx, origin, step_key, (*depends)[i])) return false;
        }
    }
    if (commands) {
        for (size_t i = 0; i < arena_arr_len(*commands); ++i) {
            SV_List argv = NULL;
            if (!custom_expand_command_argv(ctx, &(*commands)[i], command_expand_lists, &argv) ||
                !eval_emit_build_step_add_command(ctx, origin, step_key, (uint32_t)i, &argv)) {
                return false;
            }
        }
    }
    return true;
}

static bool custom_emit_build_step(EvalExecContext *ctx,
                                   Cmake_Event_Origin origin,
                                   String_View step_key,
                                   Event_Build_Step_Kind step_kind,
                                   String_View owner_target_name,
                                   bool append,
                                   bool verbatim,
                                   bool uses_terminal,
                                   bool command_expand_lists,
                                   bool depends_explicit_only,
                                   bool codegen,
                                   String_View working_dir,
                                   String_View comment,
                                   String_View main_dependency,
                                   String_View depfile,
                                   String_View job_pool,
                                   String_View job_server_aware,
                                   const SV_List *outputs,
                                   const SV_List *byproducts,
                                   const SV_List *depends,
                                   const Eval_Custom_Command_Record_List *commands) {
    if (!ctx) return false;
    if (!eval_emit_build_step_declare(ctx,
                                      origin,
                                      step_key,
                                      step_kind,
                                      owner_target_name,
                                      append,
                                      verbatim,
                                      uses_terminal,
                                      command_expand_lists,
                                      depends_explicit_only,
                                      codegen,
                                      working_dir,
                                      comment,
                                      main_dependency,
                                      depfile,
                                      job_pool,
                                      job_server_aware)) {
        return false;
    }

    if (outputs) {
        for (size_t i = 0; i < arena_arr_len(*outputs); ++i) {
            if (!eval_emit_build_step_add_output(ctx, origin, step_key, (*outputs)[i]) ||
                !custom_emit_generated_mark(ctx, origin, (*outputs)[i])) {
                return false;
            }
        }
    }
    if (byproducts) {
        for (size_t i = 0; i < arena_arr_len(*byproducts); ++i) {
            if (!eval_emit_build_step_add_byproduct(ctx, origin, step_key, (*byproducts)[i]) ||
                !custom_emit_generated_mark(ctx, origin, (*byproducts)[i])) {
                return false;
            }
        }
    }
    if (depends) {
        for (size_t i = 0; i < arena_arr_len(*depends); ++i) {
            if (!eval_emit_build_step_add_dependency(ctx, origin, step_key, (*depends)[i])) return false;
        }
    }
    return custom_emit_build_step_payload(ctx, origin, step_key, command_expand_lists, NULL, commands);
}

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
    Eval_Custom_Command_Record_List commands;
    SV_List sources;
} Add_Custom_Target_Opts;

static bool add_custom_target_on_option(EvalExecContext *ctx,
                                        void *userdata,
                                        int id,
                                        SV_List values,
                                        size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Custom_Target_Opts *st = (Add_Custom_Target_Opts*)userdata;
    switch (id) {
    case CUSTOM_TARGET_OPT_DEPENDS:
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->depends, values[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_BYPRODUCTS:
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->byproducts, values[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_SOURCES:
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->sources, values[i])) return false;
        }
        return true;
    case CUSTOM_TARGET_OPT_WORKING_DIRECTORY:
        if (arena_arr_len(values) > 0) st->working_dir = values[0];
        return true;
    case CUSTOM_TARGET_OPT_COMMENT:
        if (arena_arr_len(values) > 0) st->comment = values[0];
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
        return custom_command_record_push(ctx, &st->commands, values);
    }
    case CUSTOM_TARGET_OPT_JOB_POOL:
        st->has_job_pool = true;
        if (arena_arr_len(values) > 0) st->job_pool = values[0];
        return true;
    case CUSTOM_TARGET_OPT_JOB_SERVER_AWARE:
        st->has_job_server_aware = true;
        st->job_server_aware = (arena_arr_len(values) > 0) ? values[0] : nob_sv_from_cstr("1");
        return true;
    default:
        return true;
    }
}

static bool add_custom_noop_positional(EvalExecContext *ctx,
                                       void *userdata,
                                       String_View value,
                                       size_t token_index) {
    (void)ctx;
    (void)userdata;
    (void)value;
    (void)token_index;
    return true;
}

static bool apply_subdir_system_default_to_target(EvalExecContext *ctx,
                                                  Cmake_Event_Origin o,
                                                  String_View target_name) {
    String_View raw = eval_var_get_visible(ctx, nob_sv_from_cstr("NOBIFY_SUBDIR_SYSTEM_DEFAULT"));
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
    String_View command_name;
    Cmake_Event_Origin origin;
    Event_Build_Step_Kind step_kind;
    bool got_stage;
    size_t stage_count;
    bool append;
    bool verbatim;
    bool uses_terminal;
    bool command_expand_lists;
    bool depends_explicit_only;
    bool codegen;
    bool has_implicit_depends;
    bool has_depfile;
    bool has_job_pool;
    String_View job_pool;
    bool has_job_server_aware;
    String_View job_server_aware;
    String_View working_dir;
    String_View comment;
    String_View main_dependency;
    String_View depfile;
    SV_List outputs;
    SV_List byproducts;
    SV_List depends;
    Eval_Custom_Command_Record_List commands;
} Add_Custom_Command_Opts;

typedef struct {
    String_View component;
    String_View command;
    Cmake_Event_Origin origin;
    String_View usage_hint;
} Add_Custom_Positional_Context;

typedef struct {
    Add_Custom_Command_Opts *opt;
    Add_Custom_Positional_Context positional;
    bool had_positional_error;
} Add_Custom_Command_Parse_Context;

static bool add_custom_command_on_option(EvalExecContext *ctx,
                                         void *userdata,
                                         int id,
                                         SV_List values,
                                         size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Custom_Command_Opts *st = (Add_Custom_Command_Opts*)userdata;
    switch (id) {
    case CUSTOM_CMD_OPT_OUTPUT:
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->outputs, values[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_PRE_BUILD:
        st->got_stage = true;
        st->step_kind = EVENT_BUILD_STEP_TARGET_PRE_BUILD;
        st->stage_count++;
        return true;
    case CUSTOM_CMD_OPT_PRE_LINK:
        st->got_stage = true;
        st->step_kind = EVENT_BUILD_STEP_TARGET_PRE_LINK;
        st->stage_count++;
        return true;
    case CUSTOM_CMD_OPT_POST_BUILD:
        st->got_stage = true;
        st->step_kind = EVENT_BUILD_STEP_TARGET_POST_BUILD;
        st->stage_count++;
        return true;
    case CUSTOM_CMD_OPT_COMMAND: {
        return custom_command_record_push(ctx, &st->commands, values);
    }
    case CUSTOM_CMD_OPT_DEPENDS:
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->depends, values[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_BYPRODUCTS:
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (!svu_list_push_temp(ctx, &st->byproducts, values[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_MAIN_DEPENDENCY:
        if (arena_arr_len(values) > 0) st->main_dependency = values[0];
        return true;
    case CUSTOM_CMD_OPT_IMPLICIT_DEPENDS:
        if (arena_arr_len(values) == 0 || (arena_arr_len(values) % 2) != 0) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), st->command_name, st->origin, nob_sv_from_cstr("IMPLICIT_DEPENDS requires language/file pairs"), nob_sv_from_cstr("Usage: IMPLICIT_DEPENDS <lang> <file> [<lang> <file> ...]"));
            return false;
        }
        st->has_implicit_depends = true;
        for (size_t i = 0; i < arena_arr_len(values); i += 2) {
            if (!eval_sv_eq_ci_lit(values[i], "C") &&
                !eval_sv_eq_ci_lit(values[i], "CXX")) {
                EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("dispatcher"), st->command_name, st->origin, nob_sv_from_cstr("Unsupported IMPLICIT_DEPENDS language"), values[i]);
                return false;
            }
        }
        for (size_t i = 1; i < arena_arr_len(values); i += 2) {
            if (!svu_list_push_temp(ctx, &st->depends, values[i])) return false;
        }
        return true;
    case CUSTOM_CMD_OPT_DEPFILE:
        if (arena_arr_len(values) > 0) st->depfile = values[0];
        st->has_depfile = true;
        return true;
    case CUSTOM_CMD_OPT_WORKING_DIRECTORY:
        if (arena_arr_len(values) > 0) st->working_dir = values[0];
        return true;
    case CUSTOM_CMD_OPT_COMMENT:
        if (arena_arr_len(values) > 0) st->comment = values[0];
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
        st->has_job_pool = true;
        if (arena_arr_len(values) > 0) st->job_pool = values[0];
        return true;
    case CUSTOM_CMD_OPT_JOB_SERVER_AWARE:
        st->has_job_server_aware = true;
        st->job_server_aware = (arena_arr_len(values) > 0) ? values[0] : nob_sv_from_cstr("1");
        return true;
    default:
        return true;
    }
}

static bool add_custom_command_on_option_parse_ctx(EvalExecContext *ctx,
                                                   void *userdata,
                                                   int id,
                                                   SV_List values,
                                                   size_t token_index) {
    if (!ctx || !userdata) return false;
    Add_Custom_Command_Parse_Context *st = (Add_Custom_Command_Parse_Context*)userdata;
    if (!st->opt) return false;
    return add_custom_command_on_option(ctx, st->opt, id, values, token_index);
}

static bool add_custom_error_positional_parse_ctx(EvalExecContext *ctx,
                                                  void *userdata,
                                                  String_View value,
                                                  size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Custom_Command_Parse_Context *st = (Add_Custom_Command_Parse_Context*)userdata;
    EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, st->positional.component, st->positional.command, st->positional.origin, nob_sv_from_cstr("Unexpected argument in add_custom_command()"), value);
    st->had_positional_error = true;
    return false;
}

Eval_Result eval_handle_add_custom_target(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_custom_target() missing target name"), nob_sv_from_cstr("Usage: add_custom_target(<name> [ALL] [COMMAND ...])"));
        return eval_result_from_ctx(ctx);
    }

    String_View name = a[0];
    bool all = false;
    size_t parse_start = 1;
    if (parse_start < arena_arr_len(a) && eval_sv_eq_ci_lit(a[parse_start], "ALL")) {
        all = true;
        parse_start++;
    }

    static const Eval_Opt_Spec k_custom_target_specs[] = {
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_DEPENDS, "DEPENDS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_BYPRODUCTS, "BYPRODUCTS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_SOURCES, "SOURCES", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_COMMENT, "COMMENT", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_VERBATIM, "VERBATIM", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_USES_TERMINAL, "USES_TERMINAL", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_JOB_POOL, "JOB_POOL", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_JOB_SERVER_AWARE, "JOB_SERVER_AWARE", EVAL_OPT_OPTIONAL_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_TARGET_OPT_COMMAND, "COMMAND", EVAL_OPT_MULTI),
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
        return eval_result_from_ctx(ctx);
    }

    (void)eval_target_register(ctx, name);

    if (!eval_emit_target_declare(ctx,
                                  o,
                                  name,
                                  EV_TARGET_LIBRARY_UNKNOWN,
                                  false,
                                  false,
                                  nob_sv_from_cstr(""))) {
        return eval_result_fatal();
    }

    if (!eval_target_apply_defined_initializers(ctx, o, name)) return eval_result_from_ctx(ctx);
    if (!apply_subdir_system_default_to_target(ctx, o, name)) return eval_result_from_ctx(ctx);

    if (!emit_target_prop_set(ctx,
                              o,
                              name,
                              nob_sv_from_cstr("EXCLUDE_FROM_ALL"),
                              all ? nob_sv_from_cstr("0") : nob_sv_from_cstr("1"),
                              EV_PROP_SET)) {
        return eval_result_from_ctx(ctx);
    }
    if (opt.has_job_pool && opt.job_pool.count > 0) {
        if (!emit_target_prop_set(ctx,
                                  o,
                                  name,
                                  nob_sv_from_cstr("JOB_POOL"),
                                  opt.job_pool,
                                  EV_PROP_SET)) {
            return eval_result_from_ctx(ctx);
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
            return eval_result_from_ctx(ctx);
        }
    }
    {
        String_View step_key = eval_alloc_build_step_key(ctx);
        String_View job_pool = opt.has_job_pool ? opt.job_pool : nob_sv_from_cstr("");
        String_View job_server_aware = opt.has_job_server_aware ? opt.job_server_aware : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (!custom_emit_build_step(ctx,
                                    o,
                                    step_key,
                                    EVENT_BUILD_STEP_CUSTOM_TARGET,
                                    name,
                                    false,
                                    opt.verbatim,
                                    opt.uses_terminal,
                                    opt.command_expand_lists,
                                    false,
                                    false,
                                    opt.working_dir,
                                    opt.comment,
                                    nob_sv_from_cstr(""),
                                    nob_sv_from_cstr(""),
                                    job_pool,
                                    job_server_aware,
                                    NULL,
                                    &opt.byproducts,
                                    &opt.depends,
                                    &opt.commands)) {
            return eval_result_fatal();
        }
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_add_custom_command(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_custom_command() requires TARGET or OUTPUT signature"), nob_sv_from_cstr("Usage: add_custom_command(TARGET <tgt> ... ) or add_custom_command(OUTPUT <files...> ...)"));
        return eval_result_from_ctx(ctx);
    }

    bool mode_target = eval_sv_eq_ci_lit(a[0], "TARGET");
    bool mode_output = eval_sv_eq_ci_lit(a[0], "OUTPUT");
    if (!mode_target && !mode_output) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "dispatcher", nob_sv_from_cstr("Unsupported add_custom_command() signature"), nob_sv_from_cstr("Use TARGET or OUTPUT signatures"));
        return eval_result_from_ctx(ctx);
    }

    String_View target_name = nob_sv_from_cstr("");
    size_t parse_start = 0;
    if (mode_target) {
        if (arena_arr_len(a) < 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_custom_command(TARGET ...) requires target name and stage"), nob_sv_from_cstr("Usage: add_custom_command(TARGET <target> PRE_BUILD|PRE_LINK|POST_BUILD COMMAND <cmd> ...)"));
            return eval_result_from_ctx(ctx);
        }
        target_name = a[1];
        if (!eval_target_known(ctx, target_name)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("add_custom_command(TARGET ...) target was not declared"), target_name);
            return eval_result_from_ctx(ctx);
        }
        if (eval_target_alias_known(ctx, target_name)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_custom_command(TARGET ...) cannot be used on ALIAS targets"), target_name);
            return eval_result_from_ctx(ctx);
        }
        parse_start = 2;
    }

    static const Eval_Opt_Spec k_custom_command_output_specs[] = {
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_OUTPUT, "OUTPUT", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_COMMAND, "COMMAND", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_DEPENDS, "DEPENDS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_BYPRODUCTS, "BYPRODUCTS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_MAIN_DEPENDENCY, "MAIN_DEPENDENCY", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_IMPLICIT_DEPENDS, "IMPLICIT_DEPENDS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_DEPFILE, "DEPFILE", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_COMMENT, "COMMENT", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_APPEND, "APPEND", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_VERBATIM, "VERBATIM", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_USES_TERMINAL, "USES_TERMINAL", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_DEPENDS_EXPLICIT_ONLY, "DEPENDS_EXPLICIT_ONLY", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_CODEGEN, "CODEGEN", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_JOB_POOL, "JOB_POOL", EVAL_OPT_OPTIONAL_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_JOB_SERVER_AWARE, "JOB_SERVER_AWARE", EVAL_OPT_OPTIONAL_SINGLE),
    };
    static const Eval_Opt_Spec k_custom_command_target_specs[] = {
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_PRE_BUILD, "PRE_BUILD", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_PRE_LINK, "PRE_LINK", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_POST_BUILD, "POST_BUILD", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_COMMAND, "COMMAND", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_BYPRODUCTS, "BYPRODUCTS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_COMMENT, "COMMENT", EVAL_OPT_SINGLE),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_VERBATIM, "VERBATIM", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_USES_TERMINAL, "USES_TERMINAL", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(CUSTOM_CMD_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG),
    };
    Add_Custom_Command_Opts opt = {0};
    opt.command_name = node->as.cmd.name;
    opt.origin = o;
    opt.step_kind = EVENT_BUILD_STEP_TARGET_PRE_BUILD;
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    Add_Custom_Command_Parse_Context parse_ctx = {0};
    parse_ctx.opt = &opt;
    parse_ctx.positional.component = nob_sv_from_cstr("dispatcher");
    parse_ctx.positional.command = node->as.cmd.name;
    parse_ctx.positional.origin = o;
    parse_ctx.positional.usage_hint = nob_sv_from_cstr("");
    const Eval_Opt_Spec *specs = mode_target ? k_custom_command_target_specs : k_custom_command_output_specs;
    size_t spec_count = mode_target ? NOB_ARRAY_LEN(k_custom_command_target_specs) : NOB_ARRAY_LEN(k_custom_command_output_specs);
    if (!eval_opt_parse_walk(ctx,
                             a,
                             parse_start,
                             specs,
                             spec_count,
                             cfg,
                             add_custom_command_on_option_parse_ctx,
                             add_custom_error_positional_parse_ctx,
                             &parse_ctx)) {
        return eval_result_from_ctx(ctx);
    }
    if (parse_ctx.had_positional_error) return eval_result_from_ctx(ctx);

    if (mode_target) {
        if (!opt.got_stage) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_custom_command(TARGET ...) requires PRE_BUILD, PRE_LINK or POST_BUILD"), nob_sv_from_cstr("Specify one build stage keyword"));
            return eval_result_from_ctx(ctx);
        }
        if (opt.stage_count > 1) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_custom_command(TARGET ...) accepts exactly one build stage"), nob_sv_from_cstr("Use one of PRE_BUILD, PRE_LINK, POST_BUILD"));
            return eval_result_from_ctx(ctx);
        }
    } else if (opt.got_stage) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNEXPECTED_ARGUMENT, "dispatcher", nob_sv_from_cstr("add_custom_command(OUTPUT ...) does not accept build stage keywords"), nob_sv_from_cstr("PRE_BUILD/PRE_LINK/POST_BUILD are valid only for TARGET signature"));
        return eval_result_from_ctx(ctx);
    }

    if (opt.has_implicit_depends && opt.has_depfile) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_CONFLICTING_OPTIONS, "dispatcher", nob_sv_from_cstr("add_custom_command(OUTPUT ...) cannot combine DEPFILE with IMPLICIT_DEPENDS"), nob_sv_from_cstr("Use either DEPFILE or IMPLICIT_DEPENDS"));
        return eval_result_from_ctx(ctx);
    }
    if (opt.has_job_pool && opt.uses_terminal) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_CONFLICTING_OPTIONS, "dispatcher", nob_sv_from_cstr("add_custom_command(OUTPUT ...) JOB_POOL is incompatible with USES_TERMINAL"), nob_sv_from_cstr("Remove one of JOB_POOL or USES_TERMINAL"));
        return eval_result_from_ctx(ctx);
    }

    if (arena_arr_len(opt.commands) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_custom_command() has no COMMAND entries"), nob_sv_from_cstr("Provide at least one COMMAND"));
        return eval_result_from_ctx(ctx);
    }
    if (mode_output && arena_arr_len(opt.outputs) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_custom_command(OUTPUT ...) requires at least one output"), nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }
    if (opt.main_dependency.count > 0) (void)svu_list_push_temp(ctx, &opt.depends, opt.main_dependency);

    if (mode_output && opt.append) {
        String_View effective_first_output = {0};
        const Eval_Custom_Command_Output_Step *base_step = NULL;
        if (arena_arr_len(opt.byproducts) > 0 ||
            opt.has_depfile ||
            opt.has_job_pool ||
            opt.has_job_server_aware ||
            opt.uses_terminal ||
            opt.command_expand_lists ||
            opt.depends_explicit_only ||
            opt.codegen) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           o,
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_UNSUPPORTED_OPERATION,
                                           "dispatcher",
                                           nob_sv_from_cstr("add_custom_command(OUTPUT ... APPEND) uses options that cannot be appended"),
                                           nob_sv_from_cstr("APPEND supports additional COMMAND and DEPENDS entries for an existing first output only"));
            return eval_result_from_ctx(ctx);
        }
        if (!custom_command_effective_first_output(ctx, &opt.outputs, &effective_first_output)) {
            return eval_result_from_ctx(ctx);
        }
        base_step = custom_command_find_output_step(ctx, effective_first_output);
        if (!base_step) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           o,
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_NOT_FOUND,
                                           "dispatcher",
                                           nob_sv_from_cstr("add_custom_command(OUTPUT ... APPEND) could not find an existing output rule"),
                                           effective_first_output);
            return eval_result_from_ctx(ctx);
        }
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        opt.comment = nob_sv_from_cstr("");
        opt.working_dir = nob_sv_from_cstr("");
        opt.main_dependency = nob_sv_from_cstr("");
        if (!custom_emit_build_step_payload(ctx,
                                            o,
                                            base_step->step_key,
                                            false,
                                            &opt.depends,
                                            &opt.commands)) {
            return eval_result_fatal();
        }
        return eval_result_from_ctx(ctx);
    }

    {
        String_View step_key = eval_alloc_build_step_key(ctx);
        String_View effective_first_output = {0};
        String_View job_pool = opt.has_job_pool ? opt.job_pool : nob_sv_from_cstr("");
        String_View job_server_aware = opt.has_job_server_aware ? opt.job_server_aware : nob_sv_from_cstr("");
        Event_Build_Step_Kind step_kind =
            mode_target ? opt.step_kind : EVENT_BUILD_STEP_OUTPUT_RULE;
        String_View owner_target_name = mode_target ? target_name : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (!custom_emit_build_step(ctx,
                                    o,
                                    step_key,
                                    step_kind,
                                    owner_target_name,
                                    opt.append,
                                    opt.verbatim,
                                    opt.uses_terminal,
                                    opt.command_expand_lists,
                                    opt.depends_explicit_only,
                                    opt.codegen,
                                    opt.working_dir,
                                    opt.comment,
                                    opt.main_dependency,
                                    opt.depfile,
                                    job_pool,
                                    job_server_aware,
                                    mode_output ? &opt.outputs : NULL,
                                    &opt.byproducts,
                                    &opt.depends,
                                    &opt.commands)) {
            return eval_result_fatal();
        }
        if (mode_output &&
            !custom_command_effective_first_output(ctx, &opt.outputs, &effective_first_output)) {
            return eval_result_from_ctx(ctx);
        }
        if (mode_output &&
            !custom_command_register_output_step(ctx, effective_first_output, step_key)) {
            return eval_result_fatal();
        }
    }
    return eval_result_from_ctx(ctx);
}
