#include "nob_codegen_internal.h"

static bool cg_path_is_within_or_equal(String_View root_abs, String_View candidate_abs) {
    if (root_abs.count == 0 || candidate_abs.count < root_abs.count) return false;
    if (memcmp(root_abs.data, candidate_abs.data, root_abs.count) != 0) return false;
    if (candidate_abs.count == root_abs.count) return true;
    return candidate_abs.data[root_abs.count] == '/';
}

static bool cg_replay_output_is_clean_safe(CG_Context *ctx, String_View output, bool *out_clean_safe) {
    String_View output_abs = {0};
    if (!ctx || !out_clean_safe) return false;
    *out_clean_safe = false;
    if (output.count == 0) return true;
    if (!cg_absolute_from_cwd(ctx, output, &output_abs)) return false;
    *out_clean_safe = cg_path_is_within_or_equal(ctx->binary_root_abs, output_abs);
    return true;
}

static bool cg_emit_replay_configure_helpers(CG_Context *ctx, Nob_String_Builder *out) {
    String_View state_dir = {0};
    bool needs_apply_mode = false;
    bool needs_write_text = false;
    bool needs_append_text = false;
    bool needs_copy_file = false;
    bool needs_download = false;
    bool needs_tar = false;
    bool needs_sha256 = false;
    bool needs_lock = false;
    if (!ctx || !out) return false;
    if (!cg_rebase_from_binary_root(ctx, nob_sv_from_cstr(".nob/configure"), &state_dir)) return false;

    for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
        BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
        BM_Replay_Opcode opcode = bm_query_replay_action_opcode(ctx->model, id);
        switch (opcode) {
            case BM_REPLAY_OPCODE_FS_WRITE_TEXT:
                needs_apply_mode = true;
                needs_write_text = true;
                break;
            case BM_REPLAY_OPCODE_FS_APPEND_TEXT:
                needs_append_text = true;
                break;
            case BM_REPLAY_OPCODE_FS_COPY_FILE:
                needs_apply_mode = true;
                needs_copy_file = true;
                break;
            case BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL: {
                BM_String_Span argv = bm_query_replay_action_argv(ctx->model, id);
                needs_download = true;
                if (argv.count >= 2 && argv.items[0].count > 0 && argv.items[1].count > 0) {
                    needs_sha256 = true;
                }
                break;
            }
            case BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR:
            case BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR:
                needs_tar = true;
                break;
            case BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE:
            case BM_REPLAY_OPCODE_HOST_LOCK_RELEASE:
                needs_lock = true;
                break;
            case BM_REPLAY_OPCODE_FS_MKDIR:
            case BM_REPLAY_OPCODE_NONE:
                break;
        }
    }

    nob_sb_append_cstr(out,
        "static const char *configure_state_dir(void) {\n"
        "    return ");
    if (!cg_sb_append_c_string(out, state_dir)) return false;
    nob_sb_append_cstr(out,
        ";\n"
        "}\n\n"
        "static const char *configure_state_key(void) {\n"
        "    static char buf[256] = {0};\n"
        "    const char *cfg = (g_build_config && g_build_config[0] != '\\0') ? g_build_config : \"default\";\n"
        "    size_t j = 0;\n"
        "    memset(buf, 0, sizeof(buf));\n"
        "    for (size_t i = 0; cfg[i] != '\\0' && j + 1 < sizeof(buf); ++i) {\n"
        "        unsigned char c = (unsigned char)cfg[i];\n"
        "        if (isalnum(c) || c == '_' || c == '-' || c == '.') buf[j++] = (char)c;\n"
        "        else buf[j++] = '_';\n"
        "    }\n"
        "    if (j == 0) memcpy(buf, \"default\", 8);\n"
        "    return buf;\n"
        "}\n\n"
        "static const char *configure_stamp_path(void) {\n"
        "    return nob_temp_sprintf(\"%s/%s.stamp\", configure_state_dir(), configure_state_key());\n"
        "}\n\n"
        "static bool configure_stamp_is_stale(const char *stamp_path) {\n"
        "    struct stat stamp_st = {0};\n"
        "    struct stat plan_st = {0};\n"
        "    if (!stamp_path || stat(stamp_path, &stamp_st) != 0) return true;\n"
        "    if (stat(__FILE__, &plan_st) != 0) return false;\n"
        "    return plan_st.st_mtime > stamp_st.st_mtime;\n"
        "}\n\n");

    if (needs_apply_mode) {
        nob_sb_append_cstr(out,
            "static bool replay_apply_mode(const char *path, const char *mode_octal) {\n"
        "#if defined(_WIN32)\n"
        "    (void)path;\n"
        "    (void)mode_octal;\n"
        "    return true;\n"
        "#else\n"
        "    char *end = NULL;\n"
        "    long mode = 0;\n"
        "    if (!mode_octal || mode_octal[0] == '\\0') return true;\n"
        "    mode = strtol(mode_octal, &end, 8);\n"
        "    if (!end || *end != '\\0') {\n"
        "        nob_log(NOB_ERROR, \"configure: invalid octal mode '%s' for %s\", mode_octal, path ? path : \"<path>\");\n"
        "        return false;\n"
        "    }\n"
        "    return chmod(path, (mode_t)mode) == 0;\n"
        "#endif\n"
            "}\n\n");
    }

    if (needs_write_text) {
        nob_sb_append_cstr(out,
            "static bool replay_write_text(const char *path, const char *text, const char *mode_octal) {\n"
        "    FILE *fp = NULL;\n"
        "    size_t len = text ? strlen(text) : 0;\n"
        "    if (!path) return false;\n"
        "    if (!ensure_parent_dir(path)) return false;\n"
        "    fp = fopen(path, \"wb\");\n"
        "    if (!fp) {\n"
        "        nob_log(NOB_ERROR, \"configure: failed to open %s: %s\", path, strerror(errno));\n"
        "        return false;\n"
        "    }\n"
        "    if (len > 0 && fwrite(text, 1, len, fp) != len) {\n"
        "        nob_log(NOB_ERROR, \"configure: failed to write %s\", path);\n"
        "        fclose(fp);\n"
        "        return false;\n"
        "    }\n"
        "    fclose(fp);\n"
        "    return replay_apply_mode(path, mode_octal);\n"
            "}\n\n");
    }

    if (needs_append_text) {
        nob_sb_append_cstr(out,
            "static bool replay_append_text(const char *path, const char *text) {\n"
        "    FILE *fp = NULL;\n"
        "    size_t len = text ? strlen(text) : 0;\n"
        "    if (!path) return false;\n"
        "    if (!ensure_parent_dir(path)) return false;\n"
        "    fp = fopen(path, \"ab\");\n"
        "    if (!fp) {\n"
        "        nob_log(NOB_ERROR, \"configure: failed to open %s: %s\", path, strerror(errno));\n"
        "        return false;\n"
        "    }\n"
        "    if (len > 0 && fwrite(text, 1, len, fp) != len) {\n"
        "        nob_log(NOB_ERROR, \"configure: failed to append %s\", path);\n"
        "        fclose(fp);\n"
        "        return false;\n"
        "    }\n"
        "    fclose(fp);\n"
        "    return true;\n"
            "}\n\n");
    }

    if (needs_copy_file) {
        nob_sb_append_cstr(out,
            "static bool replay_copy_file_with_mode(const char *src_path, const char *dst_path, const char *mode_octal) {\n"
        "    if (!src_path || !dst_path) return false;\n"
        "    if (!ensure_parent_dir(dst_path)) return false;\n"
        "    if (!nob_copy_file(src_path, dst_path)) {\n"
        "        nob_log(NOB_ERROR, \"configure: failed to copy %s -> %s\", src_path, dst_path);\n"
        "        return false;\n"
        "    }\n"
        "    return replay_apply_mode(dst_path, mode_octal);\n"
            "}\n\n");
    }

    if (needs_sha256) {
        nob_sb_append_cstr(out,
            "static unsigned int replay_sha256_rotr(unsigned int value, unsigned int shift) {\n"
            "    return (value >> shift) | (value << (32u - shift));\n"
            "}\n\n"
            "static void replay_sha256_process_block(uint32_t state[8], const unsigned char block[64]) {\n"
            "    static const uint32_t k[64] = {\n"
            "        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,\n"
            "        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,\n"
            "        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,\n"
            "        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,\n"
            "        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,\n"
            "        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,\n"
            "        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,\n"
            "        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u\n"
            "    };\n"
            "    uint32_t w[64] = {0};\n"
            "    for (size_t i = 0; i < 16; ++i) {\n"
            "        w[i] = ((uint32_t)block[i * 4 + 0] << 24) |\n"
            "               ((uint32_t)block[i * 4 + 1] << 16) |\n"
            "               ((uint32_t)block[i * 4 + 2] << 8) |\n"
            "               (uint32_t)block[i * 4 + 3];\n"
            "    }\n"
            "    for (size_t i = 16; i < 64; ++i) {\n"
            "        uint32_t s0 = replay_sha256_rotr(w[i - 15], 7) ^ replay_sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);\n"
            "        uint32_t s1 = replay_sha256_rotr(w[i - 2], 17) ^ replay_sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);\n"
            "        w[i] = w[i - 16] + s0 + w[i - 7] + s1;\n"
            "    }\n"
            "    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];\n"
            "    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];\n"
            "    for (size_t i = 0; i < 64; ++i) {\n"
            "        uint32_t S1 = replay_sha256_rotr(e, 6) ^ replay_sha256_rotr(e, 11) ^ replay_sha256_rotr(e, 25);\n"
            "        uint32_t ch = (e & f) ^ ((~e) & g);\n"
            "        uint32_t temp1 = h + S1 + ch + k[i] + w[i];\n"
            "        uint32_t S0 = replay_sha256_rotr(a, 2) ^ replay_sha256_rotr(a, 13) ^ replay_sha256_rotr(a, 22);\n"
            "        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);\n"
            "        uint32_t temp2 = S0 + maj;\n"
            "        h = g; g = f; f = e; e = d + temp1;\n"
            "        d = c; c = b; b = a; a = temp1 + temp2;\n"
            "    }\n"
            "    state[0] += a; state[1] += b; state[2] += c; state[3] += d;\n"
            "    state[4] += e; state[5] += f; state[6] += g; state[7] += h;\n"
            "}\n\n"
            "static bool replay_sha256_hex_file(const char *path, char out_hex[65]) {\n"
            "    static const char *digits = \"0123456789abcdef\";\n"
            "    static const uint32_t init[8] = {\n"
            "        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,\n"
            "        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u\n"
            "    };\n"
            "    Nob_String_Builder sb = {0};\n"
            "    uint32_t state[8] = {0};\n"
            "    unsigned char tail[128] = {0};\n"
            "    unsigned char digest[32] = {0};\n"
            "    uint64_t bits = 0;\n"
            "    size_t full_blocks = 0;\n"
            "    size_t rem = 0;\n"
            "    if (!path || !out_hex) return false;\n"
            "    memcpy(state, init, sizeof(state));\n"
            "    if (!nob_read_entire_file(path, &sb)) {\n"
            "        nob_log(NOB_ERROR, \"configure: failed to read %s for SHA256 verification\", path);\n"
            "        return false;\n"
            "    }\n"
            "    bits = (uint64_t)sb.count * 8ull;\n"
            "    full_blocks = sb.count / 64u;\n"
            "    rem = sb.count % 64u;\n"
            "    for (size_t i = 0; i < full_blocks; ++i) replay_sha256_process_block(state, (const unsigned char *)sb.items + i * 64u);\n"
            "    if (rem > 0) memcpy(tail, (const unsigned char *)sb.items + full_blocks * 64u, rem);\n"
            "    tail[rem] = 0x80u;\n"
            "    if (rem + 1u > 56u) {\n"
            "        replay_sha256_process_block(state, tail);\n"
            "        memset(tail, 0, sizeof(tail));\n"
            "    }\n"
            "    tail[56] = (unsigned char)((bits >> 56) & 0xffu);\n"
            "    tail[57] = (unsigned char)((bits >> 48) & 0xffu);\n"
            "    tail[58] = (unsigned char)((bits >> 40) & 0xffu);\n"
            "    tail[59] = (unsigned char)((bits >> 32) & 0xffu);\n"
            "    tail[60] = (unsigned char)((bits >> 24) & 0xffu);\n"
            "    tail[61] = (unsigned char)((bits >> 16) & 0xffu);\n"
            "    tail[62] = (unsigned char)((bits >> 8) & 0xffu);\n"
            "    tail[63] = (unsigned char)(bits & 0xffu);\n"
            "    replay_sha256_process_block(state, tail);\n"
            "    nob_sb_free(sb);\n"
            "    for (size_t i = 0; i < 8; ++i) {\n"
            "        digest[i * 4 + 0] = (unsigned char)((state[i] >> 24) & 0xffu);\n"
            "        digest[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xffu);\n"
            "        digest[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xffu);\n"
            "        digest[i * 4 + 3] = (unsigned char)(state[i] & 0xffu);\n"
            "    }\n"
            "    for (size_t i = 0; i < 32; ++i) {\n"
            "        out_hex[i * 2 + 0] = digits[(digest[i] >> 4) & 0x0fu];\n"
            "        out_hex[i * 2 + 1] = digits[digest[i] & 0x0fu];\n"
            "    }\n"
            "    out_hex[64] = '\\0';\n"
            "    return true;\n"
            "}\n\n");
    }

    if (needs_download) {
        nob_sb_append_cstr(out,
            "static bool replay_download_local(const char *src_path, const char *dst_path, const char *hash_algo, const char *hash_digest) {\n"
        "    if (!src_path || !dst_path) return false;\n"
        "    if (!ensure_parent_dir(dst_path)) return false;\n"
        "    if (!nob_copy_file(src_path, dst_path)) {\n"
        "        nob_log(NOB_ERROR, \"configure: failed to materialize local download %s -> %s\", src_path, dst_path);\n"
        "        return false;\n"
        "    }\n"
        "    if (!hash_algo || hash_algo[0] == '\\0') return true;\n"
        "    if (strcmp(hash_algo, \"SHA256\") != 0) {\n"
        "        nob_log(NOB_ERROR, \"configure: unsupported local download hash algorithm '%s'\", hash_algo);\n"
        "        return false;\n"
        "    }\n");
        if (needs_sha256) {
            nob_sb_append_cstr(out,
                "    {\n"
                "        char actual_hex[65] = {0};\n"
                "        if (!replay_sha256_hex_file(dst_path, actual_hex)) return false;\n"
                "        if (!hash_digest || hash_digest[0] == '\\0' || strcmp(actual_hex, hash_digest) != 0) {\n"
                "            nob_log(NOB_ERROR, \"configure: SHA256 mismatch for %s\", dst_path);\n"
                "            return false;\n"
                "        }\n"
                "    }\n");
        } else {
            nob_sb_append_cstr(out,
                "    (void)hash_digest;\n"
                "    nob_log(NOB_ERROR, \"configure: SHA256 verification helper was not emitted\");\n"
                "    return false;\n");
        }
        nob_sb_append_cstr(out, "    return true;\n}\n\n");
    }

    if (needs_tar) {
        nob_sb_append_cstr(out,
            "static bool replay_archive_create_paxr(const char *archive_path, long long mtime_epoch, const char *const *paths, size_t path_count) {\n"
            "    Nob_Cmd cmd = {0};\n"
            "    if (!archive_path || !paths || path_count == 0) return false;\n"
            "    if (!ensure_parent_dir(archive_path)) return false;\n"
            "    nob_cmd_append(&cmd, resolve_tar_bin(), \"--format=pax\", nob_temp_sprintf(\"--mtime=@%lld\", mtime_epoch), \"-cf\", archive_path);\n"
            "    for (size_t i = 0; i < path_count; ++i) {\n"
            "        const char *parent = nob_temp_dir_name(paths[i]);\n"
            "        const char *name = nob_temp_file_name(paths[i]);\n"
            "        if (!name || name[0] == '\\0') return false;\n"
            "        nob_cmd_append(&cmd, \"-C\", (parent && parent[0] != '\\0') ? parent : \".\", name);\n"
            "    }\n"
            "    if (!nob_cmd_run(&cmd)) {\n"
            "        nob_log(NOB_ERROR, \"configure: failed to create archive %s\", archive_path);\n"
            "        nob_cmd_free(cmd);\n"
            "        return false;\n"
            "    }\n"
            "    nob_cmd_free(cmd);\n"
            "    return true;\n"
            "}\n\n"
            "static bool replay_archive_extract_tar(const char *archive_path, const char *destination) {\n"
            "    Nob_Cmd cmd = {0};\n"
            "    if (!archive_path || !destination) return false;\n"
            "    if (!ensure_dir(destination)) return false;\n"
            "    nob_cmd_append(&cmd, resolve_tar_bin(), \"-xf\", archive_path, \"-C\", destination);\n"
            "    if (!nob_cmd_run(&cmd)) {\n"
            "        nob_log(NOB_ERROR, \"configure: failed to extract archive %s\", archive_path);\n"
            "        nob_cmd_free(cmd);\n"
            "        return false;\n"
            "    }\n"
            "    nob_cmd_free(cmd);\n"
            "    return true;\n"
            "}\n\n");
    }

    if (needs_lock) {
        nob_sb_append_cstr(out,
            "typedef struct {\n"
            "    char *path;\n"
            "#if defined(_WIN32)\n"
            "    void *handle;\n"
            "#else\n"
            "    int fd;\n"
            "#endif\n"
            "} Nob_Replay_Lock;\n\n"
            "static Nob_Replay_Lock g_replay_locks[64] = {0};\n"
            "static size_t g_replay_lock_count = 0;\n\n"
            "static int replay_lock_find(const char *path) {\n"
            "    if (!path) return -1;\n"
            "    for (size_t i = 0; i < g_replay_lock_count; ++i) {\n"
            "        if (g_replay_locks[i].path && strcmp(g_replay_locks[i].path, path) == 0) return (int)i;\n"
            "    }\n"
            "    return -1;\n"
            "}\n\n"
            "static bool replay_lock_acquire(const char *path) {\n"
            "    int existing = 0;\n"
            "    if (!path) return false;\n"
            "    if (!ensure_parent_dir(path)) return false;\n"
            "    existing = replay_lock_find(path);\n"
            "    if (existing >= 0) return true;\n"
            "    if (g_replay_lock_count >= (sizeof(g_replay_locks) / sizeof(g_replay_locks[0]))) {\n"
            "        nob_log(NOB_ERROR, \"configure: too many concurrent replay locks\");\n"
            "        return false;\n"
            "    }\n"
            "#if defined(_WIN32)\n"
            "    return false;\n"
            "#else\n"
            "    {\n"
            "        int fd = open(path, O_RDWR | O_CREAT, 0666);\n"
            "        char *copy = NULL;\n"
            "        if (fd < 0) {\n"
            "            nob_log(NOB_ERROR, \"configure: failed to open lock file %s: %s\", path, strerror(errno));\n"
            "            return false;\n"
            "        }\n"
            "        if (flock(fd, LOCK_EX) != 0) {\n"
            "            nob_log(NOB_ERROR, \"configure: failed to acquire lock %s: %s\", path, strerror(errno));\n"
            "            close(fd);\n"
            "            return false;\n"
            "        }\n"
            "        copy = strdup(path);\n"
            "        if (!copy) {\n"
            "            flock(fd, LOCK_UN);\n"
            "            close(fd);\n"
            "            return false;\n"
            "        }\n"
            "        g_replay_locks[g_replay_lock_count].path = copy;\n"
            "        g_replay_locks[g_replay_lock_count].fd = fd;\n"
            "        g_replay_lock_count++;\n"
            "        return true;\n"
            "    }\n"
            "#endif\n"
            "}\n\n"
            "static bool replay_lock_release(const char *path) {\n"
            "    int idx = replay_lock_find(path);\n"
            "    if (idx < 0) return true;\n"
            "#if defined(_WIN32)\n"
            "    return false;\n"
            "#else\n"
            "    flock(g_replay_locks[idx].fd, LOCK_UN);\n"
            "    close(g_replay_locks[idx].fd);\n"
            "    free(g_replay_locks[idx].path);\n"
            "    for (size_t i = (size_t)idx + 1; i < g_replay_lock_count; ++i) g_replay_locks[i - 1] = g_replay_locks[i];\n"
            "    g_replay_lock_count--;\n"
            "    return true;\n"
            "#endif\n"
            "}\n\n");
    }

    return true;
}

