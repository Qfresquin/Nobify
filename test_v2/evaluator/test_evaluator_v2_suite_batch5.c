#include "test_evaluator_v2_support.h"

typedef struct {
    bool saw_value_names;
    bool saw_subkeys;
    bool saw_value;
    bool saw_view_64_32;
    bool saw_separator_pipe;
} Evaluator_Host_Query_Mock_Data;

static bool evaluator_host_query_windows_registry_mock(
    void *user_data,
    Arena *scratch_arena,
    const Eval_Windows_Registry_Query_Request *request,
    Eval_Windows_Registry_Query_Result *out_result) {
    (void)scratch_arena;
    Evaluator_Host_Query_Mock_Data *data = (Evaluator_Host_Query_Mock_Data*)user_data;
    if (out_result) {
        *out_result = (Eval_Windows_Registry_Query_Result){
            .found = true,
            .value = nob_sv_from_cstr(""),
            .error_message = nob_sv_from_cstr(""),
        };
    }
    if (!request || !out_result) return true;

    if (request->kind == EVAL_WINDOWS_REGISTRY_QUERY_VALUE) {
        if (data) data->saw_value = true;
        if (nob_sv_eq(request->view, nob_sv_from_cstr("64_32")) && data) data->saw_view_64_32 = true;
        out_result->value = nob_sv_from_cstr("C:/Registry/Install");
    } else if (request->kind == EVAL_WINDOWS_REGISTRY_QUERY_VALUE_NAMES) {
        if (data) data->saw_value_names = true;
        out_result->value = nob_sv_from_cstr("InstallDir;SdkRoot");
    } else if (request->kind == EVAL_WINDOWS_REGISTRY_QUERY_SUBKEYS) {
        if (data) data->saw_subkeys = true;
        if (nob_sv_eq(request->separator, nob_sv_from_cstr("|")) && data) data->saw_separator_pipe = true;
        out_result->value = nob_sv_from_cstr("ChildA|ChildB");
    }

    return true;
}

static bool evaluator_host_read_file_force_missing_os_release(void *user_data,
                                                              Arena *scratch_arena,
                                                              String_View path,
                                                              String_View *out_contents,
                                                              bool *out_found) {
    (void)user_data;
    (void)scratch_arena;
    (void)path;
    if (out_contents) *out_contents = nob_sv_from_cstr("");
    if (out_found) *out_found = false;
    return true;
}

TEST(evaluator_find_item_commands_resolve_local_paths_and_model_package_root_policies) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("src"));
    ASSERT(nob_mkdir_if_not_exists("build"));
    ASSERT(nob_mkdir_if_not_exists("src/find_items"));
    ASSERT(nob_mkdir_if_not_exists("src/find_items/nested"));
    ASSERT(nob_mkdir_if_not_exists("src/find_items/include"));
    ASSERT(nob_mkdir_if_not_exists("src/find_items/bin"));
    ASSERT(nob_mkdir_if_not_exists("src/find_items/lib"));
    ASSERT(nob_write_entire_file("src/find_items/nested/marker.txt", "x", 1));
    ASSERT(nob_write_entire_file("src/find_items/include/marker.hpp", "x", 1));
#if defined(_WIN32)
    ASSERT(nob_write_entire_file("src/find_items/bin/fake-tool.cmd", "@echo off\r\necho fake-tool\r\n",
                                 strlen("@echo off\r\necho fake-tool\r\n")));
    ASSERT(nob_write_entire_file("src/find_items/lib/sample.lib", "x", 1));
#else
    ASSERT(nob_write_entire_file("src/find_items/bin/fake-tool", "#!/bin/sh\nexit 0\n",
                                 strlen("#!/bin/sh\nexit 0\n")));
    ASSERT(chmod("src/find_items/bin/fake-tool", 0755) == 0);
    ASSERT(nob_write_entire_file("src/find_items/lib/libsample.a", "x", 1));
#endif

    ASSERT(nob_mkdir_if_not_exists("src/foo_root"));
    ASSERT(nob_mkdir_if_not_exists("src/foo_root/include"));
    ASSERT(nob_mkdir_if_not_exists("src/foo_root/lib"));
    ASSERT(nob_mkdir_if_not_exists("src/foo_root/lib/cmake"));
    ASSERT(nob_mkdir_if_not_exists("src/foo_root/lib/cmake/Foo"));
    ASSERT(nob_write_entire_file("src/foo_root/include/foo-marker.h", "x", 1));
    ASSERT(nob_write_entire_file("src/foo_root/lib/cmake/Foo/FooConfig.cmake",
                                 "find_path(FOO_INCLUDE_DIR NAMES foo-marker.h)\n",
                                 strlen("find_path(FOO_INCLUDE_DIR NAMES foo-marker.h)\n")));

    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);
    char source_root[_TINYDIR_PATH_MAX] = {0};
    char binary_root[_TINYDIR_PATH_MAX] = {0};
    char foo_root_abs[_TINYDIR_PATH_MAX] = {0};
    ASSERT(snprintf(source_root, sizeof(source_root), "%s/src", cwd) > 0);
    ASSERT(snprintf(binary_root, sizeof(binary_root), "%s/build", cwd) > 0);
    ASSERT(snprintf(foo_root_abs, sizeof(foo_root_abs), "%s/foo_root", source_root) > 0);

    char script[4096];
    int n = snprintf(
        script,
        sizeof(script),
        "find_file(MY_FILE NAMES marker.txt PATHS find_items PATH_SUFFIXES nested NO_DEFAULT_PATH)\n"
        "find_path(MY_PATH NAMES marker.hpp PATHS find_items PATH_SUFFIXES include NO_DEFAULT_PATH)\n"
        "find_program(MY_TOOL NAMES fake-tool fake-tool.cmd PATHS find_items/bin NO_DEFAULT_PATH)\n"
        "find_library(MY_LIB NAMES sample PATHS find_items/lib NO_DEFAULT_PATH)\n"
        "set(FOO_ROOT \"%s\")\n"
        "cmake_policy(SET CMP0074 NEW)\n"
        "cmake_policy(SET CMP0144 NEW)\n"
        "find_package(Foo CONFIG PATHS foo_root/lib/cmake NO_DEFAULT_PATH)\n",
        foo_root_abs);
    ASSERT(n > 0 && n < (int)sizeof(script));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(source_root);
    init.binary_dir = nob_sv_from_cstr(binary_root);
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("MY_FILE")),
                           "find_items/nested/marker.txt"));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("MY_PATH")),
                           "find_items/include"));
#if defined(_WIN32)
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("MY_TOOL")),
                           "find_items/bin/fake-tool.cmd"));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("MY_LIB")),
                           "find_items/lib/sample.lib"));
#else
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("MY_TOOL")),
                           "find_items/bin/fake-tool"));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("MY_LIB")),
                           "find_items/lib/libsample.a"));
#endif
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("FOO_INCLUDE_DIR")),
                           "foo_root/include"));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_command_rejects_unknown_option) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("find_items"));
    ASSERT(nob_mkdir_if_not_exists("find_items2"));
    ASSERT(nob_write_entire_file("find_items2/marker.txt", "x", 1));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "find_file(FOO NAMES marker.txt NO_CACHE UNKNOWN_OPTION)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_unknown = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("find_file"))) continue;
        if (!nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_UNEXPECTED_ARGUMENT"))) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*() received unknown option"))) {
            saw_unknown = true;
        }
    }
    ASSERT(saw_unknown);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_command_rejects_missing_output_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "find_file(NAMES marker.txt)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_missing = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("find_file"))) continue;
        if (!nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_MISSING_REQUIRED"))) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*() requires an output variable"))) {
            saw_missing = true;
        }
    }
    ASSERT(saw_missing);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_command_rejects_missing_registry_or_validator_values) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("find_items3"));
    ASSERT(nob_write_entire_file("find_items3/marker.txt", "x", 1));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "find_file(REG_VIEW marker.txt REGISTRY_VIEW)\n"
        "find_file(VAL_VIEW marker.txt VALIDATOR)\n"
        "find_file(DOC_ONLY marker.txt DOC)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_registry = false;
    bool saw_validator = false;
    bool saw_doc_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("find_file"))) continue;
        if (!nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_MISSING_REQUIRED"))) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*(REGISTRY_VIEW) requires a value"))) {
            saw_registry = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*(VALIDATOR) requires a function name"))) {
            saw_validator = true;
        } else {
            saw_doc_error = true;
        }
    }
    ASSERT(saw_registry);
    ASSERT(saw_validator);
    ASSERT(!saw_doc_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_item_command_rejects_malformed_env_clause) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "find_file(BAD_ENV_END NAMES ENV)\n"
        "find_file(BAD_ENV_KEYWORD NAMES ENV PATHS find_items2)\n");
    Eval_Result result = eval_test_run(ctx, root);
    ASSERT(result.kind == EVAL_RESULT_SOFT_ERROR);

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    size_t malformed_env_diags = 0;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("find_file"))) continue;
        if (!nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_MISSING_REQUIRED"))) continue;
        if (!nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_*(ENV) requires an environment variable name"))) continue;
        malformed_env_diags++;
    }
    ASSERT(malformed_env_diags == 2);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_filename_component_covers_documented_modes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("gfc_real"));
    ASSERT(nob_mkdir_if_not_exists("gfc_real/sub"));
    ASSERT(nob_mkdir_if_not_exists("gfc spaced"));
    ASSERT(nob_write_entire_file("gfc_real/sub/file.txt", "x", 1));
#if defined(_WIN32)
    ASSERT(nob_write_entire_file("gfc spaced/tool spaced.bat", "@echo off\r\n", strlen("@echo off\r\n")));
#else
    ASSERT(nob_write_entire_file("gfc spaced/tool spaced.sh", "#!/bin/sh\n", strlen("#!/bin/sh\n")));
#endif

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "get_filename_component(GFC_DIR \"a/b/c.tar.gz\" DIRECTORY)\n"
        "get_filename_component(GFC_PATH \"a/b/\" PATH)\n"
        "get_filename_component(GFC_NAME \"a/b/c.tar.gz\" NAME)\n"
        "get_filename_component(GFC_EXT \"a/b/c.tar.gz\" EXT)\n"
        "get_filename_component(GFC_LAST_EXT \"a/b/c.tar.gz\" LAST_EXT)\n"
        "get_filename_component(GFC_NAME_WE \"a/b/c.tar.gz\" NAME_WE)\n"
        "get_filename_component(GFC_NAME_WLE \"a/b/c.tar.gz\" NAME_WLE CACHE)\n"
        "get_filename_component(GFC_ABS sub/file.txt ABSOLUTE BASE_DIR gfc_real)\n"
        "get_filename_component(GFC_REAL \"gfc_real/./sub/../sub/file.txt\" REALPATH)\n"
        "set(GFC_SPACE_ARGS sentinel-space)\n"
        "set(GFC_MISSING_ARGS sentinel-missing)\n"
        "set(GFC_CACHE_HIT keep-me)\n"
        "set(GFC_CACHE_HIT_ARGS keep-args)\n"
#if defined(_WIN32)
        "get_filename_component(GFC_PROG \"cmd /C echo\" PROGRAM PROGRAM_ARGS GFC_PROG_ARGS)\n"
        "get_filename_component(GFC_SPACE_PROG \"gfc spaced/tool spaced.bat\" PROGRAM PROGRAM_ARGS GFC_SPACE_ARGS)\n"
        "get_filename_component(GFC_CACHE_HIT \"cmd /C echo\" PROGRAM PROGRAM_ARGS GFC_CACHE_HIT_ARGS CACHE)\n"
        "get_filename_component(GFC_CACHE_PROG \"cmd /C echo\" PROGRAM PROGRAM_ARGS GFC_CACHE_PROG_ARGS CACHE)\n"
