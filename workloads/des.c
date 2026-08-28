/* des.c — compact DES, CTR mode, for the third microarchitectural data point.
 *
 * DES is interesting here precisely because its profile matches neither ASCON's
 * nor AES's: it is dominated by BIT PERMUTATION (IP, PC-1, PC-2, E, P, FP),
 * which in software becomes long chains of shift/mask/OR where every step
 * depends on the previous one. Small S-box loads exist (8 x 64 entries) but the
 * permutations dominate.
 *
 * So the expected ordering, if the ILP hypothesis is right, is:
 *   ASCON  — wide, independent register work        -> highest IPC
 *   DES    — serial bit-twiddling, few loads        -> middling IPC, ALU-bound
 *   AES    — load-to-use chains through the S-box   -> lowest IPC, LSQ-bound
 * That DES sits between them for a *different* reason is what makes it a useful
 * control rather than just a third bar on a chart.
 */
#include <stdint.h>
#include <string.h>
#include "harness.h"

/* Initial permutation */
static const uint8_t des_IP[64] = {
  58,50,42,34,26,18,10,2, 60,52,44,36,28,20,12,4,
  62,54,46,38,30,22,14,6, 64,56,48,40,32,24,16,8,
  57,49,41,33,25,17, 9,1, 59,51,43,35,27,19,11,3,
  61,53,45,37,29,21,13,5, 63,55,47,39,31,23,15,7
};

/* Final permutation (inverse of IP) */
static const uint8_t des_FP[64] = {
  40,8,48,16,56,24,64,32, 39,7,47,15,55,23,63,31,
  38,6,46,14,54,22,62,30, 37,5,45,13,53,21,61,29,
  36,4,44,12,52,20,60,28, 35,3,43,11,51,19,59,27,
  34,2,42,10,50,18,58,26, 33,1,41, 9,49,17,57,25
};

/* Expansion: 32 -> 48 bits */
static const uint8_t des_E[48] = {
  32, 1, 2, 3, 4, 5,  4, 5, 6, 7, 8, 9,
   8, 9,10,11,12,13, 12,13,14,15,16,17,
  16,17,18,19,20,21, 20,21,22,23,24,25,
  24,25,26,27,28,29, 28,29,30,31,32, 1
};

/* Permutation P, applied after the S-boxes */
static const uint8_t des_P[32] = {
  16, 7,20,21, 29,12,28,17,  1,15,23,26,  5,18,31,10,
   2, 8,24,14, 32,27, 3, 9, 19,13,30, 6, 22,11, 4,25
};

/* Permuted choice 1: 64 -> 56 bits (drops parity bits) */
static const uint8_t des_PC1[56] = {
  57,49,41,33,25,17, 9,  1,58,50,42,34,26,18,
  10, 2,59,51,43,35,27, 19,11, 3,60,52,44,36,
  63,55,47,39,31,23,15,  7,62,54,46,38,30,22,
  14, 6,61,53,45,37,29, 21,13, 5,28,20,12, 4
};

/* Permuted choice 2: 56 -> 48 bits */
static const uint8_t des_PC2[48] = {
  14,17,11,24, 1, 5,  3,28,15, 6,21,10,
  23,19,12, 4,26, 8, 16, 7,27,20,13, 2,
  41,52,31,37,47,55, 30,40,51,45,33,48,
  44,49,39,56,34,53, 46,42,50,36,29,32
};

/* Left-rotation schedule per round */
static const uint8_t des_SHIFTS[16] = {
  1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1
};

/* The eight S-boxes, 4 rows x 16 columns each. */
static const uint8_t des_SBOX[8][64] = {
  { 14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,
     0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
     4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,
    15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13 },
  { 15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,
     3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
     0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,
    13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9 },
  { 10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,
    13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
    13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,
     1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12 },
  {  7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,
    13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
    10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,
     3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14 },
  {  2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,
    14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
     4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,
    11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3 },
  { 12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,
    10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
     9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,
     4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13 },
  {  4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,
    13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
     1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,
     6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12 },
  { 13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,
     1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
     7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,
     2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11 }
};

/* Generic bit permutation. src is treated as a big-endian bit string of
 * `inbits` bits; table[i] is the 1-based source bit for output bit i.
 * This function is where DES spends most of its instructions. */
static uint64_t des_permute(uint64_t src, const uint8_t *table, int outbits, int inbits) {
    uint64_t dst = 0;
    int i;
    for (i = 0; i < outbits; ++i) {
        uint64_t bit = (src >> (inbits - table[i])) & 1ULL;
        dst |= bit << (outbits - 1 - i);
    }
    return dst;
}

