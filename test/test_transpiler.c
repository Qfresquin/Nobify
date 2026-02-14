#include "../nob.h"
#include "../lexer.h"
#include "../parser.h"
#include "../transpiler.h"
#include "../arena.h" // <--- Necessário agora
#include "../diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

// Helper para escrever arquivos temporários para testes
static void write_test_file(const char *path, const char *content) {
    if (!nob_write_entire_file(path, content, strlen(content))) {
        printf("    ! Failed to write test file: %s\n", path);
    }
}

static void remove_test_dir(const char *path) {
#if defined(_WIN32)
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static int run_shell_command_silent(const char *cmd) {
    if (!cmd) return -1;
    return system(cmd);
}

static void remove_test_tree(const char *path) {
    if (!path || !path[0]) return;
#if defined(_WIN32)
    (void)run_shell_command_silent(nob_temp_sprintf("cmd /C if exist \"%s\" rmdir /S /Q \"%s\"", path, path));
#else
    (void)run_shell_command_silent(nob_temp_sprintf("rm -rf \"%s\"", path));
#endif
}

// Macros de teste adaptadas
#define TEST(name) static void test_##name(int *passed, int *failed)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        nob_log(NOB_ERROR, " FAILED: %s (line %d): %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while(0)

#define TEST_PASS() do { \
    (*passed)++; \
} while(0)

// Helper para parsear string de entrada (Agora recebe a Arena)
static Ast_Root parse_cmake(Arena *arena, const char *input) {
    Lexer l = lexer_init(sv_from_cstr(input));
    Token_List tokens = {0};
    
    Token t = lexer_next(&l);
    while (t.kind != TOKEN_END) {
        nob_da_append(&tokens, t);
        t = lexer_next(&l);
    }
    
    // Passa a arena para o parser
    Ast_Root root = parse_tokens(arena, tokens);
    
    // Os tokens foram alocados com malloc (nob_da_append no teste), então liberamos aqui.
    // A AST fica na arena.
    free(tokens.items); 
    return root;
}

static int count_occurrences(const char *haystack, const char *needle) {
    if (!haystack || !needle || needle[0] == '\0') return 0;
    int count = 0;
    const char *p = haystack;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

// --- Testes ---

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

TEST(find_package_zlib) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input = 
        "find_package(ZLIB REQUIRED)\n"
        "add_executable(app_${ZLIB_LIBRARIES} main.c)";
    
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    
    transpile_datree(root, &sb);
    
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_z") != NULL);
    
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

TEST(configure_file_basic) {
    Arena *arena = arena_create(1024 * 1024);
    const char *in_file = "temp_configure_in.txt";
    const char *out_file = "temp_configure_out.txt";
    write_test_file(in_file, "@PROJECT_NAME@");

    const char *input =
        "project(Test)\n"
        "configure_file(temp_configure_in.txt temp_configure_out.txt @ONLY)\n"
        "file(READ temp_configure_out.txt CFG_NAME)\n"
        "add_executable(app_${CFG_NAME} main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_Test") != NULL);

    Nob_String_Builder file_out = {0};
    ASSERT(nob_read_entire_file(out_file, &file_out));
    ASSERT(strstr(file_out.items, "Test") != NULL);

    nob_sb_free(file_out);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file(in_file);
    nob_delete_file(out_file);
    TEST_PASS();
}

TEST(file_write_append_read_basic) {
    Arena *arena = arena_create(1024 * 1024);
    const char *tmp_file = "temp_file_cmd.txt";
    const char *input =
        "file(WRITE temp_file_cmd.txt hello)\n"
        "file(APPEND temp_file_cmd.txt _world)\n"
        "file(READ temp_file_cmd.txt CONTENT)\n"
        "add_executable(app_${CONTENT} main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_hello_world") != NULL);

    Nob_String_Builder file_out = {0};
    ASSERT(nob_read_entire_file(tmp_file, &file_out));
    ASSERT(strstr(file_out.items, "hello_world") != NULL);

    nob_sb_free(file_out);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file(tmp_file);
    TEST_PASS();
}

TEST(file_copy_rename_remove_download_basic) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_file_ops");
    nob_mkdir_if_not_exists("temp_file_ops");
    nob_mkdir_if_not_exists("temp_file_ops/srcdir");
    nob_mkdir_if_not_exists("temp_file_ops/srcdir/nested");
    write_test_file("temp_file_ops/src.txt", "copy_me");
    write_test_file("temp_file_ops/srcdir/nested/n1.txt", "nested_one");

    const char *input =
        "project(Test)\n"
        "file(COPY temp_file_ops/src.txt temp_file_ops/srcdir DESTINATION temp_file_ops/out)\n"
        "file(RENAME temp_file_ops/out/src.txt temp_file_ops/out/renamed.txt RESULT RENAME_RC)\n"
        "file(REMOVE temp_file_ops/out/renamed.txt)\n"
        "file(DOWNLOAD file://temp_file_ops/srcdir/nested/n1.txt temp_file_ops/out/downloaded.txt STATUS DL_STATUS LOG DL_LOG)\n"
        "add_executable(app_${RENAME_RC} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app_0") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("file") == 0);

    ASSERT(nob_file_exists("temp_file_ops/out/downloaded.txt"));
    ASSERT(!nob_file_exists("temp_file_ops/out/renamed.txt"));
    ASSERT(nob_file_exists("temp_file_ops/out/srcdir/nested/n1.txt"));

    Nob_String_Builder downloaded = {0};
    ASSERT(nob_read_entire_file("temp_file_ops/out/downloaded.txt", &downloaded));
    ASSERT(strstr(downloaded.items, "nested_one") != NULL);

    nob_sb_free(downloaded);
    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_file_ops");
    TEST_PASS();
}