#else
        "get_filename_component(GFC_PROG \"sh -c echo\" PROGRAM PROGRAM_ARGS GFC_PROG_ARGS)\n"
        "get_filename_component(GFC_SPACE_PROG \"gfc spaced/tool spaced.sh\" PROGRAM PROGRAM_ARGS GFC_SPACE_ARGS)\n"
        "get_filename_component(GFC_CACHE_HIT \"sh -c echo\" PROGRAM PROGRAM_ARGS GFC_CACHE_HIT_ARGS CACHE)\n"
        "get_filename_component(GFC_CACHE_PROG \"sh -c echo\" PROGRAM PROGRAM_ARGS GFC_CACHE_PROG_ARGS CACHE)\n"
#endif
        "get_filename_component(GFC_MISSING_PROG \"./gfc_missing_program --flag\" PROGRAM PROGRAM_ARGS GFC_MISSING_ARGS)\n"
    );
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_DIR")), nob_sv_from_cstr("a/b")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_PATH")), nob_sv_from_cstr("a")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_NAME")), nob_sv_from_cstr("c.tar.gz")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_EXT")), nob_sv_from_cstr(".tar.gz")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_LAST_EXT")), nob_sv_from_cstr(".gz")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_NAME_WE")), nob_sv_from_cstr("c")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_NAME_WLE")), nob_sv_from_cstr("c.tar")));
    ASSERT(eval_test_cache_defined(ctx, nob_sv_from_cstr("GFC_NAME_WLE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_ABS")),
                     nob_sv_from_cstr("gfc_real/sub/file.txt")));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_REAL")), "gfc_real/sub/file.txt"));
#if defined(_WIN32)
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_PROG")), "cmd.exe"));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_PROG_ARGS")), nob_sv_from_cstr(" /C echo")));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_SPACE_PROG")),
                           "gfc spaced/tool spaced.bat"));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_PROG")), "cmd.exe"));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_PROG_ARGS")),
                     nob_sv_from_cstr(" /C echo")));
#else
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_PROG")), "/sh"));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_PROG_ARGS")), nob_sv_from_cstr(" -c echo")));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_SPACE_PROG")),
                           "gfc spaced/tool spaced.sh"));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_PROG")), "/sh"));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_PROG_ARGS")),
                     nob_sv_from_cstr(" -c echo")));
