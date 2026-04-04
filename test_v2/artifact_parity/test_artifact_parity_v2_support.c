#include "test_artifact_parity_v2_support.h"

#include "test_fs.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef enum {
    ARTIFACT_PARITY_TREE_ENTRY_DIR = 0,
    ARTIFACT_PARITY_TREE_ENTRY_FILE,
    ARTIFACT_PARITY_TREE_ENTRY_LINK,
} Artifact_Parity_Tree_Entry_Kind;

typedef struct {
    Artifact_Parity_Tree_Entry_Kind kind;
    String_View relpath;
} Artifact_Parity_Tree_Entry;

static char s_artifact_parity_repo_root[_TINYDIR_PATH_MAX] = {0};

static bool artifact_parity_copy_string(const char *src,
                                        char out[_TINYDIR_PATH_MAX]) {
    int n = 0;
    if (!src || !out) return false;
    n = snprintf(out, _TINYDIR_PATH_MAX, "%s", src);
    if (n < 0 || n >= _TINYDIR_PATH_MAX) {
        nob_log(NOB_ERROR, "artifact parity: path too long: %s", src);
        return false;
    }
    return true;
}

static bool artifact_parity_path_is_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    {
        DWORD attrs = GetFileAttributesA(path);
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }
#else
    return access(path, X_OK) == 0;
#endif
}

static bool artifact_parity_sv_has_prefix(String_View sv, const char *prefix) {
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    if (!prefix || sv.count < prefix_len) return false;
    return memcmp(sv.data, prefix, prefix_len) == 0;
}

static int artifact_parity_sv_compare(String_View lhs, String_View rhs) {
    size_t common = lhs.count < rhs.count ? lhs.count : rhs.count;
    int cmp = common > 0 ? memcmp(lhs.data, rhs.data, common) : 0;
    if (cmp != 0) return cmp;
    if (lhs.count < rhs.count) return -1;
    if (lhs.count > rhs.count) return 1;
    return 0;
}

static const char *artifact_parity_domain_name(Artifact_Parity_Domain domain) {
    switch (domain) {
        case ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS: return "BUILD_OUTPUTS";
        case ARTIFACT_PARITY_DOMAIN_GENERATED_FILES: return "GENERATED_FILES";
        case ARTIFACT_PARITY_DOMAIN_INSTALL_TREE: return "INSTALL_TREE";
        case ARTIFACT_PARITY_DOMAIN_EXPORT_FILES: return "EXPORT_FILES";
        case ARTIFACT_PARITY_DOMAIN_PACKAGE_FILES: return "PACKAGE_FILES";
        case ARTIFACT_PARITY_DOMAIN_PACKAGE_METADATA: return "PACKAGE_METADATA";
    }
    return "UNKNOWN";
}

static const char *artifact_parity_capture_name(Artifact_Parity_Capture_Kind capture) {
    switch (capture) {
        case ARTIFACT_PARITY_CAPTURE_TREE: return "TREE";
        case ARTIFACT_PARITY_CAPTURE_FILE_TEXT: return "FILE_TEXT";
    }
    return "UNKNOWN";
}

static const char *artifact_parity_tree_entry_kind_name(Artifact_Parity_Tree_Entry_Kind kind) {
    switch (kind) {
        case ARTIFACT_PARITY_TREE_ENTRY_DIR: return "DIR";
        case ARTIFACT_PARITY_TREE_ENTRY_FILE: return "FILE";
        case ARTIFACT_PARITY_TREE_ENTRY_LINK: return "LINK";
    }
    return "UNKNOWN";
}

static int artifact_parity_tree_entry_compare(const void *lhs, const void *rhs) {
    const Artifact_Parity_Tree_Entry *a = (const Artifact_Parity_Tree_Entry*)lhs;
    const Artifact_Parity_Tree_Entry *b = (const Artifact_Parity_Tree_Entry*)rhs;
    int cmp = artifact_parity_sv_compare(a->relpath, b->relpath);
    if (cmp != 0) return cmp;
    if (a->kind < b->kind) return -1;
    if (a->kind > b->kind) return 1;
    return 0;
}

