#define NOB_IMPLEMENTATION
#include "nob.h"
#include "test_fs.h"
#include "test_workspace.h"

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
#if defined(__APPLE__)
#define TEST_STAT_MTIME_SEC(st) ((st).st_mtimespec.tv_sec)
#define TEST_STAT_MTIME_NSEC(st) ((st).st_mtimespec.tv_nsec)
#else
#define TEST_STAT_MTIME_SEC(st) ((st).st_mtim.tv_sec)
#define TEST_STAT_MTIME_NSEC(st) ((st).st_mtim.tv_nsec)
#endif
#endif

#define TEMP_TESTS_ROOT "Temp_tests"
#define TEMP_TESTS_RUNS TEMP_TESTS_ROOT "/runs"
#define TEMP_TESTS_BIN_ROOT TEMP_TESTS_ROOT "/bin"
#define TEMP_TESTS_OBJ_ROOT TEMP_TESTS_ROOT "/obj"
#define TEMP_TESTS_LOCKS TEMP_TESTS_ROOT "/locks"
#define TEMP_TESTS_PROBES TEMP_TESTS_ROOT "/probes"
#define TEMP_TESTS_COVERAGE TEMP_TESTS_ROOT "/coverage"

typedef void (*Append_Source_List_Fn)(Nob_Cmd *cmd);

typedef struct {
    const char *name;
    Append_Source_List_Fn append_sources;
    bool include_in_aggregate;
} Test_Module;

typedef struct {
    const char *name;
    bool use_asan;
    bool use_ubsan;
    bool use_msan;
    bool use_coverage;
    const char *asan_options_default;
    const char *ubsan_options_default;
    const char *msan_options_default;
} Test_Profile;

typedef struct {
    char root[_TINYDIR_PATH_MAX];
    char suite_copy[_TINYDIR_PATH_MAX];
} Test_Run_Workspace;

typedef struct {
    bool active;
    char session_dir[_TINYDIR_PATH_MAX];
    Nob_File_Paths binary_paths;
    Nob_File_Paths profraw_paths;
} Coverage_Context;

static bool g_runner_verbose = false;
static Coverage_Context g_coverage_ctx = {0};

static void append_test_arena_all_sources(Nob_Cmd *cmd);
static void append_test_lexer_all_sources(Nob_Cmd *cmd);
static void append_test_parser_all_sources(Nob_Cmd *cmd);
static void append_test_evaluator_all_sources(Nob_Cmd *cmd);
static void append_test_evaluator_integration_all_sources(Nob_Cmd *cmd);
static void append_test_pipeline_all_sources(Nob_Cmd *cmd);
static void append_test_codegen_all_sources(Nob_Cmd *cmd);
static void append_v2_pcre_sources(Nob_Cmd *cmd);
static void append_platform_link_flags(Nob_Cmd *cmd);
static void report_captured_test_output(const Test_Module *module,
                                        const Test_Run_Workspace *workspace,
                                        const char *stdout_path,
                                        const char *stderr_path);
static bool ensure_temp_tests_layout(const Test_Profile *profile);
static bool run_test_preflight(const Test_Profile *profile);
static const Test_Module *find_test_module(const char *name);

static const Test_Profile TEST_PROFILE_DEFAULT = {
    .name = "default",
    .use_asan = false,
    .use_ubsan = false,
    .use_msan = false,
    .use_coverage = false,
    .asan_options_default = NULL,
    .ubsan_options_default = NULL,
    .msan_options_default = NULL,
};
static const Test_Profile TEST_PROFILE_ASAN_UBSAN = {
    .name = "asan_ubsan",
    .use_asan = true,
    .use_ubsan = true,
    .use_msan = false,
    .use_coverage = false,
    .asan_options_default =
        "detect_leaks=1:detect_stack_use_after_return=1:abort_on_error=1:symbolize=1",
    .ubsan_options_default =
        "print_stacktrace=1:halt_on_error=1",
    .msan_options_default = NULL,
};
static const Test_Profile TEST_PROFILE_ASAN = {
    .name = "asan",
    .use_asan = true,
    .use_ubsan = false,
    .use_msan = false,
    .use_coverage = false,
    .asan_options_default =
        "detect_leaks=1:detect_stack_use_after_return=1:abort_on_error=1:symbolize=1",
    .ubsan_options_default = NULL,
    .msan_options_default = NULL,
};
static const Test_Profile TEST_PROFILE_UBSAN = {
    .name = "ubsan",
    .use_asan = false,
    .use_ubsan = true,
    .use_msan = false,
    .use_coverage = false,
    .asan_options_default = NULL,
    .ubsan_options_default =
        "print_stacktrace=1:halt_on_error=1",
    .msan_options_default = NULL,
};
static const Test_Profile TEST_PROFILE_MSAN = {
    .name = "msan",
    .use_asan = false,
    .use_ubsan = false,
    .use_msan = true,
    .use_coverage = false,
    .asan_options_default = NULL,
    .ubsan_options_default = NULL,
    .msan_options_default =
        "abort_on_error=1:symbolize=1:track_origins=2:poison_in_dtor=1",
};
static const Test_Profile TEST_PROFILE_COVERAGE = {
    .name = "coverage",
    .use_asan = false,
    .use_ubsan = false,
    .use_msan = false,
    .use_coverage = true,
    .asan_options_default = NULL,
    .ubsan_options_default = NULL,
    .msan_options_default = NULL,
};

static Test_Module TEST_MODULES[] = {
    {"arena", append_test_arena_all_sources, true},
    {"lexer", append_test_lexer_all_sources, true},
    {"parser", append_test_parser_all_sources, true},
    {"evaluator", append_test_evaluator_all_sources, true},
    {"evaluator-integration", append_test_evaluator_integration_all_sources, false},
    {"pipeline", append_test_pipeline_all_sources, true},
    {"codegen", append_test_codegen_all_sources, true},
};

static bool starts_with(const char *text, const char *prefix) {
    size_t prefix_len = 0;
    if (!text || !prefix) return false;
    prefix_len = strlen(prefix);
    return strncmp(text, prefix, prefix_len) == 0;
}

static bool cstr_equals(const char *a, const char *b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static bool file_paths_contains(const Nob_File_Paths *paths, const char *path) {
    if (!paths || !path) return false;
    for (size_t i = 0; i < paths->count; i++) {
        if (cstr_equals(paths->items[i], path)) return true;
    }
    return false;
}

static bool file_paths_append_unique_dup(Nob_File_Paths *paths, const char *path) {
    char *copy = NULL;
    if (!paths || !path) return false;
    if (file_paths_contains(paths, path)) return true;
    copy = strdup(path);
    if (!copy) {
        nob_log(NOB_ERROR, "failed to duplicate path %s", path);
        return false;
    }
    nob_da_append(paths, copy);
    return true;
}

static void file_paths_free_owned(Nob_File_Paths *paths) {
    if (!paths) return;
    for (size_t i = 0; i < paths->count; i++) {
        free((void*)paths->items[i]);
    }
    nob_da_free(*paths);
}

static bool test_profile_is_instrumented(const Test_Profile *profile) {
    return profile && (profile->use_asan ||
                       profile->use_ubsan ||
                       profile->use_msan ||
                       profile->use_coverage);
}

static bool test_profile_uses_clang(const Test_Profile *profile) {
    return profile && (profile->use_msan || profile->use_coverage);
}

static bool test_profile_needs_llvm_cov_tools(const Test_Profile *profile) {
    return profile && profile->use_coverage;
}

static bool path_is_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    return access(path, X_OK) == 0;
#endif
}

static bool find_executable_in_path(const char *name,
                                    char out_path[_TINYDIR_PATH_MAX]) {
    if (!name || !out_path) return false;

    if (strchr(name, '/') || strchr(name, '\\')) {
        if (!path_is_executable(name)) return false;
        if (snprintf(out_path, _TINYDIR_PATH_MAX, "%s", name) >= _TINYDIR_PATH_MAX) return false;
        return true;
    }

#if defined(_WIN32)
    {
        DWORD n = SearchPathA(NULL, name, NULL, _TINYDIR_PATH_MAX, out_path, NULL);
        return n > 0 && n < _TINYDIR_PATH_MAX;
    }
#else
    {
        const char *path_env = getenv("PATH");
        size_t temp_mark = nob_temp_save();
        if (!path_env || path_env[0] == '\0') return false;

        while (*path_env) {
            const char *sep = strchr(path_env, ':');
            size_t dir_len = sep ? (size_t)(sep - path_env) : strlen(path_env);
            const char *dir = NULL;
            const char *candidate = NULL;

            if (dir_len == 0) {
                dir = ".";
            } else {
                dir = nob_temp_strndup(path_env, dir_len);
                if (!dir) {
                    nob_temp_rewind(temp_mark);
                    return false;
                }
            }

            candidate = nob_temp_sprintf("%s/%s", dir, name);
            if (candidate && path_is_executable(candidate)) {
                if (snprintf(out_path, _TINYDIR_PATH_MAX, "%s", candidate) >= _TINYDIR_PATH_MAX) {
                    nob_temp_rewind(temp_mark);
                    return false;
                }
                nob_temp_rewind(temp_mark);
                return true;
            }

            if (!sep) break;
            path_env = sep + 1;
        }

        nob_temp_rewind(temp_mark);
        return false;
    }
#endif
}

