#include "test_codegen_v2_common.h"

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
}
