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

---

## 2026-08-29 — Config fix: `numIQEntries` renamed in gem5 25.1

`configs/o3_crypto.py` failed at `cpu.numIQEntries = args.iq_size` with
`AttributeError: Invalid assignment for Class X86O3CPU with parameter
numIQEntries`. In this gem5 version the IQ was restructured from a single
CPU-level entry count into `instQueues`, a `VectorParam(IQUnit)` — each
`IQUnit` SimObject carries its own `numEntries` (`src/cpu/o3/IQUnit.py:51`).
`BaseO3CPU.py` confirms `numIQEntries` no longer exists on the CPU model at
all (`src/cpu/o3/BaseO3CPU.py:187`, `instQueues = VectorParam.IQUnit(...)`).

Fix: import `IQUnit` from `m5.objects` and set
`cpu.instQueues = IQUnit(numEntries=args.iq_size)` — a scalar assigns fine to
a `VectorParam`, confirmed against gem5's own single-IQ example
(`configs/common/cores/arm/O3_ARM_v7a.py:204`).

**Calibration (one ASCON run, default config, N=1000 blocks):** 30 s wall
time. Log: `results/raw/calib_ascon/run.log`.

While parsing E1 (below) it turned out the same version bump also renamed
several stats `scripts/parse_stats.py` depends on
(`statIssuedInstType0::X` → `issuedInstType_0::X`, no
`commitStats0.numBranches` fetch-branch count, `iewIQFullEvents` was never a
real load-to-use stat in any version — a bad guess in the original patterns).
Fixed in the same pass; see the E1 entry for what actually exists in gem5
25.1's `stats.txt`.

---

## 2026-08-29 — Config fix: `DerivO3CPU` crash at issue-width 1 (found during E2 setup)

While setting up E2, `--issue-width 1` crashed every workload:
`gem5.opt: src/cpu/timebuf.hh:54: ... Assertion 'idx >= -past && idx <= future'
failed` in `TimeBuffer<IEWStruct>::valid`, aborting mid-run
(`results/raw/e2_ascon_iw1/run.log` from the first attempt, since overwritten).