static bool resolve_executable_from_env_or_fallbacks(const char *env_var,
                                                     const char *const *fallbacks,
                                                     size_t fallback_count,
                                                     char out_path[_TINYDIR_PATH_MAX]) {
    const char *env_value = NULL;
    if (!out_path) return false;
    out_path[0] = '\0';

    env_value = env_var ? getenv(env_var) : NULL;
    if (env_value && env_value[0] != '\0') {
        if (find_executable_in_path(env_value, out_path)) return true;
        nob_log(NOB_ERROR, "configured tool %s=%s is not executable", env_var, env_value);
        return false;
    }

    for (size_t i = 0; i < fallback_count; i++) {
        if (find_executable_in_path(fallbacks[i], out_path)) return true;
    }
    return false;
}

static bool resolve_clang_tidy_path(char out_path[_TINYDIR_PATH_MAX]) {
    static const char *const fallbacks[] = {
        "clang-tidy",
        "clang-tidy-19",
        "clang-tidy-18",
        "clang-tidy-17",
        "clang-tidy-16",
    };
    return resolve_executable_from_env_or_fallbacks("CLANG_TIDY",
                                                    fallbacks,
                                                    NOB_ARRAY_LEN(fallbacks),
                                                    out_path);
}

static bool resolve_llvm_cov_path(char out_path[_TINYDIR_PATH_MAX]) {
    static const char *const fallbacks[] = {
        "llvm-cov",
        "llvm-cov-19",
        "llvm-cov-18",
        "llvm-cov-17",
        "llvm-cov-16",
    };
    return resolve_executable_from_env_or_fallbacks("LLVM_COV",
                                                    fallbacks,
                                                    NOB_ARRAY_LEN(fallbacks),
                                                    out_path);
}

static bool resolve_llvm_profdata_path(char out_path[_TINYDIR_PATH_MAX]) {
    static const char *const fallbacks[] = {
        "llvm-profdata",
        "llvm-profdata-19",
        "llvm-profdata-18",
        "llvm-profdata-17",
        "llvm-profdata-16",
    };
    return resolve_executable_from_env_or_fallbacks("LLVM_PROFDATA",
                                                    fallbacks,
                                                    NOB_ARRAY_LEN(fallbacks),
                                                    out_path);
}

static void append_test_profile_compiler(Nob_Cmd *cmd, const Test_Profile *profile) {
    if (test_profile_uses_clang(profile)) {
        nob_cmd_append(cmd, "clang");
        return;
    }
    nob_cc(cmd);
}

static void runner_emit_log_line(Nob_Log_Level level, const char *message) {
    const char *prefix = NULL;
    FILE *stream = stderr;
    size_t len = 0;

    if (!message) return;

    switch (level) {
        case NOB_WARNING: prefix = "[WARNING] "; break;
        case NOB_ERROR: prefix = "[ERROR] "; break;
        case NOB_INFO:
        default: prefix = "[INFO] "; break;
    }

    fputs(prefix, stream);
    fputs(message, stream);
    len = strlen(message);
    if (len == 0 || message[len - 1] != '\n') fputc('\n', stream);
    fflush(stream);
}

static bool runner_should_show_info_message(const char *message) {
    if (!message) return false;
    return starts_with(message, "[v2] module ") ||
           starts_with(message, "[v2] clang-tidy") ||
           starts_with(message, "[v2] coverage ") ||
           starts_with(message, "[v2] summary:") ||
           starts_with(message, "Usage:");
}

static void runner_log_handler(Nob_Log_Level level, const char *fmt, va_list args) {
    char message[4096] = {0};
    va_list copy;
    int n = 0;

    va_copy(copy, args);
    n = vsnprintf(message, sizeof(message), fmt, copy);
    va_end(copy);
    if (n < 0) return;

    if (!g_runner_verbose) {
        if (level >= NOB_WARNING) {
            runner_emit_log_line(level, message);
            return;
        }
        if (!runner_should_show_info_message(message)) return;
    }

    runner_emit_log_line(level, message);
}

static void append_test_profile_compile_flags(Nob_Cmd *cmd, const Test_Profile *profile) {
    if (!test_profile_is_instrumented(profile)) {
        nob_cmd_append(cmd, "-O3", "-ggdb");
        return;
    }

    if (profile->use_coverage) {
        nob_cmd_append(cmd,
            "-O0",
            "-ggdb",
            "-fprofile-instr-generate",
            "-fcoverage-mapping");
        return;
    }

    nob_cmd_append(cmd, "-O1", "-ggdb", "-fno-omit-frame-pointer", "-fno-optimize-sibling-calls");
    if (profile->use_msan) {
        nob_cmd_append(cmd,
            "-fPIE",
            "-fsanitize=memory",
            "-fsanitize-memory-track-origins=2");
        return;
    }
    if (profile->use_asan && profile->use_ubsan) {
        nob_cmd_append(cmd,
            "-fsanitize=address,undefined",
            "-fsanitize-address-use-after-scope",
            "-fno-sanitize-recover=undefined");
        return;
    }
    if (profile->use_asan) {
        nob_cmd_append(cmd,
            "-fsanitize=address",
            "-fsanitize-address-use-after-scope");
        return;
    }
    if (profile->use_ubsan) {
        nob_cmd_append(cmd,
            "-fsanitize=undefined",
            "-fno-sanitize-recover=undefined");
    }
}

static void append_test_profile_link_flags(Nob_Cmd *cmd, const Test_Profile *profile) {
    if (!test_profile_is_instrumented(profile)) return;

    if (profile->use_coverage) {
        nob_cmd_append(cmd,
            "-fprofile-instr-generate",
            "-fcoverage-mapping");
        return;
    }

    nob_cmd_append(cmd, "-fno-omit-frame-pointer");
    if (profile->use_msan) {
        nob_cmd_append(cmd,
            "-pie",
            "-fsanitize=memory",
            "-fsanitize-memory-track-origins=2");
        return;
    }
    if (profile->use_asan && profile->use_ubsan) {
        nob_cmd_append(cmd, "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined");
        return;
    }
    if (profile->use_asan) {
        nob_cmd_append(cmd, "-fsanitize=address");
        return;
    }
    if (profile->use_ubsan) {
        nob_cmd_append(cmd, "-fsanitize=undefined", "-fno-sanitize-recover=undefined");
    }
}