TEST(file_glob_recurse_relative_and_list_directories_off) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_glob_ops");
    nob_mkdir_if_not_exists("temp_glob_ops");
    nob_mkdir_if_not_exists("temp_glob_ops/src");
    nob_mkdir_if_not_exists("temp_glob_ops/src/sub");
    write_test_file("temp_glob_ops/src/a.txt", "A");
    write_test_file("temp_glob_ops/src/sub/b.txt", "B");

    const char *input =
        "project(Test)\n"
        "file(GLOB_RECURSE GLOB_LIST RELATIVE temp_glob_ops LIST_DIRECTORIES OFF temp_glob_ops/src/*)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"GLOB_${GLOB_LIST}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "src/a.txt") != NULL);
    ASSERT(strstr(output, "src/sub/b.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("file") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_glob_ops");
    TEST_PASS();
}

TEST(make_directory_creates_requested_directories) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_make_dir");
    const char *input =
        "project(Test)\n"
        "make_directory(temp_make_dir/a temp_make_dir/b/c)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(nob_file_exists("temp_make_dir/a"));
    ASSERT(nob_file_exists("temp_make_dir/b/c"));
    ASSERT(diag_telemetry_unsupported_count_for("make_directory") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_make_dir");
    TEST_PASS();
}

TEST(remove_legacy_command_removes_list_items) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(MYLIST one;two;three;two)\n"
        "remove(MYLIST two missing)\n"
        "list(LENGTH MYLIST MYLEN)\n"
        "string(REPLACE \";\" \"_\" MYFLAT \"${MYLIST}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"LEN_${MYLEN}\" \"LIST_${MYFLAT}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DLEN_2") != NULL);
    ASSERT(strstr(output, "-DLIST_one_three") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("remove") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
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

TEST(write_file_legacy_write_and_append) {
    Arena *arena = arena_create(1024 * 1024);
    const char *tmp_file = "temp_write_file_cmd.txt";
    const char *input =
        "project(Test)\n"
        "write_file(temp_write_file_cmd.txt hello)\n"
        "write_file(temp_write_file_cmd.txt _world APPEND)\n"
        "file(READ temp_write_file_cmd.txt CONTENT)\n"
        "add_executable(app_${CONTENT} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_hello_world") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("write_file") == 0);
    ASSERT(diag_has_errors() == false);

    Nob_String_Builder file_out = {0};
    ASSERT(nob_read_entire_file(tmp_file, &file_out));
    ASSERT(strstr(file_out.items, "hello_world") != NULL);

    nob_sb_free(file_out);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file(tmp_file);
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

TEST(cpack_component_commands_materialize_manifest_and_variables) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_readme.txt", "pkg");
    const char *input =
        "project(Test)\n"
        "cpack_add_install_type(Full DISPLAY_NAME \"Full Install\")\n"
        "cpack_add_component_group(Runtime DISPLAY_NAME \"Runtime Group\" DESCRIPTION \"Runtime files\" EXPANDED)\n"
        "cpack_add_component(core DISPLAY_NAME \"Core\")\n"
        "cpack_add_component(runtime DISPLAY_NAME \"Runtime\" DESCRIPTION \"Runtime component\" GROUP Runtime REQUIRED DEPENDS core INSTALL_TYPES Full)\n"
        "install(FILES temp_pkg_readme.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"ALL_${CPACK_COMPONENTS_ALL}\" \"GRP_${CPACK_COMPONENT_RUNTIME_GROUP}\" \"REQ_${CPACK_COMPONENT_RUNTIME_REQUIRED}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cpack_components_manifest.txt") != NULL);
    ASSERT(strstr(output, "component:runtime|display=Runtime|description=Runtime component|group=Runtime|required=ON") != NULL);
    ASSERT(strstr(output, "group:Runtime|display=Runtime Group|description=Runtime files") != NULL);
    ASSERT(strstr(output, "install_type:Full|display=Full Install") != NULL);
    ASSERT(strstr(output, "-DGRP_Runtime") != NULL);
    ASSERT(strstr(output, "-DREQ_ON") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component_group") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_install_type") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_readme.txt");
    TEST_PASS();
}

