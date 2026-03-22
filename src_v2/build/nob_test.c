#define NOB_IMPLEMENTATION
#include "nob.h"
#include "test_fs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <errno.h>
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
#define TEMP_TESTS_WORK TEMP_TESTS_ROOT "/work"
#define TEMP_TESTS_BIN TEMP_TESTS_ROOT "/bin"
#define TEMP_TESTS_OBJ TEMP_TESTS_ROOT "/obj"

#define TEST_ARENA_OUT TEMP_TESTS_BIN "/test_arena"
#define TEST_LEXER_OUT TEMP_TESTS_BIN "/test_lexer"
#define TEST_PARSER_OUT TEMP_TESTS_BIN "/test_parser"
#define TEST_EVALUATOR_OUT TEMP_TESTS_BIN "/test_evaluator"
#define TEST_PIPELINE_OUT TEMP_TESTS_BIN "/test_pipeline"
#define TEST_CODEGEN_OUT TEMP_TESTS_BIN "/test_codegen"

#define TEST_ARENA_RUN "../bin/test_arena"
#define TEST_LEXER_RUN "../bin/test_lexer"
#define TEST_PARSER_RUN "../bin/test_parser"
#define TEST_EVALUATOR_RUN "../bin/test_evaluator"
#define TEST_PIPELINE_RUN "../bin/test_pipeline"
#define TEST_CODEGEN_RUN "../bin/test_codegen"

typedef bool (*Test_Module_Run_Fn)(void);
typedef void (*Append_Source_List_Fn)(Nob_Cmd *cmd);

typedef struct {
    const char *name;
    Test_Module_Run_Fn run;
} Test_Module;

static bool test_lexer(void);
static bool test_parser(void);
static bool test_evaluator(void);
static bool test_pipeline(void);
static bool test_arena(void);
static bool test_codegen(void);
static void append_v2_pcre_sources(Nob_Cmd *cmd);
static void append_platform_link_flags(Nob_Cmd *cmd);
static bool ensure_temp_tests_layout(void);

static Test_Module TEST_MODULES[] = {
    {"arena", test_arena},
    {"lexer", test_lexer},
    {"parser", test_parser},
    {"evaluator", test_evaluator},
    {"pipeline", test_pipeline},
    {"codegen", test_codegen},
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
        "test_v2/test_workspace.c",
        "test_v2/arena/test_arena_v2_main.c",
        "test_v2/arena/test_arena_v2_suite.c");
}

static void append_v2_lexer_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_workspace.c",
        "test_v2/lexer/test_lexer_v2_main.c",
        "test_v2/lexer/test_lexer_v2_suite.c");
}

static void append_v2_parser_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_workspace.c",
        "test_v2/parser/test_parser_v2_main.c",
        "test_v2/parser/test_parser_v2_suite.c");
}

static void append_v2_evaluator_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_workspace.c",
        "test_v2/evaluator/test_evaluator_v2_main.c",
        "test_v2/evaluator/test_evaluator_v2_suite.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch1.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch2.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch3.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch4.c",
        "test_v2/evaluator/test_evaluator_v2_suite_batch5.c");
}

static void append_v2_pipeline_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_workspace.c",
        "test_v2/pipeline/test_pipeline_v2_main.c",
        "test_v2/pipeline/test_pipeline_v2_suite.c");
}

static void append_v2_codegen_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "test_v2/test_workspace.c",
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

static const char *test_object_config_dir_temp(void) {
    const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
    const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
    bool with_curl = use_libcurl && strcmp(use_libcurl, "1") == 0;
    bool with_archive = use_libarchive && strcmp(use_libarchive, "1") == 0;
    return nob_temp_sprintf("%s/curl%d_archive%d",
                            TEMP_TESTS_OBJ,
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

static const char *test_object_path_temp(const char *source_path) {
    return nob_temp_sprintf("%s/%s.o", test_object_config_dir_temp(), source_path);
}

static const char *test_dep_path_temp(const char *source_path) {
    return nob_temp_sprintf("%s/%s.d", test_object_config_dir_temp(), source_path);
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
                              const char *dep_path) {
    Nob_Cmd cmd = {0};
    bool ok = false;

    if (!ensure_parent_dir(object_path) || !ensure_parent_dir(dep_path)) return false;
    if (!object_needs_rebuild(object_path, dep_path)) return true;

    nob_log(NOB_INFO, "[v2] compile %s", source_path);
    nob_cc(&cmd);
    append_v2_common_flags(&cmd);
    nob_cmd_append(&cmd, "-MMD", "-MF", dep_path, "-c", source_path, "-o", object_path);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool link_test_binary(const char *output_path, const Nob_File_Paths *object_paths) {
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
    nob_cc(&cmd);
    nob_cmd_append(&cmd, "-o", output_path);
    for (size_t i = 0; i < object_paths->count; ++i) {
        nob_cmd_append(&cmd, object_paths->items[i]);
    }
    append_platform_link_flags(&cmd);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);

defer:
    nob_da_free(inputs);
    return ok;
}

static bool build_incremental_test_binary(const char *output_path,
                                          Append_Source_List_Fn append_sources) {
    Nob_Cmd sources = {0};
    Nob_File_Paths object_paths = {0};
    bool ok = false;
    size_t temp_mark = nob_temp_save();

    if (!ensure_temp_tests_layout()) goto defer;

    append_sources(&sources);
    for (size_t i = 0; i < sources.count; ++i) {
        const char *source_path = sources.items[i];
        const char *object_path = test_object_path_temp(source_path);
        const char *dep_path = test_dep_path_temp(source_path);

        nob_da_append(&object_paths, object_path);
        if (!build_object_file(source_path, object_path, dep_path)) goto defer;
    }

    ok = link_test_binary(output_path, &object_paths);

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

static bool ensure_temp_tests_layout(void) {
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_ROOT)) return false;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_WORK)) return false;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_BIN)) return false;
    if (!nob_mkdir_if_not_exists(TEMP_TESTS_OBJ)) return false;
    return true;
}