static void append_v2_common_flags(Nob_Cmd *cmd, const Test_Profile *profile) {
    nob_cmd_append(cmd,
        "-D_GNU_SOURCE",
        "-Wall", "-Wextra", "-std=c11",
        "-Werror=unused-function",
        "-Werror=unused-variable",
        "-Werror=unused-but-set-variable",
        "-Wno-unused-parameter",
        "-Wno-unused-result",
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
        "-Isrc_v2/build_model",
        "-Isrc_v2/codegen",
        "-Isrc_v2/genex",
        "-Itest_v2");

    const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
    const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
    if (use_libcurl && strcmp(use_libcurl, "1") == 0) {
        nob_cmd_append(cmd, "-DEVAL_HAVE_LIBCURL=1");
    }
    if (use_libarchive && strcmp(use_libarchive, "1") == 0) {
        nob_cmd_append(cmd, "-DEVAL_HAVE_LIBARCHIVE=1");
    }

    append_test_profile_compile_flags(cmd, profile);
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
        "src_v2/evaluator/eval_exec_core.c",
        "src_v2/evaluator/eval_nested_exec.c",
        "src_v2/evaluator/eval_user_command.c",
        "src_v2/evaluator/evaluator.c",
        "src_v2/evaluator/eval_cpack.c",
        "src_v2/evaluator/eval_cmake_path.c",
        "src_v2/evaluator/eval_cmake_path_utils.c",
        "src_v2/evaluator/eval_custom.c",
        "src_v2/evaluator/eval_ctest.c",
        "src_v2/evaluator/eval_directory.c",
        "src_v2/evaluator/eval_diag.c",
        "src_v2/evaluator/eval_diag_classify.c",
        "src_v2/evaluator/eval_dispatcher.c",
        "src_v2/evaluator/eval_command_caps.c",
        "src_v2/evaluator/eval_expr.c",
        "src_v2/evaluator/eval_fetchcontent.c",
        "src_v2/evaluator/eval_hash.c",
        "src_v2/evaluator/eval_file.c",
        "src_v2/evaluator/eval_file_path.c",
        "src_v2/evaluator/eval_file_glob.c",
        "src_v2/evaluator/eval_file_rw.c",
        "src_v2/evaluator/eval_file_copy.c",
        "src_v2/evaluator/eval_file_extra.c",
        "src_v2/evaluator/eval_file_runtime_deps.c",
        "src_v2/evaluator/eval_file_fsops.c",
        "src_v2/evaluator/eval_file_backend_curl.c",
        "src_v2/evaluator/eval_file_backend_archive.c",
        "src_v2/evaluator/eval_file_transfer.c",
        "src_v2/evaluator/eval_file_generate_lock_archive.c",
        "src_v2/evaluator/eval_flow.c",
        "src_v2/evaluator/eval_flow_block.c",
        "src_v2/evaluator/eval_flow_cmake_language.c",
        "src_v2/evaluator/eval_flow_process.c",
        "src_v2/evaluator/eval_host.c",
        "src_v2/evaluator/eval_include.c",
        "src_v2/evaluator/eval_install.c",
        "src_v2/evaluator/eval_legacy.c",
        "src_v2/evaluator/eval_meta.c",
        "src_v2/evaluator/eval_opt_parser.c",
        "src_v2/evaluator/eval_package_find_item.c",
        "src_v2/evaluator/eval_package.c",
        "src_v2/evaluator/eval_property.c",
        "src_v2/evaluator/eval_project.c",
        "src_v2/evaluator/eval_list.c",
        "src_v2/evaluator/eval_list_helpers.c",
        "src_v2/evaluator/eval_math.c",
        "src_v2/evaluator/eval_compat.c",
        "src_v2/evaluator/eval_policy_engine.c",
        "src_v2/evaluator/eval_report.c",
        "src_v2/evaluator/eval_runtime_process.c",
        "src_v2/evaluator/eval_string_text.c",
        "src_v2/evaluator/eval_string_regex.c",
        "src_v2/evaluator/eval_string_json.c",
        "src_v2/evaluator/eval_string_misc.c",
        "src_v2/evaluator/eval_string.c",
        "src_v2/evaluator/eval_target_property_query.c",
        "src_v2/evaluator/eval_target_usage.c",
        "src_v2/evaluator/eval_target_source_group.c",
        "src_v2/evaluator/eval_target.c",
        "src_v2/evaluator/eval_test.c",
        "src_v2/evaluator/eval_try_compile.c",
        "src_v2/evaluator/eval_try_compile_parse.c",
        "src_v2/evaluator/eval_try_compile_exec.c",
        "src_v2/evaluator/eval_try_run.c",
        "src_v2/evaluator/eval_utils.c",
        "src_v2/evaluator/eval_utils_path.c",
        "src_v2/evaluator/eval_vars.c",
        "src_v2/evaluator/eval_vars_parse.c");
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

static void append_v2_arena_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c");
}

static void append_v2_arena_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_v2_assert.c",
        "test_v2/test_workspace.c",
        "test_v2/arena/test_arena_v2_main.c",
        "test_v2/arena/test_arena_v2_suite.c");
}

static void append_v2_lexer_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_v2_assert.c",
        "test_v2/test_workspace.c",
        "test_v2/lexer/test_lexer_v2_main.c",
        "test_v2/lexer/test_lexer_v2_suite.c");
}

static void append_v2_parser_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_v2_assert.c",
        "test_v2/test_workspace.c",
        "test_v2/parser/test_parser_v2_main.c",
        "test_v2/parser/test_parser_v2_suite.c");
}

static void append_v2_evaluator_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_v2_assert.c",
        "test_v2/test_workspace.c",
        "test_v2/evaluator/test_evaluator_v2_support.c",
        "test_v2/evaluator/test_evaluator_v2_main.c",
        "test_v2/evaluator/test_evaluator_v2_suite.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch1.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch2.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch3.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch4.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch5.c");
}

static void append_v2_evaluator_integration_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_v2_assert.c",
        "test_v2/test_workspace.c",
        "test_v2/evaluator/test_evaluator_v2_support.c",
        "test_v2/evaluator/test_evaluator_v2_integration_main.c",
        "test_v2/evaluator/test_evaluator_v2_suite.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch1.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch2.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch3.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch4.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch5.c");
}

static void append_v2_pipeline_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_v2_assert.c",
        "test_v2/test_workspace.c",
        "test_v2/pipeline/test_pipeline_v2_main.c",
        "test_v2/pipeline/test_pipeline_v2_suite.c");
}

static void append_v2_codegen_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_v2_assert.c",
        "test_v2/test_workspace.c",
        "test_v2/codegen/test_codegen_v2_support.c",
        "test_v2/codegen/test_codegen_v2_main.c",
        "test_v2/codegen/test_codegen_v2_suite.c",
        "test_v2/codegen/test_codegen_v2_suite_render.c",
        "test_v2/codegen/test_codegen_v2_suite_build.c",
        "test_v2/codegen/test_codegen_v2_suite_reject.c");
}

static void append_v2_build_model_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/build_model/build_model_builder.c",
        "src_v2/build_model/build_model_builder_directory.c",
        "src_v2/build_model/build_model_builder_install.c",
        "src_v2/build_model/build_model_builder_package.c",
        "src_v2/build_model/build_model_builder_project.c",
        "src_v2/build_model/build_model_builder_target.c",
        "src_v2/build_model/build_model_builder_test.c",
        "src_v2/build_model/build_model_freeze.c",
        "src_v2/build_model/build_model_query.c",
        "src_v2/build_model/build_model_validate.c",
        "src_v2/build_model/build_model_validate_cycles.c");
}

static void append_v2_codegen_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/codegen/nob_codegen.c");
}

static unsigned long test_process_id(void) {
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static bool build_abs_path(const char *cwd, const char *rel, char out[_TINYDIR_PATH_MAX]) {
    int n = 0;
    if (!cwd || !rel || !out) return false;
    n = snprintf(out, _TINYDIR_PATH_MAX, "%s/%s", cwd, rel);
    if (n < 0 || n >= _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "path too long while composing %s/%s", cwd, rel);
        return false;
    }
    return true;
}

static const char *test_profile_bin_dir_temp(const Test_Profile *profile) {
    return nob_temp_sprintf("%s/%s", TEMP_TESTS_BIN_ROOT, profile->name);
}

static const char *test_object_config_dir_temp(const Test_Profile *profile) {
    const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
    const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
    bool with_curl = use_libcurl && strcmp(use_libcurl, "1") == 0;
    bool with_archive = use_libarchive && strcmp(use_libarchive, "1") == 0;
    return nob_temp_sprintf("%s/%s/curl%d_archive%d",
                            TEMP_TESTS_OBJ_ROOT,
                            profile->name,
                            with_curl ? 1 : 0,
                            with_archive ? 1 : 0);
}

static bool ensure_dir_chain(const char *path) {
    char buffer[_TINYDIR_PATH_MAX] = {0};
    size_t len = 0;
    size_t start = 0;
    if (!path || path[0] == '\0') return true;

    len = strlen(path);
    if (len + 1 > sizeof(buffer)) {
        nob_log(NOB_ERROR, "path too long while creating directories: %s", path);
        return false;
    }

    memcpy(buffer, path, len + 1);

#if defined(_WIN32)
    if (len >= 2 && buffer[1] == ':') start = 2;
#else
    if (buffer[0] == '/') start = 1;
#endif

    for (size_t i = start + 1; i < len; ++i) {
        if (buffer[i] != '/' && buffer[i] != '\\') continue;
        buffer[i] = '\0';
        if (buffer[0] != '\0' && !nob_mkdir_if_not_exists(buffer)) return false;
        buffer[i] = '/';
    }

    return nob_mkdir_if_not_exists(buffer);
}

static bool ensure_parent_dir(const char *path) {
    bool ok = false;
    size_t temp_mark = nob_temp_save();
    const char *dir = nob_temp_dir_name(path);
    ok = ensure_dir_chain(dir);
    nob_temp_rewind(temp_mark);
    return ok;
}

