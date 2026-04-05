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
#include "tinydir.h"

#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static bool nobify_copy_string(const char *src,
                               char out[_TINYDIR_PATH_MAX]) {
    int n = 0;
    if (!src || !out) return false;
    n = snprintf(out, _TINYDIR_PATH_MAX, "%s", src);
    return n >= 0 && n < _TINYDIR_PATH_MAX;
}

static bool nobify_path_is_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    {
        DWORD attrs = GetFileAttributesA(path);
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }
#else
    return access(path, X_OK) == 0;
#endif
}

static bool nobify_find_executable_in_path(const char *name,
                                           char out_path[_TINYDIR_PATH_MAX]) {
    if (!name || !out_path) return false;

    if (strchr(name, '/') || strchr(name, '\\')) {
        if (!nobify_path_is_executable(name)) return false;
        return snprintf(out_path, _TINYDIR_PATH_MAX, "%s", name) < _TINYDIR_PATH_MAX;
    }

#if defined(_WIN32)
    {
        DWORD n = SearchPathA(NULL, name, NULL, _TINYDIR_PATH_MAX, out_path, NULL);
        return n > 0 && n < _TINYDIR_PATH_MAX;
    }
#else
    {
        const char *path_env = getenv("PATH");
        size_t temp_mark = nob_temp_save();
        if (!path_env || path_env[0] == '\0') return false;

        while (*path_env) {
            const char *sep = strchr(path_env, ':');
            size_t dir_len = sep ? (size_t)(sep - path_env) : strlen(path_env);
            const char *dir = dir_len == 0 ? "." : nob_temp_strndup(path_env, dir_len);
            const char *candidate = NULL;

            if (!dir) {
                nob_temp_rewind(temp_mark);
                return false;
            }
            candidate = nob_temp_sprintf("%s/%s", dir, name);
            if (candidate && nobify_path_is_executable(candidate)) {
                bool ok = snprintf(out_path, _TINYDIR_PATH_MAX, "%s", candidate) < _TINYDIR_PATH_MAX;
                nob_temp_rewind(temp_mark);
                return ok;
            }

            if (!sep) break;
            path_env = sep + 1;
        }

        nob_temp_rewind(temp_mark);
        return false;
    }
#endif
}

static bool nobify_resolve_sibling_tool(const char *tool_path,
                                        const char *tool_name,
                                        char out_path[_TINYDIR_PATH_MAX]) {
    const char *tool_dir = NULL;
    if (!tool_path || !tool_name || !out_path) return false;
    tool_dir = nob_temp_dir_name(tool_path);
    if (!tool_dir || tool_dir[0] == '\0') return false;
    if (snprintf(out_path, _TINYDIR_PATH_MAX, "%s/%s", tool_dir, tool_name) >= _TINYDIR_PATH_MAX) {
        return false;
    }
    return nobify_path_is_executable(out_path);
}

static bool nobify_find_repo_probe_cmake(char out_path[_TINYDIR_PATH_MAX]) {
    char probes_root[_TINYDIR_PATH_MAX] = {0};
    Nob_Dir_Entry dir = {0};
    const char *repo_root = getenv("CMK2NOB_TEST_REPO_ROOT");
    const char *cwd = NULL;

    if (!out_path) return false;
    if (!repo_root || repo_root[0] == '\0') {
        cwd = nob_get_current_dir_temp();
        repo_root = cwd;
    }
    if (!repo_root || repo_root[0] == '\0') return false;
    if (snprintf(probes_root,
                 sizeof(probes_root),
                 "%s/Temp_tests/probes",
                 repo_root) >= (int)sizeof(probes_root)) {
        return false;
    }
    if (!nob_file_exists(probes_root)) return false;
    if (!nob_dir_entry_open(probes_root, &dir)) return false;

    while (nob_dir_entry_next(&dir)) {
        char candidate_dir[_TINYDIR_PATH_MAX] = {0};
        char candidate_path[_TINYDIR_PATH_MAX] = {0};
        if (strcmp(dir.name, ".") == 0 || strcmp(dir.name, "..") == 0) continue;
        if (strncmp(dir.name, "cmake-", strlen("cmake-")) != 0) continue;
        if (snprintf(candidate_dir,
                     sizeof(candidate_dir),
                     "%s/%s",
                     probes_root,
                     dir.name) >= (int)sizeof(candidate_dir)) {
            continue;
        }
        if (snprintf(candidate_path,
                     sizeof(candidate_path),
                     "%s/bin/cmake",
                     candidate_dir) >= (int)sizeof(candidate_path)) {
            continue;
        }
        if (nobify_path_is_executable(candidate_path)) {
            nob_dir_entry_close(dir);
            return nobify_copy_string(candidate_path, out_path);
        }
    }

    nob_dir_entry_close(dir);
    return false;
}

