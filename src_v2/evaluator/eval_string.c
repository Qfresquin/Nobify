#include "eval_stdlib.h"

#include "evaluator_internal.h"
#include "eval_hash.h"
#include "arena_dyn.h"
#include "sv_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <pcre2posix.h>

static bool sv_parse_i64(String_View sv, long long *out) {
    if (!out || sv.count == 0) return false;
    char buf[64];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    long long v = strtoll(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out = v;
    return true;
}

static String_View list_strip_ws_view(String_View in) {
    size_t b = 0;
    while (b < in.count && isspace((unsigned char)in.data[b])) b++;

    size_t e = in.count;
    while (e > b && isspace((unsigned char)in.data[e - 1])) e--;

    return nob_sv_from_parts(in.data + b, e - b);
}

typedef struct {
    size_t length;
    String_View alphabet;
    bool has_seed;
    unsigned long long seed;
} String_Random_Options;

static String_View string_join_no_sep_temp(Evaluator_Context *ctx, String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    return svu_join_no_sep_temp(ctx, items, count);
}

static int string_sv_cmp(String_View a, String_View b) {
    size_t n = a.count < b.count ? a.count : b.count;
    if (n > 0) {
        int c = memcmp(a.data, b.data, n);
        if (c != 0) return c < 0 ? -1 : 1;
    }
    if (a.count < b.count) return -1;
    if (a.count > b.count) return 1;
    return 0;
}

static bool string_parse_u64_sv(String_View sv, unsigned long long *out) {
    if (!out || sv.count == 0) return false;
    char buf[128];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(buf, &end, 10);
    if (errno == ERANGE || !end || *end != '\0') return false;
    *out = v;
    return true;
}

static bool string_append_sv(Nob_String_Builder *sb, String_View sv) {
    if (!sb) return false;
    if (sv.count > 0) nob_sb_append_buf(sb, sv.data, sv.count);
    return true;
}

static bool string_append_sv_escaped_quotes(Nob_String_Builder *sb, String_View sv, bool escape_quotes) {
    if (!sb) return false;
    if (!escape_quotes) return string_append_sv(sb, sv);
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '"') nob_sb_append(sb, '\\');
        nob_sb_append(sb, c);
    }
    return true;
}

static bool string_configure_expand_temp(Evaluator_Context *ctx,
                                         String_View input,
                                         bool at_only,
                                         bool escape_quotes,
                                         String_View *out) {
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    Nob_String_Builder sb = {0};
    for (size_t i = 0; i < input.count;) {
        if (input.data[i] == '@') {
            size_t j = i + 1;
            while (j < input.count && input.data[j] != '@') j++;
            if (j < input.count && j > i + 1) {
                String_View key = nob_sv_from_parts(input.data + i + 1, j - (i + 1));
                String_View val = eval_var_get(ctx, key);
                (void)string_append_sv_escaped_quotes(&sb, val, escape_quotes);
                i = j + 1;
                continue;
            }
        }

        if (!at_only && input.data[i] == '$' && i + 1 < input.count && input.data[i + 1] == '{') {
            size_t j = i + 2;
            while (j < input.count && input.data[j] != '}') j++;
            if (j < input.count) {
                String_View key = nob_sv_from_parts(input.data + i + 2, j - (i + 2));
                String_View val = nob_sv_from_cstr("");
                if (key.count > 5 && memcmp(key.data, "ENV{", 4) == 0 && key.data[key.count - 1] == '}') {
                    String_View env_key = nob_sv_from_parts(key.data + 4, key.count - 5);
                    char *env_buf = (char*)arena_alloc(eval_temp_arena(ctx), env_key.count + 1);
                    EVAL_OOM_RETURN_IF_NULL(ctx, env_buf, false);
                    memcpy(env_buf, env_key.data, env_key.count);
                    env_buf[env_key.count] = '\0';
                    const char *env_val = getenv(env_buf);
                    if (env_val) val = nob_sv_from_cstr(env_val);
                } else {
                    val = eval_var_get(ctx, key);
                }
                (void)string_append_sv_escaped_quotes(&sb, val, escape_quotes);
                i = j + 1;
                continue;
            }
        }

        nob_sb_append(&sb, input.data[i]);
        i++;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
    if (!buf) {
        nob_sb_free(sb);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    }
    if (sb.count > 0) memcpy(buf, sb.items, sb.count);
    buf[sb.count] = '\0';
    nob_sb_free(sb);
    *out = nob_sv_from_cstr(buf);
    return true;
}

static String_View string_genex_strip_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t out = 0;
    for (size_t i = 0; i < in.count; i++) {
        if (i + 1 < in.count && in.data[i] == '$' && in.data[i + 1] == '<') {
            size_t j = i + 2;
            size_t depth = 1;
            while (j < in.count && depth > 0) {
                if (j + 1 < in.count && in.data[j] == '$' && in.data[j + 1] == '<') {
                    depth++;
                    j += 2;
                    continue;
                }
                if (in.data[j] == '>') {
                    depth--;
                    j++;
                    continue;
                }
                j++;
            }
            if (j == 0) break;
            i = j - 1;
            continue;
        }
        buf[out++] = in.data[i];
    }

    buf[out] = '\0';
    return nob_sv_from_cstr(buf);
}

static long long string_find_substr(String_View hay, String_View needle, bool reverse) {
    if (needle.count == 0) return reverse ? (long long)hay.count : 0;
    if (needle.count > hay.count) return -1;

    if (!reverse) {
        for (size_t i = 0; i + needle.count <= hay.count; i++) {
            if (memcmp(hay.data + i, needle.data, needle.count) == 0) return (long long)i;
        }
        return -1;
    }

    for (size_t i = hay.count - needle.count + 1; i > 0; i--) {
        size_t at = i - 1;
        if (memcmp(hay.data + at, needle.data, needle.count) == 0) return (long long)at;
    }
    return -1;
}

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

static uint32_t string_rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static uint64_t string_load_be64(const unsigned char *p) {
    return ((uint64_t)p[0] << 56) |
           ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) |
           ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) |
           ((uint64_t)p[7]);
}

static void string_store_be64(unsigned char *p, uint64_t v) {
    p[0] = (unsigned char)((v >> 56) & 0xFF);
    p[1] = (unsigned char)((v >> 48) & 0xFF);
    p[2] = (unsigned char)((v >> 40) & 0xFF);
    p[3] = (unsigned char)((v >> 32) & 0xFF);
    p[4] = (unsigned char)((v >> 24) & 0xFF);
    p[5] = (unsigned char)((v >> 16) & 0xFF);
    p[6] = (unsigned char)((v >> 8) & 0xFF);
    p[7] = (unsigned char)(v & 0xFF);
}

static uint64_t string_rotl64(uint64_t x, uint32_t n) {
    return (x << n) | (x >> (64 - n));
}

