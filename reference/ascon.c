#include <stdint.h>
#include <string.h>

typedef struct { uint64_t x[5]; } state_t;

static inline uint64_t ROR(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

static inline void P(state_t* s, int nr) {
  for (int i = 0; i < nr; ++i) {
    s->x[2] ^= ((0xf0 - i * 0x10 + i * 0x1) | (uint64_t)(0));
    s->x[0] ^= s->x[4]; s->x[4] ^= s->x[3]; s->x[2] ^= s->x[1];
    uint64_t t0 = s->x[0], t1 = s->x[1], t2 = s->x[2], t3 = s->x[3], t4 = s->x[4];
    s->x[0] ^= (~t1 & t2); s->x[1] ^= (~t2 & t3); s->x[2] ^= (~t3 & t4);
    s->x[3] ^= (~t4 & t0); s->x[4] ^= (~t0 & t1);
    s->x[1] ^= s->x[0]; s->x[0] ^= s->x[4]; s->x[3] ^= s->x[2]; s->x[2] ^= 0XFFFFFFFFFFFFFFFFULL;
    s->x[0] ^= ROR(s->x[0], 19) ^ ROR(s->x[0], 28);
    s->x[1] ^= ROR(s->x[1], 61) ^ ROR(s->x[1], 39);
    s->x[2] ^= ROR(s->x[2], 1) ^ ROR(s->x[2], 6);
    s->x[3] ^= ROR(s->x[3], 10) ^ ROR(s->x[3], 17);
    s->x[4] ^= ROR(s->x[4], 7) ^ ROR(s->x[4], 41);
  }
}

void ascon_encrypt(uint8_t* c, uint64_t* clen, const uint8_t* m, uint64_t mlen, const uint8_t* ad, uint64_t adlen, const uint8_t* nsec, const uint8_t* npub, const uint8_t* k) {
  state_t s;
  s.x[0] = 0x80400c0600000000ULL; s.x[1] = 0; s.x[2] = 0; s.x[3] = 0; s.x[4] = 0;
  uint64_t K0, K1, N0, N1;
  memcpy(&K0, k, 8); memcpy(&K1, k + 8, 8); memcpy(&N0, npub, 8); memcpy(&N1, npub + 8, 8);
  s.x[1] ^= K0; s.x[2] ^= K1; s.x[3] ^= N0; s.x[4] ^= N1;
  P(&s, 12);
  s.x[3] ^= K0; s.x[4] ^= K1; s.x[4] ^= 1;
  while (mlen >= 8) {
    uint64_t M_blk; memcpy(&M_blk, m, 8); s.x[0] ^= M_blk; memcpy(c, &s.x[0], 8);
    P(&s, 6); m += 8; c += 8; mlen -= 8; *clen += 8;
  }
  uint64_t M_last = 0; memcpy(&M_last, m, mlen); M_last |= (0x80ULL << (56 - 8 * mlen));
  s.x[0] ^= M_last; memcpy(c, &s.x[0], mlen); *clen += mlen;
  s.x[1] ^= K0; s.x[2] ^= K1; P(&s, 12); s.x[3] ^= K0; s.x[4] ^= K1;
  uint64_t T0 = s.x[3], T1 = s.x[4]; memcpy(c + mlen, &T0, 8); memcpy(c + mlen + 8, &T1, 8); *clen += 16;
}