/* selftest.c — known-answer tests for all three ciphers.
 *
 * Run this BEFORE trusting any performance number. A fast implementation that
 * computes the wrong thing is not a result. `make check` builds and runs it.
 *
 * It includes the workload .c files directly (with main() suppressed) so it
 * tests exactly the code the benchmarks run, not a copy that could drift.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Suppress the benchmark main()s and the harness ROI machinery. */
#define main ascon_main
#include "ascon.c"
#undef main

#define main aes_main
#include "aes.c"
#undef main

#define main des_main
#include "des.c"
#undef main

static int failures = 0;

static void hexdump(const char *label, const uint8_t *b, size_t n) {
    size_t i;
    printf("  %-10s ", label);
    for (i = 0; i < n; ++i) printf("%02X", b[i]);
    printf("\n");
}

static void expect(const char *name, const uint8_t *got, const uint8_t *want,
                   size_t n) {
    if (memcmp(got, want, n) == 0) {
        printf("PASS  %s\n", name);
    } else {
        printf("FAIL  %s\n", name);
        hexdump("expected", want, n);
        hexdump("got", got, n);
        failures++;
    }
}

/* ---------------------------------------------------------------- ASCON --
 * Test vector from the ASCON submission (Count 1 of LWC_AEAD_KAT_128_128):
 *   Key   = 000102...0F, Nonce = 000102...0F, PT = empty, AD = empty
 * Expected CT is the 16-byte tag alone.
 */
static void test_ascon_empty(void) {
    uint8_t key[16], npub[16], ct[32];
    uint8_t want[16] = {
        0xE3,0x55,0x15,0x9F,0x29,0x29,0x11,0xF7,
        0x94,0xCB,0x14,0x32,0xA0,0x10,0x3A,0x8A
    };
    int i;
    uint64_t clen;

    for (i = 0; i < 16; ++i) { key[i] = (uint8_t)i; npub[i] = (uint8_t)i; }

    clen = ascon128_encrypt(ct, NULL, 0, NULL, 0, npub, key);
    if (clen != 16) {
        printf("FAIL  ASCON-128 empty: clen=%llu, expected 16\n",
               (unsigned long long)clen);
        failures++;
        return;
    }
    expect("ASCON-128 KAT (empty PT, empty AD)", ct, want, 16);
}

/* ------------------------------------------------------------------ AES --
 * FIPS-197 Appendix B / C.1: the canonical AES-128 single-block vector.
 *   Key = 000102030405060708090A0B0C0D0E0F
 *   PT  = 00112233445566778899AABBCCDDEEFF
 *   CT  = 69C4E0D86A7B0430D8CDB78070B4C55A
 */
static void test_aes_fips197(void) {
    uint8_t key[16], block[16];
    uint8_t want[16] = {
        0x69,0xC4,0xE0,0xD8,0x6A,0x7B,0x04,0x30,
        0xD8,0xCD,0xB7,0x80,0x70,0xB4,0xC5,0x5A
    };
    aes_roundkey_t rk;
    int i;

    for (i = 0; i < 16; ++i) {
        key[i]   = (uint8_t)i;
        block[i] = (uint8_t)(i * 0x11);
    }

    aes_key_expansion(rk, key);
    aes128_encrypt_block(block, rk);
    expect("AES-128 FIPS-197 block", block, want, 16);
}

/* Sanity: CTR mode must be its own inverse. */
static void test_aes_ctr_roundtrip(void) {
    uint8_t pt[64], ct[64], back[64];
    aes_roundkey_t rk;
    int i;

    for (i = 0; i < 64; ++i) pt[i] = (uint8_t)(i * 7 + 3);

    aes_key_expansion(rk, HZ_KEY);
    aes128_ctr(ct, pt, 64, rk, HZ_NONCE);
    aes128_ctr(back, ct, 64, rk, HZ_NONCE);
    expect("AES-128-CTR roundtrip", back, pt, 64);
}

/* ------------------------------------------------------------------ DES --
 * Classic DES known-answer vector:
 *   Key = 133457799BBCDFF1, PT = 0123456789ABCDEF, CT = 85E813540F0AB405
 */
static void test_des_kat(void) {
    uint64_t subkeys[16];
    uint64_t ct;
    uint8_t got[8];
    uint8_t want[8] = { 0x85,0xE8,0x13,0x54,0x0F,0x0A,0xB4,0x05 };

    des_key_schedule(0x133457799BBCDFF1ULL, subkeys);
    ct = des_encrypt_block(0x0123456789ABCDEFULL, subkeys);
    des_store_be64(got, ct);
    expect("DES known-answer", got, want, 8);
}

/* All-zero key/plaintext vector: CT = 8CA64DE9C1B123A7 */
static void test_des_zero(void) {
    uint64_t subkeys[16];
    uint64_t ct;
    uint8_t got[8];
    uint8_t want[8] = { 0x8C,0xA6,0x4D,0xE9,0xC1,0xB1,0x23,0xA7 };

    des_key_schedule(0ULL, subkeys);
    ct = des_encrypt_block(0ULL, subkeys);
    des_store_be64(got, ct);
    expect("DES all-zero vector", got, want, 8);
}

static void test_des_ctr_roundtrip(void) {
    uint8_t pt[64], ct[64], back[64];
    uint64_t subkeys[16];
    int i;

    for (i = 0; i < 64; ++i) pt[i] = (uint8_t)(i * 5 + 1);

    des_key_schedule(des_load_be64(HZ_KEY), subkeys);
    des_ctr(ct, pt, 64, subkeys, HZ_NONCE);
    des_ctr(back, ct, 64, subkeys, HZ_NONCE);
    expect("DES-CTR roundtrip", back, pt, 64);
}

int main(void) {
    printf("Known-answer tests for the three benchmark ciphers\n");
    printf("--------------------------------------------------\n");

    test_ascon_empty();
    test_aes_fips197();
    test_aes_ctr_roundtrip();
    test_des_kat();
    test_des_zero();
    test_des_ctr_roundtrip();

    printf("--------------------------------------------------\n");
    if (failures == 0) {
        printf("All tests passed. Performance numbers from these binaries are\n");
        printf("measuring correct cipher implementations.\n");
        return 0;
    }
    printf("%d test(s) FAILED. Do not report performance numbers until fixed.\n",
           failures);
    return 1;
}