static const char *test_object_path_temp(const char *source_path, const Test_Profile *profile) {
    return nob_temp_sprintf("%s/%s.o", test_object_config_dir_temp(profile), source_path);
}

static const char *test_dep_path_temp(const char *source_path, const Test_Profile *profile) {
    return nob_temp_sprintf("%s/%s.d", test_object_config_dir_temp(profile), source_path);
}

static const char *test_binary_output_path_temp(const Test_Module *module, const Test_Profile *profile) {
    return nob_temp_sprintf("%s/test_%s", test_profile_bin_dir_temp(profile), module->name);
}

static const char *test_build_lock_path_temp(const Test_Profile *profile) {
    return nob_temp_sprintf("%s/%s.lock", TEMP_TESTS_LOCKS, profile->name);
}

static bool try_create_dir_lock(const char *path, bool *created) {
#if defined(_WIN32)
    if (_mkdir(path) == 0) {
        *created = true;
        return true;
    }
#else
    if (mkdir(path, 0777) == 0) {
        *created = true;
        return true;
    }
#endif
    if (errno == EEXIST) {
        *created = false;
        return true;
    }
    nob_log(NOB_ERROR, "failed to create lock directory %s: %s", path, strerror(errno));
    return false;
}

static void sleep_millis(unsigned milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    usleep((useconds_t)milliseconds * 1000);
#endif
}

static bool acquire_build_lock(const char *lock_path) {
    for (size_t attempt = 0; attempt < 300; ++attempt) {
        bool created = false;
        if (!try_create_dir_lock(lock_path, &created)) return false;
        if (created) return true;
        sleep_millis(100);
    }
    nob_log(NOB_ERROR, "timed out waiting for build lock %s", lock_path);
    return false;
}

static bool release_build_lock(const char *lock_path) {
    if (!lock_path) return true;
    if (!test_fs_remove_tree(lock_path)) {
        nob_log(NOB_ERROR, "failed to release build lock %s", lock_path);
        return false;
    }
    return true;
}

static bool depfile_collect_inputs(const char *dep_path, Nob_File_Paths *inputs) {
    Nob_String_Builder file = {0};
    Nob_String_Builder token = {0};
    const char *cursor = NULL;
    bool ok = false;

    if (!nob_read_entire_file(dep_path, &file)) goto defer;
    nob_da_append(&file, '\0');

    cursor = file.items;
    while (*cursor && *cursor != ':') ++cursor;
    if (*cursor != ':') {
        nob_log(NOB_ERROR, "invalid depfile: %s", dep_path);
        goto defer;
    }
    ++cursor;

    while (*cursor) {
        while (*cursor) {
            if (*cursor == '\\' && cursor[1] == '\n') {
                cursor += 2;
                continue;
            }
            if (*cursor == '\\' && cursor[1] == '\r' && cursor[2] == '\n') {
                cursor += 3;
                continue;
            }
            if (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
                ++cursor;
                continue;
            }
            break;
        }

        if (*cursor == '\0') break;
        token.count = 0;

        while (*cursor) {
            if (*cursor == '\\' && cursor[1] == '\n') {
                cursor += 2;
                continue;
            }
            if (*cursor == '\\' && cursor[1] == '\r' && cursor[2] == '\n') {
                cursor += 3;
                continue;
            }
            if (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') break;
            nob_da_append(&token, *cursor);
            ++cursor;
        }

        nob_da_append(&token, '\0');
        nob_da_append(inputs, nob_temp_strdup(token.items));
    }

    ok = true;

defer:
    nob_da_free(token);
    nob_da_free(file);
    return ok;
}

static int inputs_need_rebuild(const char *output_path, const Nob_File_Paths *inputs) {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA output_attr = {0};
    ULARGE_INTEGER output_time = {0};

    if (!GetFileAttributesExA(output_path, GetFileExInfoStandard, &output_attr)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return 1;
        nob_log(NOB_ERROR, "Could not stat %s: %s", output_path, nob_win32_error_message(err));
        return -1;
    }

    output_time.LowPart = output_attr.ftLastWriteTime.dwLowDateTime;
    output_time.HighPart = output_attr.ftLastWriteTime.dwHighDateTime;

    for (size_t i = 0; i < inputs->count; ++i) {
        WIN32_FILE_ATTRIBUTE_DATA input_attr = {0};
        ULARGE_INTEGER input_time = {0};
        const char *input_path = inputs->items[i];

        if (!GetFileAttributesExA(input_path, GetFileExInfoStandard, &input_attr)) {
            nob_log(NOB_ERROR, "Could not stat %s: %s", input_path, nob_win32_error_message(GetLastError()));
            return -1;
        }

        input_time.LowPart = input_attr.ftLastWriteTime.dwLowDateTime;
        input_time.HighPart = input_attr.ftLastWriteTime.dwHighDateTime;
        if (input_time.QuadPart > output_time.QuadPart) return 1;
    }
#else
    struct stat output_stat = {0};

    if (stat(output_path, &output_stat) < 0) {
        if (errno == ENOENT) return 1;
        nob_log(NOB_ERROR, "could not stat %s: %s", output_path, strerror(errno));
        return -1;
    }

    for (size_t i = 0; i < inputs->count; ++i) {
        struct stat input_stat = {0};
        const char *input_path = inputs->items[i];

        if (stat(input_path, &input_stat) < 0) {
            nob_log(NOB_ERROR, "could not stat %s: %s", input_path, strerror(errno));
            return -1;
        }

        if (TEST_STAT_MTIME_SEC(input_stat) > TEST_STAT_MTIME_SEC(output_stat)) return 1;
        if (TEST_STAT_MTIME_SEC(input_stat) == TEST_STAT_MTIME_SEC(output_stat) &&
            TEST_STAT_MTIME_NSEC(input_stat) > TEST_STAT_MTIME_NSEC(output_stat)) {
            return 1;
        }
    }
#endif

    return 0;
}

static bool object_needs_rebuild(const char *object_path, const char *dep_path) {
    Nob_File_Paths inputs = {0};
    size_t temp_mark = nob_temp_save();
    bool ok = true;
    int rebuild = 1;

    if (!nob_file_exists(object_path) || !nob_file_exists(dep_path)) goto defer;
    if (!depfile_collect_inputs(dep_path, &inputs)) goto defer;

    nob_da_append(&inputs, __FILE__);
    rebuild = inputs_need_rebuild(object_path, &inputs);
    ok = rebuild != 0;

defer:
    nob_da_free(inputs);
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool build_object_file(const char *source_path,
                              const char *object_path,
                              const char *dep_path,
                              const Test_Profile *profile) {
    Nob_Cmd cmd = {0};
    bool ok = false;

    if (!ensure_parent_dir(object_path) || !ensure_parent_dir(dep_path)) return false;
    if (!object_needs_rebuild(object_path, dep_path)) return true;

    nob_log(NOB_INFO, "[v2] compile %s", source_path);
    append_test_profile_compiler(&cmd, profile);
    append_v2_common_flags(&cmd, profile);
    nob_cmd_append(&cmd, "-MMD", "-MF", dep_path, "-c", source_path, "-o", object_path);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool link_test_binary(const char *output_path,
                             const Nob_File_Paths *object_paths,
                             const Test_Profile *profile) {
    Nob_Cmd cmd = {0};
    Nob_File_Paths inputs = {0};
    bool ok = false;

    for (size_t i = 0; i < object_paths->count; ++i) {
        nob_da_append(&inputs, object_paths->items[i]);
    }
    nob_da_append(&inputs, __FILE__);

    if (!inputs_need_rebuild(output_path, &inputs)) {
        ok = true;
        goto defer;
    }

    nob_log(NOB_INFO, "[v2] link %s", output_path);
    append_test_profile_compiler(&cmd, profile);
    nob_cmd_append(&cmd, "-o", output_path);
    for (size_t i = 0; i < object_paths->count; ++i) {
        nob_cmd_append(&cmd, object_paths->items[i]);
    }
    append_test_profile_link_flags(&cmd, profile);
    append_platform_link_flags(&cmd);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);

defer:
    nob_da_free(inputs);
    return ok;
}

static bool build_incremental_test_binary(const char *output_path,
                                          Append_Source_List_Fn append_sources,
                                          const Test_Profile *profile) {
    Nob_Cmd sources = {0};
    Nob_File_Paths object_paths = {0};
    bool ok = false;
    size_t temp_mark = nob_temp_save();

    if (!ensure_temp_tests_layout(profile)) goto defer;

    append_sources(&sources);
    for (size_t i = 0; i < sources.count; ++i) {
        const char *source_path = sources.items[i];
        const char *object_path = test_object_path_temp(source_path, profile);
        const char *dep_path = test_dep_path_temp(source_path, profile);

        nob_da_append(&object_paths, object_path);
        if (!build_object_file(source_path, object_path, dep_path, profile)) goto defer;
    }

    ok = link_test_binary(output_path, &object_paths, profile);

defer:
    nob_da_free(object_paths);
    nob_cmd_free(sources);
    nob_temp_rewind(temp_mark);
    return ok;
}

static void append_test_arena_all_sources(Nob_Cmd *cmd) {
    append_v2_arena_test_sources(cmd);
    append_v2_arena_runtime_sources(cmd);
}

static void append_test_lexer_all_sources(Nob_Cmd *cmd) {
    append_v2_lexer_test_sources(cmd);
    append_v2_lexer_runtime_sources(cmd);
}

static void append_test_parser_all_sources(Nob_Cmd *cmd) {
    append_v2_parser_test_sources(cmd);
    append_v2_parser_runtime_sources(cmd);
}

static void append_test_evaluator_all_sources(Nob_Cmd *cmd) {
    append_v2_evaluator_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_evaluator_integration_all_sources(Nob_Cmd *cmd) {
    append_v2_evaluator_integration_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_pipeline_all_sources(Nob_Cmd *cmd) {
    append_v2_pipeline_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_codegen_all_sources(Nob_Cmd *cmd) {
    append_v2_codegen_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_codegen_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
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
    const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
    const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
    if (use_libcurl && strcmp(use_libcurl, "1") == 0) {
        nob_cmd_append(cmd, "-lcurl");
    }
    if (use_libarchive && strcmp(use_libarchive, "1") == 0) {
        nob_cmd_append(cmd, "-larchive");
    }
}

static void set_env_or_unset(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

static char *dup_env_value(const char *name) {
    const char *value = getenv(name);
    size_t len = 0;
    char *copy = NULL;

    if (!value) return NULL;
    len = strlen(value);
    copy = (char*)malloc(len + 1);
    if (!copy) {
        nob_log(NOB_ERROR, "failed to preserve environment variable %s", name);
        return NULL;
    }

    memcpy(copy, value, len + 1);
    return copy;
}

static bool preserve_env_for_restore(const char *name, char **prev_value, bool *had_prev_value) {
    if (!name || !prev_value || !had_prev_value) return false;
    *prev_value = NULL;
    *had_prev_value = getenv(name) != NULL;
    if (*had_prev_value) {
        *prev_value = dup_env_value(name);
        if (!*prev_value) return false;
    }
    return true;
}

static bool ensure_temp_tests_layout(const Test_Profile *profile) {
    bool ok = false;
    size_t temp_mark = nob_temp_save();

    if (!nob_mkdir_if_not_exists(TEMP_TESTS_ROOT)) goto defer;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_RUNS)) goto defer;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_BIN_ROOT)) goto defer;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_OBJ_ROOT)) goto defer;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_LOCKS)) goto defer;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_PROBES)) goto defer;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_COVERAGE)) goto defer;
    if (profile) {
        if (!nob_mkdir_if_not_exists(test_profile_bin_dir_temp(profile))) goto defer;
        if (!ensure_dir_chain(test_object_config_dir_temp(profile))) goto defer;
    }
    ok = true;

