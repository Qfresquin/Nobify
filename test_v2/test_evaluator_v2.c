#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "arena.h"
#include "arena_dyn.h"
#include "lexer.h"
#include "parser.h"
#include "evaluator.h"
#include "evaluator_internal.h"
#include "eval_expr.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TEST(name) static void test_##name(int *passed, int *failed)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        nob_log(NOB_ERROR, "FAILED: %s:%d: %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while (0)
#define TEST_PASS() do { (*passed)++; } while (0)

typedef struct {
    Arena *temp_arena;
    Arena *event_arena;
    Cmake_Event_Stream stream;
    Evaluator_Context *ctx;
} Eval_Test_Env;

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = token;
    return true;
}

static Ast_Root parse_script(Arena *arena, const char *script) {
    Lexer lx = lexer_init(nob_sv_from_cstr(script));
    Token_List toks = {0};
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &toks, t)) return (Ast_Root){0};
    }
    return parse_tokens(arena, toks);
}

static bool env_init(Eval_Test_Env *env) {
    memset(env, 0, sizeof(*env));
    env->temp_arena = arena_create(2 * 1024 * 1024);
    env->event_arena = arena_create(2 * 1024 * 1024);
    if (!env->temp_arena || !env->event_arena) return false;

    Evaluator_Init init = {0};
    init.arena = env->temp_arena;
    init.event_arena = env->event_arena;
    init.stream = &env->stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    env->ctx = evaluator_create(&init);
    return env->ctx != NULL;
}

static void env_free(Eval_Test_Env *env) {
    if (!env) return;
    if (env->event_arena) arena_destroy(env->event_arena);
    if (env->temp_arena) arena_destroy(env->temp_arena);
    memset(env, 0, sizeof(*env));
}

static bool run_script(Eval_Test_Env *env, const char *script) {
    Ast_Root ast = parse_script(env->temp_arena, script);
    return evaluator_run(env->ctx, ast);
}

static size_t count_events(const Eval_Test_Env *env, Cmake_Event_Kind kind) {
    size_t n = 0;
    for (size_t i = 0; i < env->stream.count; i++) {
        if (env->stream.items[i].kind == kind) n++;
    }
    return n;
}

static bool has_event_item(const Eval_Test_Env *env, Cmake_Event_Kind kind, const char *item) {
    String_View needle = nob_sv_from_cstr(item);
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != kind) continue;
        if (kind == EV_GLOBAL_COMPILE_DEFINITIONS && nob_sv_eq(ev->as.global_compile_definitions.item, needle)) return true;
        if (kind == EV_GLOBAL_COMPILE_OPTIONS && nob_sv_eq(ev->as.global_compile_options.item, needle)) return true;
        if (kind == EV_TARGET_COMPILE_DEFINITIONS && nob_sv_eq(ev->as.target_compile_definitions.item, needle)) return true;
        if (kind == EV_TARGET_COMPILE_OPTIONS && nob_sv_eq(ev->as.target_compile_options.item, needle)) return true;
    }
    return false;
}

static size_t count_target_prop_events(const Eval_Test_Env *env,
                                       const char *target_name,
                                       const char *key,
                                       const char *value,
                                       Cmake_Target_Property_Op op) {
    String_View tgt = nob_sv_from_cstr(target_name);
    String_View k = nob_sv_from_cstr(key);
    String_View v = nob_sv_from_cstr(value);
    size_t n = 0;
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != EV_TARGET_PROP_SET) continue;
        if (!nob_sv_eq(ev->as.target_prop_set.target_name, tgt)) continue;
        if (!nob_sv_eq(ev->as.target_prop_set.key, k)) continue;
        if (!nob_sv_eq(ev->as.target_prop_set.value, v)) continue;
        if (ev->as.target_prop_set.op != op) continue;
        n++;
    }
    return n;
}

static size_t count_diag_warnings_for_command(const Eval_Test_Env *env, const char *command) {
    String_View cmd = nob_sv_from_cstr(command);
    size_t n = 0;
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_WARNING) continue;
        if (nob_sv_eq(ev->as.diag.command, cmd)) n++;
    }
    return n;
}

static size_t count_diag_errors_for_command(const Eval_Test_Env *env, const char *command) {
    String_View cmd = nob_sv_from_cstr(command);
    size_t n = 0;
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.command, cmd)) n++;
    }
    return n;
}

static bool has_diag_cause_contains(const Eval_Test_Env *env, Cmake_Diag_Severity sev, const char *needle) {
    String_View n = nob_sv_from_cstr(needle);
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != sev) continue;
        if (n.count == 0 || ev->as.diag.cause.count < n.count) continue;
        for (size_t j = 0; j + n.count <= ev->as.diag.cause.count; j++) {
            if (memcmp(ev->as.diag.cause.data + j, n.data, n.count) == 0) return true;
        }
    }
    return false;
}

