#!/usr/bin/env python3
"""
parse_stats.py — turn gem5 stats.txt files into a tidy CSV.

Handles the thing that quietly ruins ROI-marked runs: a stats.txt produced with
m5_reset_stats/m5_dump_stats contains MORE THAN ONE dump section. Taking the
last one, or naively grepping the whole file, mixes the measured region with
teardown. This parser splits on the section delimiters and selects the ROI dump
explicitly.

Usage:
    python3 parse_stats.py results/raw/ -o results/parsed.csv

Each input directory is expected to look like:
    results/raw/<experiment>_<workload>[_<param>]/stats.txt
and the directory name is used to label the row.
"""

import argparse
import csv
import os
import re
import sys

BEGIN = "---------- Begin Simulation Statistics ----------"
END = "---------- End Simulation Statistics   ----------"

# Stats we care about, mapped to output column names.
# gem5 renames things between versions; each entry lists candidate keys in
# priority order so the parser survives a version bump instead of silently
# emitting blanks.
WANTED = {
    "sim_seconds":     ["simSeconds"],
    "sim_ticks":       ["simTicks"],
    "cpu_cycles":      ["system.cpu.numCycles"],
    "committed_insts": ["system.cpu.commitStats0.numInsts",
                        "system.cpu.commit.committedInsts",
                        "system.cpu.committedInsts"],
    "committed_ops":   ["system.cpu.commitStats0.numOps",
                        "system.cpu.commit.committedOps"],
    "ipc":             ["system.cpu.ipc"],
    "cpi":             ["system.cpu.cpi"],

    # Memory behaviour
    "l1d_accesses":    ["system.cpu.dcache.overallAccesses::total",
                        "system.cpu.dcache.overall_accesses::total"],
    "l1d_misses":      ["system.cpu.dcache.overallMisses::total",
                        "system.cpu.dcache.overall_misses::total"],
    "l1d_miss_rate":   ["system.cpu.dcache.overallMissRate::total",
                        "system.cpu.dcache.overall_miss_rate::total"],
    "l1i_accesses":    ["system.cpu.icache.overallAccesses::total",
                        "system.cpu.icache.overall_accesses::total"],
    "l1i_misses":      ["system.cpu.icache.overallMisses::total",
                        "system.cpu.icache.overall_misses::total"],
    "l1i_miss_rate":   ["system.cpu.icache.overallMissRate::total",
                        "system.cpu.icache.overall_miss_rate::total"],
    "l2_miss_rate":    ["system.l2cache.overallMissRate::total",
                        "system.l2cache.overall_miss_rate::total"],

    # Branches
    "branches":        ["system.cpu.commitStats0.numBranches",
                        "system.cpu.branchPred.lookups_0::total",
                        "system.cpu.branchPred.lookups"],
    "branch_mispred":  ["system.cpu.commit.branchMispredicts",
                        "system.cpu.branchPred.condIncorrect"],

    # Instruction mix — the E1 evidence for "AES does more loads"
    # gem5 25.1 renamed statIssuedInstType0::X -> issuedInstType_0::X.
    "num_load_insts":  ["system.cpu.commitStats0.numLoadInsts",
                        "system.cpu.commit.loads"],
    "num_store_insts": ["system.cpu.commitStats0.numStoreInsts",
                        "system.cpu.commit.stores"],
    "int_alu_ops":     ["system.cpu.issuedInstType_0::IntAlu",
                        "system.cpu.statIssuedInstType0::IntAlu"],
    "int_mult_ops":    ["system.cpu.issuedInstType_0::IntMult",
                        "system.cpu.statIssuedInstType0::IntMult"],
    "mem_read_ops":    ["system.cpu.issuedInstType_0::MemRead",
                        "system.cpu.statIssuedInstType0::MemRead"],
    "mem_write_ops":   ["system.cpu.issuedInstType_0::MemWrite",
                        "system.cpu.statIssuedInstType0::MemWrite"],

    # Stall attribution — where the mechanism actually shows up.
    # gem5 25.1 has no per-LQ-full counter (only a combined LSQ-full count
    # under iew, plus a separate SQFullEvents under rename); lq_full_events
    # is left as the closest available proxy, not a load-only count.
    "rob_full_events":  ["system.cpu.rename.ROBFullEvents",
                         "system.cpu.rename.ROBFullEvents::total"],
    "iq_full_events":   ["system.cpu.rename.IQFullEvents",
                         "system.cpu.rename.IQFullEvents::total"],
    "lq_full_events":   ["system.cpu.iew.lsqFullEvents",
                         "system.cpu.rename.LQFullEvents"],
    "sq_full_events":   ["system.cpu.rename.SQFullEvents",
                         "system.cpu.rename.SQFullEvents::total"],
    "rename_blocked":   ["system.cpu.rename.status::Blocked",
                         "system.cpu.rename.BlockCycles"],
    # Direct load-to-use latency distribution (mean cycles) — the load/AES
    # dependency-chain metric the hypothesis is actually about.
    "iew_load_to_use":  ["system.cpu.lsq0.loadToUse::mean"],
    "dcache_avg_miss_latency": [
        "system.cpu.dcache.overallAvgMissLatency::total",
        "system.cpu.dcache.overall_avg_miss_latency::total"],
}