TEST(cpack_archive_module_normalizes_metadata_and_generates_archive_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_archive.txt", "archive");
    const char *input =
        "project(Demo VERSION 2.5.1)\n"
        "include(CPackArchive)\n"
        "set(CPACK_PACKAGE_NAME MyPkg)\n"
        "set(CPACK_GENERATOR TGZ;ZIP)\n"
        "cpack_add_component(base)\n"
        "cpack_add_component(core DEPENDS base)\n"
        "install(FILES temp_pkg_archive.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"N_${CPACK_PACKAGE_NAME}\" "
        "\"V_${CPACK_PACKAGE_VERSION}\" "
        "\"C_${CPACK_COMPONENTS_ALL}\" "
        "\"D_${CPACK_PACKAGE_DEPENDS}\" "
        "\"AE_${CPACK_ARCHIVE_ENABLED}\" "
        "\"AX_${CPACK_ARCHIVE_FILE_EXTENSION}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DN_MyPkg") != NULL);
    ASSERT(strstr(output, "-DV_2.5.1") != NULL);
    ASSERT(strstr(output, "-DC_base;core") != NULL);
    ASSERT(strstr(output, "-DD_base") != NULL);
    ASSERT(strstr(output, "-DAE_ON") != NULL);
    ASSERT(strstr(output, "-DAX_.tar.gz") != NULL);
    ASSERT(strstr(output, "cpack_archive_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_add_component") == 0);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_archive.txt");
    TEST_PASS();
}

TEST(cpack_archive_defaults_from_project_when_unset) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Awesome VERSION 1.4.0)\n"
        "include(CPackArchive)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"N_${CPACK_PACKAGE_NAME}\" "
        "\"V_${CPACK_PACKAGE_VERSION}\" "
        "\"AE_${CPACK_ARCHIVE_ENABLED}\" "
        "\"G_${CPACK_ARCHIVE_GENERATORS}\" "
        "\"F_${CPACK_PACKAGE_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DN_Awesome") != NULL);
    ASSERT(strstr(output, "-DV_1.4.0") != NULL);
    ASSERT(strstr(output, "-DAE_ON") != NULL);
    ASSERT(strstr(output, "-DG_TGZ") != NULL);
    ASSERT(strstr(output, "-DF_Awesome-1.4.0") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cpack_deb_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_deb.txt", "deb");
    const char *input =
        "project(MyProj VERSION 3.2.1)\n"
        "include(CPackDeb)\n"
        "set(CPACK_PACKAGE_NAME My Suite)\n"
        "set(CPACK_PACKAGE_DEPENDS libssl;zlib1g)\n"
        "cpack_add_component(core)\n"
        "cpack_add_component(runtime DEPENDS core)\n"
        "install(FILES temp_pkg_deb.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_DEB_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_DEB_ENABLED}\" "
        "\"N_${CPACK_DEBIAN_PACKAGE_NAME}\" "
        "\"V_${CPACK_DEBIAN_PACKAGE_VERSION}\" "
        "\"A_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}\" "
        "\"D_${CPACK_DEBIAN_PACKAGE_DEPENDS}\" "
        "\"F_${CPACK_DEBIAN_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_my-suite") != NULL);
    ASSERT(strstr(output, "-DV_3.2.1") != NULL);
    ASSERT(strstr(output, "-DA_amd64") != NULL);
    ASSERT(strstr(output, "libssl") != NULL);
    ASSERT(strstr(output, "zlib1g") != NULL);
    ASSERT(strstr(output, ".deb") != NULL);
    ASSERT(strstr(output, "cpack_deb_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_deb.txt");
    TEST_PASS();
}

TEST(cpack_rpm_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_rpm.txt", "rpm");
    const char *input =
        "project(MyProj VERSION 4.1.0)\n"
        "include(CPackRPM)\n"
        "set(CPACK_PACKAGE_NAME Server App)\n"
        "set(CPACK_PACKAGE_DEPENDS openssl;libcurl)\n"
        "set(CPACK_RPM_PACKAGE_LICENSE MIT)\n"
        "install(FILES temp_pkg_rpm.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_RPM_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_RPM_ENABLED}\" "
        "\"N_${CPACK_RPM_PACKAGE_NAME}\" "
        "\"V_${CPACK_RPM_PACKAGE_VERSION}\" "
        "\"A_${CPACK_RPM_PACKAGE_ARCHITECTURE}\" "
        "\"L_${CPACK_RPM_PACKAGE_LICENSE}\" "
        "\"R_${CPACK_RPM_PACKAGE_REQUIRES}\" "
        "\"F_${CPACK_RPM_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_server-app") != NULL);
    ASSERT(strstr(output, "-DV_4.1.0") != NULL);
    ASSERT(strstr(output, "-DA_x86_64") != NULL);
    ASSERT(strstr(output, "-DL_MIT") != NULL);
    ASSERT(strstr(output, "openssl") != NULL);
    ASSERT(strstr(output, "libcurl") != NULL);
    ASSERT(strstr(output, ".rpm") != NULL);
    ASSERT(strstr(output, "cpack_rpm_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_rpm.txt");
    TEST_PASS();
}

