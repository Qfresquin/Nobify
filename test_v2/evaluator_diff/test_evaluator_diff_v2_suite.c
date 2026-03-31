#include "../evaluator/test_evaluator_v2_support.h"
#include "test_fs.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#if defined(_WIN32)
#define DIFF_FILENO _fileno
#define DIFF_DUP _dup
#define DIFF_DUP2 _dup2
#define DIFF_OPEN _open
#define DIFF_CLOSE _close
#else
#define DIFF_FILENO fileno
#define DIFF_DUP dup
#define DIFF_DUP2 dup2
#define DIFF_OPEN open
#define DIFF_CLOSE close
#endif

typedef struct {
    const char *family_label;
    const char *case_pack_path;
} Diff_Case_Pack;

static const Diff_Case_Pack s_diff_case_packs[] = {
    {"target_usage", "test_v2/evaluator_diff/cases/target_usage_seed_cases.cmake"},
    {"list", "test_v2/evaluator_diff/cases/list_seed_cases.cmake"},
    {"var_commands", "test_v2/evaluator_diff/cases/var_commands_seed_cases.cmake"},
    {"property_query", "test_v2/evaluator_diff/cases/property_query_seed_cases.cmake"},
    {"directory_usage", "test_v2/evaluator_diff/cases/directory_usage_seed_cases.cmake"},
    {"property_setters", "test_v2/evaluator_diff/cases/property_setters_seed_cases.cmake"},
    {"testing_meta", "test_v2/evaluator_diff/cases/testing_meta_seed_cases.cmake"},
    {"argument_parsing", "test_v2/evaluator_diff/cases/argument_parsing_seed_cases.cmake"},
    {"find_pathlike", "test_v2/evaluator_diff/cases/find_pathlike_seed_cases.cmake"},
    {"host_identity", "test_v2/evaluator_diff/cases/host_identity_seed_cases.cmake"},
    {"cache_loading", "test_v2/evaluator_diff/cases/cache_loading_seed_cases.cmake"},
    {"legacy_generation", "test_v2/evaluator_diff/cases/legacy_generation_seed_cases.cmake"},
    {"cmake_path", "test_v2/evaluator_diff/cases/cmake_path_seed_cases.cmake"},
    {"get_filename_component", "test_v2/evaluator_diff/cases/get_filename_component_seed_cases.cmake"},
    {"math", "test_v2/evaluator_diff/cases/math_seed_cases.cmake"},
    {"add_targets", "test_v2/evaluator_diff/cases/add_targets_seed_cases.cmake"},
    {"add_subdirectory", "test_v2/evaluator_diff/cases/add_subdirectory_seed_cases.cmake"},
    {"string", "test_v2/evaluator_diff/cases/string_seed_cases.cmake"},
    {"top_level_project", "test_v2/evaluator_diff/cases/top_level_project_seed_cases.cmake"},
    {"message", "test_v2/evaluator_diff/cases/message_seed_cases.cmake"},
    {"configure_file", "test_v2/evaluator_diff/cases/configure_file_seed_cases.cmake"},
    {"property_wrappers", "test_v2/evaluator_diff/cases/property_wrappers_seed_cases.cmake"},
    {"include_script", "test_v2/evaluator_diff/cases/include_seed_cases.cmake"},
    {"execute_process_script", "test_v2/evaluator_diff/cases/execute_process_seed_cases.cmake"},
    {"cmake_language_script", "test_v2/evaluator_diff/cases/cmake_language_seed_cases.cmake"},
    {"dependency_provider", "test_v2/evaluator_diff/cases/dependency_provider_seed_cases.cmake"},
    {"file_script", "test_v2/evaluator_diff/cases/file_script_seed_cases.cmake"},
    {"cmake_policy_script", "test_v2/evaluator_diff/cases/cmake_policy_script_seed_cases.cmake"},
    {"configure_file_script", "test_v2/evaluator_diff/cases/configure_file_script_seed_cases.cmake"},
    {"install_host_effect", "test_v2/evaluator_diff/cases/install_host_effect_seed_cases.cmake"},
    {"export_host_effect", "test_v2/evaluator_diff/cases/export_host_effect_seed_cases.cmake"},
    {"file_host_effect", "test_v2/evaluator_diff/cases/file_host_effect_seed_cases.cmake"},
    {"fetchcontent_host_effect", "test_v2/evaluator_diff/cases/fetchcontent_host_effect_seed_cases.cmake"},
};

typedef enum {
    DIFF_EXPECT_SUCCESS = 0,
    DIFF_EXPECT_ERROR,
} Diff_Expected_Outcome;

typedef enum {
    DIFF_LAYOUT_BODY_ONLY_PROJECT = 0,
    DIFF_LAYOUT_RAW_CMAKELISTS,
} Diff_Project_Layout;

typedef enum {
    DIFF_MODE_PROJECT = 0,
    DIFF_MODE_SCRIPT,
} Diff_Case_Mode;

typedef enum {
    DIFF_PATH_SCOPE_SOURCE = 0,
    DIFF_PATH_SCOPE_BUILD,
} Diff_Path_Scope;

typedef enum {
    DIFF_ENV_SET = 0,
    DIFF_ENV_UNSET,
    DIFF_ENV_SET_PATH,
} Diff_Env_Op_Kind;

typedef enum {
    DIFF_QUERY_VAR = 0,
    DIFF_QUERY_CACHE_DEFINED,
    DIFF_QUERY_TARGET_EXISTS,
    DIFF_QUERY_TARGET_PROP,
    DIFF_QUERY_FILE_EXISTS,
    DIFF_QUERY_STDOUT,
    DIFF_QUERY_STDERR,
    DIFF_QUERY_FILE_TEXT,
    DIFF_QUERY_FILE_SHA256,
    DIFF_QUERY_TREE,
    DIFF_QUERY_CMAKE_PROP,
    DIFF_QUERY_GLOBAL_PROP,
    DIFF_QUERY_DIR_PROP,
} Diff_Query_Kind;

typedef struct {
    Diff_Query_Kind kind;
    String_View arg0;
    String_View arg1;
} Diff_Query;

typedef struct {
    Diff_Path_Scope scope;
    String_View relpath;
} Diff_Path_Entry;

typedef struct {
    Diff_Path_Scope scope;
    String_View relpath;
    String_View text;
} Diff_Text_Fixture;

typedef struct {
    Diff_Env_Op_Kind kind;
    String_View name;
    String_View value;
    Diff_Path_Scope path_scope;
    String_View path_relpath;
} Diff_Env_Op;

typedef struct {
    String_View name;
    String_View type;
    String_View value;
} Diff_Cache_Init;

typedef struct {
    String_View name;
    String_View body;
    Diff_Expected_Outcome expected_outcome;
    Diff_Case_Mode mode;
    Diff_Project_Layout layout;
    Diff_Path_Entry *files;
    Diff_Path_Entry *dirs;
    Diff_Text_Fixture *text_files;
    Diff_Env_Op *env_ops;
    Diff_Cache_Init *cache_inits;
    Diff_Query *queries;
} Diff_Case;

typedef struct {
    char cmake_bin[_TINYDIR_PATH_MAX];
    char cmake_version[64];
    bool available;
} Diff_Cmake_Config;

typedef struct {
    Diff_Expected_Outcome outcome;
    String_View probe_snapshot;
    String_View post_snapshot;
    String_View combined_snapshot;
    String_View stdout_text;
    String_View stderr_text;
    Eval_Run_Report report;
    bool have_report;
} Diff_Evaluator_Run;

typedef struct {
    Diff_Expected_Outcome outcome;
    bool command_started;
    String_View probe_snapshot;
    String_View post_snapshot;
    String_View combined_snapshot;
    String_View stdout_text;
    String_View stderr_text;
} Diff_Cmake_Run;

typedef struct {
    int saved_stdout_fd;
    int saved_stderr_fd;
    bool active;
} Diff_Std_Capture;

typedef struct {
    Test_Host_Env_Guard **items;
    size_t count;
} Diff_Env_Guard_List;

static bool diff_build_qualified_case_name(const char *family_label,
                                           String_View case_name,
                                           char out[_TINYDIR_PATH_MAX]) {
    int n = 0;
    if (!family_label || !out) return false;
    n = snprintf(out,
                 _TINYDIR_PATH_MAX,
                 "%s::%.*s",
                 family_label,
                 (int)case_name.count,
                 case_name.data ? case_name.data : "");
    return n >= 0 && n < _TINYDIR_PATH_MAX;
}

static bool diff_sv_has_prefix(String_View value, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    return value.count >= prefix_len && memcmp(value.data, prefix, prefix_len) == 0;
}

static bool diff_sv_contains(String_View value, String_View needle) {
    if (needle.count == 0 || value.count < needle.count) return false;
    for (size_t i = 0; i + needle.count <= value.count; i++) {
        if (memcmp(value.data + i, needle.data, needle.count) == 0) return true;
    }
    return false;
}

static String_View diff_copy_sv(Arena *arena, String_View value) {
    char *copy = NULL;
    if (!arena) return nob_sv_from_cstr("");
    copy = arena_strndup(arena, value.data ? value.data : "", value.count);
    if (!copy) return nob_sv_from_cstr("");
    return nob_sv_from_parts(copy, value.count);
}

static bool diff_ensure_dir_chain(const char *path) {
    char buffer[_TINYDIR_PATH_MAX] = {0};
    size_t len = 0;
    size_t start = 0;

    if (!path || path[0] == '\0') return true;
    len = strlen(path);
    if (len + 1 > sizeof(buffer)) return false;

    memcpy(buffer, path, len + 1);
#if defined(_WIN32)
    if (len >= 2 && buffer[1] == ':') start = 2;
#else
    if (buffer[0] == '/') start = 1;
#endif
    for (size_t i = start + 1; i < len; i++) {
        if (buffer[i] != '/' && buffer[i] != '\\') continue;
        buffer[i] = '\0';
        if (buffer[0] != '\0' && !nob_mkdir_if_not_exists(buffer)) return false;
        buffer[i] = '/';
    }
    return nob_mkdir_if_not_exists(buffer);
}

static bool diff_ensure_parent_dir(const char *path) {
    size_t temp_mark = nob_temp_save();
    const char *dir = nob_temp_dir_name(path);
    bool ok = diff_ensure_dir_chain(dir);
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool diff_write_entire_file(const char *path, const char *data) {
    size_t size = data ? strlen(data) : 0;
    if (!diff_ensure_parent_dir(path)) return false;
    return nob_write_entire_file(path, data ? data : "", size);
}

static bool diff_write_sv_file(const char *path, String_View data) {
    if (!diff_ensure_parent_dir(path)) return false;
    return nob_write_entire_file(path, data.data ? data.data : "", data.count);
}

static bool diff_make_empty_file(const char *path) {
    return diff_write_entire_file(path, "");
}

static bool diff_path_is_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    return access(path, X_OK) == 0;
#endif
}

static bool diff_copy_string(const char *src, char out[_TINYDIR_PATH_MAX]) {
    int n = 0;
    if (!src || !out) return false;
    n = snprintf(out, _TINYDIR_PATH_MAX, "%s", src);
    return n >= 0 && n < _TINYDIR_PATH_MAX;
}

static bool diff_read_text_file(Arena *arena, const char *path, String_View *out) {
    if (out) *out = nob_sv_from_cstr("");
    if (!arena || !path || !out) return false;
    if (!test_ws_host_path_exists(path)) return true;
    return evaluator_load_text_file_to_arena(arena, path, out);
}

static uint32_t diff_load_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static void diff_store_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)((v >> 24) & 0xFF);
    p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8) & 0xFF);
    p[3] = (unsigned char)(v & 0xFF);
}

