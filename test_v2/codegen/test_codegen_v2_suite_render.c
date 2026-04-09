#include "test_codegen_v2_common.h"

TEST(codegen_simple_executable_generates_compilable_nob) {
    Nob_String_Builder sb = {0};
    Codegen_Test_Config config = {
        .input_path = "render_src/CMakeLists.txt",
        .output_path = "render_src/nob.c",
        .source_dir = "render_src",
        .binary_dir = "render_build",
    };
    ASSERT(codegen_render_script_with_config(
        "project(Test C)\n"
        "add_executable(app main.c)\n",
        &config,
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "#define NOB_IMPLEMENTATION") != NULL);
    ASSERT(strstr(output, "#include \"nob.h\"") != NULL);
    ASSERT(strstr(output, "int main(int argc, char **argv)") != NULL);
    ASSERT(strstr(output, "../render_build/app") != NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_static_interface_alias_usage_propagates_flags) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test C)\n"
        "add_library(iface INTERFACE)\n"
        "target_include_directories(iface INTERFACE inc)\n"
        "target_compile_definitions(iface INTERFACE IFACE=1)\n"
        "target_compile_options(iface INTERFACE -Wshadow)\n"
        "target_link_options(iface INTERFACE -Wl,--as-needed)\n"
        "target_link_directories(iface INTERFACE libs)\n"
        "target_link_libraries(iface INTERFACE m)\n"
        "add_library(core STATIC core.c)\n"
        "target_link_libraries(core PUBLIC iface)\n"
        "add_library(core_alias ALIAS core)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core_alias pthread)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "-Iinc") != NULL);
    ASSERT(strstr(output, "-DIFACE=1") != NULL);
    ASSERT(strstr(output, "-Wshadow") != NULL);
    ASSERT(strstr(output, "-Wl,--as-needed") != NULL);
    ASSERT(strstr(output, "-Llibs") != NULL);
    ASSERT(strstr(output, "-lm") != NULL);
    ASSERT(strstr(output, "-lpthread") != NULL);
    ASSERT(strstr(output, "libcore.a") != NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_output_properties_shape_artifact_paths) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES OUTPUT_NAME fancy PREFIX pre_ SUFFIX .pkg ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_library(plugin SHARED plugin.c)\n"
        "set_target_properties(plugin PROPERTIES OUTPUT_NAME dyn PREFIX mod_ SUFFIX .sox LIBRARY_OUTPUT_DIRECTORY artifacts/shlib)\n"
        "add_library(bundle MODULE bundle.c)\n"
        "set_target_properties(bundle PROPERTIES LIBRARY_OUTPUT_DIRECTORY artifacts/modules)\n"
        "add_executable(app main.c)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME runner RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "artifacts/lib/pre_fancy.pkg") != NULL);
    ASSERT(strstr(output, "artifacts/shlib/mod_dyn.sox") != NULL);
    ASSERT(strstr(output, "artifacts/modules/libbundle.so") != NULL);
    ASSERT(strstr(output, "artifacts/bin/runner") != NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_runtime_emits_only_helpers_needed_for_simple_builds) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test C)\n"
        "add_executable(app main.c)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "remove_path_recursive(") != NULL);
    ASSERT(strstr(output, "require_paths(") != NULL);
    ASSERT(strstr(output, "resolve_cpack_bin(") == NULL);
    ASSERT(strstr(output, "install_copy_file(") == NULL);
    ASSERT(strstr(output, "install_copy_directory(") == NULL);
    ASSERT(strstr(output, "append_archive_tool_cmd(") == NULL);
    ASSERT(strstr(output, "resolve_link_bin(") == NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_export_only_render_does_not_emit_install_only_helpers) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "export(TARGETS core FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/CoreTargets.cmake)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "join_install_prefix(") == NULL);
    ASSERT(strstr(output, "install_component_matches(") == NULL);
    ASSERT(strstr(output, "install_copy_file(") == NULL);
    ASSERT(strstr(output, "install_copy_directory(") == NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_generated_nob_compiles_cleanly_with_werror_for_representative_paths) {
    const char *install_script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)\n"
        "add_executable(app main.c)\n"
        "install(TARGETS app DESTINATION bin)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include/demo)\n"
        "install(EXPORT DemoTargets DESTINATION lib/cmake/demo FILE DemoTargets.cmake)\n";
    const char *export_script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "export(TARGETS core FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/CoreTargets.cmake)\n";
    const char *package_script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib)\n"
        "set(CPACK_GENERATOR \"ZIP\")\n"
        "set(CPACK_PACKAGE_NAME \"DemoPkg\")\n"
        "set(CPACK_PACKAGE_VERSION \"1.2.3\")\n"
        "set(CPACK_PACKAGE_FILE_NAME \"demo-pkg\")\n"
        "include(CPack)\n";

    ASSERT(codegen_write_text_file("strict_simple_src/main.c", "int main(void) { return 0; }\n"));
    ASSERT(codegen_write_script_with_config(
        "project(Test C)\n"
        "add_executable(app main.c)\n",
        &(Codegen_Test_Config){
            .input_path = "strict_simple_src/CMakeLists.txt",
            .output_path = "strict_simple_nob.c",
            .source_dir = "strict_simple_src",
            .binary_dir = "strict_simple_build",
        }));
    ASSERT(codegen_compile_generated_nob_strict("strict_simple_nob.c", "strict_simple_nob_gen"));

    ASSERT(codegen_write_text_file("strict_install_src/core.c", "int core_value(void) { return 1; }\n"));
    ASSERT(codegen_write_text_file("strict_install_src/include/core.h", "#define CORE_VALUE 1\n"));
    ASSERT(codegen_write_text_file("strict_install_src/main.c", "int main(void) { return 0; }\n"));
    ASSERT(codegen_write_script_with_config(
        install_script,
        &(Codegen_Test_Config){
            .input_path = "strict_install_src/CMakeLists.txt",
            .output_path = "strict_install_nob.c",
            .source_dir = "strict_install_src",
            .binary_dir = "strict_install_build",
        }));
    ASSERT(codegen_compile_generated_nob_strict("strict_install_nob.c", "strict_install_nob_gen"));

    ASSERT(codegen_write_text_file("strict_export_src/core.c", "int core_value(void) { return 1; }\n"));
    ASSERT(codegen_write_script_with_config(
        export_script,
        &(Codegen_Test_Config){
            .input_path = "strict_export_src/CMakeLists.txt",
            .output_path = "strict_export_nob.c",
            .source_dir = "strict_export_src",
            .binary_dir = "strict_export_build",
        }));
    ASSERT(codegen_compile_generated_nob_strict("strict_export_nob.c", "strict_export_nob_gen"));

    ASSERT(codegen_write_text_file("strict_package_src/core.c", "int core_value(void) { return 1; }\n"));
    ASSERT(codegen_write_script_with_config(
        package_script,
        &(Codegen_Test_Config){
            .input_path = "strict_package_src/CMakeLists.txt",
            .output_path = "strict_package_nob.c",
            .source_dir = "strict_package_src",
            .binary_dir = "strict_package_build",
        }));
    ASSERT(codegen_compile_generated_nob_strict("strict_package_nob.c", "strict_package_nob_gen"));

    TEST_PASS();
}