TEST(cpack_nsis_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_nsis.txt", "nsis");
    const char *input =
        "project(MyProj VERSION 5.0.2)\n"
        "include(CPackNSIS)\n"
        "set(CPACK_PACKAGE_NAME Fancy App)\n"
        "set(CPACK_PACKAGE_CONTACT dev@company.test)\n"
        "set(CPACK_NSIS_DISPLAY_NAME FancyApp-5.0.2)\n"
        "set(CPACK_NSIS_PACKAGE_INSTALL_DIRECTORY FancyApp)\n"
        "install(FILES temp_pkg_nsis.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_NSIS_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_NSIS_ENABLED}\" "
        "\"D_${CPACK_NSIS_DISPLAY_NAME}\" "
        "\"I_${CPACK_NSIS_PACKAGE_INSTALL_DIRECTORY}\" "
        "\"C_${CPACK_NSIS_CONTACT}\" "
        "\"F_${CPACK_NSIS_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DD_FancyApp-5.0.2") != NULL);
    ASSERT(strstr(output, "-DI_FancyApp") != NULL);
    ASSERT(strstr(output, "dev@company.test") != NULL);
    ASSERT(strstr(output, ".exe") != NULL);
    ASSERT(strstr(output, "cpack_nsis_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_nsis.txt");
    TEST_PASS();
}

TEST(cpack_wix_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_wix.txt", "wix");
    const char *input =
        "project(MyProj VERSION 6.1.3)\n"
        "include(CPackWIX)\n"
        "set(CPACK_PACKAGE_NAME Tool Suite)\n"
        "set(CPACK_WIX_PRODUCT_NAME ToolSuite)\n"
        "set(CPACK_WIX_CULTURES pt-br;en-us)\n"
        "install(FILES temp_pkg_wix.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_WIX_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_WIX_ENABLED}\" "
        "\"N_${CPACK_WIX_PRODUCT_NAME}\" "
        "\"A_${CPACK_WIX_ARCHITECTURE}\" "
        "\"C_${CPACK_WIX_CULTURES}\" "
        "\"F_${CPACK_WIX_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_ToolSuite") != NULL);
    ASSERT(strstr(output, "-DA_x64") != NULL);
    ASSERT(strstr(output, "-DC_pt-br;en-us") != NULL);
    ASSERT(strstr(output, ".msi") != NULL);
    ASSERT(strstr(output, "cpack_wix_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_wix.txt");
    TEST_PASS();
}

TEST(cpack_dmg_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_dmg.txt", "dmg");
    const char *input =
        "project(MyProj VERSION 7.2.0)\n"
        "include(CPackDMG)\n"
        "set(CPACK_PACKAGE_NAME Mac Suite)\n"
        "set(CPACK_DMG_VOLUME_NAME MacSuite)\n"
        "set(CPACK_DMG_FORMAT UDZO)\n"
        "install(FILES temp_pkg_dmg.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_DMG_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_DMG_ENABLED}\" "
        "\"V_${CPACK_DMG_VOLUME_NAME}\" "
        "\"F_${CPACK_DMG_FORMAT}\" "
        "\"O_${CPACK_DMG_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DV_MacSuite") != NULL);
    ASSERT(strstr(output, "-DF_UDZO") != NULL);
    ASSERT(strstr(output, ".dmg") != NULL);
    ASSERT(strstr(output, "cpack_dmg_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_dmg.txt");
    TEST_PASS();
}

TEST(cpack_bundle_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_bundle.txt", "bundle");
    const char *input =
        "project(MyProj VERSION 7.3.1)\n"
        "include(CPackBundle)\n"
        "set(CPACK_BUNDLE_NAME AppBundle)\n"
        "set(CPACK_BUNDLE_ICON AppIcon.icns)\n"
        "install(FILES temp_pkg_bundle.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_BUNDLE_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_BUNDLE_ENABLED}\" "
        "\"N_${CPACK_BUNDLE_NAME}\" "
        "\"I_${CPACK_BUNDLE_ICON}\" "
        "\"F_${CPACK_BUNDLE_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_AppBundle") != NULL);
    ASSERT(strstr(output, "-DI_AppIcon.icns") != NULL);
    ASSERT(strstr(output, ".app") != NULL);
    ASSERT(strstr(output, "cpack_bundle_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_bundle.txt");
    TEST_PASS();
}

TEST(cpack_productbuild_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_productbuild.txt", "pkg");
    const char *input =
        "project(MyProj VERSION 8.0.0)\n"
        "include(CPackProductBuild)\n"
        "set(CPACK_PACKAGE_NAME Mac Installer)\n"
        "set(CPACK_PRODUCTBUILD_IDENTIFIER com.example.macinstaller)\n"
        "set(CPACK_PRODUCTBUILD_IDENTITY_NAME DeveloperID)\n"
        "install(FILES temp_pkg_productbuild.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_PRODUCTBUILD_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_PRODUCTBUILD_ENABLED}\" "
        "\"I_${CPACK_PRODUCTBUILD_IDENTIFIER}\" "
        "\"S_${CPACK_PRODUCTBUILD_IDENTITY_NAME}\" "
        "\"F_${CPACK_PRODUCTBUILD_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DI_com.example.macinstaller") != NULL);
    ASSERT(strstr(output, "-DS_DeveloperID") != NULL);
    ASSERT(strstr(output, ".pkg") != NULL);
    ASSERT(strstr(output, "cpack_productbuild_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("include") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_productbuild.txt");
    TEST_PASS();
}

