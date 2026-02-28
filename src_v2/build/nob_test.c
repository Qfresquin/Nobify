#define NOB_IMPLEMENTATION
#include "nob.h"
#include "tinydir.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#if !defined(_WIN32)
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define TEMP_TESTS_ROOT "Temp_tests"
#define TEMP_TESTS_WORK TEMP_TESTS_ROOT "/work"
#define TEMP_TESTS_BIN TEMP_TESTS_ROOT "/bin"

#define TEST_ARENA_OUT TEMP_TESTS_BIN "/test_arena"
#define TEST_LEXER_OUT TEMP_TESTS_BIN "/test_lexer"
#define TEST_PARSER_OUT TEMP_TESTS_BIN "/test_parser"
#define TEST_EVALUATOR_OUT TEMP_TESTS_BIN "/test_evaluator"
#define TEST_PIPELINE_OUT TEMP_TESTS_BIN "/test_pipeline"

#define TEST_ARENA_RUN "../bin/test_arena"
#define TEST_LEXER_RUN "../bin/test_lexer"
#define TEST_PARSER_RUN "../bin/test_parser"
#define TEST_EVALUATOR_RUN "../bin/test_evaluator"
#define TEST_PIPELINE_RUN "../bin/test_pipeline"

typedef struct {
    bool exists;
    bool is_dir;
    bool is_link_like;
} Tiny_Path_Info;

typedef bool (*Test_Module_Run_Fn)(void);

typedef struct {
    const char *name;
    Test_Module_Run_Fn run;
} Test_Module;

static bool test_lexer(void);
static bool test_parser(void);
static bool test_evaluator(void);
static bool test_pipeline(void);
static bool test_arena(void);

static Test_Module TEST_MODULES[] = {
    {"arena", test_arena},
    {"lexer", test_lexer},
    {"parser", test_parser},
    {"evaluator", test_evaluator},
    {"pipeline", test_pipeline},
};

static void append_v2_common_flags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "-D_GNU_SOURCE",
        "-Wall", "-Wextra", "-std=c11",
        "-O3",
        "-ggdb",
        "-DHAVE_CONFIG_H",
        "-DPCRE2_CODE_UNIT_WIDTH=8",
        "-Ivendor");

#ifdef _WIN32
    nob_cmd_append(cmd,
        "-DPCRE2_STATIC",
        "-Ivendor/pcre");
#endif

    nob_cmd_append(cmd,
        "-Isrc_v2/arena",
        "-Isrc_v2/lexer",
        "-Isrc_v2/parser",
        "-Isrc_v2/diagnostics",
        "-Isrc_v2/transpiler",
        "-Isrc_v2/evaluator",
        "-Isrc_v2/genex",
        "-Isrc_v2/build_model",
        "-I", "src obsoleto so use de referencia/logic_model",
        "-I", "src obsoleto so use de referencia/ds_adapter",
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

static void append_v2_parser_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c",
        "src_v2/parser/parser.c",
        "src_v2/diagnostics/diagnostics.c");
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

static void append_v2_lexer_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c");
}

static void append_v2_arena_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c");
}

static void append_v2_arena_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/arena/test_arena_v2_main.c",
        "test_v2/arena/test_arena_v2_suite.c");
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
        "test_v2/evaluator/test_evaluator_v2_suite.c");
}

static void append_v2_pipeline_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/pipeline/test_pipeline_v2_main.c",
        "test_v2/pipeline/test_pipeline_v2_suite.c");
}

static void append_v2_pcre_sources(Nob_Cmd *cmd) {
#ifdef _WIN32
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
#else
    (void)cmd;
#endif
}

static void append_platform_link_flags(Nob_Cmd *cmd) {
#ifndef _WIN32
    nob_cmd_append(cmd, "-lpcre2-posix");
    nob_cmd_append(cmd, "-lpcre2-8");
#else
    (void)cmd;
#endif
}

static bool tiny_is_dot_or_dotdot(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static bool tiny_join_path(const char *lhs, const char *rhs, char out[_TINYDIR_PATH_MAX]) {
    int n = snprintf(out, _TINYDIR_PATH_MAX, "%s/%s", lhs, rhs);
    if (n < 0 || n >= _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "path too long while joining '%s' and '%s'", lhs, rhs);
        return false;
    }
    return true;
}

static bool tiny_get_path_info(const char *path, Tiny_Path_Info *out) {
    if (!path || !out) return false;
    *out = (Tiny_Path_Info){0};

#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        nob_log(NOB_ERROR, "could not stat path %s: %s", path, nob_win32_error_message(err));
        return false;
    }
    out->exists = true;
    out->is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->is_link_like = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    return true;