static uint32_t diff_rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static void diff_sha256_process_block(uint32_t state[8], const unsigned char block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };

    uint32_t w[64] = {0};
    for (size_t i = 0; i < 16; i++) w[i] = diff_load_be32(block + (i * 4));
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = diff_rotr32(w[i - 15], 7) ^ diff_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = diff_rotr32(w[i - 2], 17) ^ diff_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (size_t i = 0; i < 64; i++) {
        uint32_t s1 = diff_rotr32(e, 6) ^ diff_rotr32(e, 11) ^ diff_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = diff_rotr32(a, 2) ^ diff_rotr32(a, 13) ^ diff_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void diff_sha256_compute(const unsigned char *msg, size_t len, unsigned char out[32]) {
    static const uint32_t init_state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    uint32_t state[8] = {0};
    memcpy(state, init_state, sizeof(state));

    for (size_t i = 0; i + 64 <= len; i += 64) {
        diff_sha256_process_block(state, msg + i);
    }

    unsigned char tail[128] = {0};
    size_t rem = len % 64;
    if (rem > 0) memcpy(tail, msg + (len - rem), rem);
    tail[rem] = 0x80;
    size_t tail_len = rem < 56 ? 64 : 128;
    uint64_t bits = ((uint64_t)len) * 8U;
    for (size_t i = 0; i < 8; i++) {
        tail[tail_len - 1 - i] = (unsigned char)((bits >> (8 * i)) & 0xFFU);
    }
    diff_sha256_process_block(state, tail);
    if (tail_len == 128) diff_sha256_process_block(state, tail + 64);

    for (size_t i = 0; i < 8; i++) diff_store_be32(out + (i * 4), state[i]);
}

static String_View diff_sha256_hex_to_arena(Arena *arena, const void *data, size_t size) {
    static const char lut[] = "0123456789abcdef";
    const unsigned char *bytes = (const unsigned char*)data;
    unsigned char digest[32] = {0};
    char *hex = NULL;

    if (!arena || (!bytes && size > 0)) return nob_sv_from_cstr("");
    diff_sha256_compute(bytes ? bytes : (const unsigned char*)"", size, digest);
    hex = arena_alloc(arena, 65);
    if (!hex) return nob_sv_from_cstr("");
    for (size_t i = 0; i < sizeof(digest); i++) {
        hex[i * 2 + 0] = lut[(digest[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = lut[digest[i] & 0x0F];
    }
    hex[64] = '\0';
    return nob_sv_from_parts(hex, 64);
}

static String_View diff_hash_file_sha256_to_arena(Arena *arena, const char *path) {
    Nob_String_Builder sb = {0};
    String_View out = nob_sv_from_cstr("");
    if (!arena || !path) return out;
    if (!nob_read_entire_file(path, &sb)) return out;
    out = diff_sha256_hex_to_arena(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return out;
}

static void diff_normalize_slashes(char *text) {
    if (!text) return;
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\\') text[i] = '/';
    }
}

static int diff_cstr_ptr_cmp(const void *lhs, const void *rhs) {
    const char *const *a = (const char *const*)lhs;
    const char *const *b = (const char *const*)rhs;
    return strcmp(*a, *b);
}

static bool diff_get_file_size_and_exec(const char *path, unsigned long long *out_size, bool *out_exec) {
    if (out_size) *out_size = 0;
    if (out_exec) *out_exec = false;
    if (!path) return false;
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data = {0};
    const char *ext = strrchr(path, '.');
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return false;
    if (out_size) {
        *out_size = ((unsigned long long)data.nFileSizeHigh << 32) |
                    (unsigned long long)data.nFileSizeLow;
    }
    if (out_exec && ext) {
        *out_exec = _stricmp(ext, ".exe") == 0 ||
                    _stricmp(ext, ".bat") == 0 ||
                    _stricmp(ext, ".cmd") == 0 ||
                    _stricmp(ext, ".com") == 0;
    }
    return true;
#else
    struct stat st = {0};
    if (stat(path, &st) != 0) return false;
    if (out_size) *out_size = (unsigned long long)st.st_size;
    if (out_exec) *out_exec = access(path, X_OK) == 0;
    return true;
#endif
}

static bool diff_read_link_target(const char *path, char out[_TINYDIR_PATH_MAX]) {
    if (!path || !out) return false;
#if defined(_WIN32)
    HANDLE h = CreateFileA(path,
                           0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    DWORD n = 0;
    if (h == INVALID_HANDLE_VALUE) return false;
    n = GetFinalPathNameByHandleA(h, out, _TINYDIR_PATH_MAX, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (n == 0 || n >= _TINYDIR_PATH_MAX) return false;
    if (strncmp(out, "\\\\?\\", 4) == 0) memmove(out, out + 4, strlen(out + 4) + 1);
    diff_normalize_slashes(out);
    return true;
#else
    ssize_t n = readlink(path, out, _TINYDIR_PATH_MAX - 1);
    if (n < 0 || n >= _TINYDIR_PATH_MAX) return false;
    out[n] = '\0';
    diff_normalize_slashes(out);
    return true;
#endif
}

static bool diff_collect_tree_manifest_lines(Arena *arena,
                                             const char *root,
                                             const char *relpath,
                                             char ***out_lines) {
    Test_Fs_Path_Info info = {0};
    if (!arena || !root || !relpath || !out_lines) return false;
    if (!test_fs_get_path_info(root, &info)) return false;
    if (!info.exists) return true;

    if (info.is_link_like) {
        char target[_TINYDIR_PATH_MAX] = {0};
        if (!diff_read_link_target(root, target)) return false;
        return arena_arr_push(arena,
                              *out_lines,
                              arena_strdup(arena,
                                           nob_temp_sprintf("L:%s:target=%s",
                                                            relpath,
                                                            target)));
    }

    if (info.is_dir) {
        Nob_Dir_Entry dir = {0};
        char **children = NULL;
        if (strcmp(relpath, ".") != 0 &&
            !arena_arr_push(arena,
                            *out_lines,
                            arena_strdup(arena, nob_temp_sprintf("D:%s", relpath)))) {
            return false;
        }
        if (!nob_dir_entry_open(root, &dir)) return false;
        while (nob_dir_entry_next(&dir)) {
            if (test_fs_is_dot_or_dotdot(dir.name)) continue;
            if (!arena_arr_push(arena, children, arena_strdup(arena, dir.name))) {
                nob_dir_entry_close(dir);
                return false;
            }
        }
        if (dir.error) {
            nob_dir_entry_close(dir);
            return false;
        }
        nob_dir_entry_close(dir);
        qsort(children, arena_arr_len(children), sizeof(children[0]), diff_cstr_ptr_cmp);
        for (size_t i = 0; i < arena_arr_len(children); i++) {
            char child_path[_TINYDIR_PATH_MAX] = {0};
            const char *child_rel = NULL;
            if (!test_fs_join_path(root, children[i], child_path)) return false;
            child_rel = strcmp(relpath, ".") == 0
                            ? nob_temp_sprintf("%s", children[i])
                            : nob_temp_sprintf("%s/%s", relpath, children[i]);
            if (!diff_collect_tree_manifest_lines(arena, child_path, child_rel, out_lines)) return false;
        }
        return true;
    }

    {
        unsigned long long size = 0;
        bool exec = false;
        String_View hash = {0};
        if (!diff_get_file_size_and_exec(root, &size, &exec)) return false;
        hash = diff_hash_file_sha256_to_arena(arena, root);
        if (hash.count == 0) return false;
        return arena_arr_push(arena,
                              *out_lines,
                              arena_strdup(arena,
                                           nob_temp_sprintf("F:%s:size=%llu:sha256=%.*s:exec=%d",
                                                            relpath,
                                                            size,
                                                            (int)hash.count,
                                                            hash.data,
                                                            exec ? 1 : 0)));
    }
}

static bool diff_build_tree_manifest(Arena *arena, const char *path, String_View *out_manifest) {
    Nob_String_Builder sb = {0};
    Test_Fs_Path_Info info = {0};
    char **lines = NULL;
    bool ok = false;

    if (out_manifest) *out_manifest = nob_sv_from_cstr("");
    if (!arena || !path || !out_manifest) return false;
    if (!test_fs_get_path_info(path, &info)) return false;
    if (!info.exists) return true;

    if (info.is_dir) {
        nob_sb_append_cstr(&sb, "D:.\n");
        if (!diff_collect_tree_manifest_lines(arena, path, ".", &lines)) goto defer;
    } else if (info.is_link_like) {
        char target[_TINYDIR_PATH_MAX] = {0};
        if (!diff_read_link_target(path, target)) goto defer;
        nob_sb_append_cstr(&sb, nob_temp_sprintf("L:.:target=%s\n", target));
    } else {
        unsigned long long size = 0;
        bool exec = false;
        String_View hash = {0};
        if (!diff_get_file_size_and_exec(path, &size, &exec)) goto defer;
        hash = diff_hash_file_sha256_to_arena(arena, path);
        if (hash.count == 0) goto defer;
        nob_sb_append_cstr(&sb,
                           nob_temp_sprintf("F:.:size=%llu:sha256=%.*s:exec=%d\n",
                                            size,
                                            (int)hash.count,
                                            hash.data,
                                            exec ? 1 : 0));
    }

    for (size_t i = 0; i < arena_arr_len(lines); i++) {
        nob_sb_append_cstr(&sb, lines[i]);
        nob_sb_append(&sb, '\n');
    }
    *out_manifest = diff_copy_sv(arena, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    ok = out_manifest->data != NULL;

defer:
    nob_sb_free(sb);
    return ok;
}

static void diff_sanitize_name(String_View name, char out[128]) {
    size_t wi = 0;
    if (!out) return;
    memset(out, 0, 128);
    if (name.count == 0) {
        memcpy(out, "case", 5);
        return;
    }

    for (size_t i = 0; i < name.count && wi + 1 < 128; i++) {
        unsigned char ch = (unsigned char)name.data[i];
        out[wi++] = (char)(isalnum(ch) ? ch : '_');
    }
    out[wi] = '\0';
    if (wi == 0) memcpy(out, "case", 5);
}

static bool diff_append_bracket_quoted(Nob_String_Builder *sb, String_View value) {
    size_t eq_count = 0;
    if (!sb) return false;

    for (;;) {
        char closer[16] = {0};
        size_t closer_len = 0;

        closer[closer_len++] = ']';
        for (size_t i = 0; i < eq_count; i++) closer[closer_len++] = '=';
        closer[closer_len++] = ']';
        closer[closer_len] = '\0';

        if (diff_sv_contains(value, nob_sv_from_cstr(closer))) {
            eq_count++;
            if (eq_count > 8) return false;
            continue;
        }

        nob_sb_append(&sb[0], '[');
        for (size_t i = 0; i < eq_count; i++) nob_sb_append(&sb[0], '=');
        nob_sb_append(&sb[0], '[');
        nob_sb_append_buf(&sb[0], value.data, value.count);
        nob_sb_append(&sb[0], ']');
        for (size_t i = 0; i < eq_count; i++) nob_sb_append(&sb[0], '=');
        nob_sb_append(&sb[0], ']');
        return true;
    }
}

static bool diff_append_line_expr(Nob_String_Builder *sb,
                                  size_t index,
                                  String_View prefix) {
    if (!sb) return false;
    nob_sb_append_cstr(sb, nob_temp_sprintf("set(_NOB_DIFF_LINE_%zu ", index));
    if (!diff_append_bracket_quoted(sb, prefix)) return false;
    nob_sb_append_cstr(sb, ")\n");
    nob_sb_append_cstr(sb,
                       nob_temp_sprintf("string(APPEND _NOB_DIFF_LINE_%zu \"${_NOB_DIFF_VAL_%zu}\" ",
                                        index,
                                        index));
    nob_sb_append_cstr(sb, "\"${_NOB_DIFF_NL}\")\n");
    nob_sb_append_cstr(sb,
                       nob_temp_sprintf("file(APPEND \"${_NOB_DIFF_SNAPSHOT_PATH}\" \"${_NOB_DIFF_LINE_%zu}\")\n",
                                        index));
    return true;
}

static bool diff_split_scoped_path(String_View raw,
                                   Diff_Path_Scope default_scope,
                                   Diff_Path_Scope *out_scope,
                                   String_View *out_relpath) {
    if (!out_scope || !out_relpath) return false;

    if (diff_sv_has_prefix(raw, "source/")) {
        *out_scope = DIFF_PATH_SCOPE_SOURCE;
        *out_relpath = nob_sv_from_parts(raw.data + 7, raw.count - 7);
        return true;
    }
    if (diff_sv_has_prefix(raw, "build/")) {
        *out_scope = DIFF_PATH_SCOPE_BUILD;
        *out_relpath = nob_sv_from_parts(raw.data + 6, raw.count - 6);
        return true;
    }

    *out_scope = default_scope;
    *out_relpath = raw;
    return true;
}

static bool diff_parse_scoped_path_entry(Arena *arena,
                                         String_View raw,
                                         Diff_Path_Scope default_scope,
                                         Diff_Path_Entry *out_entry) {
    Diff_Path_Scope scope = DIFF_PATH_SCOPE_SOURCE;
    String_View relpath = {0};

    if (!arena || !out_entry) return false;
    if (!diff_split_scoped_path(raw, default_scope, &scope, &relpath)) return false;
    *out_entry = (Diff_Path_Entry){
        .scope = scope,
        .relpath = diff_copy_sv(arena, relpath),
    };
    return out_entry->relpath.data != NULL;
}

static bool diff_parse_layout(String_View line, Diff_Project_Layout *out_layout) {
    if (!out_layout) return false;
    if (nob_sv_eq(line, nob_sv_from_cstr("BODY_ONLY_PROJECT"))) {
        *out_layout = DIFF_LAYOUT_BODY_ONLY_PROJECT;
        return true;
    }
    if (nob_sv_eq(line, nob_sv_from_cstr("RAW_CMAKELISTS"))) {
        *out_layout = DIFF_LAYOUT_RAW_CMAKELISTS;
        return true;
    }
    return false;
}

static bool diff_parse_mode(String_View line, Diff_Case_Mode *out_mode) {
    if (!out_mode) return false;
    if (nob_sv_eq(line, nob_sv_from_cstr("PROJECT"))) {
        *out_mode = DIFF_MODE_PROJECT;
        return true;
    }
    if (nob_sv_eq(line, nob_sv_from_cstr("SCRIPT"))) {
        *out_mode = DIFF_MODE_SCRIPT;
        return true;
    }
    return false;
}

static bool diff_parse_query(Arena *arena, String_View line, Diff_Query *out_query) {
    String_View rest = line;
    String_View first = {0};
    String_View second = {0};
    const char *space = NULL;

    if (!arena || !out_query) return false;
    *out_query = (Diff_Query){0};

    if (nob_sv_eq(line, nob_sv_from_cstr("#@@QUERY STDOUT"))) {
        out_query->kind = DIFF_QUERY_STDOUT;
        return true;
    }
    if (nob_sv_eq(line, nob_sv_from_cstr("#@@QUERY STDERR"))) {
        out_query->kind = DIFF_QUERY_STDERR;
        return true;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY VAR "))) {
        out_query->kind = DIFF_QUERY_VAR;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY CACHE_DEFINED "))) {
        out_query->kind = DIFF_QUERY_CACHE_DEFINED;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY TARGET_EXISTS "))) {
        out_query->kind = DIFF_QUERY_TARGET_EXISTS;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY TARGET_PROP "))) {
        space = memchr(rest.data, ' ', rest.count);
        if (!space) return false;
        first = nob_sv_from_parts(rest.data, (size_t)(space - rest.data));
        second = nob_sv_from_parts(space + 1, rest.count - (size_t)(space - rest.data) - 1);
        if (first.count == 0 || second.count == 0) return false;
        out_query->kind = DIFF_QUERY_TARGET_PROP;
        out_query->arg0 = diff_copy_sv(arena, first);
        out_query->arg1 = diff_copy_sv(arena, second);
        return out_query->arg0.data != NULL && out_query->arg1.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY FILE_EXISTS "))) {
        out_query->kind = DIFF_QUERY_FILE_EXISTS;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY FILE_TEXT "))) {
        out_query->kind = DIFF_QUERY_FILE_TEXT;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY FILE_SHA256 "))) {
        out_query->kind = DIFF_QUERY_FILE_SHA256;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY TREE "))) {
        out_query->kind = DIFF_QUERY_TREE;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY CMAKE_PROP "))) {
        out_query->kind = DIFF_QUERY_CMAKE_PROP;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY GLOBAL_PROP "))) {
        out_query->kind = DIFF_QUERY_GLOBAL_PROP;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY DIR_PROP "))) {
        space = memchr(rest.data, ' ', rest.count);
        if (!space) return false;
        first = nob_sv_from_parts(rest.data, (size_t)(space - rest.data));
        second = nob_sv_from_parts(space + 1, rest.count - (size_t)(space - rest.data) - 1);
        if (first.count == 0 || second.count == 0) return false;
        out_query->kind = DIFF_QUERY_DIR_PROP;
        out_query->arg0 = diff_copy_sv(arena, first);
        out_query->arg1 = diff_copy_sv(arena, second);
        return out_query->arg0.data != NULL && out_query->arg1.data != NULL;
    }

    return false;
}

static bool diff_parse_env_op(Arena *arena, String_View line, Diff_Env_Op *out_op) {
    String_View rest = line;
    String_View name = {0};
    String_View value = {0};
    const char *eq = NULL;
    const char *space = NULL;

    if (!arena || !out_op) return false;
    *out_op = (Diff_Env_Op){0};

    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@ENV_UNSET "))) {
        if (rest.count == 0) return false;
        out_op->kind = DIFF_ENV_UNSET;
        out_op->name = diff_copy_sv(arena, rest);
        return out_op->name.data != NULL;
    }

    rest = line;
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@ENV_PATH "))) {
        Diff_Path_Scope scope = DIFF_PATH_SCOPE_SOURCE;
        String_View relpath = {0};
        space = memchr(rest.data, ' ', rest.count);
        if (!space) return false;
        name = nob_sv_from_parts(rest.data, (size_t)(space - rest.data));
        value = nob_sv_from_parts(space + 1, rest.count - (size_t)(space - rest.data) - 1);
        if (name.count == 0 || value.count == 0) return false;
        if (!diff_split_scoped_path(value, DIFF_PATH_SCOPE_SOURCE, &scope, &relpath)) return false;
        out_op->kind = DIFF_ENV_SET_PATH;
        out_op->name = diff_copy_sv(arena, name);
        out_op->path_scope = scope;
        out_op->path_relpath = diff_copy_sv(arena, relpath);
        return out_op->name.data != NULL && out_op->path_relpath.data != NULL;
    }

    rest = line;
    if (!nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@ENV "))) return false;
    eq = memchr(rest.data, '=', rest.count);
    if (!eq) return false;
    name = nob_sv_from_parts(rest.data, (size_t)(eq - rest.data));
    value = nob_sv_from_parts(eq + 1, rest.count - (size_t)(eq - rest.data) - 1);
    if (name.count == 0) return false;

    out_op->kind = DIFF_ENV_SET;
    out_op->name = diff_copy_sv(arena, name);
    out_op->value = diff_copy_sv(arena, value);
    return out_op->name.data != NULL && out_op->value.data != NULL;
}

static bool diff_parse_cache_init(Arena *arena, String_View line, Diff_Cache_Init *out_init) {
    String_View rest = line;
    String_View lhs = {0};
    String_View name = {0};
    String_View type = {0};
    String_View value = {0};
    const char *eq = NULL;
    const char *colon = NULL;

    if (!arena || !out_init) return false;
    *out_init = (Diff_Cache_Init){0};

    if (!nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@CACHE_INIT "))) return false;
    eq = memchr(rest.data, '=', rest.count);
    if (!eq) return false;
    lhs = nob_sv_from_parts(rest.data, (size_t)(eq - rest.data));
    value = nob_sv_from_parts(eq + 1, rest.count - (size_t)(eq - rest.data) - 1);
    colon = memchr(lhs.data, ':', lhs.count);
    if (!colon) return false;
    name = nob_sv_from_parts(lhs.data, (size_t)(colon - lhs.data));
    type = nob_sv_from_parts(colon + 1, lhs.count - (size_t)(colon - lhs.data) - 1);
    if (name.count == 0 || type.count == 0) return false;

    out_init->name = diff_copy_sv(arena, name);
    out_init->type = diff_copy_sv(arena, type);
    out_init->value = diff_copy_sv(arena, value);
    return out_init->name.data != NULL && out_init->type.data != NULL && out_init->value.data != NULL;
}

static bool diff_parse_case(Arena *arena,
                            Test_Case_Pack_Entry entry,
                            Diff_Case *out_case) {
    Nob_String_Builder body = {0};
    bool have_outcome = false;
    bool have_layout = false;
    bool have_mode = false;
    size_t pos = 0;

    if (!arena || !out_case) return false;
    *out_case = (Diff_Case){
        .mode = DIFF_MODE_PROJECT,
        .layout = DIFF_LAYOUT_BODY_ONLY_PROJECT,
    };
    out_case->name = diff_copy_sv(arena, entry.name);
    if (!out_case->name.data) return false;

    while (pos < entry.script.count) {
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < entry.script.count && entry.script.data[line_end] != '\n') line_end++;
        pos = line_end < entry.script.count ? line_end + 1 : line_end;

        String_View raw_line = nob_sv_from_parts(entry.script.data + line_start, line_end - line_start);
        String_View line = test_case_pack_trim_cr(raw_line);

        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@OUTCOME "))) {
            if (have_outcome) return false;
            if (nob_sv_eq(line, nob_sv_from_cstr("SUCCESS"))) out_case->expected_outcome = DIFF_EXPECT_SUCCESS;
            else if (nob_sv_eq(line, nob_sv_from_cstr("ERROR"))) out_case->expected_outcome = DIFF_EXPECT_ERROR;
            else return false;
            have_outcome = true;
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@MODE "))) {
            if (have_mode || !diff_parse_mode(line, &out_case->mode)) {
                nob_sb_free(body);
                return false;
            }
            if (out_case->mode == DIFF_MODE_SCRIPT && have_layout) {
                nob_sb_free(body);
                return false;
            }
            have_mode = true;
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@PROJECT_LAYOUT "))) {
            if (have_layout || !diff_parse_layout(line, &out_case->layout)) return false;
            if (out_case->mode == DIFF_MODE_SCRIPT) {
                nob_sb_free(body);
                return false;
            }
            have_layout = true;
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@FILE "))) {
            Diff_Path_Entry file = {0};
            if (!diff_parse_scoped_path_entry(arena, line, DIFF_PATH_SCOPE_SOURCE, &file) ||
                !arena_arr_push(arena, out_case->files, file)) {
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@DIR "))) {
            Diff_Path_Entry dir = {0};
            if (!diff_parse_scoped_path_entry(arena, line, DIFF_PATH_SCOPE_SOURCE, &dir) ||
                !arena_arr_push(arena, out_case->dirs, dir)) {
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@FILE_TEXT "))) {
            Diff_Text_Fixture text_file = {0};
            Nob_String_Builder text = {0};
            Diff_Path_Entry path_entry = {0};
            bool found_end = false;

            if (!diff_parse_scoped_path_entry(arena, line, DIFF_PATH_SCOPE_SOURCE, &path_entry)) {
                nob_sb_free(text);
                return false;
            }

            while (pos < entry.script.count) {
                size_t text_line_start = pos;
                size_t text_line_end = pos;
                while (text_line_end < entry.script.count && entry.script.data[text_line_end] != '\n') text_line_end++;
                pos = text_line_end < entry.script.count ? text_line_end + 1 : text_line_end;

                String_View text_raw = nob_sv_from_parts(entry.script.data + text_line_start,
                                                         text_line_end - text_line_start);
                String_View text_line = test_case_pack_trim_cr(text_raw);
                if (nob_sv_eq(text_line, nob_sv_from_cstr("#@@END_FILE_TEXT"))) {
                    found_end = true;
                    break;
                }
                if (diff_sv_has_prefix(text_line, "#@@")) {
                    nob_sb_free(text);
                    return false;
                }
                nob_sb_append_buf(&text, text_raw.data, text_raw.count);
                nob_sb_append(&text, '\n');
            }

            if (!found_end) {
                nob_sb_free(text);
                return false;
            }

            text_file.scope = path_entry.scope;
            text_file.relpath = path_entry.relpath;
            text_file.text = diff_copy_sv(arena, nob_sv_from_parts(text.items ? text.items : "", text.count));
            nob_sb_free(text);
            if (!text_file.text.data || !arena_arr_push(arena, out_case->text_files, text_file)) return false;
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (diff_sv_has_prefix(line, "#@@ENV ") ||
            diff_sv_has_prefix(line, "#@@ENV_UNSET ") ||
            diff_sv_has_prefix(line, "#@@ENV_PATH ")) {
            Diff_Env_Op op = {0};
            if (!diff_parse_env_op(arena, line, &op) || !arena_arr_push(arena, out_case->env_ops, op)) {
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (diff_sv_has_prefix(line, "#@@CACHE_INIT ")) {
            Diff_Cache_Init init = {0};
            if (!diff_parse_cache_init(arena, line, &init) ||
                !arena_arr_push(arena, out_case->cache_inits, init)) {
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (diff_sv_has_prefix(line, "#@@QUERY ")) {
            Diff_Query query = {0};
            if (!diff_parse_query(arena, line, &query) || !arena_arr_push(arena, out_case->queries, query)) {
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (diff_sv_has_prefix(line, "#@@")) return false;

        nob_sb_append_buf(&body, raw_line.data, raw_line.count);
        nob_sb_append(&body, '\n');
    }

    if (!have_outcome) {
        nob_sb_free(body);
        return false;
    }

    if (out_case->mode == DIFF_MODE_SCRIPT) {
        for (size_t i = 0; i < arena_arr_len(out_case->files); i++) {
            if (out_case->files[i].scope == DIFF_PATH_SCOPE_BUILD) {
                nob_sb_free(body);
                return false;
            }
        }
        for (size_t i = 0; i < arena_arr_len(out_case->dirs); i++) {
            if (out_case->dirs[i].scope == DIFF_PATH_SCOPE_BUILD) {
                nob_sb_free(body);
                return false;
            }
        }
        for (size_t i = 0; i < arena_arr_len(out_case->text_files); i++) {
            if (out_case->text_files[i].scope == DIFF_PATH_SCOPE_BUILD) {
                nob_sb_free(body);
                return false;
            }
        }
        for (size_t i = 0; i < arena_arr_len(out_case->env_ops); i++) {
            if (out_case->env_ops[i].kind == DIFF_ENV_SET_PATH &&
                out_case->env_ops[i].path_scope == DIFF_PATH_SCOPE_BUILD) {
                nob_sb_free(body);
                return false;
            }
        }
    }

    out_case->body = diff_copy_sv(arena, nob_sv_from_parts(body.items ? body.items : "", body.count));
    nob_sb_free(body);
    return out_case->body.data != NULL;
}

static bool diff_append_cmake_scoped_path_literal(Nob_String_Builder *out,
                                                  Diff_Path_Scope scope,
                                                  String_View relpath) {
    if (!out) return false;
    nob_sb_append_cstr(out,
                       scope == DIFF_PATH_SCOPE_SOURCE ? "${CMAKE_SOURCE_DIR}"
                                                       : "${CMAKE_BINARY_DIR}");
    if (relpath.count > 0) {
        nob_sb_append_cstr(out, "/");
        nob_sb_append_buf(out, relpath.data, relpath.count);
    }
    return true;
}

static bool diff_query_is_postrun(Diff_Query_Kind kind);

static bool diff_build_probe_block(Arena *arena,
                                   const Diff_Case *diff_case,
                                   Nob_String_Builder *out) {
    bool needs_dir_prop_queries = false;
    if (!arena || !diff_case || !out) return false;
    for (size_t i = 0; i < arena_arr_len(diff_case->queries); i++) {
        if (diff_case->queries[i].kind == DIFF_QUERY_DIR_PROP) {
            needs_dir_prop_queries = true;
            break;
        }
    }

    nob_sb_append_cstr(out, "\n# --- nobify differential probe ---\n");
    nob_sb_append_cstr(out, "set(_NOB_DIFF_SNAPSHOT_PATH \"${CMAKE_BINARY_DIR}/diff_snapshot.txt\")\n");
    nob_sb_append_cstr(out, "set(_NOB_DIFF_NL \"\n\")\n");
    nob_sb_append_cstr(out, "file(WRITE \"${_NOB_DIFF_SNAPSHOT_PATH}\" \"OUTCOME=SUCCESS${_NOB_DIFF_NL}\")\n");
    if (needs_dir_prop_queries) {
        nob_sb_append_cstr(out, "function(_nob_diff_dir_known out_var abs_dir)\n");
        nob_sb_append_cstr(out, "  set(_NOB_DIFF_FOUND 0)\n");
        nob_sb_append_cstr(out, "  set(_NOB_DIFF_QUEUE \"${CMAKE_SOURCE_DIR}\")\n");
        nob_sb_append_cstr(out, "  while(_NOB_DIFF_QUEUE)\n");
        nob_sb_append_cstr(out, "    list(POP_FRONT _NOB_DIFF_QUEUE _NOB_DIFF_DIR)\n");
        nob_sb_append_cstr(out, "    get_property(_NOB_DIFF_BIN DIRECTORY \"${_NOB_DIFF_DIR}\" PROPERTY BINARY_DIR)\n");
        nob_sb_append_cstr(out, "    if(\"${_NOB_DIFF_DIR}\" STREQUAL \"${abs_dir}\" OR \"${_NOB_DIFF_BIN}\" STREQUAL \"${abs_dir}\")\n");
        nob_sb_append_cstr(out, "      set(_NOB_DIFF_FOUND 1)\n");
        nob_sb_append_cstr(out, "      break()\n");
        nob_sb_append_cstr(out, "    endif()\n");
        nob_sb_append_cstr(out, "    get_property(_NOB_DIFF_CHILDREN DIRECTORY \"${_NOB_DIFF_DIR}\" PROPERTY SUBDIRECTORIES)\n");
        nob_sb_append_cstr(out, "    if(_NOB_DIFF_CHILDREN)\n");
        nob_sb_append_cstr(out, "      list(APPEND _NOB_DIFF_QUEUE ${_NOB_DIFF_CHILDREN})\n");
        nob_sb_append_cstr(out, "    endif()\n");
        nob_sb_append_cstr(out, "  endwhile()\n");
        nob_sb_append_cstr(out, "  set(${out_var} \"${_NOB_DIFF_FOUND}\" PARENT_SCOPE)\n");
        nob_sb_append_cstr(out, "endfunction()\n");
    }

    for (size_t i = 0; i < arena_arr_len(diff_case->queries); i++) {
        const Diff_Query *query = &diff_case->queries[i];
        if (diff_query_is_postrun(query->kind)) continue;
        switch (query->kind) {
            case DIFF_QUERY_VAR: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("VAR:%.*s=", (int)query->arg0.count, query->arg0.data));
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("if(DEFINED %.*s)\n",
                                                    (int)query->arg0.count,
                                                    query->arg0.data));
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"${%.*s}\")\n",
                                                    i,
                                                    (int)query->arg0.count,
                                                    query->arg0.data));
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"__UNDEFINED__\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_CACHE_DEFINED: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("CACHE_DEFINED:%.*s=", (int)query->arg0.count, query->arg0.data));
                nob_sb_append_cstr(out, nob_temp_sprintf("get_property(_NOB_DIFF_VAL_%zu CACHE ", i));
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, " PROPERTY VALUE SET)\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("if(_NOB_DIFF_VAL_%zu)\n", i));
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"1\")\n", i));
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"0\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_TARGET_EXISTS: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("TARGET_EXISTS:%.*s=", (int)query->arg0.count, query->arg0.data));
                nob_sb_append_cstr(out, "if(TARGET ");
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"1\")\n", i));
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"0\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_TARGET_PROP: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("TARGET_PROP:%.*s:%.*s=",
                                     (int)query->arg0.count,
                                     query->arg0.data,
                                     (int)query->arg1.count,
                                     query->arg1.data));
                nob_sb_append_cstr(out, "if(TARGET ");
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  get_target_property(_NOB_DIFF_VAL_%zu ", i));
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, " ");
                if (!diff_append_bracket_quoted(out, query->arg1)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("  if(\"${_NOB_DIFF_VAL_%zu}\" STREQUAL \"_NOB_DIFF_VAL_%zu-NOTFOUND\")\n",
                                                    i,
                                                    i));
                nob_sb_append_cstr(out, nob_temp_sprintf("    set(_NOB_DIFF_VAL_%zu \"__UNSET__\")\n", i));
                nob_sb_append_cstr(out, "  endif()\n");
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"__MISSING_TARGET__\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_FILE_EXISTS: {
                Diff_Path_Scope scope = DIFF_PATH_SCOPE_BUILD;
                String_View relpath = {0};
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("FILE_EXISTS:%.*s=", (int)query->arg0.count, query->arg0.data));
                if (!diff_split_scoped_path(query->arg0, DIFF_PATH_SCOPE_BUILD, &scope, &relpath)) return false;

                nob_sb_append_cstr(out, nob_temp_sprintf("set(_NOB_DIFF_PATH_%zu \"", i));
                if (!diff_append_cmake_scoped_path_literal(out, scope, relpath)) return false;
                nob_sb_append_cstr(out, "\")\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("if(EXISTS \"${_NOB_DIFF_PATH_%zu}\")\n", i));
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"1\")\n", i));
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"0\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_CMAKE_PROP: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("CMAKE_PROP:%.*s=", (int)query->arg0.count, query->arg0.data));
                nob_sb_append_cstr(out, nob_temp_sprintf("get_cmake_property(_NOB_DIFF_VAL_%zu ", i));
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("if(\"${_NOB_DIFF_VAL_%zu}\" STREQUAL \"NOTFOUND\")\n",
                                                    i));
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"__UNSET__\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_GLOBAL_PROP: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("GLOBAL_PROP:%.*s=", (int)query->arg0.count, query->arg0.data));
                nob_sb_append_cstr(out, nob_temp_sprintf("get_property(_NOB_DIFF_SET_%zu GLOBAL PROPERTY ", i));
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, " SET)\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("if(_NOB_DIFF_SET_%zu)\n", i));
                nob_sb_append_cstr(out, nob_temp_sprintf("  get_property(_NOB_DIFF_VAL_%zu GLOBAL PROPERTY ", i));
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"__UNSET__\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_DIR_PROP: {
                Diff_Path_Scope scope = DIFF_PATH_SCOPE_SOURCE;
                String_View relpath = {0};
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("DIR_PROP:%.*s:%.*s=",
                                     (int)query->arg0.count,
                                     query->arg0.data,
                                     (int)query->arg1.count,
                                     query->arg1.data));
                if (!diff_split_scoped_path(query->arg0, DIFF_PATH_SCOPE_SOURCE, &scope, &relpath)) return false;

                nob_sb_append_cstr(out, nob_temp_sprintf("set(_NOB_DIFF_DIR_%zu \"", i));
                if (!diff_append_cmake_scoped_path_literal(out, scope, relpath)) return false;
                nob_sb_append_cstr(out, "\")\n");
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("_nob_diff_dir_known(_NOB_DIFF_DIR_KNOWN_%zu \"${_NOB_DIFF_DIR_%zu}\")\n",
                                                    i,
                                                    i));
                nob_sb_append_cstr(out, nob_temp_sprintf("if(_NOB_DIFF_DIR_KNOWN_%zu)\n", i));
                nob_sb_append_cstr(out, nob_temp_sprintf("  get_property(_NOB_DIFF_SET_%zu DIRECTORY \"${_NOB_DIFF_DIR_%zu}\" PROPERTY ", i, i));
                if (!diff_append_bracket_quoted(out, query->arg1)) return false;
                nob_sb_append_cstr(out, " SET)\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  if(_NOB_DIFF_SET_%zu)\n", i));
                nob_sb_append_cstr(out, nob_temp_sprintf("    get_property(_NOB_DIFF_VAL_%zu DIRECTORY \"${_NOB_DIFF_DIR_%zu}\" PROPERTY ", i, i));
                if (!diff_append_bracket_quoted(out, query->arg1)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out, "  else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("    set(_NOB_DIFF_VAL_%zu \"__UNSET__\")\n", i));
                nob_sb_append_cstr(out, "  endif()\n");
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"__MISSING_DIR__\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_STDOUT:
            case DIFF_QUERY_STDERR:
            case DIFF_QUERY_FILE_TEXT:
            case DIFF_QUERY_FILE_SHA256:
            case DIFF_QUERY_TREE:
                break;
        }
    }

    return true;
}