#endif
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_SPACE_ARGS")),
                     nob_sv_from_cstr("sentinel-space")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_MISSING_PROG")), nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_MISSING_ARGS")),
                     nob_sv_from_cstr("sentinel-missing")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_HIT")),
                     nob_sv_from_cstr("keep-me")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GFC_CACHE_HIT_ARGS")),
                     nob_sv_from_cstr("keep-args")));
    ASSERT(!eval_test_cache_defined(ctx, nob_sv_from_cstr("GFC_CACHE_HIT")));
    ASSERT(!eval_test_cache_defined(ctx, nob_sv_from_cstr("GFC_CACHE_HIT_ARGS")));
    ASSERT(eval_test_cache_defined(ctx, nob_sv_from_cstr("GFC_CACHE_PROG")));
    ASSERT(eval_test_cache_defined(ctx, nob_sv_from_cstr("GFC_CACHE_PROG_ARGS")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_filename_component_rejects_invalid_option_shapes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "get_filename_component(ONLY_TWO file)\n"
        "get_filename_component(BAD_DIR a/b DIRECTORY EXTRA)\n"
        "get_filename_component(BAD_ABS foo ABSOLUTE BASE_DIR)\n"
        "get_filename_component(BAD_PROG foo PROGRAM PROGRAM_ARGS)\n"
        "get_filename_component(BAD_MODE foo UNKNOWN)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 5);

    bool saw_missing_core_args = false;
    bool saw_bad_directory_extra = false;
    bool saw_missing_base_dir = false;
    bool saw_missing_program_args_var = false;
    bool saw_bad_mode = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("get_filename_component() requires <var> <file> <component>"))) {
            saw_missing_core_args = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_filename_component(DIRECTORY) received unexpected argument"))) {
            saw_bad_directory_extra = true;
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("EXTRA")));
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_filename_component(BASE_DIR) requires a value"))) {
            saw_missing_base_dir = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_filename_component(PROGRAM_ARGS) requires an output variable"))) {
            saw_missing_program_args_var = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_filename_component() unsupported component"))) {
            saw_bad_mode = true;
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("UNKNOWN")));
        }
    }

    ASSERT(saw_missing_core_args);
    ASSERT(saw_bad_directory_extra);
    ASSERT(saw_missing_base_dir);
    ASSERT(saw_missing_program_args_var);
    ASSERT(saw_bad_mode);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_no_module_names_configs_path_suffixes_and_registry_view) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY fp_cfg_root/sfx)\n"
        "file(WRITE fp_cfg_root/sfx/AltCfg.cmake [=[set(DemoFP_FOUND 1)\n"
        "set(DemoFP_SOURCE config)\n"
        "]=])\n"
        "find_package(DemoFP NO_MODULE NAMES AltName CONFIGS AltCfg.cmake PATH_SUFFIXES sfx PATHS fp_cfg_root REGISTRY_VIEW HOST QUIET)\n"
        "add_executable(fp_cfg_probe main.c)\n"
        "target_compile_definitions(fp_cfg_probe PRIVATE FOUND=${DemoFP_FOUND} SRC=${DemoFP_SOURCE} RV=${DemoFP_FIND_REGISTRY_VIEW})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_find_event = false;
    bool saw_found = false;
    bool saw_src_config = false;
    bool saw_rv_host = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("DemoFP"))) {
            saw_find_event = true;
            ASSERT(ev->as.package_find_result.found);
            ASSERT(nob_sv_eq(ev->as.package_find_result.mode, nob_sv_from_cstr("CONFIG")));
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("fp_cfg_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOUND=1"))) saw_found = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SRC=config"))) saw_src_config = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RV=HOST"))) saw_rv_host = true;
        }
    }

    ASSERT(saw_find_event);
    ASSERT(saw_found);
    ASSERT(saw_src_config);
    ASSERT(saw_rv_host);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_auto_prefers_config_when_requested) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY fp_pref/mod)\n"
        "file(MAKE_DIRECTORY fp_pref/cfg)\n"
        "file(WRITE fp_pref/mod/FindPrefPkg.cmake [=[set(PrefPkg_FOUND 1)\n"
        "set(PrefPkg_FROM module)\n"
        "]=])\n"
        "file(WRITE fp_pref/cfg/PrefPkgConfig.cmake [=[set(PrefPkg_FOUND 1)\n"
        "set(PrefPkg_FROM config)\n"
        "]=])\n"
        "set(CMAKE_MODULE_PATH fp_pref/mod)\n"
        "set(CMAKE_PREFIX_PATH fp_pref/cfg)\n"
        "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"
        "find_package(PrefPkg QUIET)\n"
        "add_executable(fp_pref_probe main.c)\n"
        "target_compile_definitions(fp_pref_probe PRIVATE FROM=${PrefPkg_FROM})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_config_location = false;
    bool saw_from_config = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("PrefPkg"))) {
            saw_config_location =
                nob_sv_eq(ev->as.package_find_result.location, nob_sv_from_cstr("./fp_pref/cfg/PrefPkgConfig.cmake"));
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("fp_pref_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FROM=config"))) {
            saw_from_config = true;
        }
    }

    ASSERT(saw_config_location);
    ASSERT(saw_from_config);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_cmp0074_old_ignores_root_and_new_uses_root) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY fp_cmp0074_old_root)\n"
        "file(MAKE_DIRECTORY fp_cmp0074_old_prefix)\n"
        "file(WRITE fp_cmp0074_old_root/Cmp0074OldConfig.cmake [=[set(Cmp0074Old_FOUND 1)\n"
        "set(Cmp0074Old_FROM root)\n"
        "]=])\n"
        "file(WRITE fp_cmp0074_old_prefix/Cmp0074OldConfig.cmake [=[set(Cmp0074Old_FOUND 1)\n"
        "set(Cmp0074Old_FROM prefix)\n"
        "]=])\n"
        "set(Cmp0074Old_ROOT fp_cmp0074_old_root)\n"
        "set(CMAKE_PREFIX_PATH fp_cmp0074_old_prefix)\n"
        "cmake_policy(SET CMP0074 OLD)\n"
        "find_package(Cmp0074Old CONFIG QUIET)\n"
        "file(MAKE_DIRECTORY fp_cmp0074_new_root)\n"
        "file(MAKE_DIRECTORY fp_cmp0074_new_prefix)\n"
        "file(WRITE fp_cmp0074_new_root/Cmp0074NewConfig.cmake [=[set(Cmp0074New_FOUND 1)\n"
        "set(Cmp0074New_FROM root)\n"
        "]=])\n"
        "file(WRITE fp_cmp0074_new_prefix/Cmp0074NewConfig.cmake [=[set(Cmp0074New_FOUND 1)\n"
        "set(Cmp0074New_FROM prefix)\n"
        "]=])\n"
        "set(Cmp0074New_ROOT fp_cmp0074_new_root)\n"
        "set(CMAKE_PREFIX_PATH fp_cmp0074_new_prefix)\n"
        "cmake_policy(SET CMP0074 NEW)\n"
        "find_package(Cmp0074New CONFIG QUIET)\n"
        "add_executable(fp_cmp0074_probe main.c)\n"
        "target_compile_definitions(fp_cmp0074_probe PRIVATE OLD_FROM=${Cmp0074Old_FROM} NEW_FROM=${Cmp0074New_FROM})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_old_location = false;
    bool saw_new_location = false;
    bool saw_old_from_prefix = false;
    bool saw_new_from_root = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("Cmp0074Old"))) {
            saw_old_location =
                nob_sv_eq(ev->as.package_find_result.location, nob_sv_from_cstr("./fp_cmp0074_old_prefix/Cmp0074OldConfig.cmake"));
        }
        if (ev->h.kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("Cmp0074New"))) {
            saw_new_location =
                nob_sv_eq(ev->as.package_find_result.location, nob_sv_from_cstr("./fp_cmp0074_new_root/Cmp0074NewConfig.cmake"));
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("fp_cmp0074_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_FROM=prefix"))) {
                saw_old_from_prefix = true;
            }
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_FROM=root"))) {
                saw_new_from_root = true;
            }
        }
    }

    ASSERT(saw_old_location);
    ASSERT(saw_new_location);
    ASSERT(saw_old_from_prefix);
    ASSERT(saw_new_from_root);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_uses_export_package_registry_and_respects_cmp0090_gates) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE NobifyPkgRegOffConfig.cmake [=[set(NobifyPkgRegOff_FOUND 1)\n"
        "set(NobifyPkgRegOff_FROM registry-off)\n"
        "]=])\n"
        "file(WRITE NobifyPkgRegOnConfig.cmake [=[set(NobifyPkgRegOn_FOUND 1)\n"
        "set(NobifyPkgRegOn_FROM registry-on)\n"
        "]=])\n"
        "file(WRITE NobifyPkgRegBlockedConfig.cmake [=[set(NobifyPkgRegBlocked_FOUND 1)\n"
        "set(NobifyPkgRegBlocked_FROM registry-blocked)\n"
        "]=])\n"
        "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"
        "set(CMAKE_PREFIX_PATH \"\")\n"
        "cmake_policy(SET CMP0090 NEW)\n"
        "export(PACKAGE NobifyPkgRegOff)\n"
        "find_package(NobifyPkgRegOff CONFIG QUIET)\n"
        "set(CMAKE_EXPORT_PACKAGE_REGISTRY TRUE)\n"
        "export(PACKAGE NobifyPkgRegOn)\n"
        "find_package(NobifyPkgRegOn CONFIG QUIET)\n"
        "unset(CMAKE_EXPORT_PACKAGE_REGISTRY)\n"
        "set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY TRUE)\n"
        "export(PACKAGE NobifyPkgRegBlocked)\n"
        "find_package(NobifyPkgRegBlocked CONFIG QUIET)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NobifyPkgRegOff_FOUND")),
                     nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NobifyPkgRegOn_FOUND")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NobifyPkgRegOn_FROM")),
                     nob_sv_from_cstr("registry-on")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NobifyPkgRegBlocked_FOUND")),
                     nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_MODE")),
                     nob_sv_from_cstr("PACKAGE")));

    bool saw_on_registry_location = false;
    bool saw_off_not_found = false;
    bool saw_blocked_not_found = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_FIND_PACKAGE) continue;
        if (nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("NobifyPkgRegOff"))) {
            saw_off_not_found = !ev->as.package_find_result.found;
        } else if (nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("NobifyPkgRegOn"))) {
            saw_on_registry_location =
                ev->as.package_find_result.found &&
                sv_contains_sv(ev->as.package_find_result.location, nob_sv_from_cstr("NobifyPkgRegOnConfig.cmake"));
        } else if (nob_sv_eq(ev->as.package_find_result.package_name, nob_sv_from_cstr("NobifyPkgRegBlocked"))) {
            saw_blocked_not_found = !ev->as.package_find_result.found;
        }
    }

    ASSERT(saw_off_not_found);
    ASSERT(saw_on_registry_location);
    ASSERT(saw_blocked_not_found);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_full_signature_and_variable_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    ASSERT(nob_mkdir_if_not_exists("subproj"));
    const char *sub_cmake =
        "project(SubProj DESCRIPTION subdesc HOMEPAGE_URL https://sub LANGUAGES NONE)\n"
        "add_executable(sub_probe sub.c)\n"
        "target_compile_definitions(sub_probe PRIVATE SUB_TOP=${PROJECT_IS_TOP_LEVEL} SUB_NAMED_TOP=${SubProj_IS_TOP_LEVEL} ROOT_NAME=${CMAKE_PROJECT_NAME} SUB_HOME=${PROJECT_HOMEPAGE_URL})\n";
    ASSERT(nob_write_entire_file("subproj/CMakeLists.txt", sub_cmake, strlen(sub_cmake)));

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(MainProj VERSION 1.2.3.4 DESCRIPTION rootdesc HOMEPAGE_URL https://root LANGUAGES C)\n"
        "add_executable(root_probe main.c)\n"
        "target_compile_definitions(root_probe PRIVATE ROOT_TOP=${PROJECT_IS_TOP_LEVEL} ROOT_NAMED_TOP=${MainProj_IS_TOP_LEVEL} ROOT_MAJOR=${PROJECT_VERSION_MAJOR} ROOT_MINOR=${PROJECT_VERSION_MINOR} ROOT_PATCH=${PROJECT_VERSION_PATCH} ROOT_TWEAK=${PROJECT_VERSION_TWEAK} ROOT_CMAKE_VER=${CMAKE_PROJECT_VERSION} ROOT_HOME=${PROJECT_HOMEPAGE_URL})\n"
        "add_subdirectory(subproj)\n"
        "target_compile_definitions(root_probe PRIVATE ROOT_NAME_AFTER=${CMAKE_PROJECT_NAME} ROOT_HOME_AFTER=${CMAKE_PROJECT_HOMEPAGE_URL})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_main_project_event = false;
    bool saw_sub_project_event = false;
    bool saw_root_top = false;
    bool saw_root_named_top = false;
    bool saw_root_major = false;
    bool saw_root_minor = false;
    bool saw_root_patch = false;
    bool saw_root_tweak = false;
    bool saw_root_cmake_ver = false;
    bool saw_root_home = false;
    bool saw_root_name_after = false;
    bool saw_root_home_after = false;
    bool saw_sub_top_false = false;
    bool saw_sub_named_top_false = false;
    bool saw_sub_root_name = false;
    bool saw_sub_home = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_PROJECT_DECLARE) {
            if (nob_sv_eq(ev->as.project_declare.name, nob_sv_from_cstr("MainProj")) &&
                nob_sv_eq(ev->as.project_declare.version, nob_sv_from_cstr("1.2.3.4")) &&
                nob_sv_eq(ev->as.project_declare.description, nob_sv_from_cstr("rootdesc")) &&
                nob_sv_eq(ev->as.project_declare.languages, nob_sv_from_cstr("C"))) {
                saw_main_project_event = true;
            } else if (nob_sv_eq(ev->as.project_declare.name, nob_sv_from_cstr("SubProj")) &&
                       nob_sv_eq(ev->as.project_declare.version, nob_sv_from_cstr("")) &&
                       nob_sv_eq(ev->as.project_declare.description, nob_sv_from_cstr("subdesc")) &&
                       nob_sv_eq(ev->as.project_declare.languages, nob_sv_from_cstr(""))) {
                saw_sub_project_event = true;
            }
        }

        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("root_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_TOP=TRUE"))) saw_root_top = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAMED_TOP=TRUE"))) saw_root_named_top = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_MAJOR=1"))) saw_root_major = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_MINOR=2"))) saw_root_minor = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_PATCH=3"))) saw_root_patch = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_TWEAK=4"))) saw_root_tweak = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_CMAKE_VER=1.2.3.4"))) saw_root_cmake_ver = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_HOME=https://root"))) saw_root_home = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAME_AFTER=MainProj"))) saw_root_name_after = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_HOME_AFTER=https://root"))) saw_root_home_after = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("sub_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=FALSE")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=0")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=OFF"))) {
                saw_sub_top_false = true;
            }
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=FALSE")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=0")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=OFF"))) {
                saw_sub_named_top_false = true;
            }
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAME=MainProj"))) saw_sub_root_name = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_HOME=https://sub"))) saw_sub_home = true;
        }
    }

    ASSERT(saw_main_project_event);
    ASSERT(saw_sub_project_event);
    ASSERT(saw_root_top);
    ASSERT(saw_root_named_top);
    ASSERT(saw_root_major);
    ASSERT(saw_root_minor);
    ASSERT(saw_root_patch);
    ASSERT(saw_root_tweak);
    ASSERT(saw_root_cmake_ver);
    ASSERT(saw_root_home);
    ASSERT(saw_root_name_after);
    ASSERT(saw_root_home_after);
    ASSERT(saw_sub_top_false);
    ASSERT(saw_sub_named_top_false);
    ASSERT(saw_sub_root_name);
    ASSERT(saw_sub_home);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_cmp0048_new_clears_and_old_preserves_version_vars_without_version_arg) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0048 NEW)\n"
        "set(PROJECT_VERSION stale)\n"
        "set(PROJECT_VERSION_MAJOR stale_major)\n"
        "project(NewNoVer LANGUAGES NONE)\n"
        "add_executable(project_new_nover main.c)\n"
        "target_compile_definitions(project_new_nover PRIVATE NEW_VER=${PROJECT_VERSION} NEW_MAJ=${PROJECT_VERSION_MAJOR})\n"
        "cmake_policy(SET CMP0048 OLD)\n"
        "set(PROJECT_VERSION keep)\n"
        "set(PROJECT_VERSION_MAJOR keep_major)\n"
        "project(OldNoVer LANGUAGES NONE)\n"
        "add_executable(project_old_nover main.c)\n"
        "target_compile_definitions(project_old_nover PRIVATE OLD_VER=${PROJECT_VERSION} OLD_MAJ=${PROJECT_VERSION_MAJOR})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_new_ver_empty = false;
    bool saw_new_maj_empty = false;
    bool saw_old_ver_keep = false;
    bool saw_old_maj_keep = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("project_new_nover"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_VER="))) saw_new_ver_empty = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_MAJ="))) saw_new_maj_empty = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("project_old_nover"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_VER=keep"))) saw_old_ver_keep = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_MAJ=keep_major"))) saw_old_maj_keep = true;
        }
    }

    ASSERT(saw_new_ver_empty);
    ASSERT(saw_new_maj_empty);
    ASSERT(saw_old_ver_keep);
    ASSERT(saw_old_maj_keep);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_rejects_invalid_signature_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(BadVer VERSION 1.a)\n"
        "project(BadLang LANGUAGES NONE C)\n"
        "project(BadMissingVersion VERSION)\n"
        "project(BadDesc DESCRIPTION)\n"
        "project(BadHome HOMEPAGE_URL)\n"
        "project(BadUnexpected VERSION 1.0 C)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);

    bool saw_bad_version = false;
    bool saw_none_mix = false;
    bool saw_missing_version = false;
    bool saw_missing_desc = false;
    bool saw_missing_home = false;
    bool saw_unexpected = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(VERSION ...) expects numeric components"))) {
            saw_bad_version = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("project() LANGUAGES NONE cannot be combined with other languages"))) {
            saw_none_mix = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(VERSION ...) requires a version value"))) {
            saw_missing_version = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(DESCRIPTION ...) requires a value"))) {
            saw_missing_desc = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(HOMEPAGE_URL ...) requires a value"))) {
            saw_missing_home = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project() received unexpected argument in keyword signature"))) {
            saw_unexpected = true;
        }
    }

    ASSERT(saw_bad_version);
    ASSERT(saw_none_mix);
    ASSERT(saw_missing_version);
    ASSERT(saw_missing_desc);
    ASSERT(saw_missing_home);
    ASSERT(saw_unexpected);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_policy_known_unknown_and_if_predicate) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0077 NEW)\n"
        "cmake_policy(GET CMP0077 POL_OUT)\n"
        "if(POLICY CMP0077)\n"
        "  set(IF_KNOWN 1)\n"
        "endif()\n"
        "if(POLICY CMP9999)\n"
        "  set(IF_UNKNOWN 1)\n"
        "endif()\n"
        "cmake_policy(GET CMP9999 BAD_OUT)\n"
        "add_executable(policy_pred main.c)\n"
        "target_compile_definitions(policy_pred PRIVATE POL_OUT=${POL_OUT} IF_KNOWN=${IF_KNOWN} IF_UNKNOWN=${IF_UNKNOWN})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 1);

    bool saw_pol_out = false;
    bool saw_if_known = false;
    bool saw_if_unknown_empty = false;

    bool saw_unknown_policy_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(GET ...) requires a known CMP policy id"))) {
                saw_unknown_policy_error = true;
            }
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("POL_OUT=NEW"))) {
                saw_pol_out = true;
            } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("IF_KNOWN=1"))) {
                saw_if_known = true;
            } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("IF_UNKNOWN="))) {
                saw_if_unknown_empty = true;
            }
        }
    }
    ASSERT(saw_unknown_policy_error);
    ASSERT(saw_pol_out);
    ASSERT(saw_if_known);
    ASSERT(saw_if_unknown_empty);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_policy_strict_arity_and_version_validation) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(PUSH EXTRA)\n"
        "cmake_policy(POP EXTRA)\n"
        "cmake_policy(SET CMP0077 NEW EXTRA)\n"
        "cmake_policy(GET CMP0077)\n"
        "cmake_policy(VERSION 3.10 3.11)\n"
        "cmake_policy(VERSION 2.3)\n"
        "cmake_policy(VERSION 3.29)\n"
        "cmake_policy(VERSION 3.20...3.10)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 8);

    bool saw_push_arity = false;
    bool saw_pop_arity = false;
    bool saw_set_arity = false;
    bool saw_get_arity = false;
    bool saw_version_arity = false;
    bool saw_min_floor = false;
    bool saw_min_running = false;
    bool saw_max_lt_min = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(PUSH) does not accept extra arguments"))) {
            saw_push_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(POP) does not accept extra arguments"))) {
            saw_pop_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(SET ...) expects exactly policy id and value"))) {
            saw_set_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(GET ...) expects exactly policy id and output variable"))) {
            saw_get_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) expects exactly one version argument"))) {
            saw_version_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) requires minimum version >= 2.4"))) {
            saw_min_floor = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) min version exceeds evaluator baseline"))) {
            saw_min_running = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) requires max version >= min version"))) {
            saw_max_lt_min = true;
        }
    }

    ASSERT(saw_push_arity);
    ASSERT(saw_pop_arity);
    ASSERT(saw_set_arity);
    ASSERT(saw_get_arity);
    ASSERT(saw_version_arity);
    ASSERT(saw_min_floor);
    ASSERT(saw_min_running);
    ASSERT(saw_max_lt_min);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_minimum_required_inside_function_applies_policy_not_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(VERSION 3.10)\n"
        "function(set_local_min)\n"
        "  cmake_minimum_required(VERSION 3.28)\n"
        "endfunction()\n"
        "set_local_min()\n"
        "cmake_policy(GET CMP0124 OUT_POL)\n"
        "add_executable(minreq_func main.c)\n"
        "target_compile_definitions(minreq_func PRIVATE OUT_POL=${OUT_POL} MIN_VER=${CMAKE_MINIMUM_REQUIRED_VERSION})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_out_pol = false;
    bool saw_min_ver_empty = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OUT_POL=NEW"))) {
            saw_out_pol = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("MIN_VER="))) {
            saw_min_ver_empty = true;
        }
    }
    ASSERT(saw_out_pol);
    ASSERT(saw_min_ver_empty);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cpack_commands_require_cpackcomponent_module_and_parse_component_extras) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(CPackGate)\n"
        "cpack_add_component(core)\n"
        "include(CPackComponent)\n"
        "cpack_add_component(core DISPLAY_NAME Core ARCHIVE_FILE core.txz PLIST core.plist)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_gate_error = false;
    bool saw_component_event = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unknown command")) &&
            nob_sv_eq(ev->as.diag.hint,
                      nob_sv_from_cstr("include(CPackComponent) must be called before using this command"))) {
            saw_gate_error = true;
        }
        if (ev->h.kind == EV_CPACK_ADD_COMPONENT &&
            nob_sv_eq(ev->as.cpack_add_component.name, nob_sv_from_cstr("core")) &&
            nob_sv_eq(ev->as.cpack_add_component.archive_file, nob_sv_from_cstr("core.txz")) &&
            nob_sv_eq(ev->as.cpack_add_component.plist, nob_sv_from_cstr("core.plist"))) {
            saw_component_event = true;
        }
    }
    ASSERT(saw_gate_error);
    ASSERT(saw_component_event);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cpack_commands_reject_missing_names_and_warn_on_extra_args) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(CPackDiag)\n"
        "include(CPackComponent)\n"
        "cpack_add_install_type()\n"
        "cpack_add_component_group()\n"
        "cpack_add_component()\n"
        "cpack_add_install_type(Full EXTRA)\n"
        "cpack_add_component_group(base STRAY)\n"
        "cpack_add_component(core EXTRA)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 3);
    ASSERT(report->error_count == 3);

    bool saw_install_type_missing = false;
    bool saw_group_missing = false;
    bool saw_component_missing = false;
    bool saw_install_type_extra = false;
    bool saw_group_extra = false;
    bool saw_component_extra = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cpack_add_install_type() missing name"))) {
                saw_install_type_missing = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cpack_add_component_group() missing name"))) {
                saw_group_missing = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cpack_add_component() missing name"))) {
                saw_component_missing = true;
            }
            continue;
        }
        if (ev->as.diag.severity != EV_DIAG_WARNING) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cpack_add_install_type() unexpected argument")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("EXTRA"))) {
            saw_install_type_extra = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cpack_add_component_group() unexpected argument")) &&
                   nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("STRAY"))) {
            saw_group_extra = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cpack_add_component() unsupported/extra argument")) &&
                   nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("EXTRA"))) {
            saw_component_extra = true;
        }
    }

    ASSERT(saw_install_type_missing);
    ASSERT(saw_group_missing);
    ASSERT(saw_component_missing);
    ASSERT(saw_install_type_extra);
    ASSERT(saw_group_extra);
    ASSERT(saw_component_extra);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_diag_codes_are_explicit_and_report_classes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "unknown_diag_case()\n"
        "cmake_policy(POP)\n"
        "file(READ missing_diag_input.txt OUT_VAR)\n"
        "math()\n"
        "message(WARNING warn-msg)\n"
        "message(SEND_ERROR err-msg)\n"
        "cmake_host_system_information(RESULT HOST_INFO QUERY WINDOWS_REGISTRY HKLM)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 2);
    ASSERT(report->error_count == 4);
    ASSERT(report->input_error_count == 3);
    ASSERT(report->engine_limitation_count == 1);
    ASSERT(report->io_env_error_count == 1);
    ASSERT(report->policy_conflict_count == 1);
    ASSERT(report->unsupported_count == 1);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("HOST_INFO")), nob_sv_from_cstr("")));

    bool saw_unknown_command = false;
    bool saw_policy_conflict = false;
    bool saw_file_io_failure = false;
    bool saw_math_missing_required = false;
    bool saw_message_warning = false;
    bool saw_message_error = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;

        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unknown command"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_WARNING);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_UNKNOWN_COMMAND")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("ENGINE_LIMITATION")));
            saw_unknown_command = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(POP) called without matching PUSH"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_POLICY_CONFLICT")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("POLICY_CONFLICT")));
            saw_policy_conflict = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("file(READ) failed to read file"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_IO_FAILURE")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("IO_ENV_ERROR")));
            saw_file_io_failure = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("math() requires a subcommand"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_MISSING_REQUIRED")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("INPUT_ERROR")));
            saw_math_missing_required = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("warn-msg"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_WARNING);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_SCRIPT_WARNING")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("INPUT_ERROR")));
            saw_message_warning = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("err-msg"))) {
            ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
            ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_SCRIPT_ERROR")));
            ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("INPUT_ERROR")));
            saw_message_error = true;
        }
    }

    ASSERT(saw_unknown_command);
    ASSERT(saw_policy_conflict);
    ASSERT(saw_file_io_failure);
    ASSERT(saw_math_missing_required);
    ASSERT(saw_message_warning);
    ASSERT(saw_message_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_host_system_information_windows_registry_and_fallback_scripts_are_modeled) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_write_entire_file("fallback-distrib.cmake",
                                 "set(CMAKE_GET_OS_RELEASE_FALLBACK_RESULT_ID fallback-os)\n"
                                 "set(CMAKE_GET_OS_RELEASE_FALLBACK_RESULT_VERSION_ID 42)\n",
                                 strlen("set(CMAKE_GET_OS_RELEASE_FALLBACK_RESULT_ID fallback-os)\nset(CMAKE_GET_OS_RELEASE_FALLBACK_RESULT_VERSION_ID 42)\n")));

    Evaluator_Host_Query_Mock_Data mock = {0};
    EvalServices services = {
        .user_data = &mock,
        .host_query_windows_registry = evaluator_host_query_windows_registry_mock,
        .host_read_file = evaluator_host_read_file_force_missing_os_release,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_GET_OS_RELEASE_FALLBACK_SCRIPTS fallback-distrib.cmake)\n"
        "cmake_host_system_information(RESULT HOST_VALUE QUERY WINDOWS_REGISTRY HKLM/Software/Demo VALUE InstallDir VIEW 64_32 ERROR_VARIABLE HOST_ERR)\n"
        "cmake_host_system_information(RESULT HOST_NAMES QUERY WINDOWS_REGISTRY HKLM/Software/Demo VALUE_NAMES)\n"
        "cmake_host_system_information(RESULT HOST_KEYS QUERY WINDOWS_REGISTRY HKLM/Software/Demo SUBKEYS SEPARATOR |)\n"
        "cmake_host_system_information(RESULT DISTRO_ID QUERY DISTRIB_ID)\n"
        "cmake_host_system_information(RESULT DISTRO_INFO QUERY DISTRIB_INFO)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("HOST_VALUE")),
                     nob_sv_from_cstr("C:/Registry/Install")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("HOST_NAMES")),
                     nob_sv_from_cstr("InstallDir;SdkRoot")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("HOST_KEYS")),
                     nob_sv_from_cstr("ChildA|ChildB")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("HOST_ERR")),
                     nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_ID")),
                     nob_sv_from_cstr("fallback-os")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_INFO_ID")),
                     nob_sv_from_cstr("fallback-os")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_INFO_VERSION_ID")),
                     nob_sv_from_cstr("42")));

    ASSERT(mock.saw_value);
    ASSERT(mock.saw_value_names);
    ASSERT(mock.saw_subkeys);
    ASSERT(mock.saw_view_64_32);
    ASSERT(mock.saw_separator_pipe);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_string_hash_repeat_and_json_full_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(SJSON [=[{\"k\":[1,2,3],\"s\":\"x\",\"n\":null}]=])\n"
        "string(MD5 H_MD5 \"abc\")\n"
        "string(SHA1 H_SHA1 \"abc\")\n"
        "string(SHA224 H_SHA224 \"abc\")\n"
        "string(SHA256 H_SHA256 \"abc\")\n"
        "string(SHA384 H_SHA384 \"abc\")\n"
        "string(SHA512 H_SHA512 \"abc\")\n"
        "string(SHA3_224 H_SHA3_224 \"abc\")\n"
        "string(SHA3_256 H_SHA3_256 \"abc\")\n"
        "string(SHA3_384 H_SHA3_384 \"abc\")\n"
        "string(SHA3_512 H_SHA3_512 \"abc\")\n"
        "string(REPEAT \"ab\" 3 SREP)\n"
        "string(REPEAT \"x\" 0 SREP0)\n"
        "string(JSON SJ_MEMBER MEMBER \"${SJSON}\" 1)\n"
        "string(JSON SJ_RM REMOVE \"${SJSON}\" k 1)\n"
        "string(JSON SJ_RM_LEN LENGTH \"${SJ_RM}\" k)\n"
        "string(JSON SJ_SET SET \"${SJSON}\" k 5 99)\n"
        "string(JSON SJ_SET_GET GET \"${SJ_SET}\" k 3)\n"
        "string(JSON SJ_EQ EQUAL \"${SJSON}\" \"${SJSON}\")\n"
        "string(JSON SJ_NEQ EQUAL \"${SJSON}\" [=[{\"k\":[1],\"s\":\"x\",\"n\":null}]=])\n"
        "string(JSON SJ_OK ERROR_VARIABLE SJ_ERR_OK TYPE \"${SJSON}\" k)\n"
        "string(JSON SJ_E1 ERROR_VARIABLE SJ_ERR1 GET \"${SJSON}\" missing)\n"
        "string(JSON SJ_E2 ERROR_VARIABLE SJ_ERR2 LENGTH \"${SJSON}\" s)\n"
        "add_executable(string_full_probe main.c)\n"
        "target_compile_definitions(string_full_probe PRIVATE "
        "\"H_MD5=${H_MD5}\" \"H_SHA1=${H_SHA1}\" \"H_SHA224=${H_SHA224}\" \"H_SHA256=${H_SHA256}\" "
        "\"H_SHA384=${H_SHA384}\" \"H_SHA512=${H_SHA512}\" \"H_SHA3_224=${H_SHA3_224}\" "
        "\"H_SHA3_256=${H_SHA3_256}\" \"H_SHA3_384=${H_SHA3_384}\" \"H_SHA3_512=${H_SHA3_512}\" "
        "\"SREP=${SREP}\" \"SREP0=${SREP0}\" \"SJ_MEMBER=${SJ_MEMBER}\" \"SJ_RM_LEN=${SJ_RM_LEN}\" "
        "\"SJ_SET_GET=${SJ_SET_GET}\" \"SJ_EQ=${SJ_EQ}\" \"SJ_NEQ=${SJ_NEQ}\" "
        "\"SJ_ERR_OK=${SJ_ERR_OK}\" \"SJ_E1=${SJ_E1}\" \"SJ_ERR1=${SJ_ERR1}\" \"SJ_E2=${SJ_E2}\" \"SJ_ERR2=${SJ_ERR2}\")\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_md5 = false;
    bool saw_sha1 = false;
    bool saw_sha224 = false;
    bool saw_sha256 = false;
    bool saw_sha384 = false;
    bool saw_sha512 = false;
    bool saw_sha3_224 = false;
    bool saw_sha3_256 = false;
    bool saw_sha3_384 = false;
    bool saw_sha3_512 = false;
    bool saw_repeat = false;
    bool saw_repeat_zero = false;
    bool saw_member = false;
    bool saw_rm_len = false;
    bool saw_set_get = false;
    bool saw_eq = false;
    bool saw_neq = false;
    bool saw_err_ok = false;
    bool saw_e1 = false;
    bool saw_err1 = false;
    bool saw_e2 = false;
    bool saw_err2 = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("string_full_probe"))) continue;
        String_View it = ev->as.target_compile_definitions.item;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_MD5=900150983cd24fb0d6963f7d28e17f72"))) saw_md5 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA1=a9993e364706816aba3e25717850c26c9cd0d89d"))) saw_sha1 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA224=23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"))) saw_sha224 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))) saw_sha256 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA384=cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"))) saw_sha384 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA512=ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"))) saw_sha512 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA3_224=e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf"))) saw_sha3_224 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA3_256=3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"))) saw_sha3_256 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA3_384=ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b298d88cea927ac7f539f1edf228376d25"))) saw_sha3_384 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("H_SHA3_512=b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0"))) saw_sha3_512 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SREP=ababab"))) saw_repeat = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SREP0="))) saw_repeat_zero = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_MEMBER=n"))) saw_member = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_RM_LEN=2"))) saw_rm_len = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_SET_GET=99"))) saw_set_get = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_EQ=ON"))) saw_eq = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_NEQ=OFF"))) saw_neq = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_ERR_OK=NOTFOUND"))) saw_err_ok = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_E1=missing-NOTFOUND"))) saw_e1 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_ERR1=string(JSON) object member not found: missing"))) saw_err1 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_E2=s-NOTFOUND"))) saw_e2 = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("SJ_ERR2=string(JSON LENGTH) requires ARRAY or OBJECT"))) saw_err2 = true;
    }

    ASSERT(saw_md5);
    ASSERT(saw_sha1);
    ASSERT(saw_sha224);
    ASSERT(saw_sha256);
    ASSERT(saw_sha384);
    ASSERT(saw_sha512);
    ASSERT(saw_sha3_224);
    ASSERT(saw_sha3_256);
    ASSERT(saw_sha3_384);
    ASSERT(saw_sha3_512);
    ASSERT(saw_repeat);
    ASSERT(saw_repeat_zero);
    ASSERT(saw_member);
    ASSERT(saw_rm_len);
    ASSERT(saw_set_get);
    ASSERT(saw_eq);
    ASSERT(saw_neq);
    ASSERT(saw_err_ok);
    ASSERT(saw_e1);
    ASSERT(saw_err1);
    ASSERT(saw_e2);
    ASSERT(saw_err2);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_string_text_regex_and_misc_dispatch_events) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    evaluator_set_source_date_epoch_value("946684800");
    Ast_Root root = parse_cmake(
        temp_arena,
        "set(SBUF \"Hi\")\n"
        "string(APPEND SBUF \"-There\")\n"
        "string(JOIN \":\" S_JOIN alpha beta gamma)\n"
        "set(V \"qq\")\n"
        "string(CONFIGURE \"@V@-${V}\" S_CFG @ONLY)\n"
        "string(REPLACE \"na\" \"X\" S_REPL \"banana\")\n"
        "string(TOLOWER \"MiX\" S_LOW)\n"
        "string(SUBSTRING \"abcdef\" 2 3 S_SUB)\n"
        "string(REGEX MATCH \"a[0-9]+\" S_RX_MATCH \"xxa12yy\")\n"
        "string(REGEX REPLACE \"a([0-9]+)\" \"B\\1\" S_RX_REPL \"xa12ya34\")\n"
        "string(REGEX MATCHALL \"[a-z][0-9]\" S_RX_ALL \"a1b2c3\")\n"
        "string(RANDOM LENGTH 6 ALPHABET abc RANDOM_SEED 7 S_RAND)\n"
        "string(TIMESTAMP S_TS \"%Y\" UTC)\n"
        "string(UUID S_UUID3 NAMESPACE \"6ba7b810-9dad-11d1-80b4-00c04fd430c8\" NAME \"demo\" TYPE MD5)\n"
        "string(UUID S_UUID5 NAMESPACE \"6ba7b810-9dad-11d1-80b4-00c04fd430c8\" NAME \"demo\" TYPE SHA1)\n"
        "string(SHA256 S_SHA \"abc\")\n"
        "add_executable(string_split_probe main.c)\n"
        "target_compile_definitions(string_split_probe PRIVATE "
        "\"SBUF=${SBUF}\" \"S_JOIN=${S_JOIN}\" \"S_CFG=${S_CFG}\" \"S_REPL=${S_REPL}\" "
        "\"S_LOW=${S_LOW}\" \"S_SUB=${S_SUB}\" \"S_RX_MATCH=${S_RX_MATCH}\" "
        "\"S_RX_REPL=${S_RX_REPL}\" \"S_RX_ALL=${S_RX_ALL}\" \"S_RAND=${S_RAND}\" "
        "\"S_TS=${S_TS}\" \"S_UUID3=${S_UUID3}\" \"S_UUID5=${S_UUID5}\" \"S_SHA=${S_SHA}\")\n");
    Eval_Result run_res = eval_test_run(ctx, root);
    evaluator_set_source_date_epoch_value(NULL);

    ASSERT(!eval_result_is_fatal(run_res));
    ASSERT(run_res.kind == EVAL_RESULT_OK);

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_append = false;
    bool saw_join = false;
    bool saw_configure = false;
    bool saw_replace = false;
    bool saw_lower = false;
    bool saw_substring = false;
    bool saw_regex_match = false;
    bool saw_regex_replace = false;
    bool saw_regex_all = false;
    bool saw_random = false;
    bool saw_timestamp = false;
    bool saw_uuid3 = false;
    bool saw_uuid5 = false;
    bool saw_hash = false;

    bool saw_configure_event = false;
    bool saw_replace_event = false;
    bool saw_regex_match_event = false;
    bool saw_regex_replace_event = false;
    bool saw_hash_event = false;
    bool saw_timestamp_event = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("string_split_probe"))) {
            String_View it = ev->as.target_compile_definitions.item;
            if (nob_sv_eq(it, nob_sv_from_cstr("SBUF=Hi-There"))) saw_append = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_JOIN=alpha:beta:gamma"))) saw_join = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_CFG=qq-qq"))) saw_configure = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_REPL=baXX"))) saw_replace = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_LOW=mix"))) saw_lower = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_SUB=cde"))) saw_substring = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_RX_MATCH=a12"))) saw_regex_match = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_RX_REPL=xB12yB34"))) saw_regex_replace = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_RX_ALL=a1;b2;c3"))) saw_regex_all = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_RAND=bcaacb"))) saw_random = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_TS=2000"))) saw_timestamp = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_UUID3=302a4211-9878-3ad9-b7bf-c706364668a5"))) saw_uuid3 = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_UUID5=6bc1814c-5705-5b6e-af99-104b91962282"))) saw_uuid5 = true;
            if (nob_sv_eq(it, nob_sv_from_cstr("S_SHA=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))) saw_hash = true;
        }

        if (ev->h.kind == EVENT_STRING_CONFIGURE &&
            nob_sv_eq(ev->as.string_configure.out_var, nob_sv_from_cstr("S_CFG"))) {
            saw_configure_event = true;
        }
        if (ev->h.kind == EVENT_STRING_REPLACE &&
            nob_sv_eq(ev->as.string_replace.out_var, nob_sv_from_cstr("S_REPL"))) {
            saw_replace_event = true;
        }
        if (ev->h.kind == EVENT_STRING_REGEX &&
            nob_sv_eq(ev->as.string_regex.mode, nob_sv_from_cstr("MATCH")) &&
            nob_sv_eq(ev->as.string_regex.out_var, nob_sv_from_cstr("S_RX_MATCH"))) {
            saw_regex_match_event = true;
        }
        if (ev->h.kind == EVENT_STRING_REGEX &&
            nob_sv_eq(ev->as.string_regex.mode, nob_sv_from_cstr("REPLACE")) &&
            nob_sv_eq(ev->as.string_regex.out_var, nob_sv_from_cstr("S_RX_REPL"))) {
            saw_regex_replace_event = true;
        }
        if (ev->h.kind == EVENT_STRING_HASH &&
            nob_sv_eq(ev->as.string_hash.algorithm, nob_sv_from_cstr("SHA256")) &&
            nob_sv_eq(ev->as.string_hash.out_var, nob_sv_from_cstr("S_SHA"))) {
            saw_hash_event = true;
        }
        if (ev->h.kind == EVENT_STRING_TIMESTAMP &&
            nob_sv_eq(ev->as.string_timestamp.out_var, nob_sv_from_cstr("S_TS"))) {
            saw_timestamp_event = true;
        }
    }

    ASSERT(saw_append);
    ASSERT(saw_join);
    ASSERT(saw_configure);
    ASSERT(saw_replace);
    ASSERT(saw_lower);
    ASSERT(saw_substring);
    ASSERT(saw_regex_match);
    ASSERT(saw_regex_replace);
    ASSERT(saw_regex_all);
    ASSERT(saw_random);
    ASSERT(saw_timestamp);
    ASSERT(saw_uuid3);
    ASSERT(saw_uuid5);
    ASSERT(saw_hash);

    ASSERT(saw_configure_event);
    ASSERT(saw_replace_event);
    ASSERT(saw_regex_match_event);
    ASSERT(saw_regex_replace_event);
    ASSERT(saw_hash_event);
    ASSERT(saw_timestamp_event);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_string_regex_parse_error_keeps_diag_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "string(REGEX MATCH \"[\" BAD_RX \"abc\")\n");
    Eval_Result run_res = eval_test_run(ctx, root);

    ASSERT(!eval_result_is_fatal(run_res));
    ASSERT(run_res.kind == EVAL_RESULT_SOFT_ERROR);

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);
    ASSERT(report->warning_count == 0);
    ASSERT(report->input_error_count == 1);

    bool saw_parse_diag = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (!nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Invalid regex pattern"))) continue;
        ASSERT(ev->as.diag.severity == EV_DIAG_ERROR);
        ASSERT(nob_sv_eq(ev->as.diag.code, nob_sv_from_cstr("EVAL_DIAG_PARSE_ERROR")));
        ASSERT(nob_sv_eq(ev->as.diag.error_class, nob_sv_from_cstr("INPUT_ERROR")));
        ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("[")));
        saw_parse_diag = true;
    }
    ASSERT(saw_parse_diag);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_string_find_compare_configure_random_timestamp_and_uuid_cover_remaining_option_modes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    evaluator_set_source_date_epoch_value("946684800");
    Ast_Root root = parse_cmake(
        temp_arena,
        "set(MSG [=[say \"hi\"]=])\n"
        "string(FIND \"banana\" \"na\" FIND_FWD)\n"
        "string(FIND \"banana\" \"na\" FIND_REV REVERSE)\n"
        "string(FIND \"banana\" \"zz\" FIND_NONE)\n"
        "string(FIND \"banana\" \"\" FIND_EMPTY_REV REVERSE)\n"
        "string(COMPARE LESS \"abc\" \"abd\" CMP_LESS)\n"
        "string(COMPARE GREATER \"abc\" \"abd\" CMP_GREATER_FALSE)\n"
        "string(COMPARE EQUAL \"abc\" \"abc\" CMP_EQUAL)\n"
        "string(COMPARE NOTEQUAL \"abc\" \"abc\" CMP_NOTEQUAL_FALSE)\n"
        "string(COMPARE LESS_EQUAL \"abd\" \"abd\" CMP_LESS_EQUAL)\n"
        "string(COMPARE GREATER_EQUAL \"abc\" \"abd\" CMP_GREATER_EQUAL_FALSE)\n"
        "string(CONFIGURE \"\\${MSG}-@MSG@\" CFG_BOTH ESCAPE_QUOTES)\n"
        "string(CONFIGURE \"\\${MSG}-@MSG@\" CFG_AT_ONLY @ONLY ESCAPE_QUOTES)\n"
        "string(RANDOM RANDOM_SEED 9 RAND_A)\n"
        "string(RANDOM RANDOM_SEED 9 RAND_B)\n"
        "string(TIMESTAMP TS_DEFAULT UTC)\n"
        "string(UUID UUID_UP NAMESPACE \"6ba7b810-9dad-11d1-80b4-00c04fd430c8\" NAME \"abc\" TYPE SHA1 UPPER)\n");
    Eval_Result run_res = eval_test_run(ctx, root);
    evaluator_set_source_date_epoch_value(NULL);

    ASSERT(!eval_result_is_fatal(run_res));
    ASSERT(run_res.kind == EVAL_RESULT_OK);

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("FIND_FWD")), nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("FIND_REV")), nob_sv_from_cstr("4")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("FIND_NONE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("FIND_EMPTY_REV")), nob_sv_from_cstr("6")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMP_LESS")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMP_GREATER_FALSE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMP_EQUAL")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMP_NOTEQUAL_FALSE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMP_LESS_EQUAL")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMP_GREATER_EQUAL_FALSE")), nob_sv_from_cstr("0")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_BOTH")),
                     nob_sv_from_cstr("say \\\"hi\\\"-say \\\"hi\\\"")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_AT_ONLY")),
                     nob_sv_from_cstr("${MSG}-say \\\"hi\\\"")));

    String_View rand_a = eval_test_var_get(ctx, nob_sv_from_cstr("RAND_A"));
    String_View rand_b = eval_test_var_get(ctx, nob_sv_from_cstr("RAND_B"));
    ASSERT(rand_a.count == 5);
    ASSERT(nob_sv_eq(rand_a, rand_b));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TS_DEFAULT")),
                     nob_sv_from_cstr("2000-01-01T00:00:00Z")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UUID_UP")),
                     nob_sv_from_cstr("6CB8E707-0FC5-5F55-88D4-D4FED43E64A8")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_extra_subcommands_and_download_expected_hash) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(V \"qq\")\n"
        "file(WRITE extra_src.txt \"abc\")\n"
        "file(SHA256 extra_src.txt EXTRA_HASH)\n"
        "file(CONFIGURE OUTPUT extra_cfg.txt CONTENT \"@V@-${V}\" @ONLY)\n"
        "file(READ extra_cfg.txt EXTRA_CFG)\n"
        "file(COPY_FILE extra_src.txt extra_dst.txt RESULT COPY_RES ONLY_IF_DIFFERENT INPUT_MAY_BE_RECENT)\n"
        "file(READ extra_dst.txt COPY_TXT)\n"
        "file(TOUCH extra_touch.txt)\n"
        "if(EXISTS extra_touch.txt)\n"
        "  set(TOUCH_CREATED 1)\n"
        "else()\n"
        "  set(TOUCH_CREATED 0)\n"
        "endif()\n"
        "file(TOUCH_NOCREATE extra_touch_missing.txt)\n"
        "if(EXISTS extra_touch_missing.txt)\n"
        "  set(TOUCH_NOCREATE_CREATED 1)\n"
        "else()\n"
        "  set(TOUCH_NOCREATE_CREATED 0)\n"
        "endif()\n"
        "file(WRITE extra_dl_src.txt \"hello\")\n"
        "file(DOWNLOAD extra_dl_src.txt extra_dl_ok.txt EXPECTED_HASH SHA256=2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824)\n"
        "file(READ extra_dl_ok.txt DL_OK_TXT)\n"
        "file(DOWNLOAD extra_dl_src.txt extra_dl_bad.txt EXPECTED_HASH SHA256=0000 STATUS DL_BAD_STATUS)\n"
        "list(LENGTH DL_BAD_STATUS DL_BAD_LEN)\n"
        "list(GET DL_BAD_STATUS 0 DL_BAD_CODE)\n"
        "add_executable(file_extra_probe main.c)\n"
        "target_compile_definitions(file_extra_probe PRIVATE "
        "\"EXTRA_HASH=${EXTRA_HASH}\" \"EXTRA_CFG=${EXTRA_CFG}\" "
        "\"COPY_RES=${COPY_RES}\" \"COPY_TXT=${COPY_TXT}\" "
        "\"TOUCH_CREATED=${TOUCH_CREATED}\" \"TOUCH_NOCREATE_CREATED=${TOUCH_NOCREATE_CREATED}\" "
        "\"DL_OK_TXT=${DL_OK_TXT}\" \"DL_BAD_LEN=${DL_BAD_LEN}\" \"DL_BAD_CODE=${DL_BAD_CODE}\")\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_hash = false;
    bool saw_cfg = false;
    bool saw_copy_res = false;
    bool saw_copy_txt = false;
    bool saw_touch_created = false;
    bool saw_touch_nocreate_created = false;
    bool saw_dl_ok = false;
    bool saw_dl_bad_len = false;
    bool saw_dl_bad_code = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("file_extra_probe"))) continue;
        String_View it = ev->as.target_compile_definitions.item;
        if (nob_sv_eq(it, nob_sv_from_cstr("EXTRA_HASH=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))) saw_hash = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("EXTRA_CFG=qq-qq"))) saw_cfg = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("COPY_RES=0"))) saw_copy_res = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("COPY_TXT=abc"))) saw_copy_txt = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("TOUCH_CREATED=1"))) saw_touch_created = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("TOUCH_NOCREATE_CREATED=0"))) saw_touch_nocreate_created = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("DL_OK_TXT=hello"))) saw_dl_ok = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("DL_BAD_LEN=2"))) saw_dl_bad_len = true;
        if (nob_sv_eq(it, nob_sv_from_cstr("DL_BAD_CODE=1"))) saw_dl_bad_code = true;
    }

    ASSERT(saw_hash);
    ASSERT(saw_cfg);
    ASSERT(saw_copy_res);
    ASSERT(saw_copy_txt);
    ASSERT(saw_touch_created);
    ASSERT(saw_touch_nocreate_created);
    ASSERT(saw_dl_ok);
    ASSERT(saw_dl_bad_len);
    ASSERT(saw_dl_bad_code);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_runtime_dependencies_resolve_known_host_binary) {
    char host_binary[_TINYDIR_PATH_MAX] = {0};
#if defined(_WIN32)
    if (!test_ws_host_program_in_path("cmd", host_binary)) {
        TEST_SKIP("requires cmd on PATH");
    }
#else
    if (!test_ws_host_path_exists("/bin/ls")) {
        TEST_SKIP("requires /bin/ls");
    }
    ASSERT(snprintf(host_binary, sizeof(host_binary), "%s", "/bin/ls") > 0);
#endif

    Eval_Test_Fixture *fixture = eval_test_fixture_create(2 * 1024 * 1024, 2 * 1024 * 1024, NULL);
    ASSERT(fixture != NULL);
    ASSERT(fixture->ctx != NULL);

    char script[2048] = {0};
    int script_n = snprintf(
        script,
        sizeof(script),
        "file(GET_RUNTIME_DEPENDENCIES "
        "RESOLVED_DEPENDENCIES_VAR RD_RES "
        "UNRESOLVED_DEPENDENCIES_VAR RD_UNRES "
        "EXECUTABLES [=[%s]=])\n"
        "list(LENGTH RD_RES RD_RES_LEN)\n"
        "list(LENGTH RD_UNRES RD_UNRES_LEN)\n"
        "add_executable(file_runtime_dep_probe main.c)\n"
        "target_compile_definitions(file_runtime_dep_probe PRIVATE "
        "\"RD_RES_LEN=${RD_RES_LEN}\" \"RD_UNRES_LEN=${RD_UNRES_LEN}\")\n",
        host_binary);
    ASSERT(script_n > 0 && script_n < (int)sizeof(script));

    Ast_Root root = parse_cmake(fixture->temp_arena, script);
    ASSERT(!eval_result_is_fatal(eval_test_run(fixture->ctx, root)));

    const Eval_Run_Report *report = eval_test_report(fixture->ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_rd_res_len = false;
    bool saw_rd_unres_len = false;
    for (size_t i = 0; i < fixture->stream->count; i++) {
        const Cmake_Event *ev = &fixture->stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("file_runtime_dep_probe"))) {
            continue;
        }
        String_View it = ev->as.target_compile_definitions.item;
        if (nob_sv_starts_with(it, nob_sv_from_cstr("RD_RES_LEN="))) {
            char buf[64] = {0};
            size_t n = it.count - strlen("RD_RES_LEN=");
            ASSERT(n < sizeof(buf));
            memcpy(buf, it.data + strlen("RD_RES_LEN="), n);
            ASSERT(strtol(buf, NULL, 10) > 0);
            saw_rd_res_len = true;
        } else if (nob_sv_starts_with(it, nob_sv_from_cstr("RD_UNRES_LEN="))) {
            saw_rd_unres_len = true;
        }
    }

    ASSERT(saw_rd_res_len);
    ASSERT(saw_rd_unres_len);
    TEST_PASS();
}

