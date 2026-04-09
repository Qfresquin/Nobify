#include "test_evaluator_codegen_diff_v2_common.h"

#include "../artifact_parity/test_artifact_parity_corpus_manifest.h"

#include "arena_dyn.h"

#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef enum {
    EGD_CLASS_PARITY_PASS = 0,
    EGD_CLASS_BACKEND_REJECT,
    EGD_CLASS_EVALUATOR_ONLY,
    EGD_CLASS_EXPLICIT_NON_GOAL,
} EGD_Case_Classification;

typedef enum {
    EGD_PARITY_NONE = 0,
    EGD_PARITY_BUILD_TREE,
    EGD_PARITY_INSTALL_TREE,
    EGD_PARITY_EXPORT_FILES,
    EGD_PARITY_PACKAGE_ARCHIVE,
} EGD_Parity_Kind;

typedef Test_Case_Dsl_Path_Scope EGD_Path_Scope;
typedef Test_Case_Dsl_Mode EGD_Case_Mode;
typedef Test_Case_Dsl_Expected_Outcome EGD_Expected_Outcome;
typedef Test_Case_Dsl_Path_Entry EGD_Path_Entry;
typedef Test_Case_Dsl_Text_Fixture EGD_Text_Fixture;
typedef Test_Case_Dsl_Case EGD_Parsed_Case;

#define EGD_SCOPE_SOURCE TEST_CASE_DSL_PATH_SCOPE_SOURCE
#define EGD_SCOPE_BUILD TEST_CASE_DSL_PATH_SCOPE_BUILD
#define EGD_MODE_PROJECT TEST_CASE_DSL_MODE_PROJECT
#define EGD_MODE_SCRIPT TEST_CASE_DSL_MODE_SCRIPT
#define EGD_OUTCOME_SUCCESS TEST_CASE_DSL_EXPECT_SUCCESS
#define EGD_OUTCOME_ERROR TEST_CASE_DSL_EXPECT_ERROR

enum {
    EGD_PHASE_CONFIGURE = 1u << 0,
    EGD_PHASE_BUILD = 1u << 1,
    EGD_PHASE_TEST = 1u << 2,
    EGD_PHASE_INSTALL = 1u << 3,
    EGD_PHASE_EXPORT = 1u << 4,
    EGD_PHASE_PACKAGE = 1u << 5,
    EGD_PHASE_HOST_ONLY = 1u << 6,
};

enum {
    EGD_TOOL_NONE = 0,
    EGD_TOOL_CMAKE = 1u << 0,
    EGD_TOOL_CPACK = 1u << 1,
    EGD_TOOL_GZIP = 1u << 2,
    EGD_TOOL_XZ = 1u << 3,
    EGD_TOOL_PYTHON = 1u << 4,
    EGD_TOOL_TAR = 1u << 5,
};

typedef enum {
    EGD_DIFF_TREE = 0,
    EGD_DIFF_FILE_TEXT,
    EGD_DIFF_FILE_SHA256,
} EGD_Diff_Primitive;

typedef struct {
    const char *command;
    const char *source_pack_path;
    EGD_Case_Classification classification;
    unsigned phase_mask;
    const char *domain_owner;
    const char *reason;
    const char *tracking_key;
} EGD_Command_Inventory;

typedef struct {
    const char *family;
    const char *signature;
    const char *source_pack_path;
    const char *case_name;
    EGD_Case_Classification classification;
    unsigned phase_mask;
    const char *domain_owner;
    const char *reason;
    const char *tracking_key;
    const char *workload_key;
} EGD_Subcommand_Inventory;

typedef struct {
    const char *label;
    const char *relpath;
    EGD_Diff_Primitive primitive;
} EGD_Observed_Output;

typedef struct {
    bool cmake;
    bool cpack;
    bool gzip;
    bool xz;
    bool python;
    bool tar;
} EGD_Tool_Availability;

typedef struct {
    int parity_pass;
    int backend_reject;
    int evaluator_only;
    int explicit_non_goal;
} EGD_Inventory_State_Counts;

typedef struct {
    const char *case_name;
    const char *source_pack_path;
    const char *command_family;
    const char *signature;
    EGD_Case_Classification classification;
    EGD_Parity_Kind parity_kind;
    EGD_Expected_Outcome expected_outcome;
    unsigned phase_mask;
    unsigned required_tools;
    const char *domain_owner;
    const char *reason;
    const char *tracking_key;
    const char *workload_key;
    const EGD_Observed_Output *observed_outputs;
    size_t observed_output_count;
    const Test_Manifest_Request *manifest_requests;
    size_t manifest_request_count;
    const char *package_generator;
} EGD_Case_Def;

typedef struct {
    int parity_passed;
    int backend_rejected;
    int evaluator_only;
    int explicit_non_goal;
    int skipped_by_tool;
} EGD_Case_Summary;

typedef enum {
    EGD_CORPUS_PROJECT_SUPPORTED = 0,
    EGD_CORPUS_PROJECT_KNOWN_BOUNDARY,
    EGD_CORPUS_PROJECT_BACKEND_GAP,
} EGD_Corpus_Project_Disposition;

typedef enum {
    EGD_CORPUS_SEVERITY_RELEASE_BLOCKER = 0,
    EGD_CORPUS_SEVERITY_MAJOR,
    EGD_CORPUS_SEVERITY_MINOR,
} EGD_Corpus_Finding_Severity;

typedef enum {
    EGD_CORPUS_FREQUENCY_COMMON = 0,
    EGD_CORPUS_FREQUENCY_PROJECT_LOCAL,
} EGD_Corpus_Finding_Frequency;

typedef enum {
    EGD_CORPUS_DISPOSITION_FOCUSED_PROOF_CASE = 0,
    EGD_CORPUS_DISPOSITION_BACKEND_REJECT,
    EGD_CORPUS_DISPOSITION_EXPLICIT_BOUNDARY,
} EGD_Corpus_Finding_Disposition;

typedef struct {
    const char *project_name;
    EGD_Corpus_Project_Disposition disposition;
    const char *reason;
    const char *tracking_or_boundary_key;
} EGD_Corpus_Project_Inventory;

typedef struct {
    const char *key;
    EGD_Corpus_Finding_Severity severity;
    EGD_Corpus_Finding_Frequency frequency;
    EGD_Corpus_Finding_Disposition disposition;
    const char *reason;
    const char *const *affected_projects;
    size_t affected_project_count;
} EGD_Corpus_Finding_Inventory;

#define EGD_EXPECTED_FULL_COMMANDS 124u
#define EGD_COMMAND_INVENTORY_VERSION "2026-04-09-c4"
#define EGD_SUPPORTED_SUBSET_DOC "docs/codegen/generated_backend_supported_subset.md"

#define EGD_PACK_EVAL_DEFAULT "test_v2/evaluator/golden/evaluator_default.cmake"
#define EGD_PACK_EVAL_ALL "test_v2/evaluator/golden/evaluator_all.cmake"
#define EGD_PACK_EVAL_INTEGRATION "test_v2/evaluator/golden/evaluator_integration.cmake"
#define EGD_PACK_TARGET_USAGE "test_v2/evaluator_diff/cases/target_usage_seed_cases.cmake"
#define EGD_PACK_INSTALL "test_v2/evaluator_diff/cases/install_host_effect_seed_cases.cmake"
#define EGD_PACK_EXPORT "test_v2/evaluator_diff/cases/export_host_effect_seed_cases.cmake"
#define EGD_PACK_FILE "test_v2/evaluator_diff/cases/file_host_effect_seed_cases.cmake"
#define EGD_PACK_STRING "test_v2/evaluator_diff/cases/string_seed_cases.cmake"
#define EGD_PACK_LIST "test_v2/evaluator_diff/cases/list_seed_cases.cmake"
#define EGD_PACK_MATH "test_v2/evaluator_diff/cases/math_seed_cases.cmake"
#define EGD_PACK_CMAKE_LANGUAGE "test_v2/evaluator_diff/cases/cmake_language_seed_cases.cmake"
#define EGD_PACK_CMAKE_PATH "test_v2/evaluator_diff/cases/cmake_path_seed_cases.cmake"
#define EGD_PACK_CTEST "test_v2/evaluator_diff/cases/ctest_special_seed_cases.cmake"
#define EGD_PACK_TESTING_META "test_v2/evaluator_diff/cases/testing_meta_seed_cases.cmake"
#define EGD_PACK_TRY_COMPILE "test_v2/evaluator_diff/cases/try_compile_special_seed_cases.cmake"
#define EGD_PACK_TRY_RUN "test_v2/evaluator_diff/cases/try_run_special_seed_cases.cmake"
#define EGD_PACK_FETCHCONTENT "test_v2/evaluator_diff/cases/fetchcontent_host_effect_seed_cases.cmake"
#define EGD_PACK_FIND_PACKAGE "test_v2/evaluator_diff/cases/find_package_special_seed_cases.cmake"
#define EGD_PACK_FIND_PATHLIKE "test_v2/evaluator_diff/cases/find_pathlike_seed_cases.cmake"
#define EGD_PACK_GET_FILENAME_COMPONENT "test_v2/evaluator_diff/cases/get_filename_component_seed_cases.cmake"
#define EGD_PACK_CACHE_LOADING "test_v2/evaluator_diff/cases/cache_loading_seed_cases.cmake"
#define EGD_PACK_ARGUMENT_PARSING "test_v2/evaluator_diff/cases/argument_parsing_seed_cases.cmake"
#define EGD_PACK_MESSAGE "test_v2/evaluator_diff/cases/message_seed_cases.cmake"
#define EGD_PACK_POLICY "test_v2/evaluator_diff/cases/cmake_policy_script_seed_cases.cmake"
#define EGD_PACK_INCLUDE "test_v2/evaluator_diff/cases/include_seed_cases.cmake"
#define EGD_PACK_VARS "test_v2/evaluator_diff/cases/var_commands_seed_cases.cmake"
#define EGD_PACK_PROPERTY_QUERY "test_v2/evaluator_diff/cases/property_query_seed_cases.cmake"
#define EGD_PACK_PROPERTY_SETTERS "test_v2/evaluator_diff/cases/property_setters_seed_cases.cmake"
#define EGD_PACK_PROPERTY_WRAPPERS "test_v2/evaluator_diff/cases/property_wrappers_seed_cases.cmake"
#define EGD_PACK_HOST_IDENTITY "test_v2/evaluator_diff/cases/host_identity_seed_cases.cmake"
#define EGD_PACK_CONFIGURE_FILE "test_v2/evaluator_diff/cases/configure_file_seed_cases.cmake"
#define EGD_PACK_EXECUTE_PROCESS "test_v2/evaluator_diff/cases/execute_process_seed_cases.cmake"
#define EGD_PACK_ADD_TARGETS "test_v2/evaluator_diff/cases/add_targets_seed_cases.cmake"
#define EGD_PACK_ADD_SUBDIRECTORY "test_v2/evaluator_diff/cases/add_subdirectory_seed_cases.cmake"
#define EGD_PACK_FLOW_CONTROL "test_v2/evaluator_diff/cases/flow_control_structural_seed_cases.cmake"
#define EGD_PACK_CALLABLE_SCOPE "test_v2/evaluator_diff/cases/callable_scope_structural_seed_cases.cmake"
#define EGD_PACK_FILE_API_META "test_v2/evaluator_diff/cases/file_api_meta_special_seed_cases.cmake"
#define EGD_PACK_LEGACY_META "test_v2/evaluator_diff/cases/legacy_meta_special_seed_cases.cmake"
#define EGD_PACK_SEEDS "test_v2/evaluator_codegen_diff/cases/backend_seed_cases.cmake"

