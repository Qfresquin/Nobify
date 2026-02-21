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
    ASSERT(has_event_item(&env, EV_GLOBAL_COMPILE_DEFINITIONS, "LEGACY=1"));
    ASSERT(has_event_item(&env, EV_GLOBAL_COMPILE_OPTIONS, "-fPIC"));
    ASSERT(has_event_item(&env, EV_GLOBAL_COMPILE_OPTIONS, "-Wall"));
    ASSERT(has_event_item(&env, EV_TARGET_COMPILE_DEFINITIONS, "LEGACY=1"));

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

int main(void) {
    int passed = 0;
    int failed = 0;

    test_bracket_arg_equals_delimiter(&passed, &failed);
    test_variable_expansion_and_if_ops(&passed, &failed);
    test_builtin_variables_present(&passed, &failed);
    test_dispatcher_command_handlers(&passed, &failed);
    test_find_package_handler_module_mode(&passed, &failed);

    nob_log(NOB_INFO, "v2 tests: passed=%d failed=%d", passed, failed);
    return failed == 0 ? 0 : 1;
}
