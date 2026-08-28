/* ascon.c — ASCON-128 AEAD encryption, the anchor workload.
 *
 * PROVENANCE: derived from ../reference/ascon.c (Kush's own implementation from
 * the Jan 2026 benchmark). Two deviations from that file, both deliberate:
 *
 *   1. Round constants. The reference computes the constant as
 *          0xf0 - i*0x10 + i*0x1
 *      with `i` running 0..nr-1. That is correct only for the 12-round
 *      permutation; the 6-round permutation must use the LAST six constants
 *      (i = 6..11), not the first six. The reference restarts at i=0 for every
 *      ascon_P(s,6) call, so its 6-round permutation uses the wrong constants and it
 *      will not match the ASCON test vectors.
 *
 *      Fixed here by indexing a constant table from (12 - nr), which is the
 *      standard formulation.
 *
 *   2. Associated data. The reference accepts `ad`/`adlen` and ignores them.
 *      Here AD is absorbed properly. The benchmark passes adlen=0 anyway, so
 *      this costs nothing in the measured region, but it makes the
 *      implementation honest.
 *
 * NEITHER change alters the microarchitectural character of the workload: the
 * permutation is the same five-word ARX round either way, and the round count
 * is unchanged. The fix matters for the claim "this is ASCON-128", not for the
 * claim "this is what ASCON's instruction mix looks like".
 *
 * The structure being tested: ascon_P() below is pure register work on five 64-bit
 * words. Five mutually independent S-box lines, then five independent linear
 * diffusion lines. Zero table lookups. That is the whole hypothesis.
 */
#include <stdint.h>
#include <string.h>
#include "harness.h"

typedef struct { uint64_t x[5]; } ascon_state_t;

#define ASCON_IV 0x80400c0600000000ULL
#define RATE 8  /* ASCON-128 absorbs 8 bytes per permutation */

/* Round constants, indexed 0..11 for the 12-round permutation.
 * ascon_P(s, 6) uses entries 6..11. */
static const uint64_t ascon_RC[12] = {
    0xf0ULL, 0xe1ULL, 0xd2ULL, 0xc3ULL, 0xb4ULL, 0xa5ULL,
    0x96ULL, 0x87ULL, 0x78ULL, 0x69ULL, 0x5aULL, 0x4bULL
};

static inline uint64_t ascon_ROR(uint64_t x, int n) {
    return (x >> n) | (x << (64 - n));
}

/* The ASCON permutation. This is the function the entire study is about. */
static void ascon_P(ascon_state_t *s, int nr) {
    int i;
    for (i = 12 - nr; i < 12; ++i) {
        uint64_t t0, t1, t2, t3, t4;

        /* Add round constant */
        s->x[2] ^= ascon_RC[i];

        /* Substitution layer, 5-bit S-box applied bitsliced across the words.
         * The five ^= lines below are mutually independent — this is the
         * instruction-level parallelism the hypothesis predicts will fill
         * issue slots on an out-of-order core. */
        s->x[0] ^= s->x[4];
        s->x[4] ^= s->x[3];
        s->x[2] ^= s->x[1];

        t0 = s->x[0]; t1 = s->x[1]; t2 = s->x[2];
        t3 = s->x[3]; t4 = s->x[4];

        s->x[0] ^= (~t1 & t2);
        s->x[1] ^= (~t2 & t3);
        s->x[2] ^= (~t3 & t4);
        s->x[3] ^= (~t4 & t0);
        s->x[4] ^= (~t0 & t1);

        s->x[1] ^= s->x[0];
        s->x[0] ^= s->x[4];
        s->x[3] ^= s->x[2];
        s->x[2] ^= 0xFFFFFFFFFFFFFFFFULL;

        /* Linear diffusion layer — again five independent lines. */
        s->x[0] ^= ascon_ROR(s->x[0], 19) ^ ascon_ROR(s->x[0], 28);
        s->x[1] ^= ascon_ROR(s->x[1], 61) ^ ascon_ROR(s->x[1], 39);
        s->x[2] ^= ascon_ROR(s->x[2],  1) ^ ascon_ROR(s->x[2],  6);
        s->x[3] ^= ascon_ROR(s->x[3], 10) ^ ascon_ROR(s->x[3], 17);
        s->x[4] ^= ascon_ROR(s->x[4],  7) ^ ascon_ROR(s->x[4], 41);
    }
}

