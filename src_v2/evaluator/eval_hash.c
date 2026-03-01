#include "eval_hash.h"

#include <string.h>
#include <stdint.h>

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


bool eval_hash_compute_hex_temp(Evaluator_Context *ctx,
                                String_View algo,
                                String_View input,
                                String_View *out_hex) {
    return string_hash_compute_temp(ctx, algo, input, out_hex);
}

bool eval_hash_is_supported_algo(String_View algo) {
    return eval_sv_eq_ci_lit(algo, "MD5") ||
           eval_sv_eq_ci_lit(algo, "SHA1") ||
           eval_sv_eq_ci_lit(algo, "SHA224") ||
           eval_sv_eq_ci_lit(algo, "SHA256") ||
           eval_sv_eq_ci_lit(algo, "SHA384") ||
           eval_sv_eq_ci_lit(algo, "SHA512") ||
           eval_sv_eq_ci_lit(algo, "SHA3_224") ||
           eval_sv_eq_ci_lit(algo, "SHA3_256") ||
           eval_sv_eq_ci_lit(algo, "SHA3_384") ||
           eval_sv_eq_ci_lit(algo, "SHA3_512");
}
