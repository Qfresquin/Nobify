#include "test_codegen_v2_support.h"
#include "test_fs.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static char s_codegen_repo_root[_TINYDIR_PATH_MAX] = {0};

static bool codegen_copy_string(const char *src,
                                char out[_TINYDIR_PATH_MAX]) {
    int n = 0;
    if (!src || !out) return false;
    n = snprintf(out, _TINYDIR_PATH_MAX, "%s", src);
    if (n < 0 || n >= _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "codegen test: path too long: %s", src);
        return false;
    }
    return true;
}

static bool codegen_path_is_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    {
        DWORD attrs = GetFileAttributesA(path);
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }
#else
    return access(path, X_OK) == 0;
#endif
}

static bool codegen_find_repo_probe_cmake(char out_path[_TINYDIR_PATH_MAX]) {
    char probes_root[_TINYDIR_PATH_MAX] = {0};
    Nob_Dir_Entry dir = {0};
    Test_Fs_Path_Info info = {0};
    const char *repo_root = s_codegen_repo_root[0] != '\0'
        ? s_codegen_repo_root
        : getenv(CMK2NOB_TEST_REPO_ROOT_ENV);

    if (!repo_root || repo_root[0] == '\0') return false;
    if (!test_fs_join_path(repo_root, "Temp_tests/probes", probes_root)) return false;
    if (!test_fs_get_path_info(probes_root, &info) || !info.exists || !info.is_dir) return false;
    if (!nob_dir_entry_open(probes_root, &dir)) return false;

    while (nob_dir_entry_next(&dir)) {
        char candidate_dir[_TINYDIR_PATH_MAX] = {0};
        char candidate_path[_TINYDIR_PATH_MAX] = {0};
        if (test_fs_is_dot_or_dotdot(dir.name)) continue;
        if (strncmp(dir.name, "cmake-", strlen("cmake-")) != 0) continue;
        if (!test_fs_join_path(probes_root, dir.name, candidate_dir) ||
            !test_fs_join_path(candidate_dir, "bin/cmake", candidate_path)) {
            continue;
        }
        if (codegen_path_is_executable(candidate_path)) {
            nob_dir_entry_close(dir);
            return codegen_copy_string(candidate_path, out_path);
        }
    }

    nob_dir_entry_close(dir);
    return false;
}

static bool codegen_resolve_sibling_tool(const char *tool_path,
                                         const char *tool_name,
                                         char out_path[_TINYDIR_PATH_MAX]) {
    const char *tool_dir = NULL;
    if (!tool_path || !tool_name || !out_path) return false;
    tool_dir = nob_temp_dir_name(tool_path);
    if (!tool_dir || tool_dir[0] == '\0') return false;
    if (snprintf(out_path, _TINYDIR_PATH_MAX, "%s/%s", tool_dir, tool_name) >= _TINYDIR_PATH_MAX) {
        return false;
    }
    return test_ws_host_path_exists(out_path);
}

static bool codegen_fill_host_tool_paths(Arena *arena, Nob_Codegen_Options *opts) {
    char cmake_bin[_TINYDIR_PATH_MAX] = {0};
    char cpack_bin[_TINYDIR_PATH_MAX] = {0};
    char gzip_bin[_TINYDIR_PATH_MAX] = {0};
    char xz_bin[_TINYDIR_PATH_MAX] = {0};
    const char *env_cmake = NULL;
    char *cmake_copy = NULL;
    char *cpack_copy = NULL;
    char *gzip_copy = NULL;
    char *xz_copy = NULL;
    if (!arena || !opts) return false;

    env_cmake = getenv(CMK2NOB_TEST_CMAKE_BIN_ENV);
    if (env_cmake && env_cmake[0] != '\0') {
        if (strchr(env_cmake, '/') || strchr(env_cmake, '\\')) {
            if (snprintf(cmake_bin, sizeof(cmake_bin), "%s", env_cmake) >= (int)sizeof(cmake_bin)) {
                return false;
            }
        } else if (!test_ws_host_program_in_path(env_cmake, cmake_bin)) {
            cmake_bin[0] = '\0';
        }
    } else if (test_ws_host_program_in_path("cmake", cmake_bin)) {
        /* Resolved from PATH. */
    } else if (codegen_find_repo_probe_cmake(cmake_bin)) {
        /* Resolved from repo-local probe cache. */
    } else {
        cmake_bin[0] = '\0';
    }

    (void)test_ws_host_program_in_path("gzip", gzip_bin);
    (void)test_ws_host_program_in_path("xz", xz_bin);
    if (cmake_bin[0] == '\0') {
        if (gzip_bin[0] != '\0') {
            gzip_copy = arena_strdup(arena, gzip_bin);
            if (!gzip_copy) return false;
            opts->embedded_gzip_bin = nob_sv_from_cstr(gzip_copy);
        }
        if (xz_bin[0] != '\0') {
            xz_copy = arena_strdup(arena, xz_bin);
            if (!xz_copy) return false;
            opts->embedded_xz_bin = nob_sv_from_cstr(xz_copy);
        }
        return true;
    }
    (void)codegen_resolve_sibling_tool(cmake_bin, "cpack", cpack_bin);

    cmake_copy = arena_strdup(arena, cmake_bin);
    if (!cmake_copy) return false;
    opts->embedded_cmake_bin = nob_sv_from_cstr(cmake_copy);
    if (cpack_bin[0] != '\0') {
        cpack_copy = arena_strdup(arena, cpack_bin);
        if (!cpack_copy) return false;
        opts->embedded_cpack_bin = nob_sv_from_cstr(cpack_copy);
    }
    if (gzip_bin[0] != '\0') {
        gzip_copy = arena_strdup(arena, gzip_bin);
        if (!gzip_copy) return false;
        opts->embedded_gzip_bin = nob_sv_from_cstr(gzip_copy);
    }
    if (xz_bin[0] != '\0') {
        xz_copy = arena_strdup(arena, xz_bin);
        if (!xz_copy) return false;
        opts->embedded_xz_bin = nob_sv_from_cstr(xz_copy);
    }
    return true;
}

