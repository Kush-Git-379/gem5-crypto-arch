#!/usr/bin/env python3
"""
o3_crypto.py — parameterised gem5 SE-mode config for the lightweight-crypto study.

One config script drives all three experiments; the sweeps differ only in the
command-line arguments they pass.

    E1  baseline    : defaults
    E2  issue width : --issue-width {1,2,4,8}
    E3  L1D latency : --l1d-latency {1,2,3,4}

Usage (from the gem5 root, inside the Ubuntu VM):

    build/X86/gem5.opt \
        --outdir=/path/to/results/raw/e1_ascon \
        /path/to/configs/o3_crypto.py \
        --binary /path/to/workloads/bin/ascon.gem5

Why DerivO3CPU: the Feb 2026 study used TimingSimpleCPU, which is in-order,
single-issue and has no pipeline model. Its IPC cannot speak to
instruction-level parallelism. The whole point of this project is that the
out-of-order model can.
"""

import argparse
import sys

import m5
from m5.objects import (
    AddrRange,
    BadAddr,
    Cache,
    DerivO3CPU,
    L2XBar,
    MemCtrl,
    Process,
    Root,
    SEWorkload,
    SrcClockDomain,
    System,
    SystemXBar,
    VoltageDomain,
    DDR3_1600_8x8,
)


# --------------------------------------------------------------------------
# Cache definitions
# --------------------------------------------------------------------------

class L1ICache(Cache):
    """L1 instruction cache. Held fixed across all experiments."""
    size = "32kB"
    assoc = 4
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20


class L1DCache(Cache):
    """L1 data cache.

    Size and latency are the knobs for E3. Latency is the more interesting of
    the two and less duplicative of the Feb 2026 capacity study: if AES
    degrades as load latency rises while ASCON does not, that is direct
    evidence for load-to-use serialisation.
    """
    size = "32kB"
    assoc = 8
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 16
    tgts_per_mshr = 20


class L2Cache(Cache):
    """Unified L2. Fixed; these workloads have tiny footprints and should
    essentially never reach it, which is itself worth reporting."""
    size = "256kB"
    assoc = 8
    tag_latency = 20
    data_latency = 20
    response_latency = 20
    mshrs = 20
    tgts_per_mshr = 12
    write_buffers = 8


# --------------------------------------------------------------------------
# Arguments
# --------------------------------------------------------------------------

def parse_args(argv):
    p = argparse.ArgumentParser(
        description="O3 SE-mode harness for the crypto microarchitecture study"
    )
    p.add_argument("--binary", required=True,
                   help="static workload binary to run")
    p.add_argument("--cpu-clock", default="2GHz")
    p.add_argument("--mem-size", default="512MB")

    # E2 knob
    p.add_argument("--issue-width", type=int, default=8,
                   help="O3 issue width; also scales fetch/decode/rename/commit "
                        "so the pipeline is balanced rather than "
                        "artificially throttled at one stage")

    # E3 knobs
    p.add_argument("--l1d-size", default="32kB")
    p.add_argument("--l1d-latency", type=int, default=2,
                   help="L1D tag+data latency in cycles")

    # Structure sizes — exposed so the ROB/IQ/LSQ story can be probed if the
    # E1 stall attribution points that way.
    p.add_argument("--rob-size", type=int, default=192)
    p.add_argument("--iq-size", type=int, default=64)
    p.add_argument("--lq-size", type=int, default=32)
    p.add_argument("--sq-size", type=int, default=32)

    p.add_argument("--max-insts", type=int, default=0,
                   help="cap on instructions after the ROI begins; 0 = run to "
                        "completion. Useful for calibration runs.")

    return p.parse_args(argv)


# --------------------------------------------------------------------------
# System construction
# --------------------------------------------------------------------------

def build_system(args):
    system = System()

    system.clk_domain = SrcClockDomain()
    system.clk_domain.clock = args.cpu_clock
    system.clk_domain.voltage_domain = VoltageDomain()

    system.mem_mode = "timing"
    system.mem_ranges = [AddrRange(args.mem_size)]

    # ---- CPU: out-of-order, the whole point of the study ----
    cpu = DerivO3CPU()

    w = args.issue_width
    cpu.fetchWidth = w
    cpu.decodeWidth = w
    cpu.renameWidth = w
    cpu.dispatchWidth = w
    cpu.issueWidth = w
    cpu.wbWidth = w
    cpu.commitWidth = w
    cpu.squashWidth = w

    cpu.numROBEntries = args.rob_size
    cpu.numIQEntries = args.iq_size
    cpu.LQEntries = args.lq_size
    cpu.SQEntries = args.sq_size

    system.cpu = cpu

    # ---- Caches ----
    system.cpu.icache = L1ICache()

    dcache = L1DCache()
    dcache.size = args.l1d_size
    dcache.tag_latency = args.l1d_latency
    dcache.data_latency = args.l1d_latency
    dcache.response_latency = args.l1d_latency
    system.cpu.dcache = dcache

    system.cpu.icache_port = system.cpu.icache.cpu_side
    system.cpu.dcache_port = system.cpu.dcache.cpu_side

    system.l2bus = L2XBar()
    system.cpu.icache.mem_side = system.l2bus.cpu_side_ports
    system.cpu.dcache.mem_side = system.l2bus.cpu_side_ports

    system.l2cache = L2Cache()
    system.l2cache.cpu_side = system.l2bus.mem_side_ports

    system.membus = SystemXBar()
    system.l2cache.mem_side = system.membus.cpu_side_ports

    system.cpu.createInterruptController()
    # x86 requires the interrupt controller wired to the memory bus.
    system.cpu.interrupts[0].pio = system.membus.mem_side_ports
    system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

    system.system_port = system.membus.cpu_side_ports

    system.mem_ctrl = MemCtrl()
    system.mem_ctrl.dram = DDR3_1600_8x8()
    system.mem_ctrl.dram.range = system.mem_ranges[0]
    system.mem_ctrl.port = system.membus.mem_side_ports

    return system


def main(argv):
    args = parse_args(argv)

    system = build_system(args)

    process = Process()
    process.cmd = [args.binary]
    system.workload = SEWorkload.init_compatible(args.binary)
    system.cpu.workload = process
    system.cpu.createThreads()

    root = Root(full_system=False, system=system)
    m5.instantiate()

    print("=" * 70)
    print("gem5 crypto microarchitecture study")
    print(f"  binary       : {args.binary}")
    print(f"  CPU          : DerivO3CPU @ {args.cpu_clock}")
    print(f"  issue width  : {args.issue_width}")
    print(f"  ROB / IQ     : {args.rob_size} / {args.iq_size}")
    print(f"  LQ / SQ      : {args.lq_size} / {args.sq_size}")
    print(f"  L1D          : {args.l1d_size}, {args.l1d_latency}-cycle")
    print("=" * 70)

    exit_event = m5.simulate()

    # The workload calls m5_reset_stats at the top of its loop and
    # m5_dump_stats at the bottom. The dump appears as an exit event we must
    # resume past, so setup/teardown stay out of the measured region.
    while exit_event.getCause() in ("dump stats", "reset stats",
                                    "dump/reset stats"):
        exit_event = m5.simulate()

    print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")


if __name__ == "__m5_main__":
    main(sys.argv[1:])
