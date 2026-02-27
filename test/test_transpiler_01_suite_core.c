#include "test_transpiler_shared.h"

TEST(simple_project) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = "project(TestProject VERSION 1.0)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "#define NOB_IMPLEMENTATION") != NULL);
    ASSERT(strstr(output, "#include \"nob.h\"") != NULL);
    ASSERT(strstr(output, "int main(int argc, char **argv)") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena); // Libera a AST e modelos
    TEST_PASS();
}

TEST(add_executable) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "project(Test)\n"
        "add_executable(app main.c util.c)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "app") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_library_static) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "project(Test)\n"
        "add_library(mylib STATIC lib.c)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "mylib") != NULL);
    #if defined(_WIN32)
    ASSERT(strstr(output, "mylib.lib") != NULL);
    #else
    ASSERT(strstr(output, "libmylib.a") != NULL);
    #endif
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_library_shared) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "project(Test)\n"
        "add_library(mylib SHARED lib.c)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "mylib") != NULL);
    #if defined(_WIN32)
    ASSERT(strstr(output, "mylib.dll") != NULL);
    #elif defined(__APPLE__)
    ASSERT(strstr(output, "libmylib.dylib") != NULL);
    #else
    ASSERT(strstr(output, "libmylib.so") != NULL);
    #endif
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_library_object) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(objlib OBJECT lib.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "objlib") != NULL);
    ASSERT(strstr(output, "build/objlib_0.o") != NULL);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_objlib))") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_library_interface) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(iface INTERFACE)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "iface") != NULL);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_iface))") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_library_imported) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(ext SHARED IMPORTED)\n"
        "set_target_properties(ext PROPERTIES IMPORTED_LOCATION /opt/libext.so)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "ext") != NULL);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_ext))") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_library_alias) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(real STATIC lib.c)\n"
        "add_library(real_alias ALIAS real)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "real_alias") != NULL);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_real_alias))") == NULL);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_real))") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_executable_imported_and_alias) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app_real main.c)\n"
        "add_executable(app_alias ALIAS app_real)\n"
        "add_executable(tool IMPORTED GLOBAL)\n"
        "set_target_properties(tool PROPERTIES IMPORTED_LOCATION /opt/tool)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_app_real))") != NULL);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_app_alias))") == NULL);
    ASSERT(strstr(output, "if (!nob_cmd_run_sync(cmd_tool))") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_variable) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "set(APP_NAME hello)\n"
        "add_executable(${APP_NAME} main.c)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_hello") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_statement) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "if(TRUE)\n"
        "  add_executable(selected main.c)\n"
        "else()\n"
        "  add_executable(other main.c)\n"
        "endif()";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_selected") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_other") == NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_target_properties_output_name) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME myapp)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    #if defined(_WIN32)
    ASSERT(strstr(output, "build/myapp.exe") != NULL);
    #else
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-o\", \"build/myapp\")") != NULL);
    #endif

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_target_properties_prefix_suffix) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_library(core SHARED core.c)\n"
        "set_target_properties(core PROPERTIES PREFIX \"\" SUFFIX .dylib)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_core, \"-o\", \"build/core.dylib\")") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_compile_options_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_compile_options(-Wall -Wextra)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wall\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wextra\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("add_compile_options") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_compile_definitions_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_compile_definitions(GLOBAL_DEF=1 -DLEGACY_DEF)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DGLOBAL_DEF=1\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DLEGACY_DEF\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("add_compile_definitions") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_definitions_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_definitions(-DOLD_STYLE=1 -fPIC)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DOLD_STYLE=1\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-fPIC\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("add_definitions") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_link_options_global) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_link_options(-Wl,--as-needed -s)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Wl,--as-needed\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-s\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("add_link_options") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_directory_properties_global_effects) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set_directory_properties(PROPERTIES "
        "INCLUDE_DIRECTORIES dir_a;dir_b "
        "LINK_DIRECTORIES ldir_a;ldir_b "
        "COMPILE_DEFINITIONS DIR_DEF=1;-DLEGACY_DIR "
        "COMPILE_OPTIONS -Wshadow;-Wconversion "
        "LINK_OPTIONS -Wl,--as-needed)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Idir_a\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Idir_b\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Lldir_a\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Lldir_b\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DDIR_DEF=1\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DLEGACY_DIR\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wshadow\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-Wconversion\")") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-Wl,--as-needed\")") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("set_directory_properties") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_source_files_properties_compile_props) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c util.c)\n"
        "set_source_files_properties(util.c PROPERTIES COMPILE_DEFINITIONS UTIL_ONLY=1 COMPILE_OPTIONS -Werror)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(count_occurrences(output, "nob_cmd_append(&cc_cmd, \"-DUTIL_ONLY=1\")") == 1);
    ASSERT(count_occurrences(output, "nob_cmd_append(&cc_cmd, \"-Werror\")") == 1);
    ASSERT(diag_telemetry_unsupported_count_for("set_source_files_properties") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(message_command) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = "message(STATUS \"Building project\")";
    
    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    ASSERT(sb.count > 0);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("message") == 0);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(option_command) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "option(BUILD_TESTS \"Build tests\" ON)\n"
        "add_executable(app_${BUILD_TESTS} main.c)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_ON") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(variable_interpolation) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "set(SRC_DIR src)\n"
        "set(MAIN_FILE ${SRC_DIR}/main.c)\n"
        "add_executable(app ${MAIN_FILE})";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-c\", \"src/main.c\", \"-o\", obj);") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(complex_project) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "project(ComplexTest VERSION 2.1)\n"
        "set(CMAKE_C_STANDARD 11)\n"
        "add_library(utils STATIC utils.c)\n"
        "add_library(core SHARED core.c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app utils core)\n"
        "target_include_directories(app PRIVATE include/)\n"
        "target_compile_definitions(app PRIVATE VERSION=2.1)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "NOB_IMPLEMENTATION") != NULL);
    ASSERT(strstr(output, "int main") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(foreach_loop) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "foreach(name IN ITEMS one two)\n"
        "  add_executable(${name} main.c)\n"
        "endforeach()";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_one") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_two") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(empty_project) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = "";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    ASSERT(sb.count > 0);
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "int main") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(multiline_command) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "add_executable(app\n"
        "  main.c\n"
        "  util.c\n"
        "  helper.c\n"
        ")";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "app") != NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_subdirectory_complex) {
    Arena *arena = arena_create(1024 * 1024);
    const char *test_dir = "temp_subdir_test";
    const char *sub_file = "temp_subdir_test/CMakeLists.txt";
    
    if (!nob_mkdir_if_not_exists(test_dir)) {
        printf("    ! Failed to create temp dir\n");
        (*failed)++;
        arena_destroy(arena);
        return;
    }
    
    write_test_file(sub_file, 
        "set(SUB_VAR \"inside_subdir\")\n"
        "add_library(sublib STATIC sub.c)\n"
    );
    
    const char *input = "add_subdirectory(temp_subdir_test)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    
    ASSERT(strstr(output, "Nob_Cmd cmd_sublib") != NULL);
    #if defined(_WIN32)
    ASSERT(strstr(output, "sublib.lib") != NULL);
    #else
    ASSERT(strstr(output, "libsublib.a") != NULL);
    #endif
    
    nob_sb_free(sb);
    arena_destroy(arena);
    
    nob_delete_file(sub_file);
    remove_test_dir(test_dir);
    
    TEST_PASS();
}

