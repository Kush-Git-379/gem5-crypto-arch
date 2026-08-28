#!/usr/bin/env python3
"""
plot_results.py — figures for the three experiments, from results/parsed.csv.

    python3 scripts/plot_results.py results/parsed.csv -o results/figures

Produces only what the report actually argues from:
    fig_e1_ipc.png          IPC by workload
    fig_e1_mix.png          instruction mix (the load-fraction evidence)
    fig_e1_stalls.png       stall attribution
    fig_e2_issue_width.png  IPC vs issue width  <- the ILP test
    fig_e3_l1d_latency.png  IPC vs L1D latency  <- the load-dependency test

Deliberately plain: no styling that implies precision the data does not have.
"""

import argparse
import csv
import os
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib required:  pip3 install matplotlib", file=sys.stderr)
    sys.exit(1)

# Consistent identity per workload across every figure.
STYLE = {
    "ascon": ("ASCON-128", "#2166ac", "o"),
    "aes":   ("AES-128",   "#b2182b", "s"),
    "des":   ("DES",       "#4d4d4d", "^"),
}
ORDER = ["ascon", "aes", "des"]


def load(path):
    with open(path) as fh:
        return list(csv.DictReader(fh))


def fnum(row, key):
    try:
        return float(row[key])
    except (KeyError, ValueError, TypeError):
        return None


def by_experiment(rows, exp):
    return [r for r in rows if r.get("experiment") == exp]


def save(fig, outdir, name):
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, name)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {path}")


# ------------------------------------------------------------------- E1 --

def plot_e1_ipc(rows, outdir):
    data = by_experiment(rows, "e1")
    if not data:
        return
    labels, values, colors = [], [], []
    for w in ORDER:
        r = next((x for x in data if x["workload"] == w), None)
        v = fnum(r, "ipc") if r else None
        if v is None:
            continue
        labels.append(STYLE[w][0])
        values.append(v)
        colors.append(STYLE[w][1])
    if not values:
        return

    fig, ax = plt.subplots(figsize=(6, 4))
    bars = ax.bar(labels, values, color=colors, width=0.55)
    for b, v in zip(bars, values):
        ax.text(b.get_x() + b.get_width() / 2, v, f"{v:.3f}",
                ha="center", va="bottom", fontsize=10)
    ax.set_ylabel("IPC (instructions per cycle)")
    ax.set_title("E1 — Baseline IPC under DerivO3CPU")
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    save(fig, outdir, "fig_e1_ipc.png")


def plot_e1_mix(rows, outdir):
    data = by_experiment(rows, "e1")
    if not data:
        return

    labels, loads, stores, others = [], [], [], []
    for w in ORDER:
        r = next((x for x in data if x["workload"] == w), None)
        if not r:
            continue
        lf = fnum(r, "load_frac")
        sf = fnum(r, "store_frac")
        if lf is None:
            continue
        sf = sf or 0.0
        labels.append(STYLE[w][0])
        loads.append(lf * 100)
        stores.append(sf * 100)
        others.append((1 - lf - sf) * 100)
    if not labels:
        return

    fig, ax = plt.subplots(figsize=(6.5, 4))
    ax.bar(labels, others, label="other (ALU, branch, …)",
           color="#cccccc", width=0.55)
    ax.bar(labels, loads, bottom=others, label="loads",
           color="#b2182b", width=0.55)
    ax.bar(labels, stores, bottom=[o + l for o, l in zip(others, loads)],
           label="stores", color="#ef8a62", width=0.55)
    ax.set_ylabel("% of committed instructions")
    ax.set_title("E1 — Instruction mix")
    ax.legend(frameon=False, fontsize=9)
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    save(fig, outdir, "fig_e1_mix.png")


def plot_e1_stalls(rows, outdir):
    data = by_experiment(rows, "e1")
    if not data:
        return

    cols = [("rob_full_events", "ROB full"),
            ("iq_full_events", "IQ full"),
            ("lq_full_events", "LQ full"),
            ("sq_full_events", "SQ full")]

    present = []
    for key, label in cols:
        if any(fnum(r, key) is not None for r in data):
            present.append((key, label))
    if not present:
        return

    fig, ax = plt.subplots(figsize=(7, 4))
    n = len(present)
    width = 0.8 / max(len(ORDER), 1)

    for i, w in enumerate(ORDER):
        r = next((x for x in data if x["workload"] == w), None)
        if not r:
            continue
        vals = [fnum(r, k) or 0 for k, _ in present]
        xs = [j + i * width for j in range(n)]
        ax.bar(xs, vals, width=width, label=STYLE[w][0], color=STYLE[w][1])

    ax.set_xticks([j + width for j in range(n)])
    ax.set_xticklabels([lbl for _, lbl in present])
    ax.set_ylabel("events")
    ax.set_title("E1 — Structure-full stall attribution")
    ax.legend(frameon=False, fontsize=9)
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    save(fig, outdir, "fig_e1_stalls.png")


# ---------------------------------------------------------------- E2/E3 --

def plot_sweep(rows, exp, prefix, xlabel, title, fname, outdir):
    """Shared line-plot for the two sweeps."""
    data = by_experiment(rows, exp)
    if not data:
        return

    fig, ax = plt.subplots(figsize=(6.5, 4.5))
    plotted = False

    for w in ORDER:
        pts = []
        for r in data:
            if r["workload"] != w:
                continue
            param = r.get("param", "")
            if not param.startswith(prefix):
                continue
            try:
                x = int(param[len(prefix):])
            except ValueError:
                continue
            y = fnum(r, "ipc")
            if y is not None:
                pts.append((x, y))
        if not pts:
            continue
        pts.sort()
        label, color, marker = STYLE[w]
        ax.plot([p[0] for p in pts], [p[1] for p in pts],
                marker=marker, color=color, label=label, linewidth=1.8)
        plotted = True

    if not plotted:
        plt.close(fig)
        return

    ax.set_xlabel(xlabel)
    ax.set_ylabel("IPC")
    ax.set_title(title)
    ax.legend(frameon=False)
    ax.grid(alpha=0.3)
    ax.set_axisbelow(True)
    save(fig, outdir, fname)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="?", default="results/parsed.csv")
    ap.add_argument("-o", "--outdir", default="results/figures")
    args = ap.parse_args()

    if not os.path.isfile(args.csv):
        print(f"No such file: {args.csv}", file=sys.stderr)
        print("Run parse_stats.py first.", file=sys.stderr)
        return 1

    rows = load(args.csv)
    print(f"Loaded {len(rows)} rows from {args.csv}")

    plot_e1_ipc(rows, args.outdir)
    plot_e1_mix(rows, args.outdir)
    plot_e1_stalls(rows, args.outdir)

    plot_sweep(rows, "e2", "iw",
               "Issue width",
               "E2 — IPC vs issue width (the ILP test)",
               "fig_e2_issue_width.png", args.outdir)

    plot_sweep(rows, "e3", "lat",
               "L1D access latency (cycles)",
               "E3 — IPC vs L1D latency (the load-dependency test)",
               "fig_e3_l1d_latency.png", args.outdir)

    return 0


if __name__ == "__main__":
    sys.exit(main())
