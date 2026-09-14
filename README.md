# Microarchitectural Analysis of Lightweight Cryptography

Where does ASCON-128's throughput advantage over software AES-128 actually come
from at the microarchitectural level — and what does the answer say about
processor design for IoT and edge workloads?

**Status: complete.** All three experiments (E1 baseline, E2 issue-width sweep,
E3 L1D-latency sweep) have been run, analysed and written up — see
[`docs/REPORT.md`](docs/REPORT.md) for the findings and
[`docs/FINDINGS.md`](docs/FINDINGS.md) for the dated log, including one
intermediate hypothesis the data falsified. No number appears here that did not
come from a run in `results/`.

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

**The mechanism is load density and dependency-chain position — not cache
misses, and not per-load latency.**

At the 8-wide baseline, all three workloads have near-zero L1D miss rates and
near-identical per-load latency (~3 cycles). What separates them is how many
loads there are and where they sit:

| Workload | IPC | Load frac | L1D miss | IPC scaling 1→8 wide | IPC change, L1D 1→4 cyc |
|---|---|---|---|---|---|
| **ASCON-128** | **3.079** | 5.3% | 0.0035% | **4.46×** | **−1.6%** |
| AES-128 | 1.325 | 21.4% | 0.0001% | 2.55× | −42.5% |
| DES | 2.547 | 7.0% | 0.0003% | 4.08× | −0.0% |

AES commits a load on 21.4% of instructions — 4× ASCON's rate — and those
S-box lookups sit back-to-back on the round function's critical path with too
little independent work to hide them. That structure produces 178,876 ROB-full
and 3,076,715 IQ-full stall events for AES against **3 of each** for ASCON; it
caps AES's issue-width scaling at 2.55× where ASCON reaches 4.46×; and it
leaves AES's throughput directly exposed to L1D latency even though no
individual load is slow.

![IPC versus issue width](results/figures/fig_e2_issue_width.png)

![IPC versus L1D latency](results/figures/fig_e3_l1d_latency.png)

The hypothesis holds end to end, with one honest correction: it originally
named load *latency* as the serialising factor, but per-load latency turned out
to be workload-independent. The real driver is load *density and chain
position*; latency-sensitivity is a downstream symptom of that structure. An
intermediate prediction made after E1 — that AES would prove latency-*in*sensitive
— was falsified by E3 and is recorded as such in
[`docs/FINDINGS.md`](docs/FINDINGS.md) rather than edited out.

Full analysis, figures and threats to validity: [`docs/REPORT.md`](docs/REPORT.md).

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
