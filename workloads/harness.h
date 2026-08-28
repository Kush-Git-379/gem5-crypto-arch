/* harness.h — shared benchmark scaffolding for all three cipher workloads.
 *
 * The point of this file is that ASCON, AES and DES see an IDENTICAL
 * environment: same buffer setup, same PRNG, same ROI markers, same output
 * path. Any difference in the gem5 stats between workloads must come from the
 * cipher itself, not from the scaffolding around it.
 *
 * ROI markers: gem5 SE mode understands the m5 pseudo-instructions. We reset
 * stats at the top of the timed loop and dump them at the bottom, so setup and
 * teardown are excluded. Without this, N in the low thousands is dominated by
 * process startup.
 *
 * Build with -DGEM5_ROI to enable the markers (needs util/m5 headers+lib).
 * Without it the markers compile to nothing, so the same source runs natively
 * for correctness checking.
 */
#ifndef HARNESS_H
#define HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef GEM5_ROI
#include <gem5/m5ops.h>
#define ROI_BEGIN() do { m5_reset_stats(0, 0); } while (0)
#define ROI_END()   do { m5_dump_stats(0, 0); } while (0)
#else
#define ROI_BEGIN() do { } while (0)
#define ROI_END()   do { } while (0)
#endif

/* Number of plaintext blocks processed inside the ROI.
 * Overridable at build time: -DN_BLOCKS=2000
 * Kept deliberately small — O3 simulation in a VM is orders of magnitude
 * slower than TimingSimpleCPU. Calibrate before raising this. */
#ifndef N_BLOCKS
#define N_BLOCKS 1000
#endif

/* Bytes of plaintext per block. 64 B is a realistic IoT sensor payload and is
 * a whole number of blocks for all three ciphers (ASCON rate 8, AES 16, DES 8). */
#ifndef BLOCK_BYTES
#define BLOCK_BYTES 64
#endif

/* xorshift64* — deterministic, tiny, and identical across all workloads.
 * Deliberately NOT rand(): libc rand() would inject a different instruction
 * mix per platform and pollute the comparison. */
static uint64_t hz_rng_state = 0x243F6A8885A308D3ULL; /* pi fractional bits */

static inline uint64_t hz_rand64(void) {
    uint64_t x = hz_rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    hz_rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

/* Fill a buffer with deterministic pseudo-random bytes. */
static void hz_fill(uint8_t *buf, size_t len) {
    size_t i = 0;
    while (i + 8 <= len) {
        uint64_t v = hz_rand64();
        memcpy(buf + i, &v, 8);
        i += 8;
    }
    while (i < len) {
        buf[i++] = (uint8_t)(hz_rand64() & 0xFF);
    }
}

/* Fixed key and nonce/IV, identical bytes across all three workloads so no
 * workload gets a data-dependent advantage from key material. */
static const uint8_t HZ_KEY[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};
static const uint8_t HZ_NONCE[16] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

/* Checksum folded over every ciphertext block. This exists to stop the
 * optimiser deleting the encryption as dead code — the result is consumed and
 * printed. Printing happens OUTSIDE the ROI. */
static inline uint64_t hz_accumulate(uint64_t acc, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        acc = acc * 1099511628211ULL ^ buf[i]; /* FNV-1a style */
    }
    return acc;
}

static void hz_report(const char *name, uint64_t checksum) {
    printf("%s: blocks=%d block_bytes=%d checksum=%016llx\n",
           name, (int)N_BLOCKS, (int)BLOCK_BYTES,
           (unsigned long long)checksum);
}

#endif /* HARNESS_H */
