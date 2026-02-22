#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>

static const char *APP_SRC = "src_v2/app/nobify.c";
static const char *APP_BIN = "build/nobify";
#define TEST_LEXER_BIN "build/v2/test_lexer"
#define TEST_PARSER_BIN "build/v2/test_parser"
#define TEST_EVALUATOR_BIN "build/v2/test_evaluator"

typedef bool (*Test_Module_Run_Fn)(void);

typedef struct {
    const char *name;
    const char *bin_path;
    Test_Module_Run_Fn run;
} Test_Module;

static bool test_lexer(void);
static bool test_parser(void);
static bool test_evaluator(void);

static Test_Module TEST_MODULES[] = {
    {"lexer", TEST_LEXER_BIN, test_lexer},
    {"parser", TEST_PARSER_BIN, test_parser},
    {"evaluator", TEST_EVALUATOR_BIN, test_evaluator},
};

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
        "-Itest_v2");
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
        "src_v2/evaluator/eval_diag.c",
        "src_v2/evaluator/eval_dispatcher.c",
        "src_v2/evaluator/eval_expr.c",
        "src_v2/evaluator/eval_file.c",
        "src_v2/evaluator/eval_flow.c",
        "src_v2/evaluator/eval_stdlib.c",
        "src_v2/evaluator/eval_utils.c",
        "src_v2/evaluator/eval_vars.c");
}

static void append_v2_parser_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c",
        "src_v2/parser/parser.c",
        "src_v2/diagnostics/diagnostics.c");
}

static void append_v2_lexer_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c");
}

static void append_v2_lexer_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/lexer/test_lexer_v2_main.c",
        "test_v2/lexer/test_lexer_v2_suite.c");
}

static void append_v2_parser_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/parser/test_parser_v2_main.c",
        "test_v2/parser/test_parser_v2_suite.c");
}

static void append_v2_evaluator_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/evaluator/test_evaluator_v2_main.c",
        "test_v2/evaluator/test_evaluator_v2_snapshot.c",
        "test_v2/evaluator/test_evaluator_v2_suite_all.c",
        "test_v2/evaluator/test_evaluator_v2_suite_golden.c");
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

static bool run_binary(const char *bin_path) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, nob_temp_sprintf("./%s", bin_path));
    return nob_cmd_run_sync(cmd);
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
        APP_SRC,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c",
        "src_v2/parser/parser.c",
        "src_v2/diagnostics/diagnostics.c");
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool build_test_lexer(void) {
    if (!nob_mkdir_if_not_exists("build")) return false;
    if (!nob_mkdir_if_not_exists("build/v2")) return false;

    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", TEST_LEXER_BIN);
    append_v2_lexer_test_sources(&cmd);
    append_v2_lexer_runtime_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool build_test_parser(void) {
    if (!nob_mkdir_if_not_exists("build")) return false;
    if (!nob_mkdir_if_not_exists("build/v2")) return false;

    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", TEST_PARSER_BIN);
    append_v2_parser_test_sources(&cmd);
    append_v2_parser_runtime_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool build_test_evaluator(void) {
    if (!nob_mkdir_if_not_exists("build")) return false;
    if (!nob_mkdir_if_not_exists("build/v2")) return false;

    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", TEST_EVALUATOR_BIN);
    append_v2_evaluator_test_sources(&cmd);
    append_v2_evaluator_runtime_sources(&cmd);
    append_v2_pcre_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool test_evaluator(void) {
    nob_log(NOB_INFO, "[v2] build+run evaluator");
    if (!build_test_evaluator()) return false;
    return run_binary(TEST_EVALUATOR_BIN);
}

static bool test_lexer(void) {
    nob_log(NOB_INFO, "[v2] build+run lexer");
    if (!build_test_lexer()) return false;
    return run_binary(TEST_LEXER_BIN);
}

static bool test_parser(void) {
    nob_log(NOB_INFO, "[v2] build+run parser");
    if (!build_test_parser()) return false;
    return run_binary(TEST_PARSER_BIN);
}

static bool test_v2_all(void) {
    size_t passed_modules = 0;
    size_t failed_modules = 0;
    size_t count = sizeof(TEST_MODULES) / sizeof(TEST_MODULES[0]);

    for (size_t i = 0; i < count; i++) {
        Test_Module module = TEST_MODULES[i];
        bool ok = module.run();
        if (ok) {
            passed_modules++;
            nob_log(NOB_INFO, "[v2] module %s: PASS", module.name);
        } else {
            failed_modules++;
            nob_log(NOB_ERROR, "[v2] module %s: FAIL", module.name);
        }
    }

    nob_log(NOB_INFO, "[v2] summary: passed_modules=%zu failed_modules=%zu", passed_modules, failed_modules);
    return failed_modules == 0;
}

static bool clean_all(void) {
    bool ok = true;
    if (!delete_binary_artifact(APP_BIN)) ok = false;
    size_t count = sizeof(TEST_MODULES) / sizeof(TEST_MODULES[0]);
    for (size_t i = 0; i < count; i++) {
        if (!delete_binary_artifact(TEST_MODULES[i].bin_path)) ok = false;
    }
    return ok;
}

int main(int argc, char **argv) {
    const char *cmd = (argc > 1) ? argv[1] : "build";
    if (strcmp(cmd, "build") == 0) return build_app() ? 0 : 1;
    if (strcmp(cmd, "test-lexer") == 0) return test_lexer() ? 0 : 1;
    if (strcmp(cmd, "test-parser") == 0) return test_parser() ? 0 : 1;
    if (strcmp(cmd, "test-evaluator") == 0) return test_evaluator() ? 0 : 1;
    if (strcmp(cmd, "test-v2") == 0) return test_v2_all() ? 0 : 1;
    if (strcmp(cmd, "clean") == 0) return clean_all() ? 0 : 1;

    nob_log(NOB_INFO, "Usage: %s [build|test-lexer|test-parser|test-evaluator|test-v2|clean]", argv[0]);
    return 1;
}
