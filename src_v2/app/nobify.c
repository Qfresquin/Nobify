#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "arena.h"
#include "arena_dyn.h"
#include "lexer.h"
#include "parser.h"
#include "diagnostics.h"

#include <string.h>

static void print_usage(const char *program) {
    nob_log(NOB_INFO, "Usage: %s [--strict] [--tokens] [--ast] [input]", program);
}

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) {
        return false;
    }
    list->items[list->count++] = token;
    return true;
}

int main(int argc, char **argv) {
    bool strict_mode = false;
    bool print_tokens = false;
    bool print_ast_tree = false;
    const char *input_path = "CMakeLists.txt";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strict") == 0) {
            strict_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--tokens") == 0) {
            print_tokens = true;
            continue;
        }
        if (strcmp(argv[i], "--ast") == 0) {
            print_ast_tree = true;
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (argv[i][0] == '-') {
            nob_log(NOB_ERROR, "Unknown option: %s", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        input_path = argv[i];
    }

    diag_reset();
    diag_set_strict(strict_mode);
    diag_telemetry_reset();

    Arena *arena = arena_create(4 * 1024 * 1024);
    if (!arena) {
        nob_log(NOB_ERROR, "Failed to create memory arena");
        return 1;
    }

    Nob_String_Builder sb_input = {0};
    if (!nob_read_entire_file(input_path, &sb_input)) {
        nob_log(NOB_ERROR, "Failed to read input file: %s", input_path);
        arena_destroy(arena);
        return 1;
    }

    char *content_cstr = arena_strndup(arena, sb_input.items, sb_input.count);
    nob_sb_free(sb_input);
    if (!content_cstr) {
        nob_log(NOB_ERROR, "Failed to copy input into arena");
        arena_destroy(arena);
        return 1;
    }

    String_View content = nob_sv_from_parts(content_cstr, strlen(content_cstr));
    Lexer lexer = lexer_init(content);
    Token_List tokens = {0};

    for (;;) {
        Token token = lexer_next(&lexer);
        if (token.kind == TOKEN_END) break;

        if (token.kind == TOKEN_INVALID) {
            diag_log(
                DIAG_SEV_ERROR,
                "lexer",
                input_path,
                token.line,
                token.col,
                "token",
                "invalid token",
                "check quoting, escapes or variable syntax"
            );
            arena_destroy(arena);
            return 1;
        }

        if (!token_list_append(arena, &tokens, token)) {
            nob_log(NOB_ERROR, "Out of memory while appending tokens");
            arena_destroy(arena);
            return 1;
        }

        if (print_tokens) {
            nob_log(
                NOB_INFO,
                "TOKEN|kind=%s|line=%zu|col=%zu|text=%.*s",
                token_kind_name(token.kind),
                token.line,
                token.col,
                (int)token.text.count,
                token.text.data ? token.text.data : ""
            );
        }
    }

    Ast_Root ast = parse_tokens(arena, tokens);
    nob_log(NOB_INFO, "Parsed %zu tokens into %zu root nodes", tokens.count, ast.count);

    if (print_ast_tree) {
        print_ast(ast, 0);
    }

    diag_telemetry_emit_summary();
    (void)diag_telemetry_write_report("nobify_v2_unsupported_commands.log", input_path);

    if (diag_has_errors()) {
        nob_log(NOB_ERROR, "Finished with %zu error(s) and %zu warning(s)", diag_error_count(), diag_warning_count());
        arena_destroy(arena);
        return 1;
    }

    if (diag_has_warnings()) {
        nob_log(NOB_WARNING, "Finished with %zu warning(s)", diag_warning_count());
    } else {
        nob_log(NOB_INFO, "Finished without diagnostics");
    }

    arena_destroy(arena);
    return 0;
}
