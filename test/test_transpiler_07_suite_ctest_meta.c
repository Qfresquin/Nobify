#include "test_transpiler_shared.h"

TEST(cmake_file_api_query_generates_query_files) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree(".cmake/api");
    const char *input =
        "project(Test)\n"
        "cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2 CACHE 2 TOOLCHAINS v1 CMAKEFILES 1)\n"
        "cmake_file_api(QUERY CLIENT nobify API_VERSION 1 CODEMODEL 2)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"FAPI_${CMAKE_FILE_API}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DFAPI_1") != NULL);
    ASSERT(nob_file_exists(".cmake/api/v1/query/codemodel-v2.json"));
    ASSERT(nob_file_exists(".cmake/api/v1/query/cache-v2.json"));
    ASSERT(nob_file_exists(".cmake/api/v1/query/toolchains-v1.json"));
    ASSERT(nob_file_exists(".cmake/api/v1/query/cmakefiles-v1.json"));
    ASSERT(nob_file_exists(".cmake/api/v1/query/client-nobify/query/codemodel-v2.json"));
    ASSERT(diag_telemetry_unsupported_count_for("cmake_file_api") == 0);
    ASSERT(diag_has_errors() == false);

    remove_test_tree(".cmake/api");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_instrumentation_generates_query_and_sets_vars) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree(".cmake/instrumentation");
    const char *input =
        "project(Test)\n"
        "cmake_instrumentation(API_VERSION 1 DATA_VERSION 1 "
        "HOOKS postGenerate "
        "QUERIES staticSystemInformation "
        "CALLBACK ${CMAKE_COMMAND} -P handle.cmake)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"INS_${CMAKE_INSTRUMENTATION}\" "
        "\"IAPI_${CMAKE_INSTRUMENTATION_API_VERSION}\" "
        "\"IDATA_${CMAKE_INSTRUMENTATION_DATA_VERSION}\" "
        "\"IHOOKS_${CMAKE_INSTRUMENTATION_HOOKS}\" "
        "\"IQUERIES_${CMAKE_INSTRUMENTATION_QUERIES}\" "
        "\"ICB_${CMAKE_INSTRUMENTATION_CALLBACKS}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DINS_ON") != NULL);
    ASSERT(strstr(output, "-DIAPI_1") != NULL);
    ASSERT(strstr(output, "-DIDATA_1") != NULL);
    ASSERT(strstr(output, "-DIHOOKS_postGenerate") != NULL);
    ASSERT(strstr(output, "-DIQUERIES_staticSystemInformation") != NULL);
    ASSERT(strstr(output, "-DICB_") != NULL);
    ASSERT(strstr(output, "handle.cmake") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_instrumentation") == 0);
    ASSERT(diag_has_errors() == false);

    remove_test_tree(".cmake/instrumentation");
    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(enable_testing_sets_builtin_variable) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "enable_testing()\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"TESTING_${CMAKE_TESTING_ENABLED}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DTESTING_ON") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("enable_testing") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_test_name_and_legacy_signatures) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "enable_testing()\n"
        "add_test(NAME smoke COMMAND app --ping)\n"
        "add_test(legacy app --legacy)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"TESTCOUNT_${CMAKE_CTEST_TEST_COUNT}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DTESTCOUNT_2") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("add_test") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(get_test_property_basic_fields) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "enable_testing()\n"
        "add_test(NAME smoke COMMAND app WORKING_DIRECTORY tests COMMAND_EXPAND_LISTS)\n"
        "get_test_property(smoke COMMAND T_CMD)\n"
        "get_test_property(smoke WORKING_DIRECTORY T_WD)\n"
        "get_test_property(smoke COMMAND_EXPAND_LISTS T_EXP)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"CMD_${T_CMD}\" \"WD_${T_WD}\" \"EXP_${T_EXP}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DCMD_app") != NULL);
    ASSERT(strstr(output, "-DWD_tests") != NULL);
    ASSERT(strstr(output, "-DEXP_ON") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("get_test_property") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_tests_properties_then_get_test_property) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "enable_testing()\n"
        "add_test(NAME smoke COMMAND app)\n"
        "set_tests_properties(smoke PROPERTIES TIMEOUT 45)\n"
        "get_test_property(smoke TIMEOUT TMO)\n"
        "get_test_property(smoke UNKNOWN_PROP TMISS)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"TMO_${TMO}\" \"TMISS_${TMISS}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DTMO_45") != NULL);
    ASSERT(strstr(output, "-DTMISS_NOTFOUND") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("set_tests_properties") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("get_test_property") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(ctest_script_mode_commands_update_expected_variables) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_ctest_script_mode.cmake", "set(CTEST_SCRIPT_FLAG ON)\n");
    const char *input =
        "project(Test)\n"
        "enable_testing()\n"
        "add_test(NAME smoke COMMAND app)\n"
        "include(CTestScriptMode)\n"
        "ctest_start(Experimental Nightly)\n"
        "ctest_configure(BUILD build SOURCE src RETURN_VALUE CFG_RV)\n"
        "ctest_build(RETURN_VALUE BLD_RV NUMBER_ERRORS BLD_ERR NUMBER_WARNINGS BLD_WARN)\n"
        "ctest_test(RETURN_VALUE TST_RV)\n"
        "ctest_coverage(RETURN_VALUE COV_RV)\n"
        "ctest_memcheck(RETURN_VALUE MEM_RV)\n"
        "ctest_submit(RETURN_VALUE SUB_RV)\n"
        "ctest_upload(FILES artifacts.txt)\n"
        "ctest_read_custom_files(.)\n"
        "ctest_empty_binary_directory(ctest_bin)\n"
        "ctest_sleep(0)\n"
        "ctest_run_script(temp_ctest_script_mode.cmake RETURN_VALUE RUN_RV)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"SM_${CTEST_SCRIPT_MODE}\" "
        "\"DM_${CTEST_DASHBOARD_MODEL}\" "
        "\"DT_${CTEST_DASHBOARD_TRACK}\" "
        "\"CFG_${CFG_RV}\" \"BLD_${BLD_RV}\" \"ERR_${BLD_ERR}\" \"WRN_${BLD_WARN}\" "
        "\"TST_${TST_RV}\" \"COV_${COV_RV}\" \"MEM_${MEM_RV}\" \"SUB_${SUB_RV}\" "
        "\"UP_${CTEST_UPLOAD_RETURN_VALUE}\" \"RUN_${RUN_RV}\" "
        "\"TR_${CTEST_TESTS_RUN}\" "
        "\"SFLAG_${CTEST_SCRIPT_FLAG}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DSM_ON") != NULL);
    ASSERT(strstr(output, "-DDM_Experimental") != NULL);
    ASSERT(strstr(output, "-DDT_Nightly") != NULL);
    ASSERT(strstr(output, "-DCFG_0") != NULL);
    ASSERT(strstr(output, "-DBLD_0") != NULL);
    ASSERT(strstr(output, "-DERR_0") != NULL);
    ASSERT(strstr(output, "-DWRN_0") != NULL);
    ASSERT(strstr(output, "-DTST_0") != NULL);
    ASSERT(strstr(output, "-DCOV_0") != NULL);
    ASSERT(strstr(output, "-DMEM_0") != NULL);
    ASSERT(strstr(output, "-DSUB_0") != NULL);
    ASSERT(strstr(output, "-DUP_0") != NULL);
    ASSERT(strstr(output, "-DRUN_0") != NULL);
    ASSERT(strstr(output, "-DTR_1") != NULL);
    ASSERT(strstr(output, "-DSFLAG_ON") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("ctest_start") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("ctest_run_script") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_ctest_script_mode.cmake");
    TEST_PASS();
}

