#include "test_codegen_v2_common.h"

TEST(codegen_write_file_rebases_paths_and_generated_file_compiles) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    ASSERT(arena != NULL);

    ASSERT(codegen_write_script(
        "project(Test C)\n"
        "add_executable(app src/main.c)\n",
        "CMakeLists.txt",
        "generated/out/nob.c"));

    ASSERT(codegen_load_text_file_to_arena(arena, "generated/out/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "../../src/main.c"));
    ASSERT(codegen_compile_generated_nob("generated/out/nob.c", "generated/out/nob_gen"));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_shared_and_module_targets_build_on_posix_backend) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    const char *script =
        "project(Test C)\n"
        "add_library(core SHARED core.c)\n"
        "set_target_properties(core PROPERTIES OUTPUT_NAME corex LIBRARY_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "set_target_properties(plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY artifacts/modules)\n";
    ASSERT(arena != NULL);
    ASSERT(codegen_write_text_file("core.c", "int core_value(void) { return 7; }\n"));
    ASSERT(codegen_write_text_file("plugin.c", "int plugin_value(void) { return 11; }\n"));
    ASSERT(codegen_write_script(script, "CMakeLists.txt", "generated/shared/nob.c"));
    ASSERT(codegen_load_text_file_to_arena(arena, "generated/shared/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "\"-shared\""));
    ASSERT(codegen_sv_contains(generated, "\"-fPIC\""));
    ASSERT(codegen_compile_generated_nob("generated/shared/nob.c", "generated/shared/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("generated/shared", "./nob_gen", NULL, NULL));
    ASSERT(nob_file_exists("generated/shared/../../artifacts/lib/libcorex.so"));
    ASSERT(nob_file_exists("generated/shared/../../artifacts/modules/libplugin.so"));
    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_cxx_static_dependency_uses_cxx_driver_for_link) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    const char *script =
        "project(Test C CXX)\n"
        "add_library(core STATIC core.cpp)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n";
    ASSERT(arena != NULL);
    ASSERT(codegen_write_text_file(
        "core.cpp",
        "#include <string>\n"
        "extern \"C\" int core_value(void) {\n"
        "    static std::string text = \"seven\";\n"
        "    return (int)text.size();\n"
        "}\n"));
    ASSERT(codegen_write_text_file(
        "main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 5 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script(script, "CMakeLists.txt", "generated/cxx/nob.c"));
    ASSERT(codegen_load_text_file_to_arena(arena, "generated/cxx/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "append_toolchain_cmd(&cc_cmd, true);"));
    ASSERT(codegen_sv_contains(generated, "append_toolchain_cmd(&link_cmd, true);"));
    ASSERT(codegen_compile_generated_nob("generated/cxx/nob.c", "generated/cxx/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("generated/cxx", "./nob_gen", "app", NULL));
    ASSERT(nob_file_exists("generated/cxx/../../build/app"));
    arena_destroy(arena);
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

void run_codegen_v2_build_tests(int *passed, int *failed) {
    test_codegen_write_file_rebases_paths_and_generated_file_compiles(passed, failed);
    test_codegen_shared_and_module_targets_build_on_posix_backend(passed, failed);
    test_codegen_cxx_static_dependency_uses_cxx_driver_for_link(passed, failed);
    test_codegen_ignores_cxx_modules_file_set_metadata_in_compile_inputs(passed, failed);
}