defer:
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool validate_test_profile_support(const Test_Profile *profile) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    const char *probe_source = NULL;
    const char *probe_binary = NULL;
    const char *probe_program = "int main(void) { return 0; }\n";
    size_t temp_mark = nob_temp_save();

    if (!test_profile_is_instrumented(profile)) {
        ok = true;
        goto defer;
    }

    probe_source = nob_temp_sprintf("%s/%s_probe.c", TEMP_TESTS_PROBES, profile->name);
    probe_binary = nob_temp_sprintf("%s/%s_probe", TEMP_TESTS_PROBES, profile->name);
    if (!ensure_parent_dir(probe_source) || !ensure_parent_dir(probe_binary)) goto defer;
    if (!nob_write_entire_file(probe_source, probe_program, strlen(probe_program))) goto defer;

    nob_log(NOB_INFO, "[v2] validate profile %s", profile->name);
    append_test_profile_compiler(&cmd, profile);
    append_test_profile_compile_flags(&cmd, profile);
    append_test_profile_link_flags(&cmd, profile);
    nob_cmd_append(&cmd, probe_source, "-o", probe_binary);
    ok = nob_cmd_run(&cmd);
    if (!ok) {
        nob_log(NOB_ERROR, "[v2] toolchain does not support profile %s", profile->name);
    }

defer:
    nob_cmd_free(cmd);
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool validate_coverage_tools_support(const Test_Profile *profile) {
    char llvm_cov[_TINYDIR_PATH_MAX] = {0};
    char llvm_profdata[_TINYDIR_PATH_MAX] = {0};
    if (!test_profile_needs_llvm_cov_tools(profile)) return true;
    if (!resolve_llvm_cov_path(llvm_cov)) {
        nob_log(NOB_ERROR, "[v2] missing llvm-cov executable; set LLVM_COV or install llvm-cov");
        return false;
    }
    if (!resolve_llvm_profdata_path(llvm_profdata)) {
        nob_log(NOB_ERROR, "[v2] missing llvm-profdata executable; set LLVM_PROFDATA or install llvm-profdata");
        return false;
    }
    nob_log(NOB_INFO, "[v2] coverage tools: llvm-cov=%s llvm-profdata=%s", llvm_cov, llvm_profdata);
    return true;
}

static bool prepare_test_run_workspace(Test_Run_Workspace *ws,
                                       const Test_Module *module,
                                       const Test_Profile *profile) {
    static unsigned long long run_serial = 0;
    const char *cwd = nob_get_current_dir_temp();
    char root_rel[_TINYDIR_PATH_MAX] = {0};
    int n = 0;

    if (!ws || !module || !profile || !cwd) return false;
    memset(ws, 0, sizeof(*ws));

    run_serial++;
    n = snprintf(root_rel, sizeof(root_rel), "%s/%s-%s-%lu-%lld-%llu",
                 TEMP_TESTS_RUNS,
                 module->name,
                 profile->name,
                 test_process_id(),
                 (long long)time(NULL),
                 run_serial);
    if (n < 0 || n >= (int)sizeof(root_rel)) {
        nob_log(NOB_ERROR, "generated run workspace path is too long");
        return false;
    }

    if (!build_abs_path(cwd, root_rel, ws->root)) return false;
    if (!test_fs_join_path(ws->root, "test_v2", ws->suite_copy)) return false;
    if (!test_fs_remove_tree(ws->root)) return false;
    if (!nob_mkdir_if_not_exists(ws->root)) return false;
    if (!nob_copy_directory_recursively("test_v2", ws->suite_copy)) return false;

    nob_log(NOB_INFO, "[v2] workspace created: %s", ws->root);
    return true;
}

static bool cleanup_test_run_workspace(const Test_Run_Workspace *ws) {
    if (!ws || ws->root[0] == '\0') return false;
    if (!test_fs_remove_tree(ws->root)) return false;
    nob_log(NOB_INFO, "[v2] workspace cleaned: %s", ws->root);
    return true;
}

static void report_captured_test_output(const Test_Module *module,
                                        const Test_Run_Workspace *workspace,
                                        const char *stdout_path,
                                        const char *stderr_path) {
    if (stderr_path) {
        Nob_String_Builder stderr_content = {0};
        if (nob_read_entire_file(stderr_path, &stderr_content) && stderr_content.count > 0) {
            fprintf(stderr, "[v2] %s captured stderr:\n", module->name);
            fwrite(stderr_content.items, 1, stderr_content.count, stderr);
            if (stderr_content.items[stderr_content.count - 1] != '\n') fputc('\n', stderr);
        }
        nob_sb_free(stderr_content);
    }

    if (stdout_path) {
        Nob_String_Builder stdout_content = {0};
        if (nob_read_entire_file(stdout_path, &stdout_content) && stdout_content.count > 0) {
            fprintf(stdout, "[v2] %s captured stdout:\n", module->name);
            fwrite(stdout_content.items, 1, stdout_content.count, stdout);
            if (stdout_content.items[stdout_content.count - 1] != '\n') fputc('\n', stdout);
        }
        nob_sb_free(stdout_content);
    }

    if (workspace && workspace->root[0] != '\0') {
        nob_log(NOB_ERROR, "[v2] preserved failed workspace: %s", workspace->root);
    }
}

