#include "test_codegen_v2_common.h"

static void codegen_reject_init_event(Event *ev, Event_Kind kind, size_t line) {
    if (!ev) return;
    *ev = (Event){0};
    ev->h.kind = kind;
    ev->h.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev->h.origin.line = line;
    ev->h.origin.col = 1;
}

TEST(codegen_rejects_module_target_as_link_dependency) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE plugin)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_unsupported_generator_expression_operator) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"$<JOIN:a,b>\")\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_imported_executable_as_link_dependency) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_executable(tool IMPORTED)\n"
        "set_target_properties(tool PROPERTIES IMPORTED_LOCATION /bin/true)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE tool)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_imported_module_target_as_link_dependency) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_library(plugin MODULE IMPORTED)\n"
        "set_target_properties(plugin PROPERTIES IMPORTED_LOCATION imports/libplugin.so)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE plugin)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_append_custom_command_steps) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_custom_command(OUTPUT generated.c COMMAND echo base)\n"
        "add_custom_command(OUTPUT generated.c APPEND COMMAND echo extra)\n"
        "add_executable(app main.c ${CMAKE_CURRENT_BINARY_DIR}/generated.c)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_targets_with_precompile_headers) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_executable(app main.c)\n"
        "target_precompile_headers(app PRIVATE pch.h)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_export_append_in_standalone_export_backend) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "export(TARGETS core FILE CoreTargets.cmake APPEND)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_export_cxx_modules_directory_in_standalone_export_backend) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "export(TARGETS core FILE CoreTargets.cmake CXX_MODULES_DIRECTORY modules)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_invalid_platform_backend_pair) {
    Nob_String_Builder sb = {0};
    Codegen_Test_Config config = {
        .input_path = "CMakeLists.txt",
        .output_path = "nob.c",
        .source_dir = NULL,
        .binary_dir = NULL,
        .platform = NOB_CODEGEN_PLATFORM_WINDOWS,
        .backend = NOB_CODEGEN_BACKEND_POSIX,
    };
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script_with_config(
        "project(Test C)\n"
        "add_executable(app main.c)\n",
        &config,
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_macosx_bundle_targets) {
    Nob_String_Builder sb = {0};
    Codegen_Test_Config config = {
        .input_path = "CMakeLists.txt",
        .output_path = "nob.c",
        .source_dir = NULL,
        .binary_dir = NULL,
        .platform = NOB_CODEGEN_PLATFORM_DARWIN,
        .backend = NOB_CODEGEN_BACKEND_POSIX,
    };
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script_with_config(
        "project(Test C)\n"
        "add_executable(app main.c)\n"
        "set_target_properties(app PROPERTIES MACOSX_BUNDLE ON)\n",
        &config,
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_cpack_archive_component_install_before_render) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "include(CPackComponent)\n"
        "set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)\n"
        "set(CPACK_GENERATOR TGZ)\n"
        "include(CPack)\n"
        "cpack_add_install_type(Full)\n"
        "cpack_add_component(Runtime INSTALL_TYPES Full)\n"
        "add_executable(app main.c)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_remote_download_replay_before_render) {
    Arena *builder_arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Arena *codegen_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    Nob_String_Builder sb = {0};
    Nob_Codegen_Options opts = {
        .input_path = {0},
        .output_path = {0},
        .target_platform = NOB_CODEGEN_PLATFORM_HOST,
        .backend = NOB_CODEGEN_BACKEND_AUTO,
    };

    ASSERT(builder_arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);
    ASSERT(codegen_arena != NULL);

    stream = event_stream_create(builder_arena);
    ASSERT(stream != NULL);

    codegen_reject_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_PROJECT_DECLARE, 2);
    ev.as.project_declare.name = nob_sv_from_cstr("RemoteDownloadReject");
    ev.as.project_declare.languages = nob_sv_from_cstr("C");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 3);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("remote_download");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_HOST_EFFECT;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_NONE;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_CONFIGURE;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_DIRECTORY_LEAVE, 4);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(test_semantic_pipeline_build_model_from_stream(builder_arena,
                                                          validate_arena,
                                                          model_arena,
                                                          stream,
                                                          &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);
    ASSERT(bm_query_replay_action_count(build.model) == 1);

    opts.input_path = nob_sv_from_cstr("CMakeLists.txt");
    opts.output_path = nob_sv_from_cstr("nob.c");
    ASSERT(!nob_codegen_render(build.model, codegen_arena, &opts, &sb));

    nob_sb_free(sb);
    arena_destroy(builder_arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    arena_destroy(codegen_arena);
    TEST_PASS();
}

TEST(codegen_rejects_unsupported_archive_variant_before_render) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "file(MAKE_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/archive_input\")\n"
        "file(WRITE \"${CMAKE_CURRENT_SOURCE_DIR}/archive_input/a.txt\" \"A\")\n"
        "file(ARCHIVE_CREATE OUTPUT \"${CMAKE_CURRENT_BINARY_DIR}/bad.tar.gz\" PATHS \"${CMAKE_CURRENT_SOURCE_DIR}/archive_input\" FORMAT tar COMPRESSION GZIP)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_get_runtime_dependencies_before_render) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "file(GET_RUNTIME_DEPENDENCIES RESOLVED_DEPENDENCIES_VAR RES EXECUTABLES \"${CMAKE_COMMAND}\")\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_replay_actions_before_render) {
    Arena *builder_arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Arena *codegen_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    Nob_String_Builder sb = {0};
    Nob_Codegen_Options opts = {
        .input_path = {0},
        .output_path = {0},
        .target_platform = NOB_CODEGEN_PLATFORM_HOST,
        .backend = NOB_CODEGEN_BACKEND_AUTO,
    };

    ASSERT(builder_arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);
    ASSERT(codegen_arena != NULL);

    stream = event_stream_create(builder_arena);
    ASSERT(stream != NULL);

    codegen_reject_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_PROJECT_DECLARE, 2);
    ev.as.project_declare.name = nob_sv_from_cstr("ReplayReject");
    ev.as.project_declare.languages = nob_sv_from_cstr("C");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 3);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("reject_me");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_PROCESS;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_CONFIGURE;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("tools");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 4);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("reject_me");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("tool");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_DIRECTORY_LEAVE, 5);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(builder_arena,
                                                          validate_arena,
                                                          model_arena,
                                                          stream,
                                                          &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);
    ASSERT(bm_query_replay_action_count(build.model) == 1);

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    opts.input_path = nob_sv_from_cstr("CMakeLists.txt");
    opts.output_path = nob_sv_from_cstr("nob.c");
    ASSERT(!nob_codegen_render(build.model, codegen_arena, &opts, &sb));

    nob_sb_free(sb);
    arena_destroy(builder_arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    arena_destroy(codegen_arena);
    TEST_PASS();
}

