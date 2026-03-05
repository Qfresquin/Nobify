#include "eval_test.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "eval_opt_parser.h"

#include <stdio.h>
#include <string.h>

static String_View test_current_bin_dir(Evaluator_Context *ctx) {
    return eval_current_binary_dir(ctx);
}

static String_View test_source_stem_temp(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return nob_sv_from_cstr("");
    size_t start = 0;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') start = i + 1;
    }
    size_t end = path.count;
    for (size_t i = path.count; i > start; i--) {
        if (path.data[i - 1] == '.') {
            end = i - 1;
            break;
        }
    }
    return nob_sv_from_parts(path.data + start, end - start);
}

Eval_Result eval_handle_enable_testing(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) > 0) {
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "dispatcher", nob_sv_from_cstr("Command does not accept arguments"),
                       nob_sv_from_cstr("Usage: enable_testing()"));
        return eval_result_from_ctx(ctx);
    }

    if (!eval_emit_test_enable(ctx, o)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

enum {
    ADD_TEST_OPT_WORKING_DIRECTORY = 1,
    ADD_TEST_OPT_COMMAND_EXPAND_LISTS,
    ADD_TEST_OPT_CONFIGURATIONS,
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
    if (!ctx || !userdata) return false;
    Add_Test_Option_State *st = (Add_Test_Option_State*)userdata;
    if (id == ADD_TEST_OPT_WORKING_DIRECTORY) {
        if (arena_arr_len(values) > 0) st->working_dir = values[0];
        return true;
    }
    if (id == ADD_TEST_OPT_COMMAND_EXPAND_LISTS) {
        st->command_expand_lists = true;
        return true;
    }
    if (id == ADD_TEST_OPT_CONFIGURATIONS) {
        if (arena_arr_len(values) == 0) {
            EVAL_DIAG(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("eval_test"),
                           st->command_name,
                           st->origin,
                           nob_sv_from_cstr("add_test(NAME ...) CONFIGURATIONS requires at least one value"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...> [CONFIGURATIONS <cfg>...])"));
            return false;
        }
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
    EVAL_DIAG(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("eval_test"),
                   st->command_name,
                   st->origin,
                   nob_sv_from_cstr("add_test(NAME ...) received unexpected argument"),
                   value);
    return false;
}

Eval_Result eval_handle_add_test(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "dispatcher", nob_sv_from_cstr("add_test() requires at least test name and command"),
                       nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>) or add_test(<name> <cmd...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View name = nob_sv_from_cstr("");
    String_View command = nob_sv_from_cstr("");
    String_View working_dir = nob_sv_from_cstr("");
    bool command_expand_lists = false;

    if (eval_sv_eq_ci_lit(a[0], "NAME")) {
        if (arena_arr_len(a) < 4) {
            EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "dispatcher", nob_sv_from_cstr("add_test(NAME ...) requires COMMAND clause"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>)"));
            return eval_result_from_ctx(ctx);
        }
        name = a[1];

        size_t cmd_i = 2;
        if (!eval_sv_eq_ci_lit(a[cmd_i], "COMMAND")) {
            EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "dispatcher", nob_sv_from_cstr("add_test(NAME ...) missing COMMAND"),
                           nob_sv_from_cstr("Usage: add_test(NAME <name> COMMAND <cmd...>)"));
            return eval_result_from_ctx(ctx);
        }
        cmd_i++;
        size_t cmd_start = cmd_i;
        const Eval_Opt_Spec add_test_specs[] = {
            {ADD_TEST_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE},
            {ADD_TEST_OPT_COMMAND_EXPAND_LISTS, "COMMAND_EXPAND_LISTS", EVAL_OPT_FLAG},
            {ADD_TEST_OPT_CONFIGURATIONS, "CONFIGURATIONS", EVAL_OPT_MULTI},
        };
        size_t cmd_end = cmd_i;
        while (cmd_end < arena_arr_len(a) &&
               !eval_opt_token_is_keyword(a[cmd_end], add_test_specs, NOB_ARRAY_LEN(add_test_specs))) {
            cmd_end++;
        }
        if (cmd_end <= cmd_start) {
            EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "dispatcher", nob_sv_from_cstr("add_test(NAME ...) has empty COMMAND"),
                           nob_sv_from_cstr(""));
            return eval_result_from_ctx(ctx);
        }
        command = svu_join_space_temp(ctx, &a[cmd_start], cmd_end - cmd_start);

        Add_Test_Option_State st = {
            .origin = o,
            .command_name = node->as.cmd.name,
            .working_dir = working_dir,
            .command_expand_lists = command_expand_lists,
        };
        Eval_Opt_Parse_Config cfg = {
            .origin = o,
            .component = nob_sv_from_cstr("eval_test"),
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
            return eval_result_from_ctx(ctx);
        }
        working_dir = st.working_dir;
        command_expand_lists = st.command_expand_lists;
    } else {
        name = a[0];
        command = svu_join_space_temp(ctx, &a[1], arena_arr_len(a) - 1);
    }

    {
        size_t total = strlen("NOBIFY_TEST::") + name.count;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
        memcpy(buf, "NOBIFY_TEST::", strlen("NOBIFY_TEST::"));
        memcpy(buf + strlen("NOBIFY_TEST::"), name.data, name.count);
        buf[total] = '\0';
        (void)eval_var_set_current(ctx, nob_sv_from_cstr(buf), nob_sv_from_cstr("1"));
    }
    if (!eval_emit_test_add(ctx, o, name, command, working_dir, command_expand_lists)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_create_test_sourcelist(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 3) {
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "eval_test", nob_sv_from_cstr("create_test_sourcelist() requires an output variable, driver, and at least one test"),
                       nob_sv_from_cstr("Usage: create_test_sourcelist(<sourceListVar> <driver> <tests>... [EXTRA_INCLUDE <inc>] [FUNCTION <fn>])"));
        return eval_result_from_ctx(ctx);
    }

    String_View out_var = a[0];
    String_View driver = a[1];
    String_View extra_include = nob_sv_from_cstr("");
    String_View hook_fn = nob_sv_from_cstr("");
    SV_List tests = NULL;

    size_t i = 2;
    while (i < arena_arr_len(a)) {
        if (eval_sv_eq_ci_lit(a[i], "EXTRA_INCLUDE") || eval_sv_eq_ci_lit(a[i], "FUNCTION")) break;
        if (!svu_list_push_temp(ctx, &tests, a[i])) return eval_result_from_ctx(ctx);
        i++;
    }
    while (i < arena_arr_len(a)) {
        if (eval_sv_eq_ci_lit(a[i], "EXTRA_INCLUDE")) {
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "eval_test", nob_sv_from_cstr("create_test_sourcelist(EXTRA_INCLUDE ...) requires a value"),
                               nob_sv_from_cstr("Usage: ... EXTRA_INCLUDE <header>"));
                return eval_result_from_ctx(ctx);
            }
            extra_include = a[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "FUNCTION")) {
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "eval_test", nob_sv_from_cstr("create_test_sourcelist(FUNCTION ...) requires a value"),
                               nob_sv_from_cstr("Usage: ... FUNCTION <fn>"));
                return eval_result_from_ctx(ctx);
            }
            hook_fn = a[i + 1];
            i += 2;
            continue;
        }
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "eval_test", nob_sv_from_cstr("create_test_sourcelist() received an unsupported argument"),
                       a[i]);
        return eval_result_from_ctx(ctx);
    }

    if (arena_arr_len(tests) == 0) {
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "eval_test", nob_sv_from_cstr("create_test_sourcelist() requires at least one test source"),
                       nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    if (!eval_sv_is_abs_path(driver)) {
        driver = eval_sv_path_join(eval_temp_arena(ctx), test_current_bin_dir(ctx), driver);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    }

    String_View before = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_TESTDRIVER_BEFORE_TESTMAIN"));
    String_View after = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_TESTDRIVER_AFTER_TESTMAIN"));

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "/* evaluator-generated create_test_sourcelist driver */\n");
    nob_sb_append_cstr(&sb, "#include <string.h>\n");
    if (extra_include.count > 0) {
        nob_sb_append_cstr(&sb, "#include \"");
        nob_sb_append_buf(&sb, extra_include.data, extra_include.count);
        nob_sb_append_cstr(&sb, "\"\n");
    }
    for (size_t ti = 0; ti < arena_arr_len(tests); ti++) {
        String_View stem = test_source_stem_temp(ctx, tests[ti]);
        nob_sb_append_cstr(&sb, "extern int ");
        nob_sb_append_buf(&sb, stem.data, stem.count);
        nob_sb_append_cstr(&sb, "(int, char**);\n");
    }
    if (hook_fn.count > 0) {
        nob_sb_append_cstr(&sb, "extern void ");
        nob_sb_append_buf(&sb, hook_fn.data, hook_fn.count);
        nob_sb_append_cstr(&sb, "(void);\n");
    }
    nob_sb_append_cstr(&sb, "int main(int argc, char **argv) {\n");
    if (before.count > 0) {
        nob_sb_append_buf(&sb, before.data, before.count);
        if (before.data[before.count - 1] != '\n') nob_sb_append_cstr(&sb, "\n");
    }
    if (hook_fn.count > 0) {
        nob_sb_append_cstr(&sb, "  ");
        nob_sb_append_buf(&sb, hook_fn.data, hook_fn.count);
        nob_sb_append_cstr(&sb, "();\n");
    }
    nob_sb_append_cstr(&sb, "  if (argc < 2) return 0;\n");
    for (size_t ti = 0; ti < arena_arr_len(tests); ti++) {
        String_View stem = test_source_stem_temp(ctx, tests[ti]);
        nob_sb_append_cstr(&sb, "  if (strcmp(argv[1], \"");
        nob_sb_append_buf(&sb, stem.data, stem.count);
        nob_sb_append_cstr(&sb, "\") == 0) return ");
        nob_sb_append_buf(&sb, stem.data, stem.count);
        nob_sb_append_cstr(&sb, "(argc, argv);\n");
    }
    if (after.count > 0) {
        nob_sb_append_buf(&sb, after.data, after.count);
        if (after.data[after.count - 1] != '\n') nob_sb_append_cstr(&sb, "\n");
    }
    nob_sb_append_cstr(&sb, "  return 0;\n}\n");

    if (!eval_write_text_file(ctx, driver, nob_sv_from_parts(sb.items, sb.count), false)) {
        EVAL_NODE_ORIGIN_DIAG(ctx, node, o, EV_DIAG_ERROR, "eval_test", nob_sv_from_cstr("create_test_sourcelist() failed to write the generated driver"),
                       driver);
        return eval_result_from_ctx(ctx);
    }

    SV_List out_items = NULL;
    for (size_t ti = 0; ti < arena_arr_len(tests); ti++) {
        if (!svu_list_push_temp(ctx, &out_items, tests[ti])) return eval_result_from_ctx(ctx);
    }
    if (!svu_list_push_temp(ctx, &out_items, driver)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, out_var, eval_sv_join_semi_temp(ctx, out_items, arena_arr_len(out_items)))) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

