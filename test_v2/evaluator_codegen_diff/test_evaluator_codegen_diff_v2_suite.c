#include "test_evaluator_codegen_diff_v2_common.h"

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
} EGD_Case_Classification;

typedef enum {
    EGD_PARITY_NONE = 0,
    EGD_PARITY_BUILD_TREE,
    EGD_PARITY_INSTALL_TREE,
    EGD_PARITY_EXPORT_FILES,
    EGD_PARITY_PACKAGE_ARCHIVE,
} EGD_Parity_Kind;

typedef enum {
    EGD_SCOPE_SOURCE = 0,
    EGD_SCOPE_BUILD,
} EGD_Path_Scope;

typedef enum {
    EGD_MODE_PROJECT = 0,
    EGD_MODE_SCRIPT,
} EGD_Case_Mode;

typedef enum {
    EGD_OUTCOME_SUCCESS = 0,
    EGD_OUTCOME_ERROR,
} EGD_Expected_Outcome;

typedef struct {
    EGD_Path_Scope scope;
    String_View relpath;
} EGD_Path_Entry;

typedef struct {
    EGD_Path_Scope scope;
    String_View relpath;
    String_View text;
} EGD_Text_Fixture;

typedef struct {
    String_View name;
    String_View body;
    EGD_Case_Mode mode;
    EGD_Expected_Outcome expected_outcome;
    EGD_Path_Entry *files;
    EGD_Path_Entry *dirs;
    EGD_Text_Fixture *text_files;
} EGD_Parsed_Case;

typedef struct {
    const char *family;
    const char *signature;
    const char *source_pack_path;
    const char *case_name;
    EGD_Case_Classification classification;
    const char *reason;
    const char *backlog_key;
} EGD_Subcommand_Inventory;

typedef struct {
    const char *case_name;
    const char *source_pack_path;
    const char *command_family;
    const char *signature;
    EGD_Case_Classification classification;
    EGD_Parity_Kind parity_kind;
    const char *reason;
    const char *backlog_key;
    const Test_Manifest_Request *manifest_requests;
    size_t manifest_request_count;
    const char *package_generator;
} EGD_Case_Def;

typedef struct {
    int parity_passed;
    int backend_rejected;
    int evaluator_only;
    int skipped_by_tool;
} EGD_Case_Summary;

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
#define EGD_PACK_TRY_COMPILE "test_v2/evaluator_diff/cases/try_compile_special_seed_cases.cmake"
#define EGD_PACK_TRY_RUN "test_v2/evaluator_diff/cases/try_run_special_seed_cases.cmake"
#define EGD_PACK_FETCHCONTENT "test_v2/evaluator_diff/cases/fetchcontent_host_effect_seed_cases.cmake"
#define EGD_PACK_FIND_PACKAGE "test_v2/evaluator_diff/cases/find_package_special_seed_cases.cmake"
#define EGD_PACK_FIND_PATHLIKE "test_v2/evaluator_diff/cases/find_pathlike_seed_cases.cmake"
#define EGD_PACK_CACHE_LOADING "test_v2/evaluator_diff/cases/cache_loading_seed_cases.cmake"
#define EGD_PACK_ARGUMENT_PARSING "test_v2/evaluator_diff/cases/argument_parsing_seed_cases.cmake"
#define EGD_PACK_MESSAGE "test_v2/evaluator_diff/cases/message_seed_cases.cmake"
#define EGD_PACK_POLICY "test_v2/evaluator_diff/cases/cmake_policy_script_seed_cases.cmake"
#define EGD_PACK_INCLUDE "test_v2/evaluator_diff/cases/include_seed_cases.cmake"
#define EGD_PACK_VARS "test_v2/evaluator_diff/cases/var_commands_seed_cases.cmake"
#define EGD_PACK_PROPERTY_QUERY "test_v2/evaluator_diff/cases/property_query_seed_cases.cmake"
#define EGD_PACK_PROPERTY_WRAPPERS "test_v2/evaluator_diff/cases/property_wrappers_seed_cases.cmake"
#define EGD_PACK_HOST_IDENTITY "test_v2/evaluator_diff/cases/host_identity_seed_cases.cmake"
#define EGD_PACK_CONFIGURE_FILE "test_v2/evaluator_diff/cases/configure_file_seed_cases.cmake"
#define EGD_PACK_ADD_TARGETS "test_v2/evaluator_diff/cases/add_targets_seed_cases.cmake"
#define EGD_PACK_ADD_SUBDIRECTORY "test_v2/evaluator_diff/cases/add_subdirectory_seed_cases.cmake"
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