TEST(codegen_rejects_supported_replay_opcode_outside_configure_phase) {
    Arena *builder_arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Arena *codegen_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    Nob_String_Builder sb = {0};
    Nob_Codegen_Options opts = {
        .input_path = {0},
        .output_path = {0},
        .target_platform = NOB_CODEGEN_PLATFORM_HOST,
        .backend = NOB_CODEGEN_BACKEND_AUTO,
    };

    ASSERT(builder_arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);
    ASSERT(codegen_arena != NULL);

    stream = event_stream_create(builder_arena);
    ASSERT(stream != NULL);

    codegen_reject_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_PROJECT_DECLARE, 2);
    ev.as.project_declare.name = nob_sv_from_cstr("ReplayPhaseReject");
    ev.as.project_declare.languages = nob_sv_from_cstr("C");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 3);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("build_write");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_FILESYSTEM;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_FS_WRITE_TEXT;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_BUILD;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 4);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("build_write");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("generated.txt");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 5);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("build_write");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("payload");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 6);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("build_write");
    ev.as.replay_action_add_argv.arg_index = 1;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("");
    ASSERT(event_stream_push(stream, &ev));

    codegen_reject_init_event(&ev, EVENT_DIRECTORY_LEAVE, 7);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(builder_arena,
                                                          validate_arena,
                                                          model_arena,
                                                          stream,
                                                          &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);
    ASSERT(bm_query_replay_action_count(build.model) == 1);

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    opts.input_path = nob_sv_from_cstr("CMakeLists.txt");
    opts.output_path = nob_sv_from_cstr("nob.c");
    ASSERT(!nob_codegen_render(build.model, codegen_arena, &opts, &sb));

    nob_sb_free(sb);
    arena_destroy(builder_arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    arena_destroy(codegen_arena);
    TEST_PASS();
}

void run_codegen_v2_reject_tests(int *passed, int *failed, int *skipped) {
    test_codegen_rejects_module_target_as_link_dependency(passed, failed, skipped);
    test_codegen_rejects_unsupported_generator_expression_operator(passed, failed, skipped);
    test_codegen_rejects_imported_executable_as_link_dependency(passed, failed, skipped);
    test_codegen_rejects_imported_module_target_as_link_dependency(passed, failed, skipped);
    test_codegen_rejects_append_custom_command_steps(passed, failed, skipped);
    test_codegen_rejects_targets_with_precompile_headers(passed, failed, skipped);
    test_codegen_rejects_export_append_in_standalone_export_backend(passed, failed, skipped);
    test_codegen_rejects_export_cxx_modules_directory_in_standalone_export_backend(passed, failed, skipped);
    test_codegen_rejects_invalid_platform_backend_pair(passed, failed, skipped);
    test_codegen_rejects_macosx_bundle_targets(passed, failed, skipped);
    test_codegen_rejects_cpack_archive_component_install_before_render(passed, failed, skipped);
    test_codegen_rejects_remote_download_replay_before_render(passed, failed, skipped);
    test_codegen_rejects_unsupported_archive_variant_before_render(passed, failed, skipped);
    test_codegen_rejects_get_runtime_dependencies_before_render(passed, failed, skipped);
    test_codegen_rejects_replay_actions_before_render(passed, failed, skipped);
    test_codegen_rejects_supported_replay_opcode_outside_configure_phase(passed, failed, skipped);
}
