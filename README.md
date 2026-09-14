# Microarchitectural Analysis of Lightweight Cryptography

Where does ASCON-128's throughput advantage over software AES-128 actually come
from at the microarchitectural level — and what does the answer say about
processor design for IoT and edge workloads?

**Status: experiments complete, report in draft.** All three experiments
(E1 baseline, E2 issue-width sweep, E3 L1D-latency sweep) have been run and
analysed — see [`docs/FINDINGS.md`](docs/FINDINGS.md) for the full log and
[`docs/REPORT.md`](docs/REPORT.md) for the write-up in progress. No number
appears here that did not come from a run in `results/`.

---

## Motivation

Two earlier projects of mine left a question open.

A wall-clock benchmark across 5,000 IoT payloads found **ASCON-128 running 78×
faster than AES-GCM**. A separate gem5 cache study found software AES to be
completely **cache-insensitive** — a flat 0.14% L1D miss rate from 4 kB through
64 kB — and concluded its bottleneck lay "in instruction execution, not memory
capacity."

Neither established the mechanism. The first measured *that* ASCON wins; the
second ruled out one explanation without supplying another. This project looks
for the actual cause in the microarchitecture.

### What this project does and does not explain

**It does not decompose the 78×.** That figure compared ASCON-128 against
AES-**GCM** — which adds GHASH authentication on top of the block cipher — and
was measured through Python `ctypes` bindings, so call overhead is folded into
it. It is not a clean cipher-versus-cipher number.

This project asks a narrower and better-posed question: given two ciphers
compiled the same way, run on the same simulated core, in the same mode, with
setup and teardown excluded — where does the remaining gap come from? The
answer is a mechanism, not a speedup multiplier.

For calibration, the three workloads here differ by far less than 78× (see
[`docs/FINDINGS.md`](docs/FINDINGS.md)). That is expected, and it is the point:
a modest, honestly-measured gap with an identified cause is worth more than a
large one whose cause is unknown.

## Hypothesis

ASCON's advantage is primarily an **instruction-level-parallelism** effect rather
than a memory one:

- ASCON's permutation is pure ARX-style register work — XOR, AND, NOT and
  rotations across five 64-bit words, with five mutually independent S-box lines
  that should fill issue slots well.
- Software AES depends on S-box lookups. Even when those hit L1 — and the earlier
  study showed they do — they carry load-to-use latency and occupy load/store
  queue entries, serialising the round function.

**Prediction:** ASCON shows higher IPC and scales with issue width; AES plateaus,
bound by load-dependency chains rather than execution resources.

If the measurements disagree, the writeup will say so.

## Method

Three static workloads — ASCON-128, software AES-128 (compiled `-mno-aes`, so no
hardware acceleration), and DES — under **gem5 v25.1 with `DerivO3CPU`**.

The out-of-order model matters: the earlier study used `TimingSimpleCPU`, which
has no pipeline model and therefore cannot support any claim about
instruction-level parallelism.

**Controlling the comparison.** The workloads share one harness: identical
buffers, identical deterministic PRNG, identical fixed key and nonce, and
`m5_reset_stats`/`m5_dump_stats` markers so process startup and teardown fall
outside the measured region. AES and DES both run in **CTR mode**, and ASCON in
its native AEAD mode, so all three are keystream-style stream processing rather
than one being handicapped by a mode the others do not pay for. AES-GCM is
deliberately *not* used: its GHASH step would measure authentication arithmetic,
not the S-box behaviour the hypothesis is about.

All three are verified against published test vectors (ASCON KAT, AES FIPS-197,
DES known-answer) before any performance number is taken — `make check` in
`workloads/`.

| | Experiment | Question |
|---|---|---|
| **E1** | Baseline profile | IPC, instruction mix, miss rates, branch behaviour, stall attribution |
| **E2** | Issue width 1 → 8 | Does ASCON scale with execution resources where AES does not? |
| **E3** | L1D latency sweep | Does AES degrade with load latency while ASCON is unaffected? |

## Results

ASCON's IPC advantage over AES (3.08 vs 1.32 at 8-wide issue, E1) traces to
load *density and dependency-chain position*, not cache misses or per-load
latency: AES's S-box loads are 21.4% of committed instructions and sit
back-to-back on the round function's critical path, which saturates the
ROB/IQ once issue width allows it (E2: AES scales only 2.55× from width 1→8
vs ASCON's 4.46×) and exposes AES's throughput directly to L1D latency
despite no individual load being slow (E3: AES loses 42.5% IPC from 1→4
cycle L1D latency vs 1.6% for ASCON). Full numbers, figures, and the
falsified intermediate hypothesis are in
[`docs/FINDINGS.md`](docs/FINDINGS.md); the structured write-up is
[`docs/REPORT.md`](docs/REPORT.md).

## Repository

```
reference/   Original Jan 2026 ASCON-128 implementation, kept as-is
workloads/   ascon.c, aes.c, des.c, shared harness.h, selftest.c, Makefile
configs/     o3_crypto.py — parameterised DerivO3CPU config
scripts/     run_sweep.sh, parse_stats.py, plot_results.py
results/     Raw stats.txt, parsed CSVs, figures
docs/        VM-SETUP.md, FINDINGS.md, REPORT.md
```

A note on `reference/ascon.c`: it is preserved unmodified as the artifact of the
earlier project. `workloads/ascon.c` derives from it but corrects a round-constant
bug that made the 6-round permutation use the 12-round constants — see the header
comment there and the 2026-08-29 entry in [`docs/FINDINGS.md`](docs/FINDINGS.md).
The fix is microarchitecturally neutral; it does not change the instruction mix.

## Build and run

Requires gem5 v25.1 built for X86 and its `m5` utility library. Full setup,
including the host↔VM workflow, is in [`docs/VM-SETUP.md`](docs/VM-SETUP.md).

```bash
export GEM5_ROOT=$HOME/gem5

# Build gem5's m5 op library once
cd $GEM5_ROOT/util/m5 && scons build/x86/out/m5

# Verify the ciphers, then build the static gem5 binaries
cd workloads
make check                      # known-answer tests — must pass first
make M5_PATH=$GEM5_ROOT

# Confirm no hardware AES crept in; must print 0
objdump -d bin/aes.gem5 | grep -ci aesenc

# Size the sweeps before launching them — O3 in a VM is slow
cd ..
./scripts/run_sweep.sh calibrate
./scripts/run_sweep.sh e1       # then e2, e3

# Parse and plot
python3 scripts/parse_stats.py results/raw -o results/parsed.csv
python3 scripts/plot_results.py results/parsed.csv -o results/figures
```

`N_BLOCKS` defaults to 1000 and is overridable (`make N_BLOCKS=500`). The sweep
driver skips runs that already have a `stats.txt`, so an interrupted sweep
resumes rather than restarting.

---

**Kush Mehta** — Electronics & Communication Engineering, Nirma University
[kushmehta.vercel.app](https://kushmehta.vercel.app) · [github.com/Kush-Git-379](https://github.com/Kush-Git-379)
