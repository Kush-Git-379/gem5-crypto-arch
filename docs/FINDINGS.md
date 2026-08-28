# Findings Log

Append as results land. Do not wait until the end — the writeup is far easier when
this exists, and dated entries make the eventual report honest about what was found
when.

**Rule: every number here comes from an actual run, with the `results/` path that
produced it. Nothing estimated or remembered.**

---

## Template

```
## YYYY-MM-DD — <what was run>

**Config:** <CPU model, issue width, cache params, N blocks>
**Command:** <exact gem5 invocation>
**Raw stats:** results/<path>

| Workload | IPC | L1D miss | L1I miss | Branch mispred | ROB-full cycles |
|---|---|---|---|---|---|
| ASCON-128 | | | | | |
| AES-128 | | | | | |
| DES | | | | | |

**Observation:** <what the numbers say>
**Bearing on hypothesis:** <supports / contradicts / inconclusive — and why>
**Next:** <what this implies to run next>
```

---

## Hypothesis under test

ASCON-128's throughput advantage over software AES-128 is primarily an
**instruction-level-parallelism effect**, not a memory-capacity effect: ASCON's
ARX register work fills issue slots, while AES's S-box loads serialise its round
function through load-to-use latency.

**Predictions:**
- E1 — ASCON shows higher IPC; AES shows a higher load fraction in its instruction
  mix and more LSQ/IQ stall cycles.
- E2 — ASCON's IPC scales with issue width 1→8; AES plateaus early.
- E3 — AES degrades as L1D latency rises; ASCON is largely unaffected.

A falsified prediction, clearly reported, is a legitimate and publishable-quality
result. Record what actually happened.

---

## Entries

<!-- newest at the bottom -->

## 2026-08-29 — Setup: workloads built and verified (no gem5 data yet)

**This entry contains no microarchitectural results.** It records the harness
being built and the correctness of the three cipher implementations, so the
later performance entries rest on something checked.

### Implementations

All three workloads share `workloads/harness.h`: identical buffer setup,
identical deterministic xorshift64* PRNG (not libc `rand()`, which would inject
a platform-dependent instruction mix), identical fixed key/nonce, identical
FNV-style checksum to stop the optimiser deleting the cipher as dead code, and
`m5_reset_stats`/`m5_dump_stats` ROI markers around the timed loop only.

- **ASCON-128** — derived from `reference/ascon.c`, with two fixes.
- **AES-128-CTR** — software S-box implementation, `-mno-aes`, xtime-based
  MixColumns (not T-tables), branch-free `xtime` so no data-dependent branch
  pollutes the branch-misprediction stat.
- **DES-CTR** — compact reference implementation, bit-permutation heavy.

### Bug found in `reference/ascon.c`

The reference computes its round constant as `0xf0 - i*0x10 + i*0x1` with `i`
running `0..nr-1`. That is correct only for the 12-round permutation. The
6-round permutation must use constants `i = 6..11`, but the reference restarts
at `i = 0` on every `P(s,6)` call, so it uses the wrong six constants.

`workloads/ascon.c` fixes this by indexing a constant table from `12 - nr`, and
additionally absorbs associated data (the reference accepted `ad`/`adlen` and
silently ignored them).

**Bearing on the study:** none microarchitecturally — the permutation is the
same five-word ARX round with the same round count either way, so the
instruction mix and dependency structure are unchanged. It matters for the
claim "this is ASCON-128", not for "this is what ASCON's instruction mix looks
like". `reference/ascon.c` is left untouched as the original Jan 2026 artifact.

### Known-answer tests — all pass

`workloads/selftest.c`, run via `make check`:

| Test | Result |
|---|---|
| ASCON-128 official KAT (empty PT, empty AD) | PASS |
| AES-128 FIPS-197 Appendix B/C.1 block | PASS |
| AES-128-CTR roundtrip | PASS |
| DES KAT (key 133457799BBCDFF1) | PASS |
| DES all-zero vector | PASS |
| DES-CTR roundtrip | PASS |

The ASCON KAT passing confirms the round-constant fix was the real bug.

### Native calibration timing — NOT a result

Compiled on the Windows host with TDM-GCC 10.3 at `-O2 -mno-aes
-fno-tree-vectorize`, N=200000 blocks x 64 B, single crude wall-clock run each:

| Workload | Wall time |
|---|---|
| ASCON-128 | 346 ms |
| AES-128-CTR | 512 ms |
| DES-CTR | 2900 ms |

**This is host wall-clock, not gem5, on a different compiler and OS from the
experiment. It exists only to sanity-check that the workloads run and to size
N.** No claim in the report may cite it.

**One thing it does flag:** ASCON is ~1.5x faster than AES-CTR here, not 78x.
That is not a contradiction of the Jan 2026 benchmark — that figure compared
against AES-**GCM**, which adds GHASH authentication, and was measured through
Python/ctypes. But it means the writeup must be precise: this project explains
the microarchitectural gap between *these* implementations. It must not imply
it is decomposing the 78x, because it is not.

**Bearing on hypothesis:** nothing yet. No gem5 run has been made.

**Next:** in the VM — build `libm5.a`, `make check`, verify `objdump | grep -c
aesenc` is 0 for the AES binary, then `./scripts/run_sweep.sh calibrate` to
size the sweeps before launching E1.