First hypothesis was wrong: `o3_crypto.py` was scaling `squashWidth` together
with the other pipeline widths, and throttling squash recovery to 1/cycle
alongside a 192-entry ROB looked like the likely cause. Decoupling
`squashWidth` from the sweep (left at gem5's default — instant squash) did
**not** fix the crash; it recurred identically. That decoupling is still
correct on its own methodological merits — squash width models
misprediction-recovery speed, not steady-state issue throughput, and E2's
brief says "issue width 1→2→4→8, all else fixed" — so the code change is
kept, but it was not the fix.

Actual cause: `backComSize`/`forwardComSize` (both default 5 cycles,
`src/cpu/o3/BaseO3CPU.py:135`) size the IEW↔commit communication time
buffer. At issue width 1, in-flight latency between issue and commit can
exceed that 5-cycle window, and gem5 25.1 asserts rather than growing the
buffer. Setting `cpu.backComSize = cpu.forwardComSize = 20` in
`configs/o3_crypto.py` fixed it — confirmed by a clean run of
`e2_ascon_iw1` (`results/raw/e2_ascon_iw1/stats.txt`, exit 0).

This is a real, if small, change to the CPU model: re-running the E1 ASCON
baseline under the wider buffer moved `numCycles` by 35 out of 2,165,732
(0.0016%) versus the original run — not simulator noise, a genuine effect of
the buffer size, but far too small to affect any conclusion. To keep every
number in this log comparable, **all runs from this point use
`backComSize = forwardComSize = 20`**, and E1 was re-run under it (below,
superseding the first E1 numbers, which are removed rather than kept
alongside to avoid two versions of "the" baseline table circulating).

---

## 2026-08-29 — E1: baseline microarchitectural profile (final config)

**Config:** `DerivO3CPU`, issue width 8 (fetch/decode/rename/dispatch/issue/
wb/commit all = 8; squash width left at gem5 default), ROB 192, IQ 64, LQ 32,
SQ 32, L1D 32 kB / 2-cycle, `backComSize`/`forwardComSize` = 20 (see fix
above), N=1000 blocks × 64 B. Defaults in `configs/o3_crypto.py`.
**Command:** `./scripts/run_sweep.sh e1` (wraps
`build/X86/gem5.opt --outdir=results/raw/e1_<workload> configs/o3_crypto.py
--binary workloads/bin/<workload>.gem5`)
**Raw stats:** `results/raw/e1_ascon/stats.txt`, `results/raw/e1_aes/stats.txt`,
`results/raw/e1_des/stats.txt`. Parsed: `results/parsed.csv` (rows
`e1_ascon`, `e1_aes`, `e1_des`). All three have `roi_markers=yes` (2 dump
sections, ROI-only section used).

| Workload | IPC | Load frac | L1D miss | L1I miss | Branch mispred rate | ROB-full events | IQ-full events | Load-to-use (cyc) |
|---|---|---|---|---|---|---|---|---|
| ASCON-128 | 3.079 | 5.34% | 0.0035% | 0.0062% | 3.26% | 3 | 3 | 3.03 |
| AES-128-CTR | 1.325 | 21.40% | 0.0001% | 0.0012% | 0.40% | 178,876 | 3,076,715 | 3.10 |
| DES-CTR | 2.547 | 6.99% | 0.0003% | 0.0001% | 3.19% | 44,980 | 17,031 | 3.00 |

Wall time: ASCON 27 s, AES 97 s, DES 414 s (`results/raw/e1_*/run.log`). DES's
wall time is not stable run-to-run (754 s in the first E1 attempt under the
same simulated config, minus the buffer change) — VM scheduling noise on the
host, not a simulated-result problem. Don't use a single DES wall time to
plan sweep length; budget generously.

**Observation:**

ASCON's IPC (3.08) is 2.3x AES's (1.32) at the same 8-wide config. That much
was already visible in the calibration run. What E1 adds is *why*, and it
is not what a naive reading of load-to-use latency would suggest: the
per-load latency stat (`lsq0.loadToUse::mean`) is essentially identical
across all three workloads — 3.03 / 3.10 / 3.00 cycles. AES's individual
loads are not slower. There is no L1D miss-rate story either — all three sit
at 0.0001–0.0035%, confirming the Feb 2026 study's finding still holds under
O3: AES's S-box fits L1 regardless of CPU model.

What actually differs is (a) how often AES loads, and (b) what that does to
the pipeline. AES commits a load on 21.4% of instructions, roughly 4x
ASCON's 5.3% and 3x DES's 7.0% — consistent with S-box/T-table lookups being
loads, as the hypothesis predicted. The consequence shows up directly in
stall attribution: AES hit **178,876 ROB-full stalls** and **3,076,715
IQ-full stalls** during the ROI region. ASCON hit 3 of each — background
noise. DES, despite doing far more total work (181M committed instructions
vs AES's 18.5M for the same N=1000, because this DES implementation is
bit-permutation-per-bit rather than table-based), hit 44,980 ROB-full and
17,031 IQ-full events — roughly 4x below AES's ROB-full count and 180x below
its IQ-full count, despite an order of magnitude more instructions. AES's
rename stage was blocked (`rename.status::Blocked`) for ~19% of all cycles;
the equivalent figure for ASCON is under 0.01% — noise.

**Bearing on hypothesis:** Supports it, with a correction to the mechanism
as originally stated. The brief's hypothesis framed the problem as AES loads
"carry load-to-use latency" that serialises the round function — implying
the *latency* of each load is the bottleneck. That's not what E1 shows: load
latency is flat across all three workloads (~3 cycles, i.e. the L1D hit
cost). What's flat is the per-load cost; what's different is the load
*frequency and position in the dependency chain*. AES's S-box lookups sit
back-to-back on the critical path of the round function often enough (21.4%
of all instructions) that even a uniform 3-cycle latency compounds into
massive ROB/IQ backpressure, because there isn't enough independent
ALU work to issue around them. ASCON's ARX round has the opposite structure:
few loads (5.3%), and the five S-box lines are mutually independent, so the
OoO window has plenty to issue while anything is in flight. This is still an
ILP-vs-memory-dependency story, just located in *chain structure and load
density*, not raw latency — which is exactly the kind of thing E2 (issue
width) and E3 (L1D latency) should be able to separate cleanly: if E3
confirms AES is roughly insensitive to L1D latency (since it's not
latency-bound but density/chain-bound), that's the sharper, correctly-stated
version of this result for the writeup.

**Next:** E2 — issue-width sweep (1/2/4/8), all three workloads, 12 runs.
DES's wall time swung 414–754 s for the *same* simulated config across two
runs (VM scheduling noise), so E2 is not free and its total time is hard to
predict from E1 alone — running it now and reporting real timing.

---

## 2026-08-29 — E2: issue-width sweep (1/2/4/8)

**Config:** Same as the final E1 config, varying only `--issue-width`
(fetch/decode/rename/dispatch/issue/wb/commit all scaled together;
`squashWidth` and `backComSize`/`forwardComSize` fixed as established above).
N=1000 blocks × 64 B.
**Command:** `./scripts/run_sweep.sh e2` (`e2_ascon_iw1` was produced earlier
while confirming the crash fix, under the identical final config — reused
rather than re-run).
**Raw stats:** `results/raw/e2_{ascon,aes,des}_iw{1,2,4,8}/stats.txt`.
Parsed: `results/parsed.csv` (12 `e2_*` rows). All `roi_markers=yes`.

| Issue width | ASCON IPC | AES IPC | DES IPC |
|---|---|---|---|
| 1 | 0.690 | 0.520 | 0.625 |
| 2 | 1.263 | 0.908 | 1.106 |
| 4 | 2.108 | 1.281 | 1.813 |
| 8 | 3.079 | 1.325 | 2.547 |

Wall time: ASCON iw2/4/8 = 19/16/16 s (iw1 not separately timed — reused
from the crash-fix debug run, same config, stats valid). AES 86/75/73/71 s.
DES 593/494/653/647 s. E2 total ≈ 46 min.

**Observation:** This is the cleanest result of the project so far, and it
matches the brief's E2 prediction almost exactly. ASCON's IPC climbs at
every step, including the last: +83% (1→2), +67% (2→4), **+46% (4→8)**.
DES is similar: +77%, +64%, **+41%**. AES climbs early — +75% (1→2), +41%
(2→4) — then **flattens: +3.4% (4→8)**. Going from issue width 1 to 8, ASCON
gained 4.46x IPC and DES gained 4.08x; AES gained only 2.55x. AES is not
issue-width-starved past 4 wide; ASCON and DES still are, even at 8.

The stall-attribution numbers explain why the plateau happens where it does,
not just that it happens. AES's ROB-full events are 0 at issue width 1–2,
46,972 at width 4, and 178,876 at width 8 — the ROB only becomes the
binding constraint once the front end is no longer the bottleneck, i.e.
once there's enough issue bandwidth to expose the real limiter. That real
limiter is the dependency chain through the S-box loads (established in
E1): once issue width stops being the constraint, AES hits a resource wall
that ASCON and DES, with far more independent per-round work, don't hit at
these widths.

**Bearing on hypothesis:** Strongly supports it. This is exactly the
"ASCON's IPC climbs and AES's plateaus early" signature the brief predicted
for E2, and it lines up with E1's corrected mechanism (load density and
chain structure, not raw latency): AES plateaus not because it runs out of
issue slots to fill, but because it runs out of *independent* work to fill
them with, and that shows up as a resource wall (ROB) rather than a width
constraint once width stops being the bottleneck.