static const char *s_egd_command_names[] = {
#define EGD_COMMAND_NAME(name, handler, impl, fallback) name,
    EVAL_COMMAND_REGISTRY(EGD_COMMAND_NAME)
#undef EGD_COMMAND_NAME
};

static const EGD_Subcommand_Inventory s_egd_subcommand_inventory[] = {
    {"file", "DOWNLOAD", EGD_PACK_FILE, "file_host_effect_download_surface", EGD_CLASS_BACKEND_REJECT, "host-effect subcommand still lives in evaluator-only execution today", "epic-b.file.download"},
    {"file", "ARCHIVE_CREATE|ARCHIVE_EXTRACT", EGD_PACK_FILE, "file_host_effect_archive_surface", EGD_CLASS_BACKEND_REJECT, "archive host effects are not replayed by generated Nob", "epic-b.file.archive"},
    {"file", "GENERATE|LOCK|GET_RUNTIME_DEPENDENCIES", EGD_PACK_FILE, "file_host_effect_generate_lock_and_runtime_deps_surface", EGD_CLASS_BACKEND_REJECT, "file host effects still need downstream modeling or explicit backend support", "epic-b.file.generate_lock_runtime"},
    {"string", "APPEND|JOIN|CONFIGURE|REGEX|HASH", EGD_PACK_STRING, "string_text_regex_and_misc_surface", EGD_CLASS_EVALUATOR_ONLY, "pure evaluator string semantics remain outside backend replay", NULL},
    {"list", "TRANSFORM|SORT", EGD_PACK_LIST, "list_sort_and_transform_selector_surface_matches_documented_combinations", EGD_CLASS_EVALUATOR_ONLY, "list semantics only matter through frozen downstream state", NULL},
    {"math", "EXPR", EGD_PACK_MATH, "math_expr_precedence_bitwise_and_hex_output", EGD_CLASS_EVALUATOR_ONLY, "math is evaluator-only and should not be replayed in nob.c", NULL},
    {"cmake_language", "CALL|EVAL|DEFER", EGD_PACK_CMAKE_LANGUAGE, "cmake_language_defer_queue_cancel_and_flush_surface", EGD_CLASS_EVALUATOR_ONLY, "cmake_language stays on the evaluator side unless a later product decision moves specific host effects downstream", NULL},
    {"cmake_path", "SET|GET|APPEND|COMPARE", EGD_PACK_CMAKE_PATH, "cmake_path_extended_surface_matches_local_seed", EGD_CLASS_EVALUATOR_ONLY, "path computation is evaluator-only unless it already freezes into target metadata", NULL},
    {"ctest_*", "ctest_start|ctest_test|ctest_submit", EGD_PACK_CTEST, "ctest_local_dashboard_surface", EGD_CLASS_EVALUATOR_ONLY, "ctest family is explicitly outside generated Nob replay today", NULL},
};

