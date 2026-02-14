#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program_name = "cmk2nob";

    // 1. Compila o programa principal
    if (argc > 1 && strcmp(argv[1], "build") == 0) {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "cc");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb", "-I.", "-Isrc/transpiler");
        nob_cmd_append(&cmd, "-o", program_name);
        nob_cmd_append(&cmd, "main.c", "lexer.c", "parser.c", "src/transpiler/transpiler.c", "arena.c", "build_model.c", "diagnostics.c",
            "src/transpiler/sys_utils.c", "src/transpiler/toolchain_driver.c", "src/transpiler/math_parser.c", "src/transpiler/genex_evaluator.c", "logic_model.c", "ds_adapter.c",
            "src/transpiler/cmake_path_utils.c", "src/transpiler/cmake_regex_utils.c", "src/transpiler/cmake_glob_utils.c", "src/transpiler/find_search_utils.c", "src/transpiler/cmake_meta_io.c", "src/transpiler/ctest_coverage_utils.c");
        
        if (!nob_cmd_run_sync(cmd)) return 1;
        return 0;
    }

    // 2. Compila e roda os testes
    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        const char *test_binary = "test_runner";
        Nob_Cmd cmd = {0};
        
        nob_log(NOB_INFO, "--- Building Test Runner ---");
        nob_cmd_append(&cmd, "cc");
        nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb", "-I.", "-Isrc/transpiler");
        nob_cmd_append(&cmd, "-o", test_binary);
        
        // Inclui arquivos de teste
        nob_cmd_append(&cmd, "test/test_main.c");
        nob_cmd_append(&cmd, "test/test_lexer.c");
        nob_cmd_append(&cmd, "test/test_parser.c");
        nob_cmd_append(&cmd, "test/test_transpiler.c");
        nob_cmd_append(&cmd, "test/test_arena.c");
        nob_cmd_append(&cmd, "test/test_build_model.c");
        nob_cmd_append(&cmd, "test/test_phase2_modules.c");
        nob_cmd_append(&cmd, "test/test_logic_model.c");
        
        // Inclui implementação dos módulos (sem main.c do programa principal)
        nob_cmd_append(&cmd, "lexer.c", "parser.c", "src/transpiler/transpiler.c", "arena.c", "build_model.c", "diagnostics.c",
            "src/transpiler/sys_utils.c", "src/transpiler/toolchain_driver.c", "src/transpiler/math_parser.c", "src/transpiler/genex_evaluator.c", "logic_model.c", "ds_adapter.c",
            "src/transpiler/cmake_path_utils.c", "src/transpiler/cmake_regex_utils.c", "src/transpiler/cmake_glob_utils.c", "src/transpiler/find_search_utils.c", "src/transpiler/cmake_meta_io.c", "src/transpiler/ctest_coverage_utils.c");
        
        if (!nob_cmd_run_sync(cmd)) return 1;

        // Roda os testes
        nob_log(NOB_INFO, "--- Running Tests ---");
        Nob_Cmd run_cmd = {0};
        nob_cmd_append(&run_cmd, nob_temp_sprintf("./%s", test_binary));
        if (!nob_cmd_run_sync(run_cmd)) return 1;
        
        return 0;
    }

    // Default: build
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-I.", "-Isrc/transpiler", "-o", program_name);
    nob_cmd_append(&cmd, "main.c", "lexer.c", "parser.c", "src/transpiler/transpiler.c", "arena.c", "build_model.c", "diagnostics.c",
        "src/transpiler/sys_utils.c", "src/transpiler/toolchain_driver.c", "src/transpiler/math_parser.c", "src/transpiler/genex_evaluator.c", "logic_model.c", "ds_adapter.c",
        "src/transpiler/cmake_path_utils.c", "src/transpiler/cmake_regex_utils.c", "src/transpiler/cmake_glob_utils.c", "src/transpiler/find_search_utils.c", "src/transpiler/cmake_meta_io.c", "src/transpiler/ctest_coverage_utils.c");
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}
