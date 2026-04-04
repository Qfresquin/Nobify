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
#include "build_model_validate.h"
#include "build_model_freeze.h"
#include "build_model_query.h"
#include "nob_codegen.h"

#include <string.h>

static void print_usage(const char *program) {
    nob_log(NOB_INFO,
            "Usage: %s [--strict] [--tokens] [--ast] [--events] [--source-root path] [--binary-root path] [--out path] [input]",
            program);
}

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, token);
}

static void nobify_init_event(Event *ev, Event_Kind kind, const char *file_path, size_t line) {
    if (!ev) return;
    *ev = (Event){0};
    ev->h.kind = kind;
    ev->h.origin.file_path = nob_sv_from_cstr(file_path ? file_path : "CMakeLists.txt");
    ev->h.origin.line = line;
    ev->h.origin.col = 1;
}

static Event_Stream *nobify_wrap_stream_with_root(Arena *arena,
                                                  const Event_Stream *stream,
                                                  const char *current_file,
                                                  String_View source_dir,
                                                  String_View binary_dir) {
    Event_Stream *wrapped = NULL;
    Event ev = {0};
    if (!arena || !stream) return NULL;

    wrapped = event_stream_create(arena);
    if (!wrapped) return NULL;

    nobify_init_event(&ev, EVENT_DIRECTORY_ENTER, current_file, 0);
    ev.as.directory_enter.source_dir = source_dir;
    ev.as.directory_enter.binary_dir = binary_dir;
    if (!event_stream_push(wrapped, &ev)) return NULL;

    for (size_t i = 0; i < stream->count; ++i) {
        if (!event_stream_push(wrapped, &stream->items[i])) return NULL;
    }

    nobify_init_event(&ev, EVENT_DIRECTORY_LEAVE, current_file, 0);
    ev.as.directory_leave.source_dir = source_dir;
    ev.as.directory_leave.binary_dir = binary_dir;
    if (!event_stream_push(wrapped, &ev)) return NULL;

    return wrapped;
}

