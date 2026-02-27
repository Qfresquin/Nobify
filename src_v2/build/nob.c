#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>

static const char *APP_SRC = "src_v2/app/nobify.c";
static const char *APP_BIN = "build/nobify";

static void append_v2_common_flags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "-Wall", "-Wextra", "-std=c11",
        "-DHAVE_CONFIG_H",
        "-DPCRE2_CODE_UNIT_WIDTH=8",
        "-DPCRE2_STATIC",
        "-Ivendor",
        "-Ivendor/pcre",
        "-Isrc_v2/arena",
        "-Isrc_v2/lexer",
        "-Isrc_v2/parser",
        "-Isrc_v2/diagnostics",
        "-Isrc_v2/transpiler",
        "-Isrc_v2/evaluator",
        "-Isrc_v2/genex",
        "-Isrc_v2/build_model",
        "-I", "src obsoleto so use de referencia/logic_model",
        "-I", "src obsoleto so use de referencia/ds_adapter");
}

static void append_v2_evaluator_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c",
        "src_v2/parser/parser.c",
        "src_v2/diagnostics/diagnostics.c",
        "src_v2/transpiler/event_ir.c",
        "src_v2/genex/genex.c",
        "src_v2/evaluator/evaluator.c",
        "src_v2/evaluator/eval_cpack.c",
        "src_v2/evaluator/eval_cmake_path.c",
        "src_v2/evaluator/eval_custom.c",
        "src_v2/evaluator/eval_directory.c",
        "src_v2/evaluator/eval_diag.c",
        "src_v2/evaluator/eval_dispatcher.c",
        "src_v2/evaluator/eval_expr.c",
        "src_v2/evaluator/eval_file.c",
        "src_v2/evaluator/eval_flow.c",
        "src_v2/evaluator/eval_include.c",
        "src_v2/evaluator/eval_install.c",
        "src_v2/evaluator/eval_opt_parser.c",
        "src_v2/evaluator/eval_package.c",
        "src_v2/evaluator/eval_project.c",
        "src_v2/evaluator/eval_stdlib.c",
        "src_v2/evaluator/eval_target.c",
        "src_v2/evaluator/eval_test.c",
        "src_v2/evaluator/eval_try_compile.c",
        "src_v2/evaluator/eval_utils.c",
        "src_v2/evaluator/eval_vars.c");
}

static void append_v2_build_model_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/build_model/build_model.c",
        "src_v2/build_model/build_model_builder.c",
        "src_v2/build_model/build_model_validate.c",
        "src_v2/build_model/build_model_freeze.c",
        "src_v2/build_model/build_model_query.c",
        "src obsoleto so use de referencia/logic_model/logic_model.c",
        "src obsoleto so use de referencia/ds_adapter/ds_adapter.c");
}

static void append_v2_pcre_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "vendor/pcre/pcre2_auto_possess.c",
        "vendor/pcre/pcre2_chkdint.c",
        "vendor/pcre/pcre2_chartables.c",
        "vendor/pcre/pcre2_compile.c",
        "vendor/pcre/pcre2_compile_cgroup.c",
        "vendor/pcre/pcre2_compile_class.c",
        "vendor/pcre/pcre2_config.c",
        "vendor/pcre/pcre2_context.c",
        "vendor/pcre/pcre2_convert.c",
        "vendor/pcre/pcre2_dfa_match.c",
        "vendor/pcre/pcre2_error.c",
        "vendor/pcre/pcre2_extuni.c",
        "vendor/pcre/pcre2_find_bracket.c",
        "vendor/pcre/pcre2_maketables.c",
        "vendor/pcre/pcre2_match.c",
        "vendor/pcre/pcre2_match_data.c",
        "vendor/pcre/pcre2_match_next.c",
        "vendor/pcre/pcre2_newline.c",
        "vendor/pcre/pcre2_ord2utf.c",
        "vendor/pcre/pcre2_pattern_info.c",
        "vendor/pcre/pcre2_script_run.c",
        "vendor/pcre/pcre2_serialize.c",
        "vendor/pcre/pcre2_string_utils.c",
        "vendor/pcre/pcre2_study.c",
        "vendor/pcre/pcre2_substitute.c",
        "vendor/pcre/pcre2_substring.c",
        "vendor/pcre/pcre2_tables.c",
        "vendor/pcre/pcre2_ucd.c",
        "vendor/pcre/pcre2_valid_utf.c",
        "vendor/pcre/pcre2_xclass.c",
        "vendor/pcre/pcre2posix.c");
}

static void append_platform_link_flags(Nob_Cmd *cmd) {
    (void)cmd;
}

static bool delete_binary_artifact(const char *base_path) {
    bool ok = true;
    if (nob_file_exists(base_path) && !nob_delete_file(base_path)) ok = false;

    const char *exe_path = nob_temp_sprintf("%s.exe", base_path);
    if (nob_file_exists(exe_path) && !nob_delete_file(exe_path)) ok = false;

    return ok;
}

static bool build_app(void) {
    if (!nob_mkdir_if_not_exists("build")) return false;
    if (!nob_mkdir_if_not_exists("build/v2")) return false;

    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", APP_BIN,
        APP_SRC);
    append_v2_evaluator_runtime_sources(&cmd);
    append_v2_build_model_runtime_sources(&cmd);
    append_v2_pcre_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool clean_all(void) {
    return delete_binary_artifact(APP_BIN);
}

int main(int argc, char **argv) {
    const char *cmd = (argc > 1) ? argv[1] : "build";
    if (strcmp(cmd, "build") == 0) return build_app() ? 0 : 1;
    if (strcmp(cmd, "clean") == 0) return clean_all() ? 0 : 1;

    nob_log(NOB_INFO, "Usage: %s [build|clean]", argv[0]);
    return 1;
}
