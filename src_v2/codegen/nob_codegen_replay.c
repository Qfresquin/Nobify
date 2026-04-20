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

static BM_Query_Eval_Context cg_make_replay_eval_ctx(CG_Context *ctx, String_View config) {
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx,
                                                   BM_TARGET_ID_INVALID,
                                                   BM_QUERY_USAGE_COMPILE,
                                                   config,
                                                   nob_sv_from_cstr(""));
    qctx.build_interface_active = true;
    qctx.build_local_interface_active = true;
    qctx.install_interface_active = false;
    return qctx;
}

static bool cg_resolve_replay_string_for_config(CG_Context *ctx,
                                                String_View config,
                                                String_View raw,
                                                String_View *out) {
    BM_Query_Eval_Context qctx = cg_make_replay_eval_ctx(ctx, config);
    return cg_resolve_model_string_with_query_ctx(ctx, &qctx, raw, out);
}

static bool cg_resolve_replay_path_for_config(CG_Context *ctx,
                                              String_View config,
                                              String_View raw,
                                              String_View *out) {
    String_View resolved = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!cg_resolve_replay_string_for_config(ctx, config, raw, &resolved)) return false;
    if (resolved.count == 0) {
        *out = resolved;
        return true;
    }
    return cg_rebase_path_from_cwd(ctx, resolved, out);
}

static bool cg_rebase_replay_archive_input_from_render_cwd(CG_Context *ctx,
                                                           String_View resolved_input,
                                                           String_View *out) {
    String_View input_abs = {0};
    if (!ctx || !out) return false;
    if (!cg_absolute_from_cwd(ctx, resolved_input, &input_abs)) return false;
    return cg_relative_path_to_arena(ctx->scratch, ctx->cwd_abs, input_abs, out);
}

static bool cg_resolve_replay_span_for_config(CG_Context *ctx,
                                              String_View config,
                                              BM_String_Span raw_values,
                                              String_View **out_values) {
    if (out_values) *out_values = NULL;
    if (!ctx || !out_values) return false;
    for (size_t i = 0; i < raw_values.count; ++i) {
        String_View resolved = {0};
        if (!cg_resolve_replay_string_for_config(ctx, config, raw_values.items[i], &resolved)) {
            nob_log(NOB_ERROR,
                    "codegen: failed to resolve replay operand for config `%s` at index %zu: `%s`",
                    nob_temp_sv_to_cstr(config),
                    i,
                    nob_temp_sv_to_cstr(raw_values.items[i]));
            return false;
        }
        if (!arena_arr_push(ctx->scratch, *out_values, resolved)) {
            nob_log(NOB_ERROR,
                    "codegen: failed to store replay operand for config `%s` at index %zu",
                    nob_temp_sv_to_cstr(config),
                    i);
            return false;
        }
    }
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
    bool needs_fetchcontent_source_dir = false;
    bool needs_fetchcontent_local_archive = false;
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
            case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR:
                needs_fetchcontent_source_dir = true;
                break;
            case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE: {
                BM_String_Span argv = bm_query_replay_action_argv(ctx->model, id);
                needs_fetchcontent_local_archive = true;
                needs_tar = true;
                if (argv.count >= 3 && argv.items[1].count > 0 && argv.items[2].count > 0) {
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
            case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE:
            case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT:
            case BM_REPLAY_OPCODE_PROBE_TRY_RUN:
            case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY:
            case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL:
            case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF:
            case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF:
            case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST:
            case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP:
            case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL:
            case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL:
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

    if (needs_fetchcontent_source_dir) {
        nob_sb_append_cstr(out,
            "static bool replay_fetchcontent_source_dir(const char *dependency_name, const char *source_dir, const char *binary_dir) {\n"
            "    (void)dependency_name;\n"
            "    if (!source_dir || !binary_dir) return false;\n"
            "    if (!nob_file_exists(source_dir)) {\n"
            "        nob_log(NOB_ERROR, \"configure: FetchContent source directory is missing: %s\", source_dir);\n"
            "        return false;\n"
            "    }\n"
            "    return ensure_dir(binary_dir);\n"
            "}\n\n");
    }

    if (needs_fetchcontent_local_archive && needs_tar) {
        nob_sb_append_cstr(out,
            "static bool replay_archive_extract_tar(const char *archive_path, const char *destination);\n\n");
    }

    if (needs_fetchcontent_local_archive) {
        nob_sb_append_cstr(out,
            "static bool replay_fetchcontent_local_archive(const char *dependency_name,\n"
            "                                               const char *archive_path,\n"
            "                                               const char *source_dir,\n"
            "                                               const char *binary_dir,\n"
            "                                               const char *hash_algo,\n"
            "                                               const char *hash_digest,\n"
            "                                               const char *preserve_timestamps) {\n"
            "    (void)dependency_name;\n"
            "    (void)preserve_timestamps;\n"
            "    if (!archive_path || !source_dir || !binary_dir) return false;\n"
            "    if (hash_algo && hash_algo[0] != '\\0') {\n"
            "        if (strcmp(hash_algo, \"SHA256\") != 0) {\n"
            "            nob_log(NOB_ERROR, \"configure: unsupported FetchContent archive hash algorithm '%s'\", hash_algo);\n"
            "            return false;\n"
            "        }\n");
        if (needs_sha256) {
            nob_sb_append_cstr(out,
                "        {\n"
                "            char actual_hex[65] = {0};\n"
                "            if (!replay_sha256_hex_file(archive_path, actual_hex)) return false;\n"
                "            if (!hash_digest || hash_digest[0] == '\\0' || strcmp(actual_hex, hash_digest) != 0) {\n"
                "                nob_log(NOB_ERROR, \"configure: FetchContent archive SHA256 mismatch for %s\", archive_path);\n"
                "                return false;\n"
                "            }\n"
                "        }\n");
        } else {
            nob_sb_append_cstr(out,
                "        (void)hash_digest;\n"
                "        nob_log(NOB_ERROR, \"configure: SHA256 verification helper was not emitted\");\n"
                "        return false;\n");
        }
        nob_sb_append_cstr(out,
            "    }\n"
            "    if (!ensure_dir(source_dir)) return false;\n"
            "    if (!replay_archive_extract_tar(archive_path, source_dir)) return false;\n"
            "    return ensure_dir(binary_dir);\n"
            "}\n\n");
    }

    if (needs_tar) {
        nob_sb_append_cstr(out,
            "static size_t replay_path_trimmed_len(const char *path) {\n"
            "    size_t len = path ? strlen(path) : 0u;\n"
            "    while (len > 1u && (path[len - 1] == '/' || path[len - 1] == '\\\\')) --len;\n"
            "    return len;\n"
            "}\n\n"
            "static bool replay_archive_append_input(Nob_Cmd *cmd, const char *path) {\n"
            "    size_t len = 0u;\n"
            "    if (!cmd || !path || path[0] == '\\0') return false;\n"
            "    len = replay_path_trimmed_len(path);\n"
            "    if (len == 0u) return false;\n"
            "    nob_cmd_append(cmd, nob_temp_sprintf(\"%.*s\", (int)len, path));\n"
            "    return true;\n"
            "}\n\n"
            "static bool replay_archive_create_paxr(const char *archive_path, const char *input_cwd, long long mtime_epoch, const char *const *paths, size_t path_count) {\n"
            "    Nob_Cmd cmd = {0};\n"
            "    if (!archive_path || !paths || path_count == 0) return false;\n"
            "    if (!ensure_parent_dir(archive_path)) return false;\n"
            "    nob_cmd_append(&cmd, resolve_tar_bin(), \"--format=pax\", \"--pax-option=delete=atime,delete=ctime\", nob_temp_sprintf(\"--mtime=@%lld\", mtime_epoch), \"-cf\", archive_path);\n"
            "    nob_cmd_append(&cmd, \"-C\", (input_cwd && input_cwd[0] != '\\0') ? input_cwd : \".\", \"--\");\n"
            "    for (size_t i = 0; i < path_count; ++i) {\n"
            "        if (!replay_archive_append_input(&cmd, paths[i])) {\n"
            "            nob_log(NOB_ERROR, \"configure: invalid archive input path\");\n"
            "            nob_cmd_free(cmd);\n"
            "            return false;\n"
            "        }\n"
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
        "static bool clean_all(void);\n\n"
        "static bool configure_all(bool force) {\n"
        "    const char *stamp_path = configure_stamp_path();\n"
        "    if (!ensure_dir(configure_state_dir())) return false;\n"
        "    if (!force && nob_file_exists(stamp_path) && !configure_stamp_is_stale(stamp_path)) {\n");

    if (has_output_guards) {
        for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
            String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
            if (!cg_emit_runtime_config_branches_prefix(ctx, out, branch)) return false;
            for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
                BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
                if (bm_query_replay_action_phase(ctx->model, id) != BM_REPLAY_PHASE_CONFIGURE) continue;
                BM_String_Span outputs = bm_query_replay_action_outputs(ctx->model, id);
                for (size_t output_index = 0; output_index < outputs.count; ++output_index) {
                    String_View path = {0};
                    if (!cg_resolve_replay_path_for_config(ctx, config, outputs.items[output_index], &path)) return false;
                    if (path.count == 0) continue;
                    nob_sb_append_cstr(out, "        if (!nob_file_exists(");
                    if (!cg_sb_append_c_string(out, path)) return false;
                    nob_sb_append_cstr(out, ")) goto configure_run;\n");
                }
            }
        }
        if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
    }
    nob_sb_append_cstr(out, "        return true;\n    }\n");
    if (has_output_guards) nob_sb_append_cstr(out, "configure_run:\n");
    nob_sb_append_cstr(out,
        "    if (!clean_all()) return false;\n"
        "    if (!ensure_dir(configure_state_dir())) return false;\n");

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
        for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
            String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
            String_View *resolved_inputs = NULL;
            String_View *resolved_outputs = NULL;
            String_View *resolved_argv = NULL;
            if (!cg_resolve_replay_span_for_config(ctx, config, inputs, &resolved_inputs) ||
                !cg_resolve_replay_span_for_config(ctx, config, outputs, &resolved_outputs) ||
                !cg_resolve_replay_span_for_config(ctx, config, argv, &resolved_argv) ||
                !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
                return false;
            }
            switch (opcode) {
                case BM_REPLAY_OPCODE_FS_MKDIR:
                    for (size_t output_index = 0; output_index < outputs.count; ++output_index) {
                        String_View path = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[output_index], &path)) return false;
                        nob_sb_append_cstr(out, "        if (!ensure_dir(");
                        if (!cg_sb_append_c_string(out, path)) return false;
                        nob_sb_append_cstr(out, ")) return false;\n");
                    }
                    break;

                case BM_REPLAY_OPCODE_FS_WRITE_TEXT: {
                    String_View output_path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &output_path)) return false;
                    nob_sb_append_cstr(out, "        if (!replay_write_text(");
                    if (!cg_sb_append_c_string(out, output_path)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[1])) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                    break;
                }

                case BM_REPLAY_OPCODE_FS_APPEND_TEXT: {
                    String_View output_path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &output_path)) return false;
                    nob_sb_append_cstr(out, "        if (!replay_append_text(");
                    if (!cg_sb_append_c_string(out, output_path)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                    break;
                }

                case BM_REPLAY_OPCODE_FS_COPY_FILE: {
                    String_View input_path = {0};
                    String_View output_path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, resolved_inputs[0], &input_path) ||
                        !cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &output_path)) {
                        return false;
                    }
                    nob_sb_append_cstr(out, "        if (!replay_copy_file_with_mode(");
                    if (!cg_sb_append_c_string(out, input_path)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, output_path)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                    break;
                }

                case BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL: {
                    String_View input_path = {0};
                    String_View output_path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, resolved_inputs[0], &input_path) ||
                        !cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &output_path)) {
                        return false;
                    }
                    nob_sb_append_cstr(out, "        if (!replay_download_local(");
                    if (!cg_sb_append_c_string(out, input_path)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, output_path)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[1])) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                    break;
                }

                case BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR:
                    nob_sb_append_cstr(out, "        static const char *paths[] = {");
                    for (size_t input_index = 0; input_index < inputs.count; ++input_index) {
                        String_View input_path = {0};
                        if (!cg_rebase_replay_archive_input_from_render_cwd(ctx,
                                                                            resolved_inputs[input_index],
                                                                            &input_path)) {
                            return false;
                        }
                        if (input_index > 0) nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, input_path)) return false;
                    }
                    nob_sb_append_cstr(out, "};\n        if (!replay_archive_create_paxr(");
                    {
                        String_View input_cwd = {0};
                        String_View output_path = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &output_path) ||
                            !cg_rebase_path_from_cwd(ctx, ctx->cwd_abs, &input_cwd)) {
                            return false;
                        }
                        if (!cg_sb_append_c_string(out, output_path)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, input_cwd)) return false;
                    }
                    nob_sb_append_cstr(out, ", ");
                    nob_sb_append_cstr(out, "atoll(");
                    if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                    nob_sb_append_cstr(out, "), paths, sizeof(paths) / sizeof(paths[0]))) return false;\n");
                    break;

                case BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR: {
                    String_View input_path = {0};
                    String_View output_path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, resolved_inputs[0], &input_path) ||
                        !cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &output_path)) {
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
                    if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &lock_path)) return false;
                    nob_sb_append_cstr(out, "        if (!replay_lock_acquire(");
                    if (!cg_sb_append_c_string(out, lock_path)) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                    break;
                }

                case BM_REPLAY_OPCODE_HOST_LOCK_RELEASE: {
                    String_View lock_path = {0};
                    if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &lock_path)) return false;
                    nob_sb_append_cstr(out, "        if (!replay_lock_release(");
                    if (!cg_sb_append_c_string(out, lock_path)) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                    break;
                }

                case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR: {
                    String_View source_dir = {0};
                    String_View binary_dir = {0};
                    if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &source_dir) ||
                        !cg_rebase_path_from_cwd(ctx, resolved_outputs[1], &binary_dir)) {
                        return false;
                    }
                    nob_sb_append_cstr(out, "        if (!replay_fetchcontent_source_dir(");
                    if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, source_dir)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, binary_dir)) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                    break;
                }

                case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE: {
                    String_View archive_path = {0};
                    String_View source_dir = {0};
                    String_View binary_dir = {0};
                    if (!cg_rebase_path_from_cwd(ctx, resolved_inputs[0], &archive_path) ||
                        !cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &source_dir) ||
                        !cg_rebase_path_from_cwd(ctx, resolved_outputs[1], &binary_dir)) {
                        return false;
                    }
                    nob_sb_append_cstr(out, "        if (!replay_fetchcontent_local_archive(");
                    if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, archive_path)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, source_dir)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, binary_dir)) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[1])) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[2])) return false;
                    nob_sb_append_cstr(out, ", ");
                    if (!cg_sb_append_c_string(out, resolved_argv[3])) return false;
                    nob_sb_append_cstr(out, ")) return false;\n");
                    break;
                }

                case BM_REPLAY_OPCODE_NONE:
                    nob_sb_append_cstr(out, "        nob_log(NOB_ERROR, \"configure: unsupported replay opcode marker encountered during code generation\");\n");
                    nob_sb_append_cstr(out, "        return false;\n");
                    break;

                case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE:
                case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT:
                case BM_REPLAY_OPCODE_PROBE_TRY_RUN:
                case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY:
                case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL:
                case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF:
                case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF:
                case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST:
                case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP:
                case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL:
                case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL:
                    nob_sb_append_cstr(out, "        nob_log(NOB_ERROR, \"configure: non-configure replay opcode reached configure_all()\");\n");
                    nob_sb_append_cstr(out, "        return false;\n");
                    break;
            }
        }
        if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
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