static const EGD_Case_Def s_egd_cases[] = {
    {"backend_build_controlled_artifacts", EGD_PACK_SEEDS, "add_library", "build subtree parity", EGD_CLASS_PARITY_PASS, EGD_PARITY_BUILD_TREE, "positive backend-owned build artifact parity", NULL, s_egd_build_manifests, NOB_ARRAY_LEN(s_egd_build_manifests), NULL},
    {"backend_install_supported_surface", EGD_PACK_SEEDS, "install", "supported install subset", EGD_CLASS_PARITY_PASS, EGD_PARITY_INSTALL_TREE, "positive install parity for supported subset", NULL, s_egd_install_manifests, NOB_ARRAY_LEN(s_egd_install_manifests), NULL},
    {"export_host_effect_target_and_export_file_surface", EGD_PACK_EXPORT, "export", "standalone export files", EGD_CLASS_PARITY_PASS, EGD_PARITY_EXPORT_FILES, "positive standalone export parity through explicit export command", NULL, s_egd_export_manifests, NOB_ARRAY_LEN(s_egd_export_manifests), NULL},
    {"backend_package_supported_archives", EGD_PACK_SEEDS, "include(CPack)", "package TGZ", EGD_CLASS_PARITY_PASS, EGD_PARITY_PACKAGE_ARCHIVE, "positive full-package parity for TGZ", NULL, NULL, 0, "TGZ"},
    {"backend_package_supported_archives", EGD_PACK_SEEDS, "include(CPack)", "package TXZ", EGD_CLASS_PARITY_PASS, EGD_PARITY_PACKAGE_ARCHIVE, "positive full-package parity for TXZ", NULL, NULL, 0, "TXZ"},
    {"backend_package_supported_archives", EGD_PACK_SEEDS, "include(CPack)", "package ZIP", EGD_CLASS_PARITY_PASS, EGD_PARITY_PACKAGE_ARCHIVE, "positive full-package parity for ZIP", NULL, NULL, 0, "ZIP"},
    {"backend_reject_target_precompile_headers", EGD_PACK_SEEDS, "target_precompile_headers", "PCH explicit reject", EGD_CLASS_BACKEND_REJECT, EGD_PARITY_NONE, "backend still rejects concrete precompile header semantics", "epic-a.target_precompile_headers", NULL, 0, NULL},
    {"backend_reject_export_append", EGD_PACK_SEEDS, "export", "APPEND explicit reject", EGD_CLASS_BACKEND_REJECT, EGD_PARITY_NONE, "standalone export append remains an explicit backend reject", "epic-a.export-append", NULL, 0, NULL},
    {"math_expr_precedence_bitwise_and_hex_output", EGD_PACK_MATH, "math", "math(EXPR)", EGD_CLASS_EVALUATOR_ONLY, EGD_PARITY_NONE, "pure evaluator arithmetic should stay outside backend replay", NULL, NULL, 0, NULL},
    {"string_text_regex_and_misc_surface", EGD_PACK_STRING, "string", "string() surface", EGD_CLASS_EVALUATOR_ONLY, EGD_PARITY_NONE, "string operations are evaluator-only unless they already freeze into downstream state", NULL, NULL, 0, NULL},
};

static bool egd_starts_with(const char *text, const char *prefix) {
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    if (!text || !prefix) return false;
    return strncmp(text, prefix, prefix_len) == 0;
}

