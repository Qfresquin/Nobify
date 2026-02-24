#include "eval_test.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "eval_opt_parser.h"

#include <string.h>

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

bool eval_handle_enable_testing(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count > 0) {
        eval_emit_diag(ctx,
                       EV_DIAG_WARNING,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("enable_testing() does not expect arguments"),
                       nob_sv_from_cstr("Extra arguments are ignored"));
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TESTING_ENABLE;
    ev.origin = o;
    ev.as.testing_enable.enabled = true;
    (void)eval_var_set(ctx, nob_sv_from_cstr("BUILD_TESTING"), nob_sv_from_cstr("1"));
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

enum {
    ADD_TEST_OPT_WORKING_DIRECTORY = 1,
    ADD_TEST_OPT_COMMAND_EXPAND_LISTS,
};

typedef struct {
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View working_dir;
    bool command_expand_lists;
} Add_Test_Option_State;

static bool add_test_on_option(Evaluator_Context *ctx,
                               void *userdata,
                               int id,
                               SV_List values,
                               size_t token_index) {
    (void)token_index;
    (void)ctx;
    if (!userdata) return false;
    Add_Test_Option_State *st = (Add_Test_Option_State*)userdata;
    if (id == ADD_TEST_OPT_WORKING_DIRECTORY) {
        if (values.count > 0) st->working_dir = values.items[0];
        return true;
    }
    if (id == ADD_TEST_OPT_COMMAND_EXPAND_LISTS) {
        st->command_expand_lists = true;
        return true;
    }
    return true;
}

static bool add_test_on_positional(Evaluator_Context *ctx,
                                   void *userdata,
                                   String_View value,
                                   size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Add_Test_Option_State *st = (Add_Test_Option_State*)userdata;
    eval_emit_diag(ctx,
                   EV_DIAG_WARNING,
                   nob_sv_from_cstr("dispatcher"),
                   st->command_name,
                   st->origin,
                   nob_sv_from_cstr("add_test() has unsupported/extra argument"),
                   value);
    return !eval_should_stop(ctx);
}

bool eval_handle_add_test(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("add_test() requires at least test name and command"),
                       nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>) or add_test(<name> <cmd...>)"));
        return !eval_should_stop(ctx);
    }

    String_View name = nob_sv_from_cstr("");
    String_View command = nob_sv_from_cstr("");
    String_View working_dir = nob_sv_from_cstr("");
    bool command_expand_lists = false;

    if (eval_sv_eq_ci_lit(a.items[0], "NAME")) {
        if (a.count < 4) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) requires COMMAND clause"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>)"));
            return !eval_should_stop(ctx);
        }
        name = a.items[1];

        size_t cmd_i = 2;
        if (!eval_sv_eq_ci_lit(a.items[cmd_i], "COMMAND")) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) missing COMMAND"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>)"));
            return !eval_should_stop(ctx);
        }
        cmd_i++;
        size_t cmd_start = cmd_i;
        const Eval_Opt_Spec add_test_specs[] = {
            {ADD_TEST_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE},
            {ADD_TEST_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG},
        };
        size_t cmd_end = cmd_i;
        while (cmd_end < a.count &&
               !eval_opt_token_is_keyword(a.items[cmd_end], add_test_specs, NOB_ARRAY_LEN(add_test_specs))) {
            cmd_end++;
        }
        if (cmd_end <= cmd_start) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("add_test(NAME ...) has empty COMMAND"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }
        command = svu_join_space_temp(ctx, &a.items[cmd_start], cmd_end - cmd_start);

        Add_Test_Option_State st = {
            .origin = o,
            .command_name = node->as.cmd.name,
            .working_dir = working_dir,
            .command_expand_lists = command_expand_lists,
        };
        Eval_Opt_Parse_Config cfg = {
            .origin = o,
            .component = nob_sv_from_cstr("dispatcher"),
            .command = node->as.cmd.name,
            .unknown_as_positional = true,
            .warn_unknown = false,
        };
        if (!eval_opt_parse_walk(ctx,
                                 a,
                                 cmd_end,
                                 add_test_specs,
                                 NOB_ARRAY_LEN(add_test_specs),
                                 cfg,
                                 add_test_on_option,
                                 add_test_on_positional,
                                 &st)) {
            return !eval_should_stop(ctx);
        }
        working_dir = st.working_dir;
        command_expand_lists = st.command_expand_lists;
    } else {
        name = a.items[0];
        command = svu_join_space_temp(ctx, &a.items[1], a.count - 1);
    }

    Cmake_Event ev = {0};
    ev.kind = EV_TEST_ADD;
    ev.origin = o;
    ev.as.test_add.name = sv_copy_to_event_arena(ctx, name);
    ev.as.test_add.command = sv_copy_to_event_arena(ctx, command);
    ev.as.test_add.working_dir = sv_copy_to_event_arena(ctx, working_dir);
    ev.as.test_add.command_expand_lists = command_expand_lists;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

