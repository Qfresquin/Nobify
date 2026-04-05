#include "test_artifact_parity_v2_support.h"

#include "test_fs.h"
#include "test_host_fixture_support.h"
#include "test_v2_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *archive_relpath;
    const char *consumer_relpath;
    bool consumer_uses_shared_runtime;
} Artifact_Parity_Corpus_Project;

static Artifact_Parity_Cmake_Config s_corpus_cmake = {0};
static char s_corpus_skip_reason[256] = {0};
static char s_corpus_nobify_bin[_TINYDIR_PATH_MAX] = {0};
static char s_corpus_nobify_error[256] = {0};

static const Artifact_Parity_Corpus_Project s_corpus_projects[] = {
    {
        .name = "fmt",
        .archive_relpath = "test_v2/artifact_parity/real_projects/archives/fmt.tar.gz",
        .consumer_relpath = "test_v2/artifact_parity/real_projects/consumers/fmt",
        .consumer_uses_shared_runtime = true,
    },
    {
        .name = "pugixml",
        .archive_relpath = "test_v2/artifact_parity/real_projects/archives/pugixml.tar.gz",
        .consumer_relpath = "test_v2/artifact_parity/real_projects/consumers/pugixml",
        .consumer_uses_shared_runtime = true,
    },
    {
        .name = "nlohmann_json",
        .archive_relpath = "test_v2/artifact_parity/real_projects/archives/nlohmann_json.tar.gz",
        .consumer_relpath = "test_v2/artifact_parity/real_projects/consumers/nlohmann_json",
        .consumer_uses_shared_runtime = false,
    },
    {
        .name = "cjson",
        .archive_relpath = "test_v2/artifact_parity/real_projects/archives/cjson.tar.gz",
        .consumer_relpath = "test_v2/artifact_parity/real_projects/consumers/cjson",
        .consumer_uses_shared_runtime = true,
    },
};

static bool corpus_join_abs_from_repo(const char *repo_relpath,
                                      char out_path[_TINYDIR_PATH_MAX]) {
    const char *repo_root = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    if (!repo_root || repo_root[0] == '\0' || !repo_relpath || !out_path) return false;
    return snprintf(out_path, _TINYDIR_PATH_MAX, "%s/%s", repo_root, repo_relpath) < _TINYDIR_PATH_MAX;
}

static bool corpus_copy_tree_from_repo(const char *repo_relpath, const char *dst_path) {
    char src_path[_TINYDIR_PATH_MAX] = {0};
    const char *parent_dir = NULL;
    if (!repo_relpath || !dst_path) return false;
    if (!corpus_join_abs_from_repo(repo_relpath, src_path)) return false;
    parent_dir = nob_temp_dir_name(dst_path);
    if (parent_dir && strcmp(parent_dir, ".") != 0 && !nob_mkdir_if_not_exists(parent_dir)) return false;
    return nob_copy_directory_recursively(src_path, dst_path);
}

static bool corpus_extract_archive_from_repo(const char *repo_relpath, const char *dst_path) {
    char archive_path[_TINYDIR_PATH_MAX] = {0};
    Nob_Cmd cmd = {0};
    if (!repo_relpath || !dst_path) return false;
    if (!corpus_join_abs_from_repo(repo_relpath, archive_path)) return false;
    if (!nob_mkdir_if_not_exists(dst_path)) return false;
    nob_cmd_append(&cmd, "tar", "-xzf", archive_path, "-C", dst_path);
    return nob_cmd_run_sync_and_reset(&cmd);
}

static bool corpus_capture_tree_manifest(Arena *arena,
                                         const char *base_dir,
                                         const char *relpath,
                                         const char *label,
                                         String_View *out) {
    Artifact_Parity_Manifest_Request request = {
        .domain = ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS,
        .capture = ARTIFACT_PARITY_CAPTURE_TREE,
        .label = label,
        .relpath = relpath,
    };
    return artifact_parity_capture_manifest(arena, base_dir, &request, 1, out);
}