TEST(variable_requires_legacy_sets_result_and_reports_missing_requirements) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(FEATURE_ON TRUE)\n"
        "set(HAS_A TRUE)\n"
        "set(HAS_B OFF)\n"
        "variable_requires(FEATURE_ON CHECK_OK HAS_A HAS_B)\n"
        "set(FEATURE_OFF OFF)\n"
        "set(CHECK_SKIP keep)\n"
        "variable_requires(FEATURE_OFF CHECK_SKIP HAS_B)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"REQ_${CHECK_OK}\" \"SKIP_${CHECK_SKIP}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DREQ_FALSE") != NULL);
    ASSERT(strstr(output, "-DSKIP_keep") != NULL);
    ASSERT(diag_has_errors() == true);
    ASSERT(diag_telemetry_unsupported_count_for("variable_requires") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(message_without_type) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = "message(\"hello sem tipo\")";
    
    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    ASSERT(sb.count > 0);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("message") == 0);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(message_fatal_error_stops_evaluation) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "message(FATAL_ERROR \"stop now\")\n"
        "target_compile_definitions(app PRIVATE SHOULD_NOT_APPEAR=1)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(diag_has_errors() == true);
    ASSERT(strstr(output, "SHOULD_NOT_APPEAR") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(message_fatal_error_can_continue_when_enabled) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "message(FATAL_ERROR \"stop now\")\n"
        "target_compile_definitions(app PRIVATE SHOULD_APPEAR=1)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    Transpiler_Run_Options options = {0};
    options.continue_on_fatal_error = true;
    transpile_datree_ex(root, &sb, &options);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(diag_has_errors() == false);
    ASSERT(strstr(output, "SHOULD_APPEAR=1") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_subdirectory_current_list_dir) {
    Arena *arena = arena_create(1024 * 1024);
    const char *test_dir = "temp_subdir_list_dir_test";
    const char *sub_file = "temp_subdir_list_dir_test/CMakeLists.txt";
    
    if (!nob_mkdir_if_not_exists(test_dir)) {
        printf("    ! Failed to create temp dir\n");
        (*failed)++;
        arena_destroy(arena);
        return;
    }
    
    write_test_file(sub_file,
        "add_library(sublist STATIC sub.c)\n"
        "target_include_directories(sublist PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)\n"
    );
    
    const char *input = "add_subdirectory(temp_subdir_list_dir_test)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-I") != NULL);
    ASSERT(strstr(output, "temp_subdir_list_dir_test") != NULL);
    ASSERT(strstr(output, "/include\")") != NULL);
    ASSERT(strstr(output, "CMAKE_CURRENT_LIST_DIR") == NULL);
    
    nob_sb_free(sb);
    arena_destroy(arena);
    
    nob_delete_file(sub_file);
    remove_test_dir(test_dir);
    
    TEST_PASS();
}

