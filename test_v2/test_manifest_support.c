#include "test_manifest_support.h"

#include "arena_dyn.h"
#include "test_fs.h"
#include "test_snapshot_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TEST_TREE_ENTRY_DIR = 0,
    TEST_TREE_ENTRY_FILE,
    TEST_TREE_ENTRY_LINK,
} Test_Tree_Entry_Kind;

typedef struct {
    Test_Tree_Entry_Kind kind;
    String_View relpath;
} Test_Tree_Entry;

static const char *test_tree_entry_kind_name(Test_Tree_Entry_Kind kind) {
    switch (kind) {
        case TEST_TREE_ENTRY_DIR: return "DIR";
        case TEST_TREE_ENTRY_FILE: return "FILE";
        case TEST_TREE_ENTRY_LINK: return "LINK";
    }
    return "UNKNOWN";
}

static int test_tree_entry_compare(const void *lhs, const void *rhs) {
    const Test_Tree_Entry *a = (const Test_Tree_Entry*)lhs;
    const Test_Tree_Entry *b = (const Test_Tree_Entry*)rhs;
    size_t min_len = 0;
    int cmp = 0;
    if (!a || !b) return 0;
    min_len = a->relpath.count < b->relpath.count ? a->relpath.count : b->relpath.count;
    if (min_len > 0) {
        cmp = memcmp(a->relpath.data, b->relpath.data, min_len);
        if (cmp != 0) return cmp;
    }
    if (a->relpath.count < b->relpath.count) return -1;
    if (a->relpath.count > b->relpath.count) return 1;
    if (a->kind < b->kind) return -1;
    if (a->kind > b->kind) return 1;
    return 0;
}

static bool test_manifest_collect_tree_entries(Arena *arena,
                                               const char *abs_path,
                                               const char *relpath,
                                               Test_Tree_Entry **out_entries) {
    Test_Fs_Path_Info info = {0};
    if (!arena || !abs_path || !out_entries) return false;
    if (!test_fs_get_path_info(abs_path, &info) || !info.exists) return false;

    if (relpath && relpath[0] != '\0') {
        char *copy = arena_strdup(arena, relpath);
        Test_Tree_Entry entry = {0};
        if (!copy) return false;
        entry.kind = info.is_link_like
            ? TEST_TREE_ENTRY_LINK
            : (info.is_dir ? TEST_TREE_ENTRY_DIR : TEST_TREE_ENTRY_FILE);
        entry.relpath = nob_sv_from_cstr(copy);
        if (!arena_arr_push(arena, *out_entries, entry)) return false;
    }

    if (!info.is_dir || info.is_link_like) return true;

    {
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
            } else if (snprintf(child_rel, sizeof(child_rel), "%s", dir.name) >=
                       (int)sizeof(child_rel)) {
                ok = false;
                break;
            }
            if (!test_manifest_collect_tree_entries(arena, child_abs, child_rel, out_entries)) {
                ok = false;
                break;
            }
        }

        if (dir.error) ok = false;
        nob_dir_entry_close(dir);
        return ok;
    }
}