#else
    struct stat st = {0};
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) return true;
        nob_log(NOB_ERROR, "could not lstat path %s: %s", path, strerror(errno));
        return false;
    }
    out->exists = true;
    out->is_dir = S_ISDIR(st.st_mode);
    out->is_link_like = S_ISLNK(st.st_mode);
    return true;
#endif
}

static bool tiny_delete_file_like(const char *path) {
#if defined(_WIN32)
    if (DeleteFileA(path)) return true;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
    nob_log(NOB_ERROR, "could not delete file %s: %s", path, nob_win32_error_message(err));
    return false;
#else
    if (unlink(path) == 0) return true;
    if (errno == ENOENT) return true;
    nob_log(NOB_ERROR, "could not delete file %s: %s", path, strerror(errno));
    return false;
#endif
}

static bool tiny_delete_dir_like(const char *path) {
#if defined(_WIN32)
    if (RemoveDirectoryA(path)) return true;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
    nob_log(NOB_ERROR, "could not remove directory %s: %s", path, nob_win32_error_message(err));
    return false;
#else
    if (rmdir(path) == 0) return true;
    if (errno == ENOENT) return true;
    nob_log(NOB_ERROR, "could not remove directory %s: %s", path, strerror(errno));
    return false;
#endif
}

static bool tiny_remove_tree(const char *path) {
    Tiny_Path_Info info = {0};
    if (!tiny_get_path_info(path, &info)) return false;
    if (!info.exists) return true;

    if (info.is_link_like) {
        if (info.is_dir) return tiny_delete_dir_like(path);
        return tiny_delete_file_like(path);
    }

    if (!info.is_dir) {
        return tiny_delete_file_like(path);
    }

    tinydir_dir dir = {0};
    if (tinydir_open(&dir, path) != 0) {
        nob_log(NOB_ERROR, "could not open directory %s: %s", path, strerror(errno));
        return false;
    }

    bool ok = true;
    while (dir.has_next) {
        tinydir_file file = {0};
        if (tinydir_readfile(&dir, &file) != 0) {
            nob_log(NOB_ERROR, "could not read entry from %s: %s", path, strerror(errno));
            ok = false;
            break;
        }
        if (tinydir_next(&dir) != 0 && dir.has_next) {
            nob_log(NOB_ERROR, "could not advance directory %s: %s", path, strerror(errno));
            ok = false;
            break;
        }

        if (tiny_is_dot_or_dotdot(file.name)) continue;
        if (!tiny_remove_tree(file.path)) {
            ok = false;
            break;
        }
    }

    tinydir_close(&dir);
    if (!tiny_delete_dir_like(path)) ok = false;
    return ok;
}

static bool tiny_copy_tree(const char *src, const char *dst) {
    Tiny_Path_Info info = {0};
    if (!tiny_get_path_info(src, &info)) return false;
    if (!info.exists) {
        nob_log(NOB_ERROR, "source path does not exist: %s", src);
        return false;
    }
    if (info.is_link_like) {
        nob_log(NOB_ERROR, "refusing to copy link/reparse path: %s", src);
        return false;
    }

    if (!info.is_dir) {
        return nob_copy_file(src, dst);
    }

    if (!nob_mkdir_if_not_exists(dst)) return false;

    tinydir_dir dir = {0};
    if (tinydir_open(&dir, src) != 0) {
        nob_log(NOB_ERROR, "could not open directory %s: %s", src, strerror(errno));
        return false;
    }

    bool ok = true;
    while (dir.has_next) {
        tinydir_file file = {0};
        if (tinydir_readfile(&dir, &file) != 0) {
            nob_log(NOB_ERROR, "could not read entry from %s: %s", src, strerror(errno));
            ok = false;
            break;
        }
        if (tinydir_next(&dir) != 0 && dir.has_next) {
            nob_log(NOB_ERROR, "could not advance directory %s: %s", src, strerror(errno));
            ok = false;
            break;
        }

        if (tiny_is_dot_or_dotdot(file.name)) continue;

        char dst_child[_TINYDIR_PATH_MAX] = {0};
        if (!tiny_join_path(dst, file.name, dst_child)) {
            ok = false;
            break;
        }

        if (!tiny_copy_tree(file.path, dst_child)) {
            ok = false;
            break;
        }
    }

    tinydir_close(&dir);
    return ok;
}

static bool save_current_dir(char out[_TINYDIR_PATH_MAX]) {
    const char *cwd = nob_get_current_dir_temp();
    if (!cwd) {
        nob_log(NOB_ERROR, "could not get current directory");
        return false;
    }

    size_t n = strlen(cwd);
    if (n + 1 > _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "current directory path is too long");
        return false;
    }

    memcpy(out, cwd, n + 1);
    return true;
}

