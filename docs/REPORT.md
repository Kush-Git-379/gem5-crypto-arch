# Where ASCON-128's Advantage Comes From: A Microarchitectural Study

**Kush Mehta** — Institute of Technology, Nirma University
_Draft. No results yet — see [FINDINGS.md](FINDINGS.md) for the running log._

> **Drafting rule:** every number in this document must trace to a run in
> `results/`, cited by path. Nothing estimated, remembered, or carried over from
> an earlier project. If a prediction failed, the section says so plainly and
> the abstract reflects it.

---

## Abstract

_Write last._ One paragraph: the question, the method in one clause, the
headline finding, and whether the hypothesis held. If it was falsified, say so
here — do not bury it.

## 1. Introduction

- The two prior projects and the tension between them.
- What each established and what neither did.
- Scope statement: this explains the gap between *these* implementations under
  *this* core model. It does not decompose the earlier 78× AES-GCM figure, and
  the reasons why (GHASH, ctypes overhead) belong here explicitly.

## 2. Background

- ASCON-128: sponge construction, the 5×64-bit state, the ARX permutation, why
  the S-box lines are mutually independent.
- Software AES-128: S-box substitution as memory access; why the 256-byte table
  makes it cache-insensitive but not memory-independent.
- DES: bit permutations as serial shift/mask/OR chains — a third profile that is
  neither of the above.
- Why `DerivO3CPU` and not `TimingSimpleCPU`. This is the methodological spine
  of the project and deserves its own subsection.

## 3. Method

- The shared harness: identical buffers, PRNG, key/nonce, checksum; ROI markers.
- Mode choice (CTR for AES/DES, native AEAD for ASCON) and why.
- Correctness: the test vectors, cited. Note the round-constant bug found in the
  reference implementation and that it is microarchitecturally neutral.
- Build flags, especially `-mno-aes` and `-fno-tree-vectorize`, and the
  `objdump` check that verifies the former took effect.
- Baseline O3 configuration table.
- Simulator version, host, and the fact that runs are deterministic.

## 4. E1 — Baseline microarchitectural profile

_Fill from `results/parsed.csv` (experiment = e1)._

| Workload | IPC | L1D miss | L1I miss | Branch mispred | Load frac | ROB-full | IQ-full | LQ-full |
|---|---|---|---|---|---|---|---|---|
| ASCON-128 | | | | | | | | |
| AES-128 | | | | | | | | |
| DES | | | | | | | | |

Figures: `fig_e1_ipc.png`, `fig_e1_mix.png`, `fig_e1_stalls.png`

**What to argue here:** the instruction mix is the first evidence. If AES shows
a materially higher load fraction and LSQ/IQ pressure than ASCON, that is the
mechanism appearing. If the miss rates are all near zero, that confirms the Feb
2026 finding and sharpens the question rather than answering it.

## 5. E2 — Issue width sweep

_Fill from `results/parsed.csv` (experiment = e2)._ Figure:
`fig_e2_issue_width.png`

**The direct test.** Report IPC at widths 1, 2, 4, 8 per workload, and the
*ratio* IPC(8)/IPC(1) as the scaling figure. The hypothesis predicts ASCON
scales substantially and AES plateaus early.

State the outcome plainly, including if both plateau or both scale — either
would falsify the clean version of the hypothesis.

## 6. E3 — L1D latency sweep

_Fill from `results/parsed.csv` (experiment = e3)._ Figure:
`fig_e3_l1d_latency.png`

**Isolating the load-dependency claim.** If AES's IPC degrades as L1D latency
rises from 1 to 4 cycles while ASCON's is flat, that is direct evidence for
load-to-use serialisation — and it is a stronger result than E2 alone, because
it manipulates the proposed mechanism rather than the resource that would mask
it.

## 7. Discussion

- What the three experiments jointly establish, and what they do not.
- Where DES falls and why that is informative — a middle IPC for a *different*
  reason tests whether "high IPC" and "few loads" are actually the same axis.
- Implications for processor design targeting IoT/edge: if lightweight ciphers
  are ILP-limited rather than memory-limited, issue width and ROB capacity buy
  more than cache capacity for this class of workload.

### Threats to validity

Write this section honestly and do not pad it.

- Single compiler, single optimisation level; the instruction mix is partly a
  compiler artifact.
- Scalar-only (`-fno-tree-vectorize`); a vectorised ASCON would change the
  picture and is a different question.
- Small N with ROI markers — confirm the loop is long enough that steady-state
  behaviour dominates.
- One core model and one cache hierarchy; no claim about other
  microarchitectures.
- Simulation, not silicon.

## 8. Conclusion

The mechanism, in two or three sentences, with the honest verdict on the
hypothesis.

## Reproducing

Point to the README build-and-run block and `docs/VM-SETUP.md`. State the gem5
version and commit hash used for the reported runs.

## References

- Dobraunig, Eichlseder, Mendel, Schläffer — *Ascon v1.2* (NIST LWC winner).
- NIST FIPS-197, *Advanced Encryption Standard*.
- NIST FIPS-46-3, *Data Encryption Standard*.
- gem5 documentation, `DerivO3CPU`.
- Kush Mehta, *Cryptography Benchmark for Resource-Constrained Devices* (Jan 2026).
- Kush Mehta, *gem5 L1 Cache Sensitivity Analysis* (Feb 2026, 3EC503CC24).
