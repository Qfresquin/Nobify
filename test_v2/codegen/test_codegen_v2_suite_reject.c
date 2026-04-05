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

void run_codegen_v2_reject_tests(int *passed, int *failed, int *skipped) {
    test_codegen_rejects_module_target_as_link_dependency(passed, failed, skipped);
    test_codegen_rejects_unsupported_generator_expression_operator(passed, failed, skipped);
    test_codegen_rejects_imported_executable_as_link_dependency(passed, failed, skipped);
    test_codegen_rejects_imported_module_target_as_link_dependency(passed, failed, skipped);
    test_codegen_rejects_append_custom_command_steps(passed, failed, skipped);
    test_codegen_rejects_targets_with_precompile_headers(passed, failed, skipped);
}