/* The Feistel f-function: expand, XOR subkey, S-box substitute, des_permute. */
static uint32_t des_feistel(uint32_t r, uint64_t subkey) {
    uint64_t expanded = des_permute((uint64_t)r, des_E, 48, 32) ^ subkey;
    uint32_t out = 0;
    int i;

    for (i = 0; i < 8; ++i) {
        uint8_t six = (uint8_t)((expanded >> (42 - 6 * i)) & 0x3F);
        /* row = outer two bits, col = inner four bits */
        uint8_t row = (uint8_t)(((six & 0x20) >> 4) | (six & 0x01));
        uint8_t col = (uint8_t)((six >> 1) & 0x0F);
        out |= (uint32_t)(des_SBOX[i][row * 16 + col] & 0x0F) << (28 - 4 * i);
    }

    return (uint32_t)des_permute((uint64_t)out, des_P, 32, 32);
}

/* Expand a 64-bit key into 16 x 48-bit subkeys. */
static void des_key_schedule(uint64_t key, uint64_t subkeys[16]) {
    uint64_t permuted = des_permute(key, des_PC1, 56, 64);
    uint32_t c = (uint32_t)((permuted >> 28) & 0x0FFFFFFF);
    uint32_t d = (uint32_t)(permuted & 0x0FFFFFFF);
    int i;

    for (i = 0; i < 16; ++i) {
        int s = des_SHIFTS[i];
        c = ((c << s) | (c >> (28 - s))) & 0x0FFFFFFF;
        d = ((d << s) | (d >> (28 - s))) & 0x0FFFFFFF;
        subkeys[i] = des_permute(((uint64_t)c << 28) | (uint64_t)d, des_PC2, 48, 56);
    }
}

/* Encrypt one 64-bit block. */
static uint64_t des_encrypt_block(uint64_t block, const uint64_t subkeys[16]) {
    uint64_t permuted = des_permute(block, des_IP, 64, 64);
    uint32_t l = (uint32_t)(permuted >> 32);
    uint32_t r = (uint32_t)(permuted & 0xFFFFFFFFULL);
    int i;

    for (i = 0; i < 16; ++i) {
        uint32_t tmp = r;
        r = l ^ des_feistel(r, subkeys[i]);
        l = tmp;
    }

    /* Note the final swap: the preoutput is R16||L16, not L16||R16. */
    return des_permute(((uint64_t)r << 32) | (uint64_t)l, des_FP, 64, 64);
}

static uint64_t des_load_be64(const uint8_t *p) {
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}

static void des_store_be64(uint8_t *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; ++i)
        p[i] = (uint8_t)(v >> (56 - 8 * i));
}

/* CTR mode over 8-byte blocks, mirroring the AES harness. */
static void des_ctr(uint8_t *out, const uint8_t *in, size_t len,
                    const uint64_t subkeys[16], const uint8_t *nonce) {
    uint64_t nonce_hi = des_load_be64(nonce) & 0xFFFFFFFF00000000ULL;
    uint32_t ctr = 0;
    size_t i;

    for (i = 0; i < len; i += 8) {
        size_t n = (len - i < 8) ? (len - i) : 8;
        size_t j;
        uint64_t counter = nonce_hi | (uint64_t)ctr;
        uint64_t ks = des_encrypt_block(counter, subkeys);
        uint8_t ksb[8];

        ctr++;
        des_store_be64(ksb, ks);

        for (j = 0; j < n; ++j)
            out[i + j] = in[i + j] ^ ksb[j];
    }
}

int main(void) {
    static uint8_t plaintext[BLOCK_BYTES];
    static uint8_t ciphertext[BLOCK_BYTES];
    uint64_t subkeys[16];
    uint64_t checksum = 0;
    int i;

    /* --- setup, outside the ROI --- */
    hz_fill(plaintext, BLOCK_BYTES);
    des_key_schedule(des_load_be64(HZ_KEY), subkeys);

    /* --- measured region --- */
    ROI_BEGIN();
    for (i = 0; i < N_BLOCKS; ++i) {
        des_ctr(ciphertext, plaintext, BLOCK_BYTES, subkeys, HZ_NONCE);
        checksum = hz_accumulate(checksum, ciphertext, BLOCK_BYTES);
    }
    ROI_END();
    /* --- end measured region --- */

    hz_report("DES-CTR", checksum);
    return 0;
}
