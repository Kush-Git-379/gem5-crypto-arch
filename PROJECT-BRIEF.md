# Microarchitectural Analysis of Lightweight Cryptography

**Owner:** Kush Mehta · **Started:** 29 Aug 2026 · **Hard deadline:** 5 Nov 2026

> **New Claude session: read this file first, then `docs/SESSION-CONTEXT.md`.**
> This is the full spec. You do not need the chat that produced it.

---

## 1. Why this project exists

Kush is applying to US MS programs (Fall 2027 intake, applications submitted by
**late Nov 2026**) targeting **Computer Engineering / embedded systems with a
computer-architecture lean**. His profile is strong on systems programming
(ISRO research internship: real-time C++, Reed–Solomon, protocol decoding) but
**thin on computer architecture** — one course-level gem5 study is the only
evidence.

This project closes that gap. It must be **finished, written up, and on GitHub by
5 Nov 2026** so the SOP can cite completed work, not work in progress.

Success = a project that reads as a genuine investigation with a question, a
hypothesis, a method, and a falsifiable result. Not a tutorial. Not a rebuild of
something with a thousand GitHub clones.

---

## 2. The research question

Two of Kush's earlier projects are in unacknowledged tension:

- **Cryptography Benchmark (Jan 2026)** — wall-clock benchmarking across 5,000 IoT
  payloads found **ASCON-128 ran 78× faster than AES-GCM** and 12× faster than DES.
- **gem5 L1 Cache Study (Feb 2026)** — found AES was **completely cache-insensitive**
  (flat 0.14% miss rate from 4 kB to 64 kB), concluding its bottleneck was
  "instruction execution, not memory capacity" — but never established *what* in
  execution.

The first says ASCON wins. The second says the reason isn't cache capacity. Neither
explains the mechanism.

> **Research question:** Where does ASCON-128's throughput advantage over AES-GCM
> actually come from at the microarchitectural level, and what does that imply for
> processor design targeting IoT and edge workloads?

### Hypothesis (to be confirmed or falsified — either is a result)

ASCON's advantage is **primarily an instruction-level-parallelism effect, not a
memory effect**:

- ASCON's permutation is pure **ARX-style register work** — XOR, AND, NOT, and
  rotations on five 64-bit words (see `reference/ascon.c`, the `P()` function).
  The five S-box lines are mutually independent and should fill issue slots well.
- Software AES depends on **S-box / T-table lookups**, which are loads. Even when
  they hit L1 (as the Feb 2026 study showed they do), they carry load-to-use
  latency and occupy load/store queue entries, serialising the round function.

**Prediction:** ASCON shows markedly higher IPC and scales with issue width and ROB
size; AES stays flat because it is bound by load-to-use dependency chains, not by
execution resources.

**If the data disagrees, report that.** A falsified hypothesis honestly reported is
a stronger application artifact than a fudged confirmation. Do not tune the
experiment until it agrees.

---

## 3. Method

### 3.1 The critical upgrade over the earlier study

The Feb 2026 study used **`TimingSimpleCPU`**, which is in-order, single-issue, and
has no pipeline model. Its IPC numbers cannot support any claim about
instruction-level parallelism.

**This project must use `DerivO3CPU` (out-of-order).** This is the single most
important methodological change and should be stated explicitly in the writeup —
it is what makes the ILP question answerable at all.

### 3.2 Workloads

| Workload | Source | Status | Notes |
|---|---|---|---|
| **ASCON-128** | `reference/ascon.c` (already present) | Ready | Self-contained, no deps. Ideal for SE mode. |
| **AES-128** | Bundled tiny-AES-c or hand-rolled S-box impl | To source | **Must build without AES-NI** (`-mno-aes`). Hardware AES would invalidate the comparison. |
| **DES** | Compact reference implementation | To source | Third data point; heavy bit-permutation profile. |
| ~~ECC P-256~~ | — | **Dropped** | Library deps fight gem5 SE mode. Not worth the time. |

**Scope discipline:** three workloads is the ceiling, not the floor. Do not add more.

### 3.3 Harness requirements

Each workload compiles to a **static binary** (`-static`, required for gem5 SE mode)
with an identical harness:

- Fixed key and nonce, fixed pseudo-random plaintext buffer
- Loop over **N blocks — start N small (~500–2000)**, not the 5,000 payloads of the
  original benchmark. O3 simulation is orders of magnitude slower than
  `TimingSimpleCPU`, and this runs in a VirtualBox VM.