static bool diff_generate_case_script(Arena *arena,
                                      const Diff_Case *diff_case,
                                      const char *script_path,
                                      String_View *out_script) {
    Nob_String_Builder sb = {0};
    bool ok = false;

    if (out_script) *out_script = nob_sv_from_cstr("");
    if (!arena || !diff_case || !script_path || !out_script) return false;

    for (size_t i = 0; i < arena_arr_len(diff_case->cache_inits); i++) {
        const Diff_Cache_Init *cache_init = &diff_case->cache_inits[i];
        nob_sb_append_cstr(&sb, "set(");
        nob_sb_append_buf(&sb, cache_init->name.data, cache_init->name.count);
        nob_sb_append_cstr(&sb, " ");
        if (!diff_append_bracket_quoted(&sb, cache_init->value)) goto defer;
        nob_sb_append_cstr(&sb, " CACHE ");
        nob_sb_append_buf(&sb, cache_init->type.data, cache_init->type.count);
        nob_sb_append_cstr(&sb, " \"\")\n");
    }

    if (diff_case->mode == DIFF_MODE_PROJECT &&
        diff_case->layout == DIFF_LAYOUT_BODY_ONLY_PROJECT) {
        nob_sb_append_cstr(&sb, "cmake_minimum_required(VERSION 3.28)\n");
        nob_sb_append_cstr(&sb, "project(DiffCase LANGUAGES C CXX)\n");
    }

    nob_sb_append_buf(&sb, diff_case->body.data, diff_case->body.count);
    if (diff_case->body.count == 0 || diff_case->body.data[diff_case->body.count - 1] != '\n') {
        nob_sb_append(&sb, '\n');
    }
    if (!diff_build_probe_block(arena, diff_case, &sb)) goto defer;

    nob_sb_append(&sb, '\0');
    if (!diff_write_entire_file(script_path, sb.items ? sb.items : "")) goto defer;
    *out_script = diff_copy_sv(arena, nob_sv_from_cstr(sb.items ? sb.items : ""));
    ok = out_script->data != NULL;

defer:
    nob_sb_free(sb);
    return ok;
}