TEST(cpack_ifw_module_and_configure_file_generate_manifest_and_output) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_ifw_template.txt.in", "Name=@CPACK_IFW_PACKAGE_NAME@\nTitle=@CPACK_IFW_PACKAGE_TITLE@\n");
    write_test_file("temp_pkg_ifw.txt", "ifw");
    const char *input =
        "project(MyProj VERSION 9.0.0)\n"
        "include(CPackIFW)\n"
        "include(CPackIFWConfigureFile)\n"
        "set(CPACK_IFW_PACKAGE_NAME MyIFW)\n"
        "set(CPACK_IFW_PACKAGE_TITLE MyInstaller)\n"
        "cpack_ifw_configure_file(temp_ifw_template.txt.in temp_ifw_generated.txt @ONLY)\n"
        "install(FILES temp_pkg_ifw.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_IFW_MODULE_INITIALIZED}\" "
        "\"C_${CMAKE_CPACK_IFW_CONFIGURE_FILE_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_IFW_ENABLED}\" "
        "\"N_${CPACK_IFW_PACKAGE_NAME}\" "
        "\"T_${CPACK_IFW_PACKAGE_TITLE}\" "
        "\"F_${CPACK_IFW_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DC_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_MyIFW") != NULL);
    ASSERT(strstr(output, "-DT_MyInstaller") != NULL);
    ASSERT(strstr(output, ".ifw") != NULL);
    ASSERT(strstr(output, "cpack_ifw_manifest.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("cpack_ifw_configure_file") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_ifw_template.txt.in");
    nob_delete_file("temp_ifw_generated.txt");
    nob_delete_file("temp_pkg_ifw.txt");
    TEST_PASS();
}

TEST(cpack_nuget_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_nuget.txt", "nupkg");
    const char *input =
        "project(MyProj VERSION 1.2.3)\n"
        "include(CPackNuGet)\n"
        "set(CPACK_NUGET_PACKAGE_ID My.Package)\n"
        "set(CPACK_NUGET_PACKAGE_AUTHORS Team)\n"
        "set(CPACK_NUGET_PACKAGE_DESCRIPTION CorePackage)\n"
        "install(FILES temp_pkg_nuget.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_NUGET_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_NUGET_ENABLED}\" "
        "\"I_${CPACK_NUGET_PACKAGE_ID}\" "
        "\"A_${CPACK_NUGET_PACKAGE_AUTHORS}\" "
        "\"D_${CPACK_NUGET_PACKAGE_DESCRIPTION}\" "
        "\"F_${CPACK_NUGET_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DI_My.Package") != NULL);
    ASSERT(strstr(output, "-DA_Team") != NULL);
    ASSERT(strstr(output, "-DD_CorePackage") != NULL);
    ASSERT(strstr(output, ".nupkg") != NULL);
    ASSERT(strstr(output, "cpack_nuget_manifest.txt") != NULL);
    ASSERT(diag_has_errors() == false);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_nuget.txt");
    TEST_PASS();
}

TEST(cpack_freebsd_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_freebsd.txt", "freebsd");
    const char *input =
        "project(MyProj VERSION 2.0.0)\n"
        "include(CPackFreeBSD)\n"
        "set(CPACK_FREEBSD_PACKAGE_NAME mytool)\n"
        "set(CPACK_FREEBSD_PACKAGE_ORIGIN devel/mytool)\n"
        "install(FILES temp_pkg_freebsd.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_FREEBSD_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_FREEBSD_ENABLED}\" "
        "\"N_${CPACK_FREEBSD_PACKAGE_NAME}\" "
        "\"O_${CPACK_FREEBSD_PACKAGE_ORIGIN}\" "
        "\"F_${CPACK_FREEBSD_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_mytool") != NULL);
    ASSERT(strstr(output, "-DO_devel/mytool") != NULL);
    ASSERT(strstr(output, ".pkg.txz") != NULL);
    ASSERT(strstr(output, "cpack_freebsd_manifest.txt") != NULL);
    ASSERT(diag_has_errors() == false);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_freebsd.txt");
    TEST_PASS();
}