- Identical I/O behaviour (ideally none inside the timed region)
- Use gem5 **`m5_dump_stats()` / ROI markers** so setup and teardown are excluded
  from the measured region. Without this, small workloads are dominated by startup noise.

### 3.4 Experiments

**E1 — Baseline microarchitectural profile (the core result)**
Run all three at a fixed reasonable O3 config. Extract per workload:
- IPC
- Instruction mix (integer ALU / load / store / branch) — from `system.cpu.statIssuedInstType`
- L1D and L1I miss rates
- Branch misprediction rate
- **Stall attribution** — ROB / IQ / LSQ full cycles. This is where the mechanism shows up.

**E2 — Issue-width sweep (tests the ILP hypothesis directly)**
Sweep issue width 1 → 2 → 4 → 8, all else fixed. If the hypothesis holds, ASCON's
IPC climbs and AES's plateaus early.

**E3 — One memory-side sweep (isolates the load-dependency claim)**
Either L1D size (4/16/64 kB) or L1D latency (1–4 cycles). **L1D latency is the more
interesting choice** and less duplicative of the Feb 2026 study: if AES degrades with
load latency while ASCON is unaffected, that is direct evidence for the
load-to-use-serialisation explanation.

Three experiments. That is the whole project.

### 3.5 Optional stretch — only if genuinely ahead of schedule

A Verilog ASCON permutation round to argue the hardware-acceleration case. **Default
to skipping this.** It was cut once already for schedule reasons; do not quietly
reinstate it in October.

---

## 4. Repository layout

```
gem5-crypto-arch/
├── PROJECT-BRIEF.md      # this file
├── README.md             # public-facing; becomes the GitHub landing page
├── reference/            # ascon.c, ascon.h (Kush's own earlier implementation)
├── workloads/            # benchmark sources + Makefile (static, -mno-aes)
├── configs/              # gem5 Python config scripts (O3, parameterised)
├── scripts/              # run sweeps, parse stats.txt → CSV, plot
├── results/              # raw stats.txt, parsed CSVs, figures
└── docs/
    ├── SESSION-CONTEXT.md  # background carried over from planning
    ├── FINDINGS.md         # running log — write as you go, not at the end
    └── REPORT.md           # final writeup
```

---

## 5. Schedule

| Window | Milestone |
|---|---|
| **29 Aug – 5 Sep** | Mid-sems. **No project work.** |
| **8 – 20 Sep** | Setup: source AES/DES, build static binaries, verify all three run under gem5 O3. Overlaps TOEFL prep — keep it light. |
| **21 Sep – 10 Oct** | E1 and E2. Log results in `FINDINGS.md` as they land. |
| **11 – 25 Oct** | E3, plots, `REPORT.md` drafted. |
| **26 Oct – 5 Nov** | Writeup finalised, repo cleaned, README polished, pushed public. |
| **5 Nov** | **Done.** SOP can cite it as completed. |

Applications submitted by **late Nov 2026**. There is no slack after 5 Nov.

### Risks

- **O3 + VM is slow.** Mitigation: small N, ROI markers, run sweeps overnight.
  Measure one config end-to-end early to calibrate before launching a full sweep.
- **Static linking / SE-mode syscall failures.** Mitigation: ASCON is dependency-free
  and will work; if AES or DES fights, use a more compact implementation rather than
  debugging a library for days.
- **Scope creep.** The stretch goal and dropped ECC are both already-made decisions.
  Do not relitigate them in October.

---

## 6. What this becomes

- **GitHub repo** — clean commit history, honest README with the real numbers
- **SOP paragraph** — the spine of the "why computer architecture" narrative,
  connecting embedded security work to microarchitecture
- **Portfolio entry** — `projects-embedded.html` already hosts the Feb 2026 gem5
  study as project 04; this supersedes and deepens it
- **Interview material** — a result he can explain from first principles

---

## 7. Standing instruction for the assisting session

Kush has asked for realism and honest criticism over encouragement, and that applies
here. Specifically:

- If the hypothesis fails, say so and help him report it accurately.
- If the schedule slips, say so early rather than compressing quality at the end.
- Push back on scope additions. The deadline is real and immovable.
- Numbers reported anywhere — README, report, SOP — must come from actual runs in
  `results/`. Nothing estimated, remembered, or extrapolated.