static const Test_Manifest_Request s_egd_build_manifests[] = {
    {TEST_MANIFEST_CAPTURE_TREE, "build_tree", "artifacts"},
};

static const Test_Manifest_Request s_egd_install_manifests[] = {
    {TEST_MANIFEST_CAPTURE_TREE, "install_tree", ""},
};

static const Test_Manifest_Request s_egd_export_manifests[] = {
    {TEST_MANIFEST_CAPTURE_FILE_TEXT, "export_targets", "build/meta-targets.cmake"},
    {TEST_MANIFEST_CAPTURE_FILE_TEXT, "export_install_set", "build/meta-export.cmake"},
};

static const EGD_Observed_Output s_egd_build_outputs[] = {
    {"build_tree", "artifacts", EGD_DIFF_TREE},
};

static const Test_Manifest_Request s_egd_configure_generation_manifests[] = {
    {TEST_MANIFEST_CAPTURE_TREE, "configure_tree", "generated"},
    {TEST_MANIFEST_CAPTURE_FILE_TEXT, "configured_text", "generated/configured.txt"},
    {TEST_MANIFEST_CAPTURE_FILE_TEXT, "write_append_text", "generated/write_append.txt"},
};

static const EGD_Observed_Output s_egd_configure_generation_outputs[] = {
    {"configure_tree", "generated", EGD_DIFF_TREE},
    {"configured_text", "generated/configured.txt", EGD_DIFF_FILE_TEXT},
    {"write_append_text", "generated/write_append.txt", EGD_DIFF_FILE_TEXT},
};

static const Test_Manifest_Request s_egd_configure_host_effect_manifests[] = {
    {TEST_MANIFEST_CAPTURE_TREE, "host_effect_tree", "replay"},
    {TEST_MANIFEST_CAPTURE_FILE_TEXT, "downloaded_text", "replay/downloaded.txt"},
};

static const EGD_Observed_Output s_egd_configure_host_effect_outputs[] = {
    {"host_effect_tree", "replay", EGD_DIFF_TREE},
    {"downloaded_text", "replay/downloaded.txt", EGD_DIFF_FILE_TEXT},
};

static const Test_Manifest_Request s_egd_fetchcontent_local_manifests[] = {
    {TEST_MANIFEST_CAPTURE_TREE, "fetchcontent_tree", "fc_base"},
    {TEST_MANIFEST_CAPTURE_FILE_TEXT, "saved_marker", "fc_base/saveddep-build/from_saved.txt"},
    {TEST_MANIFEST_CAPTURE_FILE_TEXT, "archive_marker", "fc_base/archivedep-build/from_archive.txt"},
};

static const EGD_Observed_Output s_egd_fetchcontent_local_outputs[] = {
    {"fetchcontent_tree", "fc_base", EGD_DIFF_TREE},
    {"saved_marker", "fc_base/saveddep-build/from_saved.txt", EGD_DIFF_FILE_TEXT},
    {"archive_marker", "fc_base/archivedep-build/from_archive.txt", EGD_DIFF_FILE_TEXT},
};

static const EGD_Observed_Output s_egd_install_outputs[] = {
    {"install_tree", "", EGD_DIFF_TREE},
};

static const EGD_Observed_Output s_egd_export_outputs[] = {
    {"export_targets", "build/meta-targets.cmake", EGD_DIFF_FILE_TEXT},
    {"export_install_set", "build/meta-export.cmake", EGD_DIFF_FILE_TEXT},
};

static const EGD_Observed_Output s_egd_package_outputs[] = {
    {"package_outputs", "packages", EGD_DIFF_TREE},
    {"package_payload", "", EGD_DIFF_TREE},
};

static const char *s_egd_command_names[] = {
#define EGD_COMMAND_NAME(name, handler, impl, fallback) name,
    EVAL_COMMAND_REGISTRY(EGD_COMMAND_NAME)
#undef EGD_COMMAND_NAME
};

static const EGD_Command_Inventory s_egd_command_inventory[] = {
#include "test_evaluator_codegen_diff_inventory.inc"
};

static const EGD_Corpus_Project_Inventory s_egd_corpus_project_inventory[] = {
    {"fmt", EGD_CORPUS_PROJECT_SUPPORTED, "Pinned fmt corpus project passes build/install/consumer parity and downstream consumer proof.", NULL},
    {"pugixml", EGD_CORPUS_PROJECT_SUPPORTED, "Pinned pugixml corpus project passes build/install/consumer parity and downstream consumer proof.", NULL},
    {"nlohmann_json", EGD_CORPUS_PROJECT_SUPPORTED, "Pinned nlohmann_json corpus project passes build/install/consumer parity and downstream consumer proof.", NULL},
    {"cjson", EGD_CORPUS_PROJECT_SUPPORTED, "Pinned cjson corpus project passes build/install/consumer parity and downstream consumer proof.", NULL},
};

static const EGD_Corpus_Finding_Inventory *s_egd_corpus_finding_inventory = NULL;
static const size_t s_egd_corpus_finding_inventory_count = 0;

static const EGD_Subcommand_Inventory s_egd_subcommand_inventory[] = {
    {"file", "DOWNLOAD", EGD_PACK_SEEDS, "backend_configure_host_effect_supported_surface", EGD_CLASS_PARITY_PASS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, "build-model.replay.configure", "local deterministic file(DOWNLOAD) now replays through configure-phase host-effect actions", NULL, "workload.codegen.configure-host-effects"},
    {"file", "ARCHIVE_CREATE|ARCHIVE_EXTRACT", EGD_PACK_SEEDS, "backend_configure_host_effect_supported_surface", EGD_CLASS_PARITY_PASS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, "build-model.replay.configure", "local pax archive create/extract now replays through configure-phase host-effect actions", NULL, "workload.codegen.configure-host-effects"},
    {"file", "GENERATE|LOCK", EGD_PACK_SEEDS, "backend_configure_host_effect_supported_surface", EGD_CLASS_PARITY_PASS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, "build-model.replay.configure", "deterministic file(GENERATE) and file(LOCK) variants now replay through configure-phase actions", NULL, "workload.codegen.configure-host-effects"},
    {"file", "GET_RUNTIME_DEPENDENCIES", EGD_PACK_FILE, "file_host_effect_generate_lock_and_runtime_deps_surface", EGD_CLASS_BACKEND_REJECT, EGD_PHASE_CONFIGURE, "replay.backlog.file-host-effects", "runtime dependency discovery remains an explicit backend reject in C2 because it does not replay as a deterministic configure effect", "epic-b.file.generate_lock_runtime", NULL},
    {"string", "APPEND|JOIN|CONFIGURE|REGEX|HASH", EGD_PACK_STRING, "string_text_regex_and_misc_surface", EGD_CLASS_EVALUATOR_ONLY, EGD_PHASE_CONFIGURE, "evaluator.frozen-semantic.string", "pure evaluator string semantics remain outside backend replay", NULL, NULL},
    {"list", "TRANSFORM|SORT", EGD_PACK_LIST, "list_sort_and_transform_selector_surface_matches_documented_combinations", EGD_CLASS_EVALUATOR_ONLY, EGD_PHASE_CONFIGURE, "evaluator.frozen-semantic.list", "list semantics only matter through frozen downstream state", NULL, NULL},
    {"math", "EXPR", EGD_PACK_MATH, "math_expr_precedence_bitwise_and_hex_output", EGD_CLASS_EVALUATOR_ONLY, EGD_PHASE_CONFIGURE, "evaluator.frozen-semantic.math", "math is evaluator-only and should not be replayed in nob.c", NULL, NULL},
    {"cmake_language", "CALL|EVAL|DEFER", EGD_PACK_CMAKE_LANGUAGE, "cmake_language_defer_queue_cancel_and_flush_surface", EGD_CLASS_EVALUATOR_ONLY, EGD_PHASE_CONFIGURE, "evaluator.frozen-semantic.cmake-language", "cmake_language stays on the evaluator side unless a later product decision moves specific host effects downstream", NULL, NULL},
    {"cmake_path", "SET|GET|APPEND|COMPARE", EGD_PACK_CMAKE_PATH, "cmake_path_extended_surface_matches_local_seed", EGD_CLASS_EVALUATOR_ONLY, EGD_PHASE_CONFIGURE, "evaluator.frozen-semantic.cmake-path", "path computation is evaluator-only unless it already freezes into target metadata", NULL, NULL},
    {"ctest_*", "ctest_empty_binary_directory|ctest_start|ctest_configure|ctest_build|ctest_test|ctest_sleep", EGD_PACK_CTEST, "ctest_local_dashboard_parity_surface", EGD_CLASS_PARITY_PASS, EGD_PHASE_TEST | EGD_PHASE_HOST_ONLY, "build-model.replay.test-driver", "local dashboard ctest steps now replay through the generated backend test-driver runtime", NULL, "workload.codegen.local-ctest"},
    {"ctest_*", "ctest_submit|ctest_upload|ctest_run_script|ctest_read_custom_files|ctest_update|ctest_coverage|ctest_memcheck", EGD_PACK_CTEST, "ctest_local_dashboard_surface", EGD_CLASS_BACKEND_REJECT, EGD_PHASE_TEST | EGD_PHASE_HOST_ONLY, "build-model.replay.test-driver", "networked, script, and probe-heavy ctest variants remain explicit backend rejects in the narrow local-only C3 surface", "epic-c.ctest-extended", NULL},
    {"FetchContent_*", "SOURCE_DIR|LOCAL_ARCHIVE", EGD_PACK_FETCHCONTENT, "fetchcontent_local_materialization_surface", EGD_CLASS_PARITY_PASS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, "build-model.replay.dependency-materialization", "local deterministic FetchContent source-dir and archive materialization now replay through the generated backend", NULL, "workload.codegen.fetchcontent-local"},
    {"FetchContent_*", "GetProperties", EGD_PACK_FETCHCONTENT, "fetchcontent_local_materialization_surface", EGD_CLASS_EVALUATOR_ONLY, EGD_PHASE_CONFIGURE, "evaluator.frozen-semantic.fetchcontent", "FetchContent_GetProperties only affects configure-time variables and remains evaluator-only", NULL, NULL},
    {"FetchContent_*", "provider|custom-command|VCS|remote", EGD_PACK_FETCHCONTENT, "fetchcontent_local_materialization_surface", EGD_CLASS_BACKEND_REJECT, EGD_PHASE_CONFIGURE, "build-model.replay.dependency-materialization", "provider, custom-command, VCS, and remote FetchContent variants remain explicit backend rejects in C3", "epic-c.fetchcontent", NULL},
};

