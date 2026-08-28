# VM Setup and Workflow

The project is authored on the Windows host (`C:\Users\HP\Documents\gem5-crypto-arch`)
and built/run inside the Ubuntu VirtualBox guest, where gem5 lives. This file
records how the two halves connect, because leaving that until results need
collecting is the standard way to lose a weekend.

---

## 1. Host ↔ VM file movement

**Use git, not a shared folder.** A VirtualBox shared folder works, but its
`vboxsf` filesystem has caused problems with build tools and file permissions,
and it leaves no history. Git gives version control for free and is what the
project needs anyway for the GitHub deliverable.

On the host, commit and push as normal. In the VM:

```bash
git clone <repo-url> ~/gem5-crypto-arch      # first time
cd ~/gem5-crypto-arch && git pull            # thereafter
```

Results flow back the same way. `results/raw/*/stats.txt` files are small text;
commit the curated ones. `m5out/` is gitignored so casual gem5 output does not
pollute the repo.

If you do prefer a shared folder for quick iteration, add yourself to `vboxsf`
and reboot the guest:

```bash
sudo usermod -aG vboxsf $USER
```

---

## 2. Building the m5 utility library

The workloads call `m5_reset_stats()` / `m5_dump_stats()` to mark the region of
interest. Those come from gem5's own `libm5.a`, which must be built once:

```bash
cd $GEM5_ROOT/util/m5
scons build/x86/out/m5
```

This produces:
- `$GEM5_ROOT/util/m5/build/x86/out/libm5.a` — link against this
- `$GEM5_ROOT/include/gem5/m5ops.h` — the header

If `scons` complains about a missing `gem5/asm/generic/m5ops.h`, the include
path is wrong; confirm `$GEM5_ROOT/include` exists in your checkout.

---

## 3. Building the workloads

```bash
cd ~/gem5-crypto-arch/workloads

make check                    # known-answer tests FIRST
make M5_PATH=$HOME/gem5       # gem5 binaries -> bin/*.gem5
```

`make check` must pass before any performance number is trusted. It verifies
ASCON against the official KAT, AES against FIPS-197, and DES against two
classic vectors. As of the initial build all six tests pass.

Confirm the binaries are genuinely static and AES-NI-free:

```bash
file bin/ascon.gem5            # expect "statically linked"
objdump -d bin/aes.gem5 | grep -ci aesenc    # expect 0
```

That second check is the important one. If it returns anything but zero, the
`-mno-aes` flag did not take effect and the whole comparison is invalid.

---

## 4. Running

```bash
cd ~/gem5-crypto-arch

export GEM5_ROOT=$HOME/gem5
./scripts/run_sweep.sh calibrate    # ALWAYS do this first
```

Calibration runs one ASCON config and prints the wall time. Multiply by 3 for
E1, 12 for E2, 12 for E3 to decide what needs to run overnight. O3 in a VM is
slow; a single run in the minutes is normal and a run in the tens of minutes
means `N_BLOCKS` should come down:

```bash
cd workloads && make clean && make N_BLOCKS=500 M5_PATH=$HOME/gem5
```

Then:

```bash
./scripts/run_sweep.sh e1
./scripts/run_sweep.sh e2
./scripts/run_sweep.sh e3
```

The driver skips any run that already has a `stats.txt`, so an interrupted
sweep resumes rather than restarting.

---

## 5. Parsing and plotting

```bash
python3 scripts/parse_stats.py results/raw -o results/parsed.csv
python3 scripts/plot_results.py results/parsed.csv -o results/figures
```

**Watch for the `roi_markers` column in the CSV.** If it says `NO`, that run's
`stats.txt` had only one dump section, meaning the binary was built without
`-DGEM5_ROI` and the numbers include process setup and teardown. Those are not
comparable to ROI-marked runs — rebuild and rerun rather than reporting them.

---

## 6. Sanity checks before trusting a sweep

- `make check` passes.
- `objdump | grep -c aesenc` returns 0 for the AES binary.
- `roi_markers` is `yes` for every row in `parsed.csv`.
- `committed_insts` is in the same order of magnitude across workloads at a
  fixed N. A wild outlier usually means one binary was built with a different
  `N_BLOCKS`.
- Two runs of the same config produce identical stats. gem5 is deterministic;
  if they differ, something is wrong with the setup.