static bool nobify_resolve_host_tool_paths(char cmake_bin[_TINYDIR_PATH_MAX],
                                           char cpack_bin[_TINYDIR_PATH_MAX]) {
    const char *env_cmake = NULL;
    if (!cmake_bin || !cpack_bin) return false;
    cmake_bin[0] = '\0';
    cpack_bin[0] = '\0';

    env_cmake = getenv("CMK2NOB_TEST_CMAKE_BIN");
    if (env_cmake && env_cmake[0] != '\0') {
        bool found = false;
        if (strchr(env_cmake, '/') || strchr(env_cmake, '\\')) {
            found = nobify_path_is_executable(env_cmake) &&
                    nobify_copy_string(env_cmake, cmake_bin);
        } else {
            found = nobify_find_executable_in_path(env_cmake, cmake_bin);
        }
        if (!found) cmake_bin[0] = '\0';
    } else if (nobify_find_executable_in_path("cmake", cmake_bin)) {
        /* Resolved from PATH. */
    } else if (nobify_find_repo_probe_cmake(cmake_bin)) {
        /* Resolved from repo-local probe cache. */
    }

    if (cmake_bin[0] == '\0') return true;
    (void)nobify_resolve_sibling_tool(cmake_bin, "cpack", cpack_bin);
    return true;
}

static const char *nobify_platform_name(Nob_Codegen_Platform platform) {
    switch (platform) {
        case NOB_CODEGEN_PLATFORM_HOST: return "host";
        case NOB_CODEGEN_PLATFORM_LINUX: return "linux";
        case NOB_CODEGEN_PLATFORM_DARWIN: return "darwin";
        case NOB_CODEGEN_PLATFORM_WINDOWS: return "windows";
    }
    return "unknown";
}

static const char *nobify_backend_name(Nob_Codegen_Backend backend) {
    switch (backend) {
        case NOB_CODEGEN_BACKEND_AUTO: return "auto";
        case NOB_CODEGEN_BACKEND_POSIX: return "posix";
        case NOB_CODEGEN_BACKEND_WIN32_MSVC: return "win32-msvc";
    }
    return "unknown";
}

static bool nobify_parse_platform(const char *value, Nob_Codegen_Platform *out) {
    if (out) *out = NOB_CODEGEN_PLATFORM_HOST;
    if (!value || !out) return false;
    if (strcmp(value, "host") == 0) *out = NOB_CODEGEN_PLATFORM_HOST;
    else if (strcmp(value, "linux") == 0) *out = NOB_CODEGEN_PLATFORM_LINUX;
    else if (strcmp(value, "darwin") == 0) *out = NOB_CODEGEN_PLATFORM_DARWIN;
    else if (strcmp(value, "windows") == 0) *out = NOB_CODEGEN_PLATFORM_WINDOWS;
    else return false;
    return true;
}

static bool nobify_parse_backend(const char *value, Nob_Codegen_Backend *out) {
    if (out) *out = NOB_CODEGEN_BACKEND_AUTO;
    if (!value || !out) return false;
    if (strcmp(value, "auto") == 0) *out = NOB_CODEGEN_BACKEND_AUTO;
    else if (strcmp(value, "posix") == 0) *out = NOB_CODEGEN_BACKEND_POSIX;
    else if (strcmp(value, "win32-msvc") == 0) *out = NOB_CODEGEN_BACKEND_WIN32_MSVC;
    else return false;
    return true;
}

static Nob_Codegen_Platform nobify_host_platform(void) {
#if defined(_WIN32)
    return NOB_CODEGEN_PLATFORM_WINDOWS;
#elif defined(__APPLE__)
    return NOB_CODEGEN_PLATFORM_DARWIN;
#else
    return NOB_CODEGEN_PLATFORM_LINUX;
#endif
}