TEST(evaluator_file_dispatcher_routes_glob_rw_and_copy_families) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY dispatch_glob/sub)\n"
        "file(WRITE dispatch_glob/a.c \"int a = 0;\\n\")\n"
        "file(WRITE dispatch_glob/sub/b.c \"int b = 0;\\n\")\n"
        "file(GLOB_RECURSE DISPATCH_GLOB RELATIVE dispatch_glob dispatch_glob/*.c dispatch_glob/*/*.c)\n"
        "file(WRITE dispatch_rw.txt \"alpha\")\n"
        "file(READ dispatch_rw.txt DISPATCH_READ)\n"
        "file(COPY dispatch_rw.txt DESTINATION dispatch_copy)\n"
        "file(READ dispatch_copy/dispatch_rw.txt DISPATCH_COPY)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    String_View glob_out = eval_test_var_get(ctx, nob_sv_from_cstr("DISPATCH_GLOB"));
    ASSERT(sv_contains_sv(glob_out, nob_sv_from_cstr("a.c")));
    ASSERT(sv_contains_sv(glob_out, nob_sv_from_cstr("sub/b.c")));
    const char *a_pos = strstr(nob_temp_sv_to_cstr(glob_out), "a.c");
    const char *b_pos = strstr(nob_temp_sv_to_cstr(glob_out), "sub/b.c");
    ASSERT(a_pos != NULL && b_pos != NULL && a_pos < b_pos);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DISPATCH_READ")), nob_sv_from_cstr("alpha")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DISPATCH_COPY")), nob_sv_from_cstr("alpha")));

    bool saw_glob_event = false;
    bool saw_write_event = false;
    bool saw_read_event = false;
    bool saw_copy_read_event = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_FS_GLOB &&
            nob_sv_eq(ev->as.fs_glob.out_var, nob_sv_from_cstr("DISPATCH_GLOB")) &&
            ev->as.fs_glob.recursive &&
            sv_contains_sv(ev->as.fs_glob.base_dir, nob_sv_from_cstr("dispatch_glob"))) {
            saw_glob_event = true;
        } else if (ev->h.kind == EVENT_FS_WRITE_FILE &&
                   sv_contains_sv(ev->as.fs_write_file.path, nob_sv_from_cstr("dispatch_rw.txt"))) {
            saw_write_event = true;
        } else if (ev->h.kind == EVENT_FS_READ_FILE &&
                   nob_sv_eq(ev->as.fs_read_file.out_var, nob_sv_from_cstr("DISPATCH_READ")) &&
                   sv_contains_sv(ev->as.fs_read_file.path, nob_sv_from_cstr("dispatch_rw.txt"))) {
            saw_read_event = true;
        } else if (ev->h.kind == EVENT_FS_READ_FILE &&
                   nob_sv_eq(ev->as.fs_read_file.out_var, nob_sv_from_cstr("DISPATCH_COPY")) &&
                   sv_contains_sv(ev->as.fs_read_file.path, nob_sv_from_cstr("dispatch_copy/dispatch_rw.txt"))) {
            saw_copy_read_event = true;
        }
    }

    ASSERT(saw_glob_event);
    ASSERT(saw_write_event);
    ASSERT(saw_read_event);
    ASSERT(saw_copy_read_event);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_glob_and_strings_cover_curl_style_queries) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY curl_style/include/curl)\n"
        "file(MAKE_DIRECTORY curl_style/certs)\n"
        "file(WRITE curl_style/include/curl/curlver.h [=[#define LIBCURL_VERSION \"8.18.0\"\n"
        "#define LIBCURL_VERSION_NUM 0x081200\n"
        "#define IGNORE_ME 1\n"
        "]=])\n"
        "file(WRITE curl_style/certs/abcdef12.0 \"CERT\")\n"
        "file(WRITE curl_style/certs/not_a_hash.txt \"SKIP\")\n"
        "file(STRINGS curl_style/include/curl/curlver.h CURL_VERSION_LINES REGEX \"#define LIBCURL_VERSION( |_NUM )\")\n"
        "file(GLOB CURL_CA_FILES RELATIVE curl_style/certs curl_style/certs/[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f].0)\n"
        "list(LENGTH CURL_VERSION_LINES CURL_VERSION_LINE_COUNT)\n"
        "list(LENGTH CURL_CA_FILES CURL_CA_COUNT)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    String_View version_lines = eval_test_var_get(ctx, nob_sv_from_cstr("CURL_VERSION_LINES"));
    ASSERT(sv_contains_sv(version_lines, nob_sv_from_cstr("LIBCURL_VERSION")));
    ASSERT(sv_contains_sv(version_lines, nob_sv_from_cstr("8.18.0")));
    ASSERT(sv_contains_sv(version_lines, nob_sv_from_cstr("LIBCURL_VERSION_NUM")));
    ASSERT(sv_contains_sv(version_lines, nob_sv_from_cstr("0x081200")));
    ASSERT(!sv_contains_sv(version_lines, nob_sv_from_cstr("IGNORE_ME")));

    String_View ca_files = eval_test_var_get(ctx, nob_sv_from_cstr("CURL_CA_FILES"));
    ASSERT(nob_sv_eq(ca_files, nob_sv_from_cstr("abcdef12.0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CURL_VERSION_LINE_COUNT")), nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CURL_CA_COUNT")), nob_sv_from_cstr("1")));

    bool saw_glob_event = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_FS_GLOB &&
                   nob_sv_eq(ev->as.fs_glob.out_var, nob_sv_from_cstr("CURL_CA_FILES")) &&
                   !ev->as.fs_glob.recursive &&
                   sv_contains_sv(ev->as.fs_glob.base_dir, nob_sv_from_cstr("curl_style/certs"))) {
            saw_glob_event = true;
        }
    }

    ASSERT(saw_glob_event);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_configure_file_expands_cmakedefines_and_copyonly) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("src"));
    ASSERT(nob_mkdir_if_not_exists("build"));
    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);
    char source_root[_TINYDIR_PATH_MAX] = {0};
    char binary_root[_TINYDIR_PATH_MAX] = {0};
    ASSERT(snprintf(source_root, sizeof(source_root), "%s/src", cwd) > 0);
    ASSERT(snprintf(binary_root, sizeof(binary_root), "%s/build", cwd) > 0);
    const char *cfg_template =
        "NAME=@NAME@\n"
        "LITERAL=${NAME}\n"
        "QUOTE=@QUOTE@\n"
        "#cmakedefine ENABLE_FEATURE\n"
        "#cmakedefine DISABLE_FEATURE\n"
        "#cmakedefine01 ENABLE_FEATURE\n"
        "#cmakedefine01 DISABLE_FEATURE\n";
    const char *cfg_copy = "@NAME@\n${NAME}\n";
    ASSERT(nob_write_entire_file("src/cfg_template.in", cfg_template, strlen(cfg_template)));
    ASSERT(nob_write_entire_file("src/cfg_copy.in", cfg_copy, strlen(cfg_copy)));
    ASSERT(nob_mkdir_if_not_exists("build/cfg_out_dir"));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(source_root);
    init.binary_dir = nob_sv_from_cstr(binary_root);
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(NAME Demo)\n"
        "set(QUOTE \"one\\\"two\")\n"
        "set(ENABLE_FEATURE ON)\n"
        "set(DISABLE_FEATURE 0)\n"
        "configure_file(cfg_template.in cfg_configured.txt @ONLY ESCAPE_QUOTES NEWLINE_STYLE DOS)\n"
        "configure_file(cfg_copy.in cfg_out_dir COPYONLY)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    String_View configured = {0};
    String_View copied = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "build/cfg_configured.txt", &configured));
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "build/cfg_out_dir/cfg_copy.in", &copied));

    ASSERT(nob_sv_eq(configured,
                     nob_sv_from_cstr("NAME=Demo\r\n"
                                      "LITERAL=${NAME}\r\n"
                                      "QUOTE=one\\\"two\r\n"
                                      "#define ENABLE_FEATURE\r\n"
                                      "/* #undef DISABLE_FEATURE */\r\n"
                                      "#define ENABLE_FEATURE 1\r\n"
                                      "#define DISABLE_FEATURE 0\r\n")));
    ASSERT(nob_sv_eq(copied, nob_sv_from_cstr("@NAME@\n${NAME}\n")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_real_path_cmp0152_old_and_new) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("cmp0152_real_target"));
    ASSERT(nob_mkdir_if_not_exists("cmp0152_real_target/child"));
    ASSERT(nob_write_entire_file("cmp0152_real_target/cmp0152_result.txt", "target\n", 7));
    ASSERT(nob_write_entire_file("cmp0152_result.txt", "cwd\n", 4));
    ASSERT(evaluator_create_directory_link_like("cmp0152_real_link", "cmp0152_real_target/child"));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0152 OLD)\n"
        "file(REAL_PATH cmp0152_real_link/../cmp0152_result.txt OUT_OLD)\n"
        "cmake_policy(SET CMP0152 NEW)\n"
        "file(REAL_PATH cmp0152_real_link/../cmp0152_result.txt OUT_NEW)\n"
        "add_executable(real_path_policy_probe main.c)\n"
        "target_compile_definitions(real_path_policy_probe PRIVATE OLD=${OUT_OLD} NEW=${OUT_NEW})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);

    char old_path[1024] = {0};
    char new_path[1024] = {0};
    int old_n = snprintf(old_path, sizeof(old_path), "%s/cmp0152_result.txt", cwd);
    int new_n = snprintf(new_path, sizeof(new_path), "%s/cmp0152_real_target/cmp0152_result.txt", cwd);
    ASSERT(old_n > 0 && old_n < (int)sizeof(old_path));
    ASSERT(new_n > 0 && new_n < (int)sizeof(new_path));
    for (size_t i = 0; old_path[i] != '\0'; i++) if (old_path[i] == '\\') old_path[i] = '/';
    for (size_t i = 0; new_path[i] != '\0'; i++) if (new_path[i] == '\\') new_path[i] = '/';

    char old_item[1200] = {0};
    char new_item[1200] = {0};
    int old_item_n = snprintf(old_item, sizeof(old_item), "OLD=%s", old_path);
    int new_item_n = snprintf(new_item, sizeof(new_item), "NEW=%s", new_path);
    ASSERT(old_item_n > 0 && old_item_n < (int)sizeof(old_item));
    ASSERT(new_item_n > 0 && new_item_n < (int)sizeof(new_item));

    bool saw_old = false;
    bool saw_new = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("real_path_policy_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr(old_item))) saw_old = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr(new_item))) saw_new = true;
    }

    ASSERT(saw_old);
    ASSERT(saw_new);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_generate_is_deferred_until_end_of_run) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("src"));
    ASSERT(nob_mkdir_if_not_exists("build"));
    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);
    char source_root[_TINYDIR_PATH_MAX] = {0};
    char binary_root[_TINYDIR_PATH_MAX] = {0};
    ASSERT(snprintf(source_root, sizeof(source_root), "%s/src", cwd) > 0);
    ASSERT(snprintf(binary_root, sizeof(binary_root), "%s/build", cwd) > 0);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(source_root);
    init.binary_dir = nob_sv_from_cstr(binary_root);
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(GEN_IN_PATH \"${CMAKE_CURRENT_SOURCE_DIR}/gen_in.txt\")\n"
        "set(GEN_OUT_CONTENT \"${CMAKE_CURRENT_BINARY_DIR}/gen_out_content.txt\")\n"
        "set(GEN_OUT_INPUT \"${CMAKE_CURRENT_BINARY_DIR}/gen_out_input.txt\")\n"
        "set(GEN_OUT_SKIP \"${CMAKE_CURRENT_BINARY_DIR}/gen_out_skip.txt\")\n"
        "file(WRITE \"${GEN_IN_PATH}\" \"IN\")\n"
        "file(GENERATE OUTPUT \"${GEN_OUT_CONTENT}\" CONTENT \"OUT\")\n"
        "file(GENERATE OUTPUT \"${GEN_OUT_INPUT}\" INPUT \"${GEN_IN_PATH}\")\n"
        "file(GENERATE OUTPUT \"${GEN_OUT_SKIP}\" CONTENT \"SKIP\" CONDITION 0)\n"
        "if(EXISTS \"${GEN_OUT_CONTENT}\")\n"
        "  set(GEN_BEFORE 1)\n"
        "else()\n"
        "  set(GEN_BEFORE 0)\n"
        "endif()\n"
        "add_executable(gen_deferred_probe main.c)\n"
        "target_compile_definitions(gen_deferred_probe PRIVATE GEN_BEFORE=${GEN_BEFORE})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_before_zero = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("gen_deferred_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GEN_BEFORE=0"))) {
            saw_before_zero = true;
        }
    }
    ASSERT(saw_before_zero);

    Ast_Root verify = parse_cmake(
        temp_arena,
        "file(READ \"${CMAKE_CURRENT_BINARY_DIR}/gen_out_content.txt\" GEN_OUT)\n"
        "file(READ \"${CMAKE_CURRENT_BINARY_DIR}/gen_out_input.txt\" GEN_IN)\n"
        "if(EXISTS \"${CMAKE_CURRENT_BINARY_DIR}/gen_out_skip.txt\")\n"
        "  set(GEN_SKIP 1)\n"
        "else()\n"
        "  set(GEN_SKIP 0)\n"
        "endif()\n"
        "add_executable(gen_deferred_verify main.c)\n"
        "target_compile_definitions(gen_deferred_verify PRIVATE GEN_OUT=${GEN_OUT} GEN_IN=${GEN_IN} GEN_SKIP=${GEN_SKIP})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, verify)));

    bool saw_out = false;
    bool saw_in = false;
    bool saw_skip = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("gen_deferred_verify"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GEN_OUT=OUT"))) saw_out = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GEN_IN=IN"))) saw_in = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GEN_SKIP=0"))) saw_skip = true;
    }
    ASSERT(saw_out);
    ASSERT(saw_in);
    ASSERT(saw_skip);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_lock_directory_and_duplicate_lock_result) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY lock_dir)\n"
        "file(LOCK lock_dir DIRECTORY RESULT_VARIABLE L1)\n"
        "file(LOCK lock_dir DIRECTORY RESULT_VARIABLE L2)\n"
        "file(LOCK lock_dir DIRECTORY RELEASE RESULT_VARIABLE L3)\n"
        "add_executable(lock_probe main.c)\n"
        "target_compile_definitions(lock_probe PRIVATE L1=${L1} L2=${L2} L3=${L3})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_l1_ok = false;
    bool saw_l2_nonzero = false;
    bool saw_l3_ok = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("lock_probe"))) continue;
        String_View item = ev->as.target_compile_definitions.item;
        if (nob_sv_eq(item, nob_sv_from_cstr("L1=0"))) saw_l1_ok = true;
        if (item.count > 3 && memcmp(item.data, "L2=", 3) == 0 && !nob_sv_eq(item, nob_sv_from_cstr("L2=0"))) {
            saw_l2_nonzero = true;
        }
        if (nob_sv_eq(item, nob_sv_from_cstr("L3=0"))) saw_l3_ok = true;
    }
    ASSERT(saw_l1_ok);
    ASSERT(saw_l2_nonzero);
    ASSERT(saw_l3_ok);
    ASSERT(nob_file_exists("lock_dir/cmake.lock"));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_file_download_probe_mode_without_destination) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE probe_src.txt \"abc\")\n"
        "file(DOWNLOAD probe_src.txt STATUS DL_STATUS LOG DL_LOG)\n"
        "list(LENGTH DL_STATUS DL_LEN)\n"
        "list(GET DL_STATUS 0 DL_CODE)\n"
        "add_executable(dl_probe main.c)\n"
        "target_compile_definitions(dl_probe PRIVATE DL_LEN=${DL_LEN} DL_CODE=${DL_CODE})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_len = false;
    bool saw_code = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("dl_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("DL_LEN=2"))) saw_len = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("DL_CODE=0"))) saw_code = true;
    }
    ASSERT(saw_len);
    ASSERT(saw_code);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_compile_no_cache_and_cmake_flags_do_not_leak) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_compile(TC_LOCAL_ONLY tc_try_local\n"
        "  SOURCE_FROM_CONTENT probe.c \"int main(void){return 0;}\"\n"
        "  CMAKE_FLAGS -DINNER_ONLY:BOOL=ON\n"
        "  NO_CACHE)\n"
        "add_executable(tc_try_local_probe main.c)\n"
        "target_compile_definitions(tc_try_local_probe PRIVATE TC_LOCAL_ONLY=${TC_LOCAL_ONLY} \"INNER_ONLY_PARENT=${INNER_ONLY}\")\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_local_only = false;
    bool saw_parent_empty = false;
    bool saw_cache_entry = false;
    bool saw_parent_binding = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_SET_CACHE_ENTRY &&
            ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("TC_LOCAL_ONLY"))) {
            saw_cache_entry = true;
        }
        if (ev->h.kind == EV_VAR_SET &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("INNER_ONLY"))) {
            saw_parent_binding = true;
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("tc_try_local_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("TC_LOCAL_ONLY=1"))) {
            saw_local_only = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INNER_ONLY_PARENT="))) {
            saw_parent_empty = true;
        }
    }

    ASSERT(saw_local_only);
    ASSERT(saw_parent_empty);
    ASSERT(!saw_cache_entry);
    ASSERT(!saw_parent_binding);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_compile_failure_populates_output_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_compile(TC_FAIL tc_try_fail\n"
        "  SOURCE_FROM_CONTENT broken.c \"int main(void){ this is not valid C; }\"\n"
        "  OUTPUT_VARIABLE TC_FAIL_LOG\n"
        "  NO_CACHE)\n"
        "string(LENGTH \"${TC_FAIL_LOG}\" TC_FAIL_LOG_LEN)\n"
        "add_executable(tc_try_fail_probe main.c)\n"
        "target_compile_definitions(tc_try_fail_probe PRIVATE TC_FAIL=${TC_FAIL} TC_FAIL_LOG_LEN=${TC_FAIL_LOG_LEN})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_fail_result = false;
    size_t fail_log_len = 0;
    bool saw_fail_log_len = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("tc_try_fail_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("TC_FAIL=0"))) {
            saw_fail_result = true;
            continue;
        }
        if (nob_sv_starts_with(ev->as.target_compile_definitions.item, nob_sv_from_cstr("TC_FAIL_LOG_LEN="))) {
            String_View len_sv = nob_sv_from_parts(
                ev->as.target_compile_definitions.item.data + strlen("TC_FAIL_LOG_LEN="),
                ev->as.target_compile_definitions.item.count - strlen("TC_FAIL_LOG_LEN="));
            char buf[64] = {0};
            ASSERT(len_sv.count < sizeof(buf));
            memcpy(buf, len_sv.data, len_sv.count);
            fail_log_len = (size_t)strtoull(buf, NULL, 10);
            saw_fail_log_len = true;
        }
    }

    ASSERT(saw_fail_result);
    ASSERT(saw_fail_log_len);
    ASSERT(fail_log_len > 0);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_compile_empty_capture_file_is_silent) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    ASSERT(nob_write_entire_file("tc_empty_capture.txt", "", 0));

    Nob_String_Builder log = {0};
    evaluator_begin_nob_log_capture();
    ASSERT(eval_test_append_file_to_log_if_nonempty(temp_arena, "./tc_empty_capture.txt", &log));
    evaluator_end_nob_log_capture();

    ASSERT(log.count == 0);
    ASSERT(g_evaluator_captured_nob_logs.count == 0);

    nob_sb_free(log);
    nob_sb_free(g_evaluator_captured_nob_logs);
    g_evaluator_captured_nob_logs = (Nob_String_Builder){0};
    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

