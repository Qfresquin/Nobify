#include "test_evaluator_v2_shared.h"

TEST(target_include_directories) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_include_directories(app PRIVATE include/)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Iinclude/\")") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_directories_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "include_directories(global_inc)\n"
        "include_directories(SYSTEM sys_inc)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Iglobal_inc\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Isys_inc\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include_directories") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_regular_expression_sets_builtin_vars) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "include_regular_expression(\"foo.*\" \"bar.*\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"RX_${CMAKE_INCLUDE_REGULAR_EXPRESSION}\" \"RC_${CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DRX_foo.*\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DRC_bar.*\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include_regular_expression") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(find_package_zlib) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_find_package_zlib");
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_zlib"));
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_zlib/CMake"));
    write_test_file("temp_find_package_zlib/CMake/FindZLIB.cmake",
        "set(ZLIB_FOUND TRUE)\n"
        "set(ZLIB_LIBRARIES z)\n");

    const char *input = 
        "set(CMAKE_MODULE_PATH temp_find_package_zlib/CMake)\n"
        "find_package(ZLIB MODULE REQUIRED)\n"
        "add_executable(app_${ZLIB_LIBRARIES} main.c)";
    
    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_z") != NULL);
    ASSERT(diag_has_errors() == false);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_find_package_zlib");
    TEST_PASS();
}

TEST(find_package_target_link_usage) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_find_package_link");
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_link"));
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_link/CMake"));
    write_test_file("temp_find_package_link/CMake/FindZLIB.cmake",
        "set(ZLIB_FOUND TRUE)\n"
        "set(ZLIB_LIBRARIES z)\n");

    const char *input =
        "project(Test)\n"
        "set(CMAKE_MODULE_PATH temp_find_package_link/CMake)\n"
        "find_package(ZLIB MODULE REQUIRED)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE ZLIB::ZLIB)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-lz\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(find_package_required_reports_error_when_missing) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "find_package(DefinitelyMissing REQUIRED)\n"
        "add_executable(app_${DefinitelyMissing_FOUND} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_FALSE") == NULL);
    ASSERT(diag_has_errors() == true);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_find_package_link");
    TEST_PASS();
}

TEST(find_package_config_mode_uses_dir_and_config_vars) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_find_package_config");
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_config"));
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_config/MyPkg"));
    write_test_file("temp_find_package_config/MyPkg/MyPkgConfig.cmake",
        "set(MyPkg_FOUND TRUE)\n"
        "set(MyPkg_LIBRARIES mypkg)\n");

    const char *input =
        "project(Test)\n"
        "set(MyPkg_DIR temp_find_package_config/MyPkg)\n"
        "find_package(MyPkg CONFIG REQUIRED)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"PKG_${MyPkg_FOUND}\" \"DIR_${MyPkg_DIR}\" \"CFG_${MyPkg_CONFIG}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DPKG_TRUE") != NULL);
    ASSERT(strstr(output, "temp_find_package_config/MyPkg") != NULL);
    ASSERT(strstr(output, "MyPkgConfig.cmake") != NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_find_package_config");
    TEST_PASS();
}

TEST(find_package_no_default_path_ignores_dir_and_prefix_defaults) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_find_package_nodefault");
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_nodefault"));
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_nodefault/MyPkg"));
    write_test_file("temp_find_package_nodefault/MyPkg/MyPkgConfig.cmake",
        "set(MyPkg_FOUND TRUE)\n"
        "set(MyPkg_LIBRARIES mypkg)\n");

    const char *input =
        "project(Test)\n"
        "set(MyPkg_DIR temp_find_package_nodefault/MyPkg)\n"
        "set(CMAKE_PREFIX_PATH temp_find_package_nodefault)\n"
        "find_package(MyPkg CONFIG QUIET NO_DEFAULT_PATH)\n"
        "set(FIRST_RESULT ${MyPkg_FOUND})\n"
        "find_package(MyPkg CONFIG QUIET NO_DEFAULT_PATH PATHS temp_find_package_nodefault/MyPkg)\n"
        "add_executable(app_${FIRST_RESULT}_${MyPkg_FOUND} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app_FALSE_TRUE") != NULL);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_find_package_nodefault");
    TEST_PASS();
}