static bool artifact_parity_find_repo_probe_cmake(char out_path[_TINYDIR_PATH_MAX]) {
    char probes_root[_TINYDIR_PATH_MAX] = {0};
    Nob_Dir_Entry dir = {0};
    Test_Fs_Path_Info info = {0};
    const char *repo_root = s_artifact_parity_repo_root[0] != '\0'
        ? s_artifact_parity_repo_root
        : getenv(CMK2NOB_TEST_REPO_ROOT_ENV);

    if (!repo_root || repo_root[0] == '\0') return false;
    if (!test_fs_join_path(repo_root, "Temp_tests/probes", probes_root)) return false;
    if (!test_fs_get_path_info(probes_root, &info) || !info.exists || !info.is_dir) return false;
    if (!nob_dir_entry_open(probes_root, &dir)) return false;

    while (nob_dir_entry_next(&dir)) {
        char candidate_dir[_TINYDIR_PATH_MAX] = {0};
        char candidate_path[_TINYDIR_PATH_MAX] = {0};
        if (test_fs_is_dot_or_dotdot(dir.name)) continue;
        if (strncmp(dir.name, "cmake-", strlen("cmake-")) != 0) continue;
        if (!test_fs_join_path(probes_root, dir.name, candidate_dir) ||
            !test_fs_join_path(candidate_dir, "bin/cmake", candidate_path)) {
            continue;
        }
        if (artifact_parity_path_is_executable(candidate_path)) {
            nob_dir_entry_close(dir);
            return artifact_parity_copy_string(candidate_path, out_path);
        }
    }

    nob_dir_entry_close(dir);
    return false;
}

static bool artifact_parity_collect_tree_entries(Arena *arena,
                                                 const char *abs_path,
                                                 const char *relpath,
                                                 Artifact_Parity_Tree_Entry **out_entries) {
    Test_Fs_Path_Info info = {0};
    if (!arena || !abs_path || !out_entries) return false;
    if (!test_fs_get_path_info(abs_path, &info) || !info.exists) return false;

    if (relpath && relpath[0] != '\0') {
        char *copy = arena_strndup(arena, relpath, strlen(relpath));
        Artifact_Parity_Tree_Entry entry = {0};
        if (!copy) return false;
        entry.kind = info.is_link_like
            ? ARTIFACT_PARITY_TREE_ENTRY_LINK
            : (info.is_dir ? ARTIFACT_PARITY_TREE_ENTRY_DIR : ARTIFACT_PARITY_TREE_ENTRY_FILE);
        entry.relpath = nob_sv_from_cstr(copy);
        if (!arena_arr_push(arena, *out_entries, entry)) return false;
    }

    if (!info.is_dir || info.is_link_like) return true;

    Nob_Dir_Entry dir = {0};
    bool ok = true;
    if (!nob_dir_entry_open(abs_path, &dir)) return false;

    while (nob_dir_entry_next(&dir)) {
        char child_abs[_TINYDIR_PATH_MAX] = {0};
        char child_rel[_TINYDIR_PATH_MAX] = {0};
        if (test_fs_is_dot_or_dotdot(dir.name)) continue;
        if (!test_fs_join_path(abs_path, dir.name, child_abs)) {
            ok = false;
            break;
        }
        if (relpath && relpath[0] != '\0') {
            if (!test_fs_join_path(relpath, dir.name, child_rel)) {
                ok = false;
                break;
            }
        } else {
            int n = snprintf(child_rel, sizeof(child_rel), "%s", dir.name);
            if (n < 0 || n >= (int)sizeof(child_rel)) {
                ok = false;
                break;
            }
        }
        if (!artifact_parity_collect_tree_entries(arena, child_abs, child_rel, out_entries)) {
            ok = false;
            break;
        }
    }

    if (dir.error) ok = false;
    nob_dir_entry_close(dir);
    return ok;
}

static bool artifact_parity_append_manifest_header(Nob_String_Builder *sb,
                                                   const Artifact_Parity_Manifest_Request *request) {
    if (!sb || !request) return false;
    nob_sb_append_cstr(sb, "SECTION domain=");
    nob_sb_append_cstr(sb, artifact_parity_domain_name(request->domain));
    nob_sb_append_cstr(sb, " capture=");
    nob_sb_append_cstr(sb, artifact_parity_capture_name(request->capture));
    nob_sb_append_cstr(sb, " label=");
    test_snapshot_append_escaped_sv(sb, nob_sv_from_cstr(request->label ? request->label : ""));
    nob_sb_append_cstr(sb, " path=");
    test_snapshot_append_escaped_sv(sb, nob_sv_from_cstr(request->relpath ? request->relpath : ""));
    nob_sb_append_cstr(sb, "\n");
    return true;
}

