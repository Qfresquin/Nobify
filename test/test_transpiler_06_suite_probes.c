#include "test_transpiler_shared.h"

TEST(cmake_minimum_required_try_compile_and_get_cmake_property_supported) {
    Arena *arena = arena_create(1024 * 1024);
    const char *src_file = "temp_try_compile_ok.c";
    const char *input =
        "cmake_minimum_required(VERSION 3.7...3.16 FATAL_ERROR)\n"
        "project(Test)\n"
        "set(FOO bar CACHE STRING \"\")\n"
        "try_compile(HAVE_FOO ${CMAKE_BINARY_DIR} temp_try_compile_ok.c OUTPUT_VARIABLE TRY_LOG)\n"
        "get_cmake_property(CACHE_VARS CACHE_VARIABLES)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"MIN_${CMAKE_MINIMUM_REQUIRED_VERSION}\" \"VER_${CMAKE_VERSION}\" \"TRY_${HAVE_FOO}\")";

    write_test_file(src_file, "int main(void){ return 0; }\n");
    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "MIN_3.7") != NULL);
    ASSERT(strstr(output, "VER_3.16.0") != NULL);
    ASSERT(strstr(output, "TRY_1") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_minimum_required") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("try_compile") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("get_cmake_property") == 0);
    ASSERT(diag_has_errors() == false);

    nob_delete_file(src_file);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_minimum_required_range_sets_policy_vars) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "cmake_minimum_required(VERSION 3.16...3.27 FATAL_ERROR)\n"
        "project(Test)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"MIN_${CMAKE_MINIMUM_REQUIRED_VERSION}\" "
        "\"POL_${CMAKE_POLICY_VERSION}\" "
        "\"POLMIN_${CMAKE_POLICY_VERSION_MINIMUM}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "MIN_3.16") != NULL);
    ASSERT(strstr(output, "POL_3.27") != NULL);
    ASSERT(strstr(output, "POLMIN_3.16") != NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_minimum_required") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_minimum_required_invalid_signature_reports_error) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "cmake_minimum_required(3.16)\n"
        "project(Test)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(diag_has_errors() == true);
    ASSERT(strstr(output, "Nob_Cmd cmd_app") == NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(try_compile_mingw64_version_behaves_like_curl_check) {
    Arena *arena = arena_create(1024 * 1024);
    const char *src_file = "temp_try_compile_mingw.c";
    const char *input =
        "project(Test)\n"
#if defined(__MINGW32__)
        "set(MINGW ON)\n"
#else
        "set(MINGW OFF)\n"
#endif
        "if(MINGW)\n"
        "  try_compile(MINGW64_VERSION ${CMAKE_BINARY_DIR} temp_try_compile_mingw.c OUTPUT_VARIABLE CURL_TEST_OUTPUT)\n"
        "  if(MINGW64_VERSION)\n"
        "    string(REGEX MATCH \"MINGW64_VERSION=[0-9]+\\.[0-9]+\" CURL_TEST_OUTPUT \"${CURL_TEST_OUTPUT}\")\n"
        "    string(REGEX REPLACE \"MINGW64_VERSION=\" \"\" MINGW64_VERSION \"${CURL_TEST_OUTPUT}\")\n"
        "    if(MINGW64_VERSION VERSION_LESS 3.0)\n"
        "      message(FATAL_ERROR \"mingw-w64 3.0 or upper is required\")\n"
        "    endif()\n"
        "  endif()\n"
        "endif()\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"MINGW_VER_${MINGW64_VERSION}\")";

    write_test_file(src_file, "int main(void){ return 0; }\n");
    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(diag_has_errors() == false);
    ASSERT(strstr(output, "MINGW_VER_") != NULL);

    nob_delete_file(src_file);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(try_compile_real_failure_sets_zero_and_output_log) {
    Arena *arena = arena_create(1024 * 1024);
    const char *bad_src = "temp_try_compile_fail.c";
    const char *input =
        "project(Test)\n"
        "try_compile(HAVE_BAD ${CMAKE_BINARY_DIR} temp_try_compile_fail.c OUTPUT_VARIABLE TC_LOG)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"BAD_${HAVE_BAD}\" \"LOG_${TC_LOG}\")";

    write_test_file(bad_src, "int main(void) { this will fail; }\n");
    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DBAD_0") != NULL);
    ASSERT(strstr(output, "-DLOG_") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("try_compile") == 0);

    nob_delete_file(bad_src);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(try_run_sets_compile_and_run_results) {
    Arena *arena = arena_create(1024 * 1024);
    const char *ok_src = "temp_try_run_ok.c";
    const char *bad_src = "temp_try_run_bad.c";
    const char *input =
        "project(Test)\n"
        "try_run(RUN_OK COMPILE_OK ${CMAKE_BINARY_DIR} temp_try_run_ok.c COMPILE_OUTPUT_VARIABLE COMPILE_LOG RUN_OUTPUT_VARIABLE RUN_LOG)\n"
        "try_run(RUN_BAD COMPILE_BAD ${CMAKE_BINARY_DIR} temp_try_run_bad.c OUTPUT_VARIABLE RUN_BAD_LOG)\n"
        "string(REPLACE \" \" \"_\" RUN_LOG_FLAT \"${RUN_LOG}\")\n"
        "string(REPLACE \" \" \"_\" RUN_BAD_LOG_FLAT \"${RUN_BAD_LOG}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"TRC_${COMPILE_OK}\" \"TRR_${RUN_OK}\" \"TRCB_${COMPILE_BAD}\" \"TRRB_${RUN_BAD}\" "
        "\"TRL_${RUN_LOG_FLAT}\" \"TRBL_${RUN_BAD_LOG_FLAT}\")";

    write_test_file(ok_src,
        "#include <stdio.h>\n"
        "int main(void){ puts(\"try_run_ok_token\"); return 0; }\n");
    write_test_file(bad_src,
        "#include <stdio.h>\n"
        "int main(void){ puts(\"try_run_bad_token\"); return 1; }\n");

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DTRC_1") != NULL);
    ASSERT(strstr(output, "-DTRR_0") != NULL);
    ASSERT(strstr(output, "-DTRCB_1") != NULL);
    ASSERT(strstr(output, "-DTRRB_1") != NULL);
    ASSERT(strstr(output, "try_run_ok_token") != NULL);
    ASSERT(strstr(output, "try_run_bad_token") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("try_run") == 0);
    ASSERT(diag_has_errors() == false);

    nob_delete_file(ok_src);
    nob_delete_file(bad_src);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(try_run_cross_compiling_sets_failed_to_run) {
    Arena *arena = arena_create(1024 * 1024);
    const char *src = "temp_try_run_cross.c";
    const char *input =
        "project(Test)\n"
        "set(CMAKE_CROSSCOMPILING ON)\n"
        "try_run(RUN_RC COMPILE_RC ${CMAKE_BINARY_DIR} temp_try_run_cross.c RUN_OUTPUT_VARIABLE RUN_LOG)\n"
        "string(REPLACE \" \" \"_\" RUN_LOG_FLAT \"${RUN_LOG}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"CRC_${COMPILE_RC}\" \"RRC_${RUN_RC}\" \"RLOG_${RUN_LOG_FLAT}\")";

    write_test_file(src, "int main(void){ return 0; }\n");
    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DCRC_1") != NULL);
    ASSERT(strstr(output, "-DRRC_FAILED_TO_RUN") != NULL);
    ASSERT(strstr(output, "CMAKE_CROSSCOMPILING") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("try_run") == 0);
    ASSERT(diag_has_errors() == false);

    nob_delete_file(src);
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(check_commands_basic_family) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "check_symbol_exists(EINTR errno.h HAVE_EINTR)\n"
        "check_function_exists(printf HAVE_PRINTF)\n"
        "check_include_file(stdio.h HAVE_STDIO_H)\n"
        "check_include_files(\"stddef.h;stdio.h\" HAVE_SYS_HEADERS)\n"
        "check_type_size(\"long long\" SIZEOF_LONG_LONG)\n"
        "check_c_compiler_flag(\"-Wall\" HAVE_WALL)\n"
        "check_struct_has_member(\"struct stat\" st_mtime \"sys/stat.h\" HAVE_STAT_MTIME)\n"
        "check_c_source_compiles(\"int main(void){return 0;}\" HAVE_MINIMAL_MAIN)\n"
        "check_library_exists(\"c\" \"printf\" \"\" HAVE_LIBSOCKET)\n"
        "check_c_source_runs(\"int main(void){return 0;}\" HAVE_MINIMAL_RUN)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"SYM_${HAVE_EINTR}\" \"FUN_${HAVE_PRINTF}\" "
        "\"INC_${HAVE_STDIO_H}\" \"INCS_${HAVE_SYS_HEADERS}\" "
        "\"TS_${SIZEOF_LONG_LONG}\" \"HAVE_TS_${HAVE_SIZEOF_LONG_LONG}\" "
        "\"CFLAG_${HAVE_WALL}\" \"SHM_${HAVE_STAT_MTIME}\" "
        "\"CSC_${HAVE_MINIMAL_MAIN}\" \"LIB_${HAVE_LIBSOCKET}\" \"RUN_${HAVE_MINIMAL_RUN}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DSYM_1") != NULL);
    ASSERT(strstr(output, "-DFUN_1") != NULL);
    ASSERT(strstr(output, "-DINC_1") != NULL);
    ASSERT(strstr(output, "-DINCS_1") != NULL);
    ASSERT(strstr(output, "-DTS_8") != NULL);
    ASSERT(strstr(output, "-DHAVE_TS_1") != NULL);
    ASSERT(strstr(output, "-DCFLAG_1") != NULL);
    ASSERT(strstr(output, "-DSHM_1") != NULL);
    ASSERT(strstr(output, "-DCSC_1") != NULL);
    ASSERT(strstr(output, "-DLIB_") != NULL);
    ASSERT(strstr(output, "-DRUN_1") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("check_symbol_exists") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_function_exists") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_include_file") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_include_files") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_type_size") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_c_compiler_flag") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_struct_has_member") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_c_source_compiles") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_library_exists") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("check_c_source_runs") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(check_c_source_runs_real_probe_optional) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMK2NOB_REAL_PROBES ON)\n"
        "check_c_source_runs(\"int main(void){return 0;}\" RUN_OK)\n"
        "check_c_source_runs(\"int main(void){return 1;}\" RUN_BAD)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"RUNOK_${RUN_OK}\" \"RUNBAD_${RUN_BAD}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "RUNOK_1") != NULL);
    ASSERT(strstr(output, "RUNBAD_0") != NULL);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(check_symbol_and_compiles_real_probe_optional) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(CMK2NOB_REAL_PROBES ON)\n"
        "check_symbol_exists(EINTR errno.h SYM_OK)\n"
        "check_symbol_exists(THIS_SYMBOL_DOES_NOT_EXIST errno.h SYM_BAD)\n"
        "check_c_source_compiles(\"int main(void){return 0;}\" COMP_OK)\n"
        "check_c_source_compiles(\"int main(void){ this is broken; }\" COMP_BAD)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"SYMOK_${SYM_OK}\" \"SYMBAD_${SYM_BAD}\" \"COMPOK_${COMP_OK}\" \"COMPBAD_${COMP_BAD}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "SYMOK_1") != NULL);
    ASSERT(strstr(output, "SYMBAD_0") != NULL);
    ASSERT(strstr(output, "COMPOK_1") != NULL);
    ASSERT(strstr(output, "COMPBAD_0") != NULL);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_required_settings_apply_to_checks_and_try_compile) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_required_probe");
    nob_mkdir_if_not_exists("temp_required_probe");
    write_test_file("temp_required_probe/required_probe.h",
                    "#ifndef REQUIRED_FLAG\n"
                    "#error REQUIRED_FLAG missing\n"
                    "#endif\n");
    write_test_file("temp_required_probe/probe.c",
                    "#include <required_probe.h>\n"
                    "int main(void){ return 0; }\n");

    const char *input =
        "project(Test)\n"
        "set(CMK2NOB_REAL_PROBES ON)\n"
        "set(CMAKE_REQUIRED_INCLUDES \"${CMAKE_CURRENT_SOURCE_DIR}/temp_required_probe\")\n"
        "set(CMAKE_REQUIRED_DEFINITIONS \"REQUIRED_FLAG=1\")\n"
        "check_c_source_compiles(\"#include <required_probe.h>\\nint main(void){return 0;}\" CHECK_OK)\n"
        "try_compile(TRY_OK ${CMAKE_BINARY_DIR} temp_required_probe/probe.c)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"CHECK_${CHECK_OK}\" \"TRY_${TRY_OK}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "CHECK_1") != NULL);
    ASSERT(strstr(output, "TRY_1") != NULL);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    remove_test_tree("temp_required_probe");
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_end_to_end_compile_flags_equivalence_optional) {
    const char *fixture_dir = "temp_e2e_equiv";
    const char *cmakelists_path = "temp_e2e_equiv/CMakeLists.txt";

    remove_test_tree(fixture_dir);
    nob_mkdir_if_not_exists(fixture_dir);
    nob_mkdir_if_not_exists("temp_e2e_equiv/include");
    nob_mkdir_if_not_exists("temp_e2e_equiv/app_inc");

    write_test_file("temp_e2e_equiv/main.c", "int main(void){return 0;}\n");
    write_test_file("temp_e2e_equiv/lib.c", "int lib_fn(void){return 1;}\n");
    write_test_file("temp_e2e_equiv/include/lib.h", "int lib_fn(void);\n");
    write_test_file("temp_e2e_equiv/app_inc/app.h", "#define APP_H 1\n");
    write_test_file(cmakelists_path,
        "cmake_minimum_required(VERSION 3.16)\n"
        "project(E2EEquiv C)\n"
        "add_library(mylib STATIC lib.c)\n"
        "target_include_directories(mylib PRIVATE include)\n"
        "target_compile_definitions(mylib PRIVATE LIBDEF=1)\n"
        "target_compile_options(mylib PRIVATE -Wall)\n"
        "add_executable(app main.c)\n"
        "target_include_directories(app PRIVATE app_inc)\n"
        "target_compile_definitions(app PRIVATE APPDEF=42)\n"
        "target_compile_options(app PRIVATE -Wextra)\n"
        "target_link_libraries(app PRIVATE mylib m)\n");

#if defined(_WIN32)
    int has_cmake = run_shell_command_silent("cmake --version > NUL 2>&1");
#else
    int has_cmake = run_shell_command_silent("cmake --version > /dev/null 2>&1");
#endif
    if (has_cmake != 0) {
        nob_log(NOB_INFO, "Skipping optional e2e equivalence test: cmake not found");
        remove_test_tree(fixture_dir);
        TEST_PASS();
    }

    int cmake_ok;
#if defined(_WIN32)
    cmake_ok = run_shell_command_silent(
        "cmake -S \"temp_e2e_equiv\" -B \"temp_e2e_equiv/build\" -G \"Ninja\" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > NUL 2>&1");
    if (cmake_ok != 0) {
        cmake_ok = run_shell_command_silent(
            "cmake -S \"temp_e2e_equiv\" -B \"temp_e2e_equiv/build\" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > NUL 2>&1");
    }
#else
    cmake_ok = run_shell_command_silent(
        "cmake -S \"temp_e2e_equiv\" -B \"temp_e2e_equiv/build\" -G \"Ninja\" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null 2>&1");
    if (cmake_ok != 0) {
        cmake_ok = run_shell_command_silent(
            "cmake -S \"temp_e2e_equiv\" -B \"temp_e2e_equiv/build\" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null 2>&1");
    }
#endif
    if (cmake_ok != 0 || !nob_file_exists("temp_e2e_equiv/build/compile_commands.json")) {
        nob_log(NOB_INFO, "Skipping optional e2e equivalence test: cmake configure/export failed");
        remove_test_tree(fixture_dir);
        TEST_PASS();
    }

    Nob_String_Builder cc_json = {0};
    ASSERT(nob_read_entire_file("temp_e2e_equiv/build/compile_commands.json", &cc_json));

    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);
    Nob_String_Builder cmake_input = {0};
    ASSERT(nob_read_entire_file(cmakelists_path, &cmake_input));
    char *cmake_input_cstr = nob_temp_sprintf("%.*s", (int)cmake_input.count, cmake_input.items);
    Ast_Root root = parse_cmake(arena, cmake_input_cstr);
    Nob_String_Builder nob_codegen = {0};
    transpile_datree_with_input_path(root, &nob_codegen, cmakelists_path);

    char *cc_text = nob_temp_sprintf("%.*s", (int)cc_json.count, cc_json.items);
    char *nob_text = nob_temp_sprintf("%.*s", (int)nob_codegen.count, nob_codegen.items);

    ASSERT(strstr(cc_text, "-DAPPDEF=42") != NULL);
    ASSERT(strstr(cc_text, "-DLIBDEF=1") != NULL);
    ASSERT(strstr(cc_text, "-I") != NULL);
    ASSERT(strstr(cc_text, "-Wextra") != NULL);
    ASSERT(strstr(cc_text, "-Wall") != NULL);

    ASSERT(strstr(nob_text, "-DAPPDEF=42") != NULL);
    ASSERT(strstr(nob_text, "-DLIBDEF=1") != NULL);
    ASSERT(strstr(nob_text, "-Iapp_inc") != NULL);
    ASSERT(strstr(nob_text, "-Iinclude") != NULL);
    ASSERT(strstr(nob_text, "-Wextra") != NULL);
    ASSERT(strstr(nob_text, "-Wall") != NULL);