TEST(codegen_render_multi_config_mixed_language_and_imported_queries_stay_stable) {
    Nob_String_Builder sb = {0};
    Codegen_Test_Config config = {
        .input_path = "memo_codegen_src/CMakeLists.txt",
        .output_path = "memo_codegen_nob.c",
        .source_dir = "memo_codegen_src",
        .binary_dir = "memo_codegen_build",
    };
    ASSERT(codegen_render_script_with_config(
        "project(Test LANGUAGES C CXX)\n"
        "add_library(iface INTERFACE)\n"
        "target_include_directories(iface INTERFACE\n"
        "  \"$<BUILD_INTERFACE:iface/include>\"\n"
        "  \"$<INSTALL_INTERFACE:iface_install/include>\")\n"
        "target_compile_definitions(iface INTERFACE\n"
        "  \"$<$<CONFIG:Debug>:DBG_MODE>\"\n"
        "  \"$<$<COMPILE_LANGUAGE:C>:C_ONLY>\"\n"
        "  \"$<$<COMPILE_LANGUAGE:CXX>:CXX_ONLY>\"\n"
        "  \"$<$<PLATFORM_ID:Linux>:LINUX_ONLY>\")\n"
        "target_link_libraries(iface INTERFACE \"$<LINK_ONLY:m>\")\n"
        "add_library(ext SHARED IMPORTED)\n"
        "set_target_properties(ext PROPERTIES\n"
        "  IMPORTED_LOCATION imports/libbase.so\n"
        "  IMPORTED_LOCATION_DEBUG imports/libdebug.so\n"
        "  IMPORTED_IMPLIB_DEBUG imports/libdebug_link.so\n"
        "  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug\n"
        "  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG \"CXX;C\")\n"
        "add_executable(app main.c main.cpp)\n"
        "target_link_libraries(app PRIVATE iface ext)\n",
        &config,
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "DBG_MODE") != NULL);
    ASSERT(strstr(output, "C_ONLY") != NULL);
    ASSERT(strstr(output, "CXX_ONLY") != NULL);
    ASSERT(strstr(output, "LINUX_ONLY") != NULL);
    ASSERT(strstr(output, "imports/libbase.so") != NULL);
    ASSERT(strstr(output, "imports/libdebug_link.so") != NULL);
    ASSERT(strstr(output, "config_matches(g_build_config") != NULL);
    ASSERT(strstr(output, "-lm") != NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

void run_codegen_v2_render_tests(int *passed, int *failed, int *skipped) {
    test_codegen_simple_executable_generates_compilable_nob(passed, failed, skipped);
    test_codegen_static_interface_alias_usage_propagates_flags(passed, failed, skipped);
    test_codegen_output_properties_shape_artifact_paths(passed, failed, skipped);
    test_codegen_runtime_emits_only_helpers_needed_for_simple_builds(passed, failed, skipped);
    test_codegen_export_only_render_does_not_emit_install_only_helpers(passed, failed, skipped);
    test_codegen_generated_nob_compiles_cleanly_with_werror_for_representative_paths(passed, failed, skipped);
    test_codegen_render_multi_config_mixed_language_and_imported_queries_stay_stable(passed, failed, skipped);
}