static bool egd_ends_with(const char *text, const char *suffix) {
    size_t text_len = text ? strlen(text) : 0;
    size_t suffix_len = suffix ? strlen(suffix) : 0;
    if (!text || !suffix || suffix_len > text_len) return false;
    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

static bool egd_sv_has_prefix(String_View sv, const char *prefix) {
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    if (!prefix || sv.count < prefix_len) return false;
    return memcmp(sv.data, prefix, prefix_len) == 0;
}

static String_View egd_copy_sv(Arena *arena, String_View sv) {
    char *copy = NULL;
    if (!arena) return nob_sv_from_cstr("");
    copy = arena_strndup(arena, sv.data ? sv.data : "", sv.count);
    if (!copy) return nob_sv_from_cstr("");
    return nob_sv_from_parts(copy, sv.count);
}

static EGD_Case_Classification egd_classify_command(const char *command) {
    if (!command) return EGD_CLASS_EVALUATOR_ONLY;
    if (egd_starts_with(command, "ctest_") ||
        strcmp(command, "string") == 0 ||
        strcmp(command, "list") == 0 ||
        strcmp(command, "math") == 0 ||
        strcmp(command, "set") == 0 ||
        strcmp(command, "unset") == 0 ||
        strcmp(command, "message") == 0 ||
        strcmp(command, "cmake_policy") == 0 ||
        strcmp(command, "cmake_path") == 0 ||
        strcmp(command, "cmake_language") == 0 ||
        strcmp(command, "cmake_parse_arguments") == 0 ||
        strcmp(command, "get_property") == 0 ||
        egd_starts_with(command, "get_") ||
        strcmp(command, "option") == 0 ||
        strcmp(command, "mark_as_advanced") == 0 ||
        strcmp(command, "remove") == 0 ||
        strcmp(command, "remove_definitions") == 0 ||
        strcmp(command, "separate_arguments") == 0 ||
        strcmp(command, "site_name") == 0 ||
        strcmp(command, "build_command") == 0 ||
        strcmp(command, "build_name") == 0 ||
        strcmp(command, "cmake_host_system_information") == 0) {
        return EGD_CLASS_EVALUATOR_ONLY;
    }

    if (strcmp(command, "file") == 0 ||
        strcmp(command, "write_file") == 0 ||
        strcmp(command, "make_directory") == 0 ||
        strcmp(command, "execute_process") == 0 ||
        strcmp(command, "exec_program") == 0 ||
        strcmp(command, "try_compile") == 0 ||
        strcmp(command, "try_run") == 0 ||
        strcmp(command, "cmake_file_api") == 0 ||
        strcmp(command, "target_precompile_headers") == 0 ||
        egd_starts_with(command, "FetchContent_") ||
        egd_starts_with(command, "cpack_add_") ||
        strcmp(command, "create_test_sourcelist") == 0 ||
        strcmp(command, "qt_wrap_cpp") == 0 ||
        strcmp(command, "qt_wrap_ui") == 0 ||
        strcmp(command, "fltk_wrap_ui") == 0 ||
        strcmp(command, "export_library_dependencies") == 0 ||
        strcmp(command, "include_external_msproject") == 0 ||
        strcmp(command, "include_regular_expression") == 0 ||
        strcmp(command, "output_required_files") == 0 ||
        strcmp(command, "source_group") == 0 ||
        strcmp(command, "load_command") == 0) {
        return EGD_CLASS_BACKEND_REJECT;
    }

    return EGD_CLASS_PARITY_PASS;
}

static const char *egd_command_source_pack(const char *command) {
    if (!command) return EGD_PACK_EVAL_DEFAULT;
    if (strcmp(command, "string") == 0) return EGD_PACK_STRING;
    if (strcmp(command, "list") == 0) return EGD_PACK_LIST;
    if (strcmp(command, "math") == 0) return EGD_PACK_MATH;
    if (strcmp(command, "cmake_language") == 0) return EGD_PACK_CMAKE_LANGUAGE;
    if (strcmp(command, "cmake_path") == 0) return EGD_PACK_CMAKE_PATH;
    if (egd_starts_with(command, "ctest_")) return EGD_PACK_CTEST;
    if (strcmp(command, "install") == 0 ||
        strcmp(command, "install_files") == 0 ||
        strcmp(command, "install_programs") == 0 ||
        strcmp(command, "install_targets") == 0) return EGD_PACK_INSTALL;
    if (strcmp(command, "export") == 0) return EGD_PACK_EXPORT;
    if (strcmp(command, "file") == 0 ||
        strcmp(command, "write_file") == 0 ||
        strcmp(command, "make_directory") == 0) return EGD_PACK_FILE;
    if (egd_starts_with(command, "FetchContent_")) return EGD_PACK_FETCHCONTENT;
    if (strcmp(command, "try_compile") == 0) return EGD_PACK_TRY_COMPILE;
    if (strcmp(command, "try_run") == 0) return EGD_PACK_TRY_RUN;
    if (strcmp(command, "find_package") == 0) return EGD_PACK_FIND_PACKAGE;
    if (egd_starts_with(command, "find_")) return EGD_PACK_FIND_PATHLIKE;
    if (strcmp(command, "load_cache") == 0) return EGD_PACK_CACHE_LOADING;
    if (strcmp(command, "separate_arguments") == 0 ||
        strcmp(command, "cmake_parse_arguments") == 0) return EGD_PACK_ARGUMENT_PARSING;
    if (strcmp(command, "message") == 0) return EGD_PACK_MESSAGE;
    if (strcmp(command, "cmake_policy") == 0) return EGD_PACK_POLICY;
    if (strcmp(command, "include") == 0 || strcmp(command, "include_guard") == 0) return EGD_PACK_INCLUDE;
    if (strcmp(command, "set") == 0 ||
        strcmp(command, "unset") == 0 ||
        strcmp(command, "option") == 0 ||
        strcmp(command, "mark_as_advanced") == 0 ||
        strcmp(command, "remove") == 0) return EGD_PACK_VARS;
    if (strcmp(command, "get_property") == 0 ||
        strcmp(command, "get_target_property") == 0 ||
        strcmp(command, "get_source_file_property") == 0 ||
        strcmp(command, "get_test_property") == 0) return EGD_PACK_PROPERTY_QUERY;
    if (strcmp(command, "get_cmake_property") == 0 ||
        strcmp(command, "get_directory_property") == 0) return EGD_PACK_PROPERTY_WRAPPERS;
    if (strcmp(command, "build_command") == 0 ||
        strcmp(command, "build_name") == 0 ||
        strcmp(command, "site_name") == 0 ||
        strcmp(command, "cmake_host_system_information") == 0) return EGD_PACK_HOST_IDENTITY;
    if (strcmp(command, "configure_file") == 0) return EGD_PACK_CONFIGURE_FILE;
    if (egd_starts_with(command, "target_") ||
        strcmp(command, "include_directories") == 0 ||
        strcmp(command, "link_directories") == 0 ||
        strcmp(command, "link_libraries") == 0) return EGD_PACK_TARGET_USAGE;
    if (egd_starts_with(command, "add_") ||
        strcmp(command, "project") == 0 ||
        strcmp(command, "cmake_minimum_required") == 0) return EGD_PACK_ADD_TARGETS;
    if (strcmp(command, "add_subdirectory") == 0) return EGD_PACK_ADD_SUBDIRECTORY;
    return EGD_PACK_EVAL_DEFAULT;
}

static const char *egd_command_reason(const char *command) {
    switch (egd_classify_command(command)) {
        case EGD_CLASS_PARITY_PASS:
            return "command participates in downstream state or backend execution already exercised by supported codegen paths";
        case EGD_CLASS_BACKEND_REJECT:
            return "command is implemented by the evaluator but still needs explicit backend work or deliberate non-goal treatment";
        case EGD_CLASS_EVALUATOR_ONLY:
            return "command remains evaluator-only and should affect codegen only through already-frozen downstream state";
    }
    return "unclassified";
}

static const char *egd_command_backlog_key(const char *command) {
    if (!command) return NULL;
    if (strcmp(command, "file") == 0 ||
        strcmp(command, "write_file") == 0 ||
        strcmp(command, "make_directory") == 0) return "epic-b.file-host-effects";
    if (strcmp(command, "target_precompile_headers") == 0) return "epic-a.target_precompile_headers";
    if (strcmp(command, "try_compile") == 0 || strcmp(command, "try_run") == 0) return "epic-c.try-apis";
    if (strcmp(command, "execute_process") == 0 || strcmp(command, "exec_program") == 0) return "epic-c.process-probes";
    if (egd_starts_with(command, "FetchContent_")) return "epic-c.fetchcontent";
    if (egd_starts_with(command, "cpack_add_")) return "epic-a.component-packaging";
    if (egd_starts_with(command, "ctest_")) return "epic-c.ctest";
    if (strcmp(command, "cmake_file_api") == 0) return "epic-c.file-api";
    return "epic-backend-reject.default";
}

static const char *egd_classification_name(EGD_Case_Classification classification) {
    switch (classification) {
        case EGD_CLASS_PARITY_PASS: return "parity-pass";
        case EGD_CLASS_BACKEND_REJECT: return "backend-reject";
        case EGD_CLASS_EVALUATOR_ONLY: return "evaluator-only";
    }
    return "unknown";
}

static bool egd_case_exists_in_pack(Arena *arena,
                                    const char *case_pack_path,
                                    const char *case_name) {
    String_View content = {0};
    Test_Case_Pack_Entry *entries = NULL;
    if (!arena || !case_pack_path || !case_name) return false;
    if (!test_snapshot_load_text_file_to_arena(arena, case_pack_path, &content) ||
        !test_snapshot_parse_case_pack_to_arena(arena, content, &entries)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(entries); ++i) {
        if (nob_sv_eq(entries[i].name, nob_sv_from_cstr(case_name))) return true;
    }
    return false;
}

static String_View egd_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        sv.count--;
    }
    return sv;
}

