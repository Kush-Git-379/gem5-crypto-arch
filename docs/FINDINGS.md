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