static bool prepare_temp_tests_workspace(void) {
    if (!tiny_remove_tree(TEMP_TESTS_ROOT)) return false;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_ROOT)) return false;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_WORK)) return false;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_BIN)) return false;
    if (!tiny_copy_tree("test_v2", TEMP_TESTS_WORK "/test_v2")) return false;
    return true;
}

static bool cleanup_temp_tests_workspace(void) {
    return tiny_remove_tree(TEMP_TESTS_ROOT);
}

static bool run_binary_in_workspace(const char *bin_rel_path) {
    char cwd[_TINYDIR_PATH_MAX] = {0};
    if (!save_current_dir(cwd)) return false;
    if (!nob_set_current_dir(TEMP_TESTS_WORK)) return false;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, bin_rel_path);
    bool ok = nob_cmd_run_sync(cmd);

    if (!nob_set_current_dir(cwd)) {
        nob_log(NOB_ERROR, "failed to restore current directory to %s", cwd);
        ok = false;
    }

    return ok;
}

static bool build_test_lexer(void) {
    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", TEST_LEXER_OUT);
    append_v2_lexer_test_sources(&cmd);
    append_v2_lexer_runtime_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool build_test_arena(void) {
    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", TEST_ARENA_OUT);
    append_v2_arena_test_sources(&cmd);
    append_v2_arena_runtime_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool build_test_parser(void) {
    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", TEST_PARSER_OUT);
    append_v2_parser_test_sources(&cmd);
    append_v2_parser_runtime_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool build_test_evaluator(void) {
    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", TEST_EVALUATOR_OUT);
    append_v2_evaluator_test_sources(&cmd);
    append_v2_evaluator_runtime_sources(&cmd);
    append_v2_pcre_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool build_test_pipeline(void) {
    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", TEST_PIPELINE_OUT);
    append_v2_pipeline_test_sources(&cmd);
    append_v2_evaluator_runtime_sources(&cmd);
    append_v2_build_model_runtime_sources(&cmd);
    append_v2_pcre_sources(&cmd);
    append_platform_link_flags(&cmd);
    return nob_cmd_run_sync(cmd);
}

static bool test_evaluator(void) {
    nob_log(NOB_INFO, "[v2] build+run evaluator");
    if (!build_test_evaluator()) return false;
    return run_binary_in_workspace(TEST_EVALUATOR_RUN);
}

static bool test_pipeline(void) {
    nob_log(NOB_INFO, "[v2] build+run pipeline");
    if (!build_test_pipeline()) return false;
    return run_binary_in_workspace(TEST_PIPELINE_RUN);
}

static bool test_arena(void) {
    nob_log(NOB_INFO, "[v2] build+run arena");
    if (!build_test_arena()) return false;
    return run_binary_in_workspace(TEST_ARENA_RUN);
}

static bool test_lexer(void) {
    nob_log(NOB_INFO, "[v2] build+run lexer");
    if (!build_test_lexer()) return false;
    return run_binary_in_workspace(TEST_LEXER_RUN);
}

static bool test_parser(void) {
    nob_log(NOB_INFO, "[v2] build+run parser");
    if (!build_test_parser()) return false;
    return run_binary_in_workspace(TEST_PARSER_RUN);
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

static bool run_in_temp_workspace(Test_Module_Run_Fn run_fn) {
    bool prepare_ok = prepare_temp_tests_workspace();
    bool run_ok = false;

    if (prepare_ok) {
        run_ok = run_fn();
    }

    bool cleanup_ok = cleanup_temp_tests_workspace();
    if (!cleanup_ok) {
        nob_log(NOB_ERROR, "[v2] failed to cleanup %s", TEMP_TESTS_ROOT);
    }

    return prepare_ok && run_ok && cleanup_ok;
}

int main(int argc, char **argv) {
    const char *cmd = (argc > 1) ? argv[1] : "test-v2";

    if (strcmp(cmd, "test-arena") == 0) return run_in_temp_workspace(test_arena) ? 0 : 1;
    if (strcmp(cmd, "test-lexer") == 0) return run_in_temp_workspace(test_lexer) ? 0 : 1;
    if (strcmp(cmd, "test-parser") == 0) return run_in_temp_workspace(test_parser) ? 0 : 1;
    if (strcmp(cmd, "test-evaluator") == 0) return run_in_temp_workspace(test_evaluator) ? 0 : 1;
    if (strcmp(cmd, "test-pipeline") == 0) return run_in_temp_workspace(test_pipeline) ? 0 : 1;
    if (strcmp(cmd, "test-v2") == 0) return run_in_temp_workspace(test_v2_all) ? 0 : 1;

    nob_log(NOB_INFO, "Usage: %s [test-arena|test-lexer|test-parser|test-evaluator|test-pipeline|test-v2]", argv[0]);
    return 1;
}