static bool artifact_parity_append_tree_manifest(Arena *arena,
                                                 Nob_String_Builder *sb,
                                                 const char *abs_path) {
    Test_Fs_Path_Info info = {0};
    Artifact_Parity_Tree_Entry *entries = NULL;
    if (!arena || !sb || !abs_path) return false;
    if (!test_fs_get_path_info(abs_path, &info)) return false;

    if (!info.exists) {
        nob_sb_append_cstr(sb, "STATUS MISSING\n");
        return true;
    }

    nob_sb_append_cstr(sb, "STATUS PRESENT root_kind=");
    nob_sb_append_cstr(sb, info.is_link_like ? "LINK" : (info.is_dir ? "DIR" : "FILE"));
    nob_sb_append_cstr(sb, "\n");

    if (!artifact_parity_collect_tree_entries(arena, abs_path, "", &entries)) return false;
    if (arena_arr_len(entries) > 1) {
        qsort(entries,
              arena_arr_len(entries),
              sizeof(entries[0]),
              artifact_parity_tree_entry_compare);
    }

    for (size_t i = 0; i < arena_arr_len(entries); ++i) {
        nob_sb_append_cstr(sb, artifact_parity_tree_entry_kind_name(entries[i].kind));
        nob_sb_append_cstr(sb, " ");
        test_snapshot_append_escaped_sv(sb, entries[i].relpath);
        nob_sb_append_cstr(sb, "\n");
    }

    return true;
}

static bool artifact_parity_append_file_text_manifest(Arena *arena,
                                                      Nob_String_Builder *sb,
                                                      const char *abs_path) {
    Test_Fs_Path_Info info = {0};
    String_View text = {0};
    String_View normalized = {0};
    if (!arena || !sb || !abs_path) return false;
    if (!test_fs_get_path_info(abs_path, &info)) return false;

    if (!info.exists) {
        nob_sb_append_cstr(sb, "STATUS MISSING\n");
        return true;
    }
    if (info.is_dir) {
        nob_sb_append_cstr(sb, "STATUS TYPE_MISMATCH actual=DIR\n");
        return true;
    }
    if (!test_snapshot_load_text_file_to_arena(arena, abs_path, &text)) return false;
    normalized = test_snapshot_normalize_newlines_to_arena(arena, text);
    nob_sb_append_cstr(sb, "STATUS PRESENT\nTEXT ");
    test_snapshot_append_escaped_sv(sb, normalized);
    nob_sb_append_cstr(sb, "\n");
    return true;
}

void artifact_parity_test_set_repo_root(const char *repo_root) {
    snprintf(s_artifact_parity_repo_root,
             sizeof(s_artifact_parity_repo_root),
             "%s",
             repo_root ? repo_root : "");
}