TEST(cpack_cygwin_module_normalizes_metadata_and_generates_manifest) {
    Arena *arena = arena_create(1024 * 1024);
    write_test_file("temp_pkg_cygwin.txt", "cygwin");
    const char *input =
        "project(MyProj VERSION 3.1.4)\n"
        "include(CPackCygwin)\n"
        "set(CPACK_CYGWIN_PACKAGE_NAME mycyg)\n"
        "set(CPACK_CYGWIN_PACKAGE_DEPENDS libstdc++;zlib)\n"
        "install(FILES temp_pkg_cygwin.txt DESTINATION pkg)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE "
        "\"M_${CMAKE_CPACK_CYGWIN_MODULE_INITIALIZED}\" "
        "\"E_${CPACK_CYGWIN_ENABLED}\" "
        "\"N_${CPACK_CYGWIN_PACKAGE_NAME}\" "
        "\"D_${CPACK_CYGWIN_PACKAGE_DEPENDS}\" "
        "\"F_${CPACK_CYGWIN_FILE_NAME}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);
    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DM_ON") != NULL);
    ASSERT(strstr(output, "-DE_ON") != NULL);
    ASSERT(strstr(output, "-DN_mycyg") != NULL);
    ASSERT(strstr(output, "libstdc++") != NULL);
    ASSERT(strstr(output, ".tar.xz") != NULL);
    ASSERT(strstr(output, "cpack_cygwin_manifest.txt") != NULL);
    ASSERT(diag_has_errors() == false);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file("temp_pkg_cygwin.txt");
    TEST_PASS();
}

TEST(find_package_target_link_usage) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "find_package(ZLIB REQUIRED)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE ZLIB::ZLIB)";

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
    ASSERT(strstr(output, "Nob_Cmd cmd_app_FALSE") != NULL);
    ASSERT(diag_has_errors() == true);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(find_package_config_mode_uses_dir_and_config_vars) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(MyPkg_DIR /tmp/mypkg)\n"
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
    ASSERT(strstr(output, "-DDIR_/tmp/mypkg") != NULL);
    ASSERT(strstr(output, "MyPkgConfig.cmake") != NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(find_package_module_mode_prefers_module_resolution) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(ZLIB_DIR /tmp/zlib-config)\n"
        "find_package(ZLIB MODULE REQUIRED)\n"
        "add_executable(app_${ZLIB_LIBRARIES} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app_z") != NULL);
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
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
    const char *input =
        "project(Test)\n"
        "set(Bar_DIR /tmp/bar)\n"
        "set(Bar_AVAILABLE_COMPONENTS core;net)\n"
        "find_package(Bar CONFIG REQUIRED COMPONENTS core gui OPTIONAL_COMPONENTS net)\n"
        "add_executable(app_${Bar_FOUND}_${Bar_core_FOUND}_${Bar_gui_FOUND}_${Bar_net_FOUND} main.c)\n"
        "set(Foo_DIR /tmp/foo)\n"
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
    ASSERT(diag_has_errors() == true);
    ASSERT(diag_telemetry_unsupported_count_for("find_package") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_pkg_config_imported_target_and_vars) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "cmake_pkg_config(IMPORT zlib PREFIX ZLIBPC IMPORTED_TARGET REQUIRED VERSION 1.0)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE PkgConfig::ZLIBPC)\n"
        "target_compile_definitions(app PRIVATE "
        "\"PCF_${ZLIBPC_FOUND}\" "
        "\"PCV_${ZLIBPC_VERSION}\" "
        "\"PCL_${ZLIBPC_LIBRARIES}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DPCF_TRUE") != NULL);
    ASSERT(strstr(output, "-DPCV_1.2.13") != NULL);
    ASSERT(strstr(output, "-DPCL_z") != NULL);
#if defined(_WIN32)
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"z.lib\")") != NULL);
#else
    ASSERT(strstr(output, "nob_cmd_append(&cmd_app, \"-lz\")") != NULL);
#endif
    ASSERT(diag_has_errors() == false);
    ASSERT(diag_telemetry_unsupported_count_for("cmake_pkg_config") == 0);

    nob_sb_free(sb);
    arena_destroy(arena);
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
    transpiler_set_continue_on_fatal_error(true);
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);
    transpiler_set_continue_on_fatal_error(false);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(diag_has_errors() == false);
    ASSERT(strstr(output, "SHOULD_APPEAR=1") != NULL);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

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
    ASSERT(strstr(output, "-DM_123yy") != NULL);
    ASSERT(strstr(output, "-DX_id999") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("string") == 0);

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

TEST(check_commands_basic_family) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "check_symbol_exists(EINTR errno.h HAVE_EINTR)\n"
        "check_function_exists(printf HAVE_PRINTF)\n"
        "check_include_file(stdio.h HAVE_STDIO_H)\n"
        "check_include_files(\"sys/types.h;sys/socket.h\" HAVE_SYS_HEADERS)\n"
        "check_type_size(\"long long\" SIZEOF_LONG_LONG)\n"
        "check_c_compiler_flag(\"-Wall\" HAVE_WALL)\n"
        "check_struct_has_member(\"struct stat\" st_mtime \"sys/stat.h\" HAVE_STAT_MTIME)\n"
        "check_c_source_compiles(\"int main(void){return 0;}\" HAVE_MINIMAL_MAIN)\n"
        "check_library_exists(\"socket\" \"connect\" \"\" HAVE_LIBSOCKET)\n"
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
    ASSERT(strstr(output, "-DLIB_1") != NULL);
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

