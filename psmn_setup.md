# GIZMO Stellar Feedback Stack — PSMN Setup

From a clean home directory to a running baseline simulation: virtualenv, repos, soundwave test, and a 40 M☉ feedback run.

---

## 1. Create `~/.gizmo`

Tells the build system which Makefile configuration to use.

```bash
echo 'SYSTYPE="PSMN"' > ~/.gizmo
```

## 2. Create Python virtualenv

```bash
python3 -m venv ~/py313
```

Optionally auto-activate in new shells:

```bash
echo 'source ~/py313/bin/activate' >> ~/.bashrc
```

## 3. Clone repos

```bash
git clone https://github.com/mikegrudic/gizmo_leshouches
git clone https://github.com/mikegrudic/MakeCloud
git clone https://github.com/mikegrudic/meshoid
git clone https://github.com/mikegrudic/CrunchSnaps
```

## 4. Install Python packages

All four repos install as editable packages (`-e`), so edits to source files take effect immediately without reinstalling. The `[test]` extras on `gizmo_leshouches` pull in `pytest`, `h5py`, `matplotlib`, and other test dependencies.

```bash
~/py313/bin/pip install -e ~/gizmo_leshouches[test] \
    -e ~/MakeCloud \
    -e ~/meshoid \
    -e ~/CrunchSnaps \
    jupyter
```

## 5. Run the soundwave test

Verifies the MPI setup is correct before committing to a long run. Do this **before** writing your own `Config.sh` — the test framework overwrites the root `Config.sh` with the test's own config when it builds.

`soundwave_test.sh`:
```bash
#!/bin/bash
#SBATCH --job-name=soundwave
#SBATCH --partition=Lake-short
#SBATCH --nodes=1 --ntasks=8 --cpus-per-task=3
#SBATCH --time=01:00:00
#SBATCH --output=%x_%j.out

cd ~/gizmo_leshouches
~/py313/bin/pytest test/soundwave -v
```

```bash
sbatch soundwave_test.sh
```

## 6. Write `Config.sh`

Specifies which physics modules to compile into the GIZMO binary. `SINGLE_STAR_STARFORGE_DEFAULTS` enables a curated set of modules for stellar feedback experiments; the remaining flags layer on specific feedback channels.

```bash
cat > ~/gizmo_leshouches/Config.sh << 'EOF'
SINGLE_STAR_STARFORGE_DEFAULTS
BOX_PERIODIC
SELFGRAVITY_OFF
COOLING
OUTPUT_COOLRATE_DETAIL
EOF
```

## 7. Build GIZMO

```bash
cd ~/gizmo_leshouches && make -j8
```

Produces a `GIZMO` binary in the repo root (~4 MB with the full feedback stack). Compiler warnings about `MPI_TYPE_TIME` are harmless.

## 8. Generate initial conditions with MakeCloud

Creates a 2×10⁴ M☉ uniform-density cloud (10 pc radius, 32 000 gas cells, solar metallicity, no turbulence) with a 40 M☉ star at the center. Also writes a `params_*.txt` file with sensible defaults for the run.

```bash
mkdir -p ~/run/feedback_baseline && cd ~/run/feedback_baseline
~/py313/bin/MakeCloud --N=32000 --alpha_turb=0 --Mstar=40 --star_age=0.1
```

> **Note** — `--star_age=0.1` sets the star's age to 0.1 Myr so it is already on the main sequence at the start of the run. A 40 M☉ star lives ~4 Myr, so winds and radiation will operate for most of a 4-hour Lake-short job before the supernova fires. To catch the SN within a shorter run, set `--star_age` closer to the main-sequence lifetime.

## 9. Submit the baseline feedback run


`~/run/feedback_baseline/run.sh`:
```bash
#!/bin/bash
#SBATCH --job-name=feedback_baseline
#SBATCH --partition=Lake-short
#SBATCH --nodes=1 --ntasks=24 --cpus-per-task=1
#SBATCH --time=04:00:00
#SBATCH --output=%x_%j.out

cd ~/run/feedback_baseline
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

srun --mpi=pmix_v5 --cpu-bind=none \
    ~/gizmo_leshouches/GIZMO \
    params_M2e4_Mstar40_R10_Z1_S0_A0_B0.1_I1_Res32_n2_sol0.5_42.txt 0
```

```bash
sbatch ~/run/feedback_baseline/run.sh
```

> **Note** — `--mpi=pmix_v5` is required on PSMN. Without it, OpenMPI falls back to starting each `srun` rank as a singleton (MPI_Comm_size = 1), silently corrupting the simulation without any error message.

---

## Quick reference

You can just copy and paste these commands to do everything.

```bash
# ~/.gizmo and venv
echo 'SYSTYPE="PSMN"' > ~/.gizmo
python3 -m venv ~/py313
echo 'source ~/py313/bin/activate' >> ~/.bashrc  # optional

# repos and packages
git clone https://github.com/mikegrudic/gizmo_leshouches
git clone https://github.com/mikegrudic/MakeCloud
git clone https://github.com/mikegrudic/meshoid
git clone https://github.com/mikegrudic/CrunchSnaps
~/py313/bin/pip install -e ~/gizmo_leshouches[test] \
    -e ~/MakeCloud -e ~/meshoid -e ~/CrunchSnaps jupyter

# soundwave test (do before writing Config.sh)
cat > soundwave_test.sh << 'EOF'
#!/bin/bash
#SBATCH --job-name=soundwave
#SBATCH --partition=Lake-short
#SBATCH --nodes=1 --ntasks=8 --cpus-per-task=3
#SBATCH --time=01:00:00
#SBATCH --output=%x_%j.out
cd ~/gizmo_leshouches
~/py313/bin/pytest test/soundwave -v
EOF
sbatch soundwave_test.sh

# Config.sh and build
cat > ~/gizmo_leshouches/Config.sh << 'EOF'
SINGLE_STAR_STARFORGE_DEFAULTS
BOX_PERIODIC
SELFGRAVITY_OFF
COOLING
OUTPUT_COOLRATE_DETAIL
EOF
cd ~/gizmo_leshouches && make -j8

# ICs and run
mkdir -p ~/run/feedback_baseline && cd ~/run/feedback_baseline
~/py313/bin/MakeCloud --N=32000 --alpha_turb=0 --Mstar=40 --star_age=0.1
cat > run.sh << 'EOF'
#!/bin/bash
#SBATCH --job-name=feedback_baseline
#SBATCH --partition=Lake-short
#SBATCH --nodes=1 --ntasks=24 --cpus-per-task=1
#SBATCH --time=04:00:00
#SBATCH --output=%x_%j.out
cd ~/run/feedback_baseline
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
srun --mpi=pmix_v5 --cpu-bind=none \
    ~/gizmo_leshouches/GIZMO \
    params_M2e4_Mstar40_R10_Z1_S0_A0_B0.1_I1_Res32_n2_sol0.5_42.txt 0
EOF
sbatch run.sh
```

---

*PSMN · Debian 13 · gompi-2025a toolchain (GCC 14.2 + OpenMPI 5.0.7) · Python 3.13*
