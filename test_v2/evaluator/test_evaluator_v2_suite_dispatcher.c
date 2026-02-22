#include "test_v2_assert.h"
#include "test_v2_suite.h"

#include "test_evaluator_v2_env.h"

#include <stdlib.h>
#include <string.h>

TEST(dispatcher_command_handlers) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    const char *script =
        "add_definitions(-DLEGACY=1 -fPIC)\n"
        "add_compile_options(-Wall)\n"
        "add_executable(app main.c)\n"
        "target_include_directories(app PRIVATE include)\n"
        "target_compile_definitions(app PRIVATE APPDEF=1)\n"
        "target_compile_options(app PRIVATE -Wextra)\n";

    ASSERT(run_script(&env, script));
    ASSERT(count_events(&env, EV_TARGET_INCLUDE_DIRECTORIES) > 0);
    ASSERT(count_events(&env, EV_TARGET_COMPILE_DEFINITIONS) > 0);
    ASSERT(count_events(&env, EV_TARGET_COMPILE_OPTIONS) > 0);
    ASSERT(has_event_item(&env, EV_GLOBAL_COMPILE_OPTIONS, "-DLEGACY=1"));
    ASSERT(has_event_item(&env, EV_GLOBAL_COMPILE_OPTIONS, "-fPIC"));
    ASSERT(has_event_item(&env, EV_GLOBAL_COMPILE_OPTIONS, "-Wall"));
    ASSERT(has_event_item(&env, EV_TARGET_COMPILE_OPTIONS, "-DLEGACY=1"));

    env_free(&env);
    TEST_PASS();
}

TEST(cmake_minimum_required_sets_minimum_version) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(run_script(&env, "cmake_minimum_required(VERSION 3.16...3.29)\n"));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("CMAKE_MINIMUM_REQUIRED_VERSION")), nob_sv_from_cstr("3.16")));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION")), nob_sv_from_cstr("3.29")));
    ASSERT(!has_diag_cause_contains(&env, EV_DIAG_WARNING, "Unknown command"));

    env_free(&env);
    TEST_PASS();
}

TEST(cmake_policy_set_get_roundtrip) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    const char *script =
        "cmake_policy(SET CMP0077 NEW)\n"
        "cmake_policy(GET CMP0077 OUT_VAR)\n";
    ASSERT(run_script(&env, script));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("OUT_VAR")), nob_sv_from_cstr("NEW")));
    ASSERT(!has_diag_cause_contains(&env, EV_DIAG_WARNING, "Unknown command"));

    env_free(&env);
    TEST_PASS();
}

TEST(find_package_handler_module_mode) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    (void)nob_mkdir_if_not_exists("temp_pkg");
    (void)nob_mkdir_if_not_exists("temp_pkg/CMake");
    ASSERT(nob_write_entire_file("temp_pkg/CMake/FindDemoPkg.cmake",
                                 "set(DemoPkg_FOUND 1)\n"
                                 "set(DemoPkg_VERSION 9.1)\n",
                                 strlen("set(DemoPkg_FOUND 1)\nset(DemoPkg_VERSION 9.1)\n")));

    const char *script =
        "set(CMAKE_MODULE_PATH temp_pkg/CMake)\n"
        "find_package(DemoPkg MODULE REQUIRED)\n";

    ASSERT(run_script(&env, script));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("DemoPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(count_events(&env, EV_FIND_PACKAGE) > 0);

#if defined(_WIN32)
    (void)system("cmd /C if exist temp_pkg rmdir /S /Q temp_pkg");
#else
    (void)system("rm -rf temp_pkg");
#endif

    env_free(&env);
    TEST_PASS();
}

TEST(find_package_preserves_script_defined_found_state) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    (void)nob_mkdir_if_not_exists("temp_pkg2");
    (void)nob_mkdir_if_not_exists("temp_pkg2/CMake");
    ASSERT(nob_write_entire_file("temp_pkg2/CMake/FindDemoPkg2.cmake",
                                 "set(DemoPkg2_FOUND 0)\n",
                                 strlen("set(DemoPkg2_FOUND 0)\n")));

    const char *script =
        "set(CMAKE_MODULE_PATH temp_pkg2/CMake)\n"
        "find_package(DemoPkg2 MODULE QUIET)\n";

    ASSERT(run_script(&env, script));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("DemoPkg2_FOUND")), nob_sv_from_cstr("0")));

#if defined(_WIN32)
    (void)system("cmd /C if exist temp_pkg2 rmdir /S /Q temp_pkg2");
#else
    (void)system("rm -rf temp_pkg2");
#endif
    env_free(&env);
    TEST_PASS();
}

