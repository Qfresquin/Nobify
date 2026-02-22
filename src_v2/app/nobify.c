#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "arena.h"
#include "arena_dyn.h"
#include "lexer.h"
#include "parser.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "event_ir.h"
#include "build_model_builder.h"
#include "build_model_freeze.h"
#include "build_model_query.h"

#include <string.h>

static void print_usage(const char *program) {
    nob_log(NOB_INFO, "Usage: %s [--strict] [--tokens] [--ast] [--events] [input]", program);
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
    bool print_events = false;
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
        if (strcmp(argv[i], "--events") == 0) {
            print_events = true;
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

    Arena *eval_arena = arena_create(8 * 1024 * 1024);
    Arena *event_arena = arena_create(16 * 1024 * 1024);
    Arena *model_arena = arena_create(16 * 1024 * 1024);
    if (!eval_arena || !event_arena || !model_arena) {
        nob_log(NOB_ERROR, "Failed to allocate evaluator/build-model arenas");
        arena_destroy(eval_arena);
        arena_destroy(event_arena);
        arena_destroy(model_arena);
        arena_destroy(arena);
        return 1;
    }

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    if (!stream) {
        nob_log(NOB_ERROR, "Failed to create event stream");
        arena_destroy(model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    Evaluator_Init eval_init = {0};
    eval_init.arena = eval_arena;
    eval_init.event_arena = event_arena;
    eval_init.stream = stream;
    eval_init.source_dir = sv_from_cstr(".");
    eval_init.binary_dir = sv_from_cstr(".");
    eval_init.current_file = input_path;

    Evaluator_Context *eval_ctx = evaluator_create(&eval_init);
    if (!eval_ctx) {
        nob_log(NOB_ERROR, "Failed to create evaluator context");
        arena_destroy(model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    if (!evaluator_run(eval_ctx, ast)) {
        nob_log(NOB_ERROR, "Evaluator failed while processing AST");
        evaluator_destroy(eval_ctx);
        arena_destroy(model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }
    evaluator_destroy(eval_ctx);

    if (print_events) {
        event_stream_dump(stream);
    }

    Build_Model_Builder *builder = builder_create(event_arena, NULL);
    if (!builder) {
        nob_log(NOB_ERROR, "Failed to create Build Model builder");
        arena_destroy(model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    if (!builder_apply_stream(builder, stream)) {
        nob_log(NOB_ERROR, "Failed to apply evaluator event stream to Build Model");
        arena_destroy(model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    const Build_Model *model = build_model_freeze(builder, model_arena);
    if (!model) {
        nob_log(NOB_ERROR, "Failed to freeze Build Model");
        arena_destroy(model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    nob_log(NOB_INFO,
            "Build Model ready: project='%.*s' targets=%zu packages=%zu tests=%zu",
            SV_Arg(bm_query_project_name(model)),
            model->target_count,
            model->package_count,
            model->test_count);

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

    arena_destroy(model_arena);
    arena_destroy(event_arena);
    arena_destroy(eval_arena);
    arena_destroy(arena);
    return 0;
}
