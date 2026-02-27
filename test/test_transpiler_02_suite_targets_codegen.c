#include "test_transpiler_shared.h"

TEST(target_link_libraries) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "add_library(mylib STATIC lib.c)\n"
        "target_link_libraries(app mylib)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
#if defined(_WIN32)
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"build/mylib.lib\")") != NULL);
#else
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"build/libmylib.a\")") != NULL);
#endif
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_link_options) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_link_options(app PRIVATE -Wl,--as-needed -s)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Wl,--as-needed\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-s\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_link_directories) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_link_directories(app PRIVATE libs /opt/mylibs -Lalready)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Llibs\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-L/opt/mylibs\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Lalready\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_compile_definitions) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE DEBUG=1)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DDEBUG=1\")") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_compile_options) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_compile_options(app PRIVATE -Wall -Wextra)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wall\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wextra\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_compile_features_basic) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_compile_features(app PRIVATE c_std_11 cxx_std_17)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-std=c11\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-std=c++17\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("target_compile_features") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_precompile_headers_basic) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_precompile_headers(app PRIVATE pch.h)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-include\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"pch.h\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("target_precompile_headers") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_sources_private) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_sources(app PRIVATE util.c helper.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "build/app_0.o") != NULL);
    ASSERT(strstr(output, "build/app_1.o") != NULL);
    ASSERT(strstr(output, "build/app_2.o") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_sources_ignores_duplicates) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_sources(app PRIVATE util.c util.c main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "build/app_0.o") != NULL);
    ASSERT(strstr(output, "build/app_1.o") != NULL);
    ASSERT(strstr(output, "build/app_2.o") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(transitive_compile_usage_requirements) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(base STATIC base.c)\n"
        "target_include_directories(base INTERFACE base_inc)\n"
        "target_compile_definitions(base INTERFACE BASE_DEF=1)\n"
        "target_compile_options(base INTERFACE -Wshadow)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE base)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Ibase_inc\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DBASE_DEF=1\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wshadow\");") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(transitive_link_usage_requirements) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(core STATIC core.c)\n"
        "target_link_libraries(core INTERFACE m)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    #if defined(_WIN32)
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"build/core.lib\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"m.lib\");") != NULL);
    #else
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"build/libcore.a\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-lm\");") != NULL);
    #endif

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_includes_and_definitions) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_include_directories(app PRIVATE include/ src/include)\n"
        "target_compile_definitions(app PRIVATE FEATURE_X=1)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Iinclude/\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Isrc/include\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DFEATURE_X=1\")") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_external_library_flags) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE m pthread)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-lm\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-lpthread\")") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_sanitized_target_identifier) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app-core main.c)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_core") != NULL);
    ASSERT(strstr(output, "Nob_File_Paths objs_app_core") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_custom_command_target_pre_build) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "add_custom_command(TARGET app PRE_BUILD COMMAND echo pre_step COMMENT \"running pre\")";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Custom commands: PRE_BUILD (1)") != NULL);
    ASSERT(strstr(output, "custom_shell = \"echo pre_step\"") != NULL);
    ASSERT(strstr(output, "nob_log(NOB_INFO, \"running pre\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_custom_command_target_post_build_workdir) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "add_custom_command(TARGET app POST_BUILD WORKING_DIRECTORY scripts COMMAND echo done)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Custom commands: POST_BUILD (1)") != NULL);
    ASSERT(strstr(output, "custom_shell = \"cd \\\"scripts\\\" && echo done\"") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&custom_cmd, \"cmd\", \"/C\", custom_shell)") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&custom_cmd, \"sh\", \"-c\", custom_shell)") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_custom_command_target_depends_byproducts) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "add_custom_command(TARGET app POST_BUILD DEPENDS stamp.in BYPRODUCTS stamp.out COMMAND echo done)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Custom commands: POST_BUILD (1)") != NULL);
    ASSERT(strstr(output, "run_custom = false;") != NULL);
    ASSERT(strstr(output, "nob_file_exists(\"stamp.out\")") != NULL);
    ASSERT(strstr(output, "nob_needs_rebuild(\"stamp.out\"") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_custom_command_output_depends_byproducts_workdir) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_custom_command(OUTPUT gen.c gen.h DEPENDS schema.idl BYPRODUCTS gen.log WORKING_DIRECTORY scripts COMMAND echo generate)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Custom commands: OUTPUT (1)") != NULL);
    ASSERT(strstr(output, "nob_file_exists(\"gen.c\")") != NULL);
    ASSERT(strstr(output, "nob_file_exists(\"gen.log\")") != NULL);
    ASSERT(strstr(output, "nob_needs_rebuild(\"gen.c\"") != NULL);
    ASSERT(strstr(output, "cd \\\"scripts\\\" && echo generate") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_custom_command_output_append_depfile_and_implicit_depends) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_custom_command(OUTPUT gen.c COMMAND python gen.py DEPENDS schema.idl)\n"
        "add_custom_command(OUTPUT gen.c APPEND COMMAND echo done DEPFILE gen.d MAIN_DEPENDENCY schema.idl IMPLICIT_DEPENDS C header.h)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Custom commands: OUTPUT (1)") != NULL);
    ASSERT(strstr(output, "python gen.py && echo done") != NULL);
    ASSERT(strstr(output, "nob_file_exists(\"gen.d\")") != NULL);
    ASSERT(strstr(output, "\"schema.idl\"") != NULL);
    ASSERT(strstr(output, "\"header.h\"") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_custom_command_output_command_expand_lists) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(LIST_ARGS a;b;c)\n"
        "add_custom_command(OUTPUT stamp.txt COMMAND_EXPAND_LISTS COMMAND echo ${LIST_ARGS})";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Custom commands: OUTPUT (1)") != NULL);
    ASSERT(strstr(output, "custom_shell = \"echo a b c\"") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_custom_command_target_depends_target_resolves_output_path) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(core STATIC core.c)\n"
        "add_executable(app main.c)\n"
        "set_target_properties(core PROPERTIES OUTPUT_NAME corex)\n"
        "add_custom_command(TARGET app POST_BUILD DEPENDS core BYPRODUCTS stamp.out COMMAND echo done)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Custom commands: POST_BUILD (1)") != NULL);
    ASSERT(strstr(output, "deps_custom_0[] = {") != NULL);
    ASSERT(strstr(output, "corex") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_custom_target_minimal) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_custom_target(gen ALL COMMAND echo gen DEPENDS seed.txt BYPRODUCTS out.txt WORKING_DIRECTORY tools)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Target: gen") != NULL);
    ASSERT(strstr(output, "custom_shell = \"cd \\\"tools\\\" && echo gen\"") != NULL);
    ASSERT(strstr(output, "nob_needs_rebuild(\"out.txt\"") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_dependencies_reorders_targets) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_custom_target(package COMMAND echo package)\n"
        "add_custom_target(gen COMMAND echo gen)\n"
        "add_dependencies(package gen)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    const char *gen_pos = strstr(output, "// --- Target: gen ---");
    const char *pkg_pos = strstr(output, "// --- Target: package ---");
    ASSERT(gen_pos != NULL);
    ASSERT(pkg_pos != NULL);
    ASSERT(gen_pos < pkg_pos);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_platform_toolchain_branches) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(core STATIC core.c)\n"
        "add_library(plugin SHARED plugin.c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core m)\n"
        "target_link_directories(app PRIVATE libs)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "#if defined(_MSC_VER) && !defined(__clang__)") != NULL);
    ASSERT(strstr(output, "lib.exe") != NULL);
    ASSERT(strstr(output, "/Fo:%s") != NULL);
    ASSERT(strstr(output, "/LIBPATH:%s") != NULL);
    ASSERT(strstr(output, "-shared\", \"-fPIC\"") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(get_target_property_and_get_property_target) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES OUTPUT_NAME fancy)\n"
        "get_target_property(OUT1 core OUTPUT_NAME)\n"
        "get_property(OUT2 TARGET core PROPERTY OUTPUT_NAME)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"A_${OUT1}\" \"B_${OUT2}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DA_fancy") != NULL);
    ASSERT(strstr(output, "-DB_fancy") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("get_target_property") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("get_property") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_property_target_and_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "set_property(TARGET app PROPERTY COMPILE_DEFINITIONS FIRST=1)\n"
        "set_property(TARGET app APPEND PROPERTY COMPILE_DEFINITIONS SECOND=2;THIRD=3)\n"
        "set_property(GLOBAL PROPERTY USE_FOLDERS ON)\n"
        "get_property(UF GLOBAL PROPERTY USE_FOLDERS)\n"
        "target_compile_definitions(app PRIVATE \"UF_${UF}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DFIRST=1") != NULL);
    ASSERT(strstr(output, "-DSECOND=2") != NULL);
    ASSERT(strstr(output, "-DTHIRD=3") != NULL);
    ASSERT(strstr(output, "-DUF_ON") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("set_property") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("get_property") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

void run_transpiler_suite_targets_codegen(int *passed, int *failed) {
    test_target_link_libraries(passed, failed);
    test_target_link_options(passed, failed);
    test_target_link_directories(passed, failed);
    test_target_compile_definitions(passed, failed);
    test_target_compile_options(passed, failed);
    test_target_compile_features_basic(passed, failed);
    test_target_precompile_headers_basic(passed, failed);
    test_target_sources_private(passed, failed);
    test_target_sources_ignores_duplicates(passed, failed);
    test_transitive_compile_usage_requirements(passed, failed);
    test_transitive_link_usage_requirements(passed, failed);
    test_codegen_includes_and_definitions(passed, failed);
    test_codegen_external_library_flags(passed, failed);
    test_codegen_sanitized_target_identifier(passed, failed);
    test_add_custom_command_target_pre_build(passed, failed);
    test_add_custom_command_target_post_build_workdir(passed, failed);
    test_add_custom_command_target_depends_byproducts(passed, failed);
    test_add_custom_command_output_depends_byproducts_workdir(passed, failed);
    test_add_custom_command_output_append_depfile_and_implicit_depends(passed, failed);
    test_add_custom_command_output_command_expand_lists(passed, failed);
    test_add_custom_command_target_depends_target_resolves_output_path(passed, failed);
    test_add_custom_target_minimal(passed, failed);
    test_add_dependencies_reorders_targets(passed, failed);
    test_codegen_platform_toolchain_branches(passed, failed);
    test_get_target_property_and_get_property_target(passed, failed);
    test_set_property_target_and_global(passed, failed);
}