static bool corpus_run_consumer_configure(const Artifact_Parity_Cmake_Config *config,
                                          const char *source_dir,
                                          const char *binary_dir,
                                          const char *prefix_path) {
    Nob_Cmd cmd = {0};
    if (!config || !config->available || !source_dir || !binary_dir || !prefix_path) return false;
    nob_cmd_append(&cmd,
                   config->cmake_bin,
                   "-S",
                   source_dir,
                   "-B",
                   binary_dir,
                   nob_temp_sprintf("-DCMAKE_PREFIX_PATH=%s", prefix_path));
    return nob_cmd_run_sync_and_reset(&cmd);
}

static bool corpus_run_consumer_build(const Artifact_Parity_Cmake_Config *config,
                                      const char *binary_dir) {
    Nob_Cmd cmd = {0};
    if (!config || !config->available || !binary_dir) return false;
    nob_cmd_append(&cmd, config->cmake_bin, "--build", binary_dir);
    return nob_cmd_run_sync_and_reset(&cmd);
}

static bool corpus_run_consumer_binary(const Artifact_Parity_Corpus_Project *project,
                                       const char *binary_dir,
                                       const char *prefix_dir) {
    char runtime_dir[_TINYDIR_PATH_MAX] = {0};
    char runtime_env[_TINYDIR_PATH_MAX * 2] = {0};
    const char *old_value = NULL;
    Test_Host_Env_Guard *env_guard = NULL;
    bool ok = false;
    if (!project || !binary_dir || !prefix_dir) return false;

    if (project->consumer_uses_shared_runtime) {
        if (!test_fs_join_path(prefix_dir, "lib", runtime_dir)) return false;
        old_value = getenv("LD_LIBRARY_PATH");
        if (old_value && old_value[0] != '\0') {
            if (snprintf(runtime_env, sizeof(runtime_env), "%s:%s", runtime_dir, old_value) >= (int)sizeof(runtime_env)) {
                return false;
            }
        } else {
            if (snprintf(runtime_env, sizeof(runtime_env), "%s", runtime_dir) >= (int)sizeof(runtime_env)) return false;
        }
        if (!test_host_env_guard_begin_heap(&env_guard, "LD_LIBRARY_PATH", runtime_env)) return false;
    }

    ok = artifact_parity_run_binary_in_dir(binary_dir, "artifacts/bin/consumer", NULL, NULL);
    test_host_env_guard_cleanup(env_guard);
    return ok;
}