static void coverage_context_reset(void) {
    file_paths_free_owned(&g_coverage_ctx.binary_paths);
    file_paths_free_owned(&g_coverage_ctx.profraw_paths);
    memset(g_coverage_ctx.session_dir, 0, sizeof(g_coverage_ctx.session_dir));
    g_coverage_ctx.active = false;
}

static bool coverage_context_begin(const char *label) {
    const char *cwd = nob_get_current_dir_temp();
    char rel_dir[_TINYDIR_PATH_MAX] = {0};
    int n = 0;

    coverage_context_reset();
    if (!cwd || !label) return false;
    if (!ensure_temp_tests_layout(&TEST_PROFILE_COVERAGE)) return false;

    n = snprintf(rel_dir,
                 sizeof(rel_dir),
                 "%s/%s-%lu-%lld",
                 TEMP_TESTS_COVERAGE,
                 label,
                 test_process_id(),
                 (long long)time(NULL));
    if (n < 0 || n >= (int)sizeof(rel_dir)) {
        nob_log(NOB_ERROR, "coverage session path is too long");
        return false;
    }

    if (!build_abs_path(cwd, rel_dir, g_coverage_ctx.session_dir)) return false;
    if (!test_fs_remove_tree(g_coverage_ctx.session_dir)) return false;
    if (!ensure_dir_chain(g_coverage_ctx.session_dir)) return false;

    g_coverage_ctx.active = true;
    nob_log(NOB_INFO, "[v2] coverage session: %s", g_coverage_ctx.session_dir);
    return true;
}

