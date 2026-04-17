static bool test_profile_is_instrumented(const Test_Runner_Profile_Internal *profile) {
    return profile && (profile->use_asan ||
                       profile->use_ubsan ||
                       profile->use_msan ||
                       profile->use_coverage);
}

static bool test_profile_is_fast(const Test_Runner_Profile_Internal *profile) {
    return profile && profile->def.id == TEST_RUNNER_PROFILE_FAST;
}

static bool test_profile_uses_clang(const Test_Runner_Profile_Internal *profile) {
    return profile && (profile->use_msan || profile->use_coverage);
}

static bool test_profile_needs_llvm_cov_tools(const Test_Runner_Profile_Internal *profile) {
    return profile && profile->use_coverage;
}

static const char *test_runner_compiler_launcher(void) {
    const char *launcher = getenv("NOB_TEST_COMPILER_LAUNCHER");
    return (launcher && launcher[0] != '\0') ? launcher : NULL;
}

static size_t test_runner_compile_jobs(void) {
    const char *value = getenv("NOB_TEST_JOBS");
    long parsed = 0;
    int detected = 0;

    if (value && value[0] != '\0') {
        char *end = NULL;
        parsed = strtol(value, &end, 10);
        if (end && *end == '\0' && parsed > 0) return (size_t)parsed;
    }

    detected = nob_nprocs();
    if (detected <= 1) return 1;
    if (detected > 8) detected = 8;
    return (size_t)detected;
}

static size_t test_runner_effective_compile_jobs(size_t source_count) {
    size_t jobs = test_runner_compile_jobs();
    if (source_count == 0) return 0;
    if (jobs == 0) return 1;
    if (jobs > source_count) jobs = source_count;
    return jobs;
}

static void append_test_profile_compiler(Nob_Cmd *cmd, const Test_Runner_Profile_Internal *profile) {
    const char *launcher = test_runner_compiler_launcher();
    if (launcher) nob_cmd_append(cmd, launcher);
    if (test_profile_uses_clang(profile)) {
        nob_cmd_append(cmd, "clang");
        return;
    }
    nob_cc(cmd);
}

static Test_Runner_Build_Stats *test_runner_get_build_stats(Test_Runner_Context *ctx) {
    if (!ctx || !ctx->out_result) return NULL;
    return &ctx->out_result->build_stats;
}

static void test_runner_note_launcher_kind(Test_Runner_Context *ctx) {
    Test_Runner_Build_Stats *stats = test_runner_get_build_stats(ctx);
    if (!stats) return;
    stats->launcher_kind = test_runner_classify_launcher_kind(test_runner_compiler_launcher());
}

static void test_runner_note_compile_jobs(Test_Runner_Context *ctx, size_t jobs) {
    Test_Runner_Build_Stats *stats = test_runner_get_build_stats(ctx);
    if (!stats) return;
    if (jobs > stats->compile_jobs) stats->compile_jobs = (uint32_t)jobs;
}

static void test_runner_note_object_considered(Test_Runner_Context *ctx, bool rebuilt) {
    Test_Runner_Build_Stats *stats = test_runner_get_build_stats(ctx);
    if (!stats) return;
    stats->objects_total++;
    if (rebuilt) stats->objects_rebuilt++;
    else stats->objects_reused++;
}

static void test_runner_note_link_result(Test_Runner_Context *ctx, bool linked) {
    Test_Runner_Build_Stats *stats = test_runner_get_build_stats(ctx);
    if (!stats || !linked) return;
    stats->link_performed = 1u;
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
    bool verbose = g_active_runner_ctx && g_active_runner_ctx->verbose;

    va_copy(copy, args);
    n = vsnprintf(message, sizeof(message), fmt, copy);
    va_end(copy);
    if (n < 0) return;

    if (!verbose) {
        if (level >= NOB_WARNING) {
            runner_emit_log_line(level, message);
            return;
        }
        if (!runner_should_show_info_message(message)) return;
    }

    runner_emit_log_line(level, message);
}