static bool diff_resolve_scoped_path_actual(Diff_Path_Scope scope,
                                            String_View relpath,
                                            const char *source_dir,
                                            const char *binary_dir,
                                            char out[_TINYDIR_PATH_MAX]) {
    const char *root = scope == DIFF_PATH_SCOPE_SOURCE ? source_dir : binary_dir;
    if (!root || !out) return false;
    if (relpath.count == 0) return diff_copy_string(root, out);
    return test_fs_join_path(root, nob_temp_sv_to_cstr(relpath), out);
}

static bool diff_prepare_case_fixtures(const Diff_Case *diff_case,
                                       const char *source_dir,
                                       const char *build_eval_dir,
                                       const char *build_cmake_dir) {
    if (!diff_case || !source_dir || !build_eval_dir || !build_cmake_dir) return false;

    if (diff_case->mode == DIFF_MODE_SCRIPT) {
        for (size_t i = 0; i < arena_arr_len(diff_case->dirs); i++) {
            if (diff_case->dirs[i].scope == DIFF_PATH_SCOPE_BUILD) return false;
        }
        for (size_t i = 0; i < arena_arr_len(diff_case->files); i++) {
            if (diff_case->files[i].scope == DIFF_PATH_SCOPE_BUILD) return false;
        }
        for (size_t i = 0; i < arena_arr_len(diff_case->text_files); i++) {
            if (diff_case->text_files[i].scope == DIFF_PATH_SCOPE_BUILD) return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(diff_case->dirs); i++) {
        char path_a[_TINYDIR_PATH_MAX] = {0};
        char path_b[_TINYDIR_PATH_MAX] = {0};
        const Diff_Path_Entry *dir = &diff_case->dirs[i];
        if (dir->scope == DIFF_PATH_SCOPE_SOURCE) {
            if (!diff_resolve_scoped_path_actual(dir->scope, dir->relpath, source_dir, build_eval_dir, path_a) ||
                !diff_ensure_dir_chain(path_a)) {
                return false;
            }
        } else {
            if (!diff_resolve_scoped_path_actual(dir->scope, dir->relpath, source_dir, build_eval_dir, path_a) ||
                !diff_resolve_scoped_path_actual(dir->scope, dir->relpath, source_dir, build_cmake_dir, path_b) ||
                !diff_ensure_dir_chain(path_a) ||
                !diff_ensure_dir_chain(path_b)) {
                return false;
            }
        }
    }

    for (size_t i = 0; i < arena_arr_len(diff_case->files); i++) {
        char path_a[_TINYDIR_PATH_MAX] = {0};
        char path_b[_TINYDIR_PATH_MAX] = {0};
        const Diff_Path_Entry *file = &diff_case->files[i];
        if (file->scope == DIFF_PATH_SCOPE_SOURCE) {
            if (!diff_resolve_scoped_path_actual(file->scope, file->relpath, source_dir, build_eval_dir, path_a) ||
                !diff_make_empty_file(path_a)) {
                return false;
            }
        } else {
            if (!diff_resolve_scoped_path_actual(file->scope, file->relpath, source_dir, build_eval_dir, path_a) ||
                !diff_resolve_scoped_path_actual(file->scope, file->relpath, source_dir, build_cmake_dir, path_b) ||
                !diff_make_empty_file(path_a) ||
                !diff_make_empty_file(path_b)) {
                return false;
            }
        }
    }

    for (size_t i = 0; i < arena_arr_len(diff_case->text_files); i++) {
        char path_a[_TINYDIR_PATH_MAX] = {0};
        char path_b[_TINYDIR_PATH_MAX] = {0};
        const Diff_Text_Fixture *text_file = &diff_case->text_files[i];
        if (text_file->scope == DIFF_PATH_SCOPE_SOURCE) {
            if (!diff_resolve_scoped_path_actual(text_file->scope,
                                                 text_file->relpath,
                                                 source_dir,
                                                 build_eval_dir,
                                                 path_a) ||
                !diff_write_sv_file(path_a, text_file->text)) {
                return false;
            }
        } else {
            if (!diff_resolve_scoped_path_actual(text_file->scope,
                                                 text_file->relpath,
                                                 source_dir,
                                                 build_eval_dir,
                                                 path_a) ||
                !diff_resolve_scoped_path_actual(text_file->scope,
                                                 text_file->relpath,
                                                 source_dir,
                                                 build_cmake_dir,
                                                 path_b) ||
                !diff_write_sv_file(path_a, text_file->text) ||
                !diff_write_sv_file(path_b, text_file->text)) {
                return false;
            }
        }
    }

    return true;
}

static Diff_Expected_Outcome diff_evaluator_outcome_from_result(Eval_Result result,
                                                                const Eval_Run_Report *report) {
    if (eval_result_is_fatal(result)) return DIFF_EXPECT_ERROR;
    if (!report) return DIFF_EXPECT_ERROR;
    return report->error_count == 0 ? DIFF_EXPECT_SUCCESS : DIFF_EXPECT_ERROR;
}

static String_View diff_base64_encode_to_arena(Arena *arena, String_View input) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = 0;
    char *out = NULL;
    size_t in_i = 0;
    size_t out_i = 0;

    if (!arena) return nob_sv_from_cstr("");
    out_len = ((input.count + 2) / 3) * 4;
    out = arena_alloc(arena, out_len + 1);
    if (!out) return nob_sv_from_cstr("");

    while (in_i + 3 <= input.count) {
        unsigned int chunk = ((unsigned int)(unsigned char)input.data[in_i] << 16) |
                             ((unsigned int)(unsigned char)input.data[in_i + 1] << 8) |
                             (unsigned int)(unsigned char)input.data[in_i + 2];
        out[out_i++] = alphabet[(chunk >> 18) & 0x3F];
        out[out_i++] = alphabet[(chunk >> 12) & 0x3F];
        out[out_i++] = alphabet[(chunk >> 6) & 0x3F];
        out[out_i++] = alphabet[chunk & 0x3F];
        in_i += 3;
    }

    if (in_i < input.count) {
        unsigned int chunk = (unsigned int)(unsigned char)input.data[in_i] << 16;
        out[out_i++] = alphabet[(chunk >> 18) & 0x3F];
        if (in_i + 1 < input.count) {
            chunk |= (unsigned int)(unsigned char)input.data[in_i + 1] << 8;
            out[out_i++] = alphabet[(chunk >> 12) & 0x3F];
            out[out_i++] = alphabet[(chunk >> 6) & 0x3F];
            out[out_i++] = '=';
        } else {
            out[out_i++] = alphabet[(chunk >> 12) & 0x3F];
            out[out_i++] = '=';
            out[out_i++] = '=';
        }
    }

    out[out_i] = '\0';
    return nob_sv_from_parts(out, out_i);
}