**Next:** E3 — L1D latency sweep (1/2/3/4 cycles), all three workloads, 12
runs. This is the test that should separate the two candidate mechanisms
cleanly: if AES is roughly flat across L1D latency (since E1 showed its
loads aren't latency-bound, just dense and chained) while remaining
IPC-limited relative to ASCON, that confirms the "density/chain, not
latency" reading of E1 rather than the brief's original "load-to-use
latency serialises AES" framing. If AES instead degrades with L1D latency
the way the brief originally predicted, that would contradict the E1
correction and need to be reconciled honestly, not smoothed over.

---

## 2026-08-29 — E3: L1D latency sweep (1/2/3/4 cycles)

**Config:** Same as the final E1 config (issue width 8), varying only
`--l1d-latency` (tag + data + response latency together). N=1000 blocks ×
64 B.
**Command:** `./scripts/run_sweep.sh e3`
**Raw stats:** `results/raw/e3_{ascon,aes,des}_lat{1,2,3,4}/stats.txt`.
Parsed: `results/parsed.csv` (12 `e3_*` rows). All `roi_markers=yes`.

| L1D latency (cyc) | ASCON IPC | AES IPC | DES IPC |
|---|---|---|---|
| 1 | 3.078 | 1.645 | 2.547 |
| 2 | 3.079 | 1.325 | 2.547 |
| 3 | 3.041 | 1.103 | 2.547 |
| 4 | 3.029 | 0.945 | 2.547 |

Wall time: ASCON 27/22/25/23 s. AES 112/103/99/115 s. DES 578/582/618/601 s.
E3 total ≈ 48 min.

**Observation:** This is not the result E1's speculation predicted, and it
needs to be said plainly: **E1's forward-looking guess was wrong.** Going
from lat1 to lat4, AES's IPC drops **42.5%** (1.645 → 0.945). ASCON drops
1.6% (3.078 → 3.029) — noise-level. DES doesn't move at all (2.5473 at
every point, to four significant figures) — essentially perfectly flat.
AES is not latency-insensitive; it is by far the most latency-sensitive of
the three, losing roughly 15–20% IPC for every added cycle of L1D latency
(-19.5% lat1→2, -16.7% lat2→3, -14.3% lat3→4). Stall counts move with it:
AES's ROB-full events climb monotonically with latency (115,918 → 178,876 →
253,879 → 354,853); ASCON's stay at 3 throughout; DES's actually *decrease*
slightly (54,978 → 42,986) and are noise relative to its unchanged IPC.
`loadToUse::mean` scales 1:1 with the latency knob for all three workloads
alike (e.g. AES: 2.11 → 3.10 → 4.13 → 5.12 cycles, essentially the same
slope as ASCON's and DES's) — so the *per-load* latency cost is not workload
-specific, confirming that part of the E1 read. What E1 got wrong was the
inference drawn from it: identical per-load latency does not imply
insensitivity to that latency at the throughput level, because AES's loads
are dense and sit on the round function's critical path (established in
E1) — which is exactly the condition under which a shared per-load latency
increase compounds into a large aggregate IPC loss. ASCON and DES don't
have that structure, so the same latency increase is nearly invisible to
their throughput.