bool artifact_parity_resolve_cmake(Artifact_Parity_Cmake_Config *out_config,
                                   char skip_reason[256]) {
    Nob_Cmd cmd = {0};
    char stdout_path[_TINYDIR_PATH_MAX] = {0};
    char stderr_path[_TINYDIR_PATH_MAX] = {0};
    String_View version_text = {0};
    Arena *arena = NULL;
    const char *env_path = getenv(CMK2NOB_TEST_CMAKE_BIN_ENV);

    if (!out_config || !skip_reason) return false;
    *out_config = (Artifact_Parity_Cmake_Config){0};
    skip_reason[0] = '\0';

    if (env_path && env_path[0] != '\0') {
        bool found = false;
        if (strchr(env_path, '/') || strchr(env_path, '\\')) {
            found = artifact_parity_path_is_executable(env_path) &&
                    artifact_parity_copy_string(env_path, out_config->cmake_bin);
        } else {
            found = test_ws_host_program_in_path(env_path, out_config->cmake_bin);
        }
        if (!found) {
            snprintf(skip_reason,
                     256,
                     "%s does not point to an executable",
                     CMK2NOB_TEST_CMAKE_BIN_ENV);
            return true;
        }
    } else if (test_ws_host_program_in_path("cmake", out_config->cmake_bin)) {
        /* Resolved from PATH. */
    } else if (artifact_parity_find_repo_probe_cmake(out_config->cmake_bin)) {
        /* Resolved from repo-local probe cache. */
    } else {
        snprintf(skip_reason,
                 256,
                 "cmake not found in PATH or Temp_tests/probes");
        return true;
    }

    if (!test_fs_join_path(".", "__artifact_parity_cmake_stdout.txt", stdout_path) ||
        !test_fs_join_path(".", "__artifact_parity_cmake_stderr.txt", stderr_path)) {
        return false;
    }

    nob_cmd_append(&cmd, out_config->cmake_bin, "--version");
    if (!nob_cmd_run(&cmd, .stdout_path = stdout_path, .stderr_path = stderr_path)) {
        nob_cmd_free(cmd);
        snprintf(skip_reason, 256, "failed to execute cmake --version");
        return true;
    }
    nob_cmd_free(cmd);

    arena = arena_create(64 * 1024);
    if (!arena) return false;
    if (!test_snapshot_load_text_file_to_arena(arena, stdout_path, &version_text)) {
        arena_destroy(arena);
        return false;
    }
    version_text = test_snapshot_normalize_newlines_to_arena(arena, version_text);

    if (!artifact_parity_sv_has_prefix(version_text, "cmake version 3.28.")) {
        size_t line_end = 0;
        while (line_end < version_text.count && version_text.data[line_end] != '\n') line_end++;
        snprintf(skip_reason,
                 256,
                 "requires CMake 3.28.x, found %.*s",
                 (int)line_end,
                 version_text.data ? version_text.data : "");
        arena_destroy(arena);
        return true;
    }

    {
        const char *prefix = "cmake version ";
        size_t prefix_len = strlen(prefix);
        size_t line_end = prefix_len;
        size_t version_len = 0;
        while (line_end < version_text.count && version_text.data[line_end] != '\n') line_end++;
        version_len = line_end > prefix_len ? line_end - prefix_len : 0;
        if (version_len >= sizeof(out_config->cmake_version)) {
            version_len = sizeof(out_config->cmake_version) - 1;
        }
        memcpy(out_config->cmake_version, version_text.data + prefix_len, version_len);
        out_config->cmake_version[version_len] = '\0';
    }

    out_config->available = true;
    arena_destroy(arena);
    return true;
}

bool artifact_parity_write_text_file(const char *path, const char *text) {
    const char *dir = NULL;
    if (!path || !text) return false;
    dir = nob_temp_dir_name(path);
    if (dir && strcmp(dir, ".") != 0 && !nob_mkdir_if_not_exists(dir)) return false;
    return nob_write_entire_file(path, text, strlen(text));
}

bool artifact_parity_generate_nob(const char *script,
                                  const Artifact_Parity_Nob_Config *config,
                                  const char *output_path) {
    Test_Semantic_Pipeline_Config pipeline_config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *codegen_arena = NULL;
    Nob_Codegen_Options opts = {0};
    bool ok = false;

    if (!script || !config || !output_path) return false;

    codegen_arena = arena_create(8 * 1024 * 1024);
    if (!codegen_arena) return false;

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();

    test_semantic_pipeline_config_init(&pipeline_config);
    pipeline_config.current_file = config->current_file ? config->current_file : "CMakeLists.txt";
    if (config->source_dir.data || config->source_dir.count > 0) pipeline_config.source_dir = config->source_dir;
    if (config->binary_dir.data || config->binary_dir.count > 0) pipeline_config.binary_dir = config->binary_dir;

    ok = test_semantic_pipeline_fixture_from_script(&fixture, script, &pipeline_config);
    if (!ok || !fixture.build.freeze_ok || !fixture.build.model || diag_has_errors()) {
        arena_destroy(codegen_arena);
        test_semantic_pipeline_fixture_destroy(&fixture);
        return false;
    }

    opts.input_path = nob_sv_from_cstr(pipeline_config.current_file);
    opts.output_path = nob_sv_from_cstr(output_path);
    ok = nob_codegen_write_file(fixture.build.model, codegen_arena, &opts);

    arena_destroy(codegen_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    return ok;
}

bool artifact_parity_compile_generated_nob(const char *generated_path,
                                           const char *output_path) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    const char *repo_root = s_artifact_parity_repo_root[0] != '\0'
        ? s_artifact_parity_repo_root
        : getenv(CMK2NOB_TEST_REPO_ROOT_ENV);

    if (!repo_root || repo_root[0] == '\0' || !generated_path || !output_path) return false;
    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd,
                   "-D_GNU_SOURCE",
                   "-std=c11",
                   "-Wall",
                   "-Wextra",
                   nob_temp_sprintf("-I%s/vendor", repo_root),
                   "-o",
                   output_path,
                   generated_path);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool artifact_parity_run_binary_in_dir(const char *dir,
                                       const char *binary_path,
                                       const char *arg1,
                                       const char *arg2) {
    Nob_Cmd cmd = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;
    if (!dir || !binary_path || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);

    if (!nob_set_current_dir(dir)) return false;
    nob_cmd_append(&cmd, binary_path);
    if (arg1) nob_cmd_append(&cmd, arg1);
    if (arg2) nob_cmd_append(&cmd, arg2);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    if (!nob_set_current_dir(prev_cwd)) return false;
    return ok;
}

