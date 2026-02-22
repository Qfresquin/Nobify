#include "test_v2_assert.h"
#include "test_v2_suite.h"

#include "arena_dyn.h"
#include "diagnostics.h"
#include "lexer.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    String_View name;
    String_View script;
} Parser_Case;

typedef struct {
    Parser_Case *items;
    size_t count;
    size_t capacity;
} Parser_Case_List;

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

static bool parser_case_list_append(Arena *arena, Parser_Case_List *list, Parser_Case value) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = value;
    return true;
}

static bool sv_starts_with_cstr(String_View sv, const char *prefix) {
    String_View p = nob_sv_from_cstr(prefix);
    if (sv.count < p.count) return false;
    return memcmp(sv.data, p.data, p.count) == 0;
}

static String_View sv_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        return nob_sv_from_parts(sv.data, sv.count - 1);
    }
    return sv;
}

static bool parse_case_pack_to_arena(Arena *arena, String_View content, Parser_Case_List *out) {
    if (!arena || !out) return false;
    *out = (Parser_Case_List){0};

    Nob_String_Builder script_sb = {0};
    bool in_case = false;
    String_View current_name = {0};

    size_t pos = 0;
    while (pos < content.count) {
        size_t line_start = pos;
        while (pos < content.count && content.data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < content.count && content.data[pos] == '\n') pos++;

        String_View raw_line = nob_sv_from_parts(content.data + line_start, line_end - line_start);
        String_View line = sv_trim_cr(raw_line);

        if (sv_starts_with_cstr(line, "#@@CASE ")) {
            if (in_case) {
                nob_sb_free(script_sb);
                return false;
            }
            in_case = true;
            current_name = nob_sv_from_parts(line.data + 8, line.count - 8);
            script_sb.count = 0;
            continue;
        }

        if (nob_sv_eq(line, nob_sv_from_cstr("#@@ENDCASE"))) {
            if (!in_case) {
                nob_sb_free(script_sb);
                return false;
            }

            char *name = arena_strndup(arena, current_name.data, current_name.count);
            char *script = arena_strndup(arena, script_sb.items ? script_sb.items : "", script_sb.count);
            if (!name || !script) {
                nob_sb_free(script_sb);
                return false;
            }

            if (!parser_case_list_append(arena, out, (Parser_Case){
                .name = nob_sv_from_parts(name, current_name.count),
                .script = nob_sv_from_parts(script, script_sb.count),
            })) {
                nob_sb_free(script_sb);
                return false;
            }

            in_case = false;
            current_name = (String_View){0};
            script_sb.count = 0;
            continue;
        }

        if (in_case) {
            nob_sb_append_buf(&script_sb, raw_line.data, raw_line.count);
            nob_sb_append(&script_sb, '\n');
        }
    }

    nob_sb_free(script_sb);
    if (in_case) return false;

    for (size_t i = 0; i < out->count; i++) {
        for (size_t j = i + 1; j < out->count; j++) {
            if (nob_sv_eq(out->items[i].name, out->items[j].name)) {
                return false;
            }
        }
    }

    return out->count > 0;
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

static void set_env_or_unset(const char *name, const char *value) {
#if defined(_WIN32)
    if (value) _putenv_s(name, value);
    else _putenv_s(name, "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

static bool apply_parser_case_env(String_View name) {
    if (nob_sv_eq(name, nob_sv_from_cstr("parser_reports_block_depth_limit_exceeded"))) {
        set_env_or_unset("CMK2NOB_PARSER_MAX_BLOCK_DEPTH", "1");
        return true;
    }
    if (nob_sv_eq(name, nob_sv_from_cstr("parser_fail_fast_on_append_oom_budget"))) {
        set_env_or_unset("CMK2NOB_PARSER_FAIL_APPEND_AFTER", "0");
        return true;
    }
    return false;
}

static void clear_parser_case_env(String_View name) {
    if (nob_sv_eq(name, nob_sv_from_cstr("parser_reports_block_depth_limit_exceeded"))) {
        set_env_or_unset("CMK2NOB_PARSER_MAX_BLOCK_DEPTH", NULL);
    }
    if (nob_sv_eq(name, nob_sv_from_cstr("parser_fail_fast_on_append_oom_budget"))) {
        set_env_or_unset("CMK2NOB_PARSER_FAIL_APPEND_AFTER", NULL);
    }
}

static bool render_parser_case_snapshot_to_sb(Arena *arena, Parser_Case parser_case, Nob_String_Builder *sb) {
    bool has_case_env = apply_parser_case_env(parser_case.name);

    diag_reset();
    Ast_Root ast = parse_script_local(arena, parser_case.script.data);

    nob_sb_append_cstr(sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    nob_sb_append_cstr(sb, nob_temp_sprintf("ROOT count=%zu\n", ast.count));
    snapshot_append_node_list(sb, &ast, 0);

    if (has_case_env) clear_parser_case_env(parser_case.name);
    return true;
}

static bool render_parser_casepack_snapshot_to_arena(Arena *arena, Parser_Case_List cases, String_View *out) {
    if (!arena || !out) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "MODULE parser\n");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", cases.count));

    for (size_t i = 0; i < cases.count; i++) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases.items[i].name.data, cases.items[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");

        if (!render_parser_case_snapshot_to_sb(arena, cases.items[i], &sb)) {
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < cases.count) nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_parser_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(2 * 1024 * 1024);
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

    Parser_Case_List cases = {0};
    if (!parse_case_pack_to_arena(arena, script, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }
    if (cases.count != 14) {
        nob_log(NOB_ERROR, "golden: unexpected parser case count: got=%zu expected=14", cases.count);
        ok = false;
        goto done;
    }

    if (!render_parser_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render snapshot");
        ok = false;
        goto done;
    }

    String_View actual_norm = normalize_newlines_to_arena(arena, actual);

    const char *update = getenv("CMK2NOB_UPDATE_GOLDEN");
    if (update && strcmp(update, "1") == 0) {
        if (!nob_write_entire_file(expected_path, actual_norm.data, actual_norm.count)) {
            nob_log(NOB_ERROR, "golden: failed to update expected: %s", expected_path);
            ok = false;
        }
        goto done;
    }

    if (!load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        ok = false;
        goto done;
    }

    String_View expected_norm = normalize_newlines_to_arena(arena, expected);
    if (!nob_sv_eq(expected_norm, actual_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", expected_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

done:
    arena_destroy(arena);
    return ok;
}

static const char *PARSER_GOLDEN_DIR = "test_v2/parser/golden";

TEST(parser_golden_all_cases) {
    ASSERT(assert_parser_golden_casepack(
        nob_temp_sprintf("%s/parser_all.cmake", PARSER_GOLDEN_DIR),
        nob_temp_sprintf("%s/parser_all.txt", PARSER_GOLDEN_DIR)));
    TEST_PASS();
}

void run_parser_v2_tests(int *passed, int *failed) {
    test_parser_golden_all_cases(passed, failed);
}