static bool diff_append_b64_snapshot_line(Arena *arena,
                                          Nob_String_Builder *sb,
                                          String_View prefix,
                                          String_View value) {
    String_View normalized = {0};
    String_View encoded = {0};
    if (!arena || !sb) return false;
    normalized = evaluator_normalize_newlines_to_arena(arena, value);
    encoded = diff_base64_encode_to_arena(arena, normalized);
    if (!encoded.data) return false;
    nob_sb_append_buf(sb, prefix.data, prefix.count);
    nob_sb_append_buf(sb, encoded.data, encoded.count);
    nob_sb_append(&sb[0], '\n');
    return true;
}

static bool diff_is_cmake_stdout_boilerplate_line(String_View line) {
    return diff_sv_has_prefix(line, "-- Configuring done") ||
           diff_sv_has_prefix(line, "-- Generating done") ||
           diff_sv_has_prefix(line, "-- Build files have been written to:") ||
           diff_sv_has_prefix(line, "-- Configuring incomplete, errors occurred!");
}

static String_View diff_filter_cmake_stdout_to_arena(Arena *arena, String_View value) {
    Nob_String_Builder sb = {0};
    size_t pos = 0;
    if (!arena) return nob_sv_from_cstr("");
    value = evaluator_normalize_newlines_to_arena(arena, value);
    while (pos < value.count) {
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < value.count && value.data[line_end] != '\n') line_end++;
        pos = line_end < value.count ? line_end + 1 : line_end;
        String_View line = nob_sv_from_parts(value.data + line_start, line_end - line_start);
        if (diff_is_cmake_stdout_boilerplate_line(line)) continue;
        nob_sb_append_buf(&sb, line.data, line.count);
        if (pos <= value.count) nob_sb_append(&sb, '\n');
    }
    return diff_copy_sv(arena, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
}

static bool diff_query_is_postrun(Diff_Query_Kind kind) {
    return kind == DIFF_QUERY_STDOUT ||
           kind == DIFF_QUERY_STDERR ||
           kind == DIFF_QUERY_FILE_EXISTS ||
           kind == DIFF_QUERY_FILE_TEXT ||
           kind == DIFF_QUERY_FILE_SHA256 ||
           kind == DIFF_QUERY_TREE;
}

static bool diff_case_requires_postrun_compare(const Diff_Case *diff_case) {
    if (!diff_case) return false;
    for (size_t i = 0; i < arena_arr_len(diff_case->queries); i++) {
        if (diff_query_is_postrun(diff_case->queries[i].kind)) return true;
    }
    return false;
}