bool codegen_host_cmake_available(void) {
    Arena *arena = arena_create(4096);
    Nob_Codegen_Options opts = {0};
    bool ok = false;
    if (!arena) return false;
    ok = codegen_fill_host_tool_paths(arena, &opts) && opts.embedded_cmake_bin.count > 0;
    arena_destroy(arena);
    return ok;
}

static bool codegen_mkdirs(const char *path) {
    char buf[_TINYDIR_PATH_MAX] = {0};
    size_t len = 0;
    if (!path || path[0] == '\0' || strcmp(path, ".") == 0) return true;
    len = strlen(path);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, path, len + 1);

    for (size_t i = 1; i < len; ++i) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        if (buf[0] != '\0' && !nob_mkdir_if_not_exists(buf)) return false;
        buf[i] = '/';
    }

    return nob_mkdir_if_not_exists(buf);
}

static bool codegen_render_or_write_script(const char *script,
                                           const Codegen_Test_Config *config,
                                           Nob_String_Builder *out,
                                           bool write_file) {
    Test_Semantic_Pipeline_Config pipeline_config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *codegen_arena = arena_create(8 * 1024 * 1024);
    const char *effective_input_path = NULL;
    const char *effective_output_path = NULL;
    const char *effective_source_dir = NULL;
    const char *effective_binary_dir = NULL;
    bool ok = false;
    if (!codegen_arena) return false;

    effective_input_path =
        (config && config->input_path && config->input_path[0] != '\0')
            ? config->input_path
            : "CMakeLists.txt";
    effective_output_path =
        (config && config->output_path && config->output_path[0] != '\0')
            ? config->output_path
            : nob_temp_sprintf("%s/nob.c", nob_temp_dir_name(effective_input_path));
    effective_source_dir =
        (config && config->source_dir && config->source_dir[0] != '\0')
            ? config->source_dir
            : nob_temp_dir_name(effective_input_path);
    effective_binary_dir =
        (config && config->binary_dir && config->binary_dir[0] != '\0')
            ? config->binary_dir
            : effective_source_dir;

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();

    test_semantic_pipeline_config_init(&pipeline_config);
    pipeline_config.current_file = effective_input_path;
    pipeline_config.source_dir = nob_sv_from_cstr(effective_source_dir);
    pipeline_config.binary_dir = nob_sv_from_cstr(effective_binary_dir);

    ok = test_semantic_pipeline_fixture_from_script(&fixture, script, &pipeline_config);
    if (!ok || !fixture.build.freeze_ok || !fixture.build.model || diag_has_errors()) {
        arena_destroy(codegen_arena);
        test_semantic_pipeline_fixture_destroy(&fixture);
        return false;
    }

    {
        Nob_Codegen_Options opts = {
            .input_path = nob_sv_from_cstr(effective_input_path),
            .output_path = nob_sv_from_cstr(effective_output_path),
            .source_root = nob_sv_from_cstr(effective_source_dir),
            .binary_root = nob_sv_from_cstr(effective_binary_dir),
            .target_platform = config ? config->platform : NOB_CODEGEN_PLATFORM_HOST,
            .backend = config ? config->backend : NOB_CODEGEN_BACKEND_AUTO,
        };
        if (!codegen_fill_host_tool_paths(codegen_arena, &opts)) {
            arena_destroy(codegen_arena);
            test_semantic_pipeline_fixture_destroy(&fixture);
            return false;
        }
        ok = write_file
            ? nob_codegen_write_file(fixture.build.model, codegen_arena, &opts)
            : nob_codegen_render(fixture.build.model, codegen_arena, &opts, out);
    }

    arena_destroy(codegen_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    return ok;
}

static bool codegen_run_binary(const char *binary_path, const char *arg1, const char *arg2) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!binary_path) return false;
    nob_cmd_append(&cmd, binary_path);
    if (arg1) nob_cmd_append(&cmd, arg1);
    if (arg2) nob_cmd_append(&cmd, arg2);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool codegen_run_binary_argv(const char *binary_path,
                                    const char *const *argv,
                                    size_t argc) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!binary_path) return false;
    nob_cmd_append(&cmd, binary_path);
    for (size_t i = 0; i < argc; ++i) {
        if (!argv || !argv[i]) continue;
        nob_cmd_append(&cmd, argv[i]);
    }
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