static bool cg_emit_configure_functions(CG_Context *ctx, Nob_String_Builder *out) {
    size_t configure_count = 0;
    bool has_output_guards = false;
    if (!ctx || !out) return false;
    for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
        BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
        if (bm_query_replay_action_phase(ctx->model, id) == BM_REPLAY_PHASE_CONFIGURE) {
            configure_count++;
            if (bm_query_replay_action_outputs(ctx->model, id).count > 0) has_output_guards = true;
        }
    }

    if (!cg_emit_replay_configure_helpers(ctx, out)) return false;

    nob_sb_append_cstr(out,
        "static bool configure_all(bool force) {\n"
        "    const char *stamp_path = configure_stamp_path();\n"
        "    if (!ensure_dir(configure_state_dir())) return false;\n"
        "    if (!force && nob_file_exists(stamp_path) && !configure_stamp_is_stale(stamp_path)) {\n");

    if (has_output_guards) {
        for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
            BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
            if (bm_query_replay_action_phase(ctx->model, id) != BM_REPLAY_PHASE_CONFIGURE) continue;
            BM_String_Span outputs = bm_query_replay_action_outputs(ctx->model, id);
            for (size_t output_index = 0; output_index < outputs.count; ++output_index) {
                String_View path = {0};
                if (!cg_rebase_path_from_cwd(ctx, outputs.items[output_index], &path)) return false;
                nob_sb_append_cstr(out, "        if (!nob_file_exists(");
                if (!cg_sb_append_c_string(out, path)) return false;
                nob_sb_append_cstr(out, ")) goto configure_run;\n");
            }
        }
    }
    nob_sb_append_cstr(out, "        return true;\n    }\n");
    if (has_output_guards) nob_sb_append_cstr(out, "configure_run:\n");

    for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
        BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
        BM_Replay_Opcode opcode = bm_query_replay_action_opcode(ctx->model, id);
        BM_String_Span inputs = {0};
        BM_String_Span outputs = {0};
        BM_String_Span argv = {0};
        if (bm_query_replay_action_phase(ctx->model, id) != BM_REPLAY_PHASE_CONFIGURE) continue;

        inputs = bm_query_replay_action_inputs(ctx->model, id);
        outputs = bm_query_replay_action_outputs(ctx->model, id);
        argv = bm_query_replay_action_argv(ctx->model, id);

        nob_sb_append_cstr(out, "    {\n");
        switch (opcode) {
            case BM_REPLAY_OPCODE_FS_MKDIR:
                for (size_t output_index = 0; output_index < outputs.count; ++output_index) {
                    String_View path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, outputs.items[output_index], &path)) return false;
                    nob_sb_append_cstr(out, "        if (!ensure_dir(");
                    if (!cg_sb_append_c_string(out, path)) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                }
                break;

            case BM_REPLAY_OPCODE_FS_WRITE_TEXT: {
                String_View output_path = {0};
                if (!cg_rebase_path_from_cwd(ctx, outputs.items[0], &output_path)) return false;
                nob_sb_append_cstr(out, "        if (!replay_write_text(");
                if (!cg_sb_append_c_string(out, output_path)) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, argv.items[0])) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, argv.items[1])) return false;
                nob_sb_append_cstr(out, ")) return false;\n");
                break;
            }

            case BM_REPLAY_OPCODE_FS_APPEND_TEXT: {
                String_View output_path = {0};
                if (!cg_rebase_path_from_cwd(ctx, outputs.items[0], &output_path)) return false;
                nob_sb_append_cstr(out, "        if (!replay_append_text(");
                if (!cg_sb_append_c_string(out, output_path)) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, argv.items[0])) return false;
                nob_sb_append_cstr(out, ")) return false;\n");
                break;
            }

            case BM_REPLAY_OPCODE_FS_COPY_FILE: {
                String_View input_path = {0};
                String_View output_path = {0};
                if (!cg_rebase_path_from_cwd(ctx, inputs.items[0], &input_path) ||
                    !cg_rebase_path_from_cwd(ctx, outputs.items[0], &output_path)) {
                    return false;
                }
                nob_sb_append_cstr(out, "        if (!replay_copy_file_with_mode(");
                if (!cg_sb_append_c_string(out, input_path)) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, output_path)) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, argv.items[0])) return false;
                nob_sb_append_cstr(out, ")) return false;\n");
                break;
            }

            case BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL: {
                String_View input_path = {0};
                String_View output_path = {0};
                if (!cg_rebase_path_from_cwd(ctx, inputs.items[0], &input_path) ||
                    !cg_rebase_path_from_cwd(ctx, outputs.items[0], &output_path)) {
                    return false;
                }
                nob_sb_append_cstr(out, "        if (!replay_download_local(");
                if (!cg_sb_append_c_string(out, input_path)) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, output_path)) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, argv.items[0])) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, argv.items[1])) return false;
                nob_sb_append_cstr(out, ")) return false;\n");
                break;
            }

            case BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR:
                nob_sb_append_cstr(out, "        static const char *paths[] = {");
                for (size_t input_index = 0; input_index < inputs.count; ++input_index) {
                    String_View input_path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, inputs.items[input_index], &input_path)) return false;
                    if (input_index > 0) nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, input_path)) return false;
                }
                nob_sb_append_cstr(out, "};\n        if (!replay_archive_create_paxr(");
                {
                    String_View output_path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, outputs.items[0], &output_path)) return false;
                    if (!cg_sb_append_c_string(out, output_path)) return false;
                }
                nob_sb_append_cstr(out, ", ");
                nob_sb_append_cstr(out, "atoll(");
                if (!cg_sb_append_c_string(out, argv.items[0])) return false;
                nob_sb_append_cstr(out, "), paths, sizeof(paths) / sizeof(paths[0]))) return false;\n");
                break;

            case BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR: {
                String_View input_path = {0};
                String_View output_path = {0};
                if (!cg_rebase_path_from_cwd(ctx, inputs.items[0], &input_path) ||
                    !cg_rebase_path_from_cwd(ctx, outputs.items[0], &output_path)) {
                    return false;
                }
                nob_sb_append_cstr(out, "        if (!replay_archive_extract_tar(");
                if (!cg_sb_append_c_string(out, input_path)) return false;
                nob_sb_append_cstr(out, ", ");
                if (!cg_sb_append_c_string(out, output_path)) return false;
                nob_sb_append_cstr(out, ")) return false;\n");
                break;
            }

            case BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE: {
                String_View lock_path = {0};
                if (!cg_rebase_path_from_cwd(ctx, outputs.items[0], &lock_path)) return false;
                nob_sb_append_cstr(out, "        if (!replay_lock_acquire(");
                if (!cg_sb_append_c_string(out, lock_path)) return false;
                nob_sb_append_cstr(out, ")) return false;\n");
                break;
            }

            case BM_REPLAY_OPCODE_HOST_LOCK_RELEASE: {
                String_View lock_path = {0};
                if (!cg_rebase_path_from_cwd(ctx, outputs.items[0], &lock_path)) return false;
                nob_sb_append_cstr(out, "        if (!replay_lock_release(");
                if (!cg_sb_append_c_string(out, lock_path)) return false;
                nob_sb_append_cstr(out, ")) return false;\n");
                break;
            }

            case BM_REPLAY_OPCODE_NONE:
                nob_sb_append_cstr(out, "        nob_log(NOB_ERROR, \"configure: unsupported replay opcode marker encountered during code generation\");\n");
                nob_sb_append_cstr(out, "        return false;\n");
                break;
        }
        nob_sb_append_cstr(out, "    }\n");
    }

    nob_sb_append_cstr(out,
        "    return nob_write_entire_file(stamp_path, \"\", 0);\n"
        "}\n\n"
        "static bool ensure_configured(void) {\n"
        "    return configure_all(false);\n"
        "}\n\n");
    return true;
}