static void evaluator_internal_cleanup_stack_failure_helper_impl(int *passed, int *failed, int *skipped) {
    (void)passed;
    (void)skipped;
    ASSERT(evaluator_test_begin_nob_log_capture_guarded());
    ASSERT(evaluator_test_guard_env("NOBIFY_CLEANUP_GUARD", "dirty"));
    (*failed)++;
}

static void evaluator_internal_cleanup_stack_failure_helper_run(int *passed, int *failed, int *skipped) {
    Test_Case_Workspace test_ws_case = {0};
    Test_V2_Cleanup_Stack prev_cleanup_stack = test_v2_cleanup_scope_enter();
    if (!test_ws_case_enter(&test_ws_case, "evaluator_internal_cleanup_stack_failure_helper")) {
        test_v2_emit_failure_message(__func__, 0, "could not enter isolated test workspace");
        nob_log(NOB_ERROR, "FAILED: %s: could not enter isolated test workspace", __func__);
        (*failed)++;
        test_v2_cleanup_scope_leave(prev_cleanup_stack);
        return;
    }

    evaluator_internal_cleanup_stack_failure_helper_impl(passed, failed, skipped);
    test_v2_cleanup_run_all();
    if (!test_ws_case_leave(&test_ws_case)) {
        test_v2_emit_failure_message(__func__, 0, "could not cleanup isolated test workspace");
        nob_log(NOB_ERROR, "FAILED: %s: could not cleanup isolated test workspace", __func__);
        (*failed)++;
    }
    test_v2_cleanup_scope_leave(prev_cleanup_stack);
}

