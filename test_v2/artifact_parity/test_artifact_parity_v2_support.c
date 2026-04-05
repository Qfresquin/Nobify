#include "test_artifact_parity_v2_support.h"

#include "test_fs.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
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
static char s_artifact_parity_cmake_bin_dir[_TINYDIR_PATH_MAX] = {0};

static bool artifact_parity_mkdirs(const char *path) {
    char buf[_TINYDIR_PATH_MAX] = {0};
    size_t len = 0;
    if (!path || path[0] == '\0' || strcmp(path, ".") == 0) return true;
    len = strlen(path);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, path, len + 1);

    for (size_t i = 1; i < len; ++i) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        if (buf[0] != '\0' && !nob_mkdir_if_not_exists(buf)) return false;
        buf[i] = '/';
    }

    return nob_mkdir_if_not_exists(buf);
}

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

static bool artifact_parity_copy_parent_dir(const char *path,
                                            char out[_TINYDIR_PATH_MAX]) {
    const char *slash = NULL;
    size_t len = 0;
    if (!path || !out) return false;
    slash = strrchr(path, '/');
#if defined(_WIN32)
    {
        const char *backslash = strrchr(path, '\\');
        if (!slash || (backslash && backslash > slash)) slash = backslash;
    }
#endif
    if (!slash) return artifact_parity_copy_string(".", out);
    len = (size_t)(slash - path);
    if (len == 0) len = 1;
    if (len >= _TINYDIR_PATH_MAX) return false;
    memcpy(out, path, len);
    out[len] = '\0';
    return true;
}

static bool artifact_parity_push_cmake_tool_path(char **saved_path_out) {
    const char *old_path = NULL;
    char *new_path = NULL;
    size_t new_len = 0;
    if (!saved_path_out) return false;
    *saved_path_out = NULL;
    if (s_artifact_parity_cmake_bin_dir[0] == '\0') return true;

    old_path = getenv("PATH");
    if (old_path) {
        *saved_path_out = strdup(old_path);
        if (!*saved_path_out) return false;
    }

    new_len = strlen(s_artifact_parity_cmake_bin_dir) + 1;
    if (old_path && old_path[0] != '\0') new_len += 1 + strlen(old_path);
    new_path = (char*)malloc(new_len);
    if (!new_path) {
        free(*saved_path_out);
        *saved_path_out = NULL;
        return false;
    }

#if defined(_WIN32)
    if (old_path && old_path[0] != '\0') {
        snprintf(new_path, new_len, "%s;%s", s_artifact_parity_cmake_bin_dir, old_path);
    } else {
        snprintf(new_path, new_len, "%s", s_artifact_parity_cmake_bin_dir);
    }
    if (_putenv_s("PATH", new_path) != 0) {
        free(new_path);
        free(*saved_path_out);
        *saved_path_out = NULL;
        return false;
    }
#else
    if (old_path && old_path[0] != '\0') {
        snprintf(new_path, new_len, "%s:%s", s_artifact_parity_cmake_bin_dir, old_path);
    } else {
        snprintf(new_path, new_len, "%s", s_artifact_parity_cmake_bin_dir);
    }
    if (setenv("PATH", new_path, 1) != 0) {
        free(new_path);
        free(*saved_path_out);
        *saved_path_out = NULL;
        return false;
    }
#endif

    free(new_path);
    return true;
}

static void artifact_parity_pop_cmake_tool_path(char *saved_path) {
#if defined(_WIN32)
    if (saved_path) {
        (void)_putenv_s("PATH", saved_path);
    } else {
        (void)_putenv_s("PATH", "");
    }
#else
    if (saved_path) {
        (void)setenv("PATH", saved_path, 1);
    } else {
        (void)unsetenv("PATH");
    }
#endif
    free(saved_path);
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

static bool artifact_parity_make_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    return true;
#else
    return chmod(path, 0755) == 0;
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
        case ARTIFACT_PARITY_CAPTURE_FILE_SHA256: return "FILE_SHA256";
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

static bool artifact_parity_find_sibling_tool(const char *tool_path,
                                              const char *tool_name,
                                              char out_path[_TINYDIR_PATH_MAX]) {
    const char *tool_dir = NULL;
    if (!tool_path || !tool_name || !out_path) return false;
    tool_dir = nob_temp_dir_name(tool_path);
    if (!tool_dir || tool_dir[0] == '\0') return false;
    if (!test_fs_join_path(tool_dir, tool_name, out_path)) return false;
    return artifact_parity_path_is_executable(out_path);
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

static uint32_t artifact_parity_load_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static void artifact_parity_store_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)((v >> 24) & 0xFFU);
    p[1] = (unsigned char)((v >> 16) & 0xFFU);
    p[2] = (unsigned char)((v >> 8) & 0xFFU);
    p[3] = (unsigned char)(v & 0xFFU);
}

