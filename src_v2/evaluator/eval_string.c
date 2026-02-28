#include "eval_stdlib.h"

#include "evaluator_internal.h"
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

static void string_sha256_compute(const unsigned char *msg, size_t len, unsigned char out[32]) {
    uint32_t state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };

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

    for (size_t i = 0; i < 8; i++) string_store_be32(out + (i * 4), state[i]);
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

static bool string_hash_compute_temp(Evaluator_Context *ctx, String_View algo, String_View input, String_View *out_hash) {
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

typedef struct {
    String_Json_Type type;
    size_t start;
    size_t end;
} String_Json_Node;

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

static bool string_json_parse_value(String_View json, size_t *io, String_Json_Node *out);

static bool string_json_parse_array(String_View json, size_t *io, String_Json_Node *out) {
    size_t start = *io;
    if (*io >= json.count || json.data[*io] != '[') return false;
    (*io)++;
    string_json_skip_ws(json, io);
    if (*io < json.count && json.data[*io] == ']') {
        (*io)++;
        *out = (String_Json_Node){ .type = STRING_JSON_ARRAY, .start = start, .end = *io };
        return true;
    }

    for (;;) {
        String_Json_Node v = {0};
        if (!string_json_parse_value(json, io, &v)) return false;
        (void)v;
        string_json_skip_ws(json, io);
        if (*io >= json.count) return false;
        if (json.data[*io] == ',') {
            (*io)++;
            string_json_skip_ws(json, io);
            continue;
        }
        if (json.data[*io] == ']') {
            (*io)++;
            *out = (String_Json_Node){ .type = STRING_JSON_ARRAY, .start = start, .end = *io };
            return true;
        }
        return false;
    }
}

static bool string_json_parse_object(String_View json, size_t *io, String_Json_Node *out) {
    size_t start = *io;
    if (*io >= json.count || json.data[*io] != '{') return false;
    (*io)++;
    string_json_skip_ws(json, io);
    if (*io < json.count && json.data[*io] == '}') {
        (*io)++;
        *out = (String_Json_Node){ .type = STRING_JSON_OBJECT, .start = start, .end = *io };
        return true;
    }

    for (;;) {
        size_t k0 = 0, k1 = 0;
        if (!string_json_parse_string_token(json, io, &k0, &k1)) return false;
        (void)k0;
        (void)k1;
        string_json_skip_ws(json, io);
        if (*io >= json.count || json.data[*io] != ':') return false;
        (*io)++;
        string_json_skip_ws(json, io);
        String_Json_Node v = {0};
        if (!string_json_parse_value(json, io, &v)) return false;
        (void)v;
        string_json_skip_ws(json, io);
        if (*io >= json.count) return false;
        if (json.data[*io] == ',') {
            (*io)++;
            string_json_skip_ws(json, io);
            continue;
        }
        if (json.data[*io] == '}') {
            (*io)++;
            *out = (String_Json_Node){ .type = STRING_JSON_OBJECT, .start = start, .end = *io };
            return true;
        }
        return false;
    }
}

static bool string_json_parse_value(String_View json, size_t *io, String_Json_Node *out) {
    if (!io || !out) return false;
    string_json_skip_ws(json, io);
    if (*io >= json.count) return false;

    size_t start = *io;
    char c = json.data[*io];
    if (c == '"') {
        if (!string_json_parse_string_token(json, io, NULL, NULL)) return false;
        *out = (String_Json_Node){ .type = STRING_JSON_STRING, .start = start, .end = *io };
        return true;
    }
    if (c == '{') return string_json_parse_object(json, io, out);
    if (c == '[') return string_json_parse_array(json, io, out);
    if (c == '-' || (c >= '0' && c <= '9')) {
        if (!string_json_parse_number_token(json, io)) return false;
        *out = (String_Json_Node){ .type = STRING_JSON_NUMBER, .start = start, .end = *io };
        return true;
    }
    if (c == 't') {
        if (!string_json_match_lit(json, io, "true")) return false;
        *out = (String_Json_Node){ .type = STRING_JSON_BOOL, .start = start, .end = *io };
        return true;
    }
    if (c == 'f') {
        if (!string_json_match_lit(json, io, "false")) return false;
        *out = (String_Json_Node){ .type = STRING_JSON_BOOL, .start = start, .end = *io };
        return true;
    }
    if (c == 'n') {
        if (!string_json_match_lit(json, io, "null")) return false;
        *out = (String_Json_Node){ .type = STRING_JSON_NULL, .start = start, .end = *io };
        return true;
    }
    return false;
}

static bool string_json_parse_root(String_View json, String_Json_Node *out) {
    if (!out) return false;
    size_t i = 0;
    if (!string_json_parse_value(json, &i, out)) return false;
    string_json_skip_ws(json, &i);
    return i == json.count;
}

static bool string_json_object_find_member(Evaluator_Context *ctx,
                                           String_View json,
                                           String_Json_Node obj,
                                           String_View key,
                                           bool *found,
                                           String_Json_Node *out) {
    if (!ctx || !found || !out || obj.type != STRING_JSON_OBJECT) return false;
    *found = false;

    size_t i = obj.start + 1;
    string_json_skip_ws(json, &i);
    if (i >= obj.end) return false;
    if (json.data[i] == '}') return true;

    for (;;) {
        size_t k0 = 0, k1 = 0;
        if (!string_json_parse_string_token(json, &i, &k0, &k1)) return false;
        String_View key_raw = nob_sv_from_parts(json.data + k0, k1 - k0);
        String_View key_dec = key_raw;
        bool need_decode = false;
        for (size_t t = 0; t < key_raw.count; t++) {
            if (key_raw.data[t] == '\\') {
                need_decode = true;
                break;
            }
        }
        if (need_decode) {
            if (!string_json_decode_string_temp(ctx, key_raw, &key_dec)) return false;
        }

        string_json_skip_ws(json, &i);
        if (i >= obj.end || json.data[i] != ':') return false;
        i++;
        string_json_skip_ws(json, &i);

        String_Json_Node val = {0};
        if (!string_json_parse_value(json, &i, &val)) return false;
        if (nob_sv_eq(key_dec, key)) {
            *found = true;
            *out = val;
            return true;
        }

        string_json_skip_ws(json, &i);
        if (i >= obj.end) return false;
        if (json.data[i] == ',') {
            i++;
            string_json_skip_ws(json, &i);
            continue;
        }
        if (json.data[i] == '}') return true;
        return false;
    }
}

static bool string_json_array_find_index(String_View json,
                                         String_Json_Node arr,
                                         size_t wanted,
                                         bool *found,
                                         String_Json_Node *out) {
    if (!found || !out || arr.type != STRING_JSON_ARRAY) return false;
    *found = false;

    size_t i = arr.start + 1;
    string_json_skip_ws(json, &i);
    if (i >= arr.end) return false;
    if (json.data[i] == ']') return true;

    size_t index = 0;
    for (;;) {
        String_Json_Node val = {0};
        if (!string_json_parse_value(json, &i, &val)) return false;
        if (index == wanted) {
            *found = true;
            *out = val;
            return true;
        }
        index++;
        string_json_skip_ws(json, &i);
        if (i >= arr.end) return false;
        if (json.data[i] == ',') {
            i++;
            string_json_skip_ws(json, &i);
            continue;
        }
        if (json.data[i] == ']') return true;
        return false;
    }
}

static bool string_json_container_length(String_View json, String_Json_Node node, size_t *out_len) {
    if (!out_len) return false;
    *out_len = 0;
    if (node.type == STRING_JSON_OBJECT) {
        size_t i = node.start + 1;
        string_json_skip_ws(json, &i);
        if (i >= node.end) return false;
        if (json.data[i] == '}') return true;
        for (;;) {
            size_t k0 = 0, k1 = 0;
            if (!string_json_parse_string_token(json, &i, &k0, &k1)) return false;
            (void)k0; (void)k1;
            string_json_skip_ws(json, &i);
            if (i >= node.end || json.data[i] != ':') return false;
            i++;
            string_json_skip_ws(json, &i);
            String_Json_Node val = {0};
            if (!string_json_parse_value(json, &i, &val)) return false;
            (void)val;
            (*out_len)++;
            string_json_skip_ws(json, &i);
            if (i >= node.end) return false;
            if (json.data[i] == ',') {
                i++;
                string_json_skip_ws(json, &i);
                continue;
            }
            if (json.data[i] == '}') return true;
            return false;
        }
    }

    if (node.type == STRING_JSON_ARRAY) {
        size_t i = node.start + 1;
        string_json_skip_ws(json, &i);
        if (i >= node.end) return false;
        if (json.data[i] == ']') return true;
        for (;;) {
            String_Json_Node val = {0};
            if (!string_json_parse_value(json, &i, &val)) return false;
            (void)val;
            (*out_len)++;
            string_json_skip_ws(json, &i);
            if (i >= node.end) return false;
            if (json.data[i] == ',') {
                i++;
                string_json_skip_ws(json, &i);
                continue;
            }
            if (json.data[i] == ']') return true;
            return false;
        }
    }

    return false;
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

static bool string_json_get_temp(Evaluator_Context *ctx,
                                 String_View json,
                                 String_Json_Node node,
                                 String_View *out) {
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (node.start > node.end || node.end > json.count) return false;

    if (node.type == STRING_JSON_STRING) {
        size_t i = node.start;
        size_t s0 = 0, s1 = 0;
        if (!string_json_parse_string_token(json, &i, &s0, &s1)) return false;
        String_View raw = nob_sv_from_parts(json.data + s0, s1 - s0);
        return string_json_decode_string_temp(ctx, raw, out);
    }
    if (node.type == STRING_JSON_NUMBER) {
        *out = nob_sv_from_parts(json.data + node.start, node.end - node.start);
        return true;
    }
    if (node.type == STRING_JSON_BOOL) {
        if (node.end - node.start == 4 && memcmp(json.data + node.start, "true", 4) == 0) {
            *out = nob_sv_from_cstr("ON");
        } else {
            *out = nob_sv_from_cstr("OFF");
        }
        return true;
    }
    if (node.type == STRING_JSON_NULL) {
        *out = nob_sv_from_cstr("");
        return true;
    }
    *out = nob_sv_from_parts(json.data + node.start, node.end - node.start);
    return true;
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
        eval_sv_eq_ci_lit(a.items[0], "SHA256")) {
        if (a.count != 3) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(MD5/SHA1/SHA256) requires output variable and input"),
                           nob_sv_from_cstr("Usage: string(MD5|SHA1|SHA256 <out-var> <input>)"));
            return !eval_should_stop(ctx);
        }
        String_View hash = nob_sv_from_cstr("");
        if (!string_hash_compute_temp(ctx, a.items[0], a.items[2], &hash)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("Unsupported hash algorithm"),
                           a.items[0]);
            return !eval_should_stop(ctx);
        }
        (void)eval_var_set(ctx, a.items[1], hash);
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "JSON")) {
        if (a.count < 4) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(JSON) requires out-var, mode and JSON string"),
                           nob_sv_from_cstr("Usage: string(JSON <out-var> GET|TYPE|LENGTH <json> [path...])"));
            return !eval_should_stop(ctx);
        }
        String_View out_var = a.items[1];
        String_View json_mode = a.items[2];
        String_View json_text = a.items[3];
        String_Json_Node current = {0};
        if (!string_json_parse_root(json_text, &current)) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(JSON) received invalid JSON input"),
                           json_text);
            return !eval_should_stop(ctx);
        }

        for (size_t i = 4; i < a.count; i++) {
            if (current.type == STRING_JSON_OBJECT) {
                bool found = false;
                String_Json_Node next = {0};
                if (!string_json_object_find_member(ctx, json_text, current, a.items[i], &found, &next)) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("string(JSON) failed to parse object member access"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                if (!found) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("string(JSON) object member not found"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                current = next;
                continue;
            }

            if (current.type == STRING_JSON_ARRAY) {
                long long raw_idx = -1;
                if (!sv_parse_i64(a.items[i], &raw_idx) || raw_idx < 0) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("string(JSON) array index must be non-negative integer"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                bool found = false;
                String_Json_Node next = {0};
                if (!string_json_array_find_index(json_text, current, (size_t)raw_idx, &found, &next)) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("string(JSON) failed to parse array access"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                if (!found) {
                    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                                   nob_sv_from_cstr("string(JSON) array index out of range"),
                                   a.items[i]);
                    return !eval_should_stop(ctx);
                }
                current = next;
                continue;
            }

            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                           nob_sv_from_cstr("string(JSON) path traverses non-container value"),
                           a.items[i]);
            return !eval_should_stop(ctx);
        }

        if (eval_sv_eq_ci_lit(json_mode, "GET")) {
            String_View out = nob_sv_from_cstr("");
            if (!string_json_get_temp(ctx, json_text, current, &out)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(JSON GET) failed to extract value"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            (void)eval_var_set(ctx, out_var, out);
            return !eval_should_stop(ctx);
        }
        if (eval_sv_eq_ci_lit(json_mode, "TYPE")) {
            (void)eval_var_set(ctx, out_var, string_json_type_name(current.type));
            return !eval_should_stop(ctx);
        }
        if (eval_sv_eq_ci_lit(json_mode, "LENGTH")) {
            size_t len = 0;
            if (!string_json_container_length(json_text, current, &len)) {
                eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                               nob_sv_from_cstr("string(JSON LENGTH) requires ARRAY or OBJECT"),
                               nob_sv_from_cstr(""));
                return !eval_should_stop(ctx);
            }
            char num_buf[64];
            snprintf(num_buf, sizeof(num_buf), "%zu", len);
            (void)eval_var_set(ctx, out_var, nob_sv_from_cstr(num_buf));
            return !eval_should_stop(ctx);
        }
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("string"), node->as.cmd.name, o,
                       nob_sv_from_cstr("Unsupported string(JSON) mode"),
                       nob_sv_from_cstr("Implemented: GET, TYPE, LENGTH"));
        return !eval_should_stop(ctx);
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
                   nob_sv_from_cstr("Implemented: APPEND, PREPEND, CONCAT, JOIN, LENGTH, STRIP, FIND, COMPARE, ASCII, HEX, CONFIGURE, MAKE_C_IDENTIFIER, GENEX_STRIP, RANDOM, TIMESTAMP, UUID, MD5, SHA1, SHA256, REPLACE, TOUPPER, TOLOWER, SUBSTRING, REGEX MATCH, REGEX REPLACE, REGEX MATCHALL, JSON(GET|TYPE|LENGTH)"));
    eval_request_stop_on_error(ctx);
    return !eval_should_stop(ctx);
}