static void append_test_profile_compile_flags(Nob_Cmd *cmd, const Test_Runner_Profile_Internal *profile) {
    if (!test_profile_is_instrumented(profile)) {
        if (test_profile_is_fast(profile)) {
            nob_cmd_append(cmd, "-Og", "-g1");
            return;
        }
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

static void append_test_profile_link_flags(Nob_Cmd *cmd, const Test_Runner_Profile_Internal *profile) {
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

static void append_v2_common_flags(Nob_Cmd *cmd, const Test_Runner_Profile_Internal *profile) {
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

    {
        const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
        const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
        if (use_libcurl && strcmp(use_libcurl, "1") == 0) {
            nob_cmd_append(cmd, "-DEVAL_HAVE_LIBCURL=1");
        }
        if (use_libarchive && strcmp(use_libarchive, "1") == 0) {
            nob_cmd_append(cmd, "-DEVAL_HAVE_LIBARCHIVE=1");
        }
    }

    append_test_profile_compile_flags(cmd, profile);
}

static void append_platform_link_flags(Nob_Cmd *cmd) {
#ifndef _WIN32
    nob_cmd_append(cmd, "-lpcre2-posix");
    nob_cmd_append(cmd, "-lpcre2-8");
#else
    (void)cmd;
#endif
    {
        const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
        const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
        if (use_libcurl && strcmp(use_libcurl, "1") == 0) {
            nob_cmd_append(cmd, "-lcurl");
        }
        if (use_libarchive && strcmp(use_libarchive, "1") == 0) {
            nob_cmd_append(cmd, "-larchive");
        }
    }
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

static const char *test_profile_bin_dir_temp(const Test_Runner_Profile_Internal *profile) {
    return nob_temp_sprintf("%s/%s", TEMP_TESTS_BIN_ROOT, profile->def.name);
}

static const char *test_object_config_dir_temp(const Test_Runner_Profile_Internal *profile) {
    const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
    const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
    bool with_curl = use_libcurl && strcmp(use_libcurl, "1") == 0;
    bool with_archive = use_libarchive && strcmp(use_libarchive, "1") == 0;
    return nob_temp_sprintf("%s/%s/curl%d_archive%d",
                            TEMP_TESTS_OBJ_ROOT,
                            profile->def.name,
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

static const char *test_object_path_temp(const char *source_path, const Test_Runner_Profile_Internal *profile) {
    return nob_temp_sprintf("%s/%s.o", test_object_config_dir_temp(profile), source_path);
}

static const char *test_dep_path_temp(const char *source_path, const Test_Runner_Profile_Internal *profile) {
    return nob_temp_sprintf("%s/%s.d", test_object_config_dir_temp(profile), source_path);
}

static const char *test_binary_output_path_temp(const Test_Runner_Module_Internal *module,
                                                const Test_Runner_Profile_Internal *profile) {
    return nob_temp_sprintf("%s/test_%s", test_profile_bin_dir_temp(profile), module->def.name);
}

static const char *test_nobify_output_path_temp(const Test_Runner_Profile_Internal *profile) {
    return nob_temp_sprintf("%s/nobify_tool", test_profile_bin_dir_temp(profile));
}

static const char *test_build_lock_path_temp(const Test_Runner_Profile_Internal *profile) {
    return nob_temp_sprintf("%s/%s.lock", TEMP_TESTS_LOCKS, profile->def.name);
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
    if (!append_runner_build_inputs(&inputs)) goto defer;

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
                              const Test_Runner_Profile_Internal *profile,
                              Test_Runner_Context *ctx) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    bool needs_rebuild = false;

    if (!ensure_parent_dir(object_path) || !ensure_parent_dir(dep_path)) return false;
    needs_rebuild = object_needs_rebuild(object_path, dep_path);
    test_runner_note_object_considered(ctx, needs_rebuild);
    if (!needs_rebuild) return true;

    nob_log(NOB_INFO, "[v2] compile %s", source_path);
    append_test_profile_compiler(&cmd, profile);
    append_v2_common_flags(&cmd, profile);
    nob_cmd_append(&cmd, "-MMD", "-MF", dep_path, "-c", source_path, "-o", object_path);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool enqueue_object_build(Nob_Procs *procs,
                                 size_t max_jobs,
                                 const char *source_path,
                                 const char *object_path,
                                 const char *dep_path,
                                 const Test_Runner_Profile_Internal *profile,
                                 Test_Runner_Context *ctx) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    bool needs_rebuild = false;

    if (!ensure_parent_dir(object_path) || !ensure_parent_dir(dep_path)) return false;
    needs_rebuild = object_needs_rebuild(object_path, dep_path);
    test_runner_note_object_considered(ctx, needs_rebuild);
    if (!needs_rebuild) return true;

    nob_log(NOB_INFO, "[v2] compile %s", source_path);
    append_test_profile_compiler(&cmd, profile);
    append_v2_common_flags(&cmd, profile);
    nob_cmd_append(&cmd, "-MMD", "-MF", dep_path, "-c", source_path, "-o", object_path);
    ok = nob_cmd_run(&cmd,
                     .async = procs,
                     .max_procs = max_jobs);
    nob_cmd_free(cmd);
    return ok;
}

static bool link_test_binary(const char *output_path,
                             const Nob_File_Paths *object_paths,
                             const Test_Runner_Profile_Internal *profile,
                             bool *out_link_performed) {
    Nob_Cmd cmd = {0};
    Nob_File_Paths inputs = {0};
    bool ok = false;

    if (out_link_performed) *out_link_performed = false;

    for (size_t i = 0; i < object_paths->count; ++i) {
        nob_da_append(&inputs, object_paths->items[i]);
    }
    if (!append_runner_build_inputs(&inputs)) goto defer;

    if (!inputs_need_rebuild(output_path, &inputs)) {
        ok = true;
        goto defer;
    }
    if (out_link_performed) *out_link_performed = true;

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

static bool ensure_temp_tests_layout(const Test_Runner_Profile_Internal *profile) {
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

static bool build_incremental_test_binary(const char *output_path,
                                          Test_Runner_Context *ctx,
                                          Append_Source_List_Fn append_sources,
                                          const Test_Runner_Profile_Internal *profile) {
    Nob_Cmd sources = {0};
    Nob_File_Paths object_paths = {0};
    Nob_Procs procs = {0};
    bool ok = false;
    bool linked = false;
    size_t max_jobs = 0;
    size_t temp_mark = nob_temp_save();

    if (!ensure_temp_tests_layout(profile)) goto defer;
    test_runner_note_launcher_kind(ctx);

    append_sources(&sources);
    max_jobs = test_runner_effective_compile_jobs(sources.count);
    test_runner_note_compile_jobs(ctx, max_jobs);
    for (size_t i = 0; i < sources.count; ++i) {
        const char *source_path = sources.items[i];
        const char *object_path = test_object_path_temp(source_path, profile);
        const char *dep_path = test_dep_path_temp(source_path, profile);

        nob_da_append(&object_paths, object_path);
        if (max_jobs <= 1) {
            if (!build_object_file(source_path, object_path, dep_path, profile, ctx)) goto defer;
            continue;
        }
        if (!enqueue_object_build(&procs, max_jobs, source_path, object_path, dep_path, profile, ctx)) goto defer;
    }

    if (!nob_procs_flush(&procs)) goto defer;
    ok = link_test_binary(output_path, &object_paths, profile, &linked);
    test_runner_note_link_result(ctx, linked);

defer:
    nob_da_free(procs);
    nob_da_free(object_paths);
    nob_cmd_free(sources);
    nob_temp_rewind(temp_mark);
    return ok;
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
    copy = (char *)malloc(len + 1);
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

static bool parity_module_requires_cmake_3_28(const Test_Runner_Module_Internal *module) {
    if (!module) return false;
    return cstr_equals(module->def.name, "artifact-parity") ||
           cstr_equals(module->def.name, "artifact-parity-corpus") ||
           cstr_equals(module->def.name, "evaluator-codegen-diff");
}

static bool resolve_local_cmake_3_28_6(char out_path[_TINYDIR_PATH_MAX]) {
    const char *home = getenv("HOME");
    if (!out_path) return false;
    out_path[0] = '\0';
    if (!home || home[0] == '\0') return false;
    if (snprintf(out_path,
                 _TINYDIR_PATH_MAX,
                 "%s/.local/opt/cmake-3.28.6/bin/cmake",
                 home) >= _TINYDIR_PATH_MAX) {
        out_path[0] = '\0';
        return false;
    }
    return nob_file_exists(out_path);
}

static bool prepare_test_run_workspace(Test_Run_Workspace *ws,
                                       const Test_Runner_Module_Internal *module,
                                       const Test_Runner_Profile_Internal *profile) {
    static unsigned long long run_serial = 0;
    const char *cwd = nob_get_current_dir_temp();
    char root_rel[_TINYDIR_PATH_MAX] = {0};
    int n = 0;

    if (!ws || !module || !profile || !cwd) return false;
    memset(ws, 0, sizeof(*ws));

    run_serial++;
    n = snprintf(root_rel,
                 sizeof(root_rel),
                 "%s/%s-%s-%lu-%lld-%llu",
                 TEMP_TESTS_RUNS,
                 module->def.name,
                 profile->def.name,
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

static void report_captured_test_output(const Test_Runner_Module_Def *module,
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

static void test_runner_result_set_case_name(Test_Runner_Result *result, const char *case_name) {
    if (!result) return;
    (void)test_runner_copy_string(result->case_name, sizeof(result->case_name), case_name);
}

static void test_runner_result_clear_failure_summary(Test_Runner_Result *result) {
    if (!result) return;
    result->failure_summary[0] = '\0';
}

static bool test_runner_result_set_failure_summary(Test_Runner_Result *result,
                                                   const Nob_String_Builder *summary) {
    if (!result || !summary || summary->count == 0) return false;
    return test_runner_copy_string(result->failure_summary,
                                   sizeof(result->failure_summary),
                                   summary->items);
}

static bool test_runner_compose_unmatched_case_summary(Test_Runner_Result *result,
                                                       const char *case_name) {
    Nob_String_Builder summary = {0};
    bool ok = false;

    if (!result || !case_name || case_name[0] == '\0') return false;
    nob_sb_append_cstr(&summary, "failure summary:\n");
    nob_sb_appendf(&summary, "case=%s status=not-found\n", case_name);
    nob_sb_append_null(&summary);
    ok = test_runner_result_set_failure_summary(result, &summary);
    nob_sb_free(summary);
    return ok;
}

static bool test_runner_compose_failure_summary_from_file(Test_Runner_Result *result,
                                                          const char *summary_path,
                                                          const char *workspace_path) {
    Nob_String_Builder raw = {0};
    Nob_String_Builder summary = {0};
    char *line = NULL;
    char *line_state = NULL;
    bool ok = false;
    bool have_entries = false;
    size_t shown = 0;
    size_t hidden = 0;
    const size_t limit = 8;

    if (!result || !summary_path) return false;
    if (!nob_file_exists(summary_path)) return false;
    if (!nob_read_entire_file(summary_path, &raw) || raw.count == 0) goto defer;
    nob_sb_append_null(&raw);

    nob_sb_append_cstr(&summary, "failure summary:\n");
    if (workspace_path && workspace_path[0] != '\0') {
        nob_sb_appendf(&summary, "workspace=%s\n", workspace_path);
    }

    for (line = strtok_r(raw.items, "\n", &line_state);
         line;
         line = strtok_r(NULL, "\n", &line_state)) {
        char *field = NULL;
        char *field_state = NULL;
        char *case_name = NULL;
        char *file_name = NULL;
        char *line_value = NULL;
        long line_number = 0;

        for (field = strtok_r(line, "\t", &field_state);
             field;
             field = strtok_r(NULL, "\t", &field_state)) {
            if (starts_with(field, "case=")) case_name = field + strlen("case=");
            else if (starts_with(field, "file=")) file_name = field + strlen("file=");
            else if (starts_with(field, "line=")) line_value = field + strlen("line=");
        }

        if (!case_name || case_name[0] == '\0') continue;
        have_entries = true;
        if (line_value && line_value[0] != '\0') line_number = strtol(line_value, NULL, 10);

        if (shown < limit) {
            nob_sb_appendf(&summary, "case=%s", case_name);
            if (file_name && file_name[0] != '\0') {
                if (line_number > 0) nob_sb_appendf(&summary, " file=%s:%ld", file_name, line_number);
                else nob_sb_appendf(&summary, " file=%s", file_name);
            }
            nob_sb_append_cstr(&summary, "\n");
            shown++;
        } else {
            hidden++;
        }
    }

    if (!have_entries) goto defer;
    if (hidden > 0) nob_sb_appendf(&summary, "more=%zu\n", hidden);
    nob_sb_append_null(&summary);
    ok = test_runner_result_set_failure_summary(result, &summary);

defer:
    nob_sb_free(summary);
    nob_sb_free(raw);
    return ok;
}

static bool coverage_context_begin(Test_Runner_Context *ctx, const char *label) {
    const char *cwd = nob_get_current_dir_temp();
    char rel_dir[_TINYDIR_PATH_MAX] = {0};
    int n = 0;

    coverage_context_reset(ctx);
    if (!cwd || !label) return false;
    if (!ensure_temp_tests_layout(&TEST_RUNNER_PROFILES[TEST_RUNNER_PROFILE_COVERAGE])) return false;

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

    if (!build_abs_path(cwd, rel_dir, ctx->coverage.session_dir)) return false;
    if (!test_fs_remove_tree(ctx->coverage.session_dir)) return false;
    if (!ensure_dir_chain(ctx->coverage.session_dir)) return false;

    ctx->coverage.active = true;
    nob_log(NOB_INFO, "[v2] coverage session: %s", ctx->coverage.session_dir);
    return true;
}

static bool coverage_context_register_module(Test_Runner_Context *ctx,
                                             const Test_Runner_Module_Internal *module,
                                             const char *binary_rel_path) {
    char binary_abs[_TINYDIR_PATH_MAX] = {0};
    char profraw_abs[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = NULL;
    int n = 0;

    if (!ctx->coverage.active || !module || !binary_rel_path) return true;
    cwd = nob_get_current_dir_temp();
    if (!cwd) return false;
    if (!build_abs_path(cwd, binary_rel_path, binary_abs)) return false;
    n = snprintf(profraw_abs,
                 sizeof(profraw_abs),
                 "%s/%s.profraw",
                 ctx->coverage.session_dir,
                 module->def.name);
    if (n < 0 || n >= (int)sizeof(profraw_abs)) {
        nob_log(NOB_ERROR, "coverage raw profile path is too long for %s", module->def.name);
        return false;
    }

    if (!file_paths_append_unique_dup(&ctx->coverage.binary_paths, binary_abs)) return false;
    if (!file_paths_append_unique_dup(&ctx->coverage.profraw_paths, profraw_abs)) return false;
    return true;
}

static const char *coverage_profraw_path_for_module_temp(Test_Runner_Context *ctx,
                                                         const Test_Runner_Module_Internal *module) {
    if (!ctx->coverage.active || !module) return NULL;
    return nob_temp_sprintf("%s/%s.profraw", ctx->coverage.session_dir, module->def.name);
}

static bool run_command_capture_stdout(Nob_Cmd *cmd, const char *stdout_path) {
    if (!cmd || !stdout_path) return false;
    return nob_cmd_run(&cmd[0], .stdout_path = stdout_path);
}

static bool generate_coverage_report(Test_Runner_Context *ctx,
                                     const Test_Runner_Module_Internal *module,
                                     bool run_all) {
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

    if (!ctx->coverage.active) return true;
    if (ctx->coverage.binary_paths.count == 0 || ctx->coverage.profraw_paths.count == 0) {
        nob_log(NOB_ERROR, "[v2] no coverage artifacts were collected");
        goto defer;
    }
    if (!resolve_llvm_cov_path(llvm_cov) || !resolve_llvm_profdata_path(llvm_profdata)) goto defer;

    if (snprintf(profdata_path, sizeof(profdata_path), "%s/coverage.profdata", ctx->coverage.session_dir) >= (int)sizeof(profdata_path)) {
        nob_log(NOB_ERROR, "coverage profdata path is too long");
        goto defer;
    }
    if (snprintf(summary_path, sizeof(summary_path), "%s/summary.txt", ctx->coverage.session_dir) >= (int)sizeof(summary_path)) {
        nob_log(NOB_ERROR, "coverage summary path is too long");
        goto defer;
    }
    if (snprintf(html_dir, sizeof(html_dir), "%s/html", ctx->coverage.session_dir) >= (int)sizeof(html_dir)) {
        nob_log(NOB_ERROR, "coverage html path is too long");
        goto defer;
    }

    nob_cmd_append(&merge_cmd, llvm_profdata, "merge", "-sparse");
    for (size_t i = 0; i < ctx->coverage.profraw_paths.count; i++) {
        if (!nob_file_exists(ctx->coverage.profraw_paths.items[i])) continue;
        nob_cmd_append(&merge_cmd, ctx->coverage.profraw_paths.items[i]);
    }
    nob_cmd_append(&merge_cmd, "-o", profdata_path);
    if (!nob_cmd_run(&merge_cmd)) goto defer;

    nob_cmd_append(&report_cmd,
                   llvm_cov,
                   "report",
                   ctx->coverage.binary_paths.items[0],
                   "-instr-profile",
                   profdata_path,
                   "-ignore-filename-regex",
                   ignore_regex);
    for (size_t i = 1; i < ctx->coverage.binary_paths.count; i++) {
        nob_cmd_append(&report_cmd, "-object", ctx->coverage.binary_paths.items[i]);
    }
    if (!run_command_capture_stdout(&report_cmd, summary_path)) goto defer;

    if (!ensure_dir_chain(html_dir)) goto defer;
    nob_cmd_append(&show_cmd,
                   llvm_cov,
                   "show",
                   ctx->coverage.binary_paths.items[0],
                   "-format",
                   "html",
                   "-output-dir",
                   html_dir,
                   "-instr-profile",
                   profdata_path,
                   "-ignore-filename-regex",
                   ignore_regex);
    for (size_t i = 1; i < ctx->coverage.binary_paths.count; i++) {
        nob_cmd_append(&show_cmd, "-object", ctx->coverage.binary_paths.items[i]);
    }
    if (!nob_cmd_run(&show_cmd)) goto defer;

    nob_log(NOB_INFO,
            "[v2] coverage report generated for %s at %s (summary: %s, html: %s)",
            run_all ? "smoke" : module->def.name,
            ctx->coverage.session_dir,
            summary_path,
            html_dir);
    ok = true;

defer:
    nob_cmd_free(merge_cmd);
    nob_cmd_free(report_cmd);
    nob_cmd_free(show_cmd);
    return ok;
}

static bool run_binary_in_workspace(Test_Runner_Context *ctx,
                                    const Test_Runner_Module_Internal *module,
                                    const Test_Runner_Profile_Internal *profile,
                                    const char *binary_rel_path) {
    char cwd[_TINYDIR_PATH_MAX] = {0};
    char binary_abs[_TINYDIR_PATH_MAX] = {0};
    char stdout_log_abs[_TINYDIR_PATH_MAX] = {0};
    char stderr_log_abs[_TINYDIR_PATH_MAX] = {0};
    char case_match_abs[_TINYDIR_PATH_MAX] = {0};
    char failure_summary_abs[_TINYDIR_PATH_MAX] = {0};
    char nobify_tool_abs[_TINYDIR_PATH_MAX] = {0};
    char parity_cmake_bin_abs[_TINYDIR_PATH_MAX] = {0};
    char *prev_runner = NULL;
    char *prev_reuse_cwd = NULL;
    char *prev_repo_root = NULL;
    char *prev_nobify_bin = NULL;
    char *prev_cmake_bin = NULL;
    char *prev_case_filter = NULL;
    char *prev_case_match_path = NULL;
    char *prev_failure_summary_path = NULL;
    char *prev_asan_options = NULL;
    char *prev_ubsan_options = NULL;
    char *prev_msan_options = NULL;
    char *prev_llvm_profile_file = NULL;
    bool had_prev_runner = false;
    bool had_prev_reuse_cwd = false;
    bool had_prev_repo_root = false;
    bool had_prev_nobify_bin = false;
    bool had_prev_cmake_bin = false;
    bool had_prev_case_filter = false;
    bool had_prev_case_match_path = false;
    bool had_prev_failure_summary_path = false;
    bool had_prev_asan_options = false;
    bool had_prev_ubsan_options = false;
    bool had_prev_msan_options = false;
    bool had_prev_llvm_profile_file = false;
    Test_Run_Workspace workspace = {0};
    Nob_Cmd cmd = {0};
    bool ok = false;
    bool case_matched = true;
    bool cleanup_ok = true;
    bool workspace_preserved = false;

    if (!test_fs_save_current_dir(cwd)) goto defer;
    if (!build_abs_path(cwd, binary_rel_path, binary_abs)) goto defer;
    if (!prepare_test_run_workspace(&workspace, module, profile)) goto defer;
    if (!test_fs_join_path(workspace.root, "test.stdout.log", stdout_log_abs)) goto defer;
    if (!test_fs_join_path(workspace.root, "test.stderr.log", stderr_log_abs)) goto defer;
    if (!test_fs_join_path(workspace.root, "case_filter.match", case_match_abs)) goto defer;
    if (!test_fs_join_path(workspace.root, "failure_summary.txt", failure_summary_abs)) goto defer;
    if (nob_file_exists(case_match_abs) && !nob_delete_file(case_match_abs)) goto defer;
    if (nob_file_exists(failure_summary_abs) && !nob_delete_file(failure_summary_abs)) goto defer;

    if (!preserve_env_for_restore(CMK2NOB_TEST_RUNNER_ENV, &prev_runner, &had_prev_runner)) goto defer;
    if (!preserve_env_for_restore(CMK2NOB_TEST_WS_REUSE_CWD_ENV, &prev_reuse_cwd, &had_prev_reuse_cwd)) goto defer;
    if (!preserve_env_for_restore(CMK2NOB_TEST_REPO_ROOT_ENV, &prev_repo_root, &had_prev_repo_root)) goto defer;
    if (!preserve_env_for_restore(CMK2NOB_TEST_NOBIFY_BIN_ENV, &prev_nobify_bin, &had_prev_nobify_bin)) goto defer;
    if (!preserve_env_for_restore(CMK2NOB_TEST_CMAKE_BIN_ENV, &prev_cmake_bin, &had_prev_cmake_bin)) goto defer;
    if (!preserve_env_for_restore(CMK2NOB_TEST_CASE_FILTER_ENV, &prev_case_filter, &had_prev_case_filter)) goto defer;
    if (!preserve_env_for_restore(CMK2NOB_TEST_CASE_MATCH_PATH_ENV,
                                  &prev_case_match_path,
                                  &had_prev_case_match_path)) {
        goto defer;
    }
    if (!preserve_env_for_restore(CMK2NOB_TEST_FAILURE_SUMMARY_ENV,
                                  &prev_failure_summary_path,
                                  &had_prev_failure_summary_path)) {
        goto defer;
    }
    if (!preserve_env_for_restore("ASAN_OPTIONS", &prev_asan_options, &had_prev_asan_options)) goto defer;
    if (!preserve_env_for_restore("UBSAN_OPTIONS", &prev_ubsan_options, &had_prev_ubsan_options)) goto defer;
    if (!preserve_env_for_restore("MSAN_OPTIONS", &prev_msan_options, &had_prev_msan_options)) goto defer;
    if (!preserve_env_for_restore("LLVM_PROFILE_FILE", &prev_llvm_profile_file, &had_prev_llvm_profile_file)) goto defer;
    (void)had_prev_runner;
    (void)had_prev_reuse_cwd;
    (void)had_prev_repo_root;
    (void)had_prev_nobify_bin;
    (void)had_prev_cmake_bin;

    if (module &&
        (cstr_equals(module->def.name, "artifact-parity") ||
         cstr_equals(module->def.name, "artifact-parity-corpus"))) {
        const char *nobify_rel_path = test_nobify_output_path_temp(profile);
        if (!build_incremental_test_binary(nobify_rel_path,
                                           ctx,
                                           append_test_nobify_all_sources,
                                           profile)) {
            goto defer;
        }
        if (!build_abs_path(cwd, nobify_rel_path, nobify_tool_abs)) goto defer;
    }

    if (module && parity_module_requires_cmake_3_28(module)) {
        (void)resolve_local_cmake_3_28_6(parity_cmake_bin_abs);
    }

    if (!nob_set_current_dir(workspace.root)) goto defer;
    set_env_or_unset(CMK2NOB_TEST_RUNNER_ENV, "1");
    set_env_or_unset(CMK2NOB_TEST_WS_REUSE_CWD_ENV, "1");
    set_env_or_unset(CMK2NOB_TEST_REPO_ROOT_ENV, cwd);
    set_env_or_unset(CMK2NOB_TEST_NOBIFY_BIN_ENV,
                     nobify_tool_abs[0] != '\0' ? nobify_tool_abs : NULL);
    set_env_or_unset(CMK2NOB_TEST_CMAKE_BIN_ENV,
                     parity_cmake_bin_abs[0] != '\0' ? parity_cmake_bin_abs : NULL);
    set_env_or_unset(CMK2NOB_TEST_CASE_FILTER_ENV,
                     ctx && ctx->request && ctx->request->case_name[0] != '\0'
                         ? ctx->request->case_name
                         : NULL);
    set_env_or_unset(CMK2NOB_TEST_CASE_MATCH_PATH_ENV, case_match_abs);
    set_env_or_unset(CMK2NOB_TEST_FAILURE_SUMMARY_ENV, failure_summary_abs);
    if (!had_prev_asan_options && profile->asan_options_default) {
        set_env_or_unset("ASAN_OPTIONS", profile->asan_options_default);
    }
    if (!had_prev_ubsan_options && profile->ubsan_options_default) {
        set_env_or_unset("UBSAN_OPTIONS", profile->ubsan_options_default);
    }
    if (!had_prev_msan_options && profile->msan_options_default) {
        set_env_or_unset("MSAN_OPTIONS", profile->msan_options_default);
    }
    if (profile->use_coverage && ctx->coverage.active) {
        const char *profraw_path = coverage_profraw_path_for_module_temp(ctx, module);
        if (!profraw_path) goto defer;
        if (nob_file_exists(profraw_path) && !nob_delete_file(profraw_path)) goto defer;
        set_env_or_unset("LLVM_PROFILE_FILE", profraw_path);
    }

    nob_cmd_append(&cmd, binary_abs);
    if (ctx->verbose) {
        ok = nob_cmd_run(&cmd);
    } else {
        ok = nob_cmd_run(&cmd, .stdout_path = stdout_log_abs, .stderr_path = stderr_log_abs);
    }

    if (!nob_set_current_dir(cwd)) {
        nob_log(NOB_ERROR, "failed to restore current directory to %s", cwd);
        ok = false;
    }

    if (ctx && ctx->request && ctx->request->case_name[0] != '\0') {
        case_matched = nob_file_exists(case_match_abs);
        if (!case_matched) {
            nob_log(NOB_ERROR,
                    "[v2] case filter `%s` matched no cases in module %s",
                    ctx->request->case_name,
                    module->def.name);
            ok = false;
            if (ctx->out_result) {
                (void)test_runner_copy_string(ctx->out_result->summary,
                                              sizeof(ctx->out_result->summary),
                                              nob_temp_sprintf("module %s --case %s: FAIL",
                                                               module->def.name,
                                                               ctx->request->case_name));
                (void)test_runner_compose_unmatched_case_summary(ctx->out_result,
                                                                 ctx->request->case_name);
            }
        }
    }

    if (!ok && ctx && ctx->out_result && ctx->out_result->failure_summary[0] == '\0') {
        (void)test_runner_compose_failure_summary_from_file(ctx->out_result,
                                                            failure_summary_abs,
                                                            workspace.root);
    }

    if (!ok) {
        if (ctx && ctx->out_result) {
            (void)test_runner_copy_string(ctx->out_result->preserved_workspace_path,
                                          sizeof(ctx->out_result->preserved_workspace_path),
                                          workspace.root);
            if (!ctx->verbose) {
                (void)test_runner_copy_string(ctx->out_result->stdout_log_path,
                                              sizeof(ctx->out_result->stdout_log_path),
                                              stdout_log_abs);
                (void)test_runner_copy_string(ctx->out_result->stderr_log_path,
                                              sizeof(ctx->out_result->stderr_log_path),
                                              stderr_log_abs);
            }
        }
        if (!ctx->verbose) {
            report_captured_test_output(&module->def, &workspace, stdout_log_abs, stderr_log_abs);
        } else if (workspace.root[0] != '\0') {
            nob_log(NOB_ERROR, "[v2] preserved failed workspace: %s", workspace.root);
        }
        workspace_preserved = true;
    } else {
        cleanup_ok = cleanup_test_run_workspace(&workspace);
        if (!cleanup_ok) {
            nob_log(NOB_ERROR, "[v2] failed to cleanup run workspace for %s", module->def.name);
        }
    }

defer:
    set_env_or_unset(CMK2NOB_TEST_RUNNER_ENV, prev_runner);
    set_env_or_unset(CMK2NOB_TEST_WS_REUSE_CWD_ENV, prev_reuse_cwd);
    set_env_or_unset(CMK2NOB_TEST_REPO_ROOT_ENV, prev_repo_root);
    set_env_or_unset(CMK2NOB_TEST_NOBIFY_BIN_ENV, had_prev_nobify_bin ? prev_nobify_bin : NULL);
    set_env_or_unset(CMK2NOB_TEST_CMAKE_BIN_ENV, had_prev_cmake_bin ? prev_cmake_bin : NULL);
    set_env_or_unset(CMK2NOB_TEST_CASE_FILTER_ENV, had_prev_case_filter ? prev_case_filter : NULL);
    set_env_or_unset(CMK2NOB_TEST_CASE_MATCH_PATH_ENV,
                     had_prev_case_match_path ? prev_case_match_path : NULL);
    set_env_or_unset(CMK2NOB_TEST_FAILURE_SUMMARY_ENV,
                     had_prev_failure_summary_path ? prev_failure_summary_path : NULL);
    if (profile->asan_options_default) {
        set_env_or_unset("ASAN_OPTIONS", had_prev_asan_options ? prev_asan_options : NULL);
    }
    if (profile->ubsan_options_default) {
        set_env_or_unset("UBSAN_OPTIONS", had_prev_ubsan_options ? prev_ubsan_options : NULL);
    }
    if (profile->msan_options_default) {
        set_env_or_unset("MSAN_OPTIONS", had_prev_msan_options ? prev_msan_options : NULL);
    }
    if (profile->use_coverage) {
        set_env_or_unset("LLVM_PROFILE_FILE", had_prev_llvm_profile_file ? prev_llvm_profile_file : NULL);
    }
    free(prev_runner);
    free(prev_reuse_cwd);
    free(prev_repo_root);
    free(prev_nobify_bin);
    free(prev_cmake_bin);
    free(prev_case_filter);
    free(prev_case_match_path);
    free(prev_failure_summary_path);
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

static bool run_test_module(Test_Runner_Context *ctx,
                            const Test_Runner_Module_Internal *module,
                            const Test_Runner_Profile_Internal *profile) {
    bool ok = false;
    bool lock_acquired = false;
    size_t temp_mark = nob_temp_save();
    const char *binary_rel_path = test_binary_output_path_temp(module, profile);
    const char *lock_path = test_build_lock_path_temp(profile);

    nob_log(NOB_INFO, "[v2] build+run %s (%s)", module->def.name, profile->def.name);

    lock_acquired = acquire_build_lock(lock_path);
    if (!lock_acquired) goto defer;
    if (!build_incremental_test_binary(binary_rel_path, ctx, module->append_sources, profile)) goto defer;
    if (profile->use_coverage && !coverage_context_register_module(ctx, module, binary_rel_path)) goto defer;
    if (!release_build_lock(lock_path)) {
        lock_acquired = false;
        goto defer;
    }
    lock_acquired = false;

    ok = run_binary_in_workspace(ctx, module, profile, binary_rel_path);

defer:
    if (lock_acquired) {
        (void)release_build_lock(lock_path);
    }
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool run_all_test_modules(Test_Runner_Context *ctx,
                                 const Test_Runner_Profile_Internal *profile) {
    size_t passed_modules = 0;
    size_t failed_modules = 0;
    size_t skipped_modules = 0;

    for (size_t i = 0; i < NOB_ARRAY_LEN(TEST_RUNNER_MODULES); i++) {
        const Test_Runner_Module_Internal *module = &TEST_RUNNER_MODULES[i];
        bool ok = false;
        if (!module->def.include_in_aggregate) {
            skipped_modules++;
            continue;
        }

        ok = run_test_module(ctx, module, profile);
        if (ok) {
            passed_modules++;
            nob_log(NOB_INFO, "[v2] module %s: PASS", module->def.name);
        } else {
            failed_modules++;
            nob_log(NOB_ERROR, "[v2] module %s: FAIL", module->def.name);
        }
    }

    nob_log(NOB_INFO,
            "[v2] summary: passed_modules=%zu failed_modules=%zu skipped_modules=%zu",
            passed_modules,
            failed_modules,
            skipped_modules);
    test_runner_result_set_summary(ctx,
                                   "aggregate: passed=%zu failed=%zu skipped=%zu",
                                   passed_modules,
                                   failed_modules,
                                   skipped_modules);
    return failed_modules == 0;
}

static bool collect_module_sources_unique(const Test_Runner_Module_Internal *module,
                                          Nob_File_Paths *out_sources) {
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
    if (!out_sources) return false;
    for (size_t i = 0; i < NOB_ARRAY_LEN(TEST_RUNNER_MODULES); i++) {
        if (!TEST_RUNNER_MODULES[i].def.include_in_aggregate) continue;
        if (!collect_module_sources_unique(&TEST_RUNNER_MODULES[i], out_sources)) return false;
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
        append_v2_common_flags(&cmd, &TEST_RUNNER_PROFILES[TEST_RUNNER_PROFILE_DEFAULT]);
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

static bool run_tidy_command(const Test_Runner_Module_Internal *module, bool run_all) {
    Nob_File_Paths sources = {0};
    bool ok = false;

    if (!ensure_temp_tests_layout(&TEST_RUNNER_PROFILES[TEST_RUNNER_PROFILE_DEFAULT])) return false;
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

static void test_runner_result_set_module_status_summary(Test_Runner_Context *ctx,
                                                         const Test_Runner_Module_Internal *module,
                                                         const Test_Runner_Request *request,
                                                         bool ok) {
    const char *module_name = module ? module->def.name : "unknown";

    if (!ctx || !request) return;
    if (request->case_name[0] != '\0') {
        test_runner_result_set_summary(ctx,
                                       "module %s --case %s: %s",
                                       module_name,
                                       request->case_name,
                                       ok ? "PASS" : "FAIL");
        return;
    }

    test_runner_result_set_summary(ctx, "module %s: %s", module_name, ok ? "PASS" : "FAIL");
}

bool test_runner_execute(const Test_Runner_Request *request,
                         Test_Runner_Result *out_result) {
    const Test_Runner_Module_Internal *module = NULL;
    const Test_Runner_Profile_Internal *profile = NULL;
    Test_Runner_Context ctx = {0};
    bool ok = false;
    bool coverage_started = false;

    if (out_result) *out_result = (Test_Runner_Result){0};
    if (!request) return false;

    if (request->action != TEST_RUNNER_ACTION_CLEAN) {
        if ((size_t)request->profile_id >= NOB_ARRAY_LEN(TEST_RUNNER_PROFILES)) {
            nob_log(NOB_ERROR, "invalid test profile id: %d", (int)request->profile_id);
            return false;
        }
        profile = &TEST_RUNNER_PROFILES[request->profile_id];
    }

    if (request->action == TEST_RUNNER_ACTION_RUN_MODULE ||
        request->action == TEST_RUNNER_ACTION_RUN_TIDY_MODULE) {
        if ((size_t)request->module_id >= NOB_ARRAY_LEN(TEST_RUNNER_MODULES)) {
            nob_log(NOB_ERROR, "invalid test module id: %d", (int)request->module_id);
            return false;
        }
        module = &TEST_RUNNER_MODULES[request->module_id];
    }

    ctx.verbose = request->verbose;
    ctx.request = request;
    ctx.out_result = out_result;
    if (out_result) {
        test_runner_result_set_case_name(out_result, request->case_name);
        test_runner_result_clear_failure_summary(out_result);
    }
    g_active_runner_ctx = &ctx;
    nob_set_log_handler(runner_log_handler);

    switch (request->action) {
        case TEST_RUNNER_ACTION_CLEAN:
            if (nob_file_exists(TEMP_TESTS_DAEMON_SOCKET) || nob_file_exists(TEMP_TESTS_DAEMON_PID)) {
                nob_log(NOB_ERROR,
                        "`./build/nob test clean` is unavailable while the test daemon is active; stop `./build/nob test daemon` first");
                ok = false;
                test_runner_result_set_summary(&ctx, "clean: fail");
                break;
            }
            ok = test_fs_remove_tree(TEMP_TESTS_ROOT);
            test_runner_result_set_summary(&ctx, "clean: %s", ok ? "pass" : "fail");
            break;

        case TEST_RUNNER_ACTION_RUN_TIDY_AGGREGATE:
            ok = run_tidy_command(NULL, true);
            test_runner_result_set_summary(&ctx, "tidy all: %s", ok ? "pass" : "fail");
            break;

        case TEST_RUNNER_ACTION_RUN_TIDY_MODULE:
            ok = run_tidy_command(module, false);
            test_runner_result_set_summary(&ctx,
                                           "tidy %s: %s",
                                           module ? module->def.name : "unknown",
                                           ok ? "pass" : "fail");
            break;

        case TEST_RUNNER_ACTION_RUN_AGGREGATE:
        case TEST_RUNNER_ACTION_RUN_MODULE:
            if (request->skip_preflight) {
                if (!ensure_temp_tests_layout(profile)) break;
            } else if (!run_test_preflight(&ctx, profile)) {
                break;
            }
            if (profile->use_coverage) {
                const char *label = request->action == TEST_RUNNER_ACTION_RUN_AGGREGATE ? "smoke" : module->def.name;
                if (!coverage_context_begin(&ctx, label)) break;
                coverage_started = true;
            }

            if (request->action == TEST_RUNNER_ACTION_RUN_AGGREGATE) {
                ok = run_all_test_modules(&ctx, profile);
                if (coverage_started && ok) {
                    ok = generate_coverage_report(&ctx, module, true);
                }
            } else {
                ok = run_test_module(&ctx, module, profile);
                nob_log(ok ? NOB_INFO : NOB_ERROR,
                        "[v2] module %s: %s",
                        module->def.name,
                        ok ? "PASS" : "FAIL");
                test_runner_result_set_module_status_summary(&ctx, module, request, ok);
                if (coverage_started && ok) {
                    ok = generate_coverage_report(&ctx, module, false);
                }
            }
            break;
    }

    if (coverage_started) coverage_context_reset(&ctx);
    g_active_runner_ctx = NULL;
    if (out_result) {
        if (out_result->summary[0] == '\0') {
            switch (request->action) {
                case TEST_RUNNER_ACTION_RUN_AGGREGATE:
                    test_runner_result_set_summary(&ctx, "aggregate: %s", ok ? "PASS" : "FAIL");
                    break;
                case TEST_RUNNER_ACTION_RUN_MODULE:
                    test_runner_result_set_module_status_summary(&ctx, module, request, ok);
                    break;
                default:
                    test_runner_result_set_summary(&ctx, "runner action %d: %s", (int)request->action, ok ? "PASS" : "FAIL");
                    break;
            }
        }
        out_result->ok = ok;
        out_result->exit_code = ok ? 0 : 1;
    }
    return ok;
}