TEST(bracket_arg_equals_delimiter) {
    Arena *arena = arena_create(256 * 1024);
    ASSERT(arena != NULL);

    Ast_Root ast = parse_script(arena, "set(X [=[a;b]=])\nset(Y [==[z]==])");
    ASSERT(ast.count == 2);
    ASSERT(ast.items[0].kind == NODE_COMMAND);
    ASSERT(ast.items[0].as.cmd.args.count == 2);
    ASSERT(ast.items[0].as.cmd.args.items[1].kind == ARG_BRACKET);
    ASSERT(ast.items[1].as.cmd.args.items[1].kind == ARG_BRACKET);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(variable_expansion_and_if_ops) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(eval_var_set(env.ctx, nob_sv_from_cstr("FOO"), nob_sv_from_cstr("abc")));
    String_View expanded = eval_expand_vars(env.ctx, nob_sv_from_cstr("pre_${FOO}_post"));
    ASSERT(nob_sv_eq(expanded, nob_sv_from_cstr("pre_abc_post")));

    const char *script =
        "set(MYLIST \"b;a;c\")\n"
        "if(a IN_LIST MYLIST)\n"
        "  set(IN_LIST_OK 1)\n"
        "endif()\n"
        "if(\"a\\\\b\" PATH_EQUAL \"a/b\")\n"
        "  set(PATH_EQ_OK 1)\n"
        "endif()";

    ASSERT(run_script(&env, script));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("IN_LIST_OK")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("PATH_EQ_OK")), nob_sv_from_cstr("1")));

    env_free(&env);
    TEST_PASS();
}

TEST(builtin_variables_present) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_VERSION")));
    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_SYSTEM_NAME")));
    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME")));
    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_C_COMPILER_ID")));
    ASSERT(eval_var_defined(env.ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID")));

    env_free(&env);
    TEST_PASS();
}

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

TEST(file_read_rejects_absolute_outside_project_scope) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(!run_script(&env, "file(READ /tmp/nobify_forbidden OUT)\n"));
    ASSERT(count_diag_errors_for_command(&env, "file") >= 1);
    ASSERT(has_diag_cause_contains(&env, EV_DIAG_ERROR, "Security Violation"));

    env_free(&env);
    TEST_PASS();
}

TEST(file_strings_rejects_absolute_outside_project_scope) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(!run_script(&env, "file(STRINGS /tmp/nobify_forbidden OUT)\n"));
    ASSERT(count_diag_errors_for_command(&env, "file") >= 1);
    ASSERT(has_diag_cause_contains(&env, EV_DIAG_ERROR, "Security Violation"));

    env_free(&env);
    TEST_PASS();
}

TEST(file_read_relative_inside_project_scope_still_works) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(nob_write_entire_file("temp_read_ok.txt", "hello\n", 6));
    ASSERT(run_script(&env, "file(READ temp_read_ok.txt OUT)\n"));
    ASSERT(nob_sv_eq(eval_var_get(env.ctx, nob_sv_from_cstr("OUT")), nob_sv_from_cstr("hello\n")));

    (void)nob_delete_file("temp_read_ok.txt");
    env_free(&env);
    TEST_PASS();
}

TEST(file_copy_with_permissions_executes_without_legacy_no_effect_warning) {
    Eval_Test_Env env = {0};
    ASSERT(env_init(&env));

    ASSERT(nob_write_entire_file("temp_copy_perm_src.txt", "x", 1));
    ASSERT(run_script(&env, "file(COPY temp_copy_perm_src.txt DESTINATION temp_copy_perm_dst PERMISSIONS OWNER_READ OWNER_WRITE)\n"));
    ASSERT(nob_file_exists("temp_copy_perm_dst/temp_copy_perm_src.txt"));
    ASSERT(!has_diag_cause_contains(&env, EV_DIAG_WARNING, "currently have no effect"));

    (void)nob_delete_file("temp_copy_perm_src.txt");
    (void)nob_delete_file("temp_copy_perm_dst/temp_copy_perm_src.txt");
#if defined(_WIN32)
    (void)system("cmd /C if exist temp_copy_perm_dst rmdir /S /Q temp_copy_perm_dst");
#else
    (void)system("rm -rf temp_copy_perm_dst");
#endif
    env_free(&env);
    TEST_PASS();
}

int main(void) {
    int passed = 0;
    int failed = 0;

    test_bracket_arg_equals_delimiter(&passed, &failed);
    test_variable_expansion_and_if_ops(&passed, &failed);
    test_builtin_variables_present(&passed, &failed);
    test_dispatcher_command_handlers(&passed, &failed);
    test_cmake_minimum_required_sets_minimum_version(&passed, &failed);
    test_cmake_policy_set_get_roundtrip(&passed, &failed);
    test_find_package_handler_module_mode(&passed, &failed);
    test_find_package_preserves_script_defined_found_state(&passed, &failed);
    test_find_package_config_components_and_version(&passed, &failed);
    test_find_package_config_version_file_can_reject(&passed, &failed);
    test_set_target_properties_preserves_genex_semicolon_unquoted(&passed, &failed);
    test_set_property_target_ops_emit_expected_event_op(&passed, &failed);
    test_set_property_non_target_scope_emits_warning(&passed, &failed);
    test_file_read_rejects_absolute_outside_project_scope(&passed, &failed);
    test_file_strings_rejects_absolute_outside_project_scope(&passed, &failed);
    test_file_read_relative_inside_project_scope_still_works(&passed, &failed);
    test_file_copy_with_permissions_executes_without_legacy_no_effect_warning(&passed, &failed);

    nob_log(NOB_INFO, "v2 tests: passed=%d failed=%d", passed, failed);
    return failed == 0 ? 0 : 1;
}
