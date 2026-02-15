#include "nob.h"
#include "lexer.h"
#include "parser.h"
#include "transpiler.h"
#include "arena.h"
#include <string.h>

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

static Ast_Root parse_cmake(Arena *arena, const char *input) {
    Lexer l = lexer_init(sv_from_cstr(input));
    Token_List tokens = {0};
    Token t = lexer_next(&l);
    while (t.kind != TOKEN_END) {
        nob_da_append(&tokens, t);
        t = lexer_next(&l);
    }
    Ast_Root root = parse_tokens(arena, tokens);
    free(tokens.items);
    return root;
}

static void assert_legacy_v2_equal(int *passed, int *failed, const char *input, const char *input_path) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    Ast_Root root = parse_cmake(arena, input);

    String_Builder legacy_out = {0};
    String_Builder v2_out = {0};

    Transpiler_Run_Options run_opts = {
        .input_path = input_path,
        .continue_on_fatal_error = false,
    };
    Transpiler_Compat_Profile compat = {
        .kind = TRANSPILER_COMPAT_PROFILE_CMAKE_3_X,
        .cmake_version = sv_from_cstr("3.x"),
        .allow_behavior_drift = false,
    };

    transpile_datree_ex(root, &legacy_out, &run_opts);
    transpile_datree_v2(root, &v2_out, &run_opts, &compat);

    ASSERT(legacy_out.count == v2_out.count);
    ASSERT((legacy_out.count == 0) || memcmp(legacy_out.items, v2_out.items, legacy_out.count) == 0);

    nob_sb_free(legacy_out);
    nob_sb_free(v2_out);
    arena_destroy(arena);
    (*passed)++;
    (void)failed;
}

TEST(v2_diff_core_control_flow) {
    const char *input =
        "project(DiffCore)\n"
        "set(FLAG ON)\n"
        "if(FLAG)\n"
        "  add_executable(app main.c)\n"
        "endif()\n";
    assert_legacy_v2_equal(passed, failed, input, "CMakeLists.txt");
}

TEST(v2_diff_target_properties) {
    const char *input =
        "project(DiffTarget)\n"
        "add_library(mylib STATIC lib.c)\n"
        "set_target_properties(mylib PROPERTIES OUTPUT_NAME mylib_custom)\n"
        "target_compile_definitions(mylib PRIVATE LIBDEF=1)\n";
    assert_legacy_v2_equal(passed, failed, input, "CMakeLists.txt");
}

TEST(v2_diff_find_and_include) {
    const char *input =
        "project(DiffFind)\n"
        "include(GNUInstallDirs)\n"
        "find_package(ZLIB QUIET)\n"
        "add_executable(app main.c)\n";
    assert_legacy_v2_equal(passed, failed, input, "CMakeLists.txt");
}

TEST(v2_diff_file_ops) {
    const char *input =
        "project(DiffFile)\n"
        "file(WRITE out.txt \"hello\")\n"
        "file(APPEND out.txt \" world\")\n"
        "add_executable(app main.c)\n";
    assert_legacy_v2_equal(passed, failed, input, "CMakeLists.txt");
}

TEST(v2_diff_checks_and_try_compile) {
    const char *input =
        "project(DiffChecks)\n"
        "check_c_source_compiles(\"int main(void){return 0;}\" CC_OK)\n"
        "try_compile(TC_OK ${CMAKE_BINARY_DIR} probe.c)\n"
        "add_executable(app main.c)\n";
    assert_legacy_v2_equal(passed, failed, input, "CMakeLists.txt");
}

void run_transpiler_v2_diff_tests(int *passed, int *failed) {
    test_v2_diff_core_control_flow(passed, failed);
    test_v2_diff_target_properties(passed, failed);
    test_v2_diff_find_and_include(passed, failed);
    test_v2_diff_file_ops(passed, failed);
    test_v2_diff_checks_and_try_compile(passed, failed);
}