static bool diff_build_postrun_snapshot(Arena *arena,
                                        const Diff_Case *diff_case,
                                        String_View stdout_text,
                                        String_View stderr_text,
                                        const char *source_dir,
                                        const char *binary_dir,
                                        String_View *out_snapshot) {
    Nob_String_Builder sb = {0};
    bool ok = false;

    if (out_snapshot) *out_snapshot = nob_sv_from_cstr("");
    if (!arena || !diff_case || !source_dir || !binary_dir || !out_snapshot) return false;

    for (size_t i = 0; i < arena_arr_len(diff_case->queries); i++) {
        const Diff_Query *query = &diff_case->queries[i];
        switch (query->kind) {
            case DIFF_QUERY_STDOUT: {
                if (!diff_append_b64_snapshot_line(arena,
                                                   &sb,
                                                   nob_sv_from_cstr("STDOUT_B64="),
                                                   stdout_text)) {
                    goto defer;
                }
                break;
            }

            case DIFF_QUERY_STDERR: {
                if (!diff_append_b64_snapshot_line(arena,
                                                   &sb,
                                                   nob_sv_from_cstr("STDERR_B64="),
                                                   stderr_text)) {
                    goto defer;
                }
                break;
            }

            case DIFF_QUERY_FILE_EXISTS: {
                Diff_Path_Scope scope = DIFF_PATH_SCOPE_BUILD;
                String_View relpath = {0};
                char path[_TINYDIR_PATH_MAX] = {0};
                Test_Fs_Path_Info info = {0};
                nob_sb_append_cstr(&sb,
                                   nob_temp_sprintf("FILE_EXISTS:%.*s=",
                                                    (int)query->arg0.count,
                                                    query->arg0.data));
                if (!diff_split_scoped_path(query->arg0, DIFF_PATH_SCOPE_BUILD, &scope, &relpath) ||
                    !diff_resolve_scoped_path_actual(scope, relpath, source_dir, binary_dir, path) ||
                    !test_fs_get_path_info(path, &info)) {
                    goto defer;
                }
                nob_sb_append_cstr(&sb, info.exists ? "1\n" : "0\n");
                break;
            }

            case DIFF_QUERY_FILE_TEXT: {
                Diff_Path_Scope scope = DIFF_PATH_SCOPE_BUILD;
                String_View relpath = {0};
                char path[_TINYDIR_PATH_MAX] = {0};
                Test_Fs_Path_Info info = {0};
                String_View file_text = {0};
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("FILE_TEXT_B64:%.*s=", (int)query->arg0.count, query->arg0.data));

                if (!diff_split_scoped_path(query->arg0, DIFF_PATH_SCOPE_BUILD, &scope, &relpath) ||
                    !diff_resolve_scoped_path_actual(scope, relpath, source_dir, binary_dir, path) ||
                    !test_fs_get_path_info(path, &info)) {
                    goto defer;
                }

                nob_sb_append_buf(&sb, prefix.data, prefix.count);
                if (!info.exists || info.is_dir) {
                    nob_sb_append_cstr(&sb, "__MISSING_FILE__\n");
                    break;
                }

                if (!diff_read_text_file(arena, path, &file_text)) goto defer;
                if (!diff_append_b64_snapshot_line(arena, &sb, nob_sv_from_cstr(""), file_text)) goto defer;
                break;
            }

            case DIFF_QUERY_FILE_SHA256: {
                Diff_Path_Scope scope = DIFF_PATH_SCOPE_BUILD;
                String_View relpath = {0};
                char path[_TINYDIR_PATH_MAX] = {0};
                Test_Fs_Path_Info info = {0};
                String_View hash = {0};
                nob_sb_append_cstr(&sb,
                                   nob_temp_sprintf("FILE_SHA256:%.*s=",
                                                    (int)query->arg0.count,
                                                    query->arg0.data));
                if (!diff_split_scoped_path(query->arg0, DIFF_PATH_SCOPE_BUILD, &scope, &relpath) ||
                    !diff_resolve_scoped_path_actual(scope, relpath, source_dir, binary_dir, path) ||
                    !test_fs_get_path_info(path, &info)) {
                    goto defer;
                }
                if (!info.exists) {
                    nob_sb_append_cstr(&sb, "__MISSING_FILE__\n");
                    break;
                }
                if (info.is_dir) {
                    nob_sb_append_cstr(&sb, "__IS_DIR__\n");
                    break;
                }
                hash = diff_hash_file_sha256_to_arena(arena, path);
                if (hash.count == 0) goto defer;
                nob_sb_append_buf(&sb, hash.data, hash.count);
                nob_sb_append(&sb, '\n');
                break;
            }

            case DIFF_QUERY_TREE: {
                Diff_Path_Scope scope = DIFF_PATH_SCOPE_BUILD;
                String_View relpath = {0};
                char path[_TINYDIR_PATH_MAX] = {0};
                Test_Fs_Path_Info info = {0};
                String_View manifest = {0};
                nob_sb_append_cstr(&sb,
                                   nob_temp_sprintf("TREE_B64:%.*s=",
                                                    (int)query->arg0.count,
                                                    query->arg0.data));
                if (!diff_split_scoped_path(query->arg0, DIFF_PATH_SCOPE_BUILD, &scope, &relpath) ||
                    !diff_resolve_scoped_path_actual(scope, relpath, source_dir, binary_dir, path) ||
                    !test_fs_get_path_info(path, &info)) {
                    goto defer;
                }
                if (!info.exists) {
                    nob_sb_append_cstr(&sb, "__MISSING_PATH__\n");
                    break;
                }
                if (!diff_build_tree_manifest(arena, path, &manifest)) goto defer;
                if (!diff_append_b64_snapshot_line(arena, &sb, nob_sv_from_cstr(""), manifest)) goto defer;
                break;
            }

            case DIFF_QUERY_VAR:
            case DIFF_QUERY_CACHE_DEFINED:
            case DIFF_QUERY_TARGET_EXISTS:
            case DIFF_QUERY_TARGET_PROP:
            case DIFF_QUERY_CMAKE_PROP:
            case DIFF_QUERY_GLOBAL_PROP:
            case DIFF_QUERY_DIR_PROP:
                break;
        }
    }

    *out_snapshot = diff_copy_sv(arena, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    ok = out_snapshot->data != NULL;

defer:
    nob_sb_free(sb);
    return ok;
}