TEST(macro_invocation_add_executable) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "macro(make_app name src)\n"
        "  add_executable(${name} ${src})\n"
        "endmacro()\n"
        "make_app(app main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app") != NULL);
    ASSERT(strstr(output, "\"main.c\"") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(macro_invocation_set_variable_param) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "macro(set_named var value)\n"
        "  set(${var} ${value})\n"
        "endmacro()\n"
        "set_named(TGT app)\n"
        "add_executable(${TGT} main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(function_invocation_add_executable) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "function(make_app name src)\n"
        "  add_executable(${name} ${src})\n"
        "endfunction()\n"
        "make_app(app main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app") != NULL);
    ASSERT(strstr(output, "\"main.c\"") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(function_scope_parent_scope) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "function(make_target out_name)\n"
        "  set(${out_name} app PARENT_SCOPE)\n"
        "endfunction()\n"
        "make_target(TGT)\n"
        "add_executable(${TGT} main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(function_local_scope_no_leak) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(TMP global)\n"
        "function(override_tmp)\n"
        "  set(TMP local_only)\n"
        "endfunction()\n"
        "override_tmp()\n"
        "add_executable(${TMP} other.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};

    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_global") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_local_only") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(math_expr_basic_and_hex_decimal_output) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(A 7)\n"
        "math(EXPR SUM \"${A} + 5\")\n"
        "set(H 0000000a)\n"
        "math(EXPR HD \"0x${H}\" OUTPUT_FORMAT DECIMAL)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE SUM=${SUM} HD=${HD})";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DSUM=12\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DHD=10\");") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("math") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(math_expr_shift_wrap_and_hex_lowercase) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "math(EXPR LSHIFT \"1 << 65\")\n"
        "math(EXPR HEXNEG \"-1\" OUTPUT_FORMAT HEXADECIMAL)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE LSHIFT=${LSHIFT} HEXNEG=${HEXNEG})";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DLSHIFT=2\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DHEXNEG=0xffffffffffffffff\");") != NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("math") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(math_expr_reports_invalid_format_and_range) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "math(EXPR BADFMT \"10\" OUTPUT_FORMAT BIN)\n"
        "math(EXPR OOR \"0xffffffffffffffff\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE BADFMT=${BADFMT} OOR=${OOR})";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DBADFMT=0\");") != NULL);
    ASSERT(strstr(output, "nob_cmd_append(&cc_cmd, \"-DOOR=0\");") != NULL);
    ASSERT(diag_has_errors() == true);
    ASSERT(diag_telemetry_unsupported_count_for("math") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(while_break_skips_remaining_body) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(RUN ON)\n"
        "while(RUN)\n"
        "  break()\n"
        "  add_executable(never main.c)\n"
        "endwhile()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_never") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(while_continue_skips_remaining_body) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(RUN ON)\n"
        "while(RUN)\n"
        "  set(RUN OFF)\n"
        "  continue()\n"
        "  add_executable(never main.c)\n"
        "endwhile()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_never") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(function_return_stops_function_body) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "function(make_targets)\n"
        "  add_executable(one main.c)\n"
        "  return()\n"
        "  add_executable(two main.c)\n"
        "endfunction()\n"
        "make_targets()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_one") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_two") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_condition_and_or_not_precedence) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(A ON)\n"
        "set(B OFF)\n"
        "set(C OFF)\n"
        "if(A AND NOT B OR C)\n"
        "  add_executable(logic_ok main.c)\n"
        "endif()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_logic_ok") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_condition_parentheses_do_not_generate_fake_commands) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(A ON)\n"
        "set(B OFF)\n"
        "set(C ON)\n"
        "if((A AND NOT B) OR (C AND (A OR B)))\n"
        "  add_executable(paren_ok main.c)\n"
        "endif()";

    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_paren_ok") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("AND") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("OR") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("NOT") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_condition_comparators_string_and_numeric) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "if(abc STREQUAL abc)\n"
        "  add_executable(str_ok main.c)\n"
        "endif()\n"
        "if(10 GREATER 2 AND 3 LESS_EQUAL 3 AND 7 EQUAL 7)\n"
        "  add_executable(num_ok main.c)\n"
        "endif()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_str_ok") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_num_ok") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_condition_false_branch_with_comparator) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "if(1 GREATER 2)\n"
        "  add_executable(never_cmp main.c)\n"
        "endif()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_never_cmp") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_condition_defined_operator) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(HAS_FEATURE ON)\n"
        "if(DEFINED HAS_FEATURE)\n"
        "  add_executable(def_ok main.c)\n"
        "endif()\n"
        "if(DEFINED MISSING_FEATURE)\n"
        "  add_executable(def_never main.c)\n"
        "endif()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_def_ok") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_def_never") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_condition_version_comparators) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(V1 3.27.2)\n"
        "set(V2 3.28.0)\n"
        "if(${V1} VERSION_LESS ${V2})\n"
        "  add_executable(vless_ok main.c)\n"
        "endif()\n"
        "if(${V2} VERSION_GREATER ${V1})\n"
        "  add_executable(vgreater_ok main.c)\n"
        "endif()\n"
        "if(3.28.0 VERSION_EQUAL ${V2})\n"
        "  add_executable(vequal_ok main.c)\n"
        "endif()\n"
        "if(${V1} VERSION_GREATER ${V2})\n"
        "  add_executable(vnever main.c)\n"
        "endif()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_vless_ok") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_vgreater_ok") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_vequal_ok") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_vnever") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(if_condition_defined_empty_and_unset) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(EMPTY)\n"
        "if(DEFINED EMPTY)\n"
        "  add_executable(empty_defined_ok main.c)\n"
        "endif()\n"
        "unset(EMPTY)\n"
        "if(DEFINED EMPTY)\n"
        "  add_executable(empty_defined_never main.c)\n"
        "endif()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_empty_defined_ok") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_empty_defined_never") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_env_and_defined_env) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "set(ENV{NOBIFY_TEST_ENV} abc)\n"
        "if(DEFINED ENV{NOBIFY_TEST_ENV})\n"
        "  add_executable(env_defined_ok main.c)\n"
        "endif()\n"
        "if(ENV{NOBIFY_TEST_ENV} STREQUAL abc)\n"
        "  add_executable(env_value_ok main.c)\n"
        "endif()\n"
        "unset(ENV{NOBIFY_TEST_ENV})\n"
        "if(DEFINED ENV{NOBIFY_TEST_ENV})\n"
        "  add_executable(env_defined_never main.c)\n"
        "endif()";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_env_defined_ok") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_env_value_ok") != NULL);
    ASSERT(strstr(output, "Nob_Cmd cmd_env_defined_never") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(generator_expressions_if_bool_config_nested) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMAKE_BUILD_TYPE Debug)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"$<IF:$<BOOL:$<CONFIG:Debug>>,CFG_DEBUG,CFG_OTHER>\")";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DCFG_DEBUG") != NULL);
    ASSERT(strstr(output, "-DCFG_OTHER") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(generator_expression_target_property) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME fancy)\n"
        "target_compile_definitions(app PRIVATE \"OUT_$<TARGET_PROPERTY:app,OUTPUT_NAME>\" \"TYPE_$<TARGET_PROPERTY:app,TYPE>\")";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DOUT_fancy") != NULL);
    ASSERT(strstr(output, "-DTYPE_EXECUTABLE") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(generator_expression_platform_id) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"$<IF:$<PLATFORM_ID:Windows,Darwin,Linux,Unix>,PLAT_OK,PLAT_FAIL>\" \"PLAT_$<PLATFORM_ID>\")";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DPLAT_OK") != NULL);
    ASSERT(strstr(output, "-DPLAT_FAIL") == NULL);

    const char *expected_platform = NULL;
#if defined(_WIN32)
    expected_platform = "Windows";
#elif defined(__APPLE__)
    expected_platform = "Darwin";
#elif defined(__linux__)
    expected_platform = "Linux";
#elif defined(__unix__) || defined(__unix)
    expected_platform = "Unix";
#endif
    if (expected_platform) {
        char expected_define[64] = {0};
        snprintf(expected_define, sizeof(expected_define), "-DPLAT_%s", expected_platform);
        ASSERT(strstr(output, expected_define) != NULL);
    }

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(unsupported_command_telemetry) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "string(REPLACE a b OUT x)\n"
        "list(APPEND X y)\n"
        "string(REPLACE a b OUT z)";

    diag_telemetry_reset();

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(diag_telemetry_unsupported_count_for("string") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("list") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(list_command_family) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(L A;B)\n"
        "list(APPEND L C)\n"
        "list(PREPEND L X)\n"
        "list(REMOVE_ITEM L B)\n"
        "list(REMOVE_DUPLICATES L)\n"
        "list(LENGTH L LLEN)\n"
        "list(GET L 0 FIRST)\n"
        "list(FIND L C FINDC)\n"
        "list(JOIN L , LCSV)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"LLEN_${LLEN}\" \"FIRST_${FIRST}\" \"FIND_${FINDC}\" \"CSV_${LCSV}\")";

    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DLLEN_3") != NULL);
    ASSERT(strstr(output, "-DFIRST_X") != NULL);
    ASSERT(strstr(output, "-DFIND_2") != NULL);
    ASSERT(strstr(output, "-DCSV_X,A,C") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("list") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(string_command_family) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(V \"  Abc-123  \")\n"
        "string(STRIP \"${V}\" VSTRIP)\n"
        "string(TOLOWER \"${VSTRIP}\" VLOW)\n"
        "string(TOUPPER \"${VSTRIP}\" VUP)\n"
        "string(REPLACE - _ VREP \"${VLOW}\")\n"
        "string(APPEND VREP _ok)\n"
        "string(JOIN : VJOIN \"${VLOW}\" \"${VUP}\")\n"
        "string(REGEX MATCH 123 VMATCH \"xx123yy\")\n"
        "string(REGEX REPLACE 123 999 VREG \"id123\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"S_${VSTRIP}\" \"L_${VLOW}\" \"U_${VUP}\" \"R_${VREP}\" \"J_${VJOIN}\" \"M_${VMATCH}\" \"X_${VREG}\")";

    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DS_Abc-123") != NULL);
    ASSERT(strstr(output, "-DL_abc-123") != NULL);
    ASSERT(strstr(output, "-DU_ABC-123") != NULL);
    ASSERT(strstr(output, "-DR_abc_123_ok") != NULL);
    ASSERT(strstr(output, "-DJ_abc-123:ABC-123") != NULL);
    ASSERT(strstr(output, "-DM_123") != NULL);
    ASSERT(strstr(output, "-DX_id999") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("string") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(string_escape_and_regex_concat_regressions) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(TOOLX_CFILES token_from_template)\n"
        "string(REGEX REPLACE \"\\\\$\\\\(([a-zA-Z_][a-zA-Z0-9_]*)\\\\)\" \"\\${\\\\1}\" VPKG \"$(TOOLX_CFILES)\")\n"
        "string(REPLACE \"!^!^!\" \"\\n\" VNL \"A!^!^!B\")\n"
        "string(REPLACE x y VMULTI \"a\" \"x\" \"b\")\n"
        "file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/temp_string_escape.txt \"${VNL}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"PKG_${VPKG}\" \"MULTI_${VMULTI}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DPKG_token_from_template") != NULL);
    ASSERT(strstr(output, "-DMULTI_ayb") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("string") == 0);
    ASSERT(diag_has_errors() == false);

    Nob_String_Builder file_text = {0};
    ASSERT(nob_read_entire_file("temp_string_escape.txt", &file_text));
    ASSERT(file_text.count == 3);
    ASSERT(file_text.items[0] == 'A');
    ASSERT(file_text.items[1] == '\n');
    ASSERT(file_text.items[2] == 'B');

    nob_delete_file("temp_string_escape.txt");
    nob_sb_free(file_text);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(telemetry_realloc_growth_safe) {
    diag_telemetry_reset();

    for (size_t i = 0; i < 24; i++) {
        char name[32] = {0};
        snprintf(name, sizeof(name), "cmd_%zu", i);
        diag_telemetry_record_unsupported_sv(sv_from_cstr(name));
    }
    for (size_t i = 0; i < 3; i++) {
        diag_telemetry_record_unsupported_sv(sv_from_cstr("cmd_7"));
    }

    ASSERT(diag_telemetry_unsupported_unique() == 24);
    ASSERT(diag_telemetry_unsupported_total() == 27);
    ASSERT(diag_telemetry_unsupported_count_for("cmd_7") == 4);

    TEST_PASS();
}

TEST(command_names_case_insensitive) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "PROJECT(Test)\n"
        "SET(FLAG ON)\n"
        "IF(FLAG)\n"
        "ADD_EXECUTABLE(app main.c)\n"
        "TARGET_COMPILE_DEFINITIONS(app PRIVATE UPPER_OK)\n"
        "ENDIF()";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app") != NULL);
    ASSERT(strstr(output, "-DUPPER_OK") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("PROJECT") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("ADD_EXECUTABLE") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

void run_transpiler_suite_core(int *passed, int *failed) {
    test_simple_project(passed, failed);
    test_add_executable(passed, failed);
    test_add_library_static(passed, failed);
    test_add_library_shared(passed, failed);
    test_add_library_object(passed, failed);
    test_add_library_interface(passed, failed);
    test_add_library_imported(passed, failed);
    test_add_library_alias(passed, failed);
    test_add_executable_imported_and_alias(passed, failed);
    test_set_variable(passed, failed);
    test_if_statement(passed, failed);
    test_set_target_properties_output_name(passed, failed);
    test_set_target_properties_prefix_suffix(passed, failed);
    test_add_compile_options_global(passed, failed);
    test_add_compile_definitions_global(passed, failed);
    test_add_definitions_global(passed, failed);
    test_add_link_options_global(passed, failed);
    test_set_directory_properties_global_effects(passed, failed);
    test_set_source_files_properties_compile_props(passed, failed);
    test_message_command(passed, failed);
    test_option_command(passed, failed);
    test_variable_interpolation(passed, failed);
    test_complex_project(passed, failed);
    test_foreach_loop(passed, failed);
    test_empty_project(passed, failed);
    test_multiline_command(passed, failed);
    test_add_subdirectory_complex(passed, failed);
    test_variable_requires_legacy_sets_result_and_reports_missing_requirements(passed, failed);
    test_message_without_type(passed, failed);
    test_message_fatal_error_stops_evaluation(passed, failed);
    test_message_fatal_error_can_continue_when_enabled(passed, failed);
    test_add_subdirectory_current_list_dir(passed, failed);
    test_macro_invocation_add_executable(passed, failed);
    test_macro_invocation_set_variable_param(passed, failed);
    test_function_invocation_add_executable(passed, failed);
    test_function_scope_parent_scope(passed, failed);
    test_function_local_scope_no_leak(passed, failed);
    test_math_expr_basic_and_hex_decimal_output(passed, failed);
    test_math_expr_shift_wrap_and_hex_lowercase(passed, failed);
    test_math_expr_reports_invalid_format_and_range(passed, failed);
    test_while_break_skips_remaining_body(passed, failed);
    test_while_continue_skips_remaining_body(passed, failed);
    test_function_return_stops_function_body(passed, failed);
    test_if_condition_and_or_not_precedence(passed, failed);
    test_if_condition_parentheses_do_not_generate_fake_commands(passed, failed);
    test_if_condition_comparators_string_and_numeric(passed, failed);
    test_if_condition_false_branch_with_comparator(passed, failed);
    test_if_condition_defined_operator(passed, failed);
    test_if_condition_version_comparators(passed, failed);
    test_if_condition_defined_empty_and_unset(passed, failed);
    test_set_env_and_defined_env(passed, failed);
    test_generator_expressions_if_bool_config_nested(passed, failed);
    test_generator_expression_target_property(passed, failed);
    test_generator_expression_platform_id(passed, failed);
    test_unsupported_command_telemetry(passed, failed);
    test_list_command_family(passed, failed);
    test_string_command_family(passed, failed);
    test_string_escape_and_regex_concat_regressions(passed, failed);
    test_telemetry_realloc_growth_safe(passed, failed);
    test_command_names_case_insensitive(passed, failed);
}