**Bearing on hypothesis:** Confirms the brief's original E3 prediction
cleanly and directly — "AES degrades as L1D latency rises while ASCON is
largely unaffected" is exactly what happened, with a clean 42.5%-vs-1.6%
split and DES pinned flat as a second confirming case. This also corrects
the speculative claim added to the E1 entry ("if E3 confirms AES is roughly
insensitive to L1D latency... that's the sharper, correctly-stated version")
— that guess does not hold up; it is left in place above, uncorrected in
its own text, so the log shows the reasoning as it actually happened rather
than editing history. The combined, accurate mechanism across all three
experiments: AES's throughput disadvantage comes from load-to-use
dependency chains — its S-box loads are frequent (E1), sit back-to-back on
the round function's critical path with too little independent work to
hide them (E1, E2's plateau), and because of that structure its throughput
is directly exposed to L1D latency in a way ASCON's ARX permutation and
this DES implementation are not (E3). Latency and density/chain-structure
are not competing explanations — density and chain position are *why* AES
is latency-sensitive and ASCON isn't. The hypothesis holds, end to end,
across all three experiments.

**Next:** E1/E2/E3 are the whole project per the brief (§3.4) — the
experiment phase is done. Remaining work: `scripts/plot_results.py` figures,
`docs/REPORT.md` draft, then repo cleanup and README per the 26 Oct–5 Nov
schedule window. No further sweeps are planned; do not add a fourth
experiment without discussing it first — scope is closed.

---

## 2026-09-14 — AES-NI check actually run (previously asserted, never executed)

REPORT.md §3 claimed `-mno-aes` "is verified directly, not assumed," citing
`objdump -d bin/aes.gem5 | grep -ci aesenc`. That command had never been run
against a committed artifact — `run.log` is gitignored
(`.gitignore`: `results/raw/*/run.log`), so no record of it existed anywhere
in the repo. Ran it directly against the binary checked into
`workloads/bin/aes.gem5`:

```
$ objdump -d workloads/bin/aes.gem5 | grep -ci aesenc
0
```

Result: **0**, as the S-box-table implementation predicts. This confirms
the `-mno-aes` build flag did what it was supposed to for the actual binary
E1/E2/E3 ran against, closing the gap between the report's claim and the
evidence for it.

**Bearing on hypothesis:** none directly (this is a build-correctness check,
not a performance result) — but it was load-bearing for every AES number in
the report, since a compiler-emitted `AESENC` would have collapsed the whole
S-box-load hypothesis into a no-op. REPORT.md §3 now cites this entry instead
of asserting the check as if already done.

**Next:** none — this closes out the one outstanding unverified claim in the
report.

---

## 2026-09-14 — Parser fix: `iq_full_events`/`sq_full_events` blank instead of 0 at narrow issue widths

`results/parsed.csv` had 12 blank cells: `iq_full_events` at `iw1` for all
three workloads, and `sq_full_events` at `iw1`/`iw2`/`iw4` for all three.
Cause: `lookup()`'s zero-backfill only fires when the stat key is present
*somewhere else in the same stats.txt* (`known_keys` was scoped per file).
gem5 25.1 omits a zero-valued scalar from a dump section entirely rather
than printing `0` — and at narrow issue widths, `rename.IQFullEvents` /
`rename.SQFullEvents` are genuinely 0 in *both* dump sections of that run's
file, so the key never appears anywhere in it, and the per-file backfill
never triggers. Confirmed by grepping the raw files directly, e.g.
`results/raw/e2_aes_iw1/stats.txt` never contains `rename.IQFullEvents` in
either section, while `results/raw/e2_aes_iw2/stats.txt` does (value 7998).

Fix (`scripts/parse_stats.py`): read every run's stats.txt in a first pass
and pool `known_keys` across the *entire batch* being parsed, not just the
current file, before doing the per-row lookup. A stat that's a real,
registered gem5 stat for this build appears in at least one run somewhere
in the batch (e.g. `IQFullEvents` at iw2/4/8); a stat that plain doesn't
exist in this gem5 version never appears in any run, so it correctly stays
blank rather than being spuriously zeroed.

Re-ran `python3 scripts/parse_stats.py results/raw/ -o results/parsed.csv`:
all 12 cells now read `0`; diffed against the prior CSV to confirm no other
cell changed.

**Bearing on hypothesis:** none — cosmetic data-quality fix, not a new
result. Flagged because REPORT.md §5 states "AES's ROB-full events are 0 at
issue width 1–2" — `rob_full_events` itself was never blank (its own stat
backfilled correctly within-file), so that specific claim was already
correct, but the adjacent `iq_full_events`/`sq_full_events` blanks were a
latent correctness risk for any argument that later leans on them.

**Next:** none.
