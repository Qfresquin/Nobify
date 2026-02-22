#include "test_evaluator_v2_shared.h"

TEST(config_release_compile_and_output_properties) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMAKE_BUILD_TYPE Release)\n"
        "add_executable(app main.c)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME_RELEASE app_rel COMPILE_DEFINITIONS_RELEASE REL=1 COMPILE_OPTIONS_RELEASE -O3)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "build/app_rel") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DREL=1\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-O3\");") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(config_debug_output_directory_property) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMAKE_BUILD_TYPE Debug)\n"
        "add_executable(app main.c)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG out/debug)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "out/debug/app") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(conditional_target_properties_dual_read_debug) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMAKE_BUILD_TYPE Debug)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE BASE=1)\n"
        "target_compile_options(app PRIVATE -Wall)\n"
        "target_include_directories(app PRIVATE inc_all)\n"
        "target_link_options(app PRIVATE -Wl,--base)\n"
        "target_link_directories(app PRIVATE link_all)\n"
        "set_target_properties(app PROPERTIES "
        "COMPILE_DEFINITIONS_DEBUG DBG=1 "
        "COMPILE_OPTIONS_DEBUG -Og "
        "INCLUDE_DIRECTORIES_DEBUG debug_inc "
        "LINK_OPTIONS_DEBUG -Wl,--dbg "
        "LINK_DIRECTORIES_DEBUG debug_link)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DBASE=1\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DDBG=1\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wall\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Og\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Iinc_all\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Idebug_inc\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Wl,--base\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Wl,--dbg\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Llink_all\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Ldebug_link\");") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(conditional_target_properties_dual_read_release) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMAKE_BUILD_TYPE Release)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE BASE=1)\n"
        "target_compile_options(app PRIVATE -Wall)\n"
        "target_include_directories(app PRIVATE inc_all)\n"
        "target_link_options(app PRIVATE -Wl,--base)\n"
        "target_link_directories(app PRIVATE link_all)\n"
        "set_target_properties(app PROPERTIES "
        "COMPILE_DEFINITIONS_DEBUG DBG=1 "
        "COMPILE_OPTIONS_DEBUG -Og "
        "INCLUDE_DIRECTORIES_DEBUG debug_inc "
        "LINK_OPTIONS_DEBUG -Wl,--dbg "
        "LINK_DIRECTORIES_DEBUG debug_link)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DBASE=1\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wall\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Iinc_all\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DDBG=1\");") == NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Og\");") == NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Idebug_inc\");") == NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Wl,--base\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Llink_all\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Wl,--dbg\");") == NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Ldebug_link\");") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(install_targets) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "install(TARGETS app DESTINATION bin)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "strcmp(argv[1], \"install\") == 0") != NULL);
    ASSERT(strstr(output, "if (!nob_copy_file(src, dst)) return 1;") != NULL);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"bin\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(install_targets_runtime_library_archive) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "add_library(core SHARED core.c)\n"
        "add_library(utils STATIC util.c)\n"
        "install(TARGETS app core utils RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION ar)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"bin\")") != NULL);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"lib\")") != NULL);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"ar\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(install_files_programs_directories) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "install(FILES config.ini DESTINATION etc)\n"
        "install(PROGRAMS tool.sh DESTINATION bin)\n"
        "install(DIRECTORY assets DESTINATION share)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "const char *src = \"config.ini\"") != NULL);
    ASSERT(strstr(output, "const char *src = \"tool.sh\"") != NULL);
    ASSERT(strstr(output, "nob_copy_directory_recursively(src, dst)") != NULL);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"etc\")") != NULL);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"bin\")") != NULL);
    ASSERT(strstr(output, "nob_mkdir_if_not_exists(\"share\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(link_directories_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "link_directories(BEFORE glink /opt/glink -Lalready)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Lglink\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-L/opt/glink\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Lalready\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("link_directories") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(link_libraries_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "link_libraries(m pthread)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
#if defined(_WIN32)
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"m.lib\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"pthread.lib\")") != NULL);
#else
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-lm\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-lpthread\")") != NULL);
#endif
    ASSERT(diag_telemetry_unsupported_count_for("link_libraries") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(link_libraries_global_framework) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "link_libraries(\"-framework Cocoa\")\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-framework\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("link_libraries") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(remove_definitions_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_compile_definitions(KEEP_DEF=1 DROP_DEF=1)\n"
        "add_definitions(-DOLD_KEEP=1 -DOLD_DROP=1 -fPIC)\n"
        "remove_definitions(-DDROP_DEF=1 /DOLD_DROP=1)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DKEEP_DEF=1\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DOLD_KEEP=1\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DDROP_DEF=1\")") == NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DOLD_DROP=1\")") == NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-fPIC\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("remove_definitions") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(enable_language_sets_compiler_loaded_var) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test LANGUAGES C)\n"
        "enable_language(CXX)\n"
        "if(CMAKE_CXX_COMPILER_LOADED)\n"
        "  add_executable(app main.c)\n"
        "  target_compile_definitions(app PRIVATE \"CXX_LOADED_${CMAKE_CXX_COMPILER_LOADED}\")\n"
        "endif()";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DCXX_LOADED_1\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("enable_language") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(site_name_sets_defined_variable) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "site_name(MY_SITE_NAME)\n"
        "if(DEFINED MY_SITE_NAME)\n"
        "  add_executable(app main.c)\n"
        "endif()";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("site_name") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(get_directory_property_reads_values) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set_directory_properties(PROPERTIES "
        "INCLUDE_DIRECTORIES dir_only "
        "COMPILE_DEFINITIONS DIR_DEF=1 "
        "COMPILE_OPTIONS -Wshadow "
        "LINK_DIRECTORIES ldir_only "
        "LINK_OPTIONS -Wl,--as-needed)\n"
        "get_directory_property(DIR_INC INCLUDE_DIRECTORIES)\n"
        "get_directory_property(DIR_DEF COMPILE_DEFINITIONS)\n"
        "get_directory_property(DIR_OPT COMPILE_OPTIONS)\n"
        "get_directory_property(DIR_LDIR LINK_DIRECTORIES)\n"
        "get_directory_property(DIR_LOPT LINK_OPTIONS)\n"
        "get_directory_property(PROJ_FROM_DEF DEFINITION PROJECT_NAME)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"INC_${DIR_INC}\" "
        "\"DEF_${DIR_DEF}\" "
        "\"OPT_${DIR_OPT}\" "
        "\"LDIR_${DIR_LDIR}\" "
        "\"LOPT_${DIR_LOPT}\" "
        "\"PNAME_${PROJ_FROM_DEF}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DINC_dir_only\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DDEF_DIR_DEF=1\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DPNAME_Test\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("get_directory_property") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(export_partial_support_targets_file_namespace) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(core STATIC core.c)\n"
        "export(TARGETS core FILE temp_export_targets.cmake NAMESPACE Demo::)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(diag_telemetry_unsupported_count_for("export") == 0);
    ASSERT(nob_file_exists("temp_export_targets.cmake"));

    Nob_String_Builder exported = {0};
    ASSERT(nob_read_entire_file("temp_export_targets.cmake", &exported));
    char *content = nob_temp_sprintf("%.*s", (int)exported.count, exported.items);
    ASSERT(strstr(content, "_CMK2NOB_EXPORTED_TARGETS") != NULL);
    ASSERT(strstr(content, "core") != NULL);
    ASSERT(strstr(content, "namespace: Demo::") != NULL);

    nob_delete_file("temp_export_targets.cmake");
    nob_sb_free(exported);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(export_support_export_set_from_install_targets) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(core STATIC core.c)\n"
        "add_library(util STATIC util.c)\n"
        "install(TARGETS core util EXPORT DemoExport DESTINATION lib)\n"
        "export(EXPORT DemoExport FILE temp_export_set.cmake NAMESPACE Demo::)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(diag_telemetry_unsupported_count_for("export") == 0);
    ASSERT(nob_file_exists("temp_export_set.cmake"));

    Nob_String_Builder exported = {0};
    ASSERT(nob_read_entire_file("temp_export_set.cmake", &exported));
    char *content = nob_temp_sprintf("%.*s", (int)exported.count, exported.items);
    ASSERT(strstr(content, "_CMK2NOB_EXPORTED_TARGETS") != NULL);
    ASSERT(strstr(content, "core") != NULL);
    ASSERT(strstr(content, "util") != NULL);
    ASSERT(strstr(content, "signature: EXPORT_SET") != NULL);
    ASSERT(strstr(content, "export-set: DemoExport") != NULL);
    ASSERT(strstr(content, "namespace: Demo::") != NULL);

    nob_delete_file("temp_export_set.cmake");
    nob_sb_free(exported);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(export_package_registers_package_dir) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "export(PACKAGE DemoPkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"PD_${DemoPkg_DIR}\" \"PR_${CMAKE_EXPORT_PACKAGE_REGISTRY}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DPD_") != NULL);
    ASSERT(strstr(output, "-DPR_DemoPkg") != NULL);
    ASSERT(nob_file_exists(".cmk2nob_package_registry/DemoPkg.cmake"));
    ASSERT(diag_telemetry_unsupported_count_for("export") == 0);

    Nob_String_Builder reg_file = {0};
    ASSERT(nob_read_entire_file(".cmk2nob_package_registry/DemoPkg.cmake", &reg_file));
    char *reg_content = nob_temp_sprintf("%.*s", (int)reg_file.count, reg_file.items);
    ASSERT(strstr(reg_content, "DemoPkg_DIR") != NULL);

    nob_delete_file(".cmk2nob_package_registry/DemoPkg.cmake");
    nob_sb_free(reg_file);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(get_source_file_property_reads_values) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c util.c)\n"
        "set_source_files_properties(util.c PROPERTIES COMPILE_DEFINITIONS UTIL_ONLY=1 COMPILE_OPTIONS -Werror)\n"
        "get_source_file_property(SRC_DEF util.c COMPILE_DEFINITIONS)\n"
        "get_source_file_property(SRC_OPT util.c COMPILE_OPTIONS)\n"
        "get_source_file_property(SRC_MISS main.c COMPILE_DEFINITIONS)\n"
        "target_compile_definitions(app PRIVATE \"SRCDEF_${SRC_DEF}\" \"SRCOPT_${SRC_OPT}\" \"SRCMISS_${SRC_MISS}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DSRCDEF_UTIL_ONLY=1") != NULL);
    ASSERT(strstr(output, "-DSRCOPT_-Werror") != NULL);
    ASSERT(strstr(output, "-DSRCMISS_NOTFOUND") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("get_source_file_property") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_dependent_option_command) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CURL_ENABLE_SSL OFF)\n"
        "cmake_dependent_option(CURL_USE_OPENSSL \"Enable OpenSSL\" ON CURL_ENABLE_SSL OFF)\n"
        "set(CURL_DISABLE_MIME ON)\n"
        "cmake_dependent_option(CURL_DISABLE_FORM_API \"Disable form-api\" OFF \"NOT CURL_DISABLE_MIME\" ON)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE SSL_${CURL_USE_OPENSSL} FORM_${CURL_DISABLE_FORM_API})";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DSSL_OFF") != NULL);
    ASSERT(strstr(output, "-DFORM_ON") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_dependent_option") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(subdirs_legacy_exclude_from_all) {
    Arena *arena = arena_create(1024 * 1024);
    const char *dir_a = "temp_subdirs_a";
    const char *dir_b = "temp_subdirs_b";
    const char *cmake_a = "temp_subdirs_a/CMakeLists.txt";
    const char *cmake_b = "temp_subdirs_b/CMakeLists.txt";

    ASSERT(nob_mkdir_if_not_exists(dir_a));
    ASSERT(nob_mkdir_if_not_exists(dir_b));
    write_test_file(cmake_a, "add_library(subdirs_a STATIC a.c)\n");
    write_test_file(cmake_b, "add_library(subdirs_b STATIC b.c)\n");

    const char *input =
        "subdirs(temp_subdirs_a temp_subdirs_b EXCLUDE_FROM_ALL temp_subdirs_b PREORDER)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_subdirs_a") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_subdirs_b") == NULL);
    ASSERT(diag_telemetry_unsupported_count_for("subdirs") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file(cmake_a);
    nob_delete_file(cmake_b);
    remove_test_dir(dir_a);
    remove_test_dir(dir_b);
    TEST_PASS();
}