static bool diff_build_combined_snapshot(Arena *arena,
                                         String_View probe_snapshot,
                                         String_View post_snapshot,
                                         String_View *out_snapshot) {
    Nob_String_Builder sb = {0};
    bool ok = false;

    if (out_snapshot) *out_snapshot = nob_sv_from_cstr("");
    if (!arena || !out_snapshot) return false;

    nob_sb_append_buf(&sb, probe_snapshot.data ? probe_snapshot.data : "", probe_snapshot.count);
    if (probe_snapshot.count > 0 &&
        post_snapshot.count > 0 &&
        probe_snapshot.data[probe_snapshot.count - 1] != '\n') {
        nob_sb_append(&sb, '\n');
    }
    nob_sb_append_buf(&sb, post_snapshot.data ? post_snapshot.data : "", post_snapshot.count);
    *out_snapshot = diff_copy_sv(arena, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    ok = out_snapshot->data != NULL;
    nob_sb_free(sb);
    return ok;
}

static bool diff_begin_std_capture(const char *stdout_path,
                                   const char *stderr_path,
                                   Diff_Std_Capture *capture) {
    int stdout_fd = -1;
    int stderr_fd = -1;

    if (capture) *capture = (Diff_Std_Capture){ .saved_stdout_fd = -1, .saved_stderr_fd = -1 };
    if (!stdout_path || !stderr_path || !capture) return false;

    fflush(stdout);
    fflush(stderr);

    capture->saved_stdout_fd = DIFF_DUP(DIFF_FILENO(stdout));
    capture->saved_stderr_fd = DIFF_DUP(DIFF_FILENO(stderr));
    if (capture->saved_stdout_fd < 0 || capture->saved_stderr_fd < 0) goto fail;

    stdout_fd = DIFF_OPEN(stdout_path, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666);
    stderr_fd = DIFF_OPEN(stderr_path, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666);
    if (stdout_fd < 0 || stderr_fd < 0) goto fail;

    if (DIFF_DUP2(stdout_fd, DIFF_FILENO(stdout)) < 0 ||
        DIFF_DUP2(stderr_fd, DIFF_FILENO(stderr)) < 0) {
        goto fail;
    }

    DIFF_CLOSE(stdout_fd);
    DIFF_CLOSE(stderr_fd);
    capture->active = true;
    return true;

fail:
    if (stdout_fd >= 0) DIFF_CLOSE(stdout_fd);
    if (stderr_fd >= 0) DIFF_CLOSE(stderr_fd);
    if (capture->saved_stdout_fd >= 0) {
        DIFF_CLOSE(capture->saved_stdout_fd);
        capture->saved_stdout_fd = -1;
    }
    if (capture->saved_stderr_fd >= 0) {
        DIFF_CLOSE(capture->saved_stderr_fd);
        capture->saved_stderr_fd = -1;
    }
    capture->active = false;
    return false;
}

static void diff_end_std_capture(Diff_Std_Capture *capture) {
    if (!capture || !capture->active) return;
    fflush(stdout);
    fflush(stderr);
    if (capture->saved_stdout_fd >= 0) {
        (void)DIFF_DUP2(capture->saved_stdout_fd, DIFF_FILENO(stdout));
        DIFF_CLOSE(capture->saved_stdout_fd);
        capture->saved_stdout_fd = -1;
    }
    if (capture->saved_stderr_fd >= 0) {
        (void)DIFF_DUP2(capture->saved_stderr_fd, DIFF_FILENO(stderr));
        DIFF_CLOSE(capture->saved_stderr_fd);
        capture->saved_stderr_fd = -1;
    }
    capture->active = false;
}

static bool diff_run_evaluator_case(Arena *arena,
                                    const Diff_Case *diff_case,
                                    const char *script_path,
                                    const char *source_dir,
                                    const char *binary_dir,
                                    Diff_Evaluator_Run *out_run) {
    Arena *temp_arena = NULL;
    Arena *event_arena = NULL;
    Cmake_Event_Stream *stream = NULL;
    Eval_Test_Init init = {0};
    Eval_Test_Runtime *ctx = NULL;
    String_View script = {0};
    Ast_Root root = {0};
    Diff_Std_Capture capture = {0};
    char workspace_cwd[_TINYDIR_PATH_MAX] = {0};
    char snapshot_path[_TINYDIR_PATH_MAX] = {0};
    char stdout_path[_TINYDIR_PATH_MAX] = {0};
    char stderr_path[_TINYDIR_PATH_MAX] = {0};
    bool cwd_changed = false;
    bool log_capture_started = false;

    if (out_run) *out_run = (Diff_Evaluator_Run){0};
    if (!arena || !diff_case || !script_path || !source_dir || !binary_dir || !out_run) return false;

    temp_arena = arena_create(2 * 1024 * 1024);
    event_arena = arena_create(2 * 1024 * 1024);
    if (!temp_arena || !event_arena) goto defer;

    stream = event_stream_create(event_arena);
    if (!stream) goto defer;

    if (!evaluator_load_text_file_to_arena(temp_arena, script_path, &script)) goto defer;
    root = parse_cmake(temp_arena, nob_temp_sv_to_cstr(script));

    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(source_dir);
    init.binary_dir = nob_sv_from_cstr(binary_dir);
    init.current_file = script_path;
    init.exec_mode = diff_case->mode == DIFF_MODE_SCRIPT
                         ? EVAL_EXEC_MODE_SCRIPT
                         : EVAL_EXEC_MODE_PROJECT;

    ctx = eval_test_create(&init);
    if (!ctx) goto defer;

    if (!test_fs_save_current_dir(workspace_cwd) ||
        !test_fs_join_path(binary_dir, "diff_snapshot.txt", snapshot_path) ||
        !test_fs_join_path(workspace_cwd, "evaluator_stdout.txt", stdout_path) ||
        !test_fs_join_path(workspace_cwd, "evaluator_stderr.txt", stderr_path)) {
        goto defer;
    }

    evaluator_begin_nob_log_capture();
    log_capture_started = true;
    if (!diff_begin_std_capture(stdout_path, stderr_path, &capture)) goto defer;
    if (diff_case->mode == DIFF_MODE_SCRIPT) {
        if (!nob_set_current_dir(source_dir)) goto defer;
        cwd_changed = true;
    }

    {
        Eval_Result result = eval_test_run(ctx, root);
        const Eval_Run_Report *report = eval_test_report(ctx);
        if (cwd_changed) {
            (void)nob_set_current_dir(workspace_cwd);
            cwd_changed = false;
        }
        diff_end_std_capture(&capture);
        out_run->outcome = diff_evaluator_outcome_from_result(result, report);
        if (report) {
            out_run->report = *report;
            out_run->have_report = true;
        }
    }

    evaluator_end_nob_log_capture();
    log_capture_started = false;

    if (!diff_read_text_file(arena, stdout_path, &out_run->stdout_text) ||
        !diff_read_text_file(arena, stderr_path, &out_run->stderr_text)) {
        goto defer;
    }
    out_run->stdout_text = evaluator_normalize_newlines_to_arena(arena, out_run->stdout_text);
    out_run->stderr_text = evaluator_normalize_newlines_to_arena(arena, out_run->stderr_text);

    {
        Nob_String_Builder *logs = evaluator_captured_nob_logs_ptr();
        String_View log_text = nob_sv_from_parts(logs && logs->items ? logs->items : "",
                                                 logs ? logs->count : 0);
        if (!diff_write_sv_file("evaluator_nob_log.txt", log_text)) goto defer;
    }

    if (out_run->outcome == DIFF_EXPECT_SUCCESS) {
        if (!diff_read_text_file(arena, snapshot_path, &out_run->probe_snapshot)) goto defer;
        out_run->probe_snapshot = evaluator_normalize_newlines_to_arena(arena, out_run->probe_snapshot);
    }

    if (!diff_build_postrun_snapshot(arena,
                                     diff_case,
                                     out_run->stdout_text,
                                     out_run->stderr_text,
                                     source_dir,
                                     binary_dir,
                                     &out_run->post_snapshot)) {
        goto defer;
    }

    if (out_run->outcome == DIFF_EXPECT_SUCCESS) {
        if (!diff_build_combined_snapshot(arena,
                                          out_run->probe_snapshot,
                                          out_run->post_snapshot,
                                          &out_run->combined_snapshot)) {
            goto defer;
        }
    } else {
        out_run->combined_snapshot = out_run->post_snapshot;
    }

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    return true;

defer:
    if (cwd_changed) (void)nob_set_current_dir(workspace_cwd);
    diff_end_std_capture(&capture);
    if (log_capture_started) evaluator_end_nob_log_capture();
    if (ctx) eval_test_destroy(ctx);
    if (temp_arena) arena_destroy(temp_arena);
    if (event_arena) arena_destroy(event_arena);
    return false;
}

static bool diff_run_cmake_case(Arena *arena,
                                const Diff_Case *diff_case,
                                const Diff_Cmake_Config *config,
                                const char *script_path,
                                const char *source_dir,
                                const char *binary_dir,
                                Diff_Cmake_Run *out_run) {
    Nob_Cmd cmd = {0};
    char workspace_cwd[_TINYDIR_PATH_MAX] = {0};
    char stdout_path[_TINYDIR_PATH_MAX] = {0};
    char stderr_path[_TINYDIR_PATH_MAX] = {0};
    char snapshot_path[_TINYDIR_PATH_MAX] = {0};
    bool cwd_changed = false;
    bool ok = false;

    if (out_run) *out_run = (Diff_Cmake_Run){0};
    if (!arena || !diff_case || !config || !config->available || !script_path || !source_dir ||
        !binary_dir || !out_run) {
        return false;
    }

    if (!test_fs_save_current_dir(workspace_cwd) ||
        !test_fs_join_path(workspace_cwd, "cmake_stdout.txt", stdout_path) ||
        !test_fs_join_path(workspace_cwd, "cmake_stderr.txt", stderr_path) ||
        !test_fs_join_path(binary_dir, "diff_snapshot.txt", snapshot_path)) {
        return false;
    }

    if (diff_case->mode == DIFF_MODE_SCRIPT) {
        if (!nob_set_current_dir(source_dir)) return false;
        cwd_changed = true;
        nob_cmd_append(&cmd, config->cmake_bin, "-P", script_path);
    } else {
        nob_cmd_append(&cmd, config->cmake_bin, "-S", source_dir, "-B", binary_dir);
    }
    ok = nob_cmd_run(&cmd, .stdout_path = stdout_path, .stderr_path = stderr_path);
    if (cwd_changed) {
        (void)nob_set_current_dir(workspace_cwd);
        cwd_changed = false;
    }
    nob_cmd_free(cmd);

    out_run->command_started = true;
    out_run->outcome = ok ? DIFF_EXPECT_SUCCESS : DIFF_EXPECT_ERROR;

    if (!diff_read_text_file(arena, stdout_path, &out_run->stdout_text) ||
        !diff_read_text_file(arena, stderr_path, &out_run->stderr_text)) {
        return false;
    }
    out_run->stdout_text = diff_filter_cmake_stdout_to_arena(arena, out_run->stdout_text);
    out_run->stderr_text = evaluator_normalize_newlines_to_arena(arena, out_run->stderr_text);

    if (ok) {
        if (!diff_read_text_file(arena, snapshot_path, &out_run->probe_snapshot)) return false;
        out_run->probe_snapshot = evaluator_normalize_newlines_to_arena(arena, out_run->probe_snapshot);
    }

    if (!diff_build_postrun_snapshot(arena,
                                     diff_case,
                                     out_run->stdout_text,
                                     out_run->stderr_text,
                                     source_dir,
                                     binary_dir,
                                     &out_run->post_snapshot)) {
        return false;
    }

    if (out_run->outcome == DIFF_EXPECT_SUCCESS) {
        if (!diff_build_combined_snapshot(arena,
                                          out_run->probe_snapshot,
                                          out_run->post_snapshot,
                                          &out_run->combined_snapshot)) {
            return false;
        }
    } else {
        out_run->combined_snapshot = out_run->post_snapshot;
    }

    return true;
}

static bool diff_apply_env_ops(const Diff_Case *diff_case,
                               const char *source_dir,
                               const char *binary_dir,
                               Diff_Env_Guard_List *out_guards) {
    size_t count = 0;
    if (out_guards) *out_guards = (Diff_Env_Guard_List){0};
    if (!diff_case || !source_dir || !binary_dir || !out_guards) return false;

    count = arena_arr_len(diff_case->env_ops);
    if (count == 0) return true;

    out_guards->items = calloc(count, sizeof(*out_guards->items));
    if (!out_guards->items) return false;
    out_guards->count = count;

    for (size_t i = 0; i < count; i++) {
        const Diff_Env_Op *op = &diff_case->env_ops[i];
        const char *value = NULL;
        char resolved_path[_TINYDIR_PATH_MAX] = {0};
        out_guards->items[i] = calloc(1, sizeof(*out_guards->items[i]));
        if (!out_guards->items[i]) {
            for (size_t j = i; j > 0; j--) test_host_env_guard_cleanup(out_guards->items[j - 1]);
            free(out_guards->items);
            out_guards->items = NULL;
            out_guards->count = 0;
            return false;
        }
        if (op->kind == DIFF_ENV_SET) {
            value = nob_temp_sv_to_cstr(op->value);
        } else if (op->kind == DIFF_ENV_SET_PATH) {
            if (!diff_resolve_scoped_path_actual(op->path_scope,
                                                 op->path_relpath,
                                                 source_dir,
                                                 binary_dir,
                                                 resolved_path)) {
                free(out_guards->items[i]);
                out_guards->items[i] = NULL;
                for (size_t j = i; j > 0; j--) test_host_env_guard_cleanup(out_guards->items[j - 1]);
                free(out_guards->items);
                out_guards->items = NULL;
                out_guards->count = 0;
                return false;
            }
            value = resolved_path;
        }
        if (!test_host_env_guard_begin(out_guards->items[i], nob_temp_sv_to_cstr(op->name), value)) {
            free(out_guards->items[i]);
            out_guards->items[i] = NULL;
            for (size_t j = i; j > 0; j--) test_host_env_guard_cleanup(out_guards->items[j - 1]);
            free(out_guards->items);
            out_guards->items = NULL;
            out_guards->count = 0;
            return false;
        }
    }

    return true;
}

static void diff_release_env_guards(Diff_Env_Guard_List *guards) {
    if (!guards || !guards->items) return;
    for (size_t i = guards->count; i > 0; i--) test_host_env_guard_cleanup(guards->items[i - 1]);
    free(guards->items);
    guards->items = NULL;
    guards->count = 0;
}

static bool diff_copy_path_if_exists(const char *src, const char *dst) {
    Test_Fs_Path_Info info = {0};
    if (!src || !dst) return false;
    if (!test_fs_get_path_info(src, &info)) return false;
    if (!info.exists) return true;
    if (!diff_ensure_parent_dir(dst)) return false;
    if (info.is_dir) return test_fs_copy_tree(src, dst);
    return nob_copy_file(src, dst);
}

static bool diff_preserve_failure_artifacts(String_View case_name) {
    char cwd[_TINYDIR_PATH_MAX] = {0};
    char case_root[_TINYDIR_PATH_MAX] = {0};
    char cases_root[_TINYDIR_PATH_MAX] = {0};
    char suite_work[_TINYDIR_PATH_MAX] = {0};
    char failures_root[_TINYDIR_PATH_MAX] = {0};
    char failure_dir[_TINYDIR_PATH_MAX] = {0};
    char sanitized[128] = {0};
    static const char *k_entries[] = {
        "source",
        "build_eval",
        "build_cmake",
        "cmake_stdout.txt",
        "cmake_stderr.txt",
        "evaluator_stdout.txt",
        "evaluator_stderr.txt",
        "evaluator_nob_log.txt",
        "evaluator_snapshot.txt",
        "cmake_snapshot.txt",
        "case_summary.txt",
    };

    if (!test_fs_save_current_dir(cwd)) return false;
    if (!diff_copy_string(nob_temp_dir_name(cwd), case_root)) return false;
    if (!diff_copy_string(nob_temp_dir_name(case_root), cases_root)) return false;
    if (!diff_copy_string(nob_temp_dir_name(cases_root), suite_work)) return false;

    diff_sanitize_name(case_name, sanitized);
    if (!test_fs_join_path(suite_work, "__diff_failures", failures_root)) return false;
    if (!test_fs_join_path(failures_root, sanitized, failure_dir)) return false;
    if (!diff_ensure_dir_chain(failure_dir)) return false;
    if (!test_fs_remove_tree(failure_dir)) return false;
    if (!nob_mkdir_if_not_exists(failure_dir)) return false;

    for (size_t i = 0; i < NOB_ARRAY_LEN(k_entries); i++) {
        char src[_TINYDIR_PATH_MAX] = {0};
        char dst[_TINYDIR_PATH_MAX] = {0};
        if (!test_fs_join_path(cwd, k_entries[i], src) ||
            !test_fs_join_path(failure_dir, k_entries[i], dst) ||
            !diff_copy_path_if_exists(src, dst)) {
            return false;
        }
    }

    nob_log(NOB_ERROR, "preserved differential mismatch artifacts at %s", failure_dir);
    return true;
}

static bool diff_write_case_summary(const Diff_Cmake_Config *config,
                                    const Diff_Case_Pack *case_pack,
                                    const Diff_Case *diff_case,
                                    const char *qualified_case_name,
                                    const Diff_Evaluator_Run *eval_run,
                                    const Diff_Cmake_Run *cmake_run,
                                    const char *path) {
    Nob_String_Builder sb = {0};
    bool ok = false;

    if (!config || !case_pack || !diff_case || !qualified_case_name || !eval_run || !cmake_run || !path) {
        return false;
    }

    nob_sb_append_cstr(&sb, nob_temp_sprintf("family=%s\n", case_pack->family_label));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("case_pack=%s\n", case_pack->case_pack_path));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("qualified_case=%s\n", qualified_case_name));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("case=%.*s\n", (int)diff_case->name.count, diff_case->name.data));
    nob_sb_append_cstr(&sb,
                       nob_temp_sprintf("expected=%s\n",
                                        diff_case->expected_outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR"));
    nob_sb_append_cstr(&sb,
                       nob_temp_sprintf("mode=%s\n",
                                        diff_case->mode == DIFF_MODE_SCRIPT ? "SCRIPT" : "PROJECT"));
    nob_sb_append_cstr(&sb,
                       nob_temp_sprintf("layout=%s\n",
                                        diff_case->mode == DIFF_MODE_SCRIPT
                                            ? "N/A"
                                            : (diff_case->layout == DIFF_LAYOUT_RAW_CMAKELISTS
                                                   ? "RAW_CMAKELISTS"
                                                   : "BODY_ONLY_PROJECT")));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("cmake_bin=%s\n", config->cmake_bin));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("cmake_version=%s\n", config->cmake_version));
    nob_sb_append_cstr(&sb,
                       nob_temp_sprintf("evaluator_outcome=%s\n",
                                        eval_run->outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR"));
    nob_sb_append_cstr(&sb,
                       nob_temp_sprintf("cmake_outcome=%s\n",
                                        cmake_run->outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR"));
    if (eval_run->have_report) {
        nob_sb_append_cstr(&sb,
                           nob_temp_sprintf("evaluator_report=warnings:%zu errors:%zu unsupported:%zu overall:%d\n",
                                            eval_run->report.warning_count,
                                            eval_run->report.error_count,
                                            eval_run->report.unsupported_count,
                                            (int)eval_run->report.overall_status));
    }

    nob_sb_append(&sb, '\0');
    ok = diff_write_entire_file(path, sb.items ? sb.items : "");
    nob_sb_free(sb);
    return ok;
}

static bool diff_record_snapshots(const Diff_Evaluator_Run *eval_run,
                                  const Diff_Cmake_Run *cmake_run) {
    bool ok = true;
    ok = ok && diff_write_sv_file("evaluator_snapshot.txt", eval_run->combined_snapshot);
    ok = ok && diff_write_sv_file("cmake_snapshot.txt", cmake_run->combined_snapshot);
    return ok;
}

static bool diff_case_matches(const Diff_Case *diff_case,
                              const Diff_Evaluator_Run *eval_run,
                              const Diff_Cmake_Run *cmake_run) {
    if (!diff_case || !eval_run || !cmake_run) return false;
    if (eval_run->outcome != diff_case->expected_outcome) return false;
    if (cmake_run->outcome != diff_case->expected_outcome) return false;
    if (diff_case->expected_outcome == DIFF_EXPECT_ERROR) {
        if (!diff_case_requires_postrun_compare(diff_case)) return true;
        return nob_sv_eq(eval_run->post_snapshot, cmake_run->post_snapshot);
    }
    return nob_sv_eq(eval_run->combined_snapshot, cmake_run->combined_snapshot);
}

static bool diff_resolve_cmake(Diff_Cmake_Config *out_config,
                               char skip_reason[256]) {
    Nob_Cmd cmd = {0};
    char stdout_path[_TINYDIR_PATH_MAX] = {0};
    char stderr_path[_TINYDIR_PATH_MAX] = {0};
    String_View version_text = {0};
    Arena *arena = NULL;
    const char *env_path = NULL;
    bool found = false;

    if (out_config) *out_config = (Diff_Cmake_Config){0};
    if (skip_reason) skip_reason[0] = '\0';
    if (!out_config || !skip_reason) return false;

    env_path = getenv(CMK2NOB_TEST_CMAKE_BIN_ENV);
    if (env_path && env_path[0] != '\0') {
        if (strchr(env_path, '/') || strchr(env_path, '\\')) {
            found = diff_path_is_executable(env_path) && diff_copy_string(env_path, out_config->cmake_bin);
        } else {
            found = test_ws_host_program_in_path(env_path, out_config->cmake_bin);
        }
        if (!found) {
            snprintf(skip_reason, 256, "%s does not point to an executable", CMK2NOB_TEST_CMAKE_BIN_ENV);
            return true;
        }
    } else if (!test_ws_host_program_in_path("cmake", out_config->cmake_bin)) {
        snprintf(skip_reason, 256, "cmake not found in PATH");
        return true;
    }

    if (!test_fs_join_path(".", "__cmake_version_stdout.txt", stdout_path) ||
        !test_fs_join_path(".", "__cmake_version_stderr.txt", stderr_path)) {
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
    if (!diff_read_text_file(arena, stdout_path, &version_text)) {
        arena_destroy(arena);
        return false;
    }
    version_text = evaluator_normalize_newlines_to_arena(arena, version_text);

    if (!diff_sv_has_prefix(version_text, "cmake version 3.28.")) {
        size_t line_end = 0;
        while (line_end < version_text.count && version_text.data[line_end] != '\n') line_end++;
        snprintf(skip_reason,
                 256,
                 "requires CMake 3.28.x, found %.*s",
                 (int)line_end,
                 version_text.data);
        arena_destroy(arena);
        return true;
    }

    {
        const char *prefix = "cmake version ";
        size_t prefix_len = strlen(prefix);
        size_t line_end = prefix_len;
        while (line_end < version_text.count && version_text.data[line_end] != '\n') line_end++;
        size_t version_len = line_end - prefix_len;
        if (version_len >= sizeof(out_config->cmake_version)) version_len = sizeof(out_config->cmake_version) - 1;
        memcpy(out_config->cmake_version, version_text.data + prefix_len, version_len);
        out_config->cmake_version[version_len] = '\0';
    }

    out_config->available = true;
    arena_destroy(arena);
    return true;
}

static void run_diff_case(const Diff_Cmake_Config *config,
                          const Diff_Case_Pack *case_pack,
                          const Diff_Case *diff_case,
                          int *passed,
                          int *failed,
                          int *skipped) {
    Test_Case_Workspace ws = {0};
    Arena *arena = NULL;
    Diff_Env_Guard_List env_guards = {0};
    Diff_Env_Guard_List cmake_env_guards = {0};
    char case_cwd[_TINYDIR_PATH_MAX] = {0};
    char source_dir[_TINYDIR_PATH_MAX] = {0};
    char build_eval_dir[_TINYDIR_PATH_MAX] = {0};
    char build_cmake_dir[_TINYDIR_PATH_MAX] = {0};
    char script_path[_TINYDIR_PATH_MAX] = {0};
    char qualified_case_name[_TINYDIR_PATH_MAX] = {0};
    Diff_Evaluator_Run eval_run = {0};
    Diff_Cmake_Run cmake_run = {0};
    bool ok = false;

    if (!config || !case_pack || !diff_case || !passed || !failed || !skipped) return;
    (void)skipped;

    if (!diff_build_qualified_case_name(case_pack->family_label, diff_case->name, qualified_case_name) ||
        !test_ws_case_enter(&ws, qualified_case_name)) {
        test_v2_emit_failure_message(__func__, 0, "could not enter isolated differential test workspace");
        nob_log(NOB_ERROR, "FAILED: %s: could not enter isolated differential test workspace", qualified_case_name);
        (*failed)++;
        return;
    }

    arena = arena_create(1024 * 1024);
    if (!arena) goto fail;

    if (!test_fs_save_current_dir(case_cwd) ||
        !test_fs_join_path(case_cwd, "source", source_dir) ||
        !test_fs_join_path(case_cwd, "build_eval", build_eval_dir) ||
        !test_fs_join_path(case_cwd, "build_cmake", build_cmake_dir) ||
        !test_fs_join_path(source_dir,
                           diff_case->mode == DIFF_MODE_SCRIPT ? "diff_script.cmake" : "CMakeLists.txt",
                           script_path)) {
        goto fail;
    }

    if (!nob_mkdir_if_not_exists(source_dir) ||
        !nob_mkdir_if_not_exists(build_eval_dir) ||
        !nob_mkdir_if_not_exists(build_cmake_dir) ||
        !diff_prepare_case_fixtures(diff_case, source_dir, build_eval_dir, build_cmake_dir) ||
        !diff_generate_case_script(arena, diff_case, script_path, &(String_View){0}) ||
        !diff_apply_env_ops(diff_case,
                           source_dir,
                           diff_case->mode == DIFF_MODE_SCRIPT ? source_dir : build_eval_dir,
                           &env_guards) ||
        !diff_run_evaluator_case(arena,
                                 diff_case,
                                 script_path,
                                 source_dir,
                                 diff_case->mode == DIFF_MODE_SCRIPT ? source_dir : build_eval_dir,
                                 &eval_run)) {
        goto fail;
    }

    if (diff_case->mode == DIFF_MODE_SCRIPT) {
        if (!test_fs_remove_tree(source_dir) ||
            !nob_mkdir_if_not_exists(source_dir) ||
            !diff_prepare_case_fixtures(diff_case, source_dir, build_eval_dir, build_cmake_dir) ||
            !diff_generate_case_script(arena, diff_case, script_path, &(String_View){0})) {
            goto fail;
        }
    }
    diff_release_env_guards(&env_guards);

    if (!diff_apply_env_ops(diff_case,
                            source_dir,
                            diff_case->mode == DIFF_MODE_SCRIPT ? source_dir : build_cmake_dir,
                            &cmake_env_guards) ||
        !diff_run_cmake_case(arena,
                             diff_case,
                             config,
                             script_path,
                             source_dir,
                             diff_case->mode == DIFF_MODE_SCRIPT ? source_dir : build_cmake_dir,
                             &cmake_run) ||
        !diff_record_snapshots(&eval_run, &cmake_run) ||
        !diff_write_case_summary(config,
                                 case_pack,
                                 diff_case,
                                 qualified_case_name,
                                 &eval_run,
                                 &cmake_run,
                                 "case_summary.txt")) {
        goto fail;
    }

    ok = diff_case_matches(diff_case, &eval_run, &cmake_run);
    if (!ok) {
        nob_log(NOB_ERROR,
                "differential mismatch in case %s (expected=%s evaluator=%s cmake=%s)",
                qualified_case_name,
                diff_case->expected_outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR",
                eval_run.outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR",
                cmake_run.outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR");
        if (diff_case->expected_outcome == DIFF_EXPECT_SUCCESS) {
            nob_log(NOB_ERROR,
                    "snapshot mismatch in case %s\n--- evaluator ---\n%.*s--- cmake ---\n%.*s",
                    qualified_case_name,
                    (int)eval_run.combined_snapshot.count,
                    eval_run.combined_snapshot.data ? eval_run.combined_snapshot.data : "",
                    (int)cmake_run.combined_snapshot.count,
                    cmake_run.combined_snapshot.data ? cmake_run.combined_snapshot.data : "");
        }
        (void)diff_preserve_failure_artifacts(nob_sv_from_cstr(qualified_case_name));
        (*failed)++;
    } else {
        (*passed)++;
    }

    diff_release_env_guards(&env_guards);
    diff_release_env_guards(&cmake_env_guards);
    arena_destroy(arena);
    if (!test_ws_case_leave(&ws)) {
        nob_log(NOB_ERROR, "FAILED: %s: could not cleanup isolated differential test workspace",
                qualified_case_name);
        (*failed)++;
    }
    return;

fail:
    nob_log(NOB_ERROR, "FAILED: %s: differential harness error", qualified_case_name);
    (void)diff_preserve_failure_artifacts(nob_sv_from_cstr(qualified_case_name));
    diff_release_env_guards(&env_guards);
    diff_release_env_guards(&cmake_env_guards);
    if (arena) arena_destroy(arena);
    (*failed)++;
    if (!test_ws_case_leave(&ws)) {
        nob_log(NOB_ERROR, "FAILED: %s: could not cleanup isolated differential test workspace",
                qualified_case_name);
        (*failed)++;
    }
}

static void run_evaluator_diff_case_pack(const Diff_Cmake_Config *config,
                                         const Diff_Case_Pack *case_pack,
                                         int *passed,
                                         int *failed,
                                         int *skipped) {
    Arena *arena = NULL;
    String_View content = {0};
    Test_Case_Pack_Entry *entries = NULL;

    if (!config || !case_pack || !passed || !failed || !skipped) return;
    (void)skipped;

    arena = arena_create(512 * 1024);
    if (!arena) {
        (*failed)++;
        return;
    }

    if (!evaluator_load_text_file_to_arena(arena, case_pack->case_pack_path, &content) ||
        !test_case_pack_parse(arena, content, &entries)) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to parse %s", case_pack->case_pack_path);
        arena_destroy(arena);
        (*failed)++;
        return;
    }

    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        Diff_Case diff_case = {0};
        if (!diff_parse_case(arena, entries[i], &diff_case)) {
            nob_log(NOB_ERROR,
                    "evaluator diff suite: failed to parse metadata for family %s case %.*s",
                    case_pack->family_label,
                    (int)entries[i].name.count,
                    entries[i].name.data);
            (*failed)++;
            continue;
        }
        run_diff_case(config, case_pack, &diff_case, passed, failed, skipped);
    }

    arena_destroy(arena);
}

void run_evaluator_diff_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    Diff_Cmake_Config cmake = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    char skip_reason[256] = {0};
    bool prepared = test_ws_prepare(&ws, "evaluator_diff");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    if (!diff_resolve_cmake(&cmake, skip_reason)) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to resolve cmake runtime");
        if (failed) (*failed)++;
    } else if (!cmake.available) {
        nob_log(NOB_INFO, "SKIPPED: evaluator diff suite: %s", skip_reason);
        if (skipped) (*skipped)++;
    } else {
        for (size_t i = 0; i < NOB_ARRAY_LEN(s_diff_case_packs); i++) {
            run_evaluator_diff_case_pack(&cmake, &s_diff_case_packs[i], passed, failed, skipped);
        }
    }

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
