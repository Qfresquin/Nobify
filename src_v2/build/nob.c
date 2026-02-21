#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>

static const char *APP_SRC = "src_v2/app/nobify.c";
static const char *APP_BIN = "build/nobify";

static bool build_app(void) {
    if (!nob_mkdir_if_not_exists("build")) return false;
    if (!nob_mkdir_if_not_exists("build/v2")) return false;

    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    nob_cmd_append(&cmd,
        "-Wall", "-Wextra", "-std=c11",
        "-Ivendor",
        "-Isrc_v2/arena",
        "-Isrc_v2/lexer",
        "-Isrc_v2/parser",
        "-Isrc_v2/diagnostics");
    nob_cmd_append(&cmd, "-o", APP_BIN,
        APP_SRC,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c",
        "src_v2/parser/parser.c",
        "src_v2/diagnostics/diagnostics.c");
    return nob_cmd_run_sync(cmd);
}

static bool clean_app(void) {
    if (!nob_file_exists(APP_BIN)) return true;
    return nob_delete_file(APP_BIN);
}

int main(int argc, char **argv) {
    const char *cmd = (argc > 1) ? argv[1] : "build";
    if (strcmp(cmd, "build") == 0) return build_app() ? 0 : 1;
    if (strcmp(cmd, "clean") == 0) return clean_app() ? 0 : 1;

    nob_log(NOB_INFO, "Usage: %s [build|clean]", argv[0]);
    return 1;
}