TEST(find_package_module_mode_prefers_module_resolution) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_find_package_module_pref");
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_module_pref"));
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_module_pref/CMake"));
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_module_pref/config"));
    write_test_file("temp_find_package_module_pref/CMake/FindZLIB.cmake",
        "set(ZLIB_FOUND TRUE)\n"
        "set(ZLIB_LIBRARIES zmod)\n");
    write_test_file("temp_find_package_module_pref/config/ZLIBConfig.cmake",
        "set(ZLIB_FOUND TRUE)\n"
        "set(ZLIB_LIBRARIES zcfg)\n");

    const char *input =
        "project(Test)\n"
        "set(CMAKE_MODULE_PATH temp_find_package_module_pref/CMake)\n"
        "set(ZLIB_DIR temp_find_package_module_pref/config)\n"
        "find_package(ZLIB MODULE REQUIRED)\n"
        "add_executable(app_${ZLIB_LIBRARIES} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app_zmod") != NULL);
    ASSERT(strstr(output, "cmd_app_zcfg") == NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_find_package_module_pref");
    TEST_PASS();
}

TEST(find_package_exact_version_mismatch_sets_not_found) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "find_package(ZLIB 9.9 EXACT QUIET)\n"
        "add_executable(app_${ZLIB_FOUND} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app_FALSE") != NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(find_package_components_and_component_imported_target) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_find_package_components");
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_components"));
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_components/bar"));
    ASSERT(nob_mkdir_if_not_exists("temp_find_package_components/foo"));
    write_test_file("temp_find_package_components/bar/BarConfig.cmake",
        "set(Bar_FOUND TRUE)\n"
        "set(Bar_LIBRARIES Bar)\n");
    write_test_file("temp_find_package_components/foo/FooConfig.cmake",
        "set(Foo_FOUND TRUE)\n"
        "set(Foo_LIBRARIES Foo)\n");

    const char *input =
        "project(Test)\n"
        "set(Bar_DIR temp_find_package_components/bar)\n"
        "set(Bar_AVAILABLE_COMPONENTS core;net)\n"
        "find_package(Bar CONFIG QUIET COMPONENTS core gui OPTIONAL_COMPONENTS net)\n"
        "add_executable(app_${Bar_FOUND}_${Bar_core_FOUND}_${Bar_gui_FOUND}_${Bar_net_FOUND} main.c)\n"
        "set(Foo_DIR temp_find_package_components/foo)\n"
        "set(Foo_AVAILABLE_COMPONENTS core;net)\n"
        "find_package(Foo CONFIG REQUIRED COMPONENTS core)\n"
        "add_executable(app_link main.c)\n"
        "target_link_libraries(app_link PRIVATE Foo::core)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app_FALSE_TRUE_FALSE_TRUE") != NULL);
#if defined(_WIN32)
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app_link, \"Foo.lib\")") != NULL ||
           strstr(output, "nob_cmd_append(&cmd_app_link, \"-lFoo\")") != NULL);
#else
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app_link, \"-lFoo\")") != NULL);
#endif
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_find_package_components");
    TEST_PASS();
}

TEST(cmake_pkg_config_imported_target_and_vars) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_fake_pkg_config.c",
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "int main(int argc, char **argv) {\n"
        "    if (argc < 2) return 1;\n"
        "    if (strcmp(argv[1], \"--version\") == 0) { puts(\"9.9.9\"); return 0; }\n"
        "    if (strcmp(argv[1], \"--exists\") == 0) { return 0; }\n"
        "    if (strcmp(argv[1], \"--modversion\") == 0) { puts(\"1.2.3\"); return 0; }\n"
        "    if (strcmp(argv[1], \"--libs\") == 0) { puts(\"-L/fake/lib -lz\"); return 0; }\n"
        "    if (strcmp(argv[1], \"--cflags\") == 0) { puts(\"-I/fake/include -DZLIB_OK\"); return 0; }\n"
        "    if (strcmp(argv[1], \"--libs-only-L\") == 0) { puts(\"-L/fake/lib\"); return 0; }\n"
        "    if (strcmp(argv[1], \"--cflags-only-I\") == 0) { puts(\"-I/fake/include\"); return 0; }\n"
        "    return 1;\n"
        "}\n");