static bool nobify_resolve_codegen_contract(Nob_Codegen_Platform requested_platform,
                                            Nob_Codegen_Backend requested_backend,
                                            Nob_Codegen_Platform *out_platform,
                                            Nob_Codegen_Backend *out_backend) {
    Nob_Codegen_Platform platform = requested_platform;
    Nob_Codegen_Backend backend = requested_backend;
    if (out_platform) *out_platform = NOB_CODEGEN_PLATFORM_HOST;
    if (out_backend) *out_backend = NOB_CODEGEN_BACKEND_AUTO;

    if (platform == NOB_CODEGEN_PLATFORM_HOST) {
        platform = nobify_host_platform();
    }
    if (backend == NOB_CODEGEN_BACKEND_AUTO) {
        switch (platform) {
            case NOB_CODEGEN_PLATFORM_LINUX:
            case NOB_CODEGEN_PLATFORM_DARWIN:
                backend = NOB_CODEGEN_BACKEND_POSIX;
                break;

            case NOB_CODEGEN_PLATFORM_WINDOWS:
                backend = NOB_CODEGEN_BACKEND_WIN32_MSVC;
                break;

            case NOB_CODEGEN_PLATFORM_HOST:
                backend = NOB_CODEGEN_BACKEND_AUTO;
                break;
        }
    }

    if ((platform == NOB_CODEGEN_PLATFORM_LINUX || platform == NOB_CODEGEN_PLATFORM_DARWIN) &&
        backend != NOB_CODEGEN_BACKEND_POSIX) {
        nob_log(NOB_ERROR,
                "Invalid codegen pair: platform=%s backend=%s",
                nobify_platform_name(platform),
                nobify_backend_name(backend));
        return false;
    }
    if (platform == NOB_CODEGEN_PLATFORM_WINDOWS &&
        backend != NOB_CODEGEN_BACKEND_WIN32_MSVC) {
        nob_log(NOB_ERROR,
                "Invalid codegen pair: platform=%s backend=%s",
                nobify_platform_name(platform),
                nobify_backend_name(backend));
        return false;
    }
    if (platform == NOB_CODEGEN_PLATFORM_HOST || backend == NOB_CODEGEN_BACKEND_AUTO) {
        nob_log(NOB_ERROR, "Failed to resolve codegen platform/backend contract");
        return false;
    }

    if (out_platform) *out_platform = platform;
    if (out_backend) *out_backend = backend;
    return true;
}

static void print_usage(const char *program) {
    nob_log(NOB_INFO,
            "Usage: %s [--strict] [--tokens] [--ast] [--events] [--platform host|linux|darwin|windows] [--backend auto|posix|win32-msvc] [--source-root path] [--binary-root path] [--out path] [input]",
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
    Nob_Codegen_Platform requested_platform = NOB_CODEGEN_PLATFORM_HOST;
    Nob_Codegen_Backend requested_backend = NOB_CODEGEN_BACKEND_AUTO;
    Nob_Codegen_Platform resolved_platform = NOB_CODEGEN_PLATFORM_HOST;
    Nob_Codegen_Backend resolved_backend = NOB_CODEGEN_BACKEND_AUTO;
    char cmake_bin[_TINYDIR_PATH_MAX] = {0};
    char cpack_bin[_TINYDIR_PATH_MAX] = {0};

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
        if (strcmp(argv[i], "--platform") == 0) {
            if (i + 1 >= argc) {
                nob_log(NOB_ERROR, "Missing value for --platform");
                print_usage(argv[0]);
                return 1;
            }
            if (!nobify_parse_platform(argv[++i], &requested_platform)) {
                nob_log(NOB_ERROR, "Invalid value for --platform: %s", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "--backend") == 0) {
            if (i + 1 >= argc) {
                nob_log(NOB_ERROR, "Missing value for --backend");
                print_usage(argv[0]);
                return 1;
            }
            if (!nobify_parse_backend(argv[++i], &requested_backend)) {
                nob_log(NOB_ERROR, "Invalid value for --backend: %s", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
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
    if (!nobify_resolve_codegen_contract(requested_platform,
                                         requested_backend,
                                         &resolved_platform,
                                         &resolved_backend)) {
        print_usage(argv[0]);
        return 1;
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
    if (!nobify_resolve_host_tool_paths(cmake_bin, cpack_bin)) {
        nob_log(NOB_ERROR, "Failed to resolve host tool paths");
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
        .embedded_cmake_bin = nob_sv_from_cstr(cmake_bin),
        .embedded_cpack_bin = nob_sv_from_cstr(cpack_bin),
        .target_platform = resolved_platform,
        .backend = resolved_backend,
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
