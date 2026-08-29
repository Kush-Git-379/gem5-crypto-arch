#!/usr/bin/env bash
# run_sweep.sh — drive the three experiments under gem5 O3.
#
# Run this INSIDE the Ubuntu VM. It expects:
#   - gem5 built at $GEM5_ROOT (build/X86/gem5.opt)
#   - workloads built:  cd workloads && make
#
# Usage:
#   ./scripts/run_sweep.sh calibrate   # ONE run, times it — do this first
#   ./scripts/run_sweep.sh e1          # baseline profile, 3 runs
#   ./scripts/run_sweep.sh e2          # issue width sweep, 12 runs
#   ./scripts/run_sweep.sh e3          # L1D latency sweep, 12 runs
#   ./scripts/run_sweep.sh all         # everything
#
# Read the calibration number before launching e2 or e3. If one run takes
# 10 minutes, e2 is two hours and should go overnight.

set -euo pipefail

GEM5_ROOT="${GEM5_ROOT:-$HOME/gem5}"
GEM5="${GEM5_ROOT}/build/X86/gem5.opt"

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${PROJ}/configs/o3_crypto.py"
BINDIR="${PROJ}/workloads/bin"
RAW="${PROJ}/results/raw"

WORKLOADS=(ascon aes des)

# ---------------------------------------------------------------- checks --
preflight() {
    local ok=1
    if [[ ! -x "$GEM5" ]]; then
        echo "ERROR: gem5 binary not found at $GEM5"
        echo "       Set GEM5_ROOT, e.g.  GEM5_ROOT=/opt/gem5 $0 $*"
        ok=0
    fi
    for w in "${WORKLOADS[@]}"; do
        if [[ ! -f "${BINDIR}/${w}.gem5" ]]; then
            echo "ERROR: missing ${BINDIR}/${w}.gem5 — run 'make' in workloads/"
            ok=0
        fi
    done
    [[ $ok -eq 1 ]] || exit 1
    mkdir -p "$RAW"
}

# One gem5 invocation. Args: outdir-name, workload, extra gem5-config args...
run_one() {
    local name="$1"; shift
    local workload="$1"; shift
    local outdir="${RAW}/${name}"

    if [[ -f "${outdir}/stats.txt" ]]; then
        echo "  SKIP ${name} (already has stats.txt)"
        return 0
    fi

    mkdir -p "$outdir"
    echo "  RUN  ${name}"
    local start=$SECONDS

    "$GEM5" --outdir="$outdir" "$CONFIG" \
        --binary "${BINDIR}/${workload}.gem5" \
        "$@" > "${outdir}/run.log" 2>&1 || {
            echo "  FAILED ${name} — see ${outdir}/run.log"
            tail -20 "${outdir}/run.log"
            return 1
        }

    echo "       done in $((SECONDS - start))s"
}

# ------------------------------------------------------------ experiments --

calibrate() {
    echo "Calibration: one ASCON run at default config."
    echo "Use the wall time below to decide whether e2/e3 need to go overnight."
    rm -rf "${RAW}/calib_ascon"
    run_one calib_ascon ascon
    echo
    echo "Multiply that by 3 for E1, 12 for E2, 12 for E3."
}

e1() {
    echo "E1 — baseline microarchitectural profile (default O3 config)"
    for w in "${WORKLOADS[@]}"; do
        run_one "e1_${w}" "$w"
    done
}

e2() {
    echo "E2 — issue width sweep (tests the ILP hypothesis directly)"
    for w in "${WORKLOADS[@]}"; do
        for iw in 1 2 4 8; do
            run_one "e2_${w}_iw${iw}" "$w" --issue-width "$iw"
        done
    done
}

e3() {
    echo "E3 — L1D latency sweep (isolates the load-dependency claim)"
    for w in "${WORKLOADS[@]}"; do
        for lat in 1 2 3 4; do
            run_one "e3_${w}_lat${lat}" "$w" --l1d-latency "$lat"
        done
    done
}

# ------------------------------------------------------------------ main --
preflight "$@"

case "${1:-}" in
    calibrate) calibrate ;;
    e1)        e1 ;;
    e2)        e2 ;;
    e3)        e3 ;;
    all)       e1; e2; e3 ;;
    *)
        echo "Usage: $0 {calibrate|e1|e2|e3|all}"
        exit 1
        ;;
esac

echo
echo "Parse results with:"
echo "  python3 ${PROJ}/scripts/parse_stats.py ${RAW} -o ${PROJ}/results/parsed.csv"