#if defined(_WIN32)
    ASSERT(run_shell_command_silent("cc temp_fake_pkg_config.c -o temp_fake_pkg_config.exe") == 0);
    const char *fake_pkg_exe = "./temp_fake_pkg_config.exe";
#else
    ASSERT(run_shell_command_silent("cc temp_fake_pkg_config.c -o temp_fake_pkg_config") == 0);
    const char *fake_pkg_exe = "./temp_fake_pkg_config";
#endif
    char *input = nob_temp_sprintf(
        "project(Test)\n"
        "set(PKG_CONFIG_EXECUTABLE %s)\n"
        "cmake_pkg_config(IMPORT zlib PREFIX ZLIBPC IMPORTED_TARGET REQUIRED VERSION 1.0)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE PkgConfig::ZLIBPC)\n"
        "target_compile_definitions(app PRIVATE "
        "\"PCF_${ZLIBPC_FOUND}\" "
        "\"PCV_${ZLIBPC_VERSION}\" "
        "\"PCL_${ZLIBPC_LIBRARIES}\")",
        fake_pkg_exe);

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DPCF_TRUE") != NULL);
    ASSERT(strstr(output, "-DPCV_1.2.3") != NULL);
    ASSERT(strstr(output, "-DPCL_-L/fake/lib;-lz") != NULL);
#if defined(_WIN32)
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"z.lib\")") != NULL);
#else
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-lz\")") != NULL);
#endif
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_pkg_config") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_fake_pkg_config.c");
#if defined(_WIN32)
    nob_delete_file("temp_fake_pkg_config.exe");
#else
    nob_delete_file("temp_fake_pkg_config");
#endif
    TEST_PASS();
}

TEST(find_program_and_find_library_basic) {
    Arena *arena = arena_create(1024 * 1024);
    nob_mkdir_if_not_exists("temp_find");
    nob_mkdir_if_not_exists("temp_find/bin");
    nob_mkdir_if_not_exists("temp_find/lib");
    write_test_file("temp_find/bin/mytool", "echo tool\n");
    write_test_file("temp_find/bin/mytool.exe", "MZ");
    write_test_file("temp_find/lib/libmylib.a", "!<arch>\n");
    write_test_file("temp_find/lib/mylib.lib", "LIB");

    const char *input =
        "project(Test)\n"
        "find_program(MY_PROG NAMES mytool PATHS temp_find/bin NO_DEFAULT_PATH)\n"
        "find_library(MY_LIB NAMES mylib PATHS temp_find/lib NO_DEFAULT_PATH)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"PROG_${MY_PROG}\" \"LIB_${MY_LIB}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DPROG_") != NULL);
    ASSERT(strstr(output, "temp_find/bin") != NULL);
    ASSERT(strstr(output, "-DLIB_") != NULL);
    ASSERT(strstr(output, "temp_find/lib") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("find_program") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("find_library") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_find/bin/mytool");
    nob_delete_file("temp_find/bin/mytool.exe");
    nob_delete_file("temp_find/lib/libmylib.a");
    nob_delete_file("temp_find/lib/mylib.lib");
    remove_test_dir("temp_find/bin");
    remove_test_dir("temp_find/lib");
    remove_test_dir("temp_find");
    TEST_PASS();
}

TEST(find_program_notfound_sets_notfound) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "find_program(MISSING_PROG NAMES definitely_missing_binary PATHS temp_find/bin NO_DEFAULT_PATH)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"MISS_${MISSING_PROG}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DMISS_MISSING_PROG-NOTFOUND") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("find_program") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(find_program_no_system_environment_path_excludes_path_env) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_find_env");
    nob_mkdir_if_not_exists("temp_find_env");
    nob_mkdir_if_not_exists("temp_find_env/bin");
    write_test_file("temp_find_env/bin/envtool", "tool\n");
    write_test_file("temp_find_env/bin/envtool.exe", "MZ");

    const char *old_path = getenv("PATH");
    const char *saved_path = old_path ? arena_strdup(arena, old_path) : NULL;