static bool cg_emit_test_functions(CG_Context *ctx, Nob_String_Builder *out) {
    size_t test_count = 0;
    size_t test_driver_count = 0;
    size_t test_replay_count = 0;
    String_View generated_source_dir = {0};
    String_View generated_binary_dir = {0};
    if (!ctx || !out) return false;

    test_count = bm_query_test_count(ctx->model);
    if (!cg_rebase_path_from_cwd(ctx, ctx->source_root_abs, &generated_source_dir) ||
        !cg_rebase_path_from_cwd(ctx, ctx->binary_root_abs, &generated_binary_dir)) {
        nob_log(NOB_ERROR,
                "codegen: failed to rebase generated test roots source=`%s` binary=`%s`",
                nob_temp_sv_to_cstr(ctx->source_root_abs),
                nob_temp_sv_to_cstr(ctx->binary_root_abs));
        return false;
    }
    for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
        BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
        if (bm_query_replay_action_phase(ctx->model, id) != BM_REPLAY_PHASE_TEST) continue;
        test_replay_count++;
        if (bm_query_replay_action_kind(ctx->model, id) == BM_REPLAY_ACTION_TEST_DRIVER) {
            test_driver_count++;
        }
    }

    nob_sb_append_cstr(out,
        "typedef struct {\n"
        "    char **items;\n"
        "    size_t count;\n"
        "    size_t capacity;\n"
        "} Nob_Test_String_List;\n\n"
        "typedef struct {\n"
        "    const char *name;\n"
        "    const char *command;\n"
        "    const char *command_base_dir;\n"
        "    const char *working_dir;\n"
        "    bool command_expand_lists;\n"
        "    const char *const *configurations;\n"
        "    size_t configuration_count;\n"
        "} Nob_Generated_Test_Case;\n\n"
        "typedef struct {\n"
        "    const char *name;\n"
        "    bool passed;\n"
        "    int exit_code;\n"
        "} Nob_Generated_Test_Result;\n\n"
        "typedef struct {\n"
        "    const char *name;\n"
        "    char *command;\n"
        "    char *backend_command;\n"
        "    const char *working_dir;\n"
        "    char *stdout_text;\n"
        "    char *stderr_text;\n"
        "    size_t defect_count;\n"
        "    int exit_code;\n"
        "    bool passed;\n"
        "} Nob_Generated_Memcheck_Result;\n\n"
        "static char *test_strdup_n(const char *src, size_t len) {\n"
        "    char *copy = (char *)malloc(len + 1u);\n"
        "    if (!copy) return NULL;\n"
        "    if (len > 0 && src) memcpy(copy, src, len);\n"
        "    copy[len] = '\\0';\n"
        "    return copy;\n"
        "}\n\n"
        "static bool test_path_is_absolute(const char *path) {\n"
        "    if (!path || path[0] == '\\0') return false;\n"
        "    if (path[0] == '/' || path[0] == '\\\\') return true;\n"
        "    return strlen(path) >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\\\');\n"
        "}\n\n"
        "static const char *test_path_basename(const char *path) {\n"
        "    const char *last_slash = NULL;\n"
        "    const char *last_backslash = NULL;\n"
        "    if (!path) return \"\";\n"
        "    last_slash = strrchr(path, '/');\n"
        "    last_backslash = strrchr(path, '\\\\');\n"
        "    if (last_slash && last_backslash) return (last_slash > last_backslash ? last_slash : last_backslash) + 1;\n"
        "    if (last_slash) return last_slash + 1;\n"
        "    if (last_backslash) return last_backslash + 1;\n"
        "    return path;\n"
        "}\n\n"
        "static bool test_token_looks_like_path(const char *token) {\n"
        "    if (!token || token[0] == '\\0' || test_path_is_absolute(token)) return false;\n"
        "    if (token[0] == '.') return true;\n"
        "    return strchr(token, '/') != NULL || strchr(token, '\\\\') != NULL;\n"
        "}\n\n"
        "static bool test_command_uses_script_launcher(const char *token) {\n"
        "    const char *base = test_path_basename(token);\n"
        "    if (!base || base[0] == '\\0') return false;\n"
        "    return strcmp(base, \"sh\") == 0 ||\n"
        "           strcmp(base, \"bash\") == 0 ||\n"
        "           strcmp(base, \"dash\") == 0 ||\n"
        "           strcmp(base, \"zsh\") == 0 ||\n"
        "           strcmp(base, \"python\") == 0 ||\n"
        "           strcmp(base, \"python3\") == 0 ||\n"
        "           strcmp(base, \"python.exe\") == 0 ||\n"
        "           strcmp(base, \"perl\") == 0 ||\n"
        "           strcmp(base, \"ruby\") == 0 ||\n"
        "           strcmp(base, \"node\") == 0 ||\n"
        "           strcmp(base, \"cmd\") == 0 ||\n"
        "           strcmp(base, \"cmd.exe\") == 0 ||\n"
        "           strcmp(base, \"powershell\") == 0 ||\n"
        "           strcmp(base, \"pwsh\") == 0;\n"
        "}\n\n"
        "static char *test_resolve_token_from_base(const char *base_dir, const char *token) {\n"
        "    const char *candidate = NULL;\n"
        "    const char *resolved = NULL;\n"
        "    const char *cwd = NULL;\n"
        "    if (!base_dir || !token || !test_token_looks_like_path(token)) return NULL;\n"
        "    candidate = nob_temp_sprintf(\"%s/%s\", base_dir, token);\n"
        "    if (!candidate || !nob_file_exists(candidate)) return NULL;\n"
        "    resolved = candidate;\n"
        "    if (!test_path_is_absolute(candidate)) {\n"
        "        cwd = nob_get_current_dir_temp();\n"
        "        if (!cwd) return NULL;\n"
        "        resolved = nob_temp_sprintf(\"%s/%s\", cwd, candidate);\n"
        "    }\n"
        "    return test_strdup_n(resolved, strlen(resolved));\n"
        "}\n\n"
        "static bool test_replace_token_from_base(Nob_Test_String_List *argv,\n"
        "                                        size_t index,\n"
        "                                        const char *base_dir) {\n"
        "    char *replacement = NULL;\n"
        "    if (!argv || index >= argv->count || !base_dir || base_dir[0] == '\\0') return true;\n"
        "    replacement = test_resolve_token_from_base(base_dir, argv->items[index]);\n"
        "    if (!replacement) return true;\n"
        "    free(argv->items[index]);\n"
        "    argv->items[index] = replacement;\n"
        "    return true;\n"
        "}\n\n"
        "static bool test_normalize_command_tokens(Nob_Test_String_List *argv,\n"
        "                                         const char *base_dir) {\n"
        "    if (!argv || !base_dir || base_dir[0] == '\\0') return true;\n"
        "    if (!test_replace_token_from_base(argv, 0u, base_dir)) return false;\n"
        "    if (argv->count > 1u && test_command_uses_script_launcher(argv->items[0])) {\n"
        "        if (!test_replace_token_from_base(argv, 1u, base_dir)) return false;\n"
        "    }\n"
        "    return true;\n"
        "}\n\n"
        "static bool test_string_list_push(Nob_Test_String_List *list, const char *src, size_t len) {\n"
        "    char *copy = NULL;\n"
        "    char **grown = NULL;\n"
        "    size_t new_capacity = 0;\n"
        "    if (!list) return false;\n"
        "    if (list->count == list->capacity) {\n"
        "        new_capacity = list->capacity > 0 ? list->capacity * 2u : 8u;\n"
        "        grown = (char **)realloc(list->items, new_capacity * sizeof(list->items[0]));\n"
        "        if (!grown) return false;\n"
        "        list->items = grown;\n"
        "        list->capacity = new_capacity;\n"
        "    }\n"
        "    copy = test_strdup_n(src, len);\n"
        "    if (!copy) return false;\n"
        "    list->items[list->count++] = copy;\n"
        "    return true;\n"
        "}\n\n"
        "static void test_string_list_free(Nob_Test_String_List *list) {\n"
        "    if (!list) return;\n"
        "    for (size_t i = 0; i < list->count; ++i) free(list->items[i]);\n"
        "    free(list->items);\n"
        "    list->items = NULL;\n"
        "    list->count = 0;\n"
        "    list->capacity = 0;\n"
        "}\n\n"
        "static bool test_parse_command_tokens(const char *raw, bool expand_lists, Nob_Test_String_List *out) {\n"
        "    Nob_String_Builder token = {0};\n"
        "    bool in_single = false;\n"
        "    bool in_double = false;\n"
        "    bool escape = false;\n"
        "    bool token_started = false;\n"
        "    if (!out) return false;\n"
        "    if (!raw || raw[0] == '\\0') return true;\n"
        "    for (size_t i = 0; raw[i] != '\\0'; ++i) {\n"
        "        unsigned char ch = (unsigned char)raw[i];\n"
        "        bool separator = !in_single && !in_double && !escape &&\n"
        "            (isspace(ch) || (expand_lists && ch == ';'));\n"
        "        if (separator) {\n"
        "            if (token_started || token.count > 0) {\n"
        "                if (!test_string_list_push(out, token.items ? token.items : \"\", token.count)) {\n"
        "                    nob_sb_free(token);\n"
        "                    return false;\n"
        "                }\n"
        "                token.count = 0;\n"
        "                token_started = false;\n"
        "                if (token.items) token.items[0] = '\\0';\n"
        "            }\n"
        "            continue;\n"
        "        }\n"
        "        token_started = true;\n"
        "        if (escape) {\n"
        "            nob_sb_append(&token, (char)ch);\n"
        "            escape = false;\n"
        "            continue;\n"
        "        }\n"
        "        if (!in_single && ch == '\\\\') {\n"
        "            escape = true;\n"
        "            continue;\n"
        "        }\n"
        "        if (!in_double && ch == '\\'') {\n"
        "            in_single = !in_single;\n"
        "            continue;\n"
        "        }\n"
        "        if (!in_single && ch == '\"') {\n"
        "            in_double = !in_double;\n"
        "            continue;\n"
        "        }\n"
        "        nob_sb_append(&token, (char)ch);\n"
        "    }\n"
        "    if (escape) nob_sb_append(&token, '\\\\');\n"
        "    if (token_started || token.count > 0) {\n"
        "        if (!test_string_list_push(out, token.items ? token.items : \"\", token.count)) {\n"
        "            nob_sb_free(token);\n"
        "            return false;\n"
        "        }\n"
        "    }\n"
        "    nob_sb_free(token);\n"
        "    return true;\n"
        "}\n\n"
        "static bool test_eq_ci(const char *lhs, const char *rhs) {\n"
        "    if (!lhs || !rhs) return false;\n"
        "    while (*lhs && *rhs) {\n"
        "        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) return false;\n"
        "        ++lhs;\n"
        "        ++rhs;\n"
        "    }\n"
        "    return *lhs == '\\0' && *rhs == '\\0';\n"
        "}\n\n"
        "static bool test_name_selected(const char *name, const char *const *selected_names, size_t selected_count) {\n"
        "    if (!name) return false;\n"
        "    if (!selected_names || selected_count == 0) return true;\n"
        "    for (size_t i = 0; i < selected_count; ++i) {\n"
        "        if (selected_names[i] && strcmp(name, selected_names[i]) == 0) return true;\n"
        "    }\n"
        "    return false;\n"
        "}\n\n"
        "static bool test_config_selected(const Nob_Generated_Test_Case *test_case, const char *config_filter) {\n"
        "    if (!test_case) return false;\n"
        "    if (!config_filter || config_filter[0] == '\\0' || test_case->configuration_count == 0) return true;\n"
        "    for (size_t i = 0; i < test_case->configuration_count; ++i) {\n"
        "        if (test_case->configurations[i] && test_eq_ci(test_case->configurations[i], config_filter)) return true;\n"
        "    }\n"
        "    return false;\n"
        "}\n\n"
        "static unsigned long long test_name_hash(const char *text) {\n"
        "    unsigned long long hash = 1469598103934665603ull;\n"
        "    if (!text) return hash;\n"
        "    while (*text) {\n"
        "        hash ^= (unsigned long long)(unsigned char)*text++;\n"
        "        hash *= 1099511628211ull;\n"
        "    }\n"
        "    return hash;\n"
        "}\n\n"
        "static void test_sort_indices_deterministic(size_t *indices, size_t count, const Nob_Generated_Test_Case *cases) {\n"
        "    if (!indices || !cases) return;\n"
        "    for (size_t i = 0; i < count; ++i) {\n"
        "        size_t best = i;\n"
        "        unsigned long long best_hash = test_name_hash(cases[indices[i]].name);\n"
        "        for (size_t j = i + 1; j < count; ++j) {\n"
        "            unsigned long long candidate = test_name_hash(cases[indices[j]].name);\n"
        "            if (candidate < best_hash) {\n"
        "                best = j;\n"
        "                best_hash = candidate;\n"
        "            }\n"
        "        }\n"
        "        if (best != i) {\n"
        "            size_t tmp = indices[i];\n"
        "            indices[i] = indices[best];\n"
        "            indices[best] = tmp;\n"
        "        }\n"
        "    }\n"
        "}\n\n"
        "static bool test_write_buffer_file(const char *path, const char *data, size_t len) {\n"
        "    if (!path) return false;\n"
        "    if (!ensure_parent_dir(path)) return false;\n"
        "    return nob_write_entire_file(path, data ? data : \"\", len);\n"
        "}\n\n"
        "static bool test_write_text_file(const char *path, const char *text) {\n"
        "    return test_write_buffer_file(path, text ? text : \"\", text ? strlen(text) : 0u);\n"
        "}\n\n"
        "static char *test_string_list_join(const Nob_Test_String_List *list, const char *separator) {\n"
        "    Nob_String_Builder sb = {0};\n"
        "    size_t separator_len = separator ? strlen(separator) : 0u;\n"
        "    char *joined = NULL;\n"
        "    if (!list) return NULL;\n"
        "    for (size_t i = 0; i < list->count; ++i) {\n"
        "        if (i > 0 && separator_len > 0) nob_sb_append_buf(&sb, separator, separator_len);\n"
        "        nob_sb_append_cstr(&sb, list->items[i] ? list->items[i] : \"\");\n"
        "    }\n"
        "    joined = test_strdup_n(sb.items ? sb.items : \"\", sb.count);\n"
        "    nob_sb_free(sb);\n"
        "    return joined;\n"
        "}\n\n"
        "static void generated_memcheck_result_clear(Nob_Generated_Memcheck_Result *result) {\n"
        "    if (!result) return;\n"
        "    free(result->command);\n"
        "    free(result->backend_command);\n"
        "    free(result->stdout_text);\n"
        "    free(result->stderr_text);\n"
        "    memset(result, 0, sizeof(*result));\n"
        "}\n\n"
        "static const char *test_bool_text(bool value) {\n"
        "    return value ? \"true\" : \"false\";\n"
        "}\n\n"
        "static bool test_parse_size_value(const char *text, size_t *out_value) {\n"
        "    size_t value = 0;\n"
        "    if (!text || !out_value || text[0] == '\\0') return false;\n"
        "    for (size_t i = 0; text[i] != '\\0'; ++i) {\n"
        "        unsigned char ch = (unsigned char)text[i];\n"
        "        if (ch < '0' || ch > '9') return false;\n"
        "        value = value * 10u + (size_t)(ch - '0');\n"
        "    }\n"
        "    *out_value = value;\n"
        "    return true;\n"
        "}\n\n"
        "static bool test_parse_optional_index(const char *text, size_t fallback_value, size_t *out_value) {\n"
        "    if (!out_value) return false;\n"
        "    if (!text || text[0] == '\\0') {\n"
        "        *out_value = fallback_value;\n"
        "        return true;\n"
        "    }\n"
        "    return test_parse_size_value(text, out_value);\n"
        "}\n\n"
        "typedef enum {\n"
        "    TEST_REPEAT_NONE = 0,\n"
        "    TEST_REPEAT_UNTIL_FAIL,\n"
        "    TEST_REPEAT_UNTIL_PASS,\n"
        "    TEST_REPEAT_AFTER_TIMEOUT,\n"
        "} Nob_Generated_Test_Repeat_Mode;\n\n"
        "static bool test_parse_repeat_mode_and_count(const char *text,\n"
        "                                             Nob_Generated_Test_Repeat_Mode *out_mode,\n"
        "                                             size_t *out_count) {\n"
        "    const char *colon = NULL;\n"
        "    size_t count = 0;\n"
        "    if (!out_mode || !out_count) return false;\n"
        "    *out_mode = TEST_REPEAT_NONE;\n"
        "    *out_count = 1u;\n"
        "    if (!text || text[0] == '\\0') return true;\n"
        "    colon = strchr(text, ':');\n"
        "    if (!colon || colon == text || colon[1] == '\\0') return false;\n"
        "    if (!test_parse_size_value(colon + 1, &count) || count == 0) return false;\n"
        "    if ((size_t)(colon - text) == strlen(\"UNTIL_FAIL\") && strncmp(text, \"UNTIL_FAIL\", strlen(\"UNTIL_FAIL\")) == 0) {\n"
        "        *out_mode = TEST_REPEAT_UNTIL_FAIL;\n"
        "    } else if ((size_t)(colon - text) == strlen(\"UNTIL_PASS\") && strncmp(text, \"UNTIL_PASS\", strlen(\"UNTIL_PASS\")) == 0) {\n"
        "        *out_mode = TEST_REPEAT_UNTIL_PASS;\n"
        "    } else if ((size_t)(colon - text) == strlen(\"AFTER_TIMEOUT\") && strncmp(text, \"AFTER_TIMEOUT\", strlen(\"AFTER_TIMEOUT\")) == 0) {\n"
        "        *out_mode = TEST_REPEAT_AFTER_TIMEOUT;\n"
        "    } else {\n"
        "        return false;\n"
        "    }\n"
        "    *out_count = count;\n"
        "    return true;\n"
        "}\n\n"
        "static int test_parse_stop_time_seconds(const char *text, bool *out_ok) {\n"
        "    int values[3] = {0, 0, 0};\n"
        "    int count = 0;\n"
        "    const char *cursor = text;\n"
        "    if (out_ok) *out_ok = false;\n"
        "    if (!text || text[0] == '\\0') {\n"
        "        if (out_ok) *out_ok = true;\n"
        "        return -1;\n"
        "    }\n"
        "    while (*cursor && count < 3) {\n"
        "        char *end = NULL;\n"
        "        long value = strtol(cursor, &end, 10);\n"
        "        if (!end || end == cursor || value < 0 || value > 59) return -1;\n"
        "        values[count++] = (int)value;\n"
        "        if (*end == '\\0') {\n"
        "            cursor = end;\n"
        "            break;\n"
        "        }\n"
        "        if (*end != ':') return -1;\n"
        "        cursor = end + 1;\n"
        "    }\n"
        "    if (*cursor != '\\0' || (count != 2 && count != 3) || values[0] > 23) return -1;\n"
        "    if (out_ok) *out_ok = true;\n"
        "    return values[0] * 3600 + values[1] * 60 + values[2];\n"
        "}\n\n"
        "static int test_current_local_seconds_of_day(void) {\n"
        "    time_t now = time(NULL);\n"
        "    struct tm local_tm = {0};\n"
        "#if defined(_WIN32)\n"
        "    if (localtime_s(&local_tm, &now) != 0) return -1;\n"
        "#else\n"
        "    if (!localtime_r(&now, &local_tm)) return -1;\n"
        "#endif\n"
        "    return local_tm.tm_hour * 3600 + local_tm.tm_min * 60 + local_tm.tm_sec;\n"
        "}\n\n"
        "static size_t test_extract_defect_count_from_text(const char *text) {\n"
        "    size_t total = 0;\n"
        "    bool saw_explicit_summary = false;\n"
        "    const char *cursor = text ? text : \"\";\n"
        "    while (*cursor) {\n"
        "        const char *line_end = cursor;\n"
        "        while (*line_end && *line_end != '\\n' && *line_end != '\\r') ++line_end;\n"
        "        if (line_end > cursor) {\n"
        "            char *line = test_strdup_n(cursor, (size_t)(line_end - cursor));\n"
        "            if (!line) return total;\n"
        "            if (strstr(line, \"ERROR SUMMARY:\") || strstr(line, \"defect count:\") || strstr(line, \"defects=\")) {\n"
        "                char *digits = line;\n"
        "                while (*digits && !isdigit((unsigned char)*digits)) ++digits;\n"
        "                if (*digits) {\n"
        "                    char saved = '\\0';\n"
        "                    char *digit_end = digits;\n"
        "                    size_t parsed = 0;\n"
        "                    while (*digit_end && isdigit((unsigned char)*digit_end)) ++digit_end;\n"
        "                    saved = *digit_end;\n"
        "                    *digit_end = '\\0';\n"
        "                    if (test_parse_size_value(digits, &parsed)) {\n"
        "                        total += parsed;\n"
        "                        saw_explicit_summary = true;\n"
        "                    }\n"
        "                    *digit_end = saved;\n"
        "                }\n"
        "            }\n"
        "            if (!saw_explicit_summary &&\n"
        "                (strstr(line, \"AddressSanitizer\") || strstr(line, \"LeakSanitizer\") ||\n"
        "                 strstr(line, \"UndefinedBehaviorSanitizer\") || strstr(line, \"runtime error:\") ||\n"
        "                 strstr(line, \"Invalid read\") || strstr(line, \"Invalid write\") ||\n"
        "                 strstr(line, \"use-after-free\") || strstr(line, \"definitely lost\"))) {\n"
        "                total++;\n"
        "            }\n"
        "            free(line);\n"
        "        }\n"
        "        while (*line_end == '\\n' || *line_end == '\\r') ++line_end;\n"
        "        cursor = line_end;\n"
        "    }\n"
        "    return total;\n"
        "}\n\n"
        "static size_t test_memcheck_extract_defect_count(const char *stdout_text, const char *stderr_text) {\n"
        "    return test_extract_defect_count_from_text(stdout_text) +\n"
        "           test_extract_defect_count_from_text(stderr_text);\n"
        "}\n\n"
        "static void test_append_xml_escaped(Nob_String_Builder *sb, const char *text) {\n"
        "    if (!sb || !text) return;\n"
        "    while (*text) {\n"
        "        switch (*text) {\n"
        "            case '&': nob_sb_append_cstr(sb, \"&amp;\"); break;\n"
        "            case '<': nob_sb_append_cstr(sb, \"&lt;\"); break;\n"
        "            case '>': nob_sb_append_cstr(sb, \"&gt;\"); break;\n"
        "            case '\"': nob_sb_append_cstr(sb, \"&quot;\"); break;\n"
        "            default: nob_sb_append(sb, *text); break;\n"
        "        }\n"
        "        ++text;\n"
        "    }\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_tag_name(void) {\n"
        "    return nob_temp_sprintf(\"C3Local-%s\", configure_state_key());\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_testing_root(const char *build_dir) {\n"
        "    return nob_temp_sprintf(\"%s/Testing\", build_dir ? build_dir : \".\");\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_tag_dir(const char *build_dir) {\n"
        "    return nob_temp_sprintf(\"%s/%s\", ctest_testing_root(build_dir), ctest_tag_name());\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_tag_file(const char *build_dir) {\n"
        "    return nob_temp_sprintf(\"%s/TAG\", ctest_testing_root(build_dir));\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_prepare_tree(const char *build_dir, bool append_mode) {\n"
        "    const char *testing_root = ctest_testing_root(build_dir);\n"
        "    const char *tag_dir = ctest_tag_dir(build_dir);\n"
        "    if (!build_dir) return false;\n"
        "    if (!append_mode && nob_file_exists(testing_root) && !remove_path_recursive(testing_root)) return false;\n"
        "    if (!ensure_dir(tag_dir)) return false;\n"
        "    return test_write_text_file(ctest_tag_file(build_dir), nob_temp_sprintf(\"%s\\n\", ctest_tag_name()));\n"
        "}\n\n");

    size_t default_build_target_count = 0;

    for (size_t i = 0; i < ctx->target_count; ++i) {
        const CG_Target_Info *info = &ctx->targets[i];
        if (info->alias || info->imported || info->exclude_from_all || !info->emits_artifact) continue;
        default_build_target_count++;
    }

    for (size_t test_index = 0; test_index < test_count; ++test_index) {
        BM_Test_Id id = (BM_Test_Id)test_index;
        BM_String_Span configs = bm_query_test_configurations(ctx->model, id);
        if (configs.count == 0) continue;
        nob_sb_append_cstr(out, "static const char *const g_test_configs_");
        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", test_index));
        nob_sb_append_cstr(out, "[] = {");
        for (size_t cfg_index = 0; cfg_index < configs.count; ++cfg_index) {
            if (cfg_index > 0) nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, configs.items[cfg_index])) return false;
        }
        nob_sb_append_cstr(out, "};\n");
    }
    if (test_count > 0) nob_sb_append_cstr(out, "\n");

    nob_sb_append_cstr(out,
        "static const Nob_Generated_Test_Case g_generated_tests[] = {\n");
    if (test_count == 0) {
        nob_sb_append_cstr(out, "    {0},\n");
    }
    for (size_t test_index = 0; test_index < test_count; ++test_index) {
        BM_Test_Id id = (BM_Test_Id)test_index;
        BM_Directory_Id owner = bm_query_test_owner_directory(ctx->model, id);
        String_View working_dir = bm_query_test_working_directory(ctx->model, id);
        String_View effective_owner_binary_dir = {0};
        String_View emitted_command_base_dir = {0};
        String_View emitted_working_dir = {0};
        BM_String_Span configs = bm_query_test_configurations(ctx->model, id);
        if (!bm_directory_id_is_valid(owner) ||
            !cg_effective_owner_binary_dir(ctx, owner, &effective_owner_binary_dir)) {
            effective_owner_binary_dir = ctx->binary_root_abs;
        }
        if (!cg_rebase_from_base(ctx,
                                 nob_sv_from_cstr("."),
                                 effective_owner_binary_dir,
                                 &emitted_command_base_dir)) {
            return false;
        }
        if (working_dir.count > 0) {
            if (!cg_rebase_from_base(ctx, working_dir, effective_owner_binary_dir, &emitted_working_dir)) return false;
        } else if (!cg_rebase_from_base(ctx,
                                        nob_sv_from_cstr("."),
                                        effective_owner_binary_dir,
                                        &emitted_working_dir)) {
            return false;
        }
        nob_sb_append_cstr(out, "    {");
        if (!cg_sb_append_c_string(out, bm_query_test_name(ctx->model, id))) return false;
        nob_sb_append_cstr(out, ", ");
        if (!cg_sb_append_c_string(out, bm_query_test_command(ctx->model, id))) return false;
        nob_sb_append_cstr(out, ", ");
        if (!cg_sb_append_c_string(out, emitted_command_base_dir)) return false;
        nob_sb_append_cstr(out, ", ");
        if (!cg_sb_append_c_string(out, emitted_working_dir)) return false;
        nob_sb_append_cstr(out, ", ");
        nob_sb_append_cstr(out, bm_query_test_command_expand_lists(ctx->model, id) ? "true" : "false");
        nob_sb_append_cstr(out, ", ");
        if (configs.count > 0) {
            nob_sb_append_cstr(out, "g_test_configs_");
            nob_sb_append_cstr(out, nob_temp_sprintf("%zu", test_index));
        } else {
            nob_sb_append_cstr(out, "NULL");
        }
        nob_sb_append_cstr(out, ", ");
        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", configs.count));
        nob_sb_append_cstr(out, "},\n");
    }
    nob_sb_append_cstr(out,
        "};\n\n"
        "static size_t generated_test_count(void) {\n"
        "    return ");
    nob_sb_append_cstr(out, nob_temp_sprintf("%zu", test_count));
    nob_sb_append_cstr(out,
        "u;\n"
        "}\n\n"
        "static size_t generated_default_build_target_count(void) {\n"
        "    return ");
    nob_sb_append_cstr(out, nob_temp_sprintf("%zu", default_build_target_count));
    nob_sb_append_cstr(out,
        "u;\n"
        "}\n\n"
        "static bool selected_test_exists(const char *name) {\n"
        "    for (size_t i = 0; i < generated_test_count(); ++i) {\n"
        "        const Nob_Generated_Test_Case *test_case = &g_generated_tests[i];\n"
        "        if (name && strcmp(test_case->name, name) == 0) return true;\n"
        "    }\n"
        "    return false;\n"
        "}\n\n"
        "static const char *resolve_test_target_path(const char *token, bool *out_buildable) {\n"
        "    if (out_buildable) *out_buildable = false;\n"
        "    if (!token) return NULL;\n");

    for (size_t i = 0; i < ctx->target_count; ++i) {
        const CG_Target_Info *info = &ctx->targets[i];
        const CG_Target_Info *resolved = cg_target_info(ctx, info->resolved_id);
        if (!info->name.count || !resolved || resolved->kind != BM_TARGET_EXECUTABLE) continue;
        if (info->imported || !resolved->emits_artifact) continue;
        nob_sb_append_cstr(out, "    if (strcmp(token, ");
        if (!cg_sb_append_c_string(out, info->name)) return false;
        nob_sb_append_cstr(out, ") == 0) {\n");
        nob_sb_append_cstr(out, "        if (out_buildable) *out_buildable = true;\n");
        nob_sb_append_cstr(out, "        return ");
        if (!cg_sb_append_c_string(out, resolved->artifact_path)) return false;
        nob_sb_append_cstr(out, ";\n    }\n");
    }
    nob_sb_append_cstr(out,
        "    return NULL;\n"
        "}\n\n"
        "static bool build_test_target_if_needed(const char *token) {\n"
        "    if (!token) return true;\n");
    for (size_t i = 0; i < ctx->target_count; ++i) {
        const CG_Target_Info *info = &ctx->targets[i];
        const CG_Target_Info *resolved = cg_target_info(ctx, info->resolved_id);
        if (!info->name.count || !resolved || resolved->kind != BM_TARGET_EXECUTABLE) continue;
        if (info->imported || !resolved->emits_artifact) continue;
        nob_sb_append_cstr(out, "    if (strcmp(token, ");
        if (!cg_sb_append_c_string(out, info->name)) return false;
        nob_sb_append_cstr(out, ") == 0) return build_");
        nob_sb_append_cstr(out, info->ident);
        nob_sb_append_cstr(out, "();\n");
    }
    nob_sb_append_cstr(out,
        "    return true;\n"
        "}\n\n"
        "static bool build_selected_test_targets(const char *const *selected_names, size_t selected_count, const char *config_filter) {\n"
        "    for (size_t i = 0; i < generated_test_count(); ++i) {\n"
        "        const Nob_Generated_Test_Case *test_case = &g_generated_tests[i];\n"
        "        Nob_Test_String_List argv = {0};\n"
        "        bool buildable = false;\n"
        "        if (!test_name_selected(test_case->name, selected_names, selected_count) ||\n"
        "            !test_config_selected(test_case, config_filter)) {\n"
        "            continue;\n"
        "        }\n"
        "        if (!test_parse_command_tokens(test_case->command, test_case->command_expand_lists, &argv)) return false;\n"
        "        if (argv.count > 0) {\n"
        "            (void)resolve_test_target_path(argv.items[0], &buildable);\n"
        "            if (buildable && !build_test_target_if_needed(argv.items[0])) {\n"
        "                test_string_list_free(&argv);\n"
        "                return false;\n"
        "            }\n"
        "        }\n"
        "        test_string_list_free(&argv);\n"
        "    }\n"
        "    return true;\n"
        "}\n\n"
        "static bool prepare_test_command_argv(const Nob_Generated_Test_Case *test_case,\n"
        "                                      Nob_Test_String_List *argv,\n"
        "                                      bool auto_build) {\n"
        "    const char *resolved_target_path = NULL;\n"
        "    bool buildable = false;\n"
        "    if (!test_case || !argv) return false;\n"
        "    if (!test_parse_command_tokens(test_case->command, test_case->command_expand_lists, argv)) return false;\n"
        "    if (argv->count == 0) {\n"
        "        nob_log(NOB_ERROR, \"test: empty command for %s\", test_case->name ? test_case->name : \"<unnamed>\");\n"
        "        return false;\n"
        "    }\n"
        "    resolved_target_path = resolve_test_target_path(argv->items[0], &buildable);\n"
        "    if (auto_build && buildable && !build_test_target_if_needed(argv->items[0])) return false;\n"
        "    if (resolved_target_path) {\n"
        "        const char *resolved_command_path = resolved_target_path;\n"
        "        char *replacement = NULL;\n"
        "        if (!test_path_is_absolute(resolved_target_path)) {\n"
        "            const char *cwd = nob_get_current_dir_temp();\n"
        "            if (!cwd) return false;\n"
        "            resolved_command_path = nob_temp_sprintf(\"%s/%s\", cwd, resolved_target_path);\n"
        "        }\n"
        "        replacement = test_strdup_n(resolved_command_path, strlen(resolved_command_path));\n"
        "        if (!replacement) return false;\n"
        "        free(argv->items[0]);\n"
        "        argv->items[0] = replacement;\n"
        "    }\n"
        "    if (!test_normalize_command_tokens(argv, test_case->command_base_dir)) return false;\n"
        "    return true;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_write_test_reports(const char *build_dir,\n"
        "                                                            const Nob_Generated_Test_Result *results,\n"
        "                                                            size_t result_count,\n"
        "                                                            const char *output_junit) {\n"
        "    Nob_String_Builder manifest = {0};\n"
        "    Nob_String_Builder test_xml = {0};\n"
        "    Nob_String_Builder junit = {0};\n"
        "    size_t failed_count = 0;\n"
        "    const char *tag_dir = ctest_tag_dir(build_dir);\n"
        "    if (!ctest_prepare_tree(build_dir, true)) return false;\n"
        "    for (size_t i = 0; i < result_count; ++i) if (!results[i].passed) failed_count++;\n"
        "    nob_sb_append_cstr(&manifest, \"# Generated C3 test manifest\\n\");\n"
        "    nob_sb_append_cstr(&test_xml, \"<Testing>\\n\");\n"
        "    nob_sb_append_cstr(&junit, \"<testsuite tests=\\\"\");\n"
        "    nob_sb_append_cstr(&junit, nob_temp_sprintf(\"%zu\", result_count));\n"
        "    nob_sb_append_cstr(&junit, \"\\\" failures=\\\"\");\n"
        "    nob_sb_append_cstr(&junit, nob_temp_sprintf(\"%zu\", failed_count));\n"
        "    nob_sb_append_cstr(&junit, \"\\\">\\n\");\n"
        "    for (size_t i = 0; i < result_count; ++i) {\n"
        "        nob_sb_append_cstr(&manifest, results[i].name ? results[i].name : \"<unnamed>\");\n"
        "        nob_sb_append_cstr(&manifest, \"\\n\");\n"
        "        nob_sb_append_cstr(&test_xml, \"  <Test Name=\\\"\");\n"
        "        test_append_xml_escaped(&test_xml, results[i].name ? results[i].name : \"\");\n"
        "        nob_sb_append_cstr(&test_xml, \"\\\" Status=\\\"\");\n"
        "        nob_sb_append_cstr(&test_xml, results[i].passed ? \"passed\" : \"failed\");\n"
        "        nob_sb_append_cstr(&test_xml, \"\\\" ExitCode=\\\"\");\n"
        "        nob_sb_append_cstr(&test_xml, nob_temp_sprintf(\"%d\", results[i].exit_code));\n"
        "        nob_sb_append_cstr(&test_xml, \"\\\" />\\n\");\n"
        "        nob_sb_append_cstr(&junit, \"  <testcase name=\\\"\");\n"
        "        test_append_xml_escaped(&junit, results[i].name ? results[i].name : \"\");\n"
        "        nob_sb_append_cstr(&junit, \"\\\"\");\n"
        "        if (!results[i].passed) {\n"
        "            nob_sb_append_cstr(&junit, \"><failure message=\\\"command failed\\\" /></testcase>\\n\");\n"
        "        } else {\n"
        "            nob_sb_append_cstr(&junit, \" />\\n\");\n"
        "        }\n"
        "    }\n"
        "    nob_sb_append_cstr(&test_xml, \"</Testing>\\n\");\n"
        "    nob_sb_append_cstr(&junit, \"</testsuite>\\n\");\n"
        "    if (!test_write_buffer_file(nob_temp_sprintf(\"%s/TestManifest.txt\", tag_dir), manifest.items ? manifest.items : \"\", manifest.count)) {\n"
        "        nob_sb_free(manifest);\n"
        "        nob_sb_free(test_xml);\n"
        "        nob_sb_free(junit);\n"
        "        return false;\n"
        "    }\n"
        "    if (!test_write_buffer_file(nob_temp_sprintf(\"%s/Test.xml\", tag_dir), test_xml.items ? test_xml.items : \"\", test_xml.count)) {\n"
        "        nob_sb_free(manifest);\n"
        "        nob_sb_free(test_xml);\n"
        "        nob_sb_free(junit);\n"
        "        return false;\n"
        "    }\n"
        "    if (output_junit && output_junit[0] != '\\0' && !test_write_buffer_file(output_junit, junit.items ? junit.items : \"\", junit.count)) {\n"
        "        nob_sb_free(manifest);\n"
        "        nob_sb_free(test_xml);\n"
        "        nob_sb_free(junit);\n"
        "        return false;\n"
        "    }\n"
        "    nob_sb_free(manifest);\n"
        "    nob_sb_free(test_xml);\n"
        "    nob_sb_free(junit);\n"
        "    return true;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_write_coverage_reports(const char *build_dir,\n"
        "                                                                const char *model,\n"
        "                                                                const char *track,\n"
        "                                                                bool append_mode,\n"
        "                                                                const char *command_text,\n"
        "                                                                const char *const *source_paths,\n"
        "                                                                size_t source_count,\n"
        "                                                                const char *stdout_text,\n"
        "                                                                const char *stderr_text,\n"
        "                                                                int exit_code) {\n"
        "    Nob_String_Builder manifest = {0};\n"
        "    Nob_String_Builder coverage_xml = {0};\n"
        "    const char *tag_dir = ctest_tag_dir(build_dir);\n"
        "    if (!ctest_prepare_tree(build_dir, append_mode)) return false;\n"
        "    nob_sb_append_cstr(&manifest, \"Coverage.xml\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"<?xml version=\\\"1.0\\\" encoding=\\\"UTF-8\\\"?>\\n<Coverage>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <BuildDirectory>\");\n"
        "    test_append_xml_escaped(&coverage_xml, build_dir ? build_dir : \"\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</BuildDirectory>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <Model>\");\n"
        "    test_append_xml_escaped(&coverage_xml, model ? model : \"\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</Model>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <Track>\");\n"
        "    test_append_xml_escaped(&coverage_xml, track ? track : \"\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</Track>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <Command>\");\n"
        "    test_append_xml_escaped(&coverage_xml, command_text ? command_text : \"\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</Command>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <Append>\");\n"
        "    nob_sb_append_cstr(&coverage_xml, test_bool_text(append_mode));\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</Append>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <FilteredSourceCount>\");\n"
        "    nob_sb_append_cstr(&coverage_xml, nob_temp_sprintf(\"%zu\", source_count));\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</FilteredSourceCount>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <FilteredSources>\\n\");\n"
        "    for (size_t i = 0; i < source_count; ++i) {\n"
        "        nob_sb_append_cstr(&coverage_xml, \"    <Source><Path>\");\n"
        "        test_append_xml_escaped(&coverage_xml, source_paths[i] ? source_paths[i] : \"\");\n"
        "        nob_sb_append_cstr(&coverage_xml, \"</Path></Source>\\n\");\n"
        "    }\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  </FilteredSources>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <ExitCode>\");\n"
        "    nob_sb_append_cstr(&coverage_xml, nob_temp_sprintf(\"%d\", exit_code));\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</ExitCode>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <StdOut>\");\n"
        "    test_append_xml_escaped(&coverage_xml, stdout_text ? stdout_text : \"\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</StdOut>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"  <StdErr>\");\n"
        "    test_append_xml_escaped(&coverage_xml, stderr_text ? stderr_text : \"\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</StdErr>\\n\");\n"
        "    nob_sb_append_cstr(&coverage_xml, \"</Coverage>\\n\");\n"
        "    if (!test_write_buffer_file(nob_temp_sprintf(\"%s/CoverageManifest.txt\", tag_dir), manifest.items ? manifest.items : \"\", manifest.count)) {\n"
        "        nob_sb_free(manifest);\n"
        "        nob_sb_free(coverage_xml);\n"
        "        return false;\n"
        "    }\n"
        "    if (!test_write_buffer_file(nob_temp_sprintf(\"%s/Coverage.xml\", tag_dir), coverage_xml.items ? coverage_xml.items : \"\", coverage_xml.count)) {\n"
        "        nob_sb_free(manifest);\n"
        "        nob_sb_free(coverage_xml);\n"
        "        return false;\n"
        "    }\n"
        "    nob_sb_free(manifest);\n"
        "    nob_sb_free(coverage_xml);\n"
        "    return true;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_write_memcheck_reports(const char *build_dir,\n"
        "                                                                const char *model,\n"
        "                                                                const char *track,\n"
        "                                                                bool append_mode,\n"
        "                                                                const char *backend_type,\n"
        "                                                                const char *resource_spec_file,\n"
        "                                                                const char *suppression_file,\n"
        "                                                                const Nob_Generated_Memcheck_Result *results,\n"
        "                                                                size_t result_count,\n"
        "                                                                size_t total_defects,\n"
        "                                                                size_t failed_count,\n"
        "                                                                const char *output_junit) {\n"
        "    Nob_String_Builder manifest = {0};\n"
        "    Nob_String_Builder memcheck_xml = {0};\n"
        "    Nob_String_Builder junit = {0};\n"
        "    const char *tag_dir = ctest_tag_dir(build_dir);\n"
        "    if (!ctest_prepare_tree(build_dir, append_mode)) return false;\n"
        "    nob_sb_append_cstr(&manifest, \"MemCheck.xml\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"<?xml version=\\\"1.0\\\" encoding=\\\"UTF-8\\\"?>\\n<MemCheck>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <BuildDirectory>\");\n"
        "    test_append_xml_escaped(&memcheck_xml, build_dir ? build_dir : \"\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"</BuildDirectory>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <Model>\");\n"
        "    test_append_xml_escaped(&memcheck_xml, model ? model : \"\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"</Model>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <Track>\");\n"
        "    test_append_xml_escaped(&memcheck_xml, track ? track : \"\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"</Track>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <BackendType>\");\n"
        "    test_append_xml_escaped(&memcheck_xml, backend_type ? backend_type : \"\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"</BackendType>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <ResourceSpecFile>\");\n"
        "    test_append_xml_escaped(&memcheck_xml, resource_spec_file ? resource_spec_file : \"\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"</ResourceSpecFile>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <SuppressionsFile>\");\n"
        "    test_append_xml_escaped(&memcheck_xml, suppression_file ? suppression_file : \"\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"</SuppressionsFile>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <DefectCount>\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, nob_temp_sprintf(\"%zu\", total_defects));\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"</DefectCount>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <FailedTests>\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, nob_temp_sprintf(\"%zu\", failed_count));\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"</FailedTests>\\n\");\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  <Tests>\\n\");\n"
        "    nob_sb_append_cstr(&junit, \"<testsuite name=\\\"ctest_memcheck\\\" tests=\\\"\");\n"
        "    nob_sb_append_cstr(&junit, nob_temp_sprintf(\"%zu\", result_count));\n"
        "    nob_sb_append_cstr(&junit, \"\\\" failures=\\\"\");\n"
        "    nob_sb_append_cstr(&junit, nob_temp_sprintf(\"%zu\", failed_count));\n"
        "    nob_sb_append_cstr(&junit, \"\\\">\\n\");\n"
        "    for (size_t i = 0; i < result_count; ++i) {\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"    <Test Name=\\\"\");\n"
        "        test_append_xml_escaped(&memcheck_xml, results[i].name ? results[i].name : \"\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"\\\" Status=\\\"\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, results[i].passed ? \"passed\" : \"failed\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"\\\">\\n\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"      <Command>\");\n"
        "        test_append_xml_escaped(&memcheck_xml, results[i].command ? results[i].command : \"\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"</Command>\\n\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"      <BackendCommand>\");\n"
        "        test_append_xml_escaped(&memcheck_xml, results[i].backend_command ? results[i].backend_command : \"\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"</BackendCommand>\\n\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"      <WorkingDirectory>\");\n"
        "        test_append_xml_escaped(&memcheck_xml, results[i].working_dir ? results[i].working_dir : \"\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"</WorkingDirectory>\\n\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"      <Defects>\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, nob_temp_sprintf(\"%zu\", results[i].defect_count));\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"</Defects>\\n\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"      <ExitCode>\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, nob_temp_sprintf(\"%d\", results[i].exit_code));\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"</ExitCode>\\n\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"      <StdOut>\");\n"
        "        test_append_xml_escaped(&memcheck_xml, results[i].stdout_text ? results[i].stdout_text : \"\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"</StdOut>\\n\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"      <StdErr>\");\n"
        "        test_append_xml_escaped(&memcheck_xml, results[i].stderr_text ? results[i].stderr_text : \"\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"</StdErr>\\n\");\n"
        "        nob_sb_append_cstr(&memcheck_xml, \"    </Test>\\n\");\n"
        "        nob_sb_append_cstr(&junit, \"  <testcase name=\\\"\");\n"
        "        test_append_xml_escaped(&junit, results[i].name ? results[i].name : \"\");\n"
        "        nob_sb_append_cstr(&junit, \"\\\" classname=\\\"ctest_memcheck\\\"\");\n"
        "        if (results[i].passed) {\n"
        "            nob_sb_append_cstr(&junit, \" />\\n\");\n"
        "        } else {\n"
        "            nob_sb_append_cstr(&junit, \"><failure message=\\\"\");\n"
        "            test_append_xml_escaped(&junit, results[i].backend_command ? results[i].backend_command : \"command failed\");\n"
        "            nob_sb_append_cstr(&junit, \"\\\">\\n\");\n"
        "            test_append_xml_escaped(&junit, results[i].stderr_text ? results[i].stderr_text : \"\");\n"
        "            nob_sb_append_cstr(&junit, \"</failure></testcase>\\n\");\n"
        "        }\n"
        "    }\n"
        "    nob_sb_append_cstr(&memcheck_xml, \"  </Tests>\\n</MemCheck>\\n\");\n"
        "    nob_sb_append_cstr(&junit, \"</testsuite>\\n\");\n"
        "    if (!test_write_buffer_file(nob_temp_sprintf(\"%s/MemCheckManifest.txt\", tag_dir), manifest.items ? manifest.items : \"\", manifest.count)) {\n"
        "        nob_sb_free(manifest);\n"
        "        nob_sb_free(memcheck_xml);\n"
        "        nob_sb_free(junit);\n"
        "        return false;\n"
        "    }\n"
        "    if (!test_write_buffer_file(nob_temp_sprintf(\"%s/MemCheck.xml\", tag_dir), memcheck_xml.items ? memcheck_xml.items : \"\", memcheck_xml.count)) {\n"
        "        nob_sb_free(manifest);\n"
        "        nob_sb_free(memcheck_xml);\n"
        "        nob_sb_free(junit);\n"
        "        return false;\n"
        "    }\n"
        "    if (output_junit && output_junit[0] != '\\0' && !test_write_buffer_file(output_junit, junit.items ? junit.items : \"\", junit.count)) {\n"
        "        nob_sb_free(manifest);\n"
        "        nob_sb_free(memcheck_xml);\n"
        "        nob_sb_free(junit);\n"
        "        return false;\n"
        "    }\n"
        "    nob_sb_free(manifest);\n"
        "    nob_sb_free(memcheck_xml);\n"
        "    nob_sb_free(junit);\n"
        "    return true;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_coverage_local(const char *build_dir,\n"
        "                                                               const char *model,\n"
        "                                                               const char *track,\n"
        "                                                               bool append_mode,\n"
        "                                                               const char *const *source_paths,\n"
        "                                                               size_t source_count,\n"
        "                                                               const char *const *coverage_argv,\n"
        "                                                               size_t coverage_argc) {\n"
        "    Nob_Cmd cmd = {0};\n"
        "    char *stdout_text = NULL;\n"
        "    char *stderr_text = NULL;\n"
        "    char *command_text = NULL;\n"
        "    Nob_Test_String_List normalized_argv = {0};\n"
        "    bool ok = false;\n"
        "    if (!build_dir || !coverage_argv || coverage_argc == 0) return false;\n"
        "    for (size_t i = 0; i < coverage_argc; ++i) {\n"
        "        if (!test_string_list_push(&normalized_argv,\n"
        "                                   coverage_argv[i] ? coverage_argv[i] : \"\",\n"
        "                                   strlen(coverage_argv[i] ? coverage_argv[i] : \"\"))) {\n"
        "            test_string_list_free(&normalized_argv);\n"
        "            return false;\n"
        "        }\n"
        "    }\n"
        "    if (!test_normalize_command_tokens(&normalized_argv, build_dir)) {\n"
        "        test_string_list_free(&normalized_argv);\n"
        "        return false;\n"
        "    }\n"
        "    command_text = test_string_list_join(&normalized_argv, \" \");\n"
        "    if (!command_text) return false;\n"
        "    for (size_t i = 0; i < normalized_argv.count; ++i) nob_cmd_append(&cmd, normalized_argv.items[i]);\n"
        "    ok = run_cmd_capture_in_dir(build_dir, &cmd, &stdout_text, &stderr_text);\n"
        "    nob_cmd_free(cmd);\n"
        "    test_string_list_free(&normalized_argv);\n"
        "    if (!stdout_text || !stderr_text) {\n"
        "        free(command_text);\n"
        "        free(stdout_text);\n"
        "        free(stderr_text);\n"
        "        return false;\n"
        "    }\n"
        "    if (ok && !ctest_write_coverage_reports(build_dir,\n"
        "                                           model,\n"
        "                                           track,\n"
        "                                           append_mode,\n"
        "                                           command_text,\n"
        "                                           source_paths,\n"
        "                                           source_count,\n"
        "                                           stdout_text,\n"
        "                                           stderr_text,\n"
        "                                           ok ? 0 : 1)) {\n"
        "        free(command_text);\n"
        "        free(stdout_text);\n"
        "        free(stderr_text);\n"
        "        return false;\n"
        "    }\n"
        "    free(command_text);\n"
        "    free(stdout_text);\n"
        "    free(stderr_text);\n"
        "    return ok;\n"
        "}\n\n"
        "static const char *ctest_session_source_dir_for_build(const char *build_dir);\n"
        "static bool ctest_session_targets_generated_backend(const char *build_dir);\n"
        "static bool ctest_execute_memcheck_external(const char *source_dir,\n"
        "                                           const char *build_dir,\n"
        "                                           const char *output_junit,\n"
        "                                           const char *model,\n"
        "                                           bool append_mode,\n"
        "                                           const char *backend_type,\n"
        "                                           const char *resource_spec_file,\n"
        "                                           const char *suppression_file,\n"
        "                                           const char *const *prefix_argv,\n"
        "                                           size_t prefix_count,\n"
        "                                           const char *const *selected_names,\n"
        "                                           size_t selected_count,\n"
        "                                           const char *config_filter);\n\n"
        "static bool __attribute__((unused)) ctest_execute_memcheck_local(const char *build_dir,\n"
        "                                                                const char *output_junit,\n"
        "                                                                const char *model,\n"
        "                                                                const char *track,\n"
        "                                                                bool append_mode,\n"
        "                                                                const char *start_text,\n"
        "                                                                const char *end_text,\n"
        "                                                                const char *stride_text,\n"
        "                                                                const char *stop_time,\n"
        "                                                                bool stop_on_failure,\n"
        "                                                                bool schedule_random,\n"
        "                                                                const char *repeat_text,\n"
        "                                                                const char *backend_type,\n"
        "                                                                const char *resource_spec_file,\n"
        "                                                                const char *suppression_file,\n"
        "                                                                const char *const *prefix_argv,\n"
        "                                                                size_t prefix_count,\n"
        "                                                                const char *const *selected_names,\n"
        "                                                                size_t selected_count,\n"
        "                                                                const char *config_filter) {\n"
        "    size_t total_tests = generated_test_count();\n"
        "    size_t matched_count = 0;\n"
        "    size_t *matched = NULL;\n"
        "    Nob_Generated_Memcheck_Result *results = NULL;\n"
        "    size_t start_index = 1;\n"
        "    size_t end_index = 0;\n"
        "    size_t stride_value = 1;\n"
        "    size_t failed_count = 0;\n"
        "    size_t total_defects = 0;\n"
        "    size_t executed_count = 0;\n"
        "    Nob_Generated_Test_Repeat_Mode repeat_mode = TEST_REPEAT_NONE;\n"
        "    size_t repeat_count = 1;\n"
        "    bool stop_time_ok = true;\n"
        "    int stop_time_seconds = test_parse_stop_time_seconds(stop_time, &stop_time_ok);\n"
        "    bool ok = true;\n"
        "    if (!stop_time_ok) return false;\n"
        "    if (!ctest_session_targets_generated_backend(build_dir)) {\n"
        "        return ctest_execute_memcheck_external(ctest_session_source_dir_for_build(build_dir),\n"
        "                                               build_dir,\n"
        "                                               output_junit,\n"
        "                                               model,\n"
        "                                               append_mode,\n"
        "                                               backend_type,\n"
        "                                               resource_spec_file,\n"
        "                                               suppression_file,\n"
        "                                               prefix_argv,\n"
        "                                               prefix_count,\n"
        "                                               selected_names,\n"
        "                                               selected_count,\n"
        "                                               config_filter);\n"
        "    }\n"
        "    if (!test_parse_repeat_mode_and_count(repeat_text, &repeat_mode, &repeat_count)) return false;\n"
        "    if (!test_parse_optional_index(start_text, 1u, &start_index)) return false;\n"
        "    if (!test_parse_optional_index(end_text, total_tests, &end_index)) return false;\n"
        "    if (!test_parse_optional_index(stride_text, 1u, &stride_value) || stride_value == 0) return false;\n"
        "    if (selected_count > 0) {\n"
        "        for (size_t i = 0; i < selected_count; ++i) {\n"
        "            if (selected_names[i] && !selected_test_exists(selected_names[i])) {\n"
        "                nob_log(NOB_ERROR, \"test: unknown test '%s'\", selected_names[i]);\n"
        "                return false;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    matched = (size_t *)calloc(total_tests > 0 ? total_tests : 1u, sizeof(*matched));\n"
        "    results = (Nob_Generated_Memcheck_Result *)calloc(total_tests > 0 ? total_tests : 1u, sizeof(*results));\n"
        "    if (!matched || !results) {\n"
        "        free(matched);\n"
        "        free(results);\n"
        "        return false;\n"
        "    }\n"
        "    for (size_t i = 0; i < total_tests; ++i) {\n"
        "        const Nob_Generated_Test_Case *test_case = &g_generated_tests[i];\n"
        "        if (!test_name_selected(test_case->name, selected_names, selected_count) ||\n"
        "            !test_config_selected(test_case, config_filter)) {\n"
        "            continue;\n"
        "        }\n"
        "        matched[matched_count++] = i;\n"
        "    }\n"
        "    if (matched_count == 0) {\n"
        "        free(matched);\n"
        "        free(results);\n"
        "        return true;\n"
        "    }\n"
        "    if (schedule_random) test_sort_indices_deterministic(matched, matched_count, g_generated_tests);\n"
        "    if (end_index > matched_count) end_index = matched_count;\n"
        "    for (size_t match_index = 0; match_index < matched_count; ++match_index) {\n"
        "        const Nob_Generated_Test_Case *test_case = &g_generated_tests[matched[match_index]];\n"
        "        Nob_Generated_Memcheck_Result *result = &results[executed_count];\n"
        "        size_t test_number = match_index + 1u;\n"
        "        if (test_number < start_index || test_number > end_index) continue;\n"
        "        if (((test_number - start_index) % stride_value) != 0u) continue;\n"
        "        if (stop_time_seconds >= 0) {\n"
        "            int now_seconds = test_current_local_seconds_of_day();\n"
        "            if (now_seconds >= 0 && now_seconds >= stop_time_seconds) break;\n"
        "        }\n"
        "        result->name = test_case->name;\n"
        "        result->working_dir = test_case->working_dir;\n"
        "        result->exit_code = 1;\n"
        "        for (size_t attempt = 0; attempt < repeat_count; ++attempt) {\n"
        "            Nob_Test_String_List test_argv = {0};\n"
        "            Nob_Test_String_List backend_argv = {0};\n"
        "            Nob_Cmd cmd = {0};\n"
        "            bool run_ok = false;\n"
        "            generated_memcheck_result_clear(result);\n"
        "            result->name = test_case->name;\n"
        "            result->working_dir = test_case->working_dir;\n"
        "            if (!prepare_test_command_argv(test_case, &test_argv, false)) {\n"
        "                test_string_list_free(&test_argv);\n"
        "                ok = false;\n"
        "                goto memcheck_cleanup;\n"
        "            }\n"
        "            for (size_t i = 0; i < prefix_count; ++i) {\n"
        "                if (!test_string_list_push(&backend_argv, prefix_argv[i] ? prefix_argv[i] : \"\", strlen(prefix_argv[i] ? prefix_argv[i] : \"\"))) {\n"
        "                    test_string_list_free(&test_argv);\n"
        "                    test_string_list_free(&backend_argv);\n"
        "                    ok = false;\n"
        "                    goto memcheck_cleanup;\n"
        "                }\n"
        "            }\n"
        "            if (!test_normalize_command_tokens(&backend_argv, build_dir)) {\n"
        "                test_string_list_free(&test_argv);\n"
        "                test_string_list_free(&backend_argv);\n"
        "                ok = false;\n"
        "                goto memcheck_cleanup;\n"
        "            }\n"
        "            if (!test_string_list_push(&backend_argv, \"--\", 2u)) {\n"
        "                test_string_list_free(&test_argv);\n"
        "                test_string_list_free(&backend_argv);\n"
        "                ok = false;\n"
        "                goto memcheck_cleanup;\n"
        "            }\n"
        "            for (size_t i = 0; i < test_argv.count; ++i) {\n"
        "                if (!test_string_list_push(&backend_argv, test_argv.items[i], strlen(test_argv.items[i] ? test_argv.items[i] : \"\"))) {\n"
        "                    test_string_list_free(&test_argv);\n"
        "                    test_string_list_free(&backend_argv);\n"
        "                    ok = false;\n"
        "                    goto memcheck_cleanup;\n"
        "                }\n"
        "            }\n"
        "            result->command = test_string_list_join(&test_argv, \" \");\n"
        "            result->backend_command = test_string_list_join(&backend_argv, \" \");\n"
        "            if (!result->command || !result->backend_command) {\n"
        "                test_string_list_free(&test_argv);\n"
        "                test_string_list_free(&backend_argv);\n"
        "                ok = false;\n"
        "                goto memcheck_cleanup;\n"
        "            }\n"
        "            nob_log(NOB_INFO,\n"
        "                    \"ctest_memcheck local backend command: %s\",\n"
        "                    result->backend_command ? result->backend_command : \"<null>\");\n"
        "            for (size_t i = 0; i < backend_argv.count; ++i) nob_cmd_append(&cmd, backend_argv.items[i]);\n"
        "            run_ok = run_cmd_capture_in_dir(test_case->working_dir, &cmd, &result->stdout_text, &result->stderr_text);\n"
        "            nob_cmd_free(cmd);\n"
        "            test_string_list_free(&test_argv);\n"
        "            test_string_list_free(&backend_argv);\n"
        "            if (!result->stdout_text || !result->stderr_text) {\n"
        "                ok = false;\n"
        "                goto memcheck_cleanup;\n"
        "            }\n"
        "            result->defect_count = test_memcheck_extract_defect_count(result->stdout_text, result->stderr_text);\n"
        "            result->passed = run_ok && result->defect_count == 0u;\n"
        "            result->exit_code = result->passed ? 0 : 1;\n"
        "            if (repeat_mode == TEST_REPEAT_NONE) break;\n"
        "            if (repeat_mode == TEST_REPEAT_UNTIL_PASS && result->passed) break;\n"
        "            if (repeat_mode == TEST_REPEAT_UNTIL_FAIL && !result->passed) break;\n"
        "            if (repeat_mode == TEST_REPEAT_AFTER_TIMEOUT) break;\n"
        "        }\n"
        "        total_defects += result->defect_count;\n"
        "        executed_count++;\n"
        "        if (!result->passed) {\n"
        "            failed_count++;\n"
        "            if (stop_on_failure) break;\n"
        "        }\n"
        "    }\n"
        "    if (!ctest_write_memcheck_reports(build_dir,\n"
        "                                     model,\n"
        "                                     track,\n"
        "                                     append_mode,\n"
        "                                     backend_type,\n"
        "                                     resource_spec_file,\n"
        "                                     suppression_file,\n"
        "                                     results,\n"
        "                                     executed_count,\n"
        "                                     total_defects,\n"
        "                                     failed_count,\n"
        "                                     output_junit)) {\n"
        "        ok = false;\n"
        "        goto memcheck_cleanup;\n"
        "    }\n"
        "    ok = failed_count == 0u && total_defects == 0u;\n"
        "memcheck_cleanup:\n"
        "    for (size_t i = 0; i < executed_count; ++i) generated_memcheck_result_clear(&results[i]);\n"
        "    free(results);\n"
        "    free(matched);\n"
        "    return ok;\n"
        "}\n\n"
        "static bool run_registered_tests(bool auto_build,\n"
        "                                 const char *const *selected_names,\n"
        "                                 size_t selected_count,\n"
        "                                 const char *config_filter,\n"
        "                                 const char *ctest_build_dir,\n"
        "                                 const char *output_junit,\n"
        "                                 bool schedule_random,\n"
        "                                 bool stage_ctest) {\n"
        "    size_t total_tests = generated_test_count();\n"
        "    size_t matched_count = 0;\n"
        "    size_t *matched = NULL;\n"
        "    Nob_Generated_Test_Result *results = NULL;\n"
        "    bool all_passed = true;\n"
        "    if (selected_count > 0) {\n"
        "        for (size_t i = 0; i < selected_count; ++i) {\n"
        "            if (selected_names[i] && !selected_test_exists(selected_names[i])) {\n"
        "                nob_log(NOB_ERROR, \"test: unknown test '%s'\", selected_names[i]);\n"
        "                return false;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    if (total_tests == 0) {\n"
        "        nob_log(NOB_INFO, \"test: no registered tests\");\n"
        "        return true;\n"
        "    }\n"
        "    matched = (size_t *)calloc(total_tests, sizeof(*matched));\n"
        "    if (!matched) return false;\n"
        "    for (size_t i = 0; i < total_tests; ++i) {\n"
        "        const Nob_Generated_Test_Case *test_case = &g_generated_tests[i];\n"
        "        if (!test_name_selected(test_case->name, selected_names, selected_count) ||\n"
        "            !test_config_selected(test_case, config_filter)) {\n"
        "            continue;\n"
        "        }\n"
        "        matched[matched_count++] = i;\n"
        "    }\n"
        "    if (matched_count == 0) {\n"
        "        free(matched);\n"
        "        nob_log(NOB_INFO, \"test: no tests matched the current filter\");\n"
        "        return true;\n"
        "    }\n"
        "    if (schedule_random) test_sort_indices_deterministic(matched, matched_count, g_generated_tests);\n"
        "    results = (Nob_Generated_Test_Result *)calloc(matched_count, sizeof(*results));\n"
        "    if (!results) {\n"
        "        free(matched);\n"
        "        return false;\n"
        "    }\n"
        "    for (size_t match_index = 0; match_index < matched_count; ++match_index) {\n"
        "        const Nob_Generated_Test_Case *test_case = &g_generated_tests[matched[match_index]];\n"
        "        Nob_Test_String_List argv = {0};\n"
        "        bool ok = false;\n"
        "        results[match_index].name = test_case->name;\n"
        "        results[match_index].exit_code = 1;\n"
        "        if (!prepare_test_command_argv(test_case, &argv, auto_build)) {\n"
        "            free(results);\n"
        "            free(matched);\n"
        "            return false;\n"
        "        }\n"
        "        {\n"
        "            Nob_Cmd cmd = {0};\n"
        "            for (size_t arg_index = 0; arg_index < argv.count; ++arg_index) {\n"
        "                nob_cmd_append(&cmd, argv.items[arg_index]);\n"
        "            }\n"
        "            ok = run_cmd_in_dir(test_case->working_dir, &cmd);\n"
        "            nob_cmd_free(cmd);\n"
        "        }\n"
        "        results[match_index].passed = ok;\n"
        "        results[match_index].exit_code = ok ? 0 : 1;\n"
        "        if (!ok) all_passed = false;\n"
        "        test_string_list_free(&argv);\n"
        "    }\n"
        "    if (stage_ctest && ctest_build_dir && ctest_build_dir[0] != '\\0' &&\n"
        "        !ctest_write_test_reports(ctest_build_dir, results, matched_count, output_junit)) {\n"
        "        free(results);\n"
        "        free(matched);\n"
        "        return false;\n"
        "    }\n"
        "    {\n"
        "        size_t passed_count = 0;\n"
        "        for (size_t i = 0; i < matched_count; ++i) if (results[i].passed) passed_count++;\n"
        "        nob_log(all_passed ? NOB_INFO : NOB_ERROR,\n"
        "                \"test: %zu/%zu passed\",\n"
        "                passed_count,\n"
        "                matched_count);\n"
        "    }\n"
        "    free(results);\n"
        "    free(matched);\n"
        "    return all_passed;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_empty_binary_directory(const char *target_dir) {\n"
        "    if (!target_dir) return false;\n"
        "    if (nob_file_exists(target_dir)) {\n"
        "        if (nob_get_file_type(target_dir) == NOB_FILE_DIRECTORY) {\n"
        "            if (!remove_path_recursive(target_dir)) return false;\n"
        "        } else if (remove(target_dir) != 0) {\n"
        "            return false;\n"
        "        }\n"
        "    }\n"
        "    return ensure_dir(target_dir);\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_generated_source_dir(void) {\n"
        "    return ");
    if (!cg_sb_append_c_string(out, generated_source_dir)) return false;
    nob_sb_append_cstr(out,
        ";\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_generated_binary_dir(void) {\n"
        "    return ");
    if (!cg_sb_append_c_string(out, generated_binary_dir)) return false;
    nob_sb_append_cstr(out,
        ";\n"
        "}\n\n"
        "static const char * __attribute__((unused)) resolve_ctest_bin(void) {\n"
        "    const char *tool = getenv(\"NOB_CTEST_BIN\");\n"
        "    if (tool && tool[0] != '\\0') return tool;\n"
        "    return \"ctest\";\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_resolve_cmake_bin(void) {\n"
        "    const char *tool = getenv(\"NOB_CMAKE_BIN\");\n"
        "    if (tool && tool[0] != '\\0') return tool;\n"
        "    return ");
    if (!cg_sb_append_c_string(out,
                               ctx->embedded_cmake_bin_abs.count > 0
                                   ? ctx->embedded_cmake_bin_abs
                                   : nob_sv_from_cstr("cmake"))) {
        return false;
    }
    nob_sb_append_cstr(out,
        ";\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_skip_dot_prefixes(const char *path) {\n"
        "    if (!path) return \"\";\n"
        "    while (path[0] == '.' && path[1] == '/') path += 2;\n"
        "    return path;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_paths_match(const char *lhs, const char *rhs) {\n"
        "    lhs = ctest_skip_dot_prefixes(lhs);\n"
        "    rhs = ctest_skip_dot_prefixes(rhs);\n"
        "    return strcmp(lhs ? lhs : \"\", rhs ? rhs : \"\") == 0;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_path_is_within_or_equal(const char *root,\n"
        "                                                                  const char *candidate) {\n"
        "    size_t root_len = 0u;\n"
        "    root = ctest_skip_dot_prefixes(root);\n"
        "    candidate = ctest_skip_dot_prefixes(candidate);\n"
        "    if (!root || !candidate) return false;\n"
        "    root_len = strlen(root);\n"
        "    if (root_len == 0u) return candidate[0] == '\\0';\n"
        "    if (strncmp(root, candidate, root_len) != 0) return false;\n"
        "    if (candidate[root_len] == '\\0') return true;\n"
        "    return candidate[root_len] == '/';\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_generated_backend_has_local_surface(void) {\n"
        "    return generated_test_count() > 0u || generated_default_build_target_count() > 0u;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_targets_generated_backend(const char *source_dir,\n"
        "                                                                    const char *build_dir) {\n"
        "    return ctest_generated_backend_has_local_surface() &&\n"
        "           ctest_path_is_within_or_equal(ctest_generated_source_dir(), source_dir) &&\n"
        "           ctest_path_is_within_or_equal(ctest_generated_binary_dir(), build_dir);\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_build_dir_is_generated_backend(const char *build_dir) {\n"
        "    return ctest_generated_backend_has_local_surface() &&\n"
        "           ctest_path_is_within_or_equal(ctest_generated_binary_dir(), build_dir);\n"
        "}\n\n"
        "static const char *g_ctest_session_source_dir = NULL;\n"
        "static const char *g_ctest_session_build_dir = NULL;\n\n"
        "static void __attribute__((unused)) ctest_update_session_context(const char *source_dir,\n"
        "                                                                const char *build_dir) {\n"
        "    g_ctest_session_source_dir = source_dir;\n"
        "    g_ctest_session_build_dir = build_dir;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_session_matches_build_dir(const char *build_dir) {\n"
        "    return g_ctest_session_build_dir && ctest_paths_match(build_dir, g_ctest_session_build_dir);\n"
        "}\n\n"
        "static const char * __attribute__((unused)) ctest_session_source_dir_for_build(const char *build_dir) {\n"
        "    if (!ctest_session_matches_build_dir(build_dir)) return NULL;\n"
        "    return g_ctest_session_source_dir ? g_ctest_session_source_dir : \"\";\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_session_targets_generated_backend(const char *build_dir) {\n"
        "    const char *source_dir = ctest_session_source_dir_for_build(build_dir);\n"
        "    if (!source_dir) return ctest_build_dir_is_generated_backend(build_dir);\n"
        "    return ctest_targets_generated_backend(source_dir, build_dir);\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_configure_local(const char *source_dir,\n"
        "                                                                 const char *build_dir) {\n"
        "    Nob_Cmd cmd = {0};\n"
        "    bool ok = false;\n"
        "    if (!source_dir || source_dir[0] == '\\0' || !build_dir || build_dir[0] == '\\0') return false;\n"
        "    if (!ensure_dir(build_dir)) return false;\n"
        "    nob_cmd_append(&cmd, ctest_resolve_cmake_bin(), \"-S\", source_dir, \"-B\", build_dir);\n"
        "    ok = nob_cmd_run(&cmd);\n"
        "    nob_cmd_free(cmd);\n"
        "    return ok;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_build_local(const char *build_dir,\n"
        "                                                             const char *configuration,\n"
        "                                                             const char *target) {\n"
        "    Nob_Cmd cmd = {0};\n"
        "    bool ok = false;\n"
        "    if (!build_dir || build_dir[0] == '\\0') return false;\n"
        "    nob_cmd_append(&cmd, ctest_resolve_cmake_bin(), \"--build\", build_dir);\n"
        "    if (configuration && configuration[0] != '\\0') nob_cmd_append(&cmd, \"--config\", configuration);\n"
        "    if (target && target[0] != '\\0') nob_cmd_append(&cmd, \"--target\", target);\n"
        "    ok = nob_cmd_run(&cmd);\n"
        "    nob_cmd_free(cmd);\n"
        "    return ok;\n"
        "}\n\n"
        "static char * __attribute__((unused)) ctest_selected_names_regex(const char *const *selected_names,\n"
        "                                                                size_t selected_count) {\n"
        "    Nob_String_Builder sb = {0};\n"
        "    char *copy = NULL;\n"
        "    if (!selected_names || selected_count == 0u) return NULL;\n"
        "    nob_sb_append_cstr(&sb, \"^(\");\n"
        "    for (size_t i = 0; i < selected_count; ++i) {\n"
        "        const char *name = selected_names[i] ? selected_names[i] : \"\";\n"
        "        if (i > 0u) nob_sb_append_cstr(&sb, \"|\");\n"
        "        nob_sb_append_cstr(&sb, name);\n"
        "    }\n"
        "    nob_sb_append_cstr(&sb, \")$\");\n"
        "    copy = (char *)malloc(sb.count + 1u);\n"
        "    if (!copy) {\n"
        "        nob_sb_free(sb);\n"
        "        return NULL;\n"
        "    }\n"
        "    if (sb.count > 0u && sb.items) memcpy(copy, sb.items, sb.count);\n"
        "    copy[sb.count] = '\\0';\n"
        "    nob_sb_free(sb);\n"
        "    return copy;\n"
        "}\n\n"
        "static void __attribute__((unused)) test_append_cmake_escaped(Nob_String_Builder *sb, const char *text) {\n"
        "    if (!sb || !text) return;\n"
        "    while (*text) {\n"
        "        if (*text == '\\\\' || *text == '\"' || *text == '$') nob_sb_append(sb, '\\\\');\n"
        "        nob_sb_append(sb, *text++);\n"
        "    }\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_script_append_set_quoted(Nob_String_Builder *sb,\n"
        "                                                                   const char *name,\n"
        "                                                                   const char *value) {\n"
        "    if (!sb || !name) return false;\n"
        "    nob_sb_append_cstr(sb, \"set(\");\n"
        "    nob_sb_append_cstr(sb, name);\n"
        "    nob_sb_append_cstr(sb, \" \\\"\");\n"
        "    test_append_cmake_escaped(sb, value ? value : \"\");\n"
        "    nob_sb_append_cstr(sb, \"\\\")\\n\");\n"
        "    return true;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_memcheck_external(const char *source_dir,\n"
        "                                                                   const char *build_dir,\n"
        "                                                                   const char *output_junit,\n"
        "                                                                   const char *model,\n"
        "                                                                   bool append_mode,\n"
        "                                                                   const char *backend_type,\n"
        "                                                                   const char *resource_spec_file,\n"
        "                                                                   const char *suppression_file,\n"
        "                                                                   const char *const *prefix_argv,\n"
        "                                                                   size_t prefix_count,\n"
        "                                                                   const char *const *selected_names,\n"
        "                                                                   size_t selected_count,\n"
        "                                                                   const char *config_filter) {\n"
        "    Nob_String_Builder options = {0};\n"
        "    Nob_String_Builder script = {0};\n"
        "    Nob_Cmd cmd = {0};\n"
        "    char *options_text = NULL;\n"
        "    char *selected_regex = NULL;\n"
        "    const char *script_path = NULL;\n"
        "    bool ok = false;\n"
        "    if (!source_dir || source_dir[0] == '\\0' || !build_dir || build_dir[0] == '\\0' ||\n"
        "        !prefix_argv || prefix_count == 0u || !prefix_argv[0] || prefix_argv[0][0] == '\\0') {\n"
        "        return false;\n"
        "    }\n"
        "    for (size_t i = 1; i < prefix_count; ++i) {\n"
        "        if (i > 1u) nob_sb_append(&options, ';');\n"
        "        nob_sb_append_cstr(&options, prefix_argv[i] ? prefix_argv[i] : \"\");\n"
        "    }\n"
        "    if (options.count > 0u) nob_sb_append(&options, ';');\n"
        "    nob_sb_append_cstr(&options, \"--\");\n"
        "    options_text = (char *)malloc(options.count + 1u);\n"
        "    if (!options_text) {\n"
        "        nob_sb_free(options);\n"
        "        return false;\n"
        "    }\n"
        "    if (options.count > 0u && options.items) memcpy(options_text, options.items, options.count);\n"
        "    options_text[options.count] = '\\0';\n"
        "    if (selected_count > 0u) {\n"
        "        selected_regex = ctest_selected_names_regex(selected_names, selected_count);\n"
        "        if (!selected_regex) {\n"
        "            free(options_text);\n"
        "            nob_sb_free(options);\n"
        "            return false;\n"
        "        }\n"
        "    }\n"
        "    script_path = nob_temp_sprintf(\"%s/.nob/ctest-external-memcheck.cmake\", build_dir);\n"
        "    nob_sb_append_cstr(&script, \"cmake_minimum_required(VERSION 3.28)\\n\");\n"
        "    if (!ctest_script_append_set_quoted(&script, \"CTEST_SOURCE_DIRECTORY\", source_dir) ||\n"
        "        !ctest_script_append_set_quoted(&script, \"CTEST_BINARY_DIRECTORY\", build_dir) ||\n"
        "        !ctest_script_append_set_quoted(&script, \"CTEST_MEMORYCHECK_COMMAND\", prefix_argv[0]) ||\n"
        "        !ctest_script_append_set_quoted(&script,\n"
        "                                        \"CTEST_MEMORYCHECK_TYPE\",\n"
        "                                        (backend_type && backend_type[0] != '\\0') ? backend_type : \"Generic\")) {\n"
        "        free(options_text);\n"
        "        free(selected_regex);\n"
        "        nob_sb_free(options);\n"
        "        nob_sb_free(script);\n"
        "        return false;\n"
        "    }\n"
        "    if (options.count > 0u &&\n"
        "        !ctest_script_append_set_quoted(&script,\n"
        "                                        \"CTEST_MEMORYCHECK_COMMAND_OPTIONS\",\n"
        "                                        options_text ? options_text : \"\")) {\n"
        "        free(options_text);\n"
        "        free(selected_regex);\n"
        "        nob_sb_free(options);\n"
        "        nob_sb_free(script);\n"
        "        return false;\n"
        "    }\n"
        "    if (config_filter && config_filter[0] != '\\0' &&\n"
        "        !ctest_script_append_set_quoted(&script, \"CTEST_CONFIGURATION_TYPE\", config_filter)) {\n"
        "        free(options_text);\n"
        "        free(selected_regex);\n"
        "        nob_sb_free(options);\n"
        "        nob_sb_free(script);\n"
        "        return false;\n"
        "    }\n"
        "    if (resource_spec_file && resource_spec_file[0] != '\\0' &&\n"
        "        !ctest_script_append_set_quoted(&script, \"CTEST_RESOURCE_SPEC_FILE\", resource_spec_file)) {\n"
        "        free(options_text);\n"
        "        free(selected_regex);\n"
        "        nob_sb_free(options);\n"
        "        nob_sb_free(script);\n"
        "        return false;\n"
        "    }\n"
        "    if (suppression_file && suppression_file[0] != '\\0' &&\n"
        "        !ctest_script_append_set_quoted(&script,\n"
        "                                        \"CTEST_MEMORYCHECK_SUPPRESSIONS_FILE\",\n"
        "                                        suppression_file)) {\n"
        "        free(options_text);\n"
        "        free(selected_regex);\n"
        "        nob_sb_free(options);\n"
        "        nob_sb_free(script);\n"
        "        return false;\n"
        "    }\n"
        "    nob_sb_append_cstr(&script, \"ctest_start(\");\n"
        "    test_append_cmake_escaped(&script, (model && model[0] != '\\0') ? model : \"Experimental\");\n"
        "    nob_sb_append_cstr(&script, \" \\\"${CTEST_SOURCE_DIRECTORY}\\\" \\\"${CTEST_BINARY_DIRECTORY}\\\" QUIET)\\n\");\n"
        "    nob_sb_append_cstr(&script, \"ctest_memcheck(\");\n"
        "    if (append_mode) nob_sb_append_cstr(&script, \"APPEND \");\n"
        "    nob_sb_append_cstr(&script, \"QUIET\");\n"
        "    if (selected_regex && selected_regex[0] != '\\0') {\n"
        "        nob_sb_append_cstr(&script, \" INCLUDE \\\"\");\n"
        "        test_append_cmake_escaped(&script, selected_regex);\n"
        "        nob_sb_append_cstr(&script, \"\\\"\");\n"
        "    }\n"
        "    if (output_junit && output_junit[0] != '\\0') {\n"
        "        nob_sb_append_cstr(&script, \" OUTPUT_JUNIT \\\"\");\n"
        "        test_append_cmake_escaped(&script, output_junit);\n"
        "        nob_sb_append_cstr(&script, \"\\\"\");\n"
        "    }\n"
        "    nob_sb_append_cstr(&script, \")\\n\");\n"
        "    if (!test_write_buffer_file(script_path, script.items ? script.items : \"\", script.count)) {\n"
        "        free(selected_regex);\n"
        "        nob_sb_free(options);\n"
        "        nob_sb_free(script);\n"
        "        return false;\n"
        "    }\n"
        "    nob_cmd_append(&cmd, resolve_ctest_bin(), \"-S\", script_path, \"-VV\");\n"
        "    ok = nob_cmd_run(&cmd);\n"
        "    nob_cmd_free(cmd);\n"
        "    free(options_text);\n"
        "    free(selected_regex);\n"
        "    nob_sb_free(options);\n"
        "    nob_sb_free(script);\n"
        "    return ok;\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_test_local(const char *build_dir,\n"
        "                                                            const char *const *selected_names,\n"
        "                                                            size_t selected_count,\n"
        "                                                            const char *config_filter,\n"
        "                                                            const char *output_junit,\n"
        "                                                            bool schedule_random) {\n"
        "    Nob_Cmd cmd = {0};\n"
        "    char *selected_regex = NULL;\n"
        "    bool ok = false;\n"
        "    if (!build_dir || build_dir[0] == '\\0') return false;\n"
        "    nob_cmd_append(&cmd, resolve_ctest_bin(), \"--test-dir\", build_dir);\n"
        "    if (config_filter && config_filter[0] != '\\0') nob_cmd_append(&cmd, \"-C\", config_filter);\n"
        "    if (schedule_random) nob_cmd_append(&cmd, \"--schedule-random\");\n"
        "    if (output_junit && output_junit[0] != '\\0') nob_cmd_append(&cmd, \"--output-junit\", output_junit);\n"
        "    if (selected_count > 0u) {\n"
        "        selected_regex = ctest_selected_names_regex(selected_names, selected_count);\n"
        "        if (!selected_regex) {\n"
        "            nob_cmd_free(cmd);\n"
        "            return false;\n"
        "        }\n"
        "        nob_cmd_append(&cmd, \"-R\", selected_regex);\n"
        "    }\n"
        "    ok = nob_cmd_run(&cmd);\n"
        "    nob_cmd_free(cmd);\n"
        "    free(selected_regex);\n"
        "    if (!ok) return false;\n"
        "    if (!ctest_prepare_tree(build_dir, true)) return false;\n"
        "    if (!test_write_text_file(nob_temp_sprintf(\"%s/Test.xml\", ctest_tag_dir(build_dir)),\n"
        "                              nob_temp_sprintf(\"<Test build=\\\"%s\\\" status=\\\"ok\\\" />\\n\",\n"
        "                                               build_dir ? build_dir : \"\"))) {\n"
        "        return false;\n"
        "    }\n"
        "    return test_write_text_file(nob_temp_sprintf(\"%s/TestManifest.txt\", ctest_tag_dir(build_dir)), \"Test.xml\\n\");\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_start_local(const char *source_dir,\n"
        "                                                             const char *build_dir,\n"
        "                                                             const char *model,\n"
        "                                                             const char *track,\n"
        "                                                             bool append_mode) {\n"
        "    (void)model;\n"
        "    (void)track;\n"
        "    ctest_update_session_context(source_dir, build_dir);\n"
        "    if (!ctest_prepare_tree(build_dir, append_mode)) return false;\n"
        "    return test_write_text_file(nob_temp_sprintf(\"%s/Start.txt\", ctest_tag_dir(build_dir)),\n"
        "                                nob_temp_sprintf(\"source=%s\\nbuild=%s\\n\", source_dir ? source_dir : \"\", build_dir ? build_dir : \"\"));\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_configure_self(const char *source_dir, const char *build_dir) {\n"
        "    ctest_update_session_context(source_dir, build_dir);\n"
        "    if (ctest_targets_generated_backend(source_dir, build_dir)) {\n"
            "        if (!configure_all(true)) return false;\n"
        "    } else if (!ctest_execute_configure_local(source_dir, build_dir)) {\n"
            "        return false;\n"
        "    }\n"
        "    if (!ctest_prepare_tree(build_dir, true)) return false;\n"
        "    if (!test_write_text_file(nob_temp_sprintf(\"%s/Configure.xml\", ctest_tag_dir(build_dir)),\n"
        "                              nob_temp_sprintf(\"<Configure source=\\\"%s\\\" build=\\\"%s\\\" status=\\\"ok\\\" />\\n\",\n"
        "                                               source_dir ? source_dir : \"\",\n"
        "                                               build_dir ? build_dir : \"\"))) {\n"
        "        return false;\n"
        "    }\n"
        "    return test_write_text_file(nob_temp_sprintf(\"%s/ConfigureManifest.txt\", ctest_tag_dir(build_dir)), \"Configure.xml\\n\");\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_build_self(const char *build_dir, const char *configuration, const char *target) {\n"
        "    const char *prev_config = g_build_config;\n"
        "    bool ok = false;\n"
        "    if (ctest_session_targets_generated_backend(build_dir)) {\n"
        "        if (configuration && configuration[0] != '\\0') g_build_config = configuration;\n"
        "        if (!ensure_configured()) {\n"
        "            g_build_config = prev_config;\n"
        "            return false;\n"
        "        }\n"
        "        ok = (target && target[0] != '\\0') ? build_request(target) : build_default_targets();\n"
        "        g_build_config = prev_config;\n"
        "        if (!ok) return false;\n"
        "    } else if (!ctest_execute_build_local(build_dir, configuration, target)) {\n"
        "        return false;\n"
        "    }\n"
        "    if (!ctest_prepare_tree(build_dir, true)) return false;\n"
        "    if (!test_write_text_file(nob_temp_sprintf(\"%s/Build.xml\", ctest_tag_dir(build_dir)),\n"
        "                              nob_temp_sprintf(\"<Build configuration=\\\"%s\\\" target=\\\"%s\\\" status=\\\"ok\\\" />\\n\",\n"
        "                                               configuration ? configuration : \"\",\n"
        "                                               target ? target : \"\"))) {\n"
        "        return false;\n"
        "    }\n"
        "    return test_write_text_file(nob_temp_sprintf(\"%s/BuildManifest.txt\", ctest_tag_dir(build_dir)), \"Build.xml\\n\");\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_test(const char *build_dir,\n"
        "                                                      const char *const *selected_names,\n"
        "                                                      size_t selected_count,\n"
        "                                                      const char *config_filter,\n"
        "                                                      const char *output_junit,\n"
        "                                                      bool schedule_random) {\n"
        "    if (!ctest_session_targets_generated_backend(build_dir)) {\n"
        "        return ctest_execute_test_local(build_dir,\n"
        "                                        selected_names,\n"
        "                                        selected_count,\n"
        "                                        config_filter,\n"
        "                                        output_junit,\n"
        "                                        schedule_random);\n"
        "    }\n"
        "    return run_registered_tests(false,\n"
        "                               selected_names,\n"
        "                               selected_count,\n"
        "                               config_filter,\n"
        "                               build_dir,\n"
        "                               output_junit,\n"
        "                               schedule_random,\n"
        "                               true);\n"
        "}\n\n"
        "static bool __attribute__((unused)) ctest_execute_sleep(const char *duration_text) {\n"
        "    char *end = NULL;\n"
        "    double seconds = 0.0;\n"
        "    long millis = 0;\n"
        "    if (!duration_text || duration_text[0] == '\\0') return true;\n"
        "    seconds = strtod(duration_text, &end);\n"
        "    if (!end || *end != '\\0' || seconds < 0.0) {\n"
        "        nob_log(NOB_ERROR, \"test: invalid ctest_sleep duration '%s'\", duration_text);\n"
        "        return false;\n"
        "    }\n"
        "    millis = (long)(seconds * 1000.0 + 0.5);\n"
        "#if defined(_WIN32)\n"
        "    Sleep((DWORD)millis);\n"
        "#else\n"
        "    usleep((useconds_t)millis * 1000u);\n"
        "#endif\n"
        "    return true;\n"
        "}\n\n"
        "static bool __attribute__((unused)) run_test_driver_replay(const char *const *selected_names, size_t selected_count, const char *config_filter) {\n"
        "    (void)selected_names;\n"
        "    (void)selected_count;\n"
        "    (void)config_filter;\n");

    if (test_replay_count == 0) {
        nob_sb_append_cstr(out, "    return true;\n");
    } else {
        for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
            BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
            BM_Replay_Opcode opcode = bm_query_replay_action_opcode(ctx->model, id);
            BM_String_Span inputs = {0};
            BM_String_Span outputs = {0};
            BM_String_Span argv = {0};
            if (bm_query_replay_action_phase(ctx->model, id) != BM_REPLAY_PHASE_TEST) continue;
            inputs = bm_query_replay_action_inputs(ctx->model, id);
            outputs = bm_query_replay_action_outputs(ctx->model, id);
            argv = bm_query_replay_action_argv(ctx->model, id);
            nob_sb_append_cstr(out, "    {\n");
            for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
                String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
                String_View *resolved_inputs = NULL;
                String_View *resolved_outputs = NULL;
                String_View *resolved_argv = NULL;
                if (!cg_resolve_replay_span_for_config(ctx, config, inputs, &resolved_inputs) ||
                    !cg_resolve_replay_span_for_config(ctx, config, outputs, &resolved_outputs) ||
                    !cg_resolve_replay_span_for_config(ctx, config, argv, &resolved_argv) ||
                    !cg_emit_runtime_config_branches_prefix_for_var(ctx, out, branch, "config_filter")) {
                    nob_log(NOB_ERROR,
                            "codegen: failed to prepare test-driver replay action %u for config `%s`",
                            (unsigned)id,
                            nob_temp_sv_to_cstr(config));
                    return false;
                }
                switch (opcode) {
                    case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY: {
                        String_View target_dir = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &target_dir)) return false;
                        nob_sb_append_cstr(out, "        if (!ctest_execute_empty_binary_directory(");
                        if (!cg_sb_append_c_string(out, target_dir)) return false;
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    }
                    case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL: {
                        String_View source_dir = {0};
                        String_View build_dir = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &source_dir) ||
                            !cg_rebase_path_from_cwd(ctx, resolved_outputs[1], &build_dir)) {
                            return false;
                        }
                        nob_sb_append_cstr(out, "        if (!ctest_execute_start_local(");
                        if (!cg_sb_append_c_string(out, source_dir)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, build_dir)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[1])) return false;
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, cg_sv_eq_lit(resolved_argv[2], "1") ? "true" : "false");
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    }
                    case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF: {
                        String_View source_dir = {0};
                        String_View build_dir = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &source_dir) ||
                            !cg_rebase_path_from_cwd(ctx, resolved_outputs[1], &build_dir)) {
                            return false;
                        }
                        nob_sb_append_cstr(out, "        if (!ctest_execute_configure_self(");
                        if (!cg_sb_append_c_string(out, source_dir)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, build_dir)) return false;
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    }
                    case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF: {
                        String_View build_dir = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &build_dir)) return false;
                        nob_sb_append_cstr(out, "        if (!ctest_execute_build_self(");
                        if (!cg_sb_append_c_string(out, build_dir)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[1])) return false;
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    }
                    case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST: {
                        String_View build_dir = {0};
                        String_View output_junit = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &build_dir)) return false;
                        if (resolved_argv[0].count > 0 &&
                            !cg_rebase_path_from_cwd(ctx, resolved_argv[0], &output_junit)) {
                            return false;
                        }
                        nob_sb_append_cstr(out, "        if (!ctest_execute_test(");
                        if (!cg_sb_append_c_string(out, build_dir)) return false;
                        nob_sb_append_cstr(out, ", selected_names, selected_count, config_filter, ");
                        if (resolved_argv[0].count > 0) {
                            if (!cg_sb_append_c_string(out, output_junit)) return false;
                        } else {
                            nob_sb_append_cstr(out, "\"\"");
                        }
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, cg_sv_eq_lit(resolved_argv[1], "1") ? "true" : "false");
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    }
                    case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP:
                        nob_sb_append_cstr(out, "        if (!ctest_execute_sleep(");
                        if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL: {
                        String_View build_dir = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &build_dir)) {
                            nob_log(NOB_ERROR,
                                    "codegen: failed to rebase ctest coverage build dir for replay action %u: `%s`",
                                    (unsigned)id,
                                    nob_temp_sv_to_cstr(resolved_outputs[0]));
                            return false;
                        }
                        nob_sb_append_cstr(out, "        if (!ctest_execute_coverage_local(");
                        if (!cg_sb_append_c_string(out, build_dir)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[1])) return false;
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, cg_sv_eq_lit(resolved_argv[2], "1") ? "true" : "false");
                        nob_sb_append_cstr(out, ", ");
                        if (inputs.count > 0) {
                            nob_sb_append_cstr(out, "(const char *const[]){");
                            for (size_t i = 0; i < inputs.count; ++i) {
                                String_View path = {0};
                                if (!cg_rebase_path_from_cwd(ctx, resolved_inputs[i], &path)) {
                                    nob_log(NOB_ERROR,
                                            "codegen: failed to rebase ctest coverage input %zu for replay action %u: `%s`",
                                            i,
                                            (unsigned)id,
                                            nob_temp_sv_to_cstr(resolved_inputs[i]));
                                    return false;
                                }
                                if (i > 0) nob_sb_append_cstr(out, ", ");
                                if (!cg_sb_append_c_string(out, path)) return false;
                            }
                            nob_sb_append_cstr(out, "}");
                        } else {
                            nob_sb_append_cstr(out, "NULL");
                        }
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", inputs.count));
                        nob_sb_append_cstr(out, ", ");
                        if (argv.count > 4) {
                            nob_sb_append_cstr(out, "(const char *const[]){");
                            for (size_t i = 4; i < argv.count; ++i) {
                                if (i > 4) nob_sb_append_cstr(out, ", ");
                                if (!cg_sb_append_c_string(out, resolved_argv[i])) return false;
                            }
                            nob_sb_append_cstr(out, "}");
                        } else {
                            nob_sb_append_cstr(out, "NULL");
                        }
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", argv.count > 4 ? argv.count - 4 : 0));
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    }
                    case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL: {
                        String_View build_dir = {0};
                        String_View output_junit = {0};
                        String_View resource_spec = {0};
                        String_View suppression_file = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &build_dir)) {
                            nob_log(NOB_ERROR,
                                    "codegen: failed to rebase ctest memcheck build dir for replay action %u: `%s`",
                                    (unsigned)id,
                                    nob_temp_sv_to_cstr(resolved_outputs[0]));
                            return false;
                        }
                        if (resolved_outputs[1].count > 0 &&
                            !cg_rebase_path_from_cwd(ctx, resolved_outputs[1], &output_junit)) {
                            nob_log(NOB_ERROR,
                                    "codegen: failed to rebase ctest memcheck junit output for replay action %u: `%s`",
                                    (unsigned)id,
                                    nob_temp_sv_to_cstr(resolved_outputs[1]));
                            return false;
                        }
                        if (resolved_inputs[0].count > 0 &&
                            !cg_rebase_path_from_cwd(ctx, resolved_inputs[0], &resource_spec)) {
                            nob_log(NOB_ERROR,
                                    "codegen: failed to rebase ctest memcheck resource spec for replay action %u: `%s`",
                                    (unsigned)id,
                                    nob_temp_sv_to_cstr(resolved_inputs[0]));
                            return false;
                        }
                        if (resolved_inputs[1].count > 0 &&
                            !cg_rebase_path_from_cwd(ctx, resolved_inputs[1], &suppression_file)) {
                            nob_log(NOB_ERROR,
                                    "codegen: failed to rebase ctest memcheck suppression file for replay action %u: `%s`",
                                    (unsigned)id,
                                    nob_temp_sv_to_cstr(resolved_inputs[1]));
                            return false;
                        }
                        nob_sb_append_cstr(out, "        if (!ctest_execute_memcheck_local(");
                        if (!cg_sb_append_c_string(out, build_dir)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (resolved_outputs[1].count > 0) {
                            if (!cg_sb_append_c_string(out, output_junit)) return false;
                        } else {
                            nob_sb_append_cstr(out, "\"\"");
                        }
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[1])) return false;
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, cg_sv_eq_lit(resolved_argv[2], "1") ? "true" : "false");
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[3])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[4])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[5])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[7])) return false;
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, cg_sv_eq_lit(resolved_argv[8], "1") ? "true" : "false");
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, cg_sv_eq_lit(resolved_argv[9], "1") ? "true" : "false");
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[10])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[12])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (resolved_inputs[0].count > 0) {
                            if (!cg_sb_append_c_string(out, resource_spec)) return false;
                        } else {
                            nob_sb_append_cstr(out, "\"\"");
                        }
                        nob_sb_append_cstr(out, ", ");
                        if (resolved_inputs[1].count > 0) {
                            if (!cg_sb_append_c_string(out, suppression_file)) return false;
                        } else {
                            nob_sb_append_cstr(out, "\"\"");
                        }
                        nob_sb_append_cstr(out, ", ");
                        if (argv.count > 14) {
                            nob_sb_append_cstr(out, "(const char *const[]){");
                            for (size_t i = 14; i < argv.count; ++i) {
                                if (i > 14) nob_sb_append_cstr(out, ", ");
                                if (!cg_sb_append_c_string(out, resolved_argv[i])) return false;
                            }
                            nob_sb_append_cstr(out, "}");
                        } else {
                            nob_sb_append_cstr(out, "NULL");
                        }
                        nob_sb_append_cstr(out, ", ");
                        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", argv.count > 14 ? argv.count - 14 : 0));
                        nob_sb_append_cstr(out, ", selected_names, selected_count, config_filter)) return false;\n");
                        break;
                    }

                    case BM_REPLAY_OPCODE_FS_MKDIR:
                        for (size_t output_index = 0; output_index < outputs.count; ++output_index) {
                            String_View path = {0};
                            if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[output_index], &path)) return false;
                            nob_sb_append_cstr(out, "        if (!ensure_dir(");
                            if (!cg_sb_append_c_string(out, path)) return false;
                            nob_sb_append_cstr(out, ")) return false;\n");
                        }
                        break;

                    case BM_REPLAY_OPCODE_FS_WRITE_TEXT: {
                        String_View output_path = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &output_path)) return false;
                        nob_sb_append_cstr(out, "        if (!replay_write_text(");
                        if (!cg_sb_append_c_string(out, output_path)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[1])) return false;
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    }

                    case BM_REPLAY_OPCODE_FS_APPEND_TEXT: {
                        String_View output_path = {0};
                        if (!cg_rebase_path_from_cwd(ctx, resolved_outputs[0], &output_path)) return false;
                        nob_sb_append_cstr(out, "        if (!replay_append_text(");
                        if (!cg_sb_append_c_string(out, output_path)) return false;
                        nob_sb_append_cstr(out, ", ");
                        if (!cg_sb_append_c_string(out, resolved_argv[0])) return false;
                        nob_sb_append_cstr(out, ")) return false;\n");
                        break;
                    }

                    case BM_REPLAY_OPCODE_NONE:
                        break;
                    case BM_REPLAY_OPCODE_FS_COPY_FILE:
                    case BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL:
                    case BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR:
                    case BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR:
                    case BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE:
                    case BM_REPLAY_OPCODE_HOST_LOCK_RELEASE:
                    case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE:
                    case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT:
                    case BM_REPLAY_OPCODE_PROBE_TRY_RUN:
                    case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR:
                    case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE:
                        nob_sb_append_cstr(out, "        nob_log(NOB_ERROR, \"test: unsupported replay opcode reached test-driver runner\");\n");
                        nob_sb_append_cstr(out, "        return false;\n");
                        break;
                }
            }
            if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
            nob_sb_append_cstr(out, "    }\n");
        }
        nob_sb_append_cstr(out, "    return true;\n");
    }

    nob_sb_append_cstr(out,
        "}\n\n"
        "static bool run_test_phase(const char *const *selected_names, size_t selected_count, const char *config_filter) {\n"
        "    if (!build_selected_test_targets(selected_names, selected_count, config_filter)) return false;\n");
    if (test_driver_count > 0) {
        nob_sb_append_cstr(out,
            "    if (!run_test_driver_replay(selected_names, selected_count, config_filter)) return false;\n"
            "    return true;\n");
    } else if (test_replay_count > 0) {
        nob_sb_append_cstr(out,
            "    if (!run_registered_tests(true,\n"
            "                               selected_names,\n"
            "                               selected_count,\n"
            "                               config_filter,\n"
            "                               NULL,\n"
            "                               NULL,\n"
            "                               false,\n"
            "                               false,\n"
            "                               false,\n"
            "                               false)) return false;\n"
            "    if (!run_test_driver_replay(selected_names, selected_count, config_filter)) return false;\n"
            "    return true;\n");
    } else {
        nob_sb_append_cstr(out,
            "    return run_registered_tests(true,\n"
            "                               selected_names,\n"
            "                               selected_count,\n"
            "                               config_filter,\n"
            "                               NULL,\n"
            "                               NULL,\n"
            "                               false,\n"
            "                               false);\n");
    }
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}
