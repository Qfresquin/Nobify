#include "test_codegen_v2_common.h"
#include "test_fs.h"

TEST(codegen_write_file_rebases_source_and_binary_roots_for_out_of_source_nob) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    Codegen_Test_Config config = {
        .input_path = "rebase_src/CMakeLists.txt",
        .output_path = "rebase_src/generated/nob.c",
        .source_dir = "rebase_src",
        .binary_dir = "rebase_build",
    };
    ASSERT(arena != NULL);

    ASSERT(codegen_write_text_file("rebase_src/src/main.c", "int main(void) { return 0; }\n"));
    ASSERT(codegen_write_script_with_config(
        "project(Test C)\n"
        "add_executable(app src/main.c)\n",
        &config));

    ASSERT(codegen_load_text_file_to_arena(arena, "rebase_src/generated/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "../src/main.c"));
    ASSERT(codegen_sv_contains(generated, "../../rebase_build/app"));
    ASSERT(codegen_sv_contains(generated, "../../rebase_build/.nob/obj"));
    ASSERT(codegen_compile_generated_nob("rebase_src/generated/nob.c", "rebase_src/generated/nob_gen"));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_default_out_of_source_top_level_targets_build_in_binary_root) {
    const char *script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "add_library(shared SHARED shared.c)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n";
    Codegen_Test_Config config = {
        .input_path = "default_src/CMakeLists.txt",
        .output_path = "default_src/generated/default/nob.c",
        .source_dir = "default_src",
        .binary_dir = "default_build",
    };

    ASSERT(codegen_write_text_file("default_src/core.c", "int core_value(void) { return 7; }\n"));
    ASSERT(codegen_write_text_file("default_src/shared.c", "int shared_value(void) { return 9; }\n"));
    ASSERT(codegen_write_text_file("default_src/plugin.c", "int plugin_value(void) { return 11; }\n"));
    ASSERT(codegen_write_text_file(
        "default_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 7 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("default_src/generated/default/nob.c",
                                         "default_src/generated/default/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("default_src/generated/default", "./nob_gen", NULL, NULL));
    ASSERT(test_ws_host_path_exists("default_build/app"));
    ASSERT(test_ws_host_path_exists("default_build/libcore.a"));
    ASSERT(test_ws_host_path_exists("default_build/libshared.so"));
    ASSERT(test_ws_host_path_exists("default_build/libplugin.so"));
    ASSERT(!test_ws_host_path_exists("default_src/app"));
    ASSERT(!test_ws_host_path_exists("default_src/libcore.a"));
    ASSERT(!test_ws_host_path_exists("default_src/libshared.so"));
    ASSERT(!test_ws_host_path_exists("default_src/libplugin.so"));
    TEST_PASS();
}

TEST(codegen_default_out_of_source_subdirectory_uses_owner_binary_dirs) {
    Test_Fs_Path_Info app_dir_info = {0};
    const char *root_script =
        "project(Test C)\n"
        "add_subdirectory(lib)\n"
        "add_subdirectory(app)\n";
    Codegen_Test_Config config = {
        .input_path = "subdir_src/CMakeLists.txt",
        .output_path = "subdir_src/generated/subdirs/nob.c",
        .source_dir = "subdir_src",
        .binary_dir = "subdir_build",
    };

    ASSERT(codegen_write_text_file(
        "subdir_src/lib/CMakeLists.txt",
        "add_library(core STATIC core.c)\n"
        "add_library(shared SHARED shared.c)\n"
        "add_library(plugin MODULE plugin.c)\n"));
    ASSERT(codegen_write_text_file(
        "subdir_src/app/CMakeLists.txt",
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"));
    ASSERT(codegen_write_text_file("subdir_src/lib/core.c", "int core_value(void) { return 3; }\n"));
    ASSERT(codegen_write_text_file("subdir_src/lib/shared.c", "int shared_value(void) { return 5; }\n"));
    ASSERT(codegen_write_text_file("subdir_src/lib/plugin.c", "int plugin_value(void) { return 7; }\n"));
    ASSERT(codegen_write_text_file(
        "subdir_src/app/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 3 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(root_script, &config));
    ASSERT(codegen_compile_generated_nob("subdir_src/generated/subdirs/nob.c",
                                         "subdir_src/generated/subdirs/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("subdir_src/generated/subdirs", "./nob_gen", NULL, NULL));
    ASSERT(test_ws_host_path_exists("subdir_build/lib/libcore.a"));
    ASSERT(test_ws_host_path_exists("subdir_build/lib/libshared.so"));
    ASSERT(test_ws_host_path_exists("subdir_build/lib/libplugin.so"));
    ASSERT(test_ws_host_path_exists("subdir_build/app/app"));
    ASSERT(!test_ws_host_path_exists("subdir_build/libcore.a"));
    ASSERT(test_fs_get_path_info("subdir_build/app", &app_dir_info));
    ASSERT(app_dir_info.exists);
    ASSERT(app_dir_info.is_dir);
    TEST_PASS();
}

TEST(codegen_explicit_output_directories_shape_out_of_source_artifacts) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    const char *script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES OUTPUT_NAME corex ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_library(shared SHARED shared.c)\n"
        "set_target_properties(shared PROPERTIES OUTPUT_NAME sharedx LIBRARY_OUTPUT_DIRECTORY artifacts/shlib)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "set_target_properties(plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY artifacts/modules)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME runner RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n";
    Codegen_Test_Config config = {
        .input_path = "explicit_src/CMakeLists.txt",
        .output_path = "explicit_src/generated/explicit/nob.c",
        .source_dir = "explicit_src",
        .binary_dir = "explicit_build",
    };
    ASSERT(arena != NULL);

    ASSERT(codegen_write_text_file("explicit_src/core.c", "int core_value(void) { return 13; }\n"));
    ASSERT(codegen_write_text_file("explicit_src/shared.c", "int shared_value(void) { return 17; }\n"));
    ASSERT(codegen_write_text_file("explicit_src/plugin.c", "int plugin_value(void) { return 19; }\n"));
    ASSERT(codegen_write_text_file(
        "explicit_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 13 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_load_text_file_to_arena(arena, "explicit_src/generated/explicit/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "\"-shared\""));
    ASSERT(codegen_sv_contains(generated, "\"-fPIC\""));
    ASSERT(codegen_sv_contains(generated, "../../../explicit_build/artifacts/bin/runner"));
    ASSERT(codegen_compile_generated_nob("explicit_src/generated/explicit/nob.c",
                                         "explicit_src/generated/explicit/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("explicit_src/generated/explicit", "./nob_gen", NULL, NULL));
    ASSERT(test_ws_host_path_exists("explicit_build/artifacts/bin/runner"));
    ASSERT(test_ws_host_path_exists("explicit_build/artifacts/lib/libcorex.a"));
    ASSERT(test_ws_host_path_exists("explicit_build/artifacts/shlib/libsharedx.so"));
    ASSERT(test_ws_host_path_exists("explicit_build/artifacts/modules/libplugin.so"));
    ASSERT(!test_ws_host_path_exists("explicit_src/artifacts"));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_cxx_static_dependency_uses_cxx_driver_for_link_out_of_source) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    const char *script =
        "project(Test C CXX)\n"
        "add_library(core STATIC core.cpp)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n";
    Codegen_Test_Config config = {
        .input_path = "cxx_src/CMakeLists.txt",
        .output_path = "cxx_src/generated/cxx/nob.c",
        .source_dir = "cxx_src",
        .binary_dir = "cxx_build",
    };
    ASSERT(arena != NULL);
    ASSERT(codegen_write_text_file(
        "cxx_src/core.cpp",
        "#include <string>\n"
        "extern \"C\" int core_value(void) {\n"
        "    static std::string text = \"seven\";\n"
        "    return (int)text.size();\n"
        "}\n"));
    ASSERT(codegen_write_text_file(
        "cxx_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 5 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_load_text_file_to_arena(arena, "cxx_src/generated/cxx/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "append_toolchain_cmd(&cc_cmd, true);"));
    ASSERT(codegen_sv_contains(generated, "append_toolchain_cmd(&link_cmd, true);"));
    ASSERT(codegen_compile_generated_nob("cxx_src/generated/cxx/nob.c", "cxx_src/generated/cxx/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("cxx_src/generated/cxx", "./nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("cxx_build/app"));
    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_clean_removes_out_of_source_outputs_but_preserves_binary_root) {
    const char *script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "add_library(shared SHARED shared.c)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n";
    Codegen_Test_Config config = {
        .input_path = "clean_src/CMakeLists.txt",
        .output_path = "clean_src/generated/clean/nob.c",
        .source_dir = "clean_src",
        .binary_dir = "clean_build",
    };

    ASSERT(codegen_write_text_file("clean_src/core.c", "int core_value(void) { return 21; }\n"));
    ASSERT(codegen_write_text_file("clean_src/shared.c", "int shared_value(void) { return 22; }\n"));
    ASSERT(codegen_write_text_file("clean_src/plugin.c", "int plugin_value(void) { return 23; }\n"));
    ASSERT(codegen_write_text_file(
        "clean_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 21 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("clean_src/generated/clean/nob.c",
                                         "clean_src/generated/clean/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("clean_src/generated/clean", "./nob_gen", NULL, NULL));
    ASSERT(test_ws_host_path_exists("clean_build/.nob"));
    ASSERT(test_ws_host_path_exists("clean_build/app"));
    ASSERT(codegen_run_binary_in_dir("clean_src/generated/clean", "./nob_gen", "clean", NULL));
    ASSERT(test_ws_host_path_exists("clean_build"));
    ASSERT(!test_ws_host_path_exists("clean_build/.nob"));
    ASSERT(!test_ws_host_path_exists("clean_build/app"));
    ASSERT(!test_ws_host_path_exists("clean_build/libcore.a"));
    ASSERT(!test_ws_host_path_exists("clean_build/libshared.so"));
    ASSERT(!test_ws_host_path_exists("clean_build/libplugin.so"));
    TEST_PASS();
}

TEST(codegen_ignores_cxx_modules_file_set_metadata_in_compile_inputs) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test CXX)\n"
        "add_library(core STATIC core.cpp)\n"
        "target_sources(core PUBLIC FILE_SET CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "add_executable(app main.cpp)\n"
        "target_link_libraries(app PRIVATE core)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "core.cpp") != NULL);
    ASSERT(strstr(output, "main.cpp") != NULL);
    ASSERT(strstr(output, "core.cppm") == NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

void run_codegen_v2_build_tests(int *passed, int *failed, int *skipped) {
    test_codegen_write_file_rebases_source_and_binary_roots_for_out_of_source_nob(passed, failed, skipped);
    test_codegen_default_out_of_source_top_level_targets_build_in_binary_root(passed, failed, skipped);
    test_codegen_default_out_of_source_subdirectory_uses_owner_binary_dirs(passed, failed, skipped);
    test_codegen_explicit_output_directories_shape_out_of_source_artifacts(passed, failed, skipped);
    test_codegen_cxx_static_dependency_uses_cxx_driver_for_link_out_of_source(passed, failed, skipped);
    test_codegen_clean_removes_out_of_source_outputs_but_preserves_binary_root(passed, failed, skipped);
    test_codegen_ignores_cxx_modules_file_set_metadata_in_compile_inputs(passed, failed, skipped);
}