#if defined(_WIN32)
    _putenv_s("PATH", "temp_find_env/bin");
#else
    setenv("PATH", "temp_find_env/bin", 1);
#endif

    const char *input =
        "project(Test)\n"
        "find_program(PROG_FROM_ENV NAMES envtool)\n"
        "find_program(PROG_NO_ENV NAMES envtool NO_SYSTEM_ENVIRONMENT_PATH)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"ENV_${PROG_FROM_ENV}\" \"NOENV_${PROG_NO_ENV}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "ENV_") != NULL);
    ASSERT(strstr(output, "temp_find_env/bin") != NULL);
    ASSERT(strstr(output, "NOENV_PROG_NO_ENV-NOTFOUND") != NULL);
    ASSERT(diag_has_errors() == false);

#if defined(_WIN32)
    _putenv_s("PATH", saved_path ? saved_path : "");
#else
    if (saved_path) setenv("PATH", saved_path, 1);
    else unsetenv("PATH");
#endif

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_find_env");
    TEST_PASS();
}

TEST(find_file_and_find_path_basic) {
    Arena *arena = arena_create(1024 * 1024);
    nob_mkdir_if_not_exists("temp_find_fp");
    nob_mkdir_if_not_exists("temp_find_fp/data");
    nob_mkdir_if_not_exists("temp_find_fp/include");
    write_test_file("temp_find_fp/data/config.ini", "mode=dev\n");
    write_test_file("temp_find_fp/include/myheader.h", "#define HDR 1\n");

    const char *input =
        "project(Test)\n"
        "find_file(MY_CFG NAMES config.ini PATHS temp_find_fp/data NO_DEFAULT_PATH)\n"
        "find_path(MY_INC_DIR NAMES myheader.h PATHS temp_find_fp/include NO_DEFAULT_PATH)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"CFG_${MY_CFG}\" \"INC_${MY_INC_DIR}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DCFG_") != NULL);
    ASSERT(strstr(output, "temp_find_fp/data") != NULL);
    ASSERT(strstr(output, "-DINC_") != NULL);
    ASSERT(strstr(output, "temp_find_fp/include") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("find_file") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("find_path") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_find_fp/data/config.ini");
    nob_delete_file("temp_find_fp/include/myheader.h");
    remove_test_dir("temp_find_fp/data");
    remove_test_dir("temp_find_fp/include");
    remove_test_dir("temp_find_fp");
    TEST_PASS();
}

TEST(find_file_and_find_path_notfound_set_notfound) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "find_file(MISS_FILE NAMES nope.file PATHS temp_find_fp/missing NO_DEFAULT_PATH)\n"
        "find_path(MISS_PATH NAMES nope.h PATHS temp_find_fp/missing NO_DEFAULT_PATH)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"MF_${MISS_FILE}\" \"MP_${MISS_PATH}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DMF_MISS_FILE-NOTFOUND") != NULL);
    ASSERT(strstr(output, "-DMP_MISS_PATH-NOTFOUND") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("find_file") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("find_path") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(use_mangled_mesa_copies_headers_and_adds_include_dir) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_mesa");
    remove_test_tree("temp_mesa_out");

    ASSERT(nob_mkdir_if_not_exists("temp_mesa"));
    ASSERT(nob_mkdir_if_not_exists("temp_mesa/GL"));
    write_test_file("temp_mesa/GL/gl_mangle.h", "#pragma once\n");
    write_test_file("temp_mesa/GL/gl.h", "#pragma once\n");

    const char *input =
        "use_mangled_mesa(temp_mesa temp_mesa_out)\n"
        "add_executable(mesa_app main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "temp_mesa_out") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-I") != NULL);
    ASSERT(nob_file_exists("temp_mesa_out/GL/gl_mangle.h"));
    ASSERT(diag_telemetry_unsupported_count_for("use_mangled_mesa") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_mesa");
    remove_test_tree("temp_mesa_out");
    TEST_PASS();
}

