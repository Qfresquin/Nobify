#include "test_artifact_parity_corpus_manifest.h"
#include "test_artifact_parity_v2_support.h"

#include "test_fs.h"
#include "test_host_fixture_support.h"
#include "test_v2_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Artifact_Parity_Cmake_Config s_corpus_cmake = {0};
static char s_corpus_skip_reason[256] = {0};
static char s_corpus_nobify_bin[_TINYDIR_PATH_MAX] = {0};
static char s_corpus_nobify_error[256] = {0};
static Artifact_Parity_Corpus_Project_List s_corpus_projects = {0};

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

    if (!test_fs_join_path(prefix_dir, "lib", runtime_dir)) return false;
    if (test_ws_host_path_exists(runtime_dir)) {
        old_value = getenv("LD_LIBRARY_PATH");
        if (old_value && old_value[0] != '\0') {
            if (snprintf(runtime_env, sizeof(runtime_env), "%s:%s", runtime_dir, old_value) >=
                (int)sizeof(runtime_env)) {
                return false;
            }
        } else {
            if (snprintf(runtime_env, sizeof(runtime_env), "%s", runtime_dir) >=
                (int)sizeof(runtime_env)) {
                return false;
            }
        }
        if (!test_host_env_guard_begin_heap(&env_guard, "LD_LIBRARY_PATH", runtime_env)) return false;
    }

    ok = artifact_parity_run_binary_in_dir(binary_dir, "artifacts/bin/consumer", NULL, NULL);
    test_host_env_guard_cleanup(env_guard);
    return ok;
}

static const char *corpus_project_case_name(const Artifact_Parity_Corpus_Project *project) {
    return project && project->name ? project->name : "";
}

static bool corpus_format_phases(const Artifact_Parity_Corpus_Project *project,
                                 char buffer[128]) {
    if (!buffer) return false;
    buffer[0] = '\0';
    return artifact_parity_corpus_format_string_list(project ? &project->supported_phases : NULL,
                                                     buffer,
                                                     128);
}