int main(int argc, char **argv) {
    bool strict_mode = false;
    bool print_tokens = false;
    bool print_ast_tree = false;
    bool print_events = false;
    const char *input_path = "CMakeLists.txt";
    const char *output_path = NULL;
    const char *source_root_path = NULL;
    const char *binary_root_path = NULL;

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
        if (strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                nob_log(NOB_ERROR, "Missing value for --out");
                print_usage(argv[0]);
                return 1;
            }
            output_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--source-root") == 0) {
            if (i + 1 >= argc) {
                nob_log(NOB_ERROR, "Missing value for --source-root");
                print_usage(argv[0]);
                return 1;
            }
            source_root_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--binary-root") == 0) {
            if (i + 1 >= argc) {
                nob_log(NOB_ERROR, "Missing value for --binary-root");
                print_usage(argv[0]);
                return 1;
            }
            binary_root_path = argv[++i];
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

    if (!output_path) {
        output_path = nob_temp_sprintf("%s/nob.c", nob_temp_dir_name(input_path));
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
    char *input_dir = arena_strdup(arena, nob_temp_dir_name(input_path));
    char *source_root = NULL;
    char *binary_root = NULL;
    if (!input_dir) {
        nob_log(NOB_ERROR, "Failed to resolve input directory");
        arena_destroy(arena);
        return 1;
    }
    source_root = arena_strdup(arena, source_root_path ? source_root_path : input_dir);
    binary_root = arena_strdup(arena, binary_root_path ? binary_root_path : source_root);
    if (!source_root || !binary_root) {
        nob_log(NOB_ERROR, "Failed to resolve effective roots");
        arena_destroy(arena);
        return 1;
    }
    Lexer lexer = lexer_init(content);
    Token_List tokens = NULL;

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
    nob_log(NOB_INFO, "Parsed %zu tokens into %zu root nodes", arena_arr_len(tokens), arena_arr_len(ast));

    if (print_ast_tree) {
        print_ast(ast, 0);
    }

    Arena *eval_arena = arena_create(8 * 1024 * 1024);
    Arena *event_arena = arena_create(16 * 1024 * 1024);
    if (!eval_arena || !event_arena) {
        nob_log(NOB_ERROR, "Failed to allocate evaluator arenas");
        arena_destroy(eval_arena);
        arena_destroy(event_arena);
        arena_destroy(arena);
        return 1;
    }

    Event_Stream *stream = event_stream_create(event_arena);
    Event_Stream *build_stream = NULL;
    if (!stream) {
        nob_log(NOB_ERROR, "Failed to create event stream");
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    EvalSession_Config session_cfg = {0};
    session_cfg.persistent_arena = event_arena;
    session_cfg.source_root = sv_from_cstr(source_root);
    session_cfg.binary_root = sv_from_cstr(binary_root);

    EvalSession *session = eval_session_create(&session_cfg);
    if (!session) {
        nob_log(NOB_ERROR, "Failed to create evaluator session");
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    EvalExec_Request eval_request = {0};
    eval_request.scratch_arena = eval_arena;
    eval_request.source_dir = sv_from_cstr(source_root);
    eval_request.binary_dir = sv_from_cstr(binary_root);
    eval_request.list_file = input_path;
    eval_request.stream = stream;

    EvalRunResult run_result = eval_session_run(session, &eval_request, ast);
    if (eval_result_is_fatal(run_result.result)) {
        nob_log(NOB_ERROR, "Evaluator failed while processing AST");
        eval_session_destroy(session);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }
    eval_session_destroy(session);

    if (print_events) {
        event_stream_dump(stream);
    }
    nob_log(NOB_INFO,
            "Semantic Event IR ready: events=%zu",
            arena_arr_len(stream->items));

    diag_telemetry_emit_summary();
    (void)diag_telemetry_write_report("nobify_v2_unsupported_commands.log", input_path);

    if (diag_has_errors()) {
        nob_log(NOB_ERROR, "Finished with %zu error(s) and %zu warning(s)", diag_error_count(), diag_warning_count());
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    if (diag_has_warnings()) {
        nob_log(NOB_WARNING, "Finished with %zu warning(s)", diag_warning_count());
    } else {
        nob_log(NOB_INFO, "Finished without diagnostics");
    }

    Arena *build_model_arena = arena_create(16 * 1024 * 1024);
    Arena *build_model_validate_arena = arena_create(8 * 1024 * 1024);
    Arena *build_model_freeze_arena = arena_create(16 * 1024 * 1024);
    Diag_Sink *build_model_sink = NULL;
    if (!build_model_arena || !build_model_validate_arena || !build_model_freeze_arena) {
        nob_log(NOB_ERROR, "Failed to allocate build-model arenas");
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    build_model_sink = bm_diag_sink_create_default(build_model_arena);
    BM_Builder *builder = bm_builder_create(build_model_arena, build_model_sink);
    if (!builder) {
        nob_log(NOB_ERROR, "Failed to create build-model builder");
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    build_stream = nobify_wrap_stream_with_root(event_arena,
                                                stream,
                                                input_path,
                                                eval_request.source_dir,
                                                eval_request.binary_dir);
    if (!build_stream) {
        nob_log(NOB_ERROR, "Failed to wrap semantic events with root directory context");
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    if (!bm_builder_apply_stream(builder, build_stream)) {
        nob_log(NOB_ERROR, "Build-model builder failed while consuming semantic events");
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    const Build_Model_Draft *draft = bm_builder_finalize(builder);
    if (!draft) {
        nob_log(NOB_ERROR, "Build-model builder failed to finalize draft");
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    if (!bm_validate_draft(draft, build_model_validate_arena, build_model_sink)) {
        nob_log(NOB_ERROR, "Build-model validation failed");
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    const Build_Model *model = bm_freeze_draft(draft, build_model_freeze_arena, build_model_sink);
    if (!model) {
        nob_log(NOB_ERROR, "Build-model freeze failed");
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    nob_log(NOB_INFO,
            "Build model ready: directories=%zu targets=%zu tests=%zu packages=%zu",
            bm_query_directory_count(model),
            bm_query_target_count(model),
            bm_query_test_count(model),
            bm_query_package_count(model));

    Arena *codegen_arena = arena_create(8 * 1024 * 1024);
    if (!codegen_arena) {
        nob_log(NOB_ERROR, "Failed to allocate codegen arena");
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }

    Nob_Codegen_Options codegen_opts = {
        .input_path = nob_sv_from_cstr(input_path),
        .output_path = nob_sv_from_cstr(output_path),
        .source_root = sv_from_cstr(source_root),
        .binary_root = sv_from_cstr(binary_root),
    };
    if (!nob_codegen_write_file(model, codegen_arena, &codegen_opts)) {
        nob_log(NOB_ERROR, "Codegen failed while writing %s", output_path);
        arena_destroy(codegen_arena);
        arena_destroy(build_model_freeze_arena);
        arena_destroy(build_model_validate_arena);
        arena_destroy(build_model_arena);
        arena_destroy(event_arena);
        arena_destroy(eval_arena);
        arena_destroy(arena);
        return 1;
    }
    nob_log(NOB_INFO, "Generated Nob build file: %s", output_path);

    arena_destroy(codegen_arena);
    arena_destroy(build_model_freeze_arena);
    arena_destroy(build_model_validate_arena);
    arena_destroy(build_model_arena);
    arena_destroy(event_arena);
    arena_destroy(eval_arena);
    arena_destroy(arena);
    return 0;
}