static const EGD_Case_Def s_egd_cases[] = {
    {"backend_build_controlled_artifacts", EGD_PACK_SEEDS, "add_library", "build subtree parity", EGD_CLASS_PARITY_PASS, EGD_PARITY_BUILD_TREE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, EGD_TOOL_CMAKE, "build-model.build-graph", "positive backend-owned build artifact parity", NULL, "workload.codegen.build-tree", s_egd_build_outputs, NOB_ARRAY_LEN(s_egd_build_outputs), s_egd_build_manifests, NOB_ARRAY_LEN(s_egd_build_manifests), NULL},
    {"backend_install_supported_surface", EGD_PACK_SEEDS, "install", "supported install subset", EGD_CLASS_PARITY_PASS, EGD_PARITY_INSTALL_TREE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD | EGD_PHASE_INSTALL, EGD_TOOL_CMAKE, "build-model.install", "positive install parity for supported subset", NULL, "workload.codegen.install-tree", s_egd_install_outputs, NOB_ARRAY_LEN(s_egd_install_outputs), s_egd_install_manifests, NOB_ARRAY_LEN(s_egd_install_manifests), NULL},
    {"export_host_effect_target_and_export_file_surface", EGD_PACK_EXPORT, "export", "standalone export files", EGD_CLASS_PARITY_PASS, EGD_PARITY_EXPORT_FILES, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_EXPORT, EGD_TOOL_CMAKE, "build-model.export", "positive standalone export parity through explicit export command", NULL, "workload.codegen.export-files", s_egd_export_outputs, NOB_ARRAY_LEN(s_egd_export_outputs), s_egd_export_manifests, NOB_ARRAY_LEN(s_egd_export_manifests), NULL},
    {"backend_configure_generation_supported_surface", EGD_PACK_SEEDS, "write_file|make_directory|file(WRITE|APPEND|MAKE_DIRECTORY)|configure_file", "configure materialization parity", EGD_CLASS_PARITY_PASS, EGD_PARITY_BUILD_TREE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, EGD_TOOL_CMAKE, "build-model.replay.configure", "positive configure replay parity for deterministic text and directory materialization", NULL, "workload.codegen.configure-materialization", s_egd_configure_generation_outputs, NOB_ARRAY_LEN(s_egd_configure_generation_outputs), s_egd_configure_generation_manifests, NOB_ARRAY_LEN(s_egd_configure_generation_manifests), NULL},
    {"backend_configure_host_effect_supported_surface", EGD_PACK_SEEDS, "file(DOWNLOAD|ARCHIVE_CREATE|ARCHIVE_EXTRACT|GENERATE|LOCK)", "configure host-effect parity", EGD_CLASS_PARITY_PASS, EGD_PARITY_BUILD_TREE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, EGD_TOOL_CMAKE | EGD_TOOL_TAR, "build-model.replay.configure", "positive configure replay parity for supported deterministic host effects", NULL, "workload.codegen.configure-host-effects", s_egd_configure_host_effect_outputs, NOB_ARRAY_LEN(s_egd_configure_host_effect_outputs), s_egd_configure_host_effect_manifests, NOB_ARRAY_LEN(s_egd_configure_host_effect_manifests), NULL},
    {"backend_package_supported_archives", EGD_PACK_SEEDS, "include(CPack)", "package TGZ", EGD_CLASS_PARITY_PASS, EGD_PARITY_PACKAGE_ARCHIVE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_PACKAGE, EGD_TOOL_CMAKE | EGD_TOOL_CPACK | EGD_TOOL_TAR | EGD_TOOL_GZIP, "build-model.package", "positive full-package parity for TGZ", NULL, "workload.codegen.package-tgz", s_egd_package_outputs, NOB_ARRAY_LEN(s_egd_package_outputs), NULL, 0, "TGZ"},
    {"backend_package_supported_archives", EGD_PACK_SEEDS, "include(CPack)", "package TXZ", EGD_CLASS_PARITY_PASS, EGD_PARITY_PACKAGE_ARCHIVE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_PACKAGE, EGD_TOOL_CMAKE | EGD_TOOL_CPACK | EGD_TOOL_TAR | EGD_TOOL_XZ, "build-model.package", "positive full-package parity for TXZ", NULL, "workload.codegen.package-txz", s_egd_package_outputs, NOB_ARRAY_LEN(s_egd_package_outputs), NULL, 0, "TXZ"},
    {"backend_package_supported_archives", EGD_PACK_SEEDS, "include(CPack)", "package ZIP", EGD_CLASS_PARITY_PASS, EGD_PARITY_PACKAGE_ARCHIVE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_PACKAGE, EGD_TOOL_CMAKE | EGD_TOOL_CPACK | EGD_TOOL_PYTHON, "build-model.package", "positive full-package parity for ZIP", NULL, "workload.codegen.package-zip", s_egd_package_outputs, NOB_ARRAY_LEN(s_egd_package_outputs), NULL, 0, "ZIP"},
    {"fetchcontent_local_materialization_surface", EGD_PACK_FETCHCONTENT, "FetchContent_MakeAvailable", "local SOURCE_DIR + local archive materialization", EGD_CLASS_PARITY_PASS, EGD_PARITY_BUILD_TREE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, EGD_TOOL_CMAKE | EGD_TOOL_TAR, "build-model.replay.dependency-materialization", "focused C3 parity for local deterministic FetchContent materialization only", NULL, "workload.codegen.fetchcontent-local", s_egd_fetchcontent_local_outputs, NOB_ARRAY_LEN(s_egd_fetchcontent_local_outputs), s_egd_fetchcontent_local_manifests, NOB_ARRAY_LEN(s_egd_fetchcontent_local_manifests), NULL},
    {"backend_reject_target_precompile_headers", EGD_PACK_SEEDS, "target_precompile_headers", "PCH explicit reject", EGD_CLASS_BACKEND_REJECT, EGD_PARITY_NONE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_BUILD, EGD_TOOL_NONE, "replay.backlog.target-usage", "replay-domain foundation landed, but concrete precompile header semantics are still explicit backend rejects", "epic-a.target_precompile_headers", NULL, NULL, 0, NULL, 0, NULL},
    {"backend_reject_export_append", EGD_PACK_SEEDS, "export", "APPEND explicit reject", EGD_CLASS_BACKEND_REJECT, EGD_PARITY_NONE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE | EGD_PHASE_EXPORT, EGD_TOOL_NONE, "replay.backlog.export", "replay-domain foundation landed, but standalone export append remains an explicit backend reject", "epic-a.export-append", NULL, NULL, 0, NULL, 0, NULL},
    {"math_expr_precedence_bitwise_and_hex_output", EGD_PACK_MATH, "math", "math(EXPR)", EGD_CLASS_EVALUATOR_ONLY, EGD_PARITY_NONE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE, EGD_TOOL_NONE, "evaluator.frozen-semantic.math", "pure evaluator arithmetic should stay outside backend replay", NULL, NULL, NULL, 0, NULL, 0, NULL},
    {"string_text_regex_and_misc_surface", EGD_PACK_STRING, "string", "string() surface", EGD_CLASS_EVALUATOR_ONLY, EGD_PARITY_NONE, EGD_OUTCOME_SUCCESS, EGD_PHASE_CONFIGURE, EGD_TOOL_NONE, "evaluator.frozen-semantic.string", "string operations are evaluator-only unless they already freeze into downstream state", NULL, NULL, NULL, 0, NULL, 0, NULL},
};

static bool egd_body_contains(String_View body, const char *needle);
static bool egd_host_program_available(const char *program);

static bool egd_ends_with(const char *text, const char *suffix) {
    size_t text_len = text ? strlen(text) : 0;
    size_t suffix_len = suffix ? strlen(suffix) : 0;
    if (!text || !suffix || suffix_len > text_len) return false;
    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

static String_View egd_copy_sv(Arena *arena, String_View sv) {
    char *copy = NULL;
    if (!arena) return nob_sv_from_cstr("");
    copy = arena_strndup(arena, sv.data ? sv.data : "", sv.count);
    if (!copy) return nob_sv_from_cstr("");
    return nob_sv_from_parts(copy, sv.count);
}

static const EGD_Command_Inventory *egd_lookup_command_inventory(const char *command) {
    if (!command) return NULL;
    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_command_inventory); ++i) {
        if (strcmp(s_egd_command_inventory[i].command, command) == 0) {
            return &s_egd_command_inventory[i];
        }
    }
    return NULL;
}

