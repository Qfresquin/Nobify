#ifndef TEST_ARTIFACT_PARITY_V2_SUPPORT_H_
#define TEST_ARTIFACT_PARITY_V2_SUPPORT_H_

#include "test_snapshot_support.h"
#include "test_workspace.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ARTIFACT_PARITY_PHASE_NONE = 0,
    ARTIFACT_PARITY_PHASE_CONFIGURE = 1u << 0,
    ARTIFACT_PARITY_PHASE_BUILD = 1u << 1,
    ARTIFACT_PARITY_PHASE_INSTALL = 1u << 2,
    ARTIFACT_PARITY_PHASE_PACKAGE = 1u << 3,
} Artifact_Parity_Phase;

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
    ARTIFACT_PARITY_CAPTURE_FILE_SHA256,
} Artifact_Parity_Capture_Kind;

typedef enum {
    ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT = 0,
    ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET,
    ARTIFACT_PARITY_NOB_COMMAND_CLEAN,
    ARTIFACT_PARITY_NOB_COMMAND_INSTALL,
    ARTIFACT_PARITY_NOB_COMMAND_PACKAGE,
} Artifact_Parity_Nob_Command_Kind;

typedef struct {
    const char *path;
    const char *contents;
} Artifact_Parity_File;

typedef struct {
    Artifact_Parity_Domain domain;
    Artifact_Parity_Capture_Kind capture;
    const char *label;
    const char *relpath;
} Artifact_Parity_Manifest_Request;

typedef struct {
    Artifact_Parity_Nob_Command_Kind kind;
    const char *arg;
} Artifact_Parity_Nob_Command;

typedef struct {
    const char *name;
    Artifact_Parity_Phase phases;
    const Artifact_Parity_File *files;
    size_t file_count;
    const Artifact_Parity_Nob_Command *nob_commands;
    size_t nob_command_count;
    const Artifact_Parity_Manifest_Request *manifest_requests;
    size_t manifest_request_count;
    const char *source_root;
    const char *cmake_binary_dir;
    const char *nob_binary_dir;
    const char *generated_nob_path;
    const char *nob_run_dir;
    const char *cmake_build_target;
    const char *cmake_base_dir;
    const char *nob_base_dir;
    const char *clean_absence_relpath;
    const char *subject;
} Artifact_Parity_Case;

typedef struct {
    char cmake_bin[_TINYDIR_PATH_MAX];
    char cpack_bin[_TINYDIR_PATH_MAX];
    char cmake_version[64];
    bool available;
    bool cpack_available;
} Artifact_Parity_Cmake_Config;

void artifact_parity_test_set_repo_root(const char *repo_root);
bool artifact_parity_resolve_cmake(Artifact_Parity_Cmake_Config *out_config,
                                   bool require_cpack,
                                   char skip_reason[256]);
bool artifact_parity_resolve_nobify_bin(char out_path[_TINYDIR_PATH_MAX],
                                        char error_reason[256]);
bool artifact_parity_write_text_file(const char *path, const char *text);
bool artifact_parity_write_executable_file(const char *path, const char *text);
bool artifact_parity_materialize_files(const char *root_dir,
                                       const Artifact_Parity_File *files,
                                       size_t file_count);
bool artifact_parity_run_nobify(const char *nobify_bin,
                                const char *input_path,
                                const char *output_path,
                                const char *source_root,
                                const char *binary_root);
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
bool artifact_parity_run_cmake_install(const Artifact_Parity_Cmake_Config *config,
                                       const char *binary_dir,
                                       const char *prefix_dir);
bool artifact_parity_run_cmake_package(const Artifact_Parity_Cmake_Config *config,
                                       const char *binary_dir);
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