TEST(evaluator_cleanup_stack_restores_log_env_and_workspace_after_assert_failure) {
    int inner_passed = 0;
    int inner_failed = 0;
    int inner_skipped = 0;
    Nob_Log_Handler *saved_handler = nob_get_log_handler();
    const char *cwd_before = nob_get_current_dir_temp();
    ASSERT(cwd_before != NULL);

    char cwd_before_copy[_TINYDIR_PATH_MAX] = {0};
    int cwd_n = snprintf(cwd_before_copy, sizeof(cwd_before_copy), "%s", cwd_before);
    ASSERT(cwd_n > 0 && (size_t)cwd_n < sizeof(cwd_before_copy));
    ASSERT(getenv("NOBIFY_CLEANUP_GUARD") == NULL);

    evaluator_internal_cleanup_stack_failure_helper_run(&inner_passed, &inner_failed, &inner_skipped);

    ASSERT(inner_passed == 0);
    ASSERT(inner_failed == 1);
    ASSERT(inner_skipped == 0);
    ASSERT(nob_get_log_handler() == saved_handler);
    ASSERT(getenv("NOBIFY_CLEANUP_GUARD") == NULL);

    const char *cwd_after = nob_get_current_dir_temp();
    ASSERT(cwd_after != NULL);
    ASSERT(strcmp(cwd_before_copy, cwd_after) == 0);

    nob_sb_free(g_evaluator_captured_nob_logs);
    g_evaluator_captured_nob_logs = (Nob_String_Builder){0};
    TEST_PASS();
}

