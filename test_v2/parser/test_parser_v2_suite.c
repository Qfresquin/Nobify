#include "test_v2_assert.h"
#include "test_v2_suite.h"

#include "arena_dyn.h"
#include "diagnostics.h"
#include "lexer.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = token;
    return true;
}

static Ast_Root parse_script_local(Arena *arena, const char *script) {
    Lexer lx = lexer_init(nob_sv_from_cstr(script));
    Token_List toks = {0};
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &toks, t)) return (Ast_Root){0};
    }
    return parse_tokens(arena, toks);
}

static bool load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    if (!arena || !path || !out) return false;
    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path, &sb)) return false;
    char *text = arena_strndup(arena, sb.items, sb.count);
    size_t len = sb.count;
    nob_sb_free(sb);
    if (!text) return false;
    *out = nob_sv_from_parts(text, len);
    return true;
}

static String_View normalize_newlines_to_arena(Arena *arena, String_View in) {
    if (!arena) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(arena, in.count + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t out_count = 0;
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (c == '\r') continue;
        buf[out_count++] = c;
    }
    buf[out_count] = '\0';
    return nob_sv_from_parts(buf, out_count);
}

static void snapshot_append_indent(Nob_String_Builder *sb, int indent) {
    for (int i = 0; i < indent; i++) nob_sb_append_cstr(sb, "  ");
}

static void snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv) {
    nob_sb_append_cstr(sb, "'");
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '\\') {
            nob_sb_append_cstr(sb, "\\\\");
        } else if (c == '\n') {
            nob_sb_append_cstr(sb, "\\n");
        } else if (c == '\r') {
            nob_sb_append_cstr(sb, "\\r");
        } else if (c == '\t') {
            nob_sb_append_cstr(sb, "\\t");
        } else if (c == '\'') {
            nob_sb_append_cstr(sb, "\\'");
        } else {
            nob_sb_append(sb, c);
        }
    }
    nob_sb_append_cstr(sb, "'");
}

static const char *arg_kind_name(Arg_Kind kind) {
    switch (kind) {
        case ARG_UNQUOTED: return "UNQUOTED";
        case ARG_QUOTED: return "QUOTED";
        case ARG_BRACKET: return "BRACKET";
        default: return "UNKNOWN";
    }
}

static const char *node_kind_name(Node_Kind kind) {
    switch (kind) {
        case NODE_COMMAND: return "COMMAND";
        case NODE_IF: return "IF";
        case NODE_FOREACH: return "FOREACH";
        case NODE_WHILE: return "WHILE";
        case NODE_FUNCTION: return "FUNCTION";
        case NODE_MACRO: return "MACRO";
        default: return "UNKNOWN";
    }
}

static void snapshot_append_args(Nob_String_Builder *sb, const char *label, const Args *args, int indent) {
    snapshot_append_indent(sb, indent);
    nob_sb_append_cstr(sb, label);
    nob_sb_append_cstr(sb, " count=");
    nob_sb_append_cstr(sb, nob_temp_sprintf("%zu\n", args ? args->count : 0));
    if (!args) return;

    for (size_t i = 0; i < args->count; i++) {
        const Arg *arg = &args->items[i];
        snapshot_append_indent(sb, indent + 1);
        nob_sb_append_cstr(sb, nob_temp_sprintf("ARG[%zu] kind=%s tokens=%zu\n", i, arg_kind_name(arg->kind), arg->count));
        for (size_t j = 0; j < arg->count; j++) {
            Token tok = arg->items[j];
            snapshot_append_indent(sb, indent + 2);
            nob_sb_append_cstr(sb, nob_temp_sprintf("TOK[%zu] kind=%s text=", j, token_kind_name(tok.kind)));
            snapshot_append_escaped_sv(sb, tok.text);
            nob_sb_append_cstr(sb, "\n");
        }
    }
}

static void snapshot_append_node_list(Nob_String_Builder *sb, const Node_List *list, int indent);