static uint32_t artifact_parity_rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static void artifact_parity_sha256_process_block(uint32_t state[8],
                                                 const unsigned char block[64]) {
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
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;
    uint32_t e = 0;
    uint32_t f = 0;
    uint32_t g = 0;
    uint32_t h = 0;

    for (size_t i = 0; i < 16; i++) w[i] = artifact_parity_load_be32(block + (i * 4));
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = artifact_parity_rotr32(w[i - 15], 7) ^
                      artifact_parity_rotr32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = artifact_parity_rotr32(w[i - 2], 17) ^
                      artifact_parity_rotr32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    for (size_t i = 0; i < 64; i++) {
        uint32_t s1 = artifact_parity_rotr32(e, 6) ^
                      artifact_parity_rotr32(e, 11) ^
                      artifact_parity_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = artifact_parity_rotr32(a, 2) ^
                      artifact_parity_rotr32(a, 13) ^
                      artifact_parity_rotr32(a, 22);
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

static void artifact_parity_sha256_compute(const unsigned char *msg,
                                           size_t len,
                                           unsigned char out[32]) {
    static const uint32_t init_state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    uint32_t state[8] = {0};
    unsigned char tail[128] = {0};
    size_t rem = 0;
    size_t tail_len = 0;
    uint64_t bits = 0;

    memcpy(state, init_state, sizeof(state));

    for (size_t i = 0; i + 64 <= len; i += 64) {
        artifact_parity_sha256_process_block(state, msg + i);
    }

    rem = len % 64;
    if (rem > 0) memcpy(tail, msg + (len - rem), rem);
    tail[rem] = 0x80U;
    tail_len = rem < 56 ? 64 : 128;
    bits = ((uint64_t)len) * 8U;
    for (size_t i = 0; i < 8; i++) {
        tail[tail_len - 1 - i] = (unsigned char)((bits >> (8 * i)) & 0xFFU);
    }
    artifact_parity_sha256_process_block(state, tail);
    if (tail_len == 128) artifact_parity_sha256_process_block(state, tail + 64);

    for (size_t i = 0; i < 8; i++) artifact_parity_store_be32(out + (i * 4), state[i]);
}

static String_View artifact_parity_sha256_hex_to_arena(Arena *arena,
                                                       const void *data,
                                                       size_t size) {
    static const char lut[] = "0123456789abcdef";
    const unsigned char *bytes = (const unsigned char*)data;
    unsigned char digest[32] = {0};
    char *hex = NULL;

    if (!arena || (!bytes && size > 0)) return nob_sv_from_cstr("");
    artifact_parity_sha256_compute(bytes ? bytes : (const unsigned char*)"", size, digest);
    hex = arena_alloc(arena, 65);
    if (!hex) return nob_sv_from_cstr("");
    for (size_t i = 0; i < sizeof(digest); i++) {
        hex[i * 2 + 0] = lut[(digest[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = lut[digest[i] & 0x0F];
    }
    hex[64] = '\0';
    return nob_sv_from_parts(hex, 64);
}

static String_View artifact_parity_hash_file_sha256_to_arena(Arena *arena, const char *path) {
    Nob_String_Builder sb = {0};
    String_View out = nob_sv_from_cstr("");
    if (!arena || !path) return out;
    if (!nob_read_entire_file(path, &sb)) return out;
    out = artifact_parity_sha256_hex_to_arena(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return out;
}

static bool artifact_parity_append_file_sha256_manifest(Arena *arena,
                                                        Nob_String_Builder *sb,
                                                        const char *abs_path) {
    Test_Fs_Path_Info info = {0};
    String_View hash = {0};
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

    hash = artifact_parity_hash_file_sha256_to_arena(arena, abs_path);
    if (hash.count == 0) return false;
    nob_sb_append_cstr(sb, "STATUS PRESENT\nSHA256 ");
    test_snapshot_append_escaped_sv(sb, hash);
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
                                   bool require_cpack,
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
    if (!artifact_parity_copy_parent_dir(out_config->cmake_bin, s_artifact_parity_cmake_bin_dir)) {
        arena_destroy(arena);
        return false;
    }
    if (artifact_parity_find_sibling_tool(out_config->cmake_bin, "cpack", out_config->cpack_bin)) {
        out_config->cpack_available = true;
    }
    if (require_cpack && !out_config->cpack_available) {
        snprintf(skip_reason,
                 256,
                 "package phase requires sibling cpack next to resolved cmake");
    }

    arena_destroy(arena);
    return true;
}

bool artifact_parity_resolve_nobify_bin(char out_path[_TINYDIR_PATH_MAX],
                                        char error_reason[256]) {
    const char *env_path = getenv(CMK2NOB_TEST_NOBIFY_BIN_ENV);
    if (!out_path || !error_reason) return false;
    out_path[0] = '\0';
    error_reason[0] = '\0';

    if (!env_path || env_path[0] == '\0') {
        snprintf(error_reason,
                 256,
                 "%s is not set; artifact-parity requires a runner-built nobify tool",
                 CMK2NOB_TEST_NOBIFY_BIN_ENV);
        return false;
    }

    if (!artifact_parity_path_is_executable(env_path) ||
        !artifact_parity_copy_string(env_path, out_path)) {
        snprintf(error_reason,
                 256,
                 "%s does not point to an executable",
                 CMK2NOB_TEST_NOBIFY_BIN_ENV);
        return false;
    }

    return true;
}

bool artifact_parity_write_text_file(const char *path, const char *text) {
    const char *dir = NULL;
    if (!path || !text) return false;
    dir = nob_temp_dir_name(path);
    if (dir && strcmp(dir, ".") != 0 && !artifact_parity_mkdirs(dir)) return false;
    return nob_write_entire_file(path, text, strlen(text));
}

bool artifact_parity_write_executable_file(const char *path, const char *text) {
    if (!artifact_parity_write_text_file(path, text)) return false;
    return artifact_parity_make_executable(path);
}

bool artifact_parity_materialize_files(const char *root_dir,
                                       const Artifact_Parity_File *files,
                                       size_t file_count) {
    char full_path[_TINYDIR_PATH_MAX] = {0};
    if (!files && file_count > 0) return false;

    for (size_t i = 0; i < file_count; ++i) {
        if (!files[i].path || !files[i].contents) return false;
        if (root_dir && root_dir[0] != '\0') {
            if (!test_fs_join_path(root_dir, files[i].path, full_path)) return false;
        } else if (!artifact_parity_copy_string(files[i].path, full_path)) {
            return false;
        }
        if (!artifact_parity_write_text_file(full_path, files[i].contents)) return false;
    }

    return true;
}

bool artifact_parity_run_nobify(const char *nobify_bin,
                                const char *input_path,
                                const char *output_path,
                                const char *source_root,
                                const char *binary_root) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!nobify_bin || !input_path || !output_path) return false;
    nob_cmd_append(&cmd, nobify_bin);
    if (source_root && source_root[0] != '\0') {
        nob_cmd_append(&cmd, "--source-root", source_root);
    }
    if (binary_root && binary_root[0] != '\0') {
        nob_cmd_append(&cmd, "--binary-root", binary_root);
    }
    nob_cmd_append(&cmd, "--out", output_path, input_path);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
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
    char *saved_path = NULL;
    bool ok = false;
    if (!dir || !binary_path || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);

    if (!nob_set_current_dir(dir)) return false;
    if (!artifact_parity_push_cmake_tool_path(&saved_path)) {
        (void)nob_set_current_dir(prev_cwd);
        return false;
    }
    nob_cmd_append(&cmd, binary_path);
    if (arg1) nob_cmd_append(&cmd, arg1);
    if (arg2) nob_cmd_append(&cmd, arg2);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    artifact_parity_pop_cmake_tool_path(saved_path);
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

bool artifact_parity_run_cmake_install(const Artifact_Parity_Cmake_Config *config,
                                       const char *binary_dir,
                                       const char *prefix_dir) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!config || !config->available || !binary_dir || !prefix_dir) return false;
    nob_cmd_append(&cmd, config->cmake_bin, "--install", binary_dir, "--prefix", prefix_dir);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool artifact_parity_run_cmake_package(const Artifact_Parity_Cmake_Config *config,
                                       const char *binary_dir) {
    Nob_Cmd cmd = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;

    if (!config || !config->available || !config->cpack_available || !binary_dir || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);

    if (!nob_set_current_dir(binary_dir)) return false;
    nob_cmd_append(&cmd, config->cpack_bin);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    if (!nob_set_current_dir(prev_cwd)) return false;
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

        switch (request->capture) {
            case ARTIFACT_PARITY_CAPTURE_TREE:
                if (!artifact_parity_append_tree_manifest(arena, &sb, abs_path)) {
                    nob_sb_free(sb);
                    return false;
                }
                break;

            case ARTIFACT_PARITY_CAPTURE_FILE_TEXT:
                if (!artifact_parity_append_file_text_manifest(arena, &sb, abs_path)) {
                    nob_sb_free(sb);
                    return false;
                }
                break;

            case ARTIFACT_PARITY_CAPTURE_FILE_SHA256:
                if (!artifact_parity_append_file_sha256_manifest(arena, &sb, abs_path)) {
                    nob_sb_free(sb);
                    return false;
                }
                break;
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