TEST(ctest_coverage_collect_gcov_generates_cdash_bundle_metadata) {
    Arena *arena = arena_create(1024 * 1024);
    nob_mkdir_if_not_exists("temp_cov");
    nob_mkdir_if_not_exists("temp_cov/src");
    nob_mkdir_if_not_exists("temp_cov/build");
    nob_mkdir_if_not_exists("temp_cov/build/sub");
    write_test_file("temp_cov/build/sub/a.gcda", "gcda");
    write_test_file("temp_cov/build/sub/b.gcno", "gcno");
    write_test_file("temp_cov/build/sub/c.gcov", "gcov");
    write_test_file("temp_cov/build/sub/ignore.txt", "ignore");

    const char *input =
        "project(Test)\n"
        "include(CTestCoverageCollectGCOV)\n"
        "ctest_coverage_collect_gcov(\n"
        "  TARBALL coverage/cov_bundle.tar\n"
        "  SOURCE temp_cov/src\n"
        "  BUILD temp_cov/build\n"
        "  GCOV_COMMAND gcov\n"
        "  GCOV_OPTIONS -b -p)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"MOD_${CMAKE_CTEST_COVERAGE_COLLECT_GCOV_MODULE_INITIALIZED}\" "
        "\"RV_${CTEST_COVERAGE_COLLECT_GCOV_RETURN_VALUE}\" "
        "\"COUNT_${CTEST_COVERAGE_COLLECT_GCOV_FILE_COUNT}\" "
        "\"TB_${CTEST_COVERAGE_COLLECT_GCOV_TARBALL}\" "
        "\"DJ_${CTEST_COVERAGE_COLLECT_GCOV_DATA_JSON}\" "
        "\"LX_${CTEST_COVERAGE_COLLECT_GCOV_COVERAGE_XML}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DMOD_ON") != NULL);
    ASSERT(strstr(output, "-DRV_0") != NULL);
    ASSERT(strstr(output, "-DCOUNT_3") != NULL);
    ASSERT(strstr(output, "cov_bundle.tar") != NULL);
    ASSERT(strstr(output, "data.json") != NULL);
    ASSERT(strstr(output, "Coverage.xml") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("ctest_coverage_collect_gcov") == 0);
    ASSERT(diag_has_errors() == false);

    ASSERT(nob_file_exists("coverage/cov_bundle.tar"));
    ASSERT(nob_file_exists("temp_cov/build/Testing/CoverageInfo/data.json"));
    ASSERT(nob_file_exists("temp_cov/build/Testing/CoverageInfo/Labels.json"));
    ASSERT(nob_file_exists("temp_cov/build/Testing/CoverageInfo/Coverage.xml"));

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("coverage/cov_bundle.tar");
    nob_delete_file("temp_cov/build/Testing/CoverageInfo/data.json");
    nob_delete_file("temp_cov/build/Testing/CoverageInfo/Labels.json");
    nob_delete_file("temp_cov/build/Testing/CoverageInfo/Coverage.xml");
    nob_delete_file("temp_cov/build/sub/a.gcda");
    nob_delete_file("temp_cov/build/sub/b.gcno");
    nob_delete_file("temp_cov/build/sub/c.gcov");
    nob_delete_file("temp_cov/build/sub/ignore.txt");
    TEST_PASS();
}