TEST(include_command_basic) {
    Arena *arena = arena_create(1024 * 1024);
    const char *inc_file = "temp_include_test.cmake";
    write_test_file(inc_file, "set(INC_NAME inc_app)\n");

    const char *input =
        "include(temp_include_test.cmake)\n"
        "add_executable(${INC_NAME} main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_inc_app") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file(inc_file);
    TEST_PASS();
}

TEST(include_external_msproject_adds_utility_target_and_depends) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_custom_target(gen)\n"
        "include_external_msproject(extproj external.vcxproj DEPENDS gen)\n"
        "add_executable(app main.c)\n"
        "add_dependencies(app extproj)\n"
        "target_compile_definitions(app PRIVATE MSP_OK=1)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    char *ext_pos = strstr(output, "// --- Target: extproj ---");
    char *app_pos = strstr(output, "// --- Target: app ---");
    ASSERT(ext_pos != NULL);
    ASSERT(app_pos != NULL);
    ASSERT(ext_pos < app_pos);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_extproj))") == NULL);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_app))") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include_external_msproject") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(makefile_style_transform_and_include_regression) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_makefile_transform");
    nob_mkdir_if_not_exists("temp_makefile_transform");
    write_test_file("temp_makefile_transform/Makefile.inc",
                    "CSOURCES = alpha \\\n"
                    "  beta \\\n"
                    "  $(TOOLX_CFILES)\n");

    const char *input =
        "project(Test)\n"
        "set(TOOLX_CFILES gamma)\n"
        "file(READ temp_makefile_transform/Makefile.inc TXT)\n"
        "string(REGEX REPLACE \"\\\\\\\\\\n\" \"!^!^!\" TXT \"${TXT}\")\n"
        "string(REGEX REPLACE \"([a-zA-Z_][a-zA-Z0-9_]*)[\\t ]*=[\\t ]*([^\\n]*)\" \"set(\\\\1 \\\\2)\" TXT \"${TXT}\")\n"
        "string(REPLACE \"!^!^!\" \"\\n\" TXT \"${TXT}\")\n"
        "string(REGEX REPLACE \"\\\\$\\\\(([a-zA-Z_][a-zA-Z0-9_]*)\\\\)\" \"\\${\\\\1}\" TXT \"${TXT}\")\n"
        "file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/temp_makefile_transform/Makefile.inc.cmake \"${TXT}\")\n"
        "include(${CMAKE_CURRENT_BINARY_DIR}/temp_makefile_transform/Makefile.inc.cmake)\n"
        "add_executable(app main.c ${CSOURCES})";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("string") == 0);

    Nob_String_Builder transformed = {0};
    ASSERT(nob_read_entire_file("temp_makefile_transform/Makefile.inc.cmake", &transformed));
    ASSERT(strstr(transformed.items, "\\)") == NULL);
    ASSERT(strstr(transformed.items, "set(CSOURCES") != NULL);

    nob_sb_free(transformed);
    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_makefile_transform");
    TEST_PASS();
}

