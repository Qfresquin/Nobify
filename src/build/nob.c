#if defined(_WIN32)
#if defined(_MSC_VER) && !defined(__clang__)
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "cl.exe", "/Ivendor", nob_temp_sprintf("/Fe:%s", (binary_path)), (source_path)
#elif defined(__clang__)
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "clang", "-Ivendor", "-x", "c", "-o", (binary_path), (source_path)
#else
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "gcc", "-Ivendor", "-x", "c", "-o", (binary_path), (source_path)
#endif
#else
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "cc", "-Ivendor", "-x", "c", "-o", (binary_path), (source_path)
#endif

#define NOB_IMPLEMENTATION
#include "nob.h"

static void append_common_include_flags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "-Ivendor",
        "-Isrc/arena",
        "-Isrc/lexer",
        "-Isrc/parser",
        "-Isrc/build_model",
        "-Isrc/logic_model",
        "-Isrc/diagnostics",
        "-Isrc/ds_adapter",
        "-Isrc/transpiler",
        "-Isrc/app");
}

static void append_project_sources(Nob_Cmd *cmd, bool with_app_main) {
    if (with_app_main) {
        nob_cmd_append(cmd, "src/app/main.c");
    }

    nob_cmd_append(cmd,
        "src/lexer/lexer.c",
        "src/parser/parser.c",
        "src/transpiler/transpiler.c",
        "src/arena/arena.c",
        "src/build_model/build_model.c",
        "src/diagnostics/diagnostics.c",
        "src/transpiler/sys_utils.c",
        "src/transpiler/toolchain_driver.c",
        "src/transpiler/math_parser.c",
        "src/transpiler/genex_evaluator.c",
        "src/logic_model/logic_model.c",
        "src/ds_adapter/ds_adapter.c",
        "src/transpiler/cmake_path_utils.c",
        "src/transpiler/cmake_regex_utils.c",
        "src/transpiler/cmake_glob_utils.c",
        "src/transpiler/find_search_utils.c",
        "src/transpiler/cmake_meta_io.c",
        "src/transpiler/ctest_coverage_utils.c");
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program_name = "cmk2nob";

    if (argc > 1 && strcmp(argv[1], "build") == 0) {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-ggdb");
        append_common_include_flags(&cmd);
        nob_cmd_append(&cmd, "-o", program_name);
        append_project_sources(&cmd, true);
        if (!nob_cmd_run_sync(cmd)) return 1;
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        const char *test_binary = "test_runner";
        Nob_Cmd cmd = {0};

        nob_log(NOB_INFO, "--- Building Test Runner ---");
        nob_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-ggdb");
        append_common_include_flags(&cmd);
        nob_cmd_append(&cmd, "-o", test_binary);

        nob_cmd_append(&cmd,
            "test/test_main.c",
            "test/test_lexer.c",
            "test/test_parser.c",
            "test/test_transpiler.c",
            "test/test_arena.c",
            "test/test_build_model.c",
            "test/test_phase2_modules.c",
            "test/test_logic_model.c");

        append_project_sources(&cmd, false);
        if (!nob_cmd_run_sync(cmd)) return 1;

        nob_log(NOB_INFO, "--- Running Tests ---");
        Nob_Cmd run_cmd = {0};
        nob_cmd_append(&run_cmd, nob_temp_sprintf("./%s", test_binary));
        if (!nob_cmd_run_sync(run_cmd)) return 1;

        return 0;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-Wall", "-Wextra");
    append_common_include_flags(&cmd);
    nob_cmd_append(&cmd, "-o", program_name);
    append_project_sources(&cmd, true);
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}