static void snapshot_append_node(Nob_String_Builder *sb, const Node *node, int indent, size_t index) {
    if (!node) return;
    snapshot_append_indent(sb, indent);
    nob_sb_append_cstr(sb, nob_temp_sprintf("NODE[%zu] kind=%s line=%zu col=%zu\n",
        index, node_kind_name(node->kind), node->line, node->col));

    switch (node->kind) {
        case NODE_COMMAND:
            snapshot_append_indent(sb, indent + 1);
            nob_sb_append_cstr(sb, "NAME=");
            snapshot_append_escaped_sv(sb, node->as.cmd.name);
            nob_sb_append_cstr(sb, "\n");
            snapshot_append_args(sb, "ARGS", &node->as.cmd.args, indent + 1);
            break;

        case NODE_IF:
            snapshot_append_args(sb, "COND", &node->as.if_stmt.condition, indent + 1);
            snapshot_append_indent(sb, indent + 1);
            nob_sb_append_cstr(sb, "THEN\n");
            snapshot_append_node_list(sb, &node->as.if_stmt.then_block, indent + 2);
            snapshot_append_indent(sb, indent + 1);
            nob_sb_append_cstr(sb, nob_temp_sprintf("ELSEIF_COUNT=%zu\n", node->as.if_stmt.elseif_clauses.count));
            for (size_t i = 0; i < node->as.if_stmt.elseif_clauses.count; i++) {
                snapshot_append_indent(sb, indent + 1);
                nob_sb_append_cstr(sb, nob_temp_sprintf("ELSEIF[%zu]\n", i));
                snapshot_append_args(sb, "COND", &node->as.if_stmt.elseif_clauses.items[i].condition, indent + 2);
                snapshot_append_indent(sb, indent + 2);
                nob_sb_append_cstr(sb, "BLOCK\n");
                snapshot_append_node_list(sb, &node->as.if_stmt.elseif_clauses.items[i].block, indent + 3);
            }
            snapshot_append_indent(sb, indent + 1);
            nob_sb_append_cstr(sb, "ELSE\n");
            snapshot_append_node_list(sb, &node->as.if_stmt.else_block, indent + 2);
            break;

        case NODE_FOREACH:
            snapshot_append_args(sb, "ARGS", &node->as.foreach_stmt.args, indent + 1);
            snapshot_append_indent(sb, indent + 1);
            nob_sb_append_cstr(sb, "BODY\n");
            snapshot_append_node_list(sb, &node->as.foreach_stmt.body, indent + 2);
            break;

        case NODE_WHILE:
            snapshot_append_args(sb, "COND", &node->as.while_stmt.condition, indent + 1);
            snapshot_append_indent(sb, indent + 1);
            nob_sb_append_cstr(sb, "BODY\n");
            snapshot_append_node_list(sb, &node->as.while_stmt.body, indent + 2);
            break;

        case NODE_FUNCTION:
        case NODE_MACRO:
            snapshot_append_indent(sb, indent + 1);
            nob_sb_append_cstr(sb, "NAME=");
            snapshot_append_escaped_sv(sb, node->as.func_def.name);
            nob_sb_append_cstr(sb, "\n");
            snapshot_append_args(sb, "PARAMS", &node->as.func_def.params, indent + 1);
            snapshot_append_indent(sb, indent + 1);
            nob_sb_append_cstr(sb, "BODY\n");
            snapshot_append_node_list(sb, &node->as.func_def.body, indent + 2);
            break;
    }
}

static void snapshot_append_node_list(Nob_String_Builder *sb, const Node_List *list, int indent) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        snapshot_append_node(sb, &list->items[i], indent, i);
    }
}