void codegen_test_set_repo_root(const char *repo_root) {
    snprintf(s_codegen_repo_root, sizeof(s_codegen_repo_root), "%s", repo_root ? repo_root : "");
}

bool codegen_render_script(const char *script,
                           const char *input_path,
                           const char *output_path,
                           Nob_String_Builder *out) {
    Codegen_Test_Config config = {
        .input_path = input_path,
        .output_path = output_path,
        .source_dir = NULL,
        .binary_dir = NULL,
        .platform = NOB_CODEGEN_PLATFORM_HOST,
        .backend = NOB_CODEGEN_BACKEND_AUTO,
    };
    return codegen_render_or_write_script(script, &config, out, false);
}

bool codegen_render_script_with_config(const char *script,
                                       const Codegen_Test_Config *config,
                                       Nob_String_Builder *out) {
    return codegen_render_or_write_script(script, config, out, false);
}

bool codegen_write_script(const char *script,
                          const char *input_path,
                          const char *output_path) {
    Codegen_Test_Config config = {
        .input_path = input_path,
        .output_path = output_path,
        .source_dir = NULL,
        .binary_dir = NULL,
        .platform = NOB_CODEGEN_PLATFORM_HOST,
        .backend = NOB_CODEGEN_BACKEND_AUTO,
    };
    return codegen_render_or_write_script(script, &config, NULL, true);
}

bool codegen_write_script_with_config(const char *script,
                                      const Codegen_Test_Config *config) {
    return codegen_render_or_write_script(script, config, NULL, true);
}

bool codegen_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!arena || !path || !out) return false;
    if (!nob_read_entire_file(path, &sb)) return false;
    copy = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool codegen_sv_contains(String_View sv, const char *needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    if (!needle || needle_len == 0 || sv.count < needle_len) return false;
    for (size_t i = 0; i + needle_len <= sv.count; ++i) {
        if (memcmp(sv.data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static bool codegen_compile_generated_nob_with_flags(const char *generated_path,
                                                     const char *output_path,
                                                     bool warnings_as_errors) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (s_codegen_repo_root[0] == '\0' || !generated_path || !output_path) return false;
    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd,
                   "-D_GNU_SOURCE",
                   "-std=c11",
                   "-Wall",
                   "-Wextra",
                   nob_temp_sprintf("-I%s/vendor", s_codegen_repo_root),
                   "-o",
                   output_path,
                   generated_path);
    if (warnings_as_errors) nob_cmd_append(&cmd, "-Werror");
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool codegen_compile_generated_nob(const char *generated_path, const char *output_path) {
    return codegen_compile_generated_nob_with_flags(generated_path, output_path, false);
}

bool codegen_compile_generated_nob_strict(const char *generated_path, const char *output_path) {
    return codegen_compile_generated_nob_with_flags(generated_path, output_path, true);
}

bool codegen_write_text_file(const char *path, const char *text) {
    const char *dir = NULL;
    if (!path || !text) return false;
    dir = nob_temp_dir_name(path);
    if (dir && strcmp(dir, ".") != 0 && !codegen_mkdirs(dir)) return false;
    return nob_write_entire_file(path, text, strlen(text));
}

bool codegen_run_binary_in_dir(const char *dir,
                               const char *binary_path,
                               const char *arg1,
                               const char *arg2) {
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;
    if (!dir || !binary_path || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);
    if (!nob_set_current_dir(dir)) return false;
    ok = codegen_run_binary(binary_path, arg1, arg2);
    if (!nob_set_current_dir(prev_cwd)) return false;
    return ok;
}

bool codegen_run_binary_in_dir_argv(const char *dir,
                                    const char *binary_path,
                                    const char *const *argv,
                                    size_t argc) {
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;
    if (!dir || !binary_path || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);
    if (!nob_set_current_dir(dir)) return false;
    ok = codegen_run_binary_argv(binary_path, argv, argc);
    if (!nob_set_current_dir(prev_cwd)) return false;
    return ok;
}