TEST(include_guard_is_supported_as_noop) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "include_guard(GLOBAL)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("include_guard") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_ctest_initializes_module_and_enables_testing) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(TestProj)\n"
        "include(CTest)\n"
        "add_test(NAME smoke COMMAND app)\n"
        "get_test_property(smoke COMMAND T_CMD)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"BT_${BUILD_TESTING}\" "
        "\"TE_${CMAKE_TESTING_ENABLED}\" "
        "\"CP_${CTEST_PROJECT_NAME}\" "
        "\"TS_${CTEST_SOURCE_DIRECTORY}\" "
        "\"TB_${CTEST_BINARY_DIRECTORY}\" "
        "\"TC_${CMAKE_CTEST_TEST_COUNT}\" "
        "\"CMD_${T_CMD}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DBT_ON") != NULL);
    ASSERT(strstr(output, "-DTE_ON") != NULL);
    ASSERT(strstr(output, "-DCP_TestProj") != NULL);
    ASSERT(strstr(output, "-DTC_1") != NULL);
    ASSERT(strstr(output, "-DCMD_app") != NULL);
    ASSERT(strstr(output, "-DTS_") != NULL);
    ASSERT(strstr(output, "-DTB_") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("add_test") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("get_test_property") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_ctest_respects_build_testing_off) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(BUILD_TESTING OFF)\n"
        "include(CTest)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"BT_${BUILD_TESTING}\" \"TE_${CMAKE_TESTING_ENABLED}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DBT_OFF") != NULL);
    ASSERT(strstr(output, "-DTE_OFF") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_ctest_use_launchers_sets_rule_launch_properties) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CTEST_USE_LAUNCHERS ON)\n"
        "include(CTestUseLaunchers)\n"
        "get_property(RLC GLOBAL PROPERTY RULE_LAUNCH_COMPILE)\n"
        "get_property(RLL GLOBAL PROPERTY RULE_LAUNCH_LINK)\n"
        "get_property(RLCU GLOBAL PROPERTY RULE_LAUNCH_CUSTOM)\n"
        "string(REPLACE \";\" \"_\" RLC_FLAT \"${RLC}\")\n"
        "string(REPLACE \";\" \"_\" RLL_FLAT \"${RLL}\")\n"
        "string(REPLACE \";\" \"_\" RLCU_FLAT \"${RLCU}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"UL_${CTEST_USE_LAUNCHERS}\" "
        "\"M_${CMAKE_CTEST_USE_LAUNCHERS_MODULE_INITIALIZED}\" "
        "\"LC_${RLC_FLAT}\" \"LL_${RLL_FLAT}\" \"LU_${RLCU_FLAT}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DUL_ON") != NULL);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DLC_") != NULL);
    ASSERT(strstr(output, "-DLL_") != NULL);
    ASSERT(strstr(output, "-DLU_") != NULL);
    ASSERT(strstr(output, "--launch_compile") != NULL);
    ASSERT(strstr(output, "--launch_link") != NULL);
    ASSERT(strstr(output, "--launch_custom") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_cycle_guard) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "include(temp_include_cycle.cmake)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"CYCLE_${CYCLE_OK}\")";

    write_test_file("temp_include_cycle.cmake",
        "include(temp_include_cycle.cmake)\n"
        "set(CYCLE_OK ON)\n");

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    nob_delete_file("temp_include_cycle.cmake");

    ASSERT(strstr(output, "-DCYCLE_ON") != NULL);
    ASSERT(diag_warning_count() > 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_uses_default_cmake_module_path) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "include(MyModule)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"MOD_${MYMOD_OK}\")";

    nob_mkdir_if_not_exists("temp_mod_default");
    nob_mkdir_if_not_exists("temp_mod_default/CMake");
    write_test_file("temp_mod_default/CMake/MyModule.cmake", "set(MYMOD_OK ON)\n");

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree_with_input_path(root, &sb, "temp_mod_default/CMakeLists.txt");

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DMOD_ON") != NULL);
    ASSERT(diag_has_errors() == false);

    nob_delete_file("temp_mod_default/CMake/MyModule.cmake");
    remove_test_dir("temp_mod_default/CMake");
    remove_test_dir("temp_mod_default");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_internal_falls_back_to_cmake_root_modules) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMAKE_ROOT \"${CMAKE_CURRENT_SOURCE_DIR}/temp_mod_internal\")\n"
        "set(CMAKE_MODULE_PATH \"${CMAKE_CURRENT_SOURCE_DIR}/temp_mod_user\")\n"
        "include(Internal/MyInternal)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"INTERNAL_${MYINTERNAL_OK}\")";

    nob_mkdir_if_not_exists("temp_mod_internal");
    nob_mkdir_if_not_exists("temp_mod_internal/Modules");
    nob_mkdir_if_not_exists("temp_mod_internal/Modules/Internal");
    nob_mkdir_if_not_exists("temp_mod_user");
    write_test_file("temp_mod_internal/Modules/Internal/MyInternal.cmake", "set(MYINTERNAL_OK ON)\n");

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree_with_input_path(root, &sb, "CMakeLists.txt");

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DINTERNAL_ON") != NULL);
    ASSERT(diag_has_errors() == false);

    nob_delete_file("temp_mod_internal/Modules/Internal/MyInternal.cmake");
    remove_test_dir("temp_mod_internal/Modules/Internal");
    remove_test_dir("temp_mod_internal/Modules");
    remove_test_dir("temp_mod_internal");
    remove_test_dir("temp_mod_user");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(include_builtin_modules_are_handled_without_missing_file_warning) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "include(CheckFunctionExists)\n"
        "include(CheckIncludeFile)\n"
        "include(GNUInstallDirs)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"BINDIR_${CMAKE_INSTALL_BINDIR}\" \"LIBDIR_${CMAKE_INSTALL_LIBDIR}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "BINDIR_bin") != NULL);
    ASSERT(strstr(output, "LIBDIR_lib") != NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_warning_count() == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(check_type_size_real_probe_with_extra_include_files) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_type_size_probe");
    nob_mkdir_if_not_exists("temp_type_size_probe");
    write_test_file("temp_type_size_probe/custom_types.h",
                    "typedef long long custom_size_probe_t;\n");

    const char *input =
        "project(Test)\n"
        "set(CMK2NOB_REAL_PROBES ON)\n"
        "set(CMAKE_REQUIRED_INCLUDES \"${CMAKE_CURRENT_SOURCE_DIR}/temp_type_size_probe\")\n"
        "set(CMAKE_EXTRA_INCLUDE_FILES \"custom_types.h\")\n"
        "check_type_size(\"custom_size_probe_t\" SIZEOF_CUSTOM_SIZE_PROBE_T)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"TSP_${SIZEOF_CUSTOM_SIZE_PROBE_T}\" "
        "\"HTSP_${HAVE_SIZEOF_CUSTOM_SIZE_PROBE_T}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "HTSP_1") != NULL);
    ASSERT(strstr(output, "TSP_") != NULL);
    ASSERT(strstr(output, "TSP_0") == NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("check_type_size") == 0);

    remove_test_tree("temp_type_size_probe");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

void run_transpiler_suite_find_include(int *passed, int *failed) {
    test_target_include_directories(passed, failed);
    test_include_directories_global(passed, failed);
    test_include_regular_expression_sets_builtin_vars(passed, failed);
    test_find_package_zlib(passed, failed);
    test_find_package_target_link_usage(passed, failed);
    test_find_package_required_reports_error_when_missing(passed, failed);
    test_find_package_config_mode_uses_dir_and_config_vars(passed, failed);
    test_find_package_no_default_path_ignores_dir_and_prefix_defaults(passed, failed);
    test_find_package_module_mode_prefers_module_resolution(passed, failed);
    test_find_package_exact_version_mismatch_sets_not_found(passed, failed);
    test_find_package_components_and_component_imported_target(passed, failed);
    test_cmake_pkg_config_imported_target_and_vars(passed, failed);
    test_find_program_and_find_library_basic(passed, failed);
    test_find_program_notfound_sets_notfound(passed, failed);
    test_find_program_no_system_environment_path_excludes_path_env(passed, failed);
    test_find_file_and_find_path_basic(passed, failed);
    test_find_file_and_find_path_notfound_set_notfound(passed, failed);
    test_use_mangled_mesa_copies_headers_and_adds_include_dir(passed, failed);
    test_include_command_basic(passed, failed);
    test_include_external_msproject_adds_utility_target_and_depends(passed, failed);
    test_makefile_style_transform_and_include_regression(passed, failed);
    test_include_guard_is_supported_as_noop(passed, failed);
    test_include_ctest_initializes_module_and_enables_testing(passed, failed);
    test_include_ctest_respects_build_testing_off(passed, failed);
    test_include_ctest_use_launchers_sets_rule_launch_properties(passed, failed);
    test_include_cycle_guard(passed, failed);
    test_include_uses_default_cmake_module_path(passed, failed);
    test_include_internal_falls_back_to_cmake_root_modules(passed, failed);
    test_include_builtin_modules_are_handled_without_missing_file_warning(passed, failed);
    test_check_type_size_real_probe_with_extra_include_files(passed, failed);
}