/* ASCON stores words big-endian. */
static uint64_t ascon_load_be64(const uint8_t *p) {
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}

static void ascon_store_be64(uint8_t *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; ++i)
        p[i] = (uint8_t)(v >> (56 - 8 * i));
}

/* ASCON-128 AEAD encryption. Returns ciphertext length (mlen + 16 tag bytes). */
static uint64_t ascon128_encrypt(uint8_t *c, const uint8_t *m, uint64_t mlen,
                                 const uint8_t *ad, uint64_t adlen,
                                 const uint8_t *npub, const uint8_t *k) {
    ascon_state_t s;
    uint64_t K0 = ascon_load_be64(k);
    uint64_t K1 = ascon_load_be64(k + 8);
    uint64_t N0 = ascon_load_be64(npub);
    uint64_t N1 = ascon_load_be64(npub + 8);
    uint64_t clen = 0;
    uint64_t i;

    /* Initialisation */
    s.x[0] = ASCON_IV;
    s.x[1] = K0;
    s.x[2] = K1;
    s.x[3] = N0;
    s.x[4] = N1;
    ascon_P(&s, 12);
    s.x[3] ^= K0;
    s.x[4] ^= K1;

    /* Associated data */
    if (adlen > 0) {
        while (adlen >= RATE) {
            s.x[0] ^= ascon_load_be64(ad);
            ascon_P(&s, 6);
            ad += RATE;
            adlen -= RATE;
        }
        /* Padded final AD block */
        {
            uint64_t last = 0;
            for (i = 0; i < adlen; ++i)
                last |= (uint64_t)ad[i] << (56 - 8 * i);
            last |= 0x80ULL << (56 - 8 * adlen);
            s.x[0] ^= last;
            ascon_P(&s, 6);
        }
    }
    /* Domain separation */
    s.x[4] ^= 1;

    /* Plaintext */
    while (mlen >= RATE) {
        s.x[0] ^= ascon_load_be64(m);
        ascon_store_be64(c, s.x[0]);
        ascon_P(&s, 6);
        m += RATE;
        c += RATE;
        mlen -= RATE;
        clen += RATE;
    }
    /* Padded final plaintext block — no permutation after it */
    {
        uint64_t last = 0;
        for (i = 0; i < mlen; ++i)
            last |= (uint64_t)m[i] << (56 - 8 * i);
        last |= 0x80ULL << (56 - 8 * mlen);
        s.x[0] ^= last;
        for (i = 0; i < mlen; ++i)
            c[i] = (uint8_t)(s.x[0] >> (56 - 8 * i));
        c += mlen;
        clen += mlen;
    }

    /* Finalisation */
    s.x[1] ^= K0;
    s.x[2] ^= K1;
    ascon_P(&s, 12);
    s.x[3] ^= K0;
    s.x[4] ^= K1;

    ascon_store_be64(c, s.x[3]);
    ascon_store_be64(c + 8, s.x[4]);
    clen += 16;

    return clen;
}

int main(void) {
    static uint8_t plaintext[BLOCK_BYTES];
    static uint8_t ciphertext[BLOCK_BYTES + 16]; /* +16 for the tag */
    uint64_t checksum = 0;
    int i;

    /* --- setup, outside the ROI --- */
    hz_fill(plaintext, BLOCK_BYTES);

    /* --- measured region --- */
    ROI_BEGIN();
    for (i = 0; i < N_BLOCKS; ++i) {
        uint64_t clen = ascon128_encrypt(ciphertext, plaintext, BLOCK_BYTES,
                                         NULL, 0, HZ_NONCE, HZ_KEY);
        checksum = hz_accumulate(checksum, ciphertext, (size_t)clen);
    }
    ROI_END();
    /* --- end measured region --- */

    hz_report("ASCON-128", checksum);
    return 0;
}
