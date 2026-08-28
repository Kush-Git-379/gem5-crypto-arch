# Session Context — background carried over from planning

Read `../PROJECT-BRIEF.md` first. This file holds the surrounding context that
shaped the spec: who Kush is, what he already built, and how he works.

---

## Who

**Kush Mehta** — B.Tech Electronics & Communication Engineering (Minor: Data Science),
Institute of Technology, Nirma University, Ahmedabad. Aug 2023 – **May 2027**.
CGPA **8.59 / 10**. GRE **325** (170 Quant / 155 Verbal). TOEFL not yet taken
(planned late Sep 2026).

Applying to **US MS programs, Fall 2027 intake**, submitting by **late Nov 2026**.
Stated goal: build a career and settle in the US. Not research-track, not
prestige-driven — so this project exists to make him a credible *computer
engineering* applicant, not to start a research career.

---

## Environment

- **gem5 v25.1** running in **Ubuntu inside VirtualBox** on a Windows 11 host.
- Host paths are Windows; the project folder is
  `C:\Users\HP\Documents\gem5-crypto-arch`. Work out how files move between host
  and VM early (shared folder or git) — do not leave it until results need collecting.
- The VM matters: O3 simulation there is slow. Calibrate with one run before
  committing to a sweep.

---

## Prior work this project builds on

### Cryptography Benchmark for Resource-Constrained Devices (Jan 2026)
Benchmarked AES-GCM, DES, ECC P-256 and ASCON-128 across 5,000 IoT payloads.
Loaded the ASCON C reference as a compiled DLL through Python `ctypes` for
native-speed timing; profiled peak memory with `tracemalloc`.
**Result: ASCON ran 78× faster than AES-GCM and 12× faster than DES.**
Mentored by Prof. Manisha (also taught his Research Methodology seminar).

Wall-clock only — it establishes *that* ASCON wins, never *why*.

### gem5 L1 Cache Sensitivity Analysis (Feb 2026, course 3EC503CC24)
gem5 v25.1, **X86 TimingSimpleCPU**. Three experiments varying cache capacity,
workload type, and set associativity.

| Measurement | Value |
|---|---|
| Matrix-multiply miss rate, 4 kB → 64 kB | 5.06% → 0.04% (IPC 0.277 → 0.310, +11.9%) |
| AES miss rate, 4 kB → 64 kB | 0.14% → 0.14% — **flat** |
| Associativity 1-way → 8-way (16 kB) | 4.74% → 0.34% (IPC +10.4%) |

Concluded AES is cache-insensitive because the 256-byte S-box fits even a 4 kB
cache, so its bottleneck "lies in instruction execution, not memory capacity" —
but never identified what in execution. **That unanswered question is this
project.**

Note the limitation being corrected: `TimingSimpleCPU` has no pipeline model, so
its IPC figures cannot speak to instruction-level parallelism.

### Other relevant background
- **ISRO SAC research internship** (May–Jul 2026, Navigation Receiver Division) —
  real-time C++, Reed–Solomon (255,32) over GF(256), RTCM/Galileo HAS protocol
  decoding, pre-C++11 toolchain. His strongest technical credential.
- **Secure GPS Telemetry Pipeline** (Mar 2026) — ESP32, AES-128 via mbedTLS, MQTT.
  The applied edge-crypto work that motivates caring about lightweight ciphers.
- **FPGA digital design** — Wallace Tree multiplier, Verilog, Quartus, ModelSim.
  Relevant only if the optional hardware stretch goal is ever revived (default: skip).
- **Team Arrow UAV Club**, Electronics Lead, Sep 2023 – present. Where the
  "IoT and edge nodes" framing comes from honestly.

---

## Assets already in the repo

- `reference/ascon.c`, `reference/ascon.h` — Kush's own ASCON-128 implementation,
  copied from `C:\Users\HP\Documents\Arduino\`. Self-contained: `stdint.h` and
  `string.h` only, five 64-bit words, `P()` permutation with ROR-based linear
  diffusion. Dependency-free, so it will run under gem5 SE mode without trouble.
  This is why ASCON is the anchor workload.

Still to source: a software AES-128 (**built with `-mno-aes`** — hardware AES
instructions would make the comparison meaningless) and a compact DES.

---

## How Kush works

- He asked explicitly for **realism, honesty and criticism** over encouragement.
  Direct pushback is wanted, not tolerated.
- He is prone to **scope enthusiasm** — ECC and a Verilog stretch goal were both
  deliberately cut. Hold those lines.
- He has **misstated his own CGPA** before (said 8.59, corrected to 8.52, then it
  genuinely rose back to 8.59). Verify facts with him rather than trusting recall;
  the same care applies to quoting benchmark numbers.
- Parallel workstream: a **separate chat handles SOP, LORs and university
  shortlisting.** Keep this session on the project. If application strategy comes
  up, note it and point him back there.

---

## Immediate next steps when work starts (after 5 Sep)

1. Sort host ↔ VM file movement.
2. Source AES-128 and DES implementations; write the shared harness.
3. Build all three static (`-static -mno-aes`), confirm they run under
   `DerivO3CPU` in SE mode.
4. Add `m5_dump_stats()` ROI markers.
5. Time one full E1 run to calibrate how long sweeps will take before launching them.