TEST(find_package_config_components_and_version) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    (void)nob_mkdir_if_not_exists("temp_pkg_cfg");
    ASSERT(nob_write_entire_file("temp_pkg_cfg/DemoCfgConfig.cmake",
                                 "if(\"${DemoCfg_FIND_COMPONENTS}\" STREQUAL \"Core;Net\")\n"
                                 "  set(DemoCfg_FOUND 1)\n"
                                 "else()\n"
                                 "  set(DemoCfg_FOUND 0)\n"
                                 "endif()\n"
                                 "set(DemoCfg_VERSION 1.2.0)\n",
                                 strlen("if(\"${DemoCfg_FIND_COMPONENTS}\" STREQUAL \"Core;Net\")\n  set(DemoCfg_FOUND 1)\nelse()\n  set(DemoCfg_FOUND 0)\nendif()\nset(DemoCfg_VERSION 1.2.0)\n")));

    const char *script_ok =
        "set(CMAKE_PREFIX_PATH temp_pkg_cfg)\n"
        "find_package(DemoCfg 1.0 CONFIG COMPONENTS Core Net QUIET)\n";
    ASSERT(run_script(&env, script_ok));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("DemoCfg_FOUND")), nob_sv_from_cstr("1")));

    const char *script_fail =
        "set(CMAKE_PREFIX_PATH temp_pkg_cfg)\n"
        "find_package(DemoCfg 2.0 EXACT CONFIG QUIET)\n";
    ASSERT(run_script(&env, script_fail));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("DemoCfg_FOUND")), nob_sv_from_cstr("0")));

#if defined(_WIN32)
    (void)system("cmd /C if exist temp_pkg_cfg rmdir /S /Q temp_pkg_cfg");
#else
    (void)system("rm -rf temp_pkg_cfg");
#endif
    env_free(&env);
    TEST_PASS();
}

TEST(find_package_config_version_file_can_reject) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    (void)nob_mkdir_if_not_exists("temp_pkg_cfgver");
    ASSERT(nob_write_entire_file("temp_pkg_cfgver/DemoVerConfig.cmake",
                                 "set(DemoVer_FOUND 1)\n"
                                 "set(DemoVer_VERSION 9.9.9)\n",
                                 strlen("set(DemoVer_FOUND 1)\nset(DemoVer_VERSION 9.9.9)\n")));
    ASSERT(nob_write_entire_file("temp_pkg_cfgver/DemoVerConfigVersion.cmake",
                                 "set(PACKAGE_VERSION 9.9.9)\n"
                                 "set(PACKAGE_VERSION_COMPATIBLE FALSE)\n"
                                 "set(PACKAGE_VERSION_EXACT FALSE)\n",
                                 strlen("set(PACKAGE_VERSION 9.9.9)\nset(PACKAGE_VERSION_COMPATIBLE FALSE)\nset(PACKAGE_VERSION_EXACT FALSE)\n")));

    const char *script =
        "set(CMAKE_PREFIX_PATH temp_pkg_cfgver)\n"
        "find_package(DemoVer 1.0 CONFIG QUIET)\n";
    ASSERT(run_script(&env, script));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("DemoVer_FOUND")), nob_sv_from_cstr("0")));

#if defined(_WIN32)
    (void)system("cmd /C if exist temp_pkg_cfgver rmdir /S /Q temp_pkg_cfgver");
#else
    (void)system("rm -rf temp_pkg_cfgver");
#endif
    env_free(&env);
    TEST_PASS();
}

TEST(set_target_properties_preserves_genex_semicolon_unquoted) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    const char *script =
        "add_executable(t main.c)\n"
        "set_target_properties(t PROPERTIES MY_PROP $<$<CONFIG:Debug>:A;B>)\n";

    ASSERT(run_script(&env, script));
    ASSERT(count_target_prop_events(&env,
                                    "t",
                                    "MY_PROP",
                                    "$<$<CONFIG:Debug>:A;B>",
                                    EV_PROP_SET) == 1);

    env_free(&env);
    TEST_PASS();
}

TEST(set_property_target_ops_emit_expected_event_op) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    const char *script =
        "add_executable(t main.c)\n"
        "set_property(TARGET t APPEND PROPERTY COMPILE_OPTIONS $<$<CONFIG:Debug>:-g>)\n"
        "set_property(TARGET t APPEND_STRING PROPERTY SUFFIX $<$<CONFIG:Debug>:_d>)\n";

    ASSERT(run_script(&env, script));
    ASSERT(count_target_prop_events(&env,
                                    "t",
                                    "COMPILE_OPTIONS",
                                    "$<$<CONFIG:Debug>:-g>",
                                    EV_PROP_APPEND_LIST) == 1);
    ASSERT(count_target_prop_events(&env,
                                    "t",
                                    "SUFFIX",
                                    "$<$<CONFIG:Debug>:_d>",
                                    EV_PROP_APPEND_STRING) == 1);

    env_free(&env);
    TEST_PASS();
}

TEST(set_property_non_target_scope_emits_warning) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(run_script(&env, "set_property(GLOBAL PROPERTY USE_FOLDERS ON)\n"));
    ASSERT(count_diag_warnings_for_command(&env, "set_property") == 1);

    env_free(&env);
    TEST_PASS();
}

void run_evaluator_dispatcher_tests(int *passed, int *failed) {
    test_dispatcher_command_handlers(passed, failed);
    test_cmake_minimum_required_sets_minimum_version(passed, failed);
    test_cmake_policy_set_get_roundtrip(passed, failed);
    test_find_package_handler_module_mode(passed, failed);
    test_find_package_preserves_script_defined_found_state(passed, failed);
    test_find_package_config_components_and_version(passed, failed);
    test_find_package_config_version_file_can_reject(passed, failed);
    test_set_target_properties_preserves_genex_semicolon_unquoted(passed, failed);
    test_set_property_target_ops_emit_expected_event_op(passed, failed);
    test_set_property_non_target_scope_emits_warning(passed, failed);
}