#if defined(_WIN32)
    ASSERT(strstr(nob_text, "build/mylib.lib") != NULL);
#else
    ASSERT(strstr(nob_text, "-lm") != NULL);
#endif

    nob_sb_free(nob_codegen);
    nob_sb_free(cmake_input);
    nob_sb_free(cc_json);
    arena_destroy(arena);
    remove_test_tree(fixture_dir);
    TEST_PASS();
}

void run_transpiler_suite_probes(int *passed, int *failed) {
    test_cmake_minimum_required_try_compile_and_get_cmake_property_supported(passed, failed);
    test_cmake_minimum_required_range_sets_policy_vars(passed, failed);
    test_cmake_minimum_required_invalid_signature_reports_error(passed, failed);
    test_try_compile_mingw64_version_behaves_like_curl_check(passed, failed);
    test_try_compile_real_failure_sets_zero_and_output_log(passed, failed);
    test_try_run_sets_compile_and_run_results(passed, failed);
    test_try_run_cross_compiling_sets_failed_to_run(passed, failed);
    test_check_commands_basic_family(passed, failed);
    test_check_c_source_runs_real_probe_optional(passed, failed);
    test_check_symbol_and_compiles_real_probe_optional(passed, failed);
    test_cmake_required_settings_apply_to_checks_and_try_compile(passed, failed);
    test_cmake_end_to_end_compile_flags_equivalence_optional(passed, failed);
}