static bool egd_tracking_key_required(EGD_Case_Classification classification) {
    return classification == EGD_CLASS_BACKEND_REJECT ||
           classification == EGD_CLASS_EXPLICIT_NON_GOAL;
}

static bool egd_phase_mask_is_valid(unsigned phase_mask) {
    unsigned known = EGD_PHASE_CONFIGURE |
                     EGD_PHASE_BUILD |
                     EGD_PHASE_TEST |
                     EGD_PHASE_INSTALL |
                     EGD_PHASE_EXPORT |
                     EGD_PHASE_PACKAGE |
                     EGD_PHASE_HOST_ONLY;
    return phase_mask != 0 && (phase_mask & ~known) == 0;
}

static bool egd_tool_mask_is_valid(unsigned tool_mask) {
    unsigned known = EGD_TOOL_NONE |
                     EGD_TOOL_CMAKE |
                     EGD_TOOL_CPACK |
                     EGD_TOOL_GZIP |
                     EGD_TOOL_XZ |
                     EGD_TOOL_PYTHON |
                     EGD_TOOL_TAR;
    return (tool_mask & ~known) == 0;
}

static const char *egd_classification_name(EGD_Case_Classification classification) {
    switch (classification) {
        case EGD_CLASS_PARITY_PASS: return "parity-pass";
        case EGD_CLASS_BACKEND_REJECT: return "backend-reject";
        case EGD_CLASS_EVALUATOR_ONLY: return "evaluator-only";
        case EGD_CLASS_EXPLICIT_NON_GOAL: return "explicit-non-goal";
    }
    return "unknown";
}

static const char *egd_corpus_project_disposition_name(EGD_Corpus_Project_Disposition disposition) {
    switch (disposition) {
        case EGD_CORPUS_PROJECT_SUPPORTED: return "supported";
        case EGD_CORPUS_PROJECT_KNOWN_BOUNDARY: return "known-boundary";
        case EGD_CORPUS_PROJECT_BACKEND_GAP: return "backend-gap";
    }
    return "unknown";
}

static const char *egd_corpus_finding_severity_name(EGD_Corpus_Finding_Severity severity) {
    switch (severity) {
        case EGD_CORPUS_SEVERITY_RELEASE_BLOCKER: return "release-blocker";
        case EGD_CORPUS_SEVERITY_MAJOR: return "major";
        case EGD_CORPUS_SEVERITY_MINOR: return "minor";
    }
    return "unknown";
}

static const char *egd_corpus_finding_frequency_name(EGD_Corpus_Finding_Frequency frequency) {
    switch (frequency) {
        case EGD_CORPUS_FREQUENCY_COMMON: return "common";
        case EGD_CORPUS_FREQUENCY_PROJECT_LOCAL: return "project-local";
    }
    return "unknown";
}

static const char *egd_corpus_finding_disposition_name(EGD_Corpus_Finding_Disposition disposition) {
    switch (disposition) {
        case EGD_CORPUS_DISPOSITION_FOCUSED_PROOF_CASE: return "focused-proof-case";
        case EGD_CORPUS_DISPOSITION_BACKEND_REJECT: return "backend-reject";
        case EGD_CORPUS_DISPOSITION_EXPLICIT_BOUNDARY: return "explicit-boundary";
    }
    return "unknown";
}

static bool egd_join_abs_from_repo(const char *repo_relpath,
                                   char out_path[_TINYDIR_PATH_MAX]) {
    const char *repo_root = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    if (!repo_root || repo_root[0] == '\0' || !repo_relpath || !out_path) return false;
    return snprintf(out_path, _TINYDIR_PATH_MAX, "%s/%s", repo_root, repo_relpath) < _TINYDIR_PATH_MAX;
}

static EGD_Corpus_Project_Disposition egd_manifest_support_tier_to_disposition(
    const char *support_tier) {
    if (!support_tier) return (EGD_Corpus_Project_Disposition)-1;
    if (strcmp(support_tier, "supported") == 0) return EGD_CORPUS_PROJECT_SUPPORTED;
    if (strcmp(support_tier, "known-boundary") == 0) return EGD_CORPUS_PROJECT_KNOWN_BOUNDARY;
    if (strcmp(support_tier, "backend-gap") == 0) return EGD_CORPUS_PROJECT_BACKEND_GAP;
    return (EGD_Corpus_Project_Disposition)-1;
}

static const EGD_Corpus_Project_Inventory *egd_lookup_corpus_project_inventory(
    const char *project_name) {
    if (!project_name) return NULL;
    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_corpus_project_inventory); ++i) {
        if (strcmp(s_egd_corpus_project_inventory[i].project_name, project_name) == 0) {
            return &s_egd_corpus_project_inventory[i];
        }
    }
    return NULL;
}

static const EGD_Corpus_Finding_Inventory *egd_lookup_corpus_finding_inventory(const char *key) {
    if (!key || key[0] == '\0') return NULL;
    for (size_t i = 0; i < s_egd_corpus_finding_inventory_count; ++i) {
        if (strcmp(s_egd_corpus_finding_inventory[i].key, key) == 0) {
            return &s_egd_corpus_finding_inventory[i];
        }
    }
    return NULL;
}

static bool egd_corpus_project_inventory_is_valid(const EGD_Corpus_Project_Inventory *item) {
    if (!item ||
        !item->project_name || item->project_name[0] == '\0' ||
        !item->reason || item->reason[0] == '\0' ||
        strcmp(egd_corpus_project_disposition_name(item->disposition), "unknown") == 0) {
        return false;
    }
    if (item->disposition != EGD_CORPUS_PROJECT_SUPPORTED &&
        (!item->tracking_or_boundary_key || item->tracking_or_boundary_key[0] == '\0')) {
        return false;
    }
    if (item->tracking_or_boundary_key && item->tracking_or_boundary_key[0] == '\0') return false;
    return true;
}

static bool egd_corpus_finding_inventory_is_valid(const EGD_Corpus_Finding_Inventory *item) {
    if (!item ||
        !item->key || item->key[0] == '\0' ||
        !item->reason || item->reason[0] == '\0' ||
        item->affected_project_count == 0 ||
        !item->affected_projects ||
        strcmp(egd_corpus_finding_severity_name(item->severity), "unknown") == 0 ||
        strcmp(egd_corpus_finding_frequency_name(item->frequency), "unknown") == 0 ||
        strcmp(egd_corpus_finding_disposition_name(item->disposition), "unknown") == 0) {
        return false;
    }
    for (size_t i = 0; i < item->affected_project_count; ++i) {
        if (!item->affected_projects[i] || item->affected_projects[i][0] == '\0') return false;
    }
    return true;
}

static void egd_inventory_state_counts_add(EGD_Inventory_State_Counts *counts,
                                           EGD_Case_Classification classification) {
    if (!counts) return;
    switch (classification) {
        case EGD_CLASS_PARITY_PASS: counts->parity_pass++; break;
        case EGD_CLASS_BACKEND_REJECT: counts->backend_reject++; break;
        case EGD_CLASS_EVALUATOR_ONLY: counts->evaluator_only++; break;
        case EGD_CLASS_EXPLICIT_NON_GOAL: counts->explicit_non_goal++; break;
    }
}

static void egd_log_inventory_state_counts(const char *label,
                                           const EGD_Inventory_State_Counts *counts) {
    if (!label || !counts) return;
    nob_log(NOB_INFO,
            "%s: parity-pass=%d backend-reject=%d evaluator-only=%d explicit-non-goal=%d",
            label,
            counts->parity_pass,
            counts->backend_reject,
            counts->evaluator_only,
            counts->explicit_non_goal);
}

static const char *egd_diff_primitive_name(EGD_Diff_Primitive primitive) {
    switch (primitive) {
        case EGD_DIFF_TREE: return "TREE";
        case EGD_DIFF_FILE_TEXT: return "FILE_TEXT";
        case EGD_DIFF_FILE_SHA256: return "FILE_SHA256";
    }
    return "unknown";
}

static const char *egd_tool_name(unsigned tool) {
    switch (tool) {
        case EGD_TOOL_CMAKE: return "cmake";
        case EGD_TOOL_CPACK: return "cpack";
        case EGD_TOOL_GZIP: return "gzip";
        case EGD_TOOL_XZ: return "xz";
        case EGD_TOOL_PYTHON: return "python";
        case EGD_TOOL_TAR: return "tar";
    }
    return "unknown-tool";
}

static void egd_format_tool_mask(unsigned tool_mask, char *buffer, size_t buffer_size) {
    size_t used = 0;
    bool first = true;
    const unsigned tools[] = {
        EGD_TOOL_CMAKE,
        EGD_TOOL_CPACK,
        EGD_TOOL_GZIP,
        EGD_TOOL_XZ,
        EGD_TOOL_PYTHON,
        EGD_TOOL_TAR,
    };

    if (!buffer || buffer_size == 0) return;
    buffer[0] = '\0';
    if (tool_mask == EGD_TOOL_NONE) {
        (void)snprintf(buffer, buffer_size, "none");
        return;
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(tools); ++i) {
        if ((tool_mask & tools[i]) == 0) continue;
        used += (size_t)snprintf(buffer + used,
                                 used < buffer_size ? buffer_size - used : 0,
                                 "%s%s",
                                 first ? "" : ",",
                                 egd_tool_name(tools[i]));
        first = false;
        if (used >= buffer_size) break;
    }
}

static bool egd_detect_tool_availability(EGD_Tool_Availability *out) {
    char cmake_bin[_TINYDIR_PATH_MAX] = {0};
    char cpack_bin[_TINYDIR_PATH_MAX] = {0};

    if (!out) return false;
    *out = (EGD_Tool_Availability){
        .cmake = codegen_resolve_host_cmake_bin(cmake_bin),
        .cpack = codegen_resolve_host_cpack_bin(cpack_bin),
        .gzip = egd_host_program_available("gzip"),
        .xz = egd_host_program_available("xz"),
        .python = egd_host_program_available("python3") || egd_host_program_available("python"),
        .tar = egd_host_program_available("tar"),
    };
    return true;
}

