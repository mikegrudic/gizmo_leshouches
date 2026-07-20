#!/bin/bash
set -e
cd ~

# ── environment ────────────────────────────────────────────────────────────────
echo 'SYSTYPE="PSMN"' > ~/.gizmo
python3 -m venv ~/py313
grep -qF 'source ~/py313/bin/activate' ~/.bashrc \
    || echo 'source ~/py313/bin/activate' >> ~/.bashrc

# ── repos ──────────────────────────────────────────────────────────────────────
git clone https://github.com/mikegrudic/gizmo_leshouches
git clone https://github.com/mikegrudic/MakeCloud
git clone https://github.com/mikegrudic/meshoid
git clone https://github.com/mikegrudic/CrunchSnaps

# ── python packages ────────────────────────────────────────────────────────────
~/py313/bin/pip install -q \
    -e "$HOME/gizmo_leshouches[test]" \
    -e ~/MakeCloud \
    -e ~/meshoid \
    -e ~/CrunchSnaps \
    jupyter

# ── soundwave test (uses its own Config.sh; must run before we write ours) ─────
cat > /tmp/soundwave_test.sh << 'EOF'
#!/bin/bash
#SBATCH --job-name=soundwave
#SBATCH --partition=Lake-short
#SBATCH --nodes=1 --ntasks=8 --cpus-per-task=3
#SBATCH --time=01:00:00
#SBATCH --output=%x_%j.out
cd ~/gizmo_leshouches
~/py313/bin/pytest test/soundwave -v
EOF

JOB=$(sbatch /tmp/soundwave_test.sh | awk '{print $NF}')
echo "Soundwave job $JOB submitted — waiting..."
while squeue -j "$JOB" -h -o "%T" 2>/dev/null | grep -qE "PENDING|RUNNING"; do
    sleep 30
done
OUT=$(find ~ -name "soundwave_${JOB}.out" 2>/dev/null | head -1)
if ! grep -q "failed" "$OUT" 2>/dev/null; then
    echo "Soundwave test passed."
else
    echo "Soundwave test FAILED. Check $OUT before continuing." >&2
    exit 1
fi

# ── Config.sh and build ────────────────────────────────────────────────────────
cat > ~/gizmo_leshouches/Config.sh << 'EOF'
SINGLE_STAR_STARFORGE_DEFAULTS
BOX_PERIODIC
SELFGRAVITY_OFF
COOLING
OUTPUT_COOLRATE_DETAIL
EOF

cd ~/gizmo_leshouches && make -j8

# ── initial conditions ─────────────────────────────────────────────────────────
mkdir -p ~/run/feedback_baseline && cd ~/run/feedback_baseline
~/py313/bin/MakeCloud --N=32000 --alpha_turb=0 --Mstar=40 --star_age=0.1

# ── submit feedback run ────────────────────────────────────────────────────────
PARAMS=$(ls params_*.txt | head -1)

cat > run.sh << EOF
#!/bin/bash
#SBATCH --job-name=feedback_baseline
#SBATCH --partition=Lake-short
#SBATCH --nodes=1 --ntasks=24 --cpus-per-task=1
#SBATCH --time=04:00:00
#SBATCH --output=%x_%j.out
cd ~/run/feedback_baseline
export OMP_NUM_THREADS=\$SLURM_CPUS_PER_TASK
srun --mpi=pmix_v5 --cpu-bind=none ~/gizmo_leshouches/GIZMO $PARAMS 0
EOF

sbatch run.sh
echo "Done."