static void test_manifest_sha256_process_block(uint32_t state[8],
                                               const unsigned char block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t w[64] = {0};
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (size_t i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; ++i) {
        uint32_t s0 = ((w[i - 15] >> 7) | (w[i - 15] << 25)) ^
                      ((w[i - 15] >> 18) | (w[i - 15] << 14)) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = ((w[i - 2] >> 17) | (w[i - 2] << 15)) ^
                      ((w[i - 2] >> 19) | (w[i - 2] << 13)) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    for (size_t i = 0; i < 64; ++i) {
        uint32_t s1 = ((e >> 6) | (e << 26)) ^
                      ((e >> 11) | (e << 21)) ^
                      ((e >> 25) | (e << 7));
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = ((a >> 2) | (a << 30)) ^
                      ((a >> 13) | (a << 19)) ^
                      ((a >> 22) | (a << 10));
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

static void test_manifest_sha256_compute(const unsigned char *msg,
                                         size_t len,
                                         unsigned char out[32]) {
    static const uint32_t init[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    uint32_t state[8] = {0};
    unsigned char tail[128] = {0};
    size_t full_blocks = len / 64;
    size_t rem = len % 64;
    size_t tail_len = 0;
    uint64_t bit_len = (uint64_t)len * 8u;

    memcpy(state, init, sizeof(state));
    for (size_t i = 0; i < full_blocks; ++i) {
        test_manifest_sha256_process_block(state, msg + (i * 64));
    }

    if (rem > 0) memcpy(tail, msg + (full_blocks * 64), rem);
    tail[rem] = 0x80u;
    tail_len = rem + 1;
    if (tail_len > 56) {
        memset(tail + tail_len, 0, 64 - tail_len);
        tail_len = 64;
        memset(tail + 64, 0, 56);
        tail_len = 128;
    } else {
        memset(tail + tail_len, 0, 56 - tail_len);
        tail_len = 64;
    }

    for (size_t i = 0; i < 8; ++i) {
        tail[tail_len - 8 + i] = (unsigned char)((bit_len >> (56 - (i * 8))) & 0xFFu);
    }

    test_manifest_sha256_process_block(state, tail);
    if (tail_len == 128) test_manifest_sha256_process_block(state, tail + 64);

    for (size_t i = 0; i < 8; ++i) {
        out[i * 4] = (unsigned char)((state[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = (unsigned char)(state[i] & 0xFFu);
    }
}

static String_View test_manifest_sha256_hex_to_arena(Arena *arena,
                                                     const unsigned char *data,
                                                     size_t size) {
    static const char digits[] = "0123456789abcdef";
    unsigned char digest[32] = {0};
    char *hex = NULL;
    if (!arena) return nob_sv_from_cstr("");
    test_manifest_sha256_compute(data ? data : (const unsigned char*)"", size, digest);
    hex = arena_alloc(arena, 65);
    if (!hex) return nob_sv_from_cstr("");
    for (size_t i = 0; i < 32; ++i) {
        hex[i * 2] = digits[(digest[i] >> 4) & 0x0Fu];
        hex[i * 2 + 1] = digits[digest[i] & 0x0Fu];
    }
    hex[64] = '\0';
    return nob_sv_from_parts(hex, 64);
}

static String_View test_manifest_hash_file_sha256_to_arena(Arena *arena,
                                                           const char *path) {
    Nob_String_Builder sb = {0};
    String_View out = {0};
    if (!arena || !path) return nob_sv_from_cstr("");
    if (!nob_read_entire_file(path, &sb)) return nob_sv_from_cstr("");
    out = test_manifest_sha256_hex_to_arena(arena,
                                            (const unsigned char*)(sb.items ? sb.items : ""),
                                            sb.count);
    nob_sb_free(sb);
    return out;
}

static const char *test_manifest_capture_name(Test_Manifest_Capture_Kind capture) {
    switch (capture) {
        case TEST_MANIFEST_CAPTURE_TREE: return "TREE";
        case TEST_MANIFEST_CAPTURE_FILE_TEXT: return "FILE_TEXT";
        case TEST_MANIFEST_CAPTURE_FILE_SHA256: return "FILE_SHA256";
    }
    return "UNKNOWN";
}

static bool test_manifest_append_header(Nob_String_Builder *sb,
                                        const Test_Manifest_Request *request) {
    if (!sb || !request || !request->label) return false;
    nob_sb_append_cstr(sb, "BEGIN SECTION ");
    nob_sb_append_cstr(sb, request->label);
    nob_sb_append_cstr(sb, " capture=");
    nob_sb_append_cstr(sb, test_manifest_capture_name(request->capture));
    nob_sb_append_cstr(sb, " relpath=");
    nob_sb_append_cstr(sb, request->relpath ? request->relpath : "");
    nob_sb_append_cstr(sb, "\n");
    return true;
}

static bool test_manifest_append_file_text(Arena *arena,
                                           Nob_String_Builder *sb,
                                           const char *abs_path) {
    Test_Fs_Path_Info info = {0};
    String_View text = {0};
    if (!arena || !sb || !abs_path) return false;
    if (!test_fs_get_path_info(abs_path, &info)) return false;
    if (!info.exists) {
        nob_sb_append_cstr(sb, "STATUS MISSING\n");
        return true;
    }
    if (info.is_dir) {
        nob_sb_append_cstr(sb, "STATUS IS_DIR\n");
        return true;
    }
    if (!test_snapshot_load_text_file_to_arena(arena, abs_path, &text)) return false;
    text = test_snapshot_normalize_newlines_to_arena(arena, text);
    nob_sb_append_cstr(sb, "STATUS PRESENT\n");
    nob_sb_append_cstr(sb, "TEXT\n");
    nob_sb_append_buf(sb, text.data ? text.data : "", text.count);
    if (text.count == 0 || text.data[text.count - 1] != '\n') {
        nob_sb_append_cstr(sb, "\n");
    }
    return true;
}

static bool test_manifest_append_file_sha256(Arena *arena,
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
        nob_sb_append_cstr(sb, "STATUS IS_DIR\n");
        return true;
    }
    hash = test_manifest_hash_file_sha256_to_arena(arena, abs_path);
    if (!hash.data) return false;
    nob_sb_append_cstr(sb, "STATUS PRESENT\n");
    nob_sb_append_cstr(sb, "SHA256 ");
    nob_sb_append_buf(sb, hash.data, hash.count);
    nob_sb_append_cstr(sb, "\n");
    return true;
}

bool test_manifest_capture_tree(Arena *arena,
                                const char *abs_path,
                                String_View *out) {
    Test_Fs_Path_Info info = {0};
    Nob_String_Builder sb = {0};
    Test_Tree_Entry *entries = NULL;
    char *copy = NULL;
    if (!arena || !abs_path || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!test_fs_get_path_info(abs_path, &info)) return false;

    if (!info.exists) {
        copy = arena_strdup(arena, "STATUS MISSING\n");
        if (!copy) return false;
        *out = nob_sv_from_cstr(copy);
        return true;
    }

    nob_sb_append_cstr(&sb, "STATUS PRESENT root_kind=");
    nob_sb_append_cstr(&sb, info.is_link_like ? "LINK" : (info.is_dir ? "DIR" : "FILE"));
    nob_sb_append_cstr(&sb, "\n");

    if (!test_manifest_collect_tree_entries(arena, abs_path, "", &entries)) {
        nob_sb_free(sb);
        return false;
    }
    if (arena_arr_len(entries) > 1) {
        qsort(entries,
              arena_arr_len(entries),
              sizeof(entries[0]),
              test_tree_entry_compare);
    }

    for (size_t i = 0; i < arena_arr_len(entries); ++i) {
        nob_sb_append_cstr(&sb, test_tree_entry_kind_name(entries[i].kind));
        nob_sb_append_cstr(&sb, " ");
        nob_sb_append_buf(&sb,
                          entries[i].relpath.data ? entries[i].relpath.data : "",
                          entries[i].relpath.count);
        nob_sb_append_cstr(&sb, "\n");
    }

    copy = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_cstr(copy);
    return true;
}

bool test_manifest_capture(Arena *arena,
                           const char *base_dir,
                           const Test_Manifest_Request *requests,
                           size_t request_count,
                           String_View *out) {
    Nob_String_Builder sb = {0};
    char abs_path[_TINYDIR_PATH_MAX] = {0};
    char *copy = NULL;
    if (!arena || !base_dir || !requests || !out) return false;
    *out = nob_sv_from_cstr("");

    for (size_t i = 0; i < request_count; ++i) {
        const Test_Manifest_Request *request = &requests[i];
        String_View section = {0};
        const char *relpath = request->relpath ? request->relpath : "";

        if (relpath[0] == '\0') {
            if (snprintf(abs_path, sizeof(abs_path), "%s", base_dir) >= (int)sizeof(abs_path)) {
                nob_sb_free(sb);
                return false;
            }
        } else if (!test_fs_join_path(base_dir, relpath, abs_path)) {
            nob_sb_free(sb);
            return false;
        }

        if (!test_manifest_append_header(&sb, request)) {
            nob_sb_free(sb);
            return false;
        }

        switch (request->capture) {
            case TEST_MANIFEST_CAPTURE_TREE:
                if (!test_manifest_capture_tree(arena, abs_path, &section)) {
                    nob_sb_free(sb);
                    return false;
                }
                nob_sb_append_buf(&sb, section.data ? section.data : "", section.count);
                break;

            case TEST_MANIFEST_CAPTURE_FILE_TEXT:
                if (!test_manifest_append_file_text(arena, &sb, abs_path)) {
                    nob_sb_free(sb);
                    return false;
                }
                break;

            case TEST_MANIFEST_CAPTURE_FILE_SHA256:
                if (!test_manifest_append_file_sha256(arena, &sb, abs_path)) {
                    nob_sb_free(sb);
                    return false;
                }
                break;
        }

        nob_sb_append_cstr(&sb, "END SECTION\n");
        if (i + 1 < request_count) nob_sb_append_cstr(&sb, "\n");
    }

    copy = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_cstr(copy);
    return true;
}

bool test_manifest_assert_equal(Arena *arena,
                                const char *subject,
                                const char *expected_label,
                                const char *actual_label,
                                String_View expected,
                                String_View actual) {
    String_View expected_norm = {0};
    String_View actual_norm = {0};
    if (!arena || !subject) return false;
    expected_norm = test_snapshot_normalize_newlines_to_arena(arena, expected);
    actual_norm = test_snapshot_normalize_newlines_to_arena(arena, actual);
    if (nob_sv_eq(expected_norm, actual_norm)) return true;
    nob_log(NOB_ERROR, "manifest mismatch for %s", subject);
    nob_log(NOB_ERROR,
            "--- %s ---\n%.*s",
            expected_label ? expected_label : "expected",
            (int)expected_norm.count,
            expected_norm.data ? expected_norm.data : "");
    nob_log(NOB_ERROR,
            "--- %s ---\n%.*s",
            actual_label ? actual_label : "actual",
            (int)actual_norm.count,
            actual_norm.data ? actual_norm.data : "");
    return false;
}