static unsigned egd_required_tools_missing(unsigned required_tools,
                                           const EGD_Tool_Availability *available) {
    unsigned missing = 0;
    if (!available) return required_tools;
    if ((required_tools & EGD_TOOL_CMAKE) && !available->cmake) missing |= EGD_TOOL_CMAKE;
    if ((required_tools & EGD_TOOL_CPACK) && !available->cpack) missing |= EGD_TOOL_CPACK;
    if ((required_tools & EGD_TOOL_GZIP) && !available->gzip) missing |= EGD_TOOL_GZIP;
    if ((required_tools & EGD_TOOL_XZ) && !available->xz) missing |= EGD_TOOL_XZ;
    if ((required_tools & EGD_TOOL_PYTHON) && !available->python) missing |= EGD_TOOL_PYTHON;
    if ((required_tools & EGD_TOOL_TAR) && !available->tar) missing |= EGD_TOOL_TAR;
    return missing;
}

static bool egd_command_name_in_registry(const char *command) {
    if (!command) return false;
    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_command_names); ++i) {
        if (strcmp(s_egd_command_names[i], command) == 0) return true;
    }
    return false;
}

static bool egd_observed_output_is_valid(const EGD_Observed_Output *output) {
    if (!output || !output->label || output->label[0] == '\0' || !output->relpath) return false;
    switch (output->primitive) {
        case EGD_DIFF_TREE:
            return true;
        case EGD_DIFF_FILE_TEXT:
        case EGD_DIFF_FILE_SHA256:
            return output->relpath[0] != '\0';
    }
    return false;
}

static bool egd_subcommand_metadata_is_valid(const EGD_Subcommand_Inventory *item) {
    if (!item ||
        !item->family || item->family[0] == '\0' ||
        !item->signature || item->signature[0] == '\0' ||
        !item->source_pack_path || item->source_pack_path[0] == '\0' ||
        !item->case_name || item->case_name[0] == '\0' ||
        !item->domain_owner || item->domain_owner[0] == '\0' ||
        !item->reason || item->reason[0] == '\0') {
        return false;
    }
    if (!egd_phase_mask_is_valid(item->phase_mask)) return false;
    if (egd_tracking_key_required(item->classification) &&
        (!item->tracking_key || item->tracking_key[0] == '\0')) {
        return false;
    }
    if (item->tracking_key && item->tracking_key[0] == '\0') return false;
    if (item->workload_key && item->workload_key[0] == '\0') return false;
    return strcmp(egd_classification_name(item->classification), "unknown") != 0;
}

static bool egd_case_metadata_is_valid(const EGD_Case_Def *case_def,
                                       const EGD_Parsed_Case *parsed_case) {
    if (!case_def ||
        !case_def->case_name || case_def->case_name[0] == '\0' ||
        !case_def->source_pack_path || case_def->source_pack_path[0] == '\0' ||
        !case_def->command_family || case_def->command_family[0] == '\0' ||
        !case_def->signature || case_def->signature[0] == '\0' ||
        !case_def->domain_owner || case_def->domain_owner[0] == '\0' ||
        !case_def->reason || case_def->reason[0] == '\0') {
        return false;
    }
    if (!egd_phase_mask_is_valid(case_def->phase_mask) ||
        !egd_tool_mask_is_valid(case_def->required_tools) ||
        strcmp(egd_classification_name(case_def->classification), "unknown") == 0) {
        return false;
    }
    if (parsed_case && parsed_case->expected_outcome != case_def->expected_outcome) return false;
    if (egd_tracking_key_required(case_def->classification) &&
        (!case_def->tracking_key || case_def->tracking_key[0] == '\0')) {
        return false;
    }
    if (case_def->tracking_key && case_def->tracking_key[0] == '\0') return false;
    if (case_def->workload_key && case_def->workload_key[0] == '\0') return false;

    if (case_def->classification == EGD_CLASS_PARITY_PASS) {
        if (case_def->parity_kind == EGD_PARITY_NONE ||
            case_def->expected_outcome != EGD_OUTCOME_SUCCESS ||
            case_def->observed_output_count == 0 ||
            !case_def->observed_outputs) {
            return false;
        }
    } else if (case_def->parity_kind != EGD_PARITY_NONE ||
               case_def->observed_output_count != 0 ||
               case_def->observed_outputs != NULL ||
               case_def->manifest_request_count != 0 ||
               case_def->manifest_requests != NULL ||
               case_def->package_generator != NULL) {
        return false;
    }

    if (case_def->manifest_request_count > 0 && !case_def->manifest_requests) return false;
    if (case_def->parity_kind == EGD_PARITY_PACKAGE_ARCHIVE) {
        if (!case_def->package_generator || case_def->package_generator[0] == '\0') return false;
    } else if (case_def->package_generator != NULL) {
        return false;
    }

    for (size_t i = 0; i < case_def->observed_output_count; ++i) {
        if (!egd_observed_output_is_valid(&case_def->observed_outputs[i])) return false;
        if (strcmp(egd_diff_primitive_name(case_def->observed_outputs[i].primitive), "unknown") == 0) {
            return false;
        }
    }

    return true;
}

static size_t egd_count_full_native_rows(String_View matrix) {
    size_t count = 0;
    size_t pos = 0;
    while (pos < matrix.count) {
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < matrix.count && matrix.data[line_end] != '\n') line_end++;
        pos = line_end < matrix.count ? line_end + 1 : line_end;

        String_View line = test_case_pack_trim_cr(
            nob_sv_from_parts(matrix.data + line_start, line_end - line_start));
        if (line.count >= 3 &&
            line.data[0] == '|' &&
            line.data[1] == ' ' &&
            line.data[2] == '`' &&
            egd_body_contains(line, "| native | FULL | FULL |")) {
            count++;
        }
    }
    return count;
}

static void egd_log_curated_subcommand_family_counts(void) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_subcommand_inventory); ++i) {
        EGD_Inventory_State_Counts counts = {0};
        bool seen = false;
        const char *family = s_egd_subcommand_inventory[i].family;
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(s_egd_subcommand_inventory[j].family, family) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        for (size_t j = 0; j < NOB_ARRAY_LEN(s_egd_subcommand_inventory); ++j) {
            if (strcmp(s_egd_subcommand_inventory[j].family, family) == 0) {
                egd_inventory_state_counts_add(&counts, s_egd_subcommand_inventory[j].classification);
            }
        }
        egd_log_inventory_state_counts(
            nob_temp_sprintf("evaluator->codegen diff curated subcommand family %s", family),
            &counts);
    }
}

static void egd_log_case_family_counts(void) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_cases); ++i) {
        EGD_Inventory_State_Counts counts = {0};
        bool seen = false;
        const char *family = s_egd_cases[i].command_family;
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(s_egd_cases[j].command_family, family) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        for (size_t j = 0; j < NOB_ARRAY_LEN(s_egd_cases); ++j) {
            if (strcmp(s_egd_cases[j].command_family, family) == 0) {
                egd_inventory_state_counts_add(&counts, s_egd_cases[j].classification);
            }
        }
        egd_log_inventory_state_counts(
            nob_temp_sprintf("evaluator->codegen diff case family %s", family),
            &counts);
    }
}

static bool egd_case_exists_in_pack(Arena *arena,
                                    const char *case_pack_path,
                                    const char *case_name) {
    return test_case_dsl_case_exists_in_pack(arena, case_pack_path, case_name);
}

static bool egd_load_case_from_pack(Arena *arena,
                                    const char *case_pack_path,
                                    const char *case_name,
                                    EGD_Parsed_Case *out_case) {
    return test_case_dsl_load_case_from_pack(arena, case_pack_path, case_name, out_case);
}

