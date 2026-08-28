# Microarchitectural Analysis of Lightweight Cryptography

Why is ASCON-128 dramatically faster than software AES-128 on general-purpose
hardware — and what does the answer say about processor design for IoT and edge
workloads?

**Status: in progress.** Results below are placeholders until measured.
No number appears here that did not come from a run in `results/`.

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

| | Experiment | Question |
|---|---|---|
| **E1** | Baseline profile | IPC, instruction mix, miss rates, branch behaviour, stall attribution |
| **E2** | Issue width 1 → 8 | Does ASCON scale with execution resources where AES does not? |
| **E3** | L1D latency sweep | Does AES degrade with load latency while ASCON is unaffected? |

## Results

_Pending._

## Repository

```
reference/   ASCON-128 implementation (self-contained C)
workloads/   Benchmark sources and build
configs/     gem5 configuration scripts
scripts/     Sweep drivers, stats parsing, plotting
results/     Raw stats.txt, parsed CSVs, figures
docs/        Findings log and final report
```

## Build and run

_To be documented once the harness is working._

---

**Kush Mehta** — Electronics & Communication Engineering, Nirma University
[kushmehta.vercel.app](https://kushmehta.vercel.app) · [github.com/Kush-Git-379](https://github.com/Kush-Git-379)