void run_transpiler_tests(int *passed, int *failed) {
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
    test_target_link_libraries(passed, failed);
    test_target_link_options(passed, failed);
    test_target_link_directories(passed, failed);
    test_set_target_properties_output_name(passed, failed);
    test_set_target_properties_prefix_suffix(passed, failed);
    test_config_release_compile_and_output_properties(passed, failed);
    test_config_debug_output_directory_property(passed, failed);
    test_conditional_target_properties_dual_read_debug(passed, failed);
    test_conditional_target_properties_dual_read_release(passed, failed);
    test_install_targets(passed, failed);
    test_install_targets_runtime_library_archive(passed, failed);
    test_install_files_programs_directories(passed, failed);
    test_target_include_directories(passed, failed);
    test_include_directories_global(passed, failed);
    test_link_directories_global(passed, failed);
    test_link_libraries_global(passed, failed);
    test_link_libraries_global_framework(passed, failed);
    test_add_compile_options_global(passed, failed);
    test_add_compile_definitions_global(passed, failed);
    test_add_definitions_global(passed, failed);
    test_remove_definitions_global(passed, failed);
    test_include_regular_expression_sets_builtin_vars(passed, failed);
    test_enable_language_sets_compiler_loaded_var(passed, failed);
    test_site_name_sets_defined_variable(passed, failed);
    test_add_link_options_global(passed, failed);
    test_set_directory_properties_global_effects(passed, failed);
    test_get_directory_property_reads_values(passed, failed);
    test_export_partial_support_targets_file_namespace(passed, failed);
    test_export_support_export_set_from_install_targets(passed, failed);
    test_export_package_registers_package_dir(passed, failed);
    test_target_compile_definitions(passed, failed);
    test_target_compile_options(passed, failed);
    test_target_compile_features_basic(passed, failed);
    test_target_precompile_headers_basic(passed, failed);
    test_set_source_files_properties_compile_props(passed, failed);
    test_get_source_file_property_reads_values(passed, failed);
    test_target_sources_private(passed, failed);
    test_target_sources_ignores_duplicates(passed, failed);
    test_transitive_compile_usage_requirements(passed, failed);
    test_transitive_link_usage_requirements(passed, failed);
    test_message_command(passed, failed);
    test_option_command(passed, failed);
    test_cmake_dependent_option_command(passed, failed);
    test_variable_interpolation(passed, failed);
    test_complex_project(passed, failed);
    test_foreach_loop(passed, failed);
    test_empty_project(passed, failed);
    test_multiline_command(passed, failed);
    test_find_package_zlib(passed, failed);
    test_find_package_target_link_usage(passed, failed);
    test_find_package_required_reports_error_when_missing(passed, failed);
    test_find_package_config_mode_uses_dir_and_config_vars(passed, failed);
    test_find_package_module_mode_prefers_module_resolution(passed, failed);
    test_find_package_exact_version_mismatch_sets_not_found(passed, failed);
    test_find_package_components_and_component_imported_target(passed, failed);
    test_cmake_pkg_config_imported_target_and_vars(passed, failed);
    test_find_program_and_find_library_basic(passed, failed);
    test_find_program_notfound_sets_notfound(passed, failed);
    test_find_file_and_find_path_basic(passed, failed);
    test_find_file_and_find_path_notfound_set_notfound(passed, failed);
    test_add_subdirectory_complex(passed, failed);
    test_subdirs_legacy_exclude_from_all(passed, failed);
    test_use_mangled_mesa_copies_headers_and_adds_include_dir(passed, failed);
    test_include_command_basic(passed, failed);
    test_configure_file_basic(passed, failed);
    test_file_write_append_read_basic(passed, failed);
    test_file_copy_rename_remove_download_basic(passed, failed);
    test_file_glob_recurse_relative_and_list_directories_off(passed, failed);
    test_make_directory_creates_requested_directories(passed, failed);
    test_remove_legacy_command_removes_list_items(passed, failed);
    test_variable_requires_legacy_sets_result_and_reports_missing_requirements(passed, failed);
    test_write_file_legacy_write_and_append(passed, failed);
    test_message_without_type(passed, failed);
    test_compat_noop_commands_are_ignored_without_unsupported(passed, failed);
    test_cpack_component_commands_materialize_manifest_and_variables(passed, failed);
    test_cpack_archive_module_normalizes_metadata_and_generates_archive_manifest(passed, failed);
    test_cpack_archive_defaults_from_project_when_unset(passed, failed);
    test_cpack_deb_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_rpm_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_nsis_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_wix_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_dmg_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_bundle_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_productbuild_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_ifw_module_and_configure_file_generate_manifest_and_output(passed, failed);
    test_cpack_nuget_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_freebsd_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_cpack_cygwin_module_normalizes_metadata_and_generates_manifest(passed, failed);
    test_message_fatal_error_stops_evaluation(passed, failed);
    test_message_fatal_error_can_continue_when_enabled(passed, failed);
    test_cmake_minimum_required_try_compile_and_get_cmake_property_supported(passed, failed);
    test_try_compile_mingw64_version_behaves_like_curl_check(passed, failed);
    test_try_compile_real_failure_sets_zero_and_output_log(passed, failed);
    test_create_test_sourcelist_generates_driver_and_list(passed, failed);
    test_try_run_sets_compile_and_run_results(passed, failed);
    test_try_run_cross_compiling_sets_failed_to_run(passed, failed);
    test_load_command_sets_loaded_variable(passed, failed);
    test_include_external_msproject_adds_utility_target_and_depends(passed, failed);
    test_qt_wrap_cpp_generates_moc_list_and_file(passed, failed);
    test_qt_wrap_ui_generates_ui_list_and_file(passed, failed);
    test_execute_process_sets_output_error_and_result_variables(passed, failed);
    test_exec_program_sets_output_and_return_value(passed, failed);
    test_fltk_wrap_ui_generates_source_list_and_file(passed, failed);
    test_codegen_includes_and_definitions(passed, failed);
    test_codegen_external_library_flags(passed, failed);
    test_codegen_sanitized_target_identifier(passed, failed);
    test_add_subdirectory_current_list_dir(passed, failed);
    test_macro_invocation_add_executable(passed, failed);
    test_macro_invocation_set_variable_param(passed, failed);
    test_function_invocation_add_executable(passed, failed);
    test_function_scope_parent_scope(passed, failed);
    test_function_local_scope_no_leak(passed, failed);
    test_add_custom_command_target_pre_build(passed, failed);
    test_add_custom_command_target_post_build_workdir(passed, failed);
    test_add_custom_command_target_depends_byproducts(passed, failed);
    test_add_custom_command_output_depends_byproducts_workdir(passed, failed);
    test_add_custom_command_output_append_depfile_and_implicit_depends(passed, failed);
    test_add_custom_command_output_command_expand_lists(passed, failed);
    test_add_custom_command_target_depends_target_resolves_output_path(passed, failed);
    test_add_custom_target_minimal(passed, failed);
    test_add_dependencies_reorders_targets(passed, failed);
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
    test_codegen_platform_toolchain_branches(passed, failed);
    test_unsupported_command_telemetry(passed, failed);
    test_list_command_family(passed, failed);
    test_separate_arguments_unix_command(passed, failed);
    test_string_command_family(passed, failed);
    test_telemetry_realloc_growth_safe(passed, failed);
    test_command_names_case_insensitive(passed, failed);
    test_mark_as_advanced_is_supported(passed, failed);
    test_include_guard_is_supported_as_noop(passed, failed);
    test_block_endblock_and_cmake_policy_are_supported_as_noop(passed, failed);
    test_package_helpers_are_supported_and_set_property_empty_is_safe(passed, failed);
    test_get_target_property_and_get_property_target(passed, failed);
    test_set_property_target_and_global(passed, failed);
    test_aux_source_directory_collects_supported_sources(passed, failed);
    test_define_property_metadata_is_visible_via_get_property(passed, failed);
    test_check_commands_basic_family(passed, failed);
    test_cmake_push_pop_check_state_restore(passed, failed);
    test_get_filename_component_modes(passed, failed);
    test_cmake_path_get_and_set_normalize(passed, failed);
    test_cmake_path_append_compare_has_is_normal_relative(passed, failed);
    test_cmake_file_api_query_generates_query_files(passed, failed);
    test_cmake_instrumentation_generates_query_and_sets_vars(passed, failed);
    test_enable_testing_sets_builtin_variable(passed, failed);
    test_add_test_name_and_legacy_signatures(passed, failed);
    test_get_test_property_basic_fields(passed, failed);
    test_set_tests_properties_then_get_test_property(passed, failed);
    test_include_ctest_initializes_module_and_enables_testing(passed, failed);
    test_include_ctest_respects_build_testing_off(passed, failed);
    test_ctest_script_mode_commands_update_expected_variables(passed, failed);
    test_include_ctest_use_launchers_sets_rule_launch_properties(passed, failed);
    test_ctest_coverage_collect_gcov_generates_cdash_bundle_metadata(passed, failed);
    test_ctest_coverage_collect_gcov_delete_removes_coverage_artifacts(passed, failed);
    test_build_command_configuration_and_target(passed, failed);
    test_include_cycle_guard(passed, failed);
    test_include_uses_default_cmake_module_path(passed, failed);
    test_include_internal_falls_back_to_cmake_root_modules(passed, failed);
    test_include_builtin_modules_are_handled_without_missing_file_warning(passed, failed);
    test_check_c_source_runs_real_probe_optional(passed, failed);
    test_check_symbol_and_compiles_real_probe_optional(passed, failed);
    test_cmake_end_to_end_compile_flags_equivalence_optional(passed, failed);
}