static bool egd_split_scoped_path(String_View raw,
                                  EGD_Path_Scope default_scope,
                                  EGD_Path_Scope *out_scope,
                                  String_View *out_relpath) {
    if (!out_scope || !out_relpath) return false;
    if (egd_sv_has_prefix(raw, "source/")) {
        *out_scope = EGD_SCOPE_SOURCE;
        *out_relpath = nob_sv_from_parts(raw.data + 7, raw.count - 7);
        return true;
    }
    if (egd_sv_has_prefix(raw, "build/")) {
        *out_scope = EGD_SCOPE_BUILD;
        *out_relpath = nob_sv_from_parts(raw.data + 6, raw.count - 6);
        return true;
    }
    *out_scope = default_scope;
    *out_relpath = raw;
    return true;
}

static bool egd_parse_scoped_path_entry(Arena *arena,
                                        String_View raw,
                                        EGD_Path_Scope default_scope,
                                        EGD_Path_Entry *out_entry) {
    EGD_Path_Scope scope = EGD_SCOPE_SOURCE;
    String_View relpath = {0};
    if (!arena || !out_entry) return false;
    if (!egd_split_scoped_path(raw, default_scope, &scope, &relpath)) return false;
    *out_entry = (EGD_Path_Entry){
        .scope = scope,
        .relpath = egd_copy_sv(arena, relpath),
    };
    return out_entry->relpath.data != NULL;
}

