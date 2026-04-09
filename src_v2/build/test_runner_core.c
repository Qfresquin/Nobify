#include "test_runner_core.h"

#include "nob.h"
#include "test_fs.h"
#include "test_workspace.h"

#include <errno.h>
#include <stdarg.h>
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
#define TEMP_TESTS_DAEMON_ROOT TEMP_TESTS_ROOT "/daemon"
#define TEMP_TESTS_DAEMON_SOCKET TEMP_TESTS_DAEMON_ROOT "/nob_testd.sock"
#define TEMP_TESTS_DAEMON_PID TEMP_TESTS_DAEMON_ROOT "/nob_testd.pid"

#define TEST_RUNNER_CORE_FILE "src_v2/build/test_runner_core.c"
#define TEST_RUNNER_REGISTRY_FILE "src_v2/build/test_runner_registry.c"
#define TEST_RUNNER_EXEC_FILE "src_v2/build/test_runner_exec.c"
#define TEST_RUNNER_PREFLIGHT_FILE "src_v2/build/test_runner_preflight.c"
#define TEST_RUNNER_CORE_HEADER "src_v2/build/test_runner_core.h"

typedef void (*Append_Source_List_Fn)(Nob_Cmd *cmd);

typedef struct {
    Test_Runner_Module_Def def;
    Append_Source_List_Fn append_sources;
} Test_Runner_Module_Internal;

typedef struct {
    Test_Runner_Profile_Def def;
    bool use_asan;
    bool use_ubsan;
    bool use_msan;
    bool use_coverage;
    const char *asan_options_default;
    const char *ubsan_options_default;
    const char *msan_options_default;
} Test_Runner_Profile_Internal;

typedef struct {
    char root[_TINYDIR_PATH_MAX];
    char suite_copy[_TINYDIR_PATH_MAX];
} Test_Run_Workspace;

typedef struct {
    bool active;
    char session_dir[_TINYDIR_PATH_MAX];
    Nob_File_Paths binary_paths;
    Nob_File_Paths profraw_paths;
} Test_Runner_Coverage_Context;

typedef struct {
    bool verbose;
    Test_Runner_Coverage_Context coverage;
    const Test_Runner_Request *request;
    Test_Runner_Result *out_result;
} Test_Runner_Context;

static Test_Runner_Context *g_active_runner_ctx = NULL;

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

Test_Runner_Launcher_Kind test_runner_classify_launcher_kind(const char *launcher_path) {
    const char *base = launcher_path;

    if (!launcher_path || launcher_path[0] == '\0') return TEST_RUNNER_LAUNCHER_NONE;
    if (strrchr(launcher_path, '/')) base = strrchr(launcher_path, '/') + 1;
    if (cstr_equals(base, "ccache")) return TEST_RUNNER_LAUNCHER_CCACHE;
    if (cstr_equals(base, "sccache")) return TEST_RUNNER_LAUNCHER_SCCACHE;
    return TEST_RUNNER_LAUNCHER_CUSTOM;
}

const char *test_runner_launcher_kind_name(Test_Runner_Launcher_Kind kind) {
    switch (kind) {
        case TEST_RUNNER_LAUNCHER_NONE: return "none";
        case TEST_RUNNER_LAUNCHER_CCACHE: return "ccache";
        case TEST_RUNNER_LAUNCHER_SCCACHE: return "sccache";
        case TEST_RUNNER_LAUNCHER_CUSTOM: return "custom";
    }
    return "unknown";
}

const char *test_runner_select_launcher_candidate(const char *override_value,
                                                  bool have_ccache,
                                                  bool have_sccache) {
    if (override_value && override_value[0] != '\0') return override_value;
    if (have_ccache) return "ccache";
    if (have_sccache) return "sccache";
    return NULL;
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
        free((void *)paths->items[i]);
    }
    nob_da_free(*paths);
}

static void coverage_context_reset(Test_Runner_Context *ctx) {
    if (!ctx) return;
    file_paths_free_owned(&ctx->coverage.binary_paths);
    file_paths_free_owned(&ctx->coverage.profraw_paths);
    memset(ctx->coverage.session_dir, 0, sizeof(ctx->coverage.session_dir));
    ctx->coverage.active = false;
}

static bool append_runner_owned_input(Nob_File_Paths *inputs, const char *path) {
    return file_paths_append_unique_dup(inputs, path);
}

static bool append_runner_build_inputs(Nob_File_Paths *inputs) {
    if (!inputs) return false;
    return append_runner_owned_input(inputs, TEST_RUNNER_CORE_HEADER) &&
           append_runner_owned_input(inputs, TEST_RUNNER_CORE_FILE) &&
           append_runner_owned_input(inputs, TEST_RUNNER_REGISTRY_FILE) &&
           append_runner_owned_input(inputs, TEST_RUNNER_EXEC_FILE) &&
           append_runner_owned_input(inputs, TEST_RUNNER_PREFLIGHT_FILE);
}

static bool test_runner_copy_string(char *dst, size_t dst_size, const char *src) {
    int n = 0;
    if (!dst || dst_size == 0) return false;
    if (!src) {
        dst[0] = '\0';
        return true;
    }
    n = snprintf(dst, dst_size, "%s", src);
    if (n < 0 || (size_t)n >= dst_size) return false;
    return true;
}

static void test_runner_result_set_summary(Test_Runner_Context *ctx, const char *fmt, ...) {
    va_list args;
    int n = 0;

    if (!ctx || !ctx->out_result || !fmt) return;

    va_start(args, fmt);
    n = vsnprintf(ctx->out_result->summary, sizeof(ctx->out_result->summary), fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(ctx->out_result->summary)) {
        ctx->out_result->summary[0] = '\0';
    }
}

static const Test_Runner_Module_Internal *find_test_module_internal(const char *name);
static const Test_Runner_Profile_Internal *find_test_profile_by_front_door_flag(const char *flag);
static bool resolve_clang_tidy_path(char out_path[_TINYDIR_PATH_MAX]);
static bool resolve_llvm_cov_path(char out_path[_TINYDIR_PATH_MAX]);
static bool resolve_llvm_profdata_path(char out_path[_TINYDIR_PATH_MAX]);

static bool run_test_preflight(Test_Runner_Context *ctx,
                               const Test_Runner_Profile_Internal *profile);
static void test_runner_result_set_summary(Test_Runner_Context *ctx, const char *fmt, ...);
static bool test_runner_copy_string(char *dst, size_t dst_size, const char *src);

#include "test_runner_registry.c"
#include "test_runner_exec.c"
#include "test_runner_preflight.c"
#include "test_runner_front_door.c"
