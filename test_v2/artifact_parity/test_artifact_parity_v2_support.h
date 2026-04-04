#ifndef TEST_ARTIFACT_PARITY_V2_SUPPORT_H_
#define TEST_ARTIFACT_PARITY_V2_SUPPORT_H_

#include "test_semantic_pipeline.h"
#include "test_snapshot_support.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "nob_codegen.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS = 0,
    ARTIFACT_PARITY_DOMAIN_GENERATED_FILES,
    ARTIFACT_PARITY_DOMAIN_INSTALL_TREE,
    ARTIFACT_PARITY_DOMAIN_EXPORT_FILES,
    ARTIFACT_PARITY_DOMAIN_PACKAGE_FILES,
    ARTIFACT_PARITY_DOMAIN_PACKAGE_METADATA,
} Artifact_Parity_Domain;

typedef enum {
    ARTIFACT_PARITY_CAPTURE_TREE = 0,
    ARTIFACT_PARITY_CAPTURE_FILE_TEXT,
} Artifact_Parity_Capture_Kind;

typedef struct {
    Artifact_Parity_Domain domain;
    Artifact_Parity_Capture_Kind capture;
    const char *label;
    const char *relpath;
} Artifact_Parity_Manifest_Request;

typedef struct {
    char cmake_bin[_TINYDIR_PATH_MAX];
    char cmake_version[64];
    bool available;
} Artifact_Parity_Cmake_Config;

typedef struct {
    const char *current_file;
    String_View source_dir;
    String_View binary_dir;
} Artifact_Parity_Nob_Config;

void artifact_parity_test_set_repo_root(const char *repo_root);
bool artifact_parity_resolve_cmake(Artifact_Parity_Cmake_Config *out_config,
                                   char skip_reason[256]);
bool artifact_parity_write_text_file(const char *path, const char *text);
bool artifact_parity_generate_nob(const char *script,
                                  const Artifact_Parity_Nob_Config *config,
                                  const char *output_path);
bool artifact_parity_compile_generated_nob(const char *generated_path,
                                           const char *output_path);
bool artifact_parity_run_binary_in_dir(const char *dir,
                                       const char *binary_path,
                                       const char *arg1,
                                       const char *arg2);
bool artifact_parity_run_cmake_configure(const Artifact_Parity_Cmake_Config *config,
                                         const char *source_dir,
                                         const char *binary_dir);
bool artifact_parity_run_cmake_build(const Artifact_Parity_Cmake_Config *config,
                                     const char *binary_dir,
                                     const char *target_name);
bool artifact_parity_capture_manifest(
    Arena *arena,
    const char *base_dir,
    const Artifact_Parity_Manifest_Request *requests,
    size_t request_count,
    String_View *out);
bool artifact_parity_assert_equal_manifests(Arena *arena,
                                            const char *subject,
                                            String_View expected,
                                            String_View actual);

#endif // TEST_ARTIFACT_PARITY_V2_SUPPORT_H_