TEST(compat_noop_commands_are_ignored_without_unsupported) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "include_guard()\n"
        "block()\n"
        "endblock()\n"
        "cmake_policy(SET CMP0001 NEW)\n"
        "cmake_language(CALL message STATUS ignored)\n"
        "source_group(TREE . FILES main.c)\n"
        "ctest_start(Experimental)\n"
        "ctest_build()\n"
        "ctest_test()\n"
        "cpack_add_component(runtime)\n"
        "build_name(BUILD_ID)\n"
        "load_cache(. READ_WITH_PREFIX P_ SOME_VAR)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_policy") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_language") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("ctest_start") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("build_name") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(create_test_sourcelist_generates_driver_and_list) {
    Arena *arena = arena_create(1024 * 1024);
    const char *driver_file = "temp_cts_driver.c";
    const char *input =
        "project(Test)\n"
        "create_test_sourcelist(TEST_SRCS temp_cts_driver.c alpha.c beta.cpp EXTRA_INCLUDE sample_header.h)\n"
        "list(LENGTH TEST_SRCS TEST_COUNT)\n"
        "string(REPLACE \";\" \"_\" TEST_SRCS_FLAT \"${TEST_SRCS}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"CTS_COUNT_${TEST_COUNT}\" \"CTS_LIST_${TEST_SRCS_FLAT}\")";

    if (nob_file_exists(driver_file)) {
        nob_delete_file(driver_file);
    }

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DCTS_COUNT_3") != NULL);
    ASSERT(strstr(output, "-DCTS_LIST_temp_cts_driver.c_alpha.c_beta.cpp") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("create_test_sourcelist") == 0);
    ASSERT(diag_has_errors() == false);
    ASSERT(nob_file_exists(driver_file));

    nob_delete_file(driver_file);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(load_command_sets_loaded_variable) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "load_command(MyLegacyCommand ${CMAKE_CURRENT_SOURCE_DIR})\n"
        "if(MyLegacyCommand_LOADED)\n"
        "  add_executable(app main.c)\n"
        "  target_compile_definitions(app PRIVATE \"LOAD_OK_${MyLegacyCommand_LOADED}\")\n"
        "endif()";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app") != NULL);
    ASSERT(strstr(output, "-DLOAD_OK_TRUE") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("load_command") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(qt_wrap_cpp_generates_moc_list_and_file) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "qt_wrap_cpp(MOC_SRCS temp_qt_widget.h)\n"
        "list(LENGTH MOC_SRCS MOC_COUNT)\n"
        "string(REPLACE \";\" \"_\" MOC_FLAT \"${MOC_SRCS}\")\n"
        "add_executable(app main.c ${MOC_SRCS})\n"
        "target_compile_definitions(app PRIVATE \"MOC_COUNT_${MOC_COUNT}\" \"MOC_FLAT_${MOC_FLAT}\")";

    write_test_file("temp_qt_widget.h", "#pragma once\n");
    if (nob_file_exists("moc_temp_qt_widget.cxx")) {
        nob_delete_file("moc_temp_qt_widget.cxx");
    }

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DMOC_COUNT_1") != NULL);
    ASSERT(strstr(output, "moc_temp_qt_widget.cxx") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("qt_wrap_cpp") == 0);
    ASSERT(diag_has_errors() == false);
    ASSERT(nob_file_exists("moc_temp_qt_widget.cxx"));

    nob_delete_file("temp_qt_widget.h");
    nob_delete_file("moc_temp_qt_widget.cxx");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(qt_wrap_ui_generates_ui_list_and_file) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "qt_wrap_ui(UI_HDRS temp_dialog.ui)\n"
        "list(LENGTH UI_HDRS UI_COUNT)\n"
        "string(REPLACE \";\" \"_\" UI_FLAT \"${UI_HDRS}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"UI_COUNT_${UI_COUNT}\" \"UI_FLAT_${UI_FLAT}\")";

    write_test_file("temp_dialog.ui", "<ui></ui>\n");
    if (nob_file_exists("ui_temp_dialog.h")) {
        nob_delete_file("ui_temp_dialog.h");
    }

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DUI_COUNT_1") != NULL);
    ASSERT(strstr(output, "ui_temp_dialog.h") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("qt_wrap_ui") == 0);
    ASSERT(diag_has_errors() == false);
    ASSERT(nob_file_exists("ui_temp_dialog.h"));

    nob_delete_file("temp_dialog.ui");
    nob_delete_file("ui_temp_dialog.h");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(execute_process_sets_output_error_and_result_variables) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
#if defined(_WIN32)
        "execute_process(COMMAND cmd /C echo hello OUTPUT_VARIABLE EP_OUT RESULT_VARIABLE EP_RC OUTPUT_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND cmd /C echo oops 1>&2 ERROR_VARIABLE EP_ERR RESULT_VARIABLE EP_ERR_RC ERROR_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND cmd /C exit 1 RESULT_VARIABLE EP_FAIL_RC)\n"
#else
        "execute_process(COMMAND sh -c \"echo hello\" OUTPUT_VARIABLE EP_OUT RESULT_VARIABLE EP_RC OUTPUT_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND sh -c \"echo oops 1>&2\" ERROR_VARIABLE EP_ERR RESULT_VARIABLE EP_ERR_RC ERROR_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND sh -c \"exit 1\" RESULT_VARIABLE EP_FAIL_RC)\n"
#endif
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"EPO_${EP_OUT}\" \"EPE_${EP_ERR}\" \"EPR_${EP_RC}\" \"EPER_${EP_ERR_RC}\" \"EPF_${EP_FAIL_RC}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DEPO_hello") != NULL);
    ASSERT(strstr(output, "-DEPE_oops") != NULL);
    ASSERT(strstr(output, "-DEPR_0") != NULL);
    ASSERT(strstr(output, "-DEPER_0") != NULL);
    ASSERT(strstr(output, "-DEPF_1") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("execute_process") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(exec_program_sets_output_and_return_value) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
#if defined(_WIN32)
        "exec_program(cmd ARGS /C echo hello OUTPUT_VARIABLE XP_OUT RETURN_VALUE XP_RC)\n"
        "exec_program(cmd ARGS /C exit 1 RETURN_VALUE XP_FAIL)\n"
#else
        "exec_program(sh ARGS -c \"echo hello\" OUTPUT_VARIABLE XP_OUT RETURN_VALUE XP_RC)\n"
        "exec_program(sh ARGS -c \"exit 1\" RETURN_VALUE XP_FAIL)\n"
#endif
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"XPO_${XP_OUT}\" \"XPR_${XP_RC}\" \"XPF_${XP_FAIL}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DXPO_hello") != NULL);
    ASSERT(strstr(output, "-DXPR_0") != NULL);
    ASSERT(strstr(output, "-DXPF_1") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("exec_program") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(fltk_wrap_ui_generates_source_list_and_file) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "fltk_wrap_ui(FLTK_SRCS temp_fltk_dialog.fl)\n"
        "list(LENGTH FLTK_SRCS FLTK_COUNT)\n"
        "string(REPLACE \";\" \"_\" FLTK_FLAT \"${FLTK_SRCS}\")\n"
        "add_executable(app main.c ${FLTK_SRCS})\n"
        "target_compile_definitions(app PRIVATE \"FLTK_COUNT_${FLTK_COUNT}\" \"FLTK_FLAT_${FLTK_FLAT}\")";

    write_test_file("temp_fltk_dialog.fl", "# fake fltk form\n");
    if (nob_file_exists("temp_fltk_dialog.cxx")) {
        nob_delete_file("temp_fltk_dialog.cxx");
    }

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DFLTK_COUNT_1") != NULL);
    ASSERT(strstr(output, "temp_fltk_dialog.cxx") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("fltk_wrap_ui") == 0);
    ASSERT(diag_has_errors() == false);
    ASSERT(nob_file_exists("temp_fltk_dialog.cxx"));

    nob_delete_file("temp_fltk_dialog.fl");
    nob_delete_file("temp_fltk_dialog.cxx");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(separate_arguments_unix_command) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "separate_arguments(SA UNIX_COMMAND \"alpha \\\"beta-gamma\\\" delta\")\n"
        "list(LENGTH SA SA_LEN)\n"
        "list(GET SA 1 SA_1)\n"
        "list(JOIN SA , SA_JOIN)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"SA_LEN_${SA_LEN}\" \"SA1_${SA_1}\" \"SAJ_${SA_JOIN}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DSA_LEN_3") != NULL);
    ASSERT(strstr(output, "-DSA1_beta-gamma") != NULL);
    ASSERT(strstr(output, "-DSAJ_alpha,beta-gamma,delta") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("separate_arguments") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(mark_as_advanced_is_supported) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "option(MY_OPT \"desc\" ON)\n"
        "mark_as_advanced(FORCE MY_OPT)\n"
        "mark_as_advanced(CLEAR MY_OPT)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(diag_telemetry_unsupported_count_for("mark_as_advanced") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(block_endblock_and_cmake_policy_are_supported_as_noop) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "cmake_policy(PUSH)\n"
        "cmake_policy(SET CMP0077 NEW)\n"
        "block(SCOPE_FOR VARIABLES)\n"
        "add_executable(app main.c)\n"
        "endblock()\n"
        "cmake_policy(POP)\n";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app") != NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("block") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("endblock") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_policy") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(package_helpers_are_supported_and_set_property_empty_is_safe) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "set_property(TARGET app PROPERTY COMPILE_OPTIONS \"\")\n"
        "write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/pkg/ConfigVersion.cmake VERSION 1.0 COMPATIBILITY SameMajorVersion)\n"
        "configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/missing-in.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/pkg/Config.cmake INSTALL_DESTINATION lib/cmake)\n";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("write_basic_package_version_file") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("configure_package_config_file") == 0);
    ASSERT(nob_file_exists("pkg/ConfigVersion.cmake"));
    ASSERT(nob_file_exists("pkg/Config.cmake"));

    nob_delete_file("pkg/ConfigVersion.cmake");
    nob_delete_file("pkg/Config.cmake");
    remove_test_dir("pkg");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(aux_source_directory_collects_supported_sources) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "aux_source_directory(temp_aux_src AUX_SRCS)\n"
        "list(LENGTH AUX_SRCS AUX_COUNT)\n"
        "add_executable(app main.c ${AUX_SRCS})\n"
        "target_compile_definitions(app PRIVATE \"AUX_COUNT_${AUX_COUNT}\")";

    remove_test_tree("temp_aux_src");
    nob_mkdir_if_not_exists("temp_aux_src");
    write_test_file("temp_aux_src/a.c", "int a(void){return 0;}\n");
    write_test_file("temp_aux_src/b.cpp", "int b(void){return 0;}\n");
    write_test_file("temp_aux_src/skip.txt", "not-a-source\n");

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DAUX_COUNT_2") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("aux_source_directory") == 0);
    ASSERT(diag_has_errors() == false);

    remove_test_tree("temp_aux_src");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(define_property_metadata_is_visible_via_get_property) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "define_property(GLOBAL PROPERTY MY_GLOBAL_PROP BRIEF_DOCS brief_doc FULL_DOCS full_doc)\n"
        "get_property(PROP_DEFINED GLOBAL PROPERTY MY_GLOBAL_PROP DEFINED)\n"
        "get_property(PROP_BRIEF GLOBAL PROPERTY MY_GLOBAL_PROP BRIEF_DOCS)\n"
        "get_property(PROP_FULL GLOBAL PROPERTY MY_GLOBAL_PROP FULL_DOCS)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"PDEF_${PROP_DEFINED}\" \"PBRIEF_${PROP_BRIEF}\" \"PFULL_${PROP_FULL}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DPDEF_1") != NULL);
    ASSERT(strstr(output, "-DPBRIEF_brief_doc") != NULL);
    ASSERT(strstr(output, "-DPFULL_full_doc") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("define_property") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("get_property") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_push_pop_check_state_restore) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMAKE_REQUIRED_FLAGS OLD_FLAG)\n"
        "cmake_push_check_state(RESET)\n"
        "if(DEFINED CMAKE_REQUIRED_FLAGS)\n"
        "  set(INNER_FLAG NOT_EMPTY)\n"
        "else()\n"
        "  set(INNER_FLAG EMPTY)\n"
        "endif()\n"
        "set(CMAKE_REQUIRED_FLAGS NEW_FLAG)\n"
        "cmake_pop_check_state()\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"OUT_${CMAKE_REQUIRED_FLAGS}\" \"INNER_${INNER_FLAG}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DOUT_OLD_FLAG") != NULL);
    ASSERT(strstr(output, "-DINNER_EMPTY") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_push_check_state") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_pop_check_state") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(get_filename_component_modes) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(P src/libcurl.so)\n"
        "get_filename_component(P_DIR ${P} DIRECTORY)\n"
        "get_filename_component(P_NAME ${P} NAME)\n"
        "get_filename_component(P_WE ${P} NAME_WE)\n"
        "get_filename_component(P_EXT ${P} EXT)\n"
        "get_filename_component(P_ABS ${P} ABSOLUTE BASE_DIR base)\n"
        "get_filename_component(P_REAL ${P} REALPATH BASE_DIR base)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"D_${P_DIR}\" \"N_${P_NAME}\" \"W_${P_WE}\" \"E_${P_EXT}\" "
        "\"A_${P_ABS}\" \"R_${P_REAL}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DD_src") != NULL);
    ASSERT(strstr(output, "-DN_libcurl.so") != NULL);
    ASSERT(strstr(output, "-DW_libcurl") != NULL);
    ASSERT(strstr(output, "-DE_.so") != NULL);
    ASSERT(strstr(output, "-DA_") != NULL);
    ASSERT(strstr(output, "-DR_") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("get_filename_component") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_path_get_and_set_normalize) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "cmake_path(SET NORM_PATH NORMALIZE \"foo/./bar/../libcurl.so\")\n"
        "cmake_path(GET NORM_PATH PARENT_PATH NORM_DIR)\n"
        "cmake_path(GET NORM_PATH STEM NORM_STEM)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"DIR_${NORM_DIR}\" \"STEM_${NORM_STEM}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "DIR_foo") != NULL);
    ASSERT(strstr(output, "STEM_libcurl") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_path") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_path_append_compare_has_is_normal_relative) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(P base)\n"
        "cmake_path(APPEND P folder file.txt NORMALIZE)\n"
        "cmake_path(HAS_FILENAME P HAS_NAME)\n"
        "cmake_path(HAS_EXTENSION P HAS_EXT)\n"
        "cmake_path(IS_ABSOLUTE P IS_ABS)\n"
        "cmake_path(COMPARE \"a/./b\" EQUAL \"a/b\" CMP_EQ)\n"
        "cmake_path(SET ABS_P NORMALIZE \"/root/dir/file.txt\")\n"
        "cmake_path(RELATIVE_PATH ABS_P BASE_DIRECTORY \"/root\" OUTPUT_VARIABLE REL_P)\n"
        "cmake_path(NORMAL_PATH REL_P OUTPUT_VARIABLE REL_NORM)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"P_${P}\" \"HN_${HAS_NAME}\" \"HE_${HAS_EXT}\" \"IA_${IS_ABS}\" "
        "\"CE_${CMP_EQ}\" \"RP_${REL_P}\" \"RN_${REL_NORM}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "P_base/folder/file.txt") != NULL);
    ASSERT(strstr(output, "HN_ON") != NULL);
    ASSERT(strstr(output, "HE_ON") != NULL);
    ASSERT(strstr(output, "IA_OFF") != NULL);
    ASSERT(strstr(output, "CE_ON") != NULL);
    ASSERT(strstr(output, "RP_dir/file.txt") != NULL);
    ASSERT(strstr(output, "RN_dir/file.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_path") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

void run_transpiler_suite_misc(int *passed, int *failed) {
    test_config_release_compile_and_output_properties(passed, failed);
    test_config_debug_output_directory_property(passed, failed);
    test_conditional_target_properties_dual_read_debug(passed, failed);
    test_conditional_target_properties_dual_read_release(passed, failed);
    test_install_targets(passed, failed);
    test_install_targets_runtime_library_archive(passed, failed);
    test_install_files_programs_directories(passed, failed);
    test_link_directories_global(passed, failed);
    test_link_libraries_global(passed, failed);
    test_link_libraries_global_framework(passed, failed);
    test_remove_definitions_global(passed, failed);
    test_enable_language_sets_compiler_loaded_var(passed, failed);
    test_site_name_sets_defined_variable(passed, failed);
    test_get_directory_property_reads_values(passed, failed);
    test_export_partial_support_targets_file_namespace(passed, failed);
    test_export_support_export_set_from_install_targets(passed, failed);
    test_export_package_registers_package_dir(passed, failed);
    test_get_source_file_property_reads_values(passed, failed);
    test_cmake_dependent_option_command(passed, failed);
    test_subdirs_legacy_exclude_from_all(passed, failed);
    test_compat_noop_commands_are_ignored_without_unsupported(passed, failed);
    test_create_test_sourcelist_generates_driver_and_list(passed, failed);
    test_load_command_sets_loaded_variable(passed, failed);
    test_qt_wrap_cpp_generates_moc_list_and_file(passed, failed);
    test_qt_wrap_ui_generates_ui_list_and_file(passed, failed);
    test_execute_process_sets_output_error_and_result_variables(passed, failed);
    test_exec_program_sets_output_and_return_value(passed, failed);
    test_fltk_wrap_ui_generates_source_list_and_file(passed, failed);
    test_separate_arguments_unix_command(passed, failed);
    test_mark_as_advanced_is_supported(passed, failed);
    test_block_endblock_and_cmake_policy_are_supported_as_noop(passed, failed);
    test_package_helpers_are_supported_and_set_property_empty_is_safe(passed, failed);
    test_aux_source_directory_collects_supported_sources(passed, failed);
    test_define_property_metadata_is_visible_via_get_property(passed, failed);
    test_cmake_push_pop_check_state_restore(passed, failed);
    test_get_filename_component_modes(passed, failed);
    test_cmake_path_get_and_set_normalize(passed, failed);
    test_cmake_path_append_compare_has_is_normal_relative(passed, failed);
}
