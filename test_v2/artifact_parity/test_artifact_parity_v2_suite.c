#include "test_artifact_parity_v2_support.h"

#include "test_v2_assert.h"

#include <stdio.h>

static Artifact_Parity_Cmake_Config s_artifact_parity_cmake = {0};
static char s_artifact_parity_skip_reason[256] = {0};

static const Artifact_Parity_Manifest_Request s_baseline_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "build_outputs", "artifacts"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "generated_tree", "generated"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "generated_meta", "generated/meta.txt"},
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_TREE, "install_tree", "install"},
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "export_files", "exports"},
    {ARTIFACT_PARITY_DOMAIN_PACKAGE_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "package_files", "packages"},
    {ARTIFACT_PARITY_DOMAIN_PACKAGE_METADATA, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "package_metadata", "package_metadata.txt"},
};

static bool artifact_parity_generate_nob_from_source_tree(const char *script) {
    Artifact_Parity_Nob_Config nob_config = {
        .current_file = "CMakeLists.txt",
        .source_dir = nob_sv_from_cstr("."),
        .binary_dir = nob_sv_from_cstr("../nob_build"),
    };
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();

    if (!script || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);

    if (!nob_set_current_dir("source")) return false;
    if (!artifact_parity_generate_nob(script, &nob_config, "../nob_build/nob.c")) {
        (void)nob_set_current_dir(prev_cwd);
        return false;
    }

    return nob_set_current_dir(prev_cwd);
}

TEST(artifact_parity_build_and_generated_manifest_matches_cmake) {
    static const char *script =
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParity VERSION 1.2.3 LANGUAGES C)\n"
        "configure_file(meta.txt.in generated/meta.txt @ONLY)\n"
        "add_library(core STATIC src/core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_executable(app src/main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n";
    static const char *meta_in =
        "PROJECT=@PROJECT_NAME@\n"
        "VERSION=@PROJECT_VERSION@\n";
    static const char *core_c =
        "int core_value(void) { return 7; }\n";
    static const char *main_c =
        "int core_value(void);\n"
        "int main(void) { return core_value() == 7 ? 0 : 1; }\n";

    Arena *arena = NULL;
    String_View cmake_manifest = {0};
    String_View nob_manifest = {0};

    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_write_text_file("source/CMakeLists.txt", script));
    ASSERT(artifact_parity_write_text_file("source/meta.txt.in", meta_in));
    ASSERT(artifact_parity_write_text_file("source/src/core.c", core_c));
    ASSERT(artifact_parity_write_text_file("source/src/main.c", main_c));

    ASSERT(artifact_parity_run_cmake_configure(&s_artifact_parity_cmake, "source", "cmake_build"));
    ASSERT(artifact_parity_run_cmake_build(&s_artifact_parity_cmake, "cmake_build", "app"));

    ASSERT(artifact_parity_generate_nob_from_source_tree(script));
    ASSERT(artifact_parity_compile_generated_nob("nob_build/nob.c", "nob_build/nob_gen"));
    ASSERT(artifact_parity_run_binary_in_dir("nob_build", "./nob_gen", "app", NULL));

    arena = arena_create(4 * 1024 * 1024);
    ASSERT(arena != NULL);
    ASSERT(artifact_parity_capture_manifest(arena,
                                            "cmake_build",
                                            s_baseline_manifest_requests,
                                            NOB_ARRAY_LEN(s_baseline_manifest_requests),
                                            &cmake_manifest));
    ASSERT(artifact_parity_capture_manifest(arena,
                                            "nob_build",
                                            s_baseline_manifest_requests,
                                            NOB_ARRAY_LEN(s_baseline_manifest_requests),
                                            &nob_manifest));
    ASSERT(artifact_parity_assert_equal_manifests(arena,
                                                  "build_and_generated",
                                                  cmake_manifest,
                                                  nob_manifest));

    arena_destroy(arena);
    TEST_PASS();
}

void run_artifact_parity_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    char repo_root[_TINYDIR_PATH_MAX] = {0};
    const char *repo_root_env = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    bool prepared = test_ws_prepare(&ws, "artifact-parity");
    bool entered = false;

    s_artifact_parity_cmake = (Artifact_Parity_Cmake_Config){0};
    s_artifact_parity_skip_reason[0] = '\0';

    if (!prepared) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to enter isolated workspace");
        (void)test_ws_cleanup(&ws);
        if (failed) (*failed)++;
        return;
    }

    snprintf(repo_root, sizeof(repo_root), "%s", repo_root_env ? repo_root_env : "");
    artifact_parity_test_set_repo_root(repo_root);

    if (!artifact_parity_resolve_cmake(&s_artifact_parity_cmake, s_artifact_parity_skip_reason)) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to resolve cmake");
        if (failed) (*failed)++;
    } else {
        test_artifact_parity_build_and_generated_manifest_matches_cmake(passed, failed, skipped);
    }

    if (!test_ws_leave(prev_cwd)) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to restore cwd");
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