static bool corpus_run_project(const Artifact_Parity_Corpus_Project *project) {
    Arena *arena = NULL;
    String_View cmake_build_manifest = {0};
    String_View nob_build_manifest = {0};
    String_View cmake_install_manifest = {0};
    String_View nob_install_manifest = {0};
    char snapshot_dst[_TINYDIR_PATH_MAX] = {0};
    char consumer_dst[_TINYDIR_PATH_MAX] = {0};
    char cmake_install_abs[_TINYDIR_PATH_MAX] = {0};
    char nob_install_abs[_TINYDIR_PATH_MAX] = {0};
    char input_abs[_TINYDIR_PATH_MAX] = {0};
    char output_abs[_TINYDIR_PATH_MAX] = {0};
    char source_root_abs[_TINYDIR_PATH_MAX] = {0};
    char binary_root_abs[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    if (!project || !cwd) return false;
    if (s_corpus_nobify_bin[0] == '\0' || !s_corpus_cmake.available) return false;

    if (snprintf(snapshot_dst, sizeof(snapshot_dst), "%s_project", project->name) >= (int)sizeof(snapshot_dst) ||
        snprintf(consumer_dst, sizeof(consumer_dst), "%s_consumer", project->name) >= (int)sizeof(consumer_dst) ||
        snprintf(cmake_install_abs, sizeof(cmake_install_abs), "%s/%s_cmake_install", cwd, project->name) >= (int)sizeof(cmake_install_abs) ||
        snprintf(nob_install_abs, sizeof(nob_install_abs), "%s/%s_project/install", cwd, project->name) >= (int)sizeof(nob_install_abs) ||
        snprintf(input_abs, sizeof(input_abs), "%s/%s_project/CMakeLists.txt", cwd, project->name) >= (int)sizeof(input_abs) ||
        snprintf(output_abs, sizeof(output_abs), "%s/%s_project/nob.c", cwd, project->name) >= (int)sizeof(output_abs) ||
        snprintf(source_root_abs, sizeof(source_root_abs), "%s/%s_project", cwd, project->name) >= (int)sizeof(source_root_abs) ||
        snprintf(binary_root_abs, sizeof(binary_root_abs), "%s/%s_nob_build", cwd, project->name) >= (int)sizeof(binary_root_abs)) {
        return false;
    }

    if (!corpus_extract_archive_from_repo(project->archive_relpath, snapshot_dst) ||
        !corpus_copy_tree_from_repo(project->consumer_relpath, consumer_dst)) {
        return false;
    }

    if (!artifact_parity_run_cmake_configure(&s_corpus_cmake,
                                             snapshot_dst,
                                             nob_temp_sprintf("%s_cmake_build", project->name),
                                             NULL) ||
        !artifact_parity_run_cmake_build(&s_corpus_cmake,
                                         nob_temp_sprintf("%s_cmake_build", project->name),
                                         NULL) ||
        !artifact_parity_run_cmake_install(&s_corpus_cmake,
                                           nob_temp_sprintf("%s_cmake_build", project->name),
                                           nob_temp_sprintf("%s_cmake_install", project->name))) {
        return false;
    }

    if (!artifact_parity_run_nobify(s_corpus_nobify_bin,
                                    input_abs,
                                    output_abs,
                                    source_root_abs,
                                    binary_root_abs) ||
        !artifact_parity_compile_generated_nob(nob_temp_sprintf("%s_project/nob.c", project->name),
                                               nob_temp_sprintf("%s_project/nob_gen", project->name)) ||
        !artifact_parity_run_binary_in_dir(nob_temp_sprintf("%s_project", project->name), "./nob_gen", NULL, NULL) ||
        !artifact_parity_run_binary_in_dir(nob_temp_sprintf("%s_project", project->name), "./nob_gen", "install", NULL)) {
        return false;
    }

    arena = arena_create(2 * 1024 * 1024);
    if (!arena) return false;
    if (!corpus_capture_tree_manifest(arena,
                                      nob_temp_sprintf("%s_cmake_build", project->name),
                                      "artifacts",
                                      "build_artifacts",
                                      &cmake_build_manifest) ||
        !corpus_capture_tree_manifest(arena,
                                      nob_temp_sprintf("%s_nob_build", project->name),
                                      "artifacts",
                                      "build_artifacts",
                                      &nob_build_manifest) ||
        !artifact_parity_assert_equal_manifests(arena,
                                                nob_temp_sprintf("%s_build_outputs", project->name),
                                                cmake_build_manifest,
                                                nob_build_manifest) ||
        !corpus_capture_tree_manifest(arena,
                                      nob_temp_sprintf("%s_cmake_install", project->name),
                                      "",
                                      "install_tree",
                                      &cmake_install_manifest) ||
        !corpus_capture_tree_manifest(arena,
                                      nob_temp_sprintf("%s_project/install", project->name),
                                      "",
                                      "install_tree",
                                      &nob_install_manifest) ||
        !artifact_parity_assert_equal_manifests(arena,
                                                nob_temp_sprintf("%s_install_tree", project->name),
                                                cmake_install_manifest,
                                                nob_install_manifest)) {
        arena_destroy(arena);
        return false;
    }
    arena_destroy(arena);

    if (!corpus_run_consumer_configure(&s_corpus_cmake,
                                       consumer_dst,
                                       nob_temp_sprintf("%s_consumer_cmake_build", project->name),
                                       cmake_install_abs) ||
        !corpus_run_consumer_build(&s_corpus_cmake,
                                   nob_temp_sprintf("%s_consumer_cmake_build", project->name)) ||
        !corpus_run_consumer_binary(project,
                                    nob_temp_sprintf("%s_consumer_cmake_build", project->name),
                                    cmake_install_abs) ||
        !corpus_run_consumer_configure(&s_corpus_cmake,
                                       consumer_dst,
                                       nob_temp_sprintf("%s_consumer_nob_build", project->name),
                                       nob_install_abs) ||
        !corpus_run_consumer_build(&s_corpus_cmake,
                                   nob_temp_sprintf("%s_consumer_nob_build", project->name)) ||
        !corpus_run_consumer_binary(project,
                                    nob_temp_sprintf("%s_consumer_nob_build", project->name),
                                    nob_install_abs)) {
        return false;
    }

    return true;
}

static bool corpus_suite_init(void) {
    artifact_parity_test_set_repo_root(getenv(CMK2NOB_TEST_REPO_ROOT_ENV));
    if (!artifact_parity_resolve_cmake(&s_corpus_cmake, false, s_corpus_skip_reason)) return false;
    if (!artifact_parity_resolve_nobify_bin(s_corpus_nobify_bin, s_corpus_nobify_error)) return false;
    return true;
}

TEST(artifact_parity_corpus_fmt_snapshot_matches_build_install_and_consumer) {
    if (!s_corpus_cmake.available) {
        TEST_SKIP(s_corpus_skip_reason[0] ? s_corpus_skip_reason : "cmake 3.28.x is not available");
    }
    ASSERT(corpus_run_project(&s_corpus_projects[0]));
    TEST_PASS();
}

TEST(artifact_parity_corpus_pugixml_snapshot_matches_build_install_and_consumer) {
    if (!s_corpus_cmake.available) {
        TEST_SKIP(s_corpus_skip_reason[0] ? s_corpus_skip_reason : "cmake 3.28.x is not available");
    }
    ASSERT(corpus_run_project(&s_corpus_projects[1]));
    TEST_PASS();
}

TEST(artifact_parity_corpus_nlohmann_json_snapshot_matches_build_install_and_consumer) {
    if (!s_corpus_cmake.available) {
        TEST_SKIP(s_corpus_skip_reason[0] ? s_corpus_skip_reason : "cmake 3.28.x is not available");
    }
    ASSERT(corpus_run_project(&s_corpus_projects[2]));
    TEST_PASS();
}

TEST(artifact_parity_corpus_cjson_snapshot_matches_build_install_and_consumer) {
    if (!s_corpus_cmake.available) {
        TEST_SKIP(s_corpus_skip_reason[0] ? s_corpus_skip_reason : "cmake 3.28.x is not available");
    }
    ASSERT(corpus_run_project(&s_corpus_projects[3]));
    TEST_PASS();
}

void run_artifact_parity_corpus_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = false;
    bool entered = false;

    prepared = test_ws_prepare(&ws, "artifact_parity_corpus");
    if (!prepared) {
        nob_log(NOB_ERROR, "artifact parity corpus suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "artifact parity corpus suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    if (!corpus_suite_init()) {
        nob_log(NOB_ERROR,
                "artifact parity corpus suite: failed to resolve toolchain: %s",
                s_corpus_nobify_error[0] ? s_corpus_nobify_error : "unknown nobify error");
        if (failed) (*failed)++;
    } else {
        test_artifact_parity_corpus_fmt_snapshot_matches_build_install_and_consumer(passed, failed, skipped);
        test_artifact_parity_corpus_pugixml_snapshot_matches_build_install_and_consumer(passed, failed, skipped);
        test_artifact_parity_corpus_nlohmann_json_snapshot_matches_build_install_and_consumer(passed, failed, skipped);
        test_artifact_parity_corpus_cjson_snapshot_matches_build_install_and_consumer(passed, failed, skipped);
    }

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "artifact parity corpus suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