static bool egd_parse_case(Arena *arena,
                           Test_Case_Pack_Entry entry,
                           EGD_Parsed_Case *out_case) {
    Nob_String_Builder body = {0};
    bool have_outcome = false;
    size_t pos = 0;
    if (!arena || !out_case) return false;
    *out_case = (EGD_Parsed_Case){
        .name = egd_copy_sv(arena, entry.name),
        .mode = EGD_MODE_PROJECT,
        .expected_outcome = EGD_OUTCOME_SUCCESS,
    };
    if (!out_case->name.data) return false;

    while (pos < entry.script.count) {
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < entry.script.count && entry.script.data[line_end] != '\n') line_end++;
        pos = line_end < entry.script.count ? line_end + 1 : line_end;

        String_View raw_line = nob_sv_from_parts(entry.script.data + line_start, line_end - line_start);
        String_View line = egd_trim_cr(raw_line);

        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@OUTCOME "))) {
            if (nob_sv_eq(line, nob_sv_from_cstr("SUCCESS"))) out_case->expected_outcome = EGD_OUTCOME_SUCCESS;
            else if (nob_sv_eq(line, nob_sv_from_cstr("ERROR"))) out_case->expected_outcome = EGD_OUTCOME_ERROR;
            else {
                nob_sb_free(body);
                return false;
            }
            have_outcome = true;
            continue;
        }

        line = egd_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@MODE "))) {
            if (nob_sv_eq(line, nob_sv_from_cstr("SCRIPT"))) out_case->mode = EGD_MODE_SCRIPT;
            else if (nob_sv_eq(line, nob_sv_from_cstr("PROJECT"))) out_case->mode = EGD_MODE_PROJECT;
            else {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = egd_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@FILE "))) {
            EGD_Path_Entry file = {0};
            if (!egd_parse_scoped_path_entry(arena, line, EGD_SCOPE_SOURCE, &file) ||
                !arena_arr_push(arena, out_case->files, file)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = egd_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@DIR "))) {
            EGD_Path_Entry dir = {0};
            if (!egd_parse_scoped_path_entry(arena, line, EGD_SCOPE_SOURCE, &dir) ||
                !arena_arr_push(arena, out_case->dirs, dir)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = egd_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@FILE_TEXT "))) {
            EGD_Path_Entry path_entry = {0};
            EGD_Text_Fixture text_file = {0};
            Nob_String_Builder text = {0};
            bool found_end = false;

            if (!egd_parse_scoped_path_entry(arena, line, EGD_SCOPE_SOURCE, &path_entry)) {
                nob_sb_free(text);
                nob_sb_free(body);
                return false;
            }

            while (pos < entry.script.count) {
                size_t text_line_start = pos;
                size_t text_line_end = pos;
                while (text_line_end < entry.script.count && entry.script.data[text_line_end] != '\n') text_line_end++;
                pos = text_line_end < entry.script.count ? text_line_end + 1 : text_line_end;

                String_View text_raw = nob_sv_from_parts(entry.script.data + text_line_start,
                                                         text_line_end - text_line_start);
                String_View text_line = egd_trim_cr(text_raw);
                if (nob_sv_eq(text_line, nob_sv_from_cstr("#@@END_FILE_TEXT"))) {
                    found_end = true;
                    break;
                }
                if (egd_sv_has_prefix(text_line, "#@@")) {
                    nob_sb_free(text);
                    nob_sb_free(body);
                    return false;
                }
                nob_sb_append_buf(&text, text_raw.data, text_raw.count);
                nob_sb_append(&text, '\n');
            }

            if (!found_end) {
                nob_sb_free(text);
                nob_sb_free(body);
                return false;
            }

            text_file.scope = path_entry.scope;
            text_file.relpath = path_entry.relpath;
            text_file.text = egd_copy_sv(arena, nob_sv_from_parts(text.items ? text.items : "", text.count));
            nob_sb_free(text);
            if (!text_file.text.data || !arena_arr_push(arena, out_case->text_files, text_file)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = egd_trim_cr(raw_line);
        if (egd_sv_has_prefix(line, "#@@QUERY ") ||
            egd_sv_has_prefix(line, "#@@ENV ") ||
            egd_sv_has_prefix(line, "#@@ENV_UNSET ") ||
            egd_sv_has_prefix(line, "#@@ENV_PATH ") ||
            egd_sv_has_prefix(line, "#@@CACHE_INIT ") ||
            egd_sv_has_prefix(line, "#@@PROJECT_LAYOUT ")) {
            continue;
        }

        line = egd_trim_cr(raw_line);
        if (egd_sv_has_prefix(line, "#@@")) {
            nob_sb_free(body);
            return false;
        }

        nob_sb_append_buf(&body, raw_line.data, raw_line.count);
        nob_sb_append(&body, '\n');
    }

    if (!have_outcome) {
        nob_sb_free(body);
        return false;
    }

    out_case->body = egd_copy_sv(arena, nob_sv_from_parts(body.items ? body.items : "", body.count));
    nob_sb_free(body);
    return out_case->body.data != NULL;
}

static bool egd_load_case_from_pack(Arena *arena,
                                    const char *case_pack_path,
                                    const char *case_name,
                                    EGD_Parsed_Case *out_case) {
    String_View content = {0};
    Test_Case_Pack_Entry *entries = NULL;
    if (!arena || !case_pack_path || !case_name || !out_case) return false;
    if (!test_snapshot_load_text_file_to_arena(arena, case_pack_path, &content) ||
        !test_snapshot_parse_case_pack_to_arena(arena, content, &entries)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(entries); ++i) {
        if (!nob_sv_eq(entries[i].name, nob_sv_from_cstr(case_name))) continue;
        return egd_parse_case(arena, entries[i], out_case);
    }
    return false;
}

static bool egd_body_contains(String_View body, const char *needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    if (!needle || needle_len == 0 || body.count < needle_len) return false;
    for (size_t i = 0; i + needle_len <= body.count; ++i) {
        if (memcmp(body.data + i, needle, needle_len) == 0) return true;
    }
    return false;
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
    String_View script_text = {0};
    char cmake_bin[_TINYDIR_PATH_MAX] = {0};
    char cpack_bin[_TINYDIR_PATH_MAX] = {0};
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

    if (!codegen_resolve_host_cmake_bin(cmake_bin)) {
        nob_log(NOB_INFO, "evaluator->codegen diff case %s skipped: cmake unavailable", case_def->case_name);
        summary->skipped_by_tool++;
        arena_destroy(arena);
        return true;
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
                nob_log(NOB_INFO,
                        "evaluator->codegen diff case %s skipped: cpack unavailable",
                        case_def->case_name);
                summary->skipped_by_tool++;
                arena_destroy(arena);
                return true;
            }
            if ((strcmp(case_def->package_generator, "TGZ") == 0 && !egd_host_program_available("gzip")) ||
                (strcmp(case_def->package_generator, "TXZ") == 0 && !egd_host_program_available("xz")) ||
                (strcmp(case_def->package_generator, "ZIP") == 0 &&
                 !egd_host_program_available("python3") &&
                 !egd_host_program_available("python"))) {
                nob_log(NOB_INFO,
                        "evaluator->codegen diff case %s skipped: missing extractor tool for %s",
                        case_def->case_name,
                        case_def->package_generator);
                summary->skipped_by_tool++;
                arena_destroy(arena);
                return true;
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

    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_command_names); ++i) {
        Command_Capability cap = {0};
        const char *command = s_egd_command_names[i];
        char expected_row[192] = {0};
        ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr(command), &cap));
        ASSERT(cap.implemented_level == EVAL_CMD_IMPL_FULL);
        ASSERT(egd_command_source_pack(command) != NULL);
        ASSERT(egd_command_reason(command) != NULL);
        ASSERT(egd_classification_name(egd_classify_command(command)) != NULL);
        if (egd_classify_command(command) == EGD_CLASS_BACKEND_REJECT) {
            ASSERT(egd_command_backlog_key(command) != NULL);
        }
        ASSERT(snprintf(expected_row,
                        sizeof(expected_row),
                        "| `%s` | native | FULL | FULL |",
                        command) < (int)sizeof(expected_row));
        ASSERT(codegen_sv_contains(matrix, expected_row));
    }

    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_subcommand_inventory); ++i) {
        const EGD_Subcommand_Inventory *item = &s_egd_subcommand_inventory[i];
        ASSERT(item->family != NULL);
        ASSERT(item->signature != NULL);
        ASSERT(item->source_pack_path != NULL);
        ASSERT(item->reason != NULL);
        ASSERT(egd_case_exists_in_pack(arena, item->source_pack_path, item->case_name));
        if (item->classification == EGD_CLASS_BACKEND_REJECT) {
            ASSERT(item->backlog_key != NULL);
        }
    }

    eval_test_destroy(ctx);
    arena_destroy(event_arena);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(evaluator_codegen_diff_executes_classified_cases) {
    EGD_Case_Summary summary = {0};
    char cmake_bin[_TINYDIR_PATH_MAX] = {0};
    bool have_cmake = codegen_resolve_host_cmake_bin(cmake_bin);
    for (size_t i = 0; i < NOB_ARRAY_LEN(s_egd_cases); ++i) {
        ASSERT(egd_run_case(&s_egd_cases[i], &summary));
    }

    nob_log(NOB_INFO,
            "evaluator->codegen diff summary: parity-pass=%d backend-reject=%d evaluator-only=%d skip-by-tool=%d",
            summary.parity_passed,
            summary.backend_rejected,
            summary.evaluator_only,
            summary.skipped_by_tool);

    if (have_cmake) {
        ASSERT(summary.parity_passed >= 3);
    } else {
        ASSERT(summary.skipped_by_tool >= 3);
    }
    ASSERT(summary.backend_rejected >= 2);
    ASSERT(summary.evaluator_only >= 2);
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