bool artifact_parity_run_cmake_configure(const Artifact_Parity_Cmake_Config *config,
                                         const char *source_dir,
                                         const char *binary_dir) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!config || !config->available || !source_dir || !binary_dir) return false;
    nob_cmd_append(&cmd, config->cmake_bin, "-S", source_dir, "-B", binary_dir);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool artifact_parity_run_cmake_build(const Artifact_Parity_Cmake_Config *config,
                                     const char *binary_dir,
                                     const char *target_name) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!config || !config->available || !binary_dir) return false;
    nob_cmd_append(&cmd, config->cmake_bin, "--build", binary_dir);
    if (target_name && target_name[0] != '\0') {
        nob_cmd_append(&cmd, "--target", target_name);
    }
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool artifact_parity_capture_manifest(
    Arena *arena,
    const char *base_dir,
    const Artifact_Parity_Manifest_Request *requests,
    size_t request_count,
    String_View *out) {
    Nob_String_Builder sb = {0};
    char abs_path[_TINYDIR_PATH_MAX] = {0};
    char *copy = NULL;
    size_t len = 0;

    if (!arena || !base_dir || !requests || !out) return false;

    nob_sb_append_cstr(&sb, "MANIFEST requests=");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu\n\n", request_count));

    for (size_t i = 0; i < request_count; ++i) {
        const Artifact_Parity_Manifest_Request *request = &requests[i];
        const char *relpath = request->relpath ? request->relpath : "";

        if (relpath[0] == '\0') {
            if (!artifact_parity_copy_string(base_dir, abs_path)) {
                nob_sb_free(sb);
                return false;
            }
        } else if (!test_fs_join_path(base_dir, relpath, abs_path)) {
            nob_sb_free(sb);
            return false;
        }

        if (!artifact_parity_append_manifest_header(&sb, request)) {
            nob_sb_free(sb);
            return false;
        }
        if (request->capture == ARTIFACT_PARITY_CAPTURE_TREE) {
            if (!artifact_parity_append_tree_manifest(arena, &sb, abs_path)) {
                nob_sb_free(sb);
                return false;
            }
        } else if (!artifact_parity_append_file_text_manifest(arena, &sb, abs_path)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append_cstr(&sb, "END SECTION\n");
        if (i + 1 < request_count) nob_sb_append_cstr(&sb, "\n");
    }

    len = sb.count;
    copy = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;

    *out = nob_sv_from_parts(copy, len);
    return true;
}

bool artifact_parity_assert_equal_manifests(Arena *arena,
                                            const char *subject,
                                            String_View expected,
                                            String_View actual) {
    String_View expected_norm = {0};
    String_View actual_norm = {0};
    if (!arena || !subject) return false;

    expected_norm = test_snapshot_normalize_newlines_to_arena(arena, expected);
    actual_norm = test_snapshot_normalize_newlines_to_arena(arena, actual);
    if (nob_sv_eq(expected_norm, actual_norm)) return true;

    nob_log(NOB_ERROR, "artifact parity mismatch for %s", subject);
    nob_log(NOB_ERROR,
            "--- cmake manifest ---\n%.*s",
            (int)expected_norm.count,
            expected_norm.data ? expected_norm.data : "");
    nob_log(NOB_ERROR,
            "--- nob manifest ---\n%.*s",
            (int)actual_norm.count,
            actual_norm.data ? actual_norm.data : "");
    return false;
}
