#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>

// Caminhos relativos à raiz do projeto
static const char *APP_SRC = "src_v2/app/nobify.c";
static const char *APP_BIN = "build/nobify";

static void append_common_flags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "-D_GNU_SOURCE",                // Necessário para algumas funções POSIX
        "-Wall", "-Wextra", "-std=c11", // Flags de aviso e padrão C
        "-O3",                          // Build otimizado
        "-ggdb",                        // Símbolos para depuração/valgrind+gdb
        "-DHAVE_CONFIG_H",
        "-DPCRE2_CODE_UNIT_WIDTH=8",   // Configuração do PCRE2
        "-Ivendor");

    // Includes do projeto
    nob_cmd_append(cmd,
        "-Isrc_v2/arena",
        "-Isrc_v2/lexer",
        "-Isrc_v2/parser",
        "-Isrc_v2/diagnostics",
        "-Isrc_v2/transpiler",
        "-Isrc_v2/evaluator",
        "-Isrc_v2/genex",
        "-Isrc_v2/build_model");
}

static void append_evaluator_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c",
        "src_v2/parser/parser.c",
        "src_v2/diagnostics/diagnostics.c",
        "src_v2/transpiler/event_ir.c",
        "src_v2/genex/genex.c",
        "src_v2/evaluator/stb_ds_impl.c",
        "src_v2/evaluator/evaluator.c",
        "src_v2/evaluator/eval_cpack.c",
        "src_v2/evaluator/eval_cmake_path.c",
        "src_v2/evaluator/eval_custom.c",
        "src_v2/evaluator/eval_directory.c",
        "src_v2/evaluator/eval_diag.c",
        "src_v2/evaluator/eval_dispatcher.c",
        "src_v2/evaluator/eval_expr.c",
        "src_v2/evaluator/eval_file.c",
        "src_v2/evaluator/eval_file_fsops.c",
        "src_v2/evaluator/eval_file_transfer.c",
        "src_v2/evaluator/eval_file_generate_lock_archive.c",
        "src_v2/evaluator/eval_flow.c",
        "src_v2/evaluator/eval_include.c",
        "src_v2/evaluator/eval_install.c",
        "src_v2/evaluator/eval_opt_parser.c",
        "src_v2/evaluator/eval_package.c",
        "src_v2/evaluator/eval_project.c",
        "src_v2/evaluator/eval_list.c",
        "src_v2/evaluator/eval_math.c",
        "src_v2/evaluator/eval_string.c",
        "src_v2/evaluator/eval_target.c",
        "src_v2/evaluator/eval_test.c",
        "src_v2/evaluator/eval_try_compile.c",
        "src_v2/evaluator/eval_utils.c",
        "src_v2/evaluator/eval_vars.c");
}

static void append_build_model_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/build_model/build_model.c",
        "src_v2/build_model/build_model_builder.c",
        "src_v2/build_model/build_model_validate.c",
        "src_v2/build_model/build_model_freeze.c",
        "src_v2/build_model/build_model_query.c",
        "src_v2/build_model/build_logic.c");
}

// No Linux, não compilamos PCRE manualmente, apenas linkamos a lib do sistema.
static void append_linker_flags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd, "-lpcre2-posix");
    nob_cmd_append(cmd, "-lpcre2-8"); // Linka com a libpcre2 instalada via apt
    nob_cmd_append(cmd, "-lm");       // Linka math lib (geralmente útil)
}

static bool build_app(void) {
    if (!nob_mkdir_if_not_exists("build")) return false;
    if (!nob_mkdir_if_not_exists("build/v2")) return false;

    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    
    // 1. Flags de compilação
    append_common_flags(&cmd);
    
    // 2. Output e Main
    nob_cmd_append(&cmd, "-o", APP_BIN, APP_SRC);
    
    // 3. Fontes do projeto
    append_evaluator_sources(&cmd);
    append_build_model_sources(&cmd);
    
    // 4. Libs externas (Linker)
    append_linker_flags(&cmd);
    
    return nob_cmd_run_sync(cmd);
}

static bool run_valgrind(int argc, char **argv) {
    if (!build_app()) return false;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "valgrind");
    
    // Flags do Valgrind
    nob_cmd_append(&cmd, "--leak-check=full");
    nob_cmd_append(&cmd, "--show-leak-kinds=all");
    nob_cmd_append(&cmd, "--track-origins=yes");
    nob_cmd_append(&cmd, "--vgdb=yes");
    nob_cmd_append(&cmd, "--vgdb-error=0");
    // nob_cmd_append(&cmd, "-s"); // Estatísticas resumidas
    
    // O seu programa
    nob_cmd_append(&cmd, APP_BIN);

    // REPASSA OS ARGUMENTOS EXTRAS (começando do índice 2)
    // Exemplo: ./nob valgrind arg1 arg2
    // argv[0]="./nob", argv[1]="valgrind", argv[2]="arg1"...
    for (int i = 2; i < argc; ++i) {
        nob_cmd_append(&cmd, argv[i]);
    }

    return nob_cmd_run_sync(cmd);
}

static bool clean_all(void) {
    if (nob_file_exists(APP_BIN)) {
        return nob_delete_file(APP_BIN);
    }
    return true;
}

int main(int argc, char **argv) {
    const char *cmd = (argc > 1) ? argv[1] : "build";

    if (strcmp(cmd, "build") == 0) return build_app() ? 0 : 1;
    if (strcmp(cmd, "clean") == 0) return clean_all() ? 0 : 1;
    
    // Passa argc e argv para a função
    if (strcmp(cmd, "valgrind") == 0) return run_valgrind(argc, argv) ? 0 : 1;

    nob_log(NOB_INFO, "Usage: %s [build|clean|valgrind]", argv[0]);
    return 1;
}
