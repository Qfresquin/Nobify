#include "eval_string_internal.h"

#include "eval_hash.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    size_t length;
    String_View alphabet;
    bool has_seed;
    unsigned long long seed;
} String_Random_Options;

static uint32_t string_load_le32(const unsigned char *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t string_load_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static void string_store_le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void string_store_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)((v >> 24) & 0xFF);
    p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8) & 0xFF);
    p[3] = (unsigned char)(v & 0xFF);
}

static uint32_t string_rotl32(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

static void string_md5_process_block(uint32_t state[4], const unsigned char block[64]) {
    static const uint32_t k[64] = {
        0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
        0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
        0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
        0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
        0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
        0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
        0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
        0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U, 0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
    };
    static const uint32_t s[64] = {
        7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
        5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U,
        4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
        6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U
    };

    uint32_t m[16];
    for (size_t i = 0; i < 16; i++) m[i] = string_load_le32(block + (i * 4));

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    for (size_t i = 0; i < 64; i++) {
        uint32_t f = 0;
        uint32_t g = 0;
        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = (uint32_t)i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (uint32_t)((5 * i + 1) % 16);
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (uint32_t)((3 * i + 5) % 16);
        } else {
            f = c ^ (b | (~d));
            g = (uint32_t)((7 * i) % 16);
        }
        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + string_rotl32(a + f + k[i] + m[g], s[i]);
        a = tmp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void string_md5_compute(const unsigned char *msg, size_t len, unsigned char out[16]) {
    uint32_t state[4] = { 0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U };

    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) {
        string_md5_process_block(state, msg + (i * 64));
    }

    unsigned char tail[128] = {0};
    size_t rem = len % 64;
    if (rem > 0) memcpy(tail, msg + (full * 64), rem);
    tail[rem] = 0x80;
    size_t tail_len = (rem < 56) ? 64 : 128;

    uint64_t bits = ((uint64_t)len) * 8U;
    for (size_t i = 0; i < 8; i++) {
        tail[tail_len - 8 + i] = (unsigned char)((bits >> (8 * i)) & 0xFFU);
    }

    string_md5_process_block(state, tail);
    if (tail_len == 128) string_md5_process_block(state, tail + 64);

    string_store_le32(out + 0, state[0]);
    string_store_le32(out + 4, state[1]);
    string_store_le32(out + 8, state[2]);
    string_store_le32(out + 12, state[3]);
}

static void string_sha1_process_block(uint32_t state[5], const unsigned char block[64]) {
    uint32_t w[80];
    for (size_t i = 0; i < 16; i++) w[i] = string_load_be32(block + (i * 4));
    for (size_t i = 16; i < 80; i++) {
        w[i] = string_rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (size_t i = 0; i < 80; i++) {
        uint32_t f = 0;
        uint32_t k = 0;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999U;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1U;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcU;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6U;
        }
        uint32_t temp = string_rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = string_rotl32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void string_sha1_compute(const unsigned char *msg, size_t len, unsigned char out[20]) {
    uint32_t state[5] = {
        0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U, 0xc3d2e1f0U
    };

    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) {
        string_sha1_process_block(state, msg + (i * 64));
    }

    unsigned char tail[128] = {0};
    size_t rem = len % 64;
    if (rem > 0) memcpy(tail, msg + (full * 64), rem);
    tail[rem] = 0x80;
    size_t tail_len = (rem < 56) ? 64 : 128;

    uint64_t bits = ((uint64_t)len) * 8U;
    for (size_t i = 0; i < 8; i++) {
        tail[tail_len - 1 - i] = (unsigned char)((bits >> (8 * i)) & 0xFFU);
    }

    string_sha1_process_block(state, tail);
    if (tail_len == 128) string_sha1_process_block(state, tail + 64);

    for (size_t i = 0; i < 5; i++) string_store_be32(out + (i * 4), state[i]);
}

static bool string_parse_uuid_bytes(String_View in, unsigned char out[16]) {
    if (!out || in.count != 36) return false;
    if (in.data[8] != '-' || in.data[13] != '-' || in.data[18] != '-' || in.data[23] != '-') return false;

    size_t bi = 0;
    for (size_t i = 0; i < in.count;) {
        if (in.data[i] == '-') {
            i++;
            continue;
        }
        if (i + 1 >= in.count || bi >= 16) return false;
        int hi = eval_string_hex_nibble(in.data[i]);
        int lo = eval_string_hex_nibble(in.data[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[bi++] = (unsigned char)((hi << 4) | lo);
        i += 2;
    }
    return bi == 16;
}

static String_View string_format_uuid_temp(Evaluator_Context *ctx, const unsigned char uuid[16], bool upper) {
    if (!ctx || !uuid) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), 37);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    const char *lut = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t k = 0;
    for (size_t i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) buf[k++] = '-';
        unsigned char b = uuid[i];
        buf[k++] = lut[(b >> 4) & 0x0F];
        buf[k++] = lut[b & 0x0F];
    }
    buf[36] = '\0';
    return nob_sv_from_cstr(buf);
}

static uint64_t string_rng_next_u64(uint64_t *state) {
    uint64_t x = *state;
    if (x == 0) x = 0x9e3779b97f4a7c15ULL;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 2685821657736338717ULL;
}

static bool string_time_from_epoch(time_t t, bool utc, struct tm *out_tm) {
    if (!out_tm) return false;
#if defined(_WIN32)
    errno_t rc = utc ? gmtime_s(out_tm, &t) : localtime_s(out_tm, &t);
    return rc == 0;
#else
    struct tm *r = utc ? gmtime_r(&t, out_tm) : localtime_r(&t, out_tm);
    return r != NULL;
#endif
}

static String_View string_timestamp_format_temp(Evaluator_Context *ctx,
                                                String_View format,
                                                bool utc) {
    if (!ctx) return nob_sv_from_cstr("");
    if (format.count == 0) format = nob_sv_from_cstr("%Y-%m-%dT%H:%M:%S");

    time_t now = time(NULL);
    const char *sde = getenv("SOURCE_DATE_EPOCH");
    if (sde && *sde) {
        char *end = NULL;
        errno = 0;
        long long sec = strtoll(sde, &end, 10);
        if (errno == 0 && end && *end == '\0') now = (time_t)sec;
    }

    struct tm tmv = {0};
    if (!string_time_from_epoch(now, utc, &tmv)) return nob_sv_from_cstr("");

    char *fmt = eval_sv_to_cstr_temp(ctx, format);
    if (!fmt) return nob_sv_from_cstr("");

    size_t cap = format.count + 128;
    if (cap < 64) cap = 64;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), cap + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t n = strftime(buf, cap + 1, fmt, &tmv);
    if (n == 0) {
        buf[0] = '\0';
        return nob_sv_from_cstr(buf);
    }
    return nob_sv_from_parts(buf, n);
}