void run_evaluator_v2_batch5(int *passed, int *failed, int *skipped) {
    test_evaluator_find_item_commands_resolve_local_paths_and_model_package_root_policies(passed, failed, skipped);
    test_evaluator_find_item_command_rejects_unknown_option(passed, failed, skipped);
    test_evaluator_find_item_command_rejects_missing_output_variable(passed, failed, skipped);
    test_evaluator_find_item_command_rejects_missing_registry_or_validator_values(passed, failed, skipped);
    test_evaluator_find_item_command_rejects_malformed_env_clause(passed, failed, skipped);
    test_evaluator_get_filename_component_covers_documented_modes(passed, failed, skipped);
    test_evaluator_get_filename_component_rejects_invalid_option_shapes(passed, failed, skipped);
    test_evaluator_find_package_no_module_names_configs_path_suffixes_and_registry_view(passed, failed, skipped);
    test_evaluator_find_package_auto_prefers_config_when_requested(passed, failed, skipped);
    test_evaluator_find_package_cmp0074_old_ignores_root_and_new_uses_root(passed, failed, skipped);
    test_evaluator_find_package_uses_export_package_registry_and_respects_cmp0090_gates(passed, failed, skipped);
    test_evaluator_project_full_signature_and_variable_surface(passed, failed, skipped);
    test_evaluator_project_cmp0048_new_clears_and_old_preserves_version_vars_without_version_arg(passed, failed, skipped);
    test_evaluator_project_rejects_invalid_signature_forms(passed, failed, skipped);
    test_evaluator_policy_known_unknown_and_if_predicate(passed, failed, skipped);
    test_evaluator_policy_strict_arity_and_version_validation(passed, failed, skipped);
    test_evaluator_cmake_minimum_required_inside_function_applies_policy_not_variable(passed, failed, skipped);
    test_evaluator_cpack_commands_require_cpackcomponent_module_and_parse_component_extras(passed, failed, skipped);
    test_evaluator_cpack_commands_reject_missing_names_and_warn_on_extra_args(passed, failed, skipped);
    test_evaluator_diag_codes_are_explicit_and_report_classes(passed, failed, skipped);
    test_evaluator_host_system_information_windows_registry_and_fallback_scripts_are_modeled(passed, failed, skipped);
    test_evaluator_string_hash_repeat_and_json_full_surface(passed, failed, skipped);
    test_evaluator_string_text_regex_and_misc_dispatch_events(passed, failed, skipped);
    test_evaluator_string_regex_parse_error_keeps_diag_surface(passed, failed, skipped);
    test_evaluator_string_find_compare_configure_random_timestamp_and_uuid_cover_remaining_option_modes(passed, failed, skipped);
    test_evaluator_file_extra_subcommands_and_download_expected_hash(passed, failed, skipped);
    test_evaluator_file_dispatcher_routes_glob_rw_and_copy_families(passed, failed, skipped);
    test_evaluator_file_glob_and_strings_cover_curl_style_queries(passed, failed, skipped);
    test_evaluator_configure_file_expands_cmakedefines_and_copyonly(passed, failed, skipped);
    test_evaluator_file_real_path_cmp0152_old_and_new(passed, failed, skipped);
    test_evaluator_file_generate_is_deferred_until_end_of_run(passed, failed, skipped);
    test_evaluator_file_lock_directory_and_duplicate_lock_result(passed, failed, skipped);
    test_evaluator_file_download_probe_mode_without_destination(passed, failed, skipped);
    test_evaluator_try_compile_no_cache_and_cmake_flags_do_not_leak(passed, failed, skipped);
    test_evaluator_try_compile_failure_populates_output_variable(passed, failed, skipped);
    test_evaluator_try_compile_empty_capture_file_is_silent(passed, failed, skipped);
    test_evaluator_cleanup_stack_restores_log_env_and_workspace_after_assert_failure(passed, failed, skipped);
}

void run_evaluator_v2_integration_batch5(int *passed, int *failed, int *skipped) {
    test_evaluator_file_runtime_dependencies_resolve_known_host_binary(passed, failed, skipped);
}