def split_sections(text):
    """Return a list of dump sections, each a dict of stat -> raw string."""
    sections = []
    current = None

    for line in text.splitlines():
        if line.startswith(BEGIN):
            current = {}
            continue
        if line.startswith(END):
            if current is not None:
                sections.append(current)
            current = None
            continue
        if current is None:
            continue

        line = line.strip()
        if not line or line.startswith("#"):
            continue

        parts = line.split()
        if len(parts) >= 2:
            current[parts[0]] = parts[1]

    return sections


def pick_roi_section(sections, path):
    """Choose the dump section corresponding to the measured region.

    With ROI markers the workload does reset_stats then dump_stats, so the
    FIRST dump section is the ROI. gem5 then emits a final section at exit
    covering teardown. If there is only one section the binary was built
    without -DGEM5_ROI and the whole program was measured — that is a
    different measurement, so we flag it loudly rather than reporting it as
    if it were an ROI number.
    """
    if not sections:
        raise ValueError(f"{path}: no statistics sections found")

    if len(sections) == 1:
        print(f"  WARNING: {path} has a single dump section — this binary was "
              f"probably built without -DGEM5_ROI, so setup/teardown are "
              f"INCLUDED in these numbers.", file=sys.stderr)
        return sections[0], False

    return sections[0], True


def lookup(section, candidates, known_keys=None):
    """Look up the first matching candidate in this dump section.

    gem5 25.1 omits a scalar stat from a dump section entirely when its
    value is exactly 0 for that interval (SQFullEvents in an ROI section
    with no SQ stalls, for example), rather than printing "0". If a
    candidate key is absent here but is a real, registered stat elsewhere
    in the same stats.txt (known_keys), that means genuinely zero for this
    section, not "stat doesn't exist in this gem5 build" — return "0"
    instead of leaving it blank so a real zero isn't mistaken for missing
    data.
    """
    for key in candidates:
        if key in section:
            return section[key]
    if known_keys:
        for key in candidates:
            if key in known_keys:
                return "0"
    return ""


def derive(row):
    """Fill in metrics gem5 may not print directly."""
    def num(key):
        try:
            return float(row[key])
        except (KeyError, ValueError):
            return None

    insts = num("committed_insts")
    cycles = num("cpu_cycles")
    if not row.get("ipc") and insts and cycles:
        row["ipc"] = f"{insts / cycles:.6f}"

    # Load fraction of committed instructions — the headline instruction-mix
    # number for the hypothesis.
    loads = num("num_load_insts")
    if loads is not None and insts:
        row["load_frac"] = f"{loads / insts:.6f}"

    stores = num("num_store_insts")
    if stores is not None and insts:
        row["store_frac"] = f"{stores / insts:.6f}"

    br = num("branches")
    mis = num("branch_mispred")
    if br and mis is not None:
        row["branch_mispred_rate"] = f"{mis / br:.6f}"

    return row


def parse_dirname(name):
    """results/raw/e2_ascon_iw4 -> (e2, ascon, iw4)"""
    parts = name.split("_")
    exp = parts[0] if parts else ""
    workload = parts[1] if len(parts) > 1 else ""
    param = "_".join(parts[2:]) if len(parts) > 2 else ""
    return exp, workload, param


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", help="directory containing per-run subdirectories")
    ap.add_argument("-o", "--output", default="results/parsed.csv")
    args = ap.parse_args()

    rows = []

    for entry in sorted(os.listdir(args.root)):
        rundir = os.path.join(args.root, entry)
        stats = os.path.join(rundir, "stats.txt")
        if not os.path.isfile(stats):
            continue

        with open(stats) as fh:
            sections = split_sections(fh.read())

        try:
            section, roi_ok = pick_roi_section(sections, stats)
        except ValueError as e:
            print(f"  SKIP: {e}", file=sys.stderr)
            continue

        exp, workload, param = parse_dirname(entry)
        known_keys = set()
        for s in sections:
            known_keys.update(s.keys())

        row = {
            "run": entry,
            "experiment": exp,
            "workload": workload,
            "param": param,
            "roi_markers": "yes" if roi_ok else "NO",
            "n_sections": len(sections),
        }
        for col, candidates in WANTED.items():
            row[col] = lookup(section, candidates, known_keys)

        rows.append(derive(row))
        print(f"  parsed {entry}  (sections={len(sections)}, roi={row['roi_markers']})")

    if not rows:
        print("No stats.txt files found.", file=sys.stderr)
        return 1

    fieldnames = list(rows[0].keys())
    for r in rows:
        for k in r:
            if k not in fieldnames:
                fieldnames.append(k)

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        for r in rows:
            writer.writerow(r)

    print(f"\nWrote {len(rows)} rows to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