static bool string_random_generate_temp(Evaluator_Context *ctx,
                                        const String_Random_Options *opt,
                                        String_View *out) {
    if (!ctx || !opt || !out) return false;
    *out = nob_sv_from_cstr("");
    if (opt->length == 0 || opt->alphabet.count == 0) return false;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), opt->length + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    uint64_t state = opt->has_seed
        ? (uint64_t)opt->seed
        : (((uint64_t)time(NULL) << 32) ^ (uint64_t)(uintptr_t)ctx ^ 0xa5a5a5a55a5a5a5aULL);
    if (state == 0) state = 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < opt->length; i++) {
        uint64_t r = string_rng_next_u64(&state);
        size_t idx = (size_t)(r % (uint64_t)opt->alphabet.count);
        buf[i] = opt->alphabet.data[idx];
    }
    buf[opt->length] = '\0';
    *out = nob_sv_from_cstr(buf);
    return true;
}

static bool string_uuid_name_based_temp(Evaluator_Context *ctx,
                                        String_View ns_sv,
                                        String_View name,
                                        String_View type,
                                        bool upper,
                                        String_View *out_uuid) {
    if (!ctx || !out_uuid) return false;
    *out_uuid = nob_sv_from_cstr("");

    unsigned char ns[16];
    if (!string_parse_uuid_bytes(ns_sv, ns)) return false;

    size_t payload_len = 16 + name.count;
    unsigned char *payload = (unsigned char*)arena_alloc(eval_temp_arena(ctx), payload_len ? payload_len : 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, payload, false);
    memcpy(payload, ns, 16);
    if (name.count > 0) memcpy(payload + 16, name.data, name.count);

    unsigned char uuid[16] = {0};
    if (eval_sv_eq_ci_lit(type, "MD5")) {
        unsigned char d[16];
        string_md5_compute(payload, payload_len, d);
        memcpy(uuid, d, 16);
        uuid[6] = (unsigned char)((uuid[6] & 0x0F) | 0x30);
    } else if (eval_sv_eq_ci_lit(type, "SHA1")) {
        unsigned char d[20];
        string_sha1_compute(payload, payload_len, d);
        memcpy(uuid, d, 16);
        uuid[6] = (unsigned char)((uuid[6] & 0x0F) | 0x50);
    } else {
        return false;
    }

    uuid[8] = (unsigned char)((uuid[8] & 0x3F) | 0x80);
    *out_uuid = string_format_uuid_temp(ctx, uuid, upper);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

Eval_Result eval_string_handle_misc(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (!ctx || !node || arena_arr_len(a) < 1 || eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (eval_sv_eq_ci_lit(a[0], "RANDOM")) {
        if (arena_arr_len(a) < 2) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(RANDOM) requires output variable"), nob_sv_from_cstr("Usage: string(RANDOM [LENGTH <n>] [ALPHABET <chars>] [RANDOM_SEED <seed>] <out-var>)"));
            return eval_result_from_ctx(ctx);
        }

        String_Random_Options opt = {
            .length = 5,
            .alphabet = nob_sv_from_cstr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"),
            .has_seed = false,
            .seed = 0,
        };
        String_View out_var = a[arena_arr_len(a) - 1];
        for (size_t i = 1; i + 1 < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "LENGTH")) {
                unsigned long long v = 0;
                if (i + 1 >= arena_arr_len(a) - 1 || !eval_string_parse_u64_sv(a[i + 1], &v) || v == 0) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(RANDOM LENGTH) expects integer > 0"), i + 1 < arena_arr_len(a) ? a[i + 1] : nob_sv_from_cstr(""));
                    return eval_result_from_ctx(ctx);
                }
                opt.length = (size_t)v;
                i++;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "ALPHABET")) {
                if (i + 1 >= arena_arr_len(a) - 1 || a[i + 1].count == 0) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(RANDOM ALPHABET) expects non-empty alphabet"), nob_sv_from_cstr(""));
                    return eval_result_from_ctx(ctx);
                }
                opt.alphabet = a[i + 1];
                i++;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "RANDOM_SEED")) {
                unsigned long long seed = 0;
                if (i + 1 >= arena_arr_len(a) - 1 || !eval_string_parse_u64_sv(a[i + 1], &seed)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(RANDOM RANDOM_SEED) expects unsigned integer"), i + 1 < arena_arr_len(a) ? a[i + 1] : nob_sv_from_cstr(""));
                    return eval_result_from_ctx(ctx);
                }
                opt.has_seed = true;
                opt.seed = seed;
                i++;
                continue;
            }
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("string(RANDOM) received unsupported option"), a[i]);
            return eval_result_from_ctx(ctx);
        }

        String_View rnd = nob_sv_from_cstr("");
        if (!string_random_generate_temp(ctx, &opt, &rnd)) return eval_result_from_ctx(ctx);
        (void)eval_var_set_current(ctx, out_var, rnd);
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "TIMESTAMP")) {
        if (arena_arr_len(a) < 2 || arena_arr_len(a) > 4) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(TIMESTAMP) expects output variable with optional format and UTC"), nob_sv_from_cstr("Usage: string(TIMESTAMP <out-var> [format] [UTC])"));
            return eval_result_from_ctx(ctx);
        }
        String_View out_var = a[1];
        String_View fmt = nob_sv_from_cstr("%Y-%m-%dT%H:%M:%S");
        bool has_fmt = false;
        bool utc = false;
        for (size_t i = 2; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "UTC")) {
                utc = true;
                continue;
            }
            if (!has_fmt) {
                fmt = a[i];
                has_fmt = true;
                continue;
            }
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("string(TIMESTAMP) received unsupported option"), a[i]);
            return eval_result_from_ctx(ctx);
        }
        (void)eval_var_set_current(ctx, out_var, string_timestamp_format_temp(ctx, fmt, utc));
        if (!eval_emit_string_timestamp(ctx, o, out_var)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "UUID")) {
        if (arena_arr_len(a) < 8) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(UUID) requires NAMESPACE, NAME and TYPE"), nob_sv_from_cstr("Usage: string(UUID <out-var> NAMESPACE <uuid> NAME <name> TYPE <MD5|SHA1> [UPPER])"));
            return eval_result_from_ctx(ctx);
        }
        String_View out_var = a[1];
        String_View ns = nob_sv_from_cstr("");
        String_View name = nob_sv_from_cstr("");
        String_View type = nob_sv_from_cstr("");
        bool has_ns = false, has_name = false, has_type = false, upper = false;
        for (size_t i = 2; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "UPPER")) {
                upper = true;
                continue;
            }
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(UUID) option is missing value"), a[i]);
                return eval_result_from_ctx(ctx);
            }
            if (eval_sv_eq_ci_lit(a[i], "NAMESPACE")) {
                ns = a[++i];
                has_ns = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "NAME")) {
                name = a[++i];
                has_name = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "TYPE")) {
                type = a[++i];
                has_type = true;
                continue;
            }
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("string(UUID) received unsupported option"), a[i]);
            return eval_result_from_ctx(ctx);
        }
        if (!has_ns || !has_name || !has_type) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(UUID) requires NAMESPACE, NAME and TYPE"), nob_sv_from_cstr("Usage: string(UUID <out-var> NAMESPACE <uuid> NAME <name> TYPE <MD5|SHA1> [UPPER])"));
            return eval_result_from_ctx(ctx);
        }
        String_View uuid_sv = nob_sv_from_cstr("");
        if (!string_uuid_name_based_temp(ctx, ns, name, type, upper, &uuid_sv)) {
            if (!string_parse_uuid_bytes(ns, (unsigned char[16]){0})) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "string", nob_sv_from_cstr("string(UUID) malformed NAMESPACE UUID"), ns);
            } else {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("string(UUID) unsupported TYPE"), type);
            }
            return eval_result_from_ctx(ctx);
        }
        (void)eval_var_set_current(ctx, out_var, uuid_sv);
        return eval_result_from_ctx(ctx);
    }

    if (eval_string_is_hash_command(a[0])) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(<HASH>) requires output variable and input"), nob_sv_from_cstr("Usage: string(<HASH> <out-var> <input>)"));
            return eval_result_from_ctx(ctx);
        }
        String_View hash = nob_sv_from_cstr("");
        if (!eval_hash_compute_hex_temp(ctx, a[0], a[2], &hash)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "string", nob_sv_from_cstr("Unsupported hash algorithm"), a[0]);
            return eval_result_from_ctx(ctx);
        }
        (void)eval_var_set_current(ctx, a[1], hash);
        if (!eval_emit_string_hash(ctx, o, a[0], a[1])) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}
