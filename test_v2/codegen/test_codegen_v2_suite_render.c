#include "test_codegen_v2_common.h"

TEST(codegen_simple_executable_generates_compilable_nob) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test C)\n"
        "add_executable(app main.c)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "#define NOB_IMPLEMENTATION") != NULL);
    ASSERT(strstr(output, "#include \"nob.h\"") != NULL);
    ASSERT(strstr(output, "int main(int argc, char **argv)") != NULL);
    ASSERT(strstr(output, "build/app") != NULL);
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
    ASSERT(strstr(output, "build/libcore.a") != NULL);
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

void run_codegen_v2_render_tests(int *passed, int *failed, int *skipped) {
    test_codegen_simple_executable_generates_compilable_nob(passed, failed, skipped);
    test_codegen_static_interface_alias_usage_propagates_flags(passed, failed, skipped);
    test_codegen_output_properties_shape_artifact_paths(passed, failed, skipped);
}