static bool coverage_context_register_module(const Test_Module *module,
                                             const char *binary_rel_path) {
    char binary_abs[_TINYDIR_PATH_MAX] = {0};
    char profraw_abs[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = NULL;
    int n = 0;

    if (!g_coverage_ctx.active || !module || !binary_rel_path) return true;
    cwd = nob_get_current_dir_temp();
    if (!cwd) return false;
    if (!build_abs_path(cwd, binary_rel_path, binary_abs)) return false;
    n = snprintf(profraw_abs,
                 sizeof(profraw_abs),
                 "%s/%s.profraw",
                 g_coverage_ctx.session_dir,
                 module->name);
    if (n < 0 || n >= (int)sizeof(profraw_abs)) {
        nob_log(NOB_ERROR, "coverage raw profile path is too long for %s", module->name);
        return false;
    }

    if (!file_paths_append_unique_dup(&g_coverage_ctx.binary_paths, binary_abs)) return false;
    if (!file_paths_append_unique_dup(&g_coverage_ctx.profraw_paths, profraw_abs)) return false;
    return true;
}

static const char *coverage_profraw_path_for_module_temp(const Test_Module *module) {
    if (!g_coverage_ctx.active || !module) return NULL;
    return nob_temp_sprintf("%s/%s.profraw", g_coverage_ctx.session_dir, module->name);
}

static bool run_command_capture_stdout(Nob_Cmd *cmd, const char *stdout_path) {
    if (!cmd || !stdout_path) return false;
    return nob_cmd_run(cmd, .stdout_path = stdout_path);
}

static bool generate_coverage_report(const Test_Module *module, bool run_all) {
    char llvm_cov[_TINYDIR_PATH_MAX] = {0};
    char llvm_profdata[_TINYDIR_PATH_MAX] = {0};
    char profdata_path[_TINYDIR_PATH_MAX] = {0};
    char summary_path[_TINYDIR_PATH_MAX] = {0};
    char html_dir[_TINYDIR_PATH_MAX] = {0};
    char ignore_regex[] = "^(test_v2/|vendor/|/usr/)";
    Nob_Cmd merge_cmd = {0};
    Nob_Cmd report_cmd = {0};
    Nob_Cmd show_cmd = {0};
    bool ok = false;

    if (!g_coverage_ctx.active) return true;
    if (g_coverage_ctx.binary_paths.count == 0 || g_coverage_ctx.profraw_paths.count == 0) {
        nob_log(NOB_ERROR, "[v2] no coverage artifacts were collected");
        goto defer;
    }
    if (!resolve_llvm_cov_path(llvm_cov) || !resolve_llvm_profdata_path(llvm_profdata)) goto defer;

    if (snprintf(profdata_path, sizeof(profdata_path), "%s/coverage.profdata", g_coverage_ctx.session_dir) >= (int)sizeof(profdata_path)) {
        nob_log(NOB_ERROR, "coverage profdata path is too long");
        goto defer;
    }
    if (snprintf(summary_path, sizeof(summary_path), "%s/summary.txt", g_coverage_ctx.session_dir) >= (int)sizeof(summary_path)) {
        nob_log(NOB_ERROR, "coverage summary path is too long");
        goto defer;
    }
    if (snprintf(html_dir, sizeof(html_dir), "%s/html", g_coverage_ctx.session_dir) >= (int)sizeof(html_dir)) {
        nob_log(NOB_ERROR, "coverage html path is too long");
        goto defer;
    }

    nob_cmd_append(&merge_cmd, llvm_profdata, "merge", "-sparse");
    for (size_t i = 0; i < g_coverage_ctx.profraw_paths.count; i++) {
        if (!nob_file_exists(g_coverage_ctx.profraw_paths.items[i])) continue;
        nob_cmd_append(&merge_cmd, g_coverage_ctx.profraw_paths.items[i]);
    }
    nob_cmd_append(&merge_cmd, "-o", profdata_path);
    if (!nob_cmd_run(&merge_cmd)) goto defer;

    nob_cmd_append(&report_cmd,
                   llvm_cov,
                   "report",
                   g_coverage_ctx.binary_paths.items[0],
                   "-instr-profile",
                   profdata_path,
                   "-ignore-filename-regex",
                   ignore_regex);
    for (size_t i = 1; i < g_coverage_ctx.binary_paths.count; i++) {
        nob_cmd_append(&report_cmd, "-object", g_coverage_ctx.binary_paths.items[i]);
    }
    if (!run_command_capture_stdout(&report_cmd, summary_path)) goto defer;

    if (!ensure_dir_chain(html_dir)) goto defer;
    nob_cmd_append(&show_cmd,
                   llvm_cov,
                   "show",
                   g_coverage_ctx.binary_paths.items[0],
                   "-format",
                   "html",
                   "-output-dir",
                   html_dir,
                   "-instr-profile",
                   profdata_path,
                   "-ignore-filename-regex",
                   ignore_regex);
    for (size_t i = 1; i < g_coverage_ctx.binary_paths.count; i++) {
        nob_cmd_append(&show_cmd, "-object", g_coverage_ctx.binary_paths.items[i]);
    }
    if (!nob_cmd_run(&show_cmd)) goto defer;

    nob_log(NOB_INFO,
            "[v2] coverage report generated for %s at %s (summary: %s, html: %s)",
            run_all ? "test-v2" : module->name,
            g_coverage_ctx.session_dir,
            summary_path,
            html_dir);
    ok = true;

defer:
    nob_cmd_free(merge_cmd);
    nob_cmd_free(report_cmd);
    nob_cmd_free(show_cmd);
    return ok;
}

static bool run_binary_in_workspace(const Test_Module *module,
                                    const Test_Profile *profile,
                                    const char *binary_rel_path,
                                    bool verbose) {
    char cwd[_TINYDIR_PATH_MAX] = {0};
    char binary_abs[_TINYDIR_PATH_MAX] = {0};
    char stdout_log_abs[_TINYDIR_PATH_MAX] = {0};
    char stderr_log_abs[_TINYDIR_PATH_MAX] = {0};
    char *prev_runner = NULL;
    char *prev_reuse_cwd = NULL;
    char *prev_repo_root = NULL;
    char *prev_asan_options = NULL;
    char *prev_ubsan_options = NULL;
    char *prev_msan_options = NULL;
    char *prev_llvm_profile_file = NULL;
    bool had_prev_runner = false;
    bool had_prev_reuse_cwd = false;
    bool had_prev_repo_root = false;
    bool had_prev_asan_options = false;
    bool had_prev_ubsan_options = false;
    bool had_prev_msan_options = false;
    bool had_prev_llvm_profile_file = false;
    Test_Run_Workspace workspace = {0};
    Nob_Cmd cmd = {0};
    bool ok = false;
    bool cleanup_ok = true;
    bool workspace_preserved = false;

    if (!test_fs_save_current_dir(cwd)) goto defer;
    if (!build_abs_path(cwd, binary_rel_path, binary_abs)) goto defer;
    if (!prepare_test_run_workspace(&workspace, module, profile)) goto defer;
    if (!test_fs_join_path(workspace.root, "test.stdout.log", stdout_log_abs)) goto defer;
    if (!test_fs_join_path(workspace.root, "test.stderr.log", stderr_log_abs)) goto defer;

    if (!preserve_env_for_restore(CMK2NOB_TEST_RUNNER_ENV, &prev_runner, &had_prev_runner)) goto defer;
    if (!preserve_env_for_restore(CMK2NOB_TEST_WS_REUSE_CWD_ENV, &prev_reuse_cwd, &had_prev_reuse_cwd)) goto defer;
    if (!preserve_env_for_restore(CMK2NOB_TEST_REPO_ROOT_ENV, &prev_repo_root, &had_prev_repo_root)) goto defer;
    if (!preserve_env_for_restore("ASAN_OPTIONS", &prev_asan_options, &had_prev_asan_options)) goto defer;
    if (!preserve_env_for_restore("UBSAN_OPTIONS", &prev_ubsan_options, &had_prev_ubsan_options)) goto defer;
    if (!preserve_env_for_restore("MSAN_OPTIONS", &prev_msan_options, &had_prev_msan_options)) goto defer;
    if (!preserve_env_for_restore("LLVM_PROFILE_FILE", &prev_llvm_profile_file, &had_prev_llvm_profile_file)) goto defer;
    (void)had_prev_runner;
    (void)had_prev_reuse_cwd;
    (void)had_prev_repo_root;

    if (!nob_set_current_dir(workspace.root)) goto defer;
    set_env_or_unset(CMK2NOB_TEST_RUNNER_ENV, "1");
    set_env_or_unset(CMK2NOB_TEST_WS_REUSE_CWD_ENV, "1");
    set_env_or_unset(CMK2NOB_TEST_REPO_ROOT_ENV, cwd);
    if (!had_prev_asan_options && profile && profile->asan_options_default) {
        set_env_or_unset("ASAN_OPTIONS", profile->asan_options_default);
    }
    if (!had_prev_ubsan_options && profile && profile->ubsan_options_default) {
        set_env_or_unset("UBSAN_OPTIONS", profile->ubsan_options_default);
    }
    if (!had_prev_msan_options && profile && profile->msan_options_default) {
        set_env_or_unset("MSAN_OPTIONS", profile->msan_options_default);
    }
    if (profile && profile->use_coverage && g_coverage_ctx.active) {
        const char *profraw_path = coverage_profraw_path_for_module_temp(module);
        if (!profraw_path) goto defer;
        if (nob_file_exists(profraw_path) && !nob_delete_file(profraw_path)) goto defer;
        set_env_or_unset("LLVM_PROFILE_FILE", profraw_path);
    }

    nob_cmd_append(&cmd, binary_abs);
    if (verbose) {
        ok = nob_cmd_run(&cmd);
    } else {
        ok = nob_cmd_run(&cmd, .stdout_path = stdout_log_abs, .stderr_path = stderr_log_abs);
    }

    if (!nob_set_current_dir(cwd)) {
        nob_log(NOB_ERROR, "failed to restore current directory to %s", cwd);
        ok = false;
    }

    if (!ok) {
        if (!verbose) {
            report_captured_test_output(module, &workspace, stdout_log_abs, stderr_log_abs);
        } else if (workspace.root[0] != '\0') {
            nob_log(NOB_ERROR, "[v2] preserved failed workspace: %s", workspace.root);
        }
        workspace_preserved = true;
    } else {
        cleanup_ok = cleanup_test_run_workspace(&workspace);
        if (!cleanup_ok) {
            nob_log(NOB_ERROR, "[v2] failed to cleanup run workspace for %s", module->name);
        }
    }

defer:
    set_env_or_unset(CMK2NOB_TEST_RUNNER_ENV, prev_runner);
    set_env_or_unset(CMK2NOB_TEST_WS_REUSE_CWD_ENV, prev_reuse_cwd);
    set_env_or_unset(CMK2NOB_TEST_REPO_ROOT_ENV, prev_repo_root);
    if (profile && profile->asan_options_default) {
        set_env_or_unset("ASAN_OPTIONS", had_prev_asan_options ? prev_asan_options : NULL);
    }
    if (profile && profile->ubsan_options_default) {
        set_env_or_unset("UBSAN_OPTIONS", had_prev_ubsan_options ? prev_ubsan_options : NULL);
    }
    if (profile && profile->msan_options_default) {
        set_env_or_unset("MSAN_OPTIONS", had_prev_msan_options ? prev_msan_options : NULL);
    }
    if (profile && profile->use_coverage) {
        set_env_or_unset("LLVM_PROFILE_FILE", had_prev_llvm_profile_file ? prev_llvm_profile_file : NULL);
    }
    free(prev_runner);
    free(prev_reuse_cwd);
    free(prev_repo_root);
    free(prev_asan_options);
    free(prev_ubsan_options);
    free(prev_msan_options);
    free(prev_llvm_profile_file);
    nob_cmd_free(cmd);
    if (!workspace_preserved && !cleanup_ok && workspace.root[0] != '\0') {
        cleanup_ok = cleanup_test_run_workspace(&workspace);
    }
    return ok && cleanup_ok;
}

static bool run_result_type_conventions_check(void) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    size_t temp_mark = nob_temp_save();
    nob_log(NOB_INFO, "[v2] check result type conventions");
    nob_cmd_append(&cmd, "bash", "test_v2/evaluator/check_result_type_conventions.sh");

    if (g_runner_verbose) {
        ok = nob_cmd_run(&cmd);
    } else {
        const char *stdout_path = nob_temp_sprintf("%s/result_type_check.stdout.log", TEMP_TESTS_PROBES);
        const char *stderr_path = nob_temp_sprintf("%s/result_type_check.stderr.log", TEMP_TESTS_PROBES);
        ok = nob_cmd_run(&cmd, .stdout_path = stdout_path, .stderr_path = stderr_path);
        if (!ok) {
            report_captured_test_output(
                &(Test_Module){ .name = "result-type-conventions" },
                NULL,
                stdout_path,
                stderr_path);
        }
    }

    nob_cmd_free(cmd);
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool run_test_preflight(const Test_Profile *profile) {
    if (!ensure_temp_tests_layout(profile)) return false;
    if (!run_result_type_conventions_check()) return false;
    nob_log(NOB_INFO, "[v2] validate workspace infra");
    if (!validate_coverage_tools_support(profile)) return false;
    return validate_test_profile_support(profile);
}

static bool run_test_module(const Test_Module *module, const Test_Profile *profile, bool verbose) {
    bool ok = false;
    bool lock_acquired = false;
    size_t temp_mark = nob_temp_save();
    const char *binary_rel_path = test_binary_output_path_temp(module, profile);
    const char *lock_path = test_build_lock_path_temp(profile);

    nob_log(NOB_INFO, "[v2] build+run %s (%s)", module->name, profile->name);

    lock_acquired = acquire_build_lock(lock_path);
    if (!lock_acquired) goto defer;
    if (!build_incremental_test_binary(binary_rel_path, module->append_sources, profile)) goto defer;
    if (profile && profile->use_coverage && !coverage_context_register_module(module, binary_rel_path)) goto defer;
    if (!release_build_lock(lock_path)) {
        lock_acquired = false;
        goto defer;
    }
    lock_acquired = false;

    ok = run_binary_in_workspace(module, profile, binary_rel_path, verbose);

defer:
    if (lock_acquired) {
        (void)release_build_lock(lock_path);
    }
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool run_all_test_modules(const Test_Profile *profile, bool verbose) {
    size_t passed_modules = 0;
    size_t failed_modules = 0;
    size_t skipped_modules = 0;
    size_t count = sizeof(TEST_MODULES) / sizeof(TEST_MODULES[0]);

    for (size_t i = 0; i < count; i++) {
        Test_Module module = TEST_MODULES[i];
        bool ok = false;
        if (!module.include_in_aggregate) {
            skipped_modules++;
            continue;
        }

        ok = run_test_module(&module, profile, verbose);
        if (ok) {
            passed_modules++;
            nob_log(NOB_INFO, "[v2] module %s: PASS", module.name);
        } else {
            failed_modules++;
            nob_log(NOB_ERROR, "[v2] module %s: FAIL", module.name);
        }
    }

    nob_log(NOB_INFO,
            "[v2] summary: passed_modules=%zu failed_modules=%zu skipped_modules=%zu",
            passed_modules,
            failed_modules,
            skipped_modules);
    return failed_modules == 0;
}

static bool collect_module_sources_unique(const Test_Module *module, Nob_File_Paths *out_sources) {
    Nob_Cmd sources = {0};
    bool ok = false;
    if (!module || !out_sources) return false;

    module->append_sources(&sources);
    for (size_t i = 0; i < sources.count; i++) {
        if (!file_paths_append_unique_dup(out_sources, sources.items[i])) goto defer;
    }
    ok = true;

defer:
    nob_cmd_free(sources);
    return ok;
}

static bool collect_all_tidy_sources_unique(Nob_File_Paths *out_sources) {
    size_t count = sizeof(TEST_MODULES) / sizeof(TEST_MODULES[0]);
    if (!out_sources) return false;
    for (size_t i = 0; i < count; i++) {
        if (!TEST_MODULES[i].include_in_aggregate) continue;
        if (!collect_module_sources_unique(&TEST_MODULES[i], out_sources)) return false;
    }
    return true;
}

static bool run_clang_tidy_for_sources(const Nob_File_Paths *sources) {
    char clang_tidy[_TINYDIR_PATH_MAX] = {0};
    size_t passed = 0;
    size_t failed = 0;

    if (!sources || sources->count == 0) {
        nob_log(NOB_ERROR, "[v2] no sources collected for clang-tidy");
        return false;
    }
    if (!resolve_clang_tidy_path(clang_tidy)) {
        nob_log(NOB_ERROR, "[v2] missing clang-tidy executable; set CLANG_TIDY or install clang-tidy");
        return false;
    }

    nob_log(NOB_INFO, "[v2] clang-tidy executable: %s", clang_tidy);
    for (size_t i = 0; i < sources->count; i++) {
        Nob_Cmd cmd = {0};
        bool ok = false;
        const char *source_path = sources->items[i];

        nob_log(NOB_INFO, "[v2] clang-tidy %s", source_path);
        nob_cmd_append(&cmd,
                       clang_tidy,
                       source_path,
                       "--quiet",
                       "--",
                       "-x",
                       "c");
        append_v2_common_flags(&cmd, &TEST_PROFILE_DEFAULT);
        ok = nob_cmd_run(&cmd);
        nob_cmd_free(cmd);
        if (ok) passed++;
        else failed++;
    }

    nob_log(NOB_INFO,
            "[v2] clang-tidy summary: passed=%zu failed=%zu",
            passed,
            failed);
    return failed == 0;
}

static bool resolve_tidy_command(const char *cmd,
                                 const Test_Module **module,
                                 bool *run_all) {
    const char *module_name = NULL;
    if (!cmd || !module || !run_all) return false;
    *module = NULL;
    *run_all = false;

    if (!starts_with(cmd, "clang-tidy-")) return false;
    module_name = cmd + strlen("clang-tidy-");
    if (strcmp(module_name, "v2") == 0) {
        *run_all = true;
        return true;
    }

    *module = find_test_module(module_name);
    return *module != NULL;
}

static bool run_tidy_command(const Test_Module *module, bool run_all) {
    Nob_File_Paths sources = {0};
    bool ok = false;

    if (!ensure_temp_tests_layout(&TEST_PROFILE_DEFAULT)) return false;
    if (run_all) {
        if (!collect_all_tidy_sources_unique(&sources)) goto defer;
    } else {
        if (!collect_module_sources_unique(module, &sources)) goto defer;
    }

    ok = run_clang_tidy_for_sources(&sources);

defer:
    file_paths_free_owned(&sources);
    return ok;
}

static const Test_Module *find_test_module(const char *name) {
    size_t count = sizeof(TEST_MODULES) / sizeof(TEST_MODULES[0]);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(TEST_MODULES[i].name, name) == 0) {
            return &TEST_MODULES[i];
        }
    }
    return NULL;
}

