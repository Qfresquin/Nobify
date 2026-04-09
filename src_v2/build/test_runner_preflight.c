static bool path_is_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    return access(path, X_OK) == 0;
#endif
}

bool test_runner_preflight_fingerprint(uint64_t *out_fingerprint) {
    static const char *const inputs[] = {
        "src_v2/build/test_runner_core.h",
        "src_v2/build/test_runner_exec.c",
        "src_v2/build/test_runner_preflight.c",
        "src_v2/build/test_runner_registry.c",
        "test_v2/evaluator/check_result_type_conventions.sh",
        "test_v2/test_v2_suite.h",
        "test_v2/test_workspace.h",
    };
    uint64_t hash = 1469598103934665603ull;

    if (!out_fingerprint) return false;
    for (size_t i = 0; i < NOB_ARRAY_LEN(inputs); ++i) {
        struct stat st = {0};
        uint64_t value = 0;
        if (stat(inputs[i], &st) < 0) {
            nob_log(NOB_ERROR, "could not stat preflight input %s: %s", inputs[i], strerror(errno));
            return false;
        }
        value = ((uint64_t)TEST_STAT_MTIME_SEC(st) << 32) ^
                (uint64_t)TEST_STAT_MTIME_NSEC(st) ^
                (uint64_t)st.st_size;
        hash = (hash * 1099511628211ull) ^ value;
    }
    *out_fingerprint = hash;
    return true;
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

bool test_runner_resolve_coverage_tools(char out_llvm_cov[TEST_RUNNER_PATH_CAPACITY],
                                        char out_llvm_profdata[TEST_RUNNER_PATH_CAPACITY]) {
    char llvm_cov[_TINYDIR_PATH_MAX] = {0};
    char llvm_profdata[_TINYDIR_PATH_MAX] = {0};

    if (!out_llvm_cov || !out_llvm_profdata) return false;
    out_llvm_cov[0] = '\0';
    out_llvm_profdata[0] = '\0';

    if (!resolve_llvm_cov_path(llvm_cov)) return false;
    if (!resolve_llvm_profdata_path(llvm_profdata)) return false;
    return test_runner_copy_string(out_llvm_cov, TEST_RUNNER_PATH_CAPACITY, llvm_cov) &&
           test_runner_copy_string(out_llvm_profdata, TEST_RUNNER_PATH_CAPACITY, llvm_profdata);
}

static bool validate_test_profile_support(const Test_Runner_Profile_Internal *profile) {
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

    probe_source = nob_temp_sprintf("%s/%s_probe.c", TEMP_TESTS_PROBES, profile->def.name);
    probe_binary = nob_temp_sprintf("%s/%s_probe", TEMP_TESTS_PROBES, profile->def.name);
    if (!ensure_parent_dir(probe_source) || !ensure_parent_dir(probe_binary)) goto defer;
    if (!nob_write_entire_file(probe_source, probe_program, strlen(probe_program))) goto defer;

    nob_log(NOB_INFO, "[v2] validate profile %s", profile->def.name);
    append_test_profile_compiler(&cmd, profile);
    append_test_profile_compile_flags(&cmd, profile);
    append_test_profile_link_flags(&cmd, profile);
    nob_cmd_append(&cmd, probe_source, "-o", probe_binary);
    ok = nob_cmd_run(&cmd);
    if (!ok) {
        nob_log(NOB_ERROR, "[v2] toolchain does not support profile %s", profile->def.name);
    }

defer:
    nob_cmd_free(cmd);
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool validate_coverage_tools_support(const Test_Runner_Profile_Internal *profile) {
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

static bool run_result_type_conventions_check(Test_Runner_Context *ctx) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    size_t temp_mark = nob_temp_save();
    nob_log(NOB_INFO, "[v2] check result type conventions");
    nob_cmd_append(&cmd, "bash", "test_v2/evaluator/check_result_type_conventions.sh");

    if (ctx && ctx->verbose) {
        ok = nob_cmd_run(&cmd);
    } else {
        const char *stdout_path = nob_temp_sprintf("%s/result_type_check.stdout.log", TEMP_TESTS_PROBES);
        const char *stderr_path = nob_temp_sprintf("%s/result_type_check.stderr.log", TEMP_TESTS_PROBES);
        ok = nob_cmd_run(&cmd, .stdout_path = stdout_path, .stderr_path = stderr_path);
        if (!ok) {
            report_captured_test_output(
                &(Test_Runner_Module_Def){ .name = "result-type-conventions" },
                NULL,
                stdout_path,
                stderr_path);
        }
    }

    nob_cmd_free(cmd);
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool run_test_preflight(Test_Runner_Context *ctx,
                               const Test_Runner_Profile_Internal *profile) {
    if (!ensure_temp_tests_layout(profile)) return false;
    if (!run_result_type_conventions_check(ctx)) return false;
    nob_log(NOB_INFO, "[v2] validate workspace infra");
    if (!validate_coverage_tools_support(profile)) return false;
    return validate_test_profile_support(profile);
}

bool test_runner_run_preflight_for_profile(Test_Runner_Profile_Id profile_id, bool verbose) {
    const Test_Runner_Profile_Internal *profile = NULL;
    Test_Runner_Context ctx = {0};
    bool ok = false;

    if ((size_t)profile_id >= NOB_ARRAY_LEN(TEST_RUNNER_PROFILES)) {
        nob_log(NOB_ERROR, "invalid test profile id for preflight: %d", (int)profile_id);
        return false;
    }

    profile = &TEST_RUNNER_PROFILES[profile_id];
    ctx.verbose = verbose;
    g_active_runner_ctx = &ctx;
    nob_set_log_handler(runner_log_handler);
    ok = run_test_preflight(&ctx, profile);
    g_active_runner_ctx = NULL;
    return ok;
}