static bool prepare_temp_tests_workspace(void) {
    if (!test_fs_remove_tree(TEMP_TESTS_WORK)) return false;
    if (!ensure_temp_tests_layout()) return false;
    if (!test_fs_copy_tree("test_v2", TEMP_TESTS_WORK "/test_v2")) return false;
    return true;
}

static bool cleanup_temp_tests_workspace(void) {
    return test_fs_remove_tree(TEMP_TESTS_WORK);
}

static bool run_binary_in_workspace(const char *bin_rel_path) {
    char cwd[_TINYDIR_PATH_MAX] = {0};
    char *prev_reuse_cwd = NULL;
    char *prev_repo_root = NULL;
    Nob_Cmd cmd = {0};
    bool ok = false;

    if (!test_fs_save_current_dir(cwd)) return false;
    prev_reuse_cwd = dup_env_value("CMK2NOB_TEST_WS_REUSE_CWD");
    if (getenv("CMK2NOB_TEST_WS_REUSE_CWD") && !prev_reuse_cwd) return false;
    prev_repo_root = dup_env_value("CMK2NOB_TEST_REPO_ROOT");
    if (getenv("CMK2NOB_TEST_REPO_ROOT") && !prev_repo_root) {
        free(prev_reuse_cwd);
        return false;
    }
    if (!nob_set_current_dir(TEMP_TESTS_WORK)) {
        free(prev_reuse_cwd);
        free(prev_repo_root);
        return false;
    }
    set_env_or_unset("CMK2NOB_TEST_WS_REUSE_CWD", "1");
    set_env_or_unset("CMK2NOB_TEST_REPO_ROOT", cwd);

    nob_cmd_append(&cmd, bin_rel_path);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    set_env_or_unset("CMK2NOB_TEST_WS_REUSE_CWD", prev_reuse_cwd);
    set_env_or_unset("CMK2NOB_TEST_REPO_ROOT", prev_repo_root);
    free(prev_reuse_cwd);
    free(prev_repo_root);

    if (!nob_set_current_dir(cwd)) {
        nob_log(NOB_ERROR, "failed to restore current directory to %s", cwd);
        ok = false;
    }

    return ok;
}

static bool build_test_lexer(void) {
    return build_incremental_test_binary(TEST_LEXER_OUT, append_test_lexer_all_sources);
}

static bool build_test_arena(void) {
    return build_incremental_test_binary(TEST_ARENA_OUT, append_test_arena_all_sources);
}

static bool build_test_parser(void) {
    return build_incremental_test_binary(TEST_PARSER_OUT, append_test_parser_all_sources);
}

static bool build_test_evaluator(void) {
    return build_incremental_test_binary(TEST_EVALUATOR_OUT, append_test_evaluator_all_sources);
}

static bool build_test_pipeline(void) {
    return build_incremental_test_binary(TEST_PIPELINE_OUT, append_test_pipeline_all_sources);
}

static bool build_test_codegen(void) {
    return build_incremental_test_binary(TEST_CODEGEN_OUT, append_test_codegen_all_sources);
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

static bool test_codegen(void) {
    nob_log(NOB_INFO, "[v2] build+run codegen");
    if (!build_test_codegen()) return false;
    return run_binary_in_workspace(TEST_CODEGEN_RUN);
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

    if (strcmp(cmd, "clean-tests") == 0) return test_fs_remove_tree(TEMP_TESTS_ROOT) ? 0 : 1;
    if (strcmp(cmd, "test-arena") == 0) return run_in_temp_workspace(test_arena) ? 0 : 1;
    if (strcmp(cmd, "test-lexer") == 0) return run_in_temp_workspace(test_lexer) ? 0 : 1;
    if (strcmp(cmd, "test-parser") == 0) return run_in_temp_workspace(test_parser) ? 0 : 1;
    if (strcmp(cmd, "test-evaluator") == 0) return run_in_temp_workspace(test_evaluator) ? 0 : 1;
    if (strcmp(cmd, "test-pipeline") == 0) return run_in_temp_workspace(test_pipeline) ? 0 : 1;
    if (strcmp(cmd, "test-codegen") == 0) return run_in_temp_workspace(test_codegen) ? 0 : 1;
    if (strcmp(cmd, "test-v2") == 0) return run_in_temp_workspace(test_v2_all) ? 0 : 1;

    nob_log(NOB_INFO, "Usage: %s [clean-tests|test-arena|test-lexer|test-parser|test-evaluator|test-pipeline|test-codegen|test-v2]", argv[0]);
    return 1;
}