static bool has_suffix(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (text_len < suffix_len) return false;
    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

static bool resolve_test_command(const char *cmd,
                                 const Test_Module **module,
                                 const Test_Profile **profile,
                                 bool *run_all) {
    char base_command[64] = {0};
    const char *module_name = NULL;
    size_t base_len = 0;

    if (!cmd || !module || !profile || !run_all) return false;
    *module = NULL;
    *profile = &TEST_PROFILE_DEFAULT;
    *run_all = false;

    if (has_suffix(cmd, "-cov")) {
        base_len = strlen(cmd) - strlen("-cov");
        if (base_len + 1 > sizeof(base_command)) return false;
        memcpy(base_command, cmd, base_len);
        base_command[base_len] = '\0';
        *profile = &TEST_PROFILE_COVERAGE;
    } else if (has_suffix(cmd, "-msan")) {
        base_len = strlen(cmd) - strlen("-msan");
        if (base_len + 1 > sizeof(base_command)) return false;
        memcpy(base_command, cmd, base_len);
        base_command[base_len] = '\0';
        *profile = &TEST_PROFILE_MSAN;
    } else if (has_suffix(cmd, "-asan")) {
        base_len = strlen(cmd) - strlen("-asan");
        if (base_len + 1 > sizeof(base_command)) return false;
        memcpy(base_command, cmd, base_len);
        base_command[base_len] = '\0';
        *profile = &TEST_PROFILE_ASAN;
    } else if (has_suffix(cmd, "-ubsan")) {
        base_len = strlen(cmd) - strlen("-ubsan");
        if (base_len + 1 > sizeof(base_command)) return false;
        memcpy(base_command, cmd, base_len);
        base_command[base_len] = '\0';
        *profile = &TEST_PROFILE_UBSAN;
    } else if (has_suffix(cmd, "-san")) {
        base_len = strlen(cmd) - strlen("-san");
        if (base_len + 1 > sizeof(base_command)) return false;
        memcpy(base_command, cmd, base_len);
        base_command[base_len] = '\0';
        *profile = &TEST_PROFILE_ASAN_UBSAN;
    } else {
        base_len = strlen(cmd);
        if (base_len + 1 > sizeof(base_command)) return false;
        memcpy(base_command, cmd, base_len + 1);
    }

    if (strcmp(base_command, "test-v2") == 0) {
        *run_all = true;
        return true;
    }
    if (strncmp(base_command, "test-", 5) != 0) return false;

    module_name = base_command + 5;
    *module = find_test_module(module_name);
    return *module != NULL;
}

static bool run_test_command(const Test_Module *module,
                             const Test_Profile *profile,
                             bool run_all,
                             bool verbose) {
    bool ok = false;
    bool coverage_started = false;

    if (!run_test_preflight(profile)) return false;
    if (profile && profile->use_coverage) {
        if (!coverage_context_begin(run_all ? "test-v2" : module->name)) return false;
        coverage_started = true;
    }
    if (run_all) {
        ok = run_all_test_modules(profile, verbose);
        if (coverage_started && ok) ok = generate_coverage_report(module, true);
        goto defer;
    }

    ok = run_test_module(module, profile, verbose);
    nob_log(ok ? NOB_INFO : NOB_ERROR,
            "[v2] module %s: %s",
            module->name,
            ok ? "PASS" : "FAIL");
    if (coverage_started && ok) ok = generate_coverage_report(module, false);

defer:
    if (coverage_started) coverage_context_reset();
    return ok;
}

int main(int argc, char **argv) {
    const char *cmd = "test-v2";
    const Test_Module *module = NULL;
    const Test_Profile *profile = NULL;
    bool run_all = false;
    bool verbose = false;
    bool command_seen = false;

    nob_set_log_handler(runner_log_handler);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
            g_runner_verbose = true;
            continue;
        }
        if (!command_seen) {
            cmd = argv[i];
            command_seen = true;
            continue;
        }
        nob_log(NOB_ERROR, "unexpected argument: %s", argv[i]);
        return 1;
    }

    if (strcmp(cmd, "clean-tests") == 0) return test_fs_remove_tree(TEMP_TESTS_ROOT) ? 0 : 1;
    if (resolve_tidy_command(cmd, &module, &run_all)) {
        return run_tidy_command(module, run_all) ? 0 : 1;
    }
    if (resolve_test_command(cmd, &module, &profile, &run_all)) {
        return run_test_command(module, profile, run_all, verbose) ? 0 : 1;
    }

    nob_log(NOB_INFO,
            "Usage: %s [--verbose] [clean-tests|clang-tidy-v2|clang-tidy-<module>|test-arena|test-lexer|test-parser|test-evaluator|test-evaluator-integration|test-pipeline|test-codegen|test-v2|test-*-cov|test-*-san|test-*-asan|test-*-ubsan|test-*-msan]",
            argv[0]);
    return 1;
}