static uint64_t string_rotr64(uint64_t x, uint32_t n) {
    return (x >> n) | (x << (64 - n));
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

static void string_sha256_process_block(uint32_t state[8], const unsigned char block[64]) {
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

    uint32_t w[64];
    for (size_t i = 0; i < 16; i++) w[i] = string_load_be32(block + (i * 4));
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = string_rotr32(w[i - 15], 7) ^ string_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = string_rotr32(w[i - 2], 17) ^ string_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
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
        uint32_t S1 = string_rotr32(e, 6) ^ string_rotr32(e, 11) ^ string_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + k[i] + w[i];
        uint32_t S0 = string_rotr32(a, 2) ^ string_rotr32(a, 13) ^ string_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

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

static void string_sha256_compute_state(const unsigned char *msg,
                                        size_t len,
                                        const uint32_t init_state[8],
                                        uint32_t out_state[8]) {
    uint32_t state[8];
    for (size_t i = 0; i < 8; i++) state[i] = init_state[i];
    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) {
        string_sha256_process_block(state, msg + (i * 64));
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

    string_sha256_process_block(state, tail);
    if (tail_len == 128) string_sha256_process_block(state, tail + 64);

    for (size_t i = 0; i < 8; i++) out_state[i] = state[i];
}

static void string_sha256_compute(const unsigned char *msg, size_t len, unsigned char out[32]) {
    static const uint32_t k_init[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    uint32_t state[8];
    string_sha256_compute_state(msg, len, k_init, state);
    for (size_t i = 0; i < 8; i++) string_store_be32(out + (i * 4), state[i]);
}

static void string_sha224_compute(const unsigned char *msg, size_t len, unsigned char out[28]) {
    static const uint32_t k_init[8] = {
        0xc1059ed8U, 0x367cd507U, 0x3070dd17U, 0xf70e5939U,
        0xffc00b31U, 0x68581511U, 0x64f98fa7U, 0xbefa4fa4U
    };
    uint32_t state[8];
    string_sha256_compute_state(msg, len, k_init, state);
    for (size_t i = 0; i < 7; i++) string_store_be32(out + (i * 4), state[i]);
}

static void string_sha512_process_block(uint64_t state[8], const unsigned char block[128]) {
    static const uint64_t k[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
    };

    uint64_t w[80];
    for (size_t i = 0; i < 16; i++) w[i] = string_load_be64(block + (i * 8));
    for (size_t i = 16; i < 80; i++) {
        uint64_t s0 = string_rotr64(w[i - 15], 1) ^ string_rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = string_rotr64(w[i - 2], 19) ^ string_rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint64_t a = state[0];
    uint64_t b = state[1];
    uint64_t c = state[2];
    uint64_t d = state[3];
    uint64_t e = state[4];
    uint64_t f = state[5];
    uint64_t g = state[6];
    uint64_t h = state[7];

    for (size_t i = 0; i < 80; i++) {
        uint64_t S1 = string_rotr64(e, 14) ^ string_rotr64(e, 18) ^ string_rotr64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t temp1 = h + S1 + ch + k[i] + w[i];
        uint64_t S0 = string_rotr64(a, 28) ^ string_rotr64(a, 34) ^ string_rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp2 = S0 + maj;

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

static void string_sha512_compute_state(const unsigned char *msg,
                                        size_t len,
                                        const uint64_t init_state[8],
                                        uint64_t out_state[8]) {
    uint64_t state[8];
    for (size_t i = 0; i < 8; i++) state[i] = init_state[i];

    size_t full = len / 128;
    for (size_t i = 0; i < full; i++) {
        string_sha512_process_block(state, msg + (i * 128));
    }

    unsigned char tail[256] = {0};
    size_t rem = len % 128;
    if (rem > 0) memcpy(tail, msg + (full * 128), rem);
    tail[rem] = 0x80;
    size_t tail_len = (rem < 112) ? 128 : 256;

    uint64_t bits_lo = ((uint64_t)len) * 8ULL;
    uint64_t bits_hi = ((uint64_t)len >> 61);
    string_store_be64(tail + tail_len - 16, bits_hi);
    string_store_be64(tail + tail_len - 8, bits_lo);

    string_sha512_process_block(state, tail);
    if (tail_len == 256) string_sha512_process_block(state, tail + 128);

    for (size_t i = 0; i < 8; i++) out_state[i] = state[i];
}

static void string_sha512_compute(const unsigned char *msg, size_t len, unsigned char out[64]) {
    static const uint64_t k_init[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };
    uint64_t state[8];
    string_sha512_compute_state(msg, len, k_init, state);
    for (size_t i = 0; i < 8; i++) string_store_be64(out + (i * 8), state[i]);
}

static void string_sha384_compute(const unsigned char *msg, size_t len, unsigned char out[48]) {
    static const uint64_t k_init[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL,
        0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
        0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL
    };
    uint64_t state[8];
    string_sha512_compute_state(msg, len, k_init, state);
    for (size_t i = 0; i < 6; i++) string_store_be64(out + (i * 8), state[i]);
}

static void string_keccakf1600(uint64_t st[25]) {
    static const uint64_t rc[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
    };
    static const int rotc[24] = {
        1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
        27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44
    };
    static const int piln[24] = {
        10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
        15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1
    };

    for (size_t r = 0; r < 24; r++) {
        uint64_t c[5];
        for (size_t x = 0; x < 5; x++) {
            c[x] = st[x] ^ st[x + 5] ^ st[x + 10] ^ st[x + 15] ^ st[x + 20];
        }

        for (size_t x = 0; x < 5; x++) {
            uint64_t d = c[(x + 4) % 5] ^ string_rotl64(c[(x + 1) % 5], 1);
            for (size_t y = 0; y < 25; y += 5) st[y + x] ^= d;
        }

        uint64_t t = st[1];
        for (size_t i = 0; i < 24; i++) {
            int j = piln[i];
            uint64_t tmp = st[j];
            st[j] = string_rotl64(t, (uint32_t)rotc[i]);
            t = tmp;
        }

        for (size_t y = 0; y < 25; y += 5) {
            uint64_t row[5];
            for (size_t x = 0; x < 5; x++) row[x] = st[y + x];
            for (size_t x = 0; x < 5; x++) {
                st[y + x] = row[x] ^ ((~row[(x + 1) % 5]) & row[(x + 2) % 5]);
            }
        }

        st[0] ^= rc[r];
    }
}

static void string_sha3_compute(const unsigned char *msg,
                                size_t len,
                                unsigned char *out,
                                size_t out_len) {
    size_t rate = 200 - (2 * out_len);
    uint64_t st[25] = {0};
    unsigned char *st_bytes = (unsigned char*)st;

    size_t off = 0;
    while (off + rate <= len) {
        for (size_t i = 0; i < rate; i++) st_bytes[i] ^= msg[off + i];
        string_keccakf1600(st);
        off += rate;
    }

    unsigned char block[200] = {0};
    size_t rem = len - off;
    for (size_t i = 0; i < rem; i++) block[i] = msg[off + i];
    block[rem] ^= 0x06;
    block[rate - 1] ^= 0x80;
    for (size_t i = 0; i < rate; i++) st_bytes[i] ^= block[i];
    string_keccakf1600(st);

    size_t out_off = 0;
    while (out_off < out_len) {
        size_t take = (out_len - out_off < rate) ? (out_len - out_off) : rate;
        memcpy(out + out_off, st_bytes, take);
        out_off += take;
        if (out_off < out_len) string_keccakf1600(st);
    }
}

static String_View string_bytes_hex_temp(Evaluator_Context *ctx, const unsigned char *bytes, size_t count, bool upper) {
    if (!ctx || !bytes) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), (count * 2) + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    const char *lut = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    for (size_t i = 0; i < count; i++) {
        unsigned char b = bytes[i];
        buf[i * 2 + 0] = lut[(b >> 4) & 0x0F];
        buf[i * 2 + 1] = lut[b & 0x0F];
    }
    buf[count * 2] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool __attribute__((unused))
string_hash_compute_temp(Evaluator_Context *ctx, String_View algo, String_View input, String_View *out_hash) {
    if (!ctx || !out_hash) return false;
    *out_hash = nob_sv_from_cstr("");
    const unsigned char *msg = (const unsigned char*)input.data;
    if (eval_sv_eq_ci_lit(algo, "MD5")) {
        unsigned char d[16];
        string_md5_compute(msg, input.count, d);
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA1")) {
        unsigned char d[20];
        string_sha1_compute(msg, input.count, d);
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA256")) {
        unsigned char d[32];
        string_sha256_compute(msg, input.count, d);
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA224")) {
        unsigned char d[28];
        string_sha224_compute(msg, input.count, d);
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA384")) {
        unsigned char d[48];
        string_sha384_compute(msg, input.count, d);
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA512")) {
        unsigned char d[64];
        string_sha512_compute(msg, input.count, d);
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA3_224")) {
        unsigned char d[28];
        string_sha3_compute(msg, input.count, d, sizeof(d));
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA3_256")) {
        unsigned char d[32];
        string_sha3_compute(msg, input.count, d, sizeof(d));
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA3_384")) {
        unsigned char d[48];
        string_sha3_compute(msg, input.count, d, sizeof(d));
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(algo, "SHA3_512")) {
        unsigned char d[64];
        string_sha3_compute(msg, input.count, d, sizeof(d));
        *out_hash = string_bytes_hex_temp(ctx, d, sizeof(d), false);
        return !eval_should_stop(ctx);
    }
    return false;
}

static int string_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
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
        int hi = string_hex_nibble(in.data[i]);
        int lo = string_hex_nibble(in.data[i + 1]);
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
        uuid[6] = (unsigned char)((uuid[6] & 0x0F) | 0x30); // version 3
    } else if (eval_sv_eq_ci_lit(type, "SHA1")) {
        unsigned char d[20];
        string_sha1_compute(payload, payload_len, d);
        memcpy(uuid, d, 16);
        uuid[6] = (unsigned char)((uuid[6] & 0x0F) | 0x50); // version 5
    } else {
        return false;
    }

    uuid[8] = (unsigned char)((uuid[8] & 0x3F) | 0x80); // RFC 4122 variant
    *out_uuid = string_format_uuid_temp(ctx, uuid, upper);
    return !eval_should_stop(ctx);
}

typedef enum {
    STRING_JSON_NULL = 0,
    STRING_JSON_BOOL,
    STRING_JSON_NUMBER,
    STRING_JSON_STRING,
    STRING_JSON_ARRAY,
    STRING_JSON_OBJECT,
} String_Json_Type;

static void string_json_skip_ws(String_View json, size_t *io) {
    if (!io) return;
    while (*io < json.count && isspace((unsigned char)json.data[*io])) (*io)++;
}

static bool string_json_parse_string_token(String_View json,
                                           size_t *io,
                                           size_t *out_inner_start,
                                           size_t *out_inner_end) {
    if (!io || *io >= json.count || json.data[*io] != '"') return false;
    size_t i = *io + 1;
    size_t inner_start = i;
    while (i < json.count) {
        char c = json.data[i];
        if (c == '"') {
            if (out_inner_start) *out_inner_start = inner_start;
            if (out_inner_end) *out_inner_end = i;
            *io = i + 1;
            return true;
        }
        if (c == '\\') {
            i++;
            if (i >= json.count) return false;
            char e = json.data[i];
            if (e == 'u') {
                if (i + 4 >= json.count) return false;
                for (size_t k = 1; k <= 4; k++) {
                    if (string_hex_nibble(json.data[i + k]) < 0) return false;
                }
                i += 5;
                continue;
            }
            if (!(e == '"' || e == '\\' || e == '/' || e == 'b' ||
                  e == 'f' || e == 'n' || e == 'r' || e == 't')) return false;
        }
        i++;
    }
    return false;
}

static bool string_json_decode_string_temp(Evaluator_Context *ctx,
                                           String_View raw,
                                           String_View *out) {
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), raw.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t w = 0;
    for (size_t i = 0; i < raw.count; i++) {
        char c = raw.data[i];
        if (c != '\\') {
            buf[w++] = c;
            continue;
        }
        if (i + 1 >= raw.count) return false;
        char e = raw.data[++i];
        switch (e) {
            case '"': buf[w++] = '"'; break;
            case '\\': buf[w++] = '\\'; break;
            case '/': buf[w++] = '/'; break;
            case 'b': buf[w++] = '\b'; break;
            case 'f': buf[w++] = '\f'; break;
            case 'n': buf[w++] = '\n'; break;
            case 'r': buf[w++] = '\r'; break;
            case 't': buf[w++] = '\t'; break;
            case 'u': {
                if (i + 4 >= raw.count) return false;
                int h0 = string_hex_nibble(raw.data[i + 1]);
                int h1 = string_hex_nibble(raw.data[i + 2]);
                int h2 = string_hex_nibble(raw.data[i + 3]);
                int h3 = string_hex_nibble(raw.data[i + 4]);
                if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return false;
                unsigned int cp = (unsigned int)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                buf[w++] = (cp <= 0x7F) ? (char)cp : '?';
                i += 4;
                break;
            }
            default: return false;
        }
    }

    buf[w] = '\0';
    *out = nob_sv_from_parts(buf, w);
    return true;
}

static bool string_json_parse_number_token(String_View json, size_t *io) {
    if (!io || *io >= json.count) return false;
    size_t i = *io;
    if (json.data[i] == '-') i++;
    if (i >= json.count) return false;

    if (json.data[i] == '0') {
        i++;
    } else if (json.data[i] >= '1' && json.data[i] <= '9') {
        i++;
        while (i < json.count && isdigit((unsigned char)json.data[i])) i++;
    } else {
        return false;
    }

    if (i < json.count && json.data[i] == '.') {
        i++;
        if (i >= json.count || !isdigit((unsigned char)json.data[i])) return false;
        while (i < json.count && isdigit((unsigned char)json.data[i])) i++;
    }

    if (i < json.count && (json.data[i] == 'e' || json.data[i] == 'E')) {
        i++;
        if (i < json.count && (json.data[i] == '+' || json.data[i] == '-')) i++;
        if (i >= json.count || !isdigit((unsigned char)json.data[i])) return false;
        while (i < json.count && isdigit((unsigned char)json.data[i])) i++;
    }

    *io = i;
    return true;
}

static bool string_json_match_lit(String_View json, size_t *io, const char *lit) {
    if (!io || !lit) return false;
    size_t n = strlen(lit);
    if (*io + n > json.count) return false;
    if (memcmp(json.data + *io, lit, n) != 0) return false;
    *io += n;
    return true;
}

static String_View string_json_type_name(String_Json_Type t) {
    switch (t) {
        case STRING_JSON_NULL: return nob_sv_from_cstr("NULL");
        case STRING_JSON_BOOL: return nob_sv_from_cstr("BOOLEAN");
        case STRING_JSON_NUMBER: return nob_sv_from_cstr("NUMBER");
        case STRING_JSON_STRING: return nob_sv_from_cstr("STRING");
        case STRING_JSON_ARRAY: return nob_sv_from_cstr("ARRAY");
        case STRING_JSON_OBJECT: return nob_sv_from_cstr("OBJECT");
        default: return nob_sv_from_cstr("");
    }
}

typedef struct String_Json_Value String_Json_Value;
typedef struct {
    String_View key;
    String_Json_Value *value;
} String_Json_Object_Entry;

struct String_Json_Value {
    String_Json_Type type;
    bool bool_value;
    String_View scalar; // STRING (decoded) or NUMBER (raw token)
    String_Json_Value **array_items;
    size_t array_count;
    size_t array_capacity;
    String_Json_Object_Entry *object_items;
    size_t object_count;
    size_t object_capacity;
};

typedef struct {
    String_View message;
    size_t path_prefix_count;
} String_Json_Error;

static String_View string_sb_to_temp_sv(Evaluator_Context *ctx, Nob_String_Builder *sb) {
    if (!ctx || !sb) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sb->count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (sb->count > 0) memcpy(buf, sb->items, sb->count);
    buf[sb->count] = '\0';
    return nob_sv_from_parts(buf, sb->count);
}

static String_View string_json_notfound_temp(Evaluator_Context *ctx, String_View *path, size_t path_count) {
    if (!ctx || path_count == 0) return nob_sv_from_cstr("NOTFOUND");
    Nob_String_Builder sb = {0};
    for (size_t i = 0; i < path_count; i++) {
        if (i > 0) nob_sb_append(&sb, '-');
        nob_sb_append_buf(&sb, path[i].data, path[i].count);
    }
    nob_sb_append_cstr(&sb, "-NOTFOUND");
    String_View out = string_sb_to_temp_sv(ctx, &sb);
    nob_sb_free(sb);
    return out;
}

static String_View string_json_message_with_token_temp(Evaluator_Context *ctx, const char *prefix, String_View token) {
    if (!ctx) return nob_sv_from_cstr("");
    Nob_String_Builder sb = {0};
    if (prefix) nob_sb_append_cstr(&sb, prefix);
    if (token.count > 0) {
        if (sb.count > 0) nob_sb_append_cstr(&sb, ": ");
        nob_sb_append_buf(&sb, token.data, token.count);
    }
    String_View out = string_sb_to_temp_sv(ctx, &sb);
    nob_sb_free(sb);
    return out;
}

static bool string_json_emit_or_store_error(Evaluator_Context *ctx,
                                            const Node *node,
                                            Cmake_Event_Origin o,
                                            bool has_error_var,
                                            String_View error_var,
                                            String_View out_var,
                                            String_View message,
                                            String_View *path,
                                            size_t path_prefix_count) {
    if (!ctx || !node) return !eval_should_stop(ctx);
    if (has_error_var) {
        (void)eval_var_set(ctx, out_var, string_json_notfound_temp(ctx, path, path_prefix_count));
        (void)eval_var_set(ctx, error_var, message);
        return !eval_should_stop(ctx);
    }
    eval_emit_diag(ctx,
                   EV_DIAG_ERROR,
                   nob_sv_from_cstr("string"),
                   node->as.cmd.name,
                   o,
                   message,
                   nob_sv_from_cstr(""));
    return !eval_should_stop(ctx);
}

static String_Json_Value *string_jsonv_new(Evaluator_Context *ctx, String_Json_Type type) {
    if (!ctx) return NULL;
    String_Json_Value *v = (String_Json_Value*)arena_alloc(eval_temp_arena(ctx), sizeof(*v));
    EVAL_OOM_RETURN_IF_NULL(ctx, v, NULL);
    memset(v, 0, sizeof(*v));
    v->type = type;
    return v;
}

static bool string_jsonv_array_push(Evaluator_Context *ctx, String_Json_Value *arr, String_Json_Value *item) {
    if (!ctx || !arr || arr->type != STRING_JSON_ARRAY || !item) return false;
    if (!arena_da_reserve(eval_temp_arena(ctx),
                          (void**)&arr->array_items,
                          &arr->array_capacity,
                          sizeof(arr->array_items[0]),
                          arr->array_count + 1)) {
        return ctx_oom(ctx);
    }
    arr->array_items[arr->array_count++] = item;
    return true;
}

static bool string_jsonv_object_push(Evaluator_Context *ctx, String_Json_Value *obj, String_View key, String_Json_Value *item) {
    if (!ctx || !obj || obj->type != STRING_JSON_OBJECT || !item) return false;
    if (!arena_da_reserve(eval_temp_arena(ctx),
                          (void**)&obj->object_items,
                          &obj->object_capacity,
                          sizeof(obj->object_items[0]),
                          obj->object_count + 1)) {
        return ctx_oom(ctx);
    }
    obj->object_items[obj->object_count++] = (String_Json_Object_Entry){ .key = key, .value = item };
    return true;
}

static bool string_jsonv_parse_value_temp(Evaluator_Context *ctx,
                                          String_View json,
                                          size_t *io,
                                          String_Json_Value **out,
                                          String_View *err_msg);

static bool string_jsonv_parse_object_temp(Evaluator_Context *ctx,
                                           String_View json,
                                           size_t *io,
                                           String_Json_Value **out,
                                           String_View *err_msg) {
    if (!ctx || !io || !out || !err_msg) return false;
    if (*io >= json.count || json.data[*io] != '{') return false;
    (*io)++;
    String_Json_Value *obj = string_jsonv_new(ctx, STRING_JSON_OBJECT);
    if (!obj) return false;

    string_json_skip_ws(json, io);
    if (*io < json.count && json.data[*io] == '}') {
        (*io)++;
        *out = obj;
        return true;
    }

    for (;;) {
        size_t k0 = 0, k1 = 0;
        if (!string_json_parse_string_token(json, io, &k0, &k1)) {
            *err_msg = nob_sv_from_cstr("string(JSON) failed to parse object key");
            return false;
        }
        String_View key_raw = nob_sv_from_parts(json.data + k0, k1 - k0);
        String_View key_dec = nob_sv_from_cstr("");
        if (!string_json_decode_string_temp(ctx, key_raw, &key_dec)) {
            *err_msg = nob_sv_from_cstr("string(JSON) failed to decode object key");
            return false;
        }
        string_json_skip_ws(json, io);
        if (*io >= json.count || json.data[*io] != ':') {
            *err_msg = nob_sv_from_cstr("string(JSON) expected ':' after object key");
            return false;
        }
        (*io)++;
        string_json_skip_ws(json, io);

        String_Json_Value *child = NULL;
        if (!string_jsonv_parse_value_temp(ctx, json, io, &child, err_msg)) return false;
        if (!string_jsonv_object_push(ctx, obj, key_dec, child)) return false;

        string_json_skip_ws(json, io);
        if (*io >= json.count) {
            *err_msg = nob_sv_from_cstr("string(JSON) object is not terminated");
            return false;
        }
        if (json.data[*io] == ',') {
            (*io)++;
            string_json_skip_ws(json, io);
            continue;
        }
        if (json.data[*io] == '}') {
            (*io)++;
            *out = obj;
            return true;
        }
        *err_msg = nob_sv_from_cstr("string(JSON) expected ',' or '}' in object");
        return false;
    }
}

static bool string_jsonv_parse_array_temp(Evaluator_Context *ctx,
                                          String_View json,
                                          size_t *io,
                                          String_Json_Value **out,
                                          String_View *err_msg) {
    if (!ctx || !io || !out || !err_msg) return false;
    if (*io >= json.count || json.data[*io] != '[') return false;
    (*io)++;
    String_Json_Value *arr = string_jsonv_new(ctx, STRING_JSON_ARRAY);
    if (!arr) return false;

    string_json_skip_ws(json, io);
    if (*io < json.count && json.data[*io] == ']') {
        (*io)++;
        *out = arr;
        return true;
    }

    for (;;) {
        String_Json_Value *child = NULL;
        if (!string_jsonv_parse_value_temp(ctx, json, io, &child, err_msg)) return false;
        if (!string_jsonv_array_push(ctx, arr, child)) return false;

        string_json_skip_ws(json, io);
        if (*io >= json.count) {
            *err_msg = nob_sv_from_cstr("string(JSON) array is not terminated");
            return false;
        }
        if (json.data[*io] == ',') {
            (*io)++;
            string_json_skip_ws(json, io);
            continue;
        }
        if (json.data[*io] == ']') {
            (*io)++;
            *out = arr;
            return true;
        }
        *err_msg = nob_sv_from_cstr("string(JSON) expected ',' or ']' in array");
        return false;
    }
}

static bool string_jsonv_parse_value_temp(Evaluator_Context *ctx,
                                          String_View json,
                                          size_t *io,
                                          String_Json_Value **out,
                                          String_View *err_msg) {
    if (!ctx || !io || !out || !err_msg) return false;
    *out = NULL;
    string_json_skip_ws(json, io);
    if (*io >= json.count) {
        *err_msg = nob_sv_from_cstr("string(JSON) unexpected end of input");
        return false;
    }

    char c = json.data[*io];
    if (c == '{') return string_jsonv_parse_object_temp(ctx, json, io, out, err_msg);
    if (c == '[') return string_jsonv_parse_array_temp(ctx, json, io, out, err_msg);
    if (c == '"') {
        size_t s0 = 0, s1 = 0;
        if (!string_json_parse_string_token(json, io, &s0, &s1)) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid string value");
            return false;
        }
        String_View raw = nob_sv_from_parts(json.data + s0, s1 - s0);
        String_View dec = nob_sv_from_cstr("");
        if (!string_json_decode_string_temp(ctx, raw, &dec)) {
            *err_msg = nob_sv_from_cstr("string(JSON) failed to decode string value");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_STRING);
        if (!v) return false;
        v->scalar = dec;
        *out = v;
        return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        size_t s = *io;
        if (!string_json_parse_number_token(json, io)) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid number");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_NUMBER);
        if (!v) return false;
        v->scalar = nob_sv_from_parts(json.data + s, *io - s);
        *out = v;
        return true;
    }
    if (c == 't') {
        if (!string_json_match_lit(json, io, "true")) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid literal");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_BOOL);
        if (!v) return false;
        v->bool_value = true;
        *out = v;
        return true;
    }
    if (c == 'f') {
        if (!string_json_match_lit(json, io, "false")) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid literal");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_BOOL);
        if (!v) return false;
        v->bool_value = false;
        *out = v;
        return true;
    }
    if (c == 'n') {
        if (!string_json_match_lit(json, io, "null")) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid literal");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_NULL);
        if (!v) return false;
        *out = v;
        return true;
    }

    *err_msg = nob_sv_from_cstr("string(JSON) invalid token");
    return false;
}

static bool string_jsonv_parse_root_temp(Evaluator_Context *ctx,
                                         String_View json,
                                         String_Json_Value **out,
                                         String_View *err_msg) {
    if (!ctx || !out || !err_msg) return false;
    size_t i = 0;
    if (!string_jsonv_parse_value_temp(ctx, json, &i, out, err_msg)) return false;
    string_json_skip_ws(json, &i);
    if (i != json.count) {
        *err_msg = nob_sv_from_cstr("string(JSON) extra trailing input");
        return false;
    }
    return true;
}

static bool string_jsonv_parse_index_sv(String_View token, bool bound_check, size_t bound, size_t *out) {
    if (!out) return false;
    long long idx = -1;
    if (!sv_parse_i64(token, &idx) || idx < 0) return false;
    if (bound_check && (size_t)idx >= bound) return false;
    *out = (size_t)idx;
    return true;
}

static bool string_jsonv_object_find(String_Json_Value *obj, String_View key, size_t *out_index) {
    if (!obj || obj->type != STRING_JSON_OBJECT || !out_index) return false;
    for (size_t i = 0; i < obj->object_count; i++) {
        if (nob_sv_eq(obj->object_items[i].key, key)) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

static bool string_jsonv_resolve_path(String_Json_Value *root,
                                      String_View *path,
                                      size_t path_count,
                                      String_Json_Value **out,
                                      String_Json_Error *err,
                                      Evaluator_Context *ctx) {
    if (!root || !out || !err || !ctx) return false;
    String_Json_Value *current = root;
    for (size_t i = 0; i < path_count; i++) {
        String_View tok = path[i];
        if (current->type == STRING_JSON_OBJECT) {
            size_t idx = 0;
            if (!string_jsonv_object_find(current, tok, &idx)) {
                err->message = string_json_message_with_token_temp(ctx, "string(JSON) object member not found", tok);
                err->path_prefix_count = i + 1;
                return false;
            }
            current = current->object_items[idx].value;
            continue;
        }
        if (current->type == STRING_JSON_ARRAY) {
            size_t idx = 0;
            if (!string_jsonv_parse_index_sv(tok, true, current->array_count, &idx)) {
                err->message = string_json_message_with_token_temp(ctx, "string(JSON) array index out of range", tok);
                err->path_prefix_count = i + 1;
                return false;
            }
            current = current->array_items[idx];
            continue;
        }
        err->message = string_json_message_with_token_temp(ctx, "string(JSON) path traverses non-container value", tok);
        err->path_prefix_count = i + 1;
        return false;
    }
    *out = current;
    return true;
}

static bool string_jsonv_equal(const String_Json_Value *a, const String_Json_Value *b) {
    if (!a || !b || a->type != b->type) return false;
    switch (a->type) {
        case STRING_JSON_NULL: return true;
        case STRING_JSON_BOOL: return a->bool_value == b->bool_value;
        case STRING_JSON_NUMBER:
        case STRING_JSON_STRING: return nob_sv_eq(a->scalar, b->scalar);
        case STRING_JSON_ARRAY:
            if (a->array_count != b->array_count) return false;
            for (size_t i = 0; i < a->array_count; i++) {
                if (!string_jsonv_equal(a->array_items[i], b->array_items[i])) return false;
            }
            return true;
        case STRING_JSON_OBJECT:
            if (a->object_count != b->object_count) return false;
            for (size_t i = 0; i < a->object_count; i++) {
                size_t j = 0;
                if (!string_jsonv_object_find((String_Json_Value*)b, a->object_items[i].key, &j)) return false;
                if (!string_jsonv_equal(a->object_items[i].value, b->object_items[j].value)) return false;
            }
            return true;
        default:
            return false;
    }
}

static void string_jsonv_append_indent(Nob_String_Builder *sb, size_t level) {
    for (size_t i = 0; i < level; i++) {
        nob_sb_append(sb, ' ');
        nob_sb_append(sb, ' ');
    }
}

static void string_jsonv_append_escaped_string(Nob_String_Builder *sb, String_View s) {
    static const char hex[] = "0123456789abcdef";
    nob_sb_append(sb, '"');
    for (size_t i = 0; i < s.count; i++) {
        unsigned char c = (unsigned char)s.data[i];
        switch (c) {
            case '"': nob_sb_append_cstr(sb, "\\\""); break;
            case '\\': nob_sb_append_cstr(sb, "\\\\"); break;
            case '\b': nob_sb_append_cstr(sb, "\\b"); break;
            case '\f': nob_sb_append_cstr(sb, "\\f"); break;
            case '\n': nob_sb_append_cstr(sb, "\\n"); break;
            case '\r': nob_sb_append_cstr(sb, "\\r"); break;
            case '\t': nob_sb_append_cstr(sb, "\\t"); break;
            default:
                if (c < 0x20) {
                    char u[6] = {'\\', 'u', '0', '0', hex[(c >> 4) & 0x0F], hex[c & 0x0F]};
                    nob_sb_append_buf(sb, u, sizeof(u));
                } else {
                    nob_sb_append(sb, (char)c);
                }
                break;
        }
    }
    nob_sb_append(sb, '"');
}

static void string_jsonv_serialize_append(Nob_String_Builder *sb, const String_Json_Value *v, size_t indent) {
    if (!sb || !v) return;
    switch (v->type) {
        case STRING_JSON_NULL:
            nob_sb_append_cstr(sb, "null");
            return;
        case STRING_JSON_BOOL:
            nob_sb_append_cstr(sb, v->bool_value ? "true" : "false");
            return;
        case STRING_JSON_NUMBER:
            nob_sb_append_buf(sb, v->scalar.data, v->scalar.count);
            return;
        case STRING_JSON_STRING:
            string_jsonv_append_escaped_string(sb, v->scalar);
            return;
        case STRING_JSON_ARRAY:
            if (v->array_count == 0) {
                nob_sb_append_cstr(sb, "[]");
                return;
            }
            nob_sb_append_cstr(sb, "[\n");
            for (size_t i = 0; i < v->array_count; i++) {
                string_jsonv_append_indent(sb, indent + 1);
                string_jsonv_serialize_append(sb, v->array_items[i], indent + 1);
                if (i + 1 < v->array_count) nob_sb_append_cstr(sb, ",");
                nob_sb_append_cstr(sb, "\n");
            }
            string_jsonv_append_indent(sb, indent);
            nob_sb_append_cstr(sb, "]");
            return;
        case STRING_JSON_OBJECT:
            if (v->object_count == 0) {
                nob_sb_append_cstr(sb, "{}");
                return;
            }
            nob_sb_append_cstr(sb, "{\n");
            for (size_t i = 0; i < v->object_count; i++) {
                string_jsonv_append_indent(sb, indent + 1);
                string_jsonv_append_escaped_string(sb, v->object_items[i].key);
                nob_sb_append_cstr(sb, " : ");
                string_jsonv_serialize_append(sb, v->object_items[i].value, indent + 1);
                if (i + 1 < v->object_count) nob_sb_append_cstr(sb, ",");
                nob_sb_append_cstr(sb, "\n");
            }
            string_jsonv_append_indent(sb, indent);
            nob_sb_append_cstr(sb, "}");
            return;
        default:
            return;
    }
}

static String_View string_jsonv_serialize_temp(Evaluator_Context *ctx, const String_Json_Value *v) {
    if (!ctx || !v) return nob_sv_from_cstr("");
    Nob_String_Builder sb = {0};
    string_jsonv_serialize_append(&sb, v, 0);
    String_View out = string_sb_to_temp_sv(ctx, &sb);
    nob_sb_free(sb);
    return out;
}

static bool string_handle_json_command(Evaluator_Context *ctx,
                                       const Node *node,
                                       Cmake_Event_Origin o,
                                       SV_List a) {
    if (!ctx || !node) return !eval_should_stop(ctx);
    if (a.count < 4) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                       nob_sv_from_cstr("string(JSON) requires out-var, mode and JSON string"),
                       nob_sv_from_cstr("Usage: string(JSON <out-var> [ERROR_VARIABLE <err-var>] <mode> <json> ...)"));
        return !eval_should_stop(ctx);
    }

    size_t pos = 1;
    String_View out_var = a.items[pos++];
    bool has_error_var = false;
    String_View error_var = nob_sv_from_cstr("");
    if (pos < a.count && eval_sv_eq_ci_lit(a.items[pos], "ERROR_VARIABLE")) {
        if (pos + 1 >= a.count) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(JSON ERROR_VARIABLE) requires output variable"),
                           nob_sv_from_cstr("Usage: string(JSON <out-var> ERROR_VARIABLE <err-var> <mode> <json> ...)"));
            return !eval_should_stop(ctx);
        }
        has_error_var = true;
        error_var = a.items[pos + 1];
        pos += 2;
        (void)eval_var_set(ctx, error_var, nob_sv_from_cstr("NOTFOUND"));
    }
    if (pos >= a.count) {
        return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                               nob_sv_from_cstr("string(JSON) missing mode argument"),
                                               NULL, 0);
    }

    String_View mode = a.items[pos++];
    if (!(eval_sv_eq_ci_lit(mode, "GET") ||
          eval_sv_eq_ci_lit(mode, "TYPE") ||
          eval_sv_eq_ci_lit(mode, "MEMBER") ||
          eval_sv_eq_ci_lit(mode, "LENGTH") ||
          eval_sv_eq_ci_lit(mode, "REMOVE") ||
          eval_sv_eq_ci_lit(mode, "SET") ||
          eval_sv_eq_ci_lit(mode, "EQUAL"))) {
        return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                               string_json_message_with_token_temp(ctx, "string(JSON) received unsupported mode", mode),
                                               NULL, 0);
    }

    if (eval_sv_eq_ci_lit(mode, "EQUAL")) {
        if (pos + 1 >= a.count || pos + 2 != a.count) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON EQUAL) expects exactly two JSON strings"),
                                                   NULL, 0);
        }
        String_View err = nob_sv_from_cstr("");
        String_Json_Value *lhs = NULL;
        if (!string_jsonv_parse_root_temp(ctx, a.items[pos], &lhs, &err)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var, err, NULL, 0);
        }
        String_Json_Value *rhs = NULL;
        if (!string_jsonv_parse_root_temp(ctx, a.items[pos + 1], &rhs, &err)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var, err, NULL, 0);
        }
        (void)eval_var_set(ctx, out_var, string_jsonv_equal(lhs, rhs) ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
        return !eval_should_stop(ctx);
    }

    if (pos >= a.count) {
        return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                               nob_sv_from_cstr("string(JSON) missing JSON string argument"),
                                               NULL, 0);
    }

    String_View err = nob_sv_from_cstr("");
    String_Json_Value *root = NULL;
    if (!string_jsonv_parse_root_temp(ctx, a.items[pos++], &root, &err)) {
        return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var, err, NULL, 0);
    }

    String_View *path = (pos < a.count) ? &a.items[pos] : NULL;
    size_t path_count = (pos < a.count) ? (a.count - pos) : 0;

    if (eval_sv_eq_ci_lit(mode, "GET") || eval_sv_eq_ci_lit(mode, "TYPE") || eval_sv_eq_ci_lit(mode, "LENGTH")) {
        String_Json_Error jerr = {0};
        String_Json_Value *target = NULL;
        if (!string_jsonv_resolve_path(root, path, path_count, &target, &jerr, ctx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   jerr.message, path, jerr.path_prefix_count);
        }
        if (eval_sv_eq_ci_lit(mode, "GET")) {
            if (target->type == STRING_JSON_STRING || target->type == STRING_JSON_NUMBER) {
                (void)eval_var_set(ctx, out_var, target->scalar);
            } else if (target->type == STRING_JSON_BOOL) {
                (void)eval_var_set(ctx, out_var, target->bool_value ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
            } else if (target->type == STRING_JSON_NULL) {
                (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(""));
            } else {
                (void)eval_var_set(ctx, out_var, string_jsonv_serialize_temp(ctx, target));
            }
            return !eval_should_stop(ctx);
        }
        if (eval_sv_eq_ci_lit(mode, "TYPE")) {
            (void)eval_var_set(ctx, out_var, string_json_type_name(target->type));
            return !eval_should_stop(ctx);
        }
        if (target->type != STRING_JSON_OBJECT && target->type != STRING_JSON_ARRAY) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON LENGTH) requires ARRAY or OBJECT"),
                                                   path, path_count);
        }
        char num[64];
        snprintf(num, sizeof(num), "%zu", target->type == STRING_JSON_ARRAY ? target->array_count : target->object_count);
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(num));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(mode, "MEMBER")) {
        if (path_count < 1) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON MEMBER) requires index argument"),
                                                   path, 0);
        }
        String_View index_sv = path[path_count - 1];
        String_Json_Error jerr = {0};
        String_Json_Value *target = NULL;
        if (!string_jsonv_resolve_path(root, path, path_count - 1, &target, &jerr, ctx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   jerr.message, path, jerr.path_prefix_count);
        }
        if (target->type != STRING_JSON_OBJECT) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON MEMBER) requires OBJECT target"),
                                                   path, path_count - 1);
        }
        size_t idx = 0;
        if (!string_jsonv_parse_index_sv(index_sv, true, target->object_count, &idx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   string_json_message_with_token_temp(ctx, "string(JSON MEMBER) invalid index", index_sv),
                                                   path, path_count);
        }
        (void)eval_var_set(ctx, out_var, target->object_items[idx].key);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(mode, "REMOVE")) {
        if (path_count < 1) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON REMOVE) requires at least one path token"),
                                                   path, 0);
        }
        String_Json_Error jerr = {0};
        String_Json_Value *parent = NULL;
        if (!string_jsonv_resolve_path(root, path, path_count - 1, &parent, &jerr, ctx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   jerr.message, path, jerr.path_prefix_count);
        }
        String_View rem = path[path_count - 1];
        if (parent->type == STRING_JSON_OBJECT) {
            size_t idx = 0;
            if (string_jsonv_object_find(parent, rem, &idx)) {
                for (size_t i = idx + 1; i < parent->object_count; i++) {
                    parent->object_items[i - 1] = parent->object_items[i];
                }
                parent->object_count--;
            }
        } else if (parent->type == STRING_JSON_ARRAY) {
            size_t idx = 0;
            if (!string_jsonv_parse_index_sv(rem, true, parent->array_count, &idx)) {
                return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                       string_json_message_with_token_temp(ctx, "string(JSON REMOVE) invalid index", rem),
                                                       path, path_count);
            }
            for (size_t i = idx + 1; i < parent->array_count; i++) parent->array_items[i - 1] = parent->array_items[i];
            parent->array_count--;
        } else {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON REMOVE) requires OBJECT or ARRAY target"),
                                                   path, path_count - 1);
        }
        (void)eval_var_set(ctx, out_var, string_jsonv_serialize_temp(ctx, root));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(mode, "SET")) {
        if (path_count < 2) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON SET) requires path and value arguments"),
                                                   path, 0);
        }
        String_View new_json = path[path_count - 1];
        String_Json_Value *new_value = NULL;
        if (!string_jsonv_parse_root_temp(ctx, new_json, &new_value, &err)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var, err, path, path_count - 1);
        }
        String_View key_or_index = path[path_count - 2];
        String_Json_Error jerr = {0};
        String_Json_Value *parent = NULL;
        if (!string_jsonv_resolve_path(root, path, path_count - 2, &parent, &jerr, ctx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   jerr.message, path, jerr.path_prefix_count);
        }
        if (parent->type == STRING_JSON_OBJECT) {
            size_t idx = 0;
            if (string_jsonv_object_find(parent, key_or_index, &idx)) {
                parent->object_items[idx].value = new_value;
            } else {
                if (!string_jsonv_object_push(ctx, parent, key_or_index, new_value)) return !eval_should_stop(ctx);
            }
        } else if (parent->type == STRING_JSON_ARRAY) {
            size_t idx = 0;
            if (!string_jsonv_parse_index_sv(key_or_index, false, 0, &idx)) {
                return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                       string_json_message_with_token_temp(ctx, "string(JSON SET) invalid array index", key_or_index),
                                                       path, path_count - 1);
            }
            if (idx < parent->array_count) {
                parent->array_items[idx] = new_value;
            } else {
                if (!string_jsonv_array_push(ctx, parent, new_value)) return !eval_should_stop(ctx);
            }
        } else {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON SET) requires OBJECT or ARRAY target"),
                                                   path, path_count - 2);
        }
        (void)eval_var_set(ctx, out_var, string_jsonv_serialize_temp(ctx, root));
        return !eval_should_stop(ctx);
    }

    return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                           nob_sv_from_cstr("Unsupported string(JSON) mode"),
                                           path, 0);
}