static bool corpus_run_project(const Artifact_Parity_Corpus_Project *project, const char **out_stage) {
    Arena *arena = NULL;
    String_View cmake_build_manifest = {0};
    String_View nob_build_manifest = {0};
    String_View cmake_install_manifest = {0};
    String_View nob_install_manifest = {0};
    char archive_relpath[_TINYDIR_PATH_MAX] = {0};
    char consumer_relpath[_TINYDIR_PATH_MAX] = {0};
    char snapshot_dst[_TINYDIR_PATH_MAX] = {0};
    char extracted_project_rel[_TINYDIR_PATH_MAX] = {0};
    char consumer_dst[_TINYDIR_PATH_MAX] = {0};
    char cmake_install_abs[_TINYDIR_PATH_MAX] = {0};
    char nob_install_abs[_TINYDIR_PATH_MAX] = {0};
    char input_abs[_TINYDIR_PATH_MAX] = {0};
    char output_abs[_TINYDIR_PATH_MAX] = {0};
    char source_root_abs[_TINYDIR_PATH_MAX] = {0};
    char binary_root_abs[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();

    if (out_stage) *out_stage = "init";
    if (!project || !cwd) return false;
    if (s_corpus_nobify_bin[0] == '\0' || !s_corpus_cmake.available) return false;
    if (!artifact_parity_corpus_project_archive_relpath(project,
                                                        archive_relpath,
                                                        sizeof(archive_relpath)) ||
        !artifact_parity_corpus_project_consumer_relpath(project,
                                                         consumer_relpath,
                                                         sizeof(consumer_relpath))) {
        return false;
    }

    if (snprintf(snapshot_dst, sizeof(snapshot_dst), "%s_project", project->name) >=
            (int)sizeof(snapshot_dst) ||
        snprintf(consumer_dst, sizeof(consumer_dst), "%s_consumer", project->name) >=
            (int)sizeof(consumer_dst) ||
        snprintf(extracted_project_rel, sizeof(extracted_project_rel), "%s_project/%s", project->name, project->archive_prefix) >=
            (int)sizeof(extracted_project_rel) ||
        snprintf(cmake_install_abs, sizeof(cmake_install_abs), "%s/%s_cmake_install", cwd, project->name) >=
            (int)sizeof(cmake_install_abs) ||
        snprintf(nob_install_abs, sizeof(nob_install_abs), "%s/%s/install", cwd, extracted_project_rel) >=
            (int)sizeof(nob_install_abs) ||
        snprintf(input_abs, sizeof(input_abs), "%s/%s/CMakeLists.txt", cwd, extracted_project_rel) >=
            (int)sizeof(input_abs) ||
        snprintf(output_abs, sizeof(output_abs), "%s/%s/nob.c", cwd, extracted_project_rel) >=
            (int)sizeof(output_abs) ||
        snprintf(source_root_abs, sizeof(source_root_abs), "%s/%s", cwd, extracted_project_rel) >=
            (int)sizeof(source_root_abs) ||
        snprintf(binary_root_abs, sizeof(binary_root_abs), "%s/%s_nob_build", cwd, project->name) >=
            (int)sizeof(binary_root_abs)) {
        return false;
    }

    if (out_stage) *out_stage = "extract_snapshot";
    if (!corpus_extract_archive_from_repo(archive_relpath, snapshot_dst)) return false;
    if (out_stage) *out_stage = "copy_consumer_fixture";
    if (!corpus_copy_tree_from_repo(consumer_relpath, consumer_dst)) return false;

    if (out_stage) *out_stage = "cmake_configure";
    if (!artifact_parity_run_cmake_configure(&s_corpus_cmake,
                                             extracted_project_rel,
                                             nob_temp_sprintf("%s_cmake_build", project->name),
                                             NULL)) {
        return false;
    }
    if (out_stage) *out_stage = "cmake_build";
    if (!artifact_parity_run_cmake_build(&s_corpus_cmake,
                                         nob_temp_sprintf("%s_cmake_build", project->name),
                                         NULL)) {
        return false;
    }
    if (out_stage) *out_stage = "cmake_install";
    if (!artifact_parity_run_cmake_install(&s_corpus_cmake,
                                           nob_temp_sprintf("%s_cmake_build", project->name),
                                           nob_temp_sprintf("%s_cmake_install", project->name),
                                           NULL)) {
        return false;
    }

    if (out_stage) *out_stage = "nobify_generate";
    if (!artifact_parity_run_nobify(s_corpus_nobify_bin,
                                    input_abs,
                                    output_abs,
                                    source_root_abs,
                                    binary_root_abs)) {
        return false;
    }
    if (out_stage) *out_stage = "generated_nob_compile";
    if (!artifact_parity_compile_generated_nob(nob_temp_sprintf("%s/nob.c", extracted_project_rel),
                                               nob_temp_sprintf("%s/nob_gen", extracted_project_rel))) {
        return false;
    }
    if (out_stage) *out_stage = "generated_nob_build";
    if (!artifact_parity_run_binary_in_dir(extracted_project_rel,
                                           "./nob_gen",
                                           NULL,
                                           NULL)) {
        return false;
    }
    if (out_stage) *out_stage = "generated_nob_install";
    if (!artifact_parity_run_binary_in_dir_argv(extracted_project_rel,
                                                "./nob_gen",
                                                (const char *[]){"install", "--prefix", nob_install_abs},
                                                3)) {
        return false;
    }

    arena = arena_create(2 * 1024 * 1024);
    if (!arena) return false;
    if (out_stage) *out_stage = "build_manifest_compare";
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
                                                nob_build_manifest)) {
        arena_destroy(arena);
        return false;
    }
    if (out_stage) *out_stage = "install_manifest_compare";
    if (!corpus_capture_tree_manifest(arena,
                                      nob_temp_sprintf("%s_cmake_install", project->name),
                                      "",
                                      "install_tree",
                                      &cmake_install_manifest) ||
        !corpus_capture_tree_manifest(arena,
                                      nob_temp_sprintf("%s/install", extracted_project_rel),
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

    if (out_stage) *out_stage = "consumer_cmake_configure";
    if (!corpus_run_consumer_configure(&s_corpus_cmake,
                                       consumer_dst,
                                       nob_temp_sprintf("%s_consumer_cmake_build", project->name),
                                       cmake_install_abs)) {
        return false;
    }
    if (out_stage) *out_stage = "consumer_cmake_build";
    if (!corpus_run_consumer_build(&s_corpus_cmake,
                                   nob_temp_sprintf("%s_consumer_cmake_build", project->name))) {
        return false;
    }
    if (out_stage) *out_stage = "consumer_cmake_run";
    if (!corpus_run_consumer_binary(project,
                                    nob_temp_sprintf("%s_consumer_cmake_build", project->name),
                                    cmake_install_abs)) {
        return false;
    }
    if (out_stage) *out_stage = "consumer_nob_configure";
    if (!corpus_run_consumer_configure(&s_corpus_cmake,
                                       consumer_dst,
                                       nob_temp_sprintf("%s_consumer_nob_build", project->name),
                                       nob_install_abs)) {
        return false;
    }
    if (out_stage) *out_stage = "consumer_nob_build";
    if (!corpus_run_consumer_build(&s_corpus_cmake,
                                   nob_temp_sprintf("%s_consumer_nob_build", project->name))) {
        return false;
    }
    if (out_stage) *out_stage = "consumer_nob_run";
    if (!corpus_run_consumer_binary(project,
                                    nob_temp_sprintf("%s_consumer_nob_build", project->name),
                                    nob_install_abs)) {
        return false;
    }

    if (out_stage) *out_stage = "done";
    return true;
}

static bool corpus_manifest_abs_path(char out_path[_TINYDIR_PATH_MAX]) {
    return corpus_join_abs_from_repo(ARTIFACT_PARITY_CORPUS_MANIFEST_PATH, out_path);
}

static bool corpus_suite_init(void) {
    char manifest_path[_TINYDIR_PATH_MAX] = {0};

    artifact_parity_test_set_repo_root(getenv(CMK2NOB_TEST_REPO_ROOT_ENV));
    if (!artifact_parity_resolve_cmake(&s_corpus_cmake, false, s_corpus_skip_reason)) return false;
    if (!artifact_parity_resolve_nobify_bin(s_corpus_nobify_bin, s_corpus_nobify_error)) return false;
    if (!corpus_manifest_abs_path(manifest_path)) return false;
    artifact_parity_corpus_manifest_free(&s_corpus_projects);
    return artifact_parity_corpus_manifest_load_path(manifest_path, &s_corpus_projects);
}

static void corpus_execute_project_case(const Artifact_Parity_Corpus_Project *project,
                                        int *passed,
                                        int *failed,
                                        int *skipped) {
    Test_Case_Workspace test_ws_case = {0};
    Test_V2_Cleanup_Stack prev_cleanup_stack = test_v2_cleanup_scope_enter();
    char phases[128] = {0};
    const char *stage = "init";
    bool entered = false;
    const char *case_name = corpus_project_case_name(project);

    if (!project || !passed || !failed || !skipped) {
        test_v2_cleanup_scope_leave(prev_cleanup_stack);
        return;
    }
    if (!test_v2_case_matches(case_name)) {
        (*skipped)++;
        test_v2_cleanup_scope_leave(prev_cleanup_stack);
        return;
    }

    (void)corpus_format_phases(project, phases);
    nob_log(NOB_INFO,
            "artifact parity corpus project `%s`: phases=%s tier=%s",
            case_name,
            phases[0] != '\0' ? phases : "<none>",
            project->support_tier ? project->support_tier : "<unknown>");

    test_v2_case_begin(case_name);
    if (!test_ws_case_enter(&test_ws_case, case_name)) {
        test_v2_emit_failure_message(__func__,
                                     __FILE__,
                                     0,
                                     "could not enter isolated test workspace");
        nob_log(NOB_ERROR,
                "FAILED: %s: could not enter isolated test workspace",
                case_name);
        (*failed)++;
        goto defer;
    }
    entered = true;

    if (!s_corpus_cmake.available) {
        nob_log(NOB_INFO,
                "SKIPPED: %s: %s",
                case_name,
                s_corpus_skip_reason[0] ? s_corpus_skip_reason : "cmake 3.28.x is not available");
        (*skipped)++;
        goto defer;
    }

    if (!corpus_run_project(project, &stage)) {
        test_v2_emit_failure_message(__func__,
                                     __FILE__,
                                     0,
                                     nob_temp_sprintf("project=%s phases=%s stage=%s",
                                                      case_name,
                                                      phases[0] != '\0' ? phases : "<none>",
                                                      stage ? stage : "<unknown>"));
        nob_log(NOB_ERROR,
                "FAILED: project=%s phases=%s stage=%s",
                case_name,
                phases[0] != '\0' ? phases : "<none>",
                stage ? stage : "<unknown>");
        (*failed)++;
        goto defer;
    }

    (*passed)++;

defer:
    test_v2_cleanup_run_all();
    if (entered && !test_ws_case_leave(&test_ws_case)) {
        test_v2_emit_failure_message(__func__,
                                     __FILE__,
                                     0,
                                     "could not cleanup isolated test workspace");
        nob_log(NOB_ERROR,
                "FAILED: %s: could not cleanup isolated test workspace",
                case_name);
        (*failed)++;
    }
    test_v2_case_end();
    test_v2_cleanup_scope_leave(prev_cleanup_stack);
}

TEST(artifact_parity_corpus_manifest_loader_accepts_current_manifest_and_preserves_order) {
    char manifest_path[_TINYDIR_PATH_MAX] = {0};
    Artifact_Parity_Corpus_Project_List projects = {0};

    ASSERT(corpus_manifest_abs_path(manifest_path));
    ASSERT(artifact_parity_corpus_manifest_load_path(manifest_path, &projects));
    ASSERT(projects.count >= 4);
    ASSERT(strcmp(projects.items[0].name, "fmt") == 0);
    ASSERT(strcmp(projects.items[1].name, "pugixml") == 0);
    ASSERT(strcmp(projects.items[2].name, "nlohmann_json") == 0);
    ASSERT(strcmp(projects.items[3].name, "cjson") == 0);
    ASSERT(artifact_parity_corpus_string_list_contains(&projects.items[0].supported_phases, "build"));
    ASSERT(artifact_parity_corpus_string_list_contains(&projects.items[0].supported_phases, "install"));
    ASSERT(artifact_parity_corpus_string_list_contains(&projects.items[0].supported_phases, "consumer"));
    ASSERT(artifact_parity_corpus_string_list_contains(&projects.items[0].expected_imported_targets,
                                                       "fmt::fmt"));
    ASSERT(artifact_parity_corpus_project_has_support_tier(&projects.items[0], "supported"));
    artifact_parity_corpus_manifest_free(&projects);
    TEST_PASS();
}

TEST(artifact_parity_corpus_manifest_loader_rejects_missing_required_fields) {
    Artifact_Parity_Corpus_Project_List projects = {0};
    const char *manifest_text =
        "{\n"
        "  \"projects\": [\n"
        "    {\n"
        "      \"name\": \"broken\",\n"
        "      \"upstream_url\": \"https://example.invalid/upstream\",\n"
        "      \"archive_url\": \"https://example.invalid/archive.tar.gz\",\n"
        "      \"pinned_ref\": \"v0\",\n"
        "      \"archive_prefix\": \"broken-v0\",\n"
        "      \"retain_paths\": [\"LICENSE\"],\n"
        "      \"supported_phases\": [\"build\"],\n"
        "      \"expected_imported_targets\": [\"broken::broken\"]\n"
        "    }\n"
        "  ]\n"
        "}\n";

    ASSERT(nob_write_entire_file("invalid_manifest.json", manifest_text, strlen(manifest_text)));
    ASSERT(!artifact_parity_corpus_manifest_load_path("invalid_manifest.json", &projects));
    TEST_PASS();
}

TEST(artifact_parity_corpus_manifest_case_names_follow_project_names) {
    char manifest_path[_TINYDIR_PATH_MAX] = {0};
    Artifact_Parity_Corpus_Project_List projects = {0};
    Test_Host_Env_Guard *guard = NULL;

    ASSERT(corpus_manifest_abs_path(manifest_path));
    ASSERT(artifact_parity_corpus_manifest_load_path(manifest_path, &projects));
    ASSERT(projects.count > 0);
    ASSERT(test_host_env_guard_begin_heap(&guard,
                                          CMK2NOB_TEST_CASE_FILTER_ENV,
                                          projects.items[0].name));
    ASSERT(strcmp(corpus_project_case_name(&projects.items[0]), projects.items[0].name) == 0);
    ASSERT(test_v2_case_matches(corpus_project_case_name(&projects.items[0])));
    test_host_env_guard_cleanup(guard);
    artifact_parity_corpus_manifest_free(&projects);
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

    test_artifact_parity_corpus_manifest_loader_accepts_current_manifest_and_preserves_order(passed,
                                                                                              failed,
                                                                                              skipped);
    test_artifact_parity_corpus_manifest_loader_rejects_missing_required_fields(passed,
                                                                                 failed,
                                                                                 skipped);
    test_artifact_parity_corpus_manifest_case_names_follow_project_names(passed,
                                                                         failed,
                                                                         skipped);

    if (!corpus_suite_init()) {
        nob_log(NOB_ERROR,
                "artifact parity corpus suite: failed to resolve toolchain or manifest: %s",
                s_corpus_nobify_error[0] ? s_corpus_nobify_error : "unknown corpus setup error");
        if (failed) (*failed)++;
    } else {
        for (size_t i = 0; i < s_corpus_projects.count; ++i) {
            corpus_execute_project_case(&s_corpus_projects.items[i], passed, failed, skipped);
        }
    }

    artifact_parity_corpus_manifest_free(&s_corpus_projects);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "artifact parity corpus suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