static bool egd_body_contains(String_View body, const char *needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    if (!needle || needle_len == 0 || body.count < needle_len) return false;
    for (size_t i = 0; i + needle_len <= body.count; ++i) {
        if (memcmp(body.data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static bool egd_load_repo_text_file(Arena *arena,
                                    const char *repo_relpath,
                                    String_View *out_text) {
    char abs_path[_TINYDIR_PATH_MAX] = {0};
    if (out_text) *out_text = nob_sv_from_cstr("");
    if (!arena || !repo_relpath || !out_text) return false;
    if (!egd_join_abs_from_repo(repo_relpath, abs_path)) return false;
    return test_snapshot_load_text_file_to_arena(arena, abs_path, out_text);
}

static bool egd_ensure_dir_chain(const char *path) {
    char buffer[_TINYDIR_PATH_MAX] = {0};
    size_t len = 0;
    if (!path || path[0] == '\0' || strcmp(path, ".") == 0) return true;
    len = strlen(path);
    if (len + 1 > sizeof(buffer)) return false;
    memcpy(buffer, path, len + 1);
    for (size_t i = 1; i < len; ++i) {
        if (buffer[i] != '/') continue;
        buffer[i] = '\0';
        if (buffer[0] != '\0' && !nob_mkdir_if_not_exists(buffer)) return false;
        buffer[i] = '/';
    }
    return nob_mkdir_if_not_exists(buffer);
}

static bool egd_ensure_parent_dir(const char *path) {
    size_t temp_mark = nob_temp_save();
    const char *dir = nob_temp_dir_name(path);
    bool ok = egd_ensure_dir_chain(dir);
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool egd_write_text_file(const char *path, String_View text) {
    FILE *f = NULL;
    if (!path) return false;
    if (!egd_ensure_parent_dir(path)) return false;
    f = fopen(path, "wb");
    if (!f) return false;
    if (text.count > 0 && fwrite(text.data, 1, text.count, f) != text.count) {
        fclose(f);
        return false;
    }
    return fclose(f) == 0;
}

static bool egd_make_file_executable_if_needed(const char *path) {
#if defined(_WIN32)
    (void)path;
    return true;
#else
    if (!path) return false;
    if (!(egd_ends_with(path, ".sh") || egd_ends_with(path, ".py"))) return true;
    return chmod(path, 0755) == 0;
#endif
}

static bool egd_resolve_scoped_path(EGD_Path_Scope scope,
                                    String_View relpath,
                                    const char *source_dir,
                                    const char *build_dir,
                                    char out[_TINYDIR_PATH_MAX]) {
    const char *root = scope == EGD_SCOPE_SOURCE ? source_dir : build_dir;
    if (!root || !out) return false;
    if (relpath.count == 0) {
        return snprintf(out, _TINYDIR_PATH_MAX, "%s", root) < _TINYDIR_PATH_MAX;
    }
    return test_fs_join_path(root, nob_temp_sv_to_cstr(relpath), out);
}

static bool egd_materialize_case_fixtures(const EGD_Parsed_Case *parsed_case,
                                          const char *source_dir,
                                          const char *build_dir) {
    if (!parsed_case || !source_dir || !build_dir) return false;

    for (size_t i = 0; i < arena_arr_len(parsed_case->dirs); ++i) {
        char path[_TINYDIR_PATH_MAX] = {0};
        if (!egd_resolve_scoped_path(parsed_case->dirs[i].scope,
                                     parsed_case->dirs[i].relpath,
                                     source_dir,
                                     build_dir,
                                     path) ||
            !egd_ensure_dir_chain(path)) {
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(parsed_case->files); ++i) {
        char path[_TINYDIR_PATH_MAX] = {0};
        if (!egd_resolve_scoped_path(parsed_case->files[i].scope,
                                     parsed_case->files[i].relpath,
                                     source_dir,
                                     build_dir,
                                     path) ||
            !egd_write_text_file(path, nob_sv_from_cstr(""))) {
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(parsed_case->text_files); ++i) {
        char path[_TINYDIR_PATH_MAX] = {0};
        if (!egd_resolve_scoped_path(parsed_case->text_files[i].scope,
                                     parsed_case->text_files[i].relpath,
                                     source_dir,
                                     build_dir,
                                     path) ||
            !egd_write_text_file(path, parsed_case->text_files[i].text) ||
            !egd_make_file_executable_if_needed(path)) {
            return false;
        }
    }

    return true;
}

static bool egd_generate_case_script(Arena *arena,
                                     const EGD_Parsed_Case *parsed_case,
                                     const char *script_path,
                                     String_View *out_script) {
    Nob_String_Builder sb = {0};
    String_View final = {0};
    if (out_script) *out_script = nob_sv_from_cstr("");
    if (!arena || !parsed_case || !script_path || !out_script) return false;

    if (parsed_case->mode == EGD_MODE_PROJECT &&
        !egd_body_contains(parsed_case->body, "project(") &&
        !egd_body_contains(parsed_case->body, "cmake_minimum_required(")) {
        nob_sb_append_cstr(&sb, "cmake_minimum_required(VERSION 3.28)\n");
        nob_sb_append_cstr(&sb, "project(EvaluatorCodegenDiff LANGUAGES C CXX)\n");
    }

    nob_sb_append_buf(&sb, parsed_case->body.data ? parsed_case->body.data : "", parsed_case->body.count);
    if (sb.count == 0 || sb.items[sb.count - 1] != '\n') nob_sb_append(&sb, '\n');
    final = egd_copy_sv(arena, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (!final.data) return false;
    if (!egd_write_text_file(script_path, final)) return false;
    *out_script = final;
    return true;
}

static bool egd_run_argv_in_dir(const char *dir,
                                const char *const *argv,
                                size_t argc) {
    Nob_Cmd cmd = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;
    if (!dir || !argv || argc == 0 || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);
    if (!nob_set_current_dir(dir)) return false;
    for (size_t i = 0; i < argc; ++i) {
        if (!argv[i]) continue;
        nob_cmd_append(&cmd, argv[i]);
    }
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    if (!nob_set_current_dir(prev_cwd)) return false;
    return ok;
}

static bool egd_host_program_available(const char *program) {
    char path[_TINYDIR_PATH_MAX] = {0};
    return program && test_ws_host_program_in_path(program, path);
}

static const char *egd_package_extension(const char *generator) {
    if (!generator) return "";
    if (strcmp(generator, "TGZ") == 0) return ".tar.gz";
    if (strcmp(generator, "TXZ") == 0) return ".tar.xz";
    if (strcmp(generator, "ZIP") == 0) return ".zip";
    return "";
}

static bool egd_extract_archive_to_dir(const char *archive_path,
                                       const char *generator,
                                       const char *dst_dir) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    char python_bin[_TINYDIR_PATH_MAX] = {0};
    if (!archive_path || !generator || !dst_dir) return false;
    if (!egd_ensure_dir_chain(dst_dir)) return false;

    if (strcmp(generator, "TGZ") == 0 || strcmp(generator, "TXZ") == 0) {
        nob_cmd_append(&cmd, "tar", "-xf", archive_path, "-C", dst_dir);
        ok = nob_cmd_run(&cmd);
        nob_cmd_free(cmd);
        return ok;
    }

    if (strcmp(generator, "ZIP") == 0) {
        if (!test_ws_host_program_in_path("python3", python_bin) &&
            !test_ws_host_program_in_path("python", python_bin)) {
            return false;
        }
        nob_cmd_append(&cmd,
                       python_bin,
                       "-c",
                       "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])",
                       archive_path,
                       dst_dir);
        ok = nob_cmd_run(&cmd);
        nob_cmd_free(cmd);
        return ok;
    }

    return false;
}

static bool egd_confirm_case_modeled(const char *script_text,
                                     const char *input_path,
                                     const char *source_dir,
                                     const char *binary_dir) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    bool ok = false;
    test_semantic_pipeline_config_init(&config);
    config.current_file = input_path;
    config.source_dir = nob_sv_from_cstr(source_dir);
    config.binary_dir = nob_sv_from_cstr(binary_dir);
    config.override_enable_export_host_effects = true;
    config.enable_export_host_effects = false;
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ok = test_semantic_pipeline_fixture_from_script(&fixture, script_text, &config);
    if (!ok || !fixture.eval_ok || diag_has_errors()) {
        test_semantic_pipeline_fixture_destroy(&fixture);
        return false;
    }
    test_semantic_pipeline_fixture_destroy(&fixture);
    return true;
}

static bool egd_compare_manifests(Arena *arena,
                                  const char *subject,
                                  const char *expected_base_dir,
                                  const char *actual_base_dir,
                                  const Test_Manifest_Request *requests,
                                  size_t request_count) {
    String_View expected = {0};
    String_View actual = {0};
    if (!arena || !subject || !expected_base_dir || !actual_base_dir || !requests) return false;
    return test_manifest_capture(arena, expected_base_dir, requests, request_count, &expected) &&
           test_manifest_capture(arena, actual_base_dir, requests, request_count, &actual) &&
           test_manifest_assert_equal(arena, subject, "cmake manifest", "nob manifest", expected, actual);
}

static bool egd_run_case(const EGD_Case_Def *case_def,
                         EGD_Case_Summary *summary) {
    Arena *arena = arena_create(8 * 1024 * 1024);
    EGD_Parsed_Case parsed = {0};
    EGD_Tool_Availability available = {0};
    String_View script_text = {0};
    char cmake_bin[_TINYDIR_PATH_MAX] = {0};
    char cpack_bin[_TINYDIR_PATH_MAX] = {0};
    char missing_tools[128] = {0};
    char cmake_src[_TINYDIR_PATH_MAX] = {0};
    char nob_src[_TINYDIR_PATH_MAX] = {0};
    char cmake_build[_TINYDIR_PATH_MAX] = {0};
    char nob_build[_TINYDIR_PATH_MAX] = {0};
    char cmake_script[_TINYDIR_PATH_MAX] = {0};
    char nob_script[_TINYDIR_PATH_MAX] = {0};
    char generated_nob[_TINYDIR_PATH_MAX] = {0};
    char generated_bin[_TINYDIR_PATH_MAX] = {0};
    char cmake_install[_TINYDIR_PATH_MAX] = {0};
    char nob_install[_TINYDIR_PATH_MAX] = {0};
    char cmake_extract[_TINYDIR_PATH_MAX] = {0};
    char nob_extract[_TINYDIR_PATH_MAX] = {0};
    char cmake_archive[_TINYDIR_PATH_MAX] = {0};
    char nob_archive[_TINYDIR_PATH_MAX] = {0};
    const char *configure_argv[] = {"", "-S", cmake_src, "-B", cmake_build};
    const char *build_argv[] = {"", "--build", cmake_build};
    const char *install_argv[] = {"", "--install", cmake_build, "--prefix", cmake_install};
    const char *nob_build_argv[] = {NULL};
    const char *nob_install_argv[] = {"install", "--prefix", nob_install};
    const char *nob_export_argv[] = {"export"};
    const char *nob_package_argv[] = {"package", "--generator", NULL};
    Codegen_Test_Config config = {0};
    bool ok = false;

    if (!arena || !case_def || !summary) {
        arena_destroy(arena);
        return false;
    }

    if (!egd_load_case_from_pack(arena, case_def->source_pack_path, case_def->case_name, &parsed)) {
        arena_destroy(arena);
        return false;
    }
    if (!egd_case_metadata_is_valid(case_def, &parsed)) {
        arena_destroy(arena);
        return false;
    }

    if (snprintf(cmake_src, sizeof(cmake_src), "%s_cmake_src", case_def->case_name) >= (int)sizeof(cmake_src) ||
        snprintf(nob_src, sizeof(nob_src), "%s_nob_src", case_def->case_name) >= (int)sizeof(nob_src) ||
        snprintf(cmake_build, sizeof(cmake_build), "%s_cmake_side/build", case_def->case_name) >= (int)sizeof(cmake_build) ||
        snprintf(nob_build, sizeof(nob_build), "%s_nob_side/build", case_def->case_name) >= (int)sizeof(nob_build) ||
        snprintf(cmake_script, sizeof(cmake_script), "%s/CMakeLists.txt", cmake_src) >= (int)sizeof(cmake_script) ||
        snprintf(nob_script, sizeof(nob_script), "%s/CMakeLists.txt", nob_src) >= (int)sizeof(nob_script) ||
        snprintf(generated_nob, sizeof(generated_nob), "%s/generated/nob.c", nob_src) >= (int)sizeof(generated_nob) ||
        snprintf(generated_bin, sizeof(generated_bin), "%s/generated/nob_gen", nob_src) >= (int)sizeof(generated_bin) ||
        snprintf(cmake_install, sizeof(cmake_install), "%s_cmake_install", case_def->case_name) >= (int)sizeof(cmake_install) ||
        snprintf(nob_install, sizeof(nob_install), "%s_nob_install", case_def->case_name) >= (int)sizeof(nob_install) ||
        snprintf(cmake_extract, sizeof(cmake_extract), "%s_cmake_extract", case_def->case_name) >= (int)sizeof(cmake_extract) ||
        snprintf(nob_extract, sizeof(nob_extract), "%s_nob_extract", case_def->case_name) >= (int)sizeof(nob_extract)) {
        arena_destroy(arena);
        return false;
    }

    if (!egd_materialize_case_fixtures(&parsed, cmake_src, cmake_build) ||
        !egd_materialize_case_fixtures(&parsed, nob_src, nob_build) ||
        !egd_generate_case_script(arena, &parsed, cmake_script, &script_text) ||
        !egd_generate_case_script(arena, &parsed, nob_script, &script_text)) {
        arena_destroy(arena);
        return false;
    }

    if (!egd_confirm_case_modeled(script_text.data, nob_script, nob_src, nob_build)) {
        arena_destroy(arena);
        return false;
    }

    if (case_def->classification == EGD_CLASS_EXPLICIT_NON_GOAL) {
        summary->explicit_non_goal++;
        arena_destroy(arena);
        return true;
    }

    if (case_def->classification == EGD_CLASS_EVALUATOR_ONLY) {
        summary->evaluator_only++;
        arena_destroy(arena);
        return true;
    }

    config.input_path = nob_script;
    config.output_path = generated_nob;
    config.source_dir = nob_src;
    config.binary_dir = nob_build;
    config.disable_export_host_effects = true;

    if (case_def->classification == EGD_CLASS_BACKEND_REJECT) {
        Nob_String_Builder render = {0};
        diag_reset();
        diag_set_strict(false);
        diag_telemetry_reset();
        ok = !codegen_render_script_with_config(script_text.data, &config, &render);
        nob_sb_free(render);
        if (ok) summary->backend_rejected++;
        arena_destroy(arena);
        return ok;
    }

    if (!egd_detect_tool_availability(&available)) {
        arena_destroy(arena);
        return false;
    }
    if (egd_required_tools_missing(case_def->required_tools, &available) != EGD_TOOL_NONE) {
        egd_format_tool_mask(egd_required_tools_missing(case_def->required_tools, &available),
                             missing_tools,
                             sizeof(missing_tools));
        nob_log(NOB_INFO,
                "evaluator->codegen diff case %s skipped: missing required tool(s): %s",
                case_def->case_name,
                missing_tools);
        summary->skipped_by_tool++;
        arena_destroy(arena);
        return true;
    }
    if (!codegen_resolve_host_cmake_bin(cmake_bin)) {
        arena_destroy(arena);
        return false;
    }

    configure_argv[0] = cmake_bin;
    build_argv[0] = cmake_bin;
    install_argv[0] = cmake_bin;

    if (!egd_run_argv_in_dir(".", configure_argv, NOB_ARRAY_LEN(configure_argv)) ||
        !codegen_write_script_with_config(script_text.data, &config) ||
        !codegen_compile_generated_nob(generated_nob, generated_bin)) {
        arena_destroy(arena);
        return false;
    }

    switch (case_def->parity_kind) {
        case EGD_PARITY_BUILD_TREE:
            ok = egd_run_argv_in_dir(".", build_argv, NOB_ARRAY_LEN(build_argv)) &&
                 codegen_run_binary_in_dir_argv(nob_temp_dir_name(generated_nob),
                                                nob_temp_sprintf("./%s", nob_temp_file_name(generated_bin)),
                                                nob_build_argv,
                                                0) &&
                 egd_compare_manifests(arena,
                                       case_def->signature,
                                       cmake_build,
                                       nob_build,
                                       case_def->manifest_requests,
                                       case_def->manifest_request_count);
            break;

        case EGD_PARITY_INSTALL_TREE:
            ok = egd_run_argv_in_dir(".", build_argv, NOB_ARRAY_LEN(build_argv)) &&
                 egd_run_argv_in_dir(".", install_argv, NOB_ARRAY_LEN(install_argv)) &&
                 codegen_run_binary_in_dir_argv(nob_temp_dir_name(generated_nob),
                                                nob_temp_sprintf("./%s", nob_temp_file_name(generated_bin)),
                                                nob_install_argv,
                                                NOB_ARRAY_LEN(nob_install_argv)) &&
                 egd_compare_manifests(arena,
                                       case_def->signature,
                                       cmake_install,
                                       nob_install,
                                       case_def->manifest_requests,
                                       case_def->manifest_request_count);
            break;

        case EGD_PARITY_EXPORT_FILES:
            ok = codegen_run_binary_in_dir_argv(nob_temp_dir_name(generated_nob),
                                                nob_temp_sprintf("./%s", nob_temp_file_name(generated_bin)),
                                                nob_export_argv,
                                                NOB_ARRAY_LEN(nob_export_argv)) &&
                 egd_compare_manifests(arena,
                                       case_def->signature,
                                       cmake_src,
                                       nob_src,
                                       case_def->manifest_requests,
                                       case_def->manifest_request_count);
            break;

        case EGD_PARITY_PACKAGE_ARCHIVE:
            if (!codegen_resolve_host_cpack_bin(cpack_bin)) {
                arena_destroy(arena);
                return false;
            }

            {
                const char *cpack_argv[] = {cpack_bin, "-G", case_def->package_generator};
                nob_package_argv[2] = case_def->package_generator;
                if (!egd_run_argv_in_dir(cmake_build, cpack_argv, NOB_ARRAY_LEN(cpack_argv)) ||
                    !codegen_run_binary_in_dir_argv(nob_temp_dir_name(generated_nob),
                                                    nob_temp_sprintf("./%s", nob_temp_file_name(generated_bin)),
                                                    nob_package_argv,
                                                    NOB_ARRAY_LEN(nob_package_argv))) {
                    arena_destroy(arena);
                    return false;
                }
            }

            if (snprintf(cmake_archive,
                         sizeof(cmake_archive),
                         "%s/packages/demo-pkg%s",
                         cmake_build,
                         egd_package_extension(case_def->package_generator)) >= (int)sizeof(cmake_archive) ||
                snprintf(nob_archive,
                         sizeof(nob_archive),
                         "%s/packages/demo-pkg%s",
                         nob_build,
                         egd_package_extension(case_def->package_generator)) >= (int)sizeof(nob_archive) ||
                !test_ws_host_path_exists(cmake_archive) ||
                !test_ws_host_path_exists(nob_archive) ||
                !egd_extract_archive_to_dir(cmake_archive, case_def->package_generator, cmake_extract) ||
                !egd_extract_archive_to_dir(nob_archive, case_def->package_generator, nob_extract)) {
                arena_destroy(arena);
                return false;
            }

            ok = egd_compare_manifests(arena,
                                       case_def->signature,
                                       cmake_build,
                                       nob_build,
                                       (Test_Manifest_Request[]){
                                           {TEST_MANIFEST_CAPTURE_TREE, "package_outputs", "packages"},
                                       },
                                       1) &&
                 egd_compare_manifests(arena,
                                       "extracted package payload",
                                       cmake_extract,
                                       nob_extract,
                                       (Test_Manifest_Request[]){
                                           {TEST_MANIFEST_CAPTURE_TREE, "package_payload", ""},
                                       },
                                       1);
            break;

        case EGD_PARITY_NONE:
            ok = false;
            break;
    }

    if (ok) summary->parity_passed++;
    arena_destroy(arena);
    return ok;
}

TEST(evaluator_codegen_diff_inventory_covers_full_commands_and_curated_subcommands) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    Eval_Test_Init init = {0};
    Eval_Test_Runtime *ctx = NULL;
    EGD_Inventory_State_Counts command_counts = {0};
    String_View matrix = {0};
    const char *repo_root = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    char matrix_path[_TINYDIR_PATH_MAX] = {0};
    ASSERT(arena && event_arena);
    ASSERT(repo_root && repo_root[0] != '\0');
    ASSERT(snprintf(matrix_path,
                    sizeof(matrix_path),
                    "%s/docs/evaluator/evaluator_coverage_matrix.md",
                    repo_root) < (int)sizeof(matrix_path));
    ASSERT(test_snapshot_load_text_file_to_arena(arena, matrix_path, &matrix));

    init.arena = arena;
    init.event_arena = event_arena;
    init.stream = event_stream_create(event_arena);
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    ASSERT(init.stream != NULL);
    ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);
    ASSERT(NOB_ARRAY_LEN(s_egd_command_names) == EGD_EXPECTED_FULL_COMMANDS);
    ASSERT(NOB_ARRAY_LEN(s_egd_command_inventory) == EGD_EXPECTED_FULL_COMMANDS);
    ASSERT(egd_count_full_native_rows(matrix) == EGD_EXPECTED_FULL_COMMANDS);

    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_command_names); ++i) {
        Command_Capability cap = {0};
        const char *command = s_egd_command_names[i];
        const EGD_Command_Inventory *item = egd_lookup_command_inventory(command);
        char expected_row[192] = {0};
        ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr(command), &cap));
        ASSERT(cap.implemented_level == EVAL_CMD_IMPL_FULL);
        ASSERT(item != NULL);
        ASSERT(item->source_pack_path != NULL && item->source_pack_path[0] != '\0');
        ASSERT(item->domain_owner != NULL && item->domain_owner[0] != '\0');
        ASSERT(item->reason != NULL && item->reason[0] != '\0');
        ASSERT(egd_phase_mask_is_valid(item->phase_mask));
        ASSERT(strcmp(egd_classification_name(item->classification), "unknown") != 0);
        ASSERT(test_snapshot_load_text_file_to_arena(arena, item->source_pack_path, &(String_View){0}));
        if (egd_tracking_key_required(item->classification)) {
            ASSERT(item->tracking_key != NULL && item->tracking_key[0] != '\0');
        } else if (item->tracking_key) {
            ASSERT(item->tracking_key[0] != '\0');
        }
        egd_inventory_state_counts_add(&command_counts, item->classification);
        ASSERT(snprintf(expected_row,
                        sizeof(expected_row),
                        "| `%s` | native | FULL | FULL |",
                        command) < (int)sizeof(expected_row));
        ASSERT(codegen_sv_contains(matrix, expected_row));
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_command_inventory); ++i) {
        const EGD_Command_Inventory *item = &s_egd_command_inventory[i];
        ASSERT(item->command != NULL && item->command[0] != '\0');
        ASSERT(egd_command_name_in_registry(item->command));
        ASSERT(item->source_pack_path != NULL && item->source_pack_path[0] != '\0');
        ASSERT(item->domain_owner != NULL && item->domain_owner[0] != '\0');
        ASSERT(item->reason != NULL && item->reason[0] != '\0');
        ASSERT(egd_phase_mask_is_valid(item->phase_mask));
        ASSERT(strcmp(egd_classification_name(item->classification), "unknown") != 0);
        if (egd_tracking_key_required(item->classification)) {
            ASSERT(item->tracking_key != NULL && item->tracking_key[0] != '\0');
        } else if (item->tracking_key) {
            ASSERT(item->tracking_key[0] != '\0');
        }
        for (size_t j = i + 1; j < NOB_ARRAY_LEN(s_egd_command_inventory); ++j) {
            ASSERT(strcmp(item->command, s_egd_command_inventory[j].command) != 0);
        }
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_subcommand_inventory); ++i) {
        const EGD_Subcommand_Inventory *item = &s_egd_subcommand_inventory[i];
        ASSERT(egd_subcommand_metadata_is_valid(item));
        ASSERT(egd_case_exists_in_pack(arena, item->source_pack_path, item->case_name));
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_cases); ++i) {
        EGD_Parsed_Case parsed = {0};
        ASSERT(egd_load_case_from_pack(arena, s_egd_cases[i].source_pack_path, s_egd_cases[i].case_name, &parsed));
        ASSERT(egd_case_metadata_is_valid(&s_egd_cases[i], &parsed));
    }

    nob_log(NOB_INFO,
            "evaluator->codegen diff command inventory version: %s",
            EGD_COMMAND_INVENTORY_VERSION);
    egd_log_inventory_state_counts("evaluator->codegen diff full-command inventory", &command_counts);
    egd_log_curated_subcommand_family_counts();

    eval_test_destroy(ctx);
    arena_destroy(event_arena);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(evaluator_codegen_diff_tool_capability_resolution_is_host_independent) {
    EGD_Tool_Availability available = {
        .cmake = true,
        .cpack = false,
        .gzip = true,
        .xz = false,
        .python = false,
        .tar = true,
    };

    ASSERT(egd_required_tools_missing(EGD_TOOL_CMAKE | EGD_TOOL_TAR, &available) == EGD_TOOL_NONE);
    ASSERT(egd_required_tools_missing(EGD_TOOL_CMAKE | EGD_TOOL_CPACK | EGD_TOOL_TAR, &available) ==
           EGD_TOOL_CPACK);
    ASSERT(egd_required_tools_missing(EGD_TOOL_XZ | EGD_TOOL_PYTHON, &available) ==
           (EGD_TOOL_XZ | EGD_TOOL_PYTHON));
    ASSERT(egd_required_tools_missing(EGD_TOOL_NONE, &available) == EGD_TOOL_NONE);
    TEST_PASS();
}