TEST(ctest_coverage_collect_gcov_delete_removes_coverage_artifacts) {
    Arena *arena = arena_create(1024 * 1024);
    nob_mkdir_if_not_exists("temp_cov_delete");
    nob_mkdir_if_not_exists("temp_cov_delete/build");
    write_test_file("temp_cov_delete/build/one.gcda", "gcda");
    write_test_file("temp_cov_delete/build/two.gcno", "gcno");

    const char *input =
        "project(Test)\n"
        "ctest_coverage_collect_gcov(TARBALL temp_cov_delete/bundle.tar BUILD temp_cov_delete/build DELETE QUIET)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"RV_${CTEST_COVERAGE_COLLECT_GCOV_RETURN_VALUE}\" \"COUNT_${CTEST_COVERAGE_COLLECT_GCOV_FILE_COUNT}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DRV_0") != NULL);
    ASSERT(strstr(output, "-DCOUNT_2") != NULL);
    ASSERT(nob_file_exists("temp_cov_delete/bundle.tar"));
    ASSERT(!nob_file_exists("temp_cov_delete/build/one.gcda"));
    ASSERT(!nob_file_exists("temp_cov_delete/build/two.gcno"));
    ASSERT(diag_telemetry_unsupported_count_for("ctest_coverage_collect_gcov") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_cov_delete/bundle.tar");
    nob_delete_file("temp_cov_delete/build/Testing/CoverageInfo/data.json");
    nob_delete_file("temp_cov_delete/build/Testing/CoverageInfo/Labels.json");
    nob_delete_file("temp_cov_delete/build/Testing/CoverageInfo/Coverage.xml");
    TEST_PASS();
}

TEST(build_command_configuration_and_target) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "build_command(BUILD_CMD CONFIGURATION Debug TARGET app)\n"
        "string(REPLACE \" \" _ BUILD_CMD_FLAT \"${BUILD_CMD}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"BC_${BUILD_CMD_FLAT}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DBC_cmake_--build_._--config_Debug_--target_app") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("build_command") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

void run_transpiler_suite_ctest_meta(int *passed, int *failed) {
    test_cmake_file_api_query_generates_query_files(passed, failed);
    test_cmake_instrumentation_generates_query_and_sets_vars(passed, failed);
    test_enable_testing_sets_builtin_variable(passed, failed);
    test_add_test_name_and_legacy_signatures(passed, failed);
    test_get_test_property_basic_fields(passed, failed);
    test_set_tests_properties_then_get_test_property(passed, failed);
    test_ctest_script_mode_commands_update_expected_variables(passed, failed);
    test_ctest_coverage_collect_gcov_generates_cdash_bundle_metadata(passed, failed);
    test_ctest_coverage_collect_gcov_delete_removes_coverage_artifacts(passed, failed);
    test_build_command_configuration_and_target(passed, failed);
}