static bool render_parser_snapshot_to_arena(Arena *arena, Ast_Root ast, String_View *out) {
    if (!arena || !out) return false;
    Nob_String_Builder sb = {0};

    nob_sb_append_cstr(&sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("ROOT count=%zu\n", ast.count));
    snapshot_append_node_list(&sb, &ast, 0);

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;
    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_parser_golden(const char *input_path, const char *golden_path) {
    Arena *arena = arena_create(512 * 1024);
    if (!arena) return false;

    String_View script = {0};
    String_View expected = {0};
    String_View actual = {0};
    bool ok = true;

    if (!load_text_file_to_arena(arena, input_path, &script)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }
    if (!load_text_file_to_arena(arena, golden_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", golden_path);
        ok = false;
        goto done;
    }

    diag_reset();
    Ast_Root ast = parse_script_local(arena, script.data);
    if (!render_parser_snapshot_to_arena(arena, ast, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render snapshot");
        ok = false;
        goto done;
    }

    String_View expected_norm = normalize_newlines_to_arena(arena, expected);
    String_View actual_norm = normalize_newlines_to_arena(arena, actual);
    if (!nob_sv_eq(expected_norm, actual_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", golden_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

done:
    arena_destroy(arena);
    return ok;
}

static const char *PARSER_GOLDEN_DIR = "test_v2/parser/golden";

static void set_env_or_unset(const char *name, const char *value) {
#if defined(_WIN32)
    if (value) _putenv_s(name, value);
    else _putenv_s(name, "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

TEST(bracket_arg_equals_delimiter) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    Ast_Root ast = parse_script_local(arena, "set(X [=[a;b]=])\nset(Y [==[z]==])");

    ASSERT(ast.count == 2);
    ASSERT(ast.items[0].kind == NODE_COMMAND);
    ASSERT(ast.items[0].as.cmd.args.count == 2);
    ASSERT(ast.items[0].as.cmd.args.items[1].kind == ARG_BRACKET);
    ASSERT(ast.items[1].as.cmd.args.items[1].kind == ARG_BRACKET);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_reports_command_without_parentheses_and_recovers) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, "set VAR value\nmessage(ok)\n");

    ASSERT(diag_has_errors());
    ASSERT(ast.count == 1);
    ASSERT(ast.items[0].kind == NODE_COMMAND);
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.name, nob_sv_from_cstr("message")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_reports_stray_paren_and_recovers) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, ")\nset(X 1)\n");

    ASSERT(diag_has_errors());
    ASSERT(ast.count == 1);
    ASSERT(ast.items[0].kind == NODE_COMMAND);
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.name, nob_sv_from_cstr("set")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_keeps_nested_parens_inside_regular_args) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, "func(a(b))");

    ASSERT(diag_has_errors() == false);
    ASSERT(ast.count == 1);
    ASSERT(ast.items[0].kind == NODE_COMMAND);
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.name, nob_sv_from_cstr("func")));
    ASSERT(ast.items[0].as.cmd.args.count == 1);
    ASSERT(ast.items[0].as.cmd.args.items[0].count == 4);
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.args.items[0].items[0].text, nob_sv_from_cstr("a")));
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.args.items[0].items[1].text, nob_sv_from_cstr("(")));
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.args.items[0].items[2].text, nob_sv_from_cstr("b")));
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.args.items[0].items[3].text, nob_sv_from_cstr(")")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_marks_quoted_string_args) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, "set(X \"a b c\")");

    ASSERT(diag_has_errors() == false);
    ASSERT(ast.count == 1);
    ASSERT(ast.items[0].kind == NODE_COMMAND);
    ASSERT(ast.items[0].as.cmd.args.count == 2);
    ASSERT(ast.items[0].as.cmd.args.items[1].kind == ARG_QUOTED);
    ASSERT(ast.items[0].as.cmd.args.items[1].count == 1);
    ASSERT(ast.items[0].as.cmd.args.items[1].items[0].kind == TOKEN_STRING);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_reports_block_depth_limit_exceeded) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    set_env_or_unset("CMK2NOB_PARSER_MAX_BLOCK_DEPTH", "1");
    diag_reset();
    Ast_Root ast = parse_script_local(
        arena,
        "if(A)\n"
        "  if(B)\n"
        "    set(X 1)\n"
        "  endif()\n"
        "endif()\n"
    );
    set_env_or_unset("CMK2NOB_PARSER_MAX_BLOCK_DEPTH", NULL);

    ASSERT(ast.count >= 1);
    ASSERT(diag_has_errors());

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_drops_command_node_on_unclosed_args) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, "set(X 1");

    ASSERT(diag_has_errors());
    ASSERT(ast.count == 0);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_drops_if_node_on_missing_endif) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, "if(A)\n  set(X 1)\n");

    ASSERT(diag_has_errors());
    ASSERT(ast.count == 0);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_fail_fast_on_append_oom_budget) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    set_env_or_unset("CMK2NOB_PARSER_FAIL_APPEND_AFTER", "0");
    diag_reset();
    Ast_Root ast = parse_script_local(arena, "set(X 1)");
    set_env_or_unset("CMK2NOB_PARSER_FAIL_APPEND_AFTER", NULL);

    ASSERT(diag_has_errors());
    ASSERT(ast.count == 0);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_invalid_command_name_emits_error_and_recovers) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, "1abc(x)\nset(A 1)\n");

    ASSERT(diag_has_errors());
    ASSERT(ast.count == 1);
    ASSERT(ast.items[0].kind == NODE_COMMAND);
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.name, nob_sv_from_cstr("set")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_unexpected_statement_token_emits_error_and_recovers) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, ";\nset(A 1)\n");

    ASSERT(diag_has_errors());
    ASSERT(ast.count == 1);
    ASSERT(ast.items[0].kind == NODE_COMMAND);
    ASSERT(nob_sv_eq(ast.items[0].as.cmd.name, nob_sv_from_cstr("set")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(parser_golden_simple_commands) {
    ASSERT(assert_parser_golden(
        nob_temp_sprintf("%s/simple_commands.cmake", PARSER_GOLDEN_DIR),
        nob_temp_sprintf("%s/simple_commands.txt", PARSER_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(parser_golden_control_flow) {
    ASSERT(assert_parser_golden(
        nob_temp_sprintf("%s/control_flow.cmake", PARSER_GOLDEN_DIR),
        nob_temp_sprintf("%s/control_flow.txt", PARSER_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(parser_golden_recovery_invalid) {
    ASSERT(assert_parser_golden(
        nob_temp_sprintf("%s/recovery_invalid.cmake", PARSER_GOLDEN_DIR),
        nob_temp_sprintf("%s/recovery_invalid.txt", PARSER_GOLDEN_DIR)));
    TEST_PASS();
}

void run_parser_v2_tests(int *passed, int *failed) {
    test_bracket_arg_equals_delimiter(passed, failed);
    test_parser_reports_command_without_parentheses_and_recovers(passed, failed);
    test_parser_reports_stray_paren_and_recovers(passed, failed);
    test_parser_keeps_nested_parens_inside_regular_args(passed, failed);
    test_parser_marks_quoted_string_args(passed, failed);
    test_parser_reports_block_depth_limit_exceeded(passed, failed);
    test_parser_drops_command_node_on_unclosed_args(passed, failed);
    test_parser_drops_if_node_on_missing_endif(passed, failed);
    test_parser_fail_fast_on_append_oom_budget(passed, failed);
    test_parser_invalid_command_name_emits_error_and_recovers(passed, failed);
    test_parser_unexpected_statement_token_emits_error_and_recovers(passed, failed);
    test_parser_golden_simple_commands(passed, failed);
    test_parser_golden_control_flow(passed, failed);
    test_parser_golden_recovery_invalid(passed, failed);
}