bool eval_handle_string(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx)) return !eval_should_stop(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || a.count < 1) return !eval_should_stop(ctx);

    if (eval_sv_eq_ci_lit(a.items[0], "APPEND") || eval_sv_eq_ci_lit(a.items[0], "PREPEND")) {
        if (a.count < 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(APPEND/PREPEND) requires variable name"),
                           nob_sv_from_cstr("Usage: string(APPEND|PREPEND <var> [input...])"));
            return !eval_should_stop(ctx);
        }
        if (a.count == 2) return !eval_should_stop(ctx);

        bool is_append = eval_sv_eq_ci_lit(a.items[0], "APPEND");
        String_View var = a.items[1];
        String_View extra = string_join_no_sep_temp(ctx, &a.items[2], a.count - 2);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        String_View curr = eval_var_get(ctx, var);
        String_View lhs = is_append ? curr : extra;
        String_View rhs = is_append ? extra : curr;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), lhs.count + rhs.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));
        if (lhs.count > 0) memcpy(buf, lhs.data, lhs.count);
        if (rhs.count > 0) memcpy(buf + lhs.count, rhs.data, rhs.count);
        buf[lhs.count + rhs.count] = '\0';
        (void)eval_var_set(ctx, var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "CONCAT")) {
        if (a.count < 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(CONCAT) requires output variable"),
                           nob_sv_from_cstr("Usage: string(CONCAT <out-var> [input...])"));
            return !eval_should_stop(ctx);
        }
        String_View out_var = a.items[1];
        String_View out = (a.count > 2) ? string_join_no_sep_temp(ctx, &a.items[2], a.count - 2) : nob_sv_from_cstr("");
        (void)eval_var_set(ctx, out_var, out);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "JOIN")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(JOIN) requires glue and output variable"),
                           nob_sv_from_cstr("Usage: string(JOIN <glue> <out-var> [input...])"));
            return !eval_should_stop(ctx);
        }

        String_View glue = a.items[1];
        String_View out_var = a.items[2];
        size_t n = (a.count > 3) ? (a.count - 3) : 0;
        size_t total = 0;
        for (size_t i = 0; i < n; i++) total += a.items[3 + i].count;
        if (n > 1) total += glue.count * (n - 1);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));

        size_t off = 0;
        for (size_t i = 0; i < n; i++) {
            if (i > 0 && glue.count > 0) {
                memcpy(buf + off, glue.data, glue.count);
                off += glue.count;
            }
            if (a.items[3 + i].count > 0) {
                memcpy(buf + off, a.items[3 + i].data, a.items[3 + i].count);
                off += a.items[3 + i].count;
            }
        }
        buf[off] = '\0';
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "LENGTH")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(LENGTH) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(LENGTH <string> <out-var>)"));
            return !eval_should_stop(ctx);
        }
        char num_buf[64];
        snprintf(num_buf, sizeof(num_buf), "%zu", a.items[1].count);
        (void)eval_var_set(ctx, a.items[2], nob_sv_from_cstr(num_buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "STRIP")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(STRIP) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(STRIP <string> <out-var>)"));
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, a.items[2], list_strip_ws_view(a.items[1]));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "FIND")) {
        if (!(a.count == 4 || a.count == 5)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(FIND) requires input, substring and output variable"),
                           nob_sv_from_cstr("Usage: string(FIND <string> <substring> <out-var> [REVERSE])"));
            return !eval_should_stop(ctx);
        }
        bool reverse = false;
        if (a.count == 5) {
            if (!eval_sv_eq_ci_lit(a.items[4], "REVERSE")) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(FIND) received unsupported option"),
                               a.items[4]);
                return !eval_should_stop(ctx);
            }
            reverse = true;
        }
        long long idx = string_find_substr(a.items[1], a.items[2], reverse);
        char num_buf[64];
        snprintf(num_buf, sizeof(num_buf), "%lld", idx);
        (void)eval_var_set(ctx, a.items[3], nob_sv_from_cstr(num_buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "COMPARE")) {
        if (a.count != 5) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(COMPARE) requires op, lhs, rhs and output variable"),
                           nob_sv_from_cstr("Usage: string(COMPARE <LESS|GREATER|EQUAL|NOTEQUAL|LESS_EQUAL|GREATER_EQUAL> <s1> <s2> <out-var>)"));
            return !eval_should_stop(ctx);
        }
        int cmp = string_sv_cmp(a.items[2], a.items[3]);
        bool ok = false;
        if (eval_sv_eq_ci_lit(a.items[1], "LESS")) ok = cmp < 0;
        else if (eval_sv_eq_ci_lit(a.items[1], "GREATER")) ok = cmp > 0;
        else if (eval_sv_eq_ci_lit(a.items[1], "EQUAL")) ok = cmp == 0;
        else if (eval_sv_eq_ci_lit(a.items[1], "NOTEQUAL")) ok = cmp != 0;
        else if (eval_sv_eq_ci_lit(a.items[1], "LESS_EQUAL")) ok = cmp <= 0;
        else if (eval_sv_eq_ci_lit(a.items[1], "GREATER_EQUAL")) ok = cmp >= 0;
        else {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(COMPARE) received unsupported operation"),
                           a.items[1]);
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, a.items[4], ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "ASCII")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(ASCII) requires at least one code and output variable"),
                           nob_sv_from_cstr("Usage: string(ASCII <code>... <out-var>)"));
            return !eval_should_stop(ctx);
        }
        String_View out_var = a.items[a.count - 1];
        size_t n = a.count - 2;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), n + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));
        for (size_t i = 0; i < n; i++) {
            long long code = 0;
            if (!sv_parse_i64(a.items[1 + i], &code) || code < 0 || code > 255) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(ASCII) code must be an integer in range [0,255]"),
                               a.items[1 + i]);
                return !eval_should_stop(ctx);
            }
            buf[i] = (char)((unsigned char)code);
        }
        buf[n] = '\0';
        (void)eval_var_set(ctx, out_var, nob_sv_from_parts(buf, n));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "HEX")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(HEX) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(HEX <string> <out-var>)"));
            return !eval_should_stop(ctx);
        }
        String_View out = string_bytes_hex_temp(ctx, (const unsigned char*)a.items[1].data, a.items[1].count, false);
        (void)eval_var_set(ctx, a.items[2], out);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "CONFIGURE")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(CONFIGURE) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(CONFIGURE <string> <out-var> [@ONLY] [ESCAPE_QUOTES])"));
            return !eval_should_stop(ctx);
        }
        bool at_only = false;
        bool escape_quotes = false;
        for (size_t i = 3; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "@ONLY")) {
                at_only = true;
            } else if (eval_sv_eq_ci_lit(a.items[i], "ESCAPE_QUOTES")) {
                escape_quotes = true;
            } else {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(CONFIGURE) received unsupported option"),
                               a.items[i]);
                return !eval_should_stop(ctx);
            }
        }
        String_View out = nob_sv_from_cstr("");
        if (!string_configure_expand_temp(ctx, a.items[1], at_only, escape_quotes, &out)) {
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, a.items[2], out);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "MAKE_C_IDENTIFIER")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(MAKE_C_IDENTIFIER) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(MAKE_C_IDENTIFIER <string> <out-var>)"));
            return !eval_should_stop(ctx);
        }
        String_View in = a.items[1];
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 2);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));
        size_t off = 0;
        if (in.count > 0 && isdigit((unsigned char)in.data[0])) buf[off++] = '_';
        for (size_t i = 0; i < in.count; i++) {
            unsigned char c = (unsigned char)in.data[i];
            buf[off++] = (isalnum(c) || c == '_') ? (char)c : '_';
        }
        buf[off] = '\0';
        (void)eval_var_set(ctx, a.items[2], nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "GENEX_STRIP")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(GENEX_STRIP) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(GENEX_STRIP <string> <out-var>)"));
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, a.items[2], string_genex_strip_temp(ctx, a.items[1]));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REPEAT")) {
        if (a.count != 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(REPEAT) requires input, count and output variable"),
                           nob_sv_from_cstr("Usage: string(REPEAT <string> <count> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        unsigned long long count = 0;
        if (!string_parse_u64_sv(a.items[2], &count)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(REPEAT) repeat count is not a non-negative integer"),
                           a.items[2]);
            return !eval_should_stop(ctx);
        }

        String_View in = a.items[1];
        if (in.count > 0 && count > (SIZE_MAX / in.count)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(REPEAT) result is too large"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        size_t total = (size_t)count * in.count;
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));
        size_t off = 0;
        for (size_t i = 0; i < (size_t)count; i++) {
            if (in.count > 0) {
                memcpy(buf + off, in.data, in.count);
                off += in.count;
            }
        }
        buf[off] = '\0';
        (void)eval_var_set(ctx, a.items[3], nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "RANDOM")) {
        if (a.count < 2) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(RANDOM) requires output variable"),
                           nob_sv_from_cstr("Usage: string(RANDOM [LENGTH <n>] [ALPHABET <chars>] [RANDOM_SEED <seed>] <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_Random_Options opt = {
            .length = 5,
            .alphabet = nob_sv_from_cstr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"),
            .has_seed = false,
            .seed = 0,
        };
        String_View out_var = a.items[a.count - 1];
        for (size_t i = 1; i + 1 < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "LENGTH")) {
                unsigned long long v = 0;
                if (i + 1 >= a.count - 1 || !string_parse_u64_sv(a.items[i + 1], &v) || v == 0) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("string(RANDOM LENGTH) expects integer > 0"),
                                   i + 1 < a.count ? a.items[i + 1] : nob_sv_from_cstr(""));
                    return !eval_should_stop(ctx);
                }
                opt.length = (size_t)v;
                i++;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "ALPHABET")) {
                if (i + 1 >= a.count - 1 || a.items[i + 1].count == 0) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("string(RANDOM ALPHABET) expects non-empty alphabet"),
                                   nob_sv_from_cstr(""));
                    return !eval_should_stop(ctx);
                }
                opt.alphabet = a.items[i + 1];
                i++;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "RANDOM_SEED")) {
                unsigned long long seed = 0;
                if (i + 1 >= a.count - 1 || !string_parse_u64_sv(a.items[i + 1], &seed)) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("string(RANDOM RANDOM_SEED) expects unsigned integer"),
                                   i + 1 < a.count ? a.items[i + 1] : nob_sv_from_cstr(""));
                    return !eval_should_stop(ctx);
                }
                opt.has_seed = true;
                opt.seed = seed;
                i++;
                continue;
            }
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(RANDOM) received unsupported option"),
                           a.items[i]);
            return !eval_should_stop(ctx);
        }

        String_View rnd = nob_sv_from_cstr("");
        if (!string_random_generate_temp(ctx, &opt, &rnd)) return !eval_should_stop(ctx);
        (void)eval_var_set(ctx, out_var, rnd);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "TIMESTAMP")) {
        if (a.count < 2 || a.count > 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(TIMESTAMP) expects output variable with optional format and UTC"),
                           nob_sv_from_cstr("Usage: string(TIMESTAMP <out-var> [format] [UTC])"));
            return !eval_should_stop(ctx);
        }
        String_View out_var = a.items[1];
        String_View fmt = nob_sv_from_cstr("%Y-%m-%dT%H:%M:%S");
        bool has_fmt = false;
        bool utc = false;
        for (size_t i = 2; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "UTC")) {
                utc = true;
                continue;
            }
            if (!has_fmt) {
                fmt = a.items[i];
                has_fmt = true;
                continue;
            }
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(TIMESTAMP) received unsupported option"),
                           a.items[i]);
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, out_var, string_timestamp_format_temp(ctx, fmt, utc));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "UUID")) {
        if (a.count < 8) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(UUID) requires NAMESPACE, NAME and TYPE"),
                           nob_sv_from_cstr("Usage: string(UUID <out-var> NAMESPACE <uuid> NAME <name> TYPE <MD5|SHA1> [UPPER])"));
            return !eval_should_stop(ctx);
        }
        String_View out_var = a.items[1];
        String_View ns = nob_sv_from_cstr("");
        String_View name = nob_sv_from_cstr("");
        String_View type = nob_sv_from_cstr("");
        bool has_ns = false, has_name = false, has_type = false, upper = false;
        for (size_t i = 2; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "UPPER")) {
                upper = true;
                continue;
            }
            if (i + 1 >= a.count) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(UUID) option is missing value"),
                               a.items[i]);
                return !eval_should_stop(ctx);
            }
            if (eval_sv_eq_ci_lit(a.items[i], "NAMESPACE")) {
                ns = a.items[++i];
                has_ns = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "NAME")) {
                name = a.items[++i];
                has_name = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "TYPE")) {
                type = a.items[++i];
                has_type = true;
                continue;
            }
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(UUID) received unsupported option"),
                           a.items[i]);
            return !eval_should_stop(ctx);
        }
        if (!has_ns || !has_name || !has_type) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(UUID) requires NAMESPACE, NAME and TYPE"),
                           nob_sv_from_cstr("Usage: string(UUID <out-var> NAMESPACE <uuid> NAME <name> TYPE <MD5|SHA1> [UPPER])"));
            return !eval_should_stop(ctx);
        }
        String_View uuid_sv = nob_sv_from_cstr("");
        if (!string_uuid_name_based_temp(ctx, ns, name, type, upper, &uuid_sv)) {
            if (!string_parse_uuid_bytes(ns, (unsigned char[16]){0})) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(UUID) malformed NAMESPACE UUID"),
                               ns);
            } else {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(UUID) unsupported TYPE"),
                               type);
            }
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, out_var, uuid_sv);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "MD5") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA1") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA224") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA256") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA384") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA512") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA3_224") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA3_256") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA3_384") ||
        eval_sv_eq_ci_lit(a.items[0], "SHA3_512")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(<HASH>) requires output variable and input"),
                           nob_sv_from_cstr("Usage: string(<HASH> <out-var> <input>)"));
            return !eval_should_stop(ctx);
        }
        String_View hash = nob_sv_from_cstr("");
        if (!eval_hash_compute_hex_temp(ctx, a.items[0], a.items[2], &hash)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("Unsupported hash algorithm"),
                           a.items[0]);
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, a.items[1], hash);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "JSON")) {
        return string_handle_json_command(ctx, node, o, a);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REPLACE")) {
        if (a.count < 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(REPLACE) requires match, replace, out-var and input"),
                           nob_sv_from_cstr("Usage: string(REPLACE <match> <replace> <out-var> <input>...)"));
            return !eval_should_stop(ctx);
        }

        String_View match = a.items[1];
        String_View repl = a.items[2];
        String_View out_var = a.items[3];
        String_View input = (a.count > 4) ? eval_sv_join_semi_temp(ctx, &a.items[4], a.count - 4) : nob_sv_from_cstr("");
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        if (match.count == 0) {
            (void)eval_var_set(ctx, out_var, input);
            return !eval_should_stop(ctx);
        }

        size_t hits = 0;
        size_t i = 0;
        while (i + match.count <= input.count) {
            if (memcmp(input.data + i, match.data, match.count) == 0) {
                hits++;
                i += match.count;
            } else {
                i++;
            }
        }

        size_t total = input.count;
        if (repl.count >= match.count) {
            total += hits * (repl.count - match.count);
        } else {
            total -= hits * (match.count - repl.count);
        }
        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));

        size_t off = 0;
        i = 0;
        while (i < input.count) {
            if (i + match.count <= input.count && memcmp(input.data + i, match.data, match.count) == 0) {
                if (repl.count) {
                    memcpy(buf + off, repl.data, repl.count);
                    off += repl.count;
                }
                i += match.count;
            } else {
                buf[off++] = input.data[i++];
            }
        }
        buf[off] = '\0';
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "TOUPPER")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(TOUPPER) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(TOUPPER <input> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View out_var = a.items[a.count - 1];
        String_View input = (a.count == 3) ? a.items[1] : eval_sv_join_semi_temp(ctx, &a.items[1], a.count - 2);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));
        for (size_t i = 0; i < input.count; i++) {
            buf[i] = (char)toupper((unsigned char)input.data[i]);
        }
        buf[input.count] = '\0';
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "TOLOWER")) {
        if (a.count < 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(TOLOWER) requires input and output variable"),
                           nob_sv_from_cstr("Usage: string(TOLOWER <input> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View out_var = a.items[a.count - 1];
        String_View input = (a.count == 3) ? a.items[1] : eval_sv_join_semi_temp(ctx, &a.items[1], a.count - 2);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, !eval_should_stop(ctx));
        for (size_t i = 0; i < input.count; i++) {
            buf[i] = (char)tolower((unsigned char)input.data[i]);
        }
        buf[input.count] = '\0';
        (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(buf));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "SUBSTRING")) {
        if (a.count != 5) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(SUBSTRING) requires input, begin, length and output variable"),
                           nob_sv_from_cstr("Usage: string(SUBSTRING <input> <begin> <length> <out-var>)"));
            return !eval_should_stop(ctx);
        }

        String_View input = a.items[1];
        long long begin = 0;
        long long length = 0;
        if (!sv_parse_i64(a.items[2], &begin) || !sv_parse_i64(a.items[3], &length)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(SUBSTRING) begin/length must be integers"),
                           nob_sv_from_cstr("Use numeric begin and length (length can be -1 for until end)"));
            return !eval_should_stop(ctx);
        }
        if (begin < 0 || length < -1) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(SUBSTRING) begin must be >= 0 and length >= -1"),
                           nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        String_View out_var = a.items[4];
        size_t b = (size_t)begin;
        if (b >= input.count) {
            (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(""));
            return !eval_should_stop(ctx);
        }

        size_t end = input.count;
        if (length >= 0) {
            size_t l = (size_t)length;
            if (b + l < end) end = b + l;
        }
        String_View out = nob_sv_from_parts(input.data + b, end - b);
        (void)eval_var_set(ctx, out_var, out);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "REGEX")) {
        if (a.count < 5) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(REGEX) requires mode and arguments"),
                           nob_sv_from_cstr("Usage: string(REGEX MATCH|REPLACE|MATCHALL ...)"));
            return !eval_should_stop(ctx);
        }
        if (eval_sv_eq_ci_lit(a.items[1], "MATCH")) {
            String_View pattern = a.items[2];
            String_View out_var = a.items[3];
            String_View input = (a.count > 4) ? eval_sv_join_semi_temp(ctx, &a.items[4], a.count - 4) : nob_sv_from_cstr("");
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

            char *pat_buf = (char*)arena_alloc(eval_temp_arena(ctx), pattern.count + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, pat_buf, !eval_should_stop(ctx));
            memcpy(pat_buf, pattern.data, pattern.count);
            pat_buf[pattern.count] = '\0';

            char *in_buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, in_buf, !eval_should_stop(ctx));
            memcpy(in_buf, input.data, input.count);
            in_buf[input.count] = '\0';

            regex_t re;
            if (regcomp(&re, pat_buf, REG_EXTENDED) != 0) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("Invalid regex pattern"), pattern);
                return !eval_should_stop(ctx);
            }

            regmatch_t m[1];
            int rc = regexec(&re, in_buf, 1, m, 0);
            regfree(&re);

            if (rc == 0 && m[0].rm_so >= 0 && m[0].rm_eo >= m[0].rm_so) {
                size_t mlen = (size_t)(m[0].rm_eo - m[0].rm_so);
                String_View match_sv = nob_sv_from_parts(in_buf + m[0].rm_so, mlen);
                (void)eval_var_set(ctx, out_var, match_sv);
            } else {
                (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(""));
            }
            return !eval_should_stop(ctx);
        }

        if (eval_sv_eq_ci_lit(a.items[1], "REPLACE")) {
            if (a.count < 6) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(REGEX REPLACE) requires regex, replace, out-var and input"),
                               nob_sv_from_cstr("Usage: string(REGEX REPLACE <regex> <replace> <out-var> <input>...)"));
                return !eval_should_stop(ctx);
            }

            String_View pattern = a.items[2];
            String_View replacement = a.items[3];
            String_View out_var = a.items[4];
            String_View input = (a.count > 5) ? eval_sv_join_semi_temp(ctx, &a.items[5], a.count - 5) : nob_sv_from_cstr("");
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

            char *pat_buf = (char*)arena_alloc(eval_temp_arena(ctx), pattern.count + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, pat_buf, !eval_should_stop(ctx));
            memcpy(pat_buf, pattern.data, pattern.count);
            pat_buf[pattern.count] = '\0';

            char *in_buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, in_buf, !eval_should_stop(ctx));
            memcpy(in_buf, input.data, input.count);
            in_buf[input.count] = '\0';

            regex_t re;
            if (regcomp(&re, pat_buf, REG_EXTENDED) != 0) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("Invalid regex pattern"), pattern);
                return !eval_should_stop(ctx);
            }

            enum { MAX_GROUPS = 10 };
            regmatch_t m[MAX_GROUPS];
            Nob_String_Builder sb = {0};
            const char *cursor = in_buf;

            for (;;) {
                int rc = regexec(&re, cursor, MAX_GROUPS, m, 0);
                if (rc != 0 || m[0].rm_so < 0 || m[0].rm_eo < m[0].rm_so) {
                    nob_sb_append_cstr(&sb, cursor);
                    break;
                }

                size_t prefix_len = (size_t)m[0].rm_so;
                if (prefix_len > 0) nob_sb_append_buf(&sb, cursor, prefix_len);

                for (size_t i = 0; i < replacement.count; i++) {
                    char c = replacement.data[i];
                    if (c == '\\' && i + 1 < replacement.count) {
                        char n = replacement.data[++i];
                        if (n >= '0' && n <= '9') {
                            size_t gi = (size_t)(n - '0');
                            if (gi < MAX_GROUPS && m[gi].rm_so >= 0 && m[gi].rm_eo >= m[gi].rm_so) {
                                size_t glen = (size_t)(m[gi].rm_eo - m[gi].rm_so);
                                if (glen > 0) nob_sb_append_buf(&sb, cursor + m[gi].rm_so, glen);
                            }
                            continue;
                        }
                        nob_sb_append(&sb, n);
                        continue;
                    }
                    nob_sb_append(&sb, c);
                }

                size_t adv = (size_t)m[0].rm_eo;
                if (adv == 0) {
                    if (*cursor == '\0') break;
                    nob_sb_append(&sb, *cursor);
                    cursor++;
                } else {
                    cursor += adv;
                }
            }

            regfree(&re);
            char *out_buf = (char*)arena_alloc(eval_temp_arena(ctx), sb.count + 1);
            if (!out_buf) {
                nob_sb_free(sb);
                EVAL_OOM_RETURN_IF_NULL(ctx, out_buf, !eval_should_stop(ctx));
            }
            if (sb.count) memcpy(out_buf, sb.items, sb.count);
            out_buf[sb.count] = '\0';
            nob_sb_free(sb);
            (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(out_buf));
            return !eval_should_stop(ctx);
        }

        if (eval_sv_eq_ci_lit(a.items[1], "MATCHALL")) {
            if (a.count < 5) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(REGEX MATCHALL) requires regex, out-var and input"),
                               nob_sv_from_cstr("Usage: string(REGEX MATCHALL <regex> <out-var> <input>...)"));
                return !eval_should_stop(ctx);
            }

            String_View pattern = a.items[2];
            String_View out_var = a.items[3];
            String_View input = (a.count > 4) ? string_join_no_sep_temp(ctx, &a.items[4], a.count - 4) : nob_sv_from_cstr("");
            if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

            char *pat_buf = (char*)arena_alloc(eval_temp_arena(ctx), pattern.count + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, pat_buf, !eval_should_stop(ctx));
            memcpy(pat_buf, pattern.data, pattern.count);
            pat_buf[pattern.count] = '\0';

            char *in_buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, in_buf, !eval_should_stop(ctx));
            memcpy(in_buf, input.data, input.count);
            in_buf[input.count] = '\0';

            regex_t re;
            if (regcomp(&re, pat_buf, REG_EXTENDED) != 0) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("Invalid regex pattern"), pattern);
                return !eval_should_stop(ctx);
            }

            SV_List matches = {0};
            const char *cursor = in_buf;
            for (;;) {
                regmatch_t m[1];
                int rc = regexec(&re, cursor, 1, m, 0);
                if (rc != 0 || m[0].rm_so < 0 || m[0].rm_eo < m[0].rm_so) break;

                String_View hit = nob_sv_from_parts(cursor + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
                if (!svu_list_push_temp(ctx, &matches, hit)) {
                    regfree(&re);
                    return !eval_should_stop(ctx);
                }

                if (m[0].rm_eo == 0) {
                    if (*cursor == '\0') break;
                    cursor++;
                } else {
                    cursor += m[0].rm_eo;
                }
            }
            regfree(&re);

            String_View out = (matches.count > 0) ? eval_sv_join_semi_temp(ctx, matches.items, matches.count) : nob_sv_from_cstr("");
            (void)eval_var_set(ctx, out_var, out);
            return !eval_should_stop(ctx);
        }

        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                       nob_sv_from_cstr("Unsupported string(REGEX) mode"),
                       nob_sv_from_cstr("Implemented: MATCH, REPLACE, MATCHALL"));
        return !eval_should_stop(ctx);
    }

    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                   nob_sv_from_cstr("Unsupported string() subcommand"),
                   nob_sv_from_cstr("Implemented: APPEND, PREPEND, CONCAT, JOIN, LENGTH, STRIP, FIND, COMPARE, ASCII, HEX, CONFIGURE, MAKE_C_IDENTIFIER, GENEX_STRIP, REPEAT, RANDOM, TIMESTAMP, UUID, MD5, SHA1, SHA224, SHA256, SHA384, SHA512, SHA3_224, SHA3_256, SHA3_384, SHA3_512, REPLACE, TOUPPER, TOLOWER, SUBSTRING, REGEX MATCH, REGEX REPLACE, REGEX MATCHALL, JSON(GET|TYPE|MEMBER|LENGTH|REMOVE|SET|EQUAL)"));
    eval_request_stop_on_error(ctx);
    return !eval_should_stop(ctx);
}