TEST(evaluator_codegen_diff_c4_corpus_inventory_and_docs_are_complete) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Artifact_Parity_Corpus_Project_List projects = {0};
    String_View supported_subset_doc = {0};
    String_View tests_readme = {0};
    String_View tests_architecture = {0};
    char manifest_path[_TINYDIR_PATH_MAX] = {0};
    const char *const release_gate_commands[] = {
        "./build/nob test evaluator-codegen-diff",
        "./build/nob test artifact-parity",
        "./build/nob test artifact-parity-corpus",
    };

    ASSERT(arena != NULL);
    ASSERT(egd_join_abs_from_repo(ARTIFACT_PARITY_CORPUS_MANIFEST_PATH, manifest_path));
    ASSERT(artifact_parity_corpus_manifest_load_path(manifest_path, &projects));
    ASSERT(egd_load_repo_text_file(arena, EGD_SUPPORTED_SUBSET_DOC, &supported_subset_doc));
    ASSERT(egd_load_repo_text_file(arena, "docs/tests/README.md", &tests_readme));
    ASSERT(egd_load_repo_text_file(arena, "docs/tests/tests_architecture.md", &tests_architecture));

    ASSERT(NOB_ARRAY_LEN(s_egd_corpus_project_inventory) == projects.count);

    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_corpus_project_inventory); ++i) {
        const EGD_Corpus_Project_Inventory *item = &s_egd_corpus_project_inventory[i];
        ASSERT(egd_corpus_project_inventory_is_valid(item));
        for (size_t j = i + 1; j < NOB_ARRAY_LEN(s_egd_corpus_project_inventory); ++j) {
            ASSERT(strcmp(item->project_name, s_egd_corpus_project_inventory[j].project_name) != 0);
        }
    }

    for (size_t i = 0; i < s_egd_corpus_finding_inventory_count; ++i) {
        const EGD_Corpus_Finding_Inventory *item = &s_egd_corpus_finding_inventory[i];
        ASSERT(egd_corpus_finding_inventory_is_valid(item));
        for (size_t j = i + 1; j < s_egd_corpus_finding_inventory_count; ++j) {
            ASSERT(strcmp(item->key, s_egd_corpus_finding_inventory[j].key) != 0);
        }
    }

    for (size_t i = 0; i < projects.count; ++i) {
        const Artifact_Parity_Corpus_Project *project = &projects.items[i];
        const EGD_Corpus_Project_Inventory *inventory =
            egd_lookup_corpus_project_inventory(project->name);
        EGD_Corpus_Project_Disposition manifest_disposition =
            egd_manifest_support_tier_to_disposition(project->support_tier);

        ASSERT(inventory != NULL);
        ASSERT(manifest_disposition != (EGD_Corpus_Project_Disposition)-1);
        ASSERT(inventory->disposition == manifest_disposition);

        if (inventory->disposition != EGD_CORPUS_PROJECT_SUPPORTED) {
            const EGD_Corpus_Finding_Inventory *finding =
                egd_lookup_corpus_finding_inventory(inventory->tracking_or_boundary_key);
            bool project_found = false;

            ASSERT(finding != NULL);
            for (size_t j = 0; j < finding->affected_project_count; ++j) {
                if (strcmp(finding->affected_projects[j], project->name) == 0) {
                    project_found = true;
                    break;
                }
            }
            ASSERT(project_found);
        }

        if (artifact_parity_corpus_project_has_support_tier(project, "supported")) {
            ASSERT(egd_body_contains(supported_subset_doc, project->name));
            for (size_t j = 0; j < project->expected_imported_targets.count; ++j) {
                ASSERT(egd_body_contains(supported_subset_doc,
                                         project->expected_imported_targets.items[j]));
            }
        }
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(release_gate_commands); ++i) {
        ASSERT(egd_body_contains(supported_subset_doc, release_gate_commands[i]));
        ASSERT(egd_body_contains(tests_readme, release_gate_commands[i]));
        ASSERT(egd_body_contains(tests_architecture, release_gate_commands[i]));
    }

    artifact_parity_corpus_manifest_free(&projects);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(evaluator_codegen_diff_executes_classified_cases) {
    EGD_Inventory_State_Counts declared = {0};
    EGD_Case_Summary summary = {0};
    EGD_Tool_Availability available = {0};
    ASSERT(egd_detect_tool_availability(&available));
    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_cases); ++i) {
        egd_inventory_state_counts_add(&declared, s_egd_cases[i].classification);
    }
    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_cases); ++i) {
        ASSERT(egd_run_case(&s_egd_cases[i], &summary));
    }

    egd_log_case_family_counts();
    egd_log_inventory_state_counts("evaluator->codegen diff declared case inventory", &declared);
    nob_log(NOB_INFO,
            "evaluator->codegen diff runtime summary: parity-pass=%d backend-reject=%d evaluator-only=%d explicit-non-goal=%d skip-by-tool=%d",
            summary.parity_passed,
            summary.backend_rejected,
            summary.evaluator_only,
            summary.explicit_non_goal,
            summary.skipped_by_tool);

    ASSERT(summary.backend_rejected == declared.backend_reject);
    ASSERT(summary.evaluator_only == declared.evaluator_only);
    ASSERT(summary.explicit_non_goal == declared.explicit_non_goal);
    ASSERT(summary.parity_passed + summary.skipped_by_tool == declared.parity_pass);

    if (available.cmake) {
        ASSERT(summary.parity_passed >= 3);
    } else {
        ASSERT(summary.skipped_by_tool >= 3);
    }
    ASSERT(summary.backend_rejected == 2);
    ASSERT(summary.evaluator_only == 2);
    TEST_PASS();
}

void run_evaluator_codegen_diff_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    char repo_root[_TINYDIR_PATH_MAX] = {0};
    const char *repo_root_env = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    bool prepared = test_ws_prepare(&ws, "evaluator_codegen_diff");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "evaluator->codegen diff suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "evaluator->codegen diff suite: failed to enter isolated workspace");
        (void)test_ws_cleanup(&ws);
        if (failed) (*failed)++;
        return;
    }

    snprintf(repo_root, sizeof(repo_root), "%s", repo_root_env ? repo_root_env : "");
    codegen_test_set_repo_root(repo_root);

    test_evaluator_codegen_diff_inventory_covers_full_commands_and_curated_subcommands(
        passed, failed, skipped);
    test_evaluator_codegen_diff_tool_capability_resolution_is_host_independent(
        passed, failed, skipped);
    test_evaluator_codegen_diff_c4_corpus_inventory_and_docs_are_complete(
        passed, failed, skipped);
    test_evaluator_codegen_diff_executes_classified_cases(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        nob_log(NOB_ERROR, "evaluator->codegen diff suite: failed to restore cwd");
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "evaluator->codegen diff suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
