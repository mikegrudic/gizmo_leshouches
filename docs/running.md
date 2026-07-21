# Running Simulations

## Running test problems

GIZMO has an automatic testing system. A host of test problems can be found in `tests/`, which can be run with pytest. For example, the most basic test for a linear soundwave can be run with 

`pytest test/soundwave`

and the simulation output snapshots will be placed in `test/soundwave/output`. At minimum, pytest will tell you whether or not the test passed. Many tests also generate informative diagnostic plots. It's always a good idea to run a few of these just to make sure GIZMO is working OK on your system.

### Plotting the output
See also: [Interfacing with GIZMO HDF5 outputs](https://starforge-tools.readthedocs.io/en/latest/wiki_pages/interfacing_with_gizmo_starforge_hdf5_outputs.html)

If we wanted to make our own plots, the simplest way is by directly interfacing with the snapshot data with `h5py`:
```python
%matplotlib inline
from matplotlib import pyplot as plt
import h5py

with h5py.File("output/snapshot_015.hdf5") as F:
    x = F["PartType0/Coordinates"][:]
    rho = F["PartType0/Density"][:]

plt.scatter(x[:,0],rho)
```




    <matplotlib.collections.PathCollection at 0x7f77d00ec980>




    
![png](images/output_0_1.png)

Note the hierarchical structure of the snapshots: "particle" type at the top level, with each particle type having a set of attributes. For full documentation of the data fields see the GIZMO docs, but some specific fields we may encounter in star formation setups are described in detail in the [starforge documentation](https://starforge-tools.readthedocs.io/en/latest/).



## Setting up an ISM cloud 

[`MakeCloud`](github.com/mikegrudic/MakeCloud/) is a tool for setting up the initial conditions for idealized GMC or ISM cloud simulations. It has many different options with certain convenient defaults.  Run `MakeCloud -h` to get a rundown of all of the different options. By default, MakeCloud assumes God's system of units for dealing with objects on the scale of GMCs and star clusters: $M_\odot$, $\rm km\thinspace s^{-1}$, $\rm pc$, and $\rm G$. The resulting time unit is $T = \rm pc / (km\thinspace s^{-1}) \approx 1 \rm Myr$. Nice, right?

An important thing to note when setting up GIZMO simulations is that, typically, we are following finite-mass, quasi-Lagrangian elements around, so we must specify a *mass* resolution, and the spatial resolution adapts to the density as $\Delta x = \left(\Delta m/\rho\right)^{1/3}$. When running `MakeCloud`, we can specify the mass resolution via either the `--N` parameter (which specifies the number of gas cells initially in the cloud), or the `--dm` parameter (which sets the actual $\Delta m$ in code units).

`MakeCloud` will generate the HDF5 initial conditions file for the cloud, as well as a parameter file with some sensible defaults. Note that if you want a static cloud you must pass `--alpha_turb=0`, otherwise the cloud will be initialized with random turbulent velocities.

### Running the simulation

The basic command to run `GIZMO` on one core is `./GIZMO params.txt 0`. The parameters file specifies where the initial conditions file can be found. The latter `0` flag indicates that we want to start a brand-new simulation, as opposed to restarting from a set of restartfiles (`1`) or from a snapshot (`2`).

GIZMO is hybrid MPI/OpenMP code, but it can be run in pure MPI mode, and we will do so for simplicity unless (god forbid) we end up tuning a performance bottleneck for the project. If using OpenMP threads, the environment variable `OMP_NUM_THREADS` must be set.

A script for submitting a GIZMO job on PSMN is:
```bash
#!/bin/bash
#SBATCH --job-name=gizmo
#SBATCH --partition=Lake-short          # 4h limit; use Lake-long for longer runs
#SBATCH --nodes=1
#SBATCH --ntasks=24                     # MPI ranks (total across all nodes)
#SBATCH --cpus-per-task=1              # OMP threads per rank; set >1 for hybrid MPI+OMP
#SBATCH --time=04:00:00
#SBATCH --output=%x_%j.out

GIZMO=~/gizmo/GIZMO
PARAMS=params.txt

export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

srun --mpi=pmix_v5 --cpu-bind=none "$GIZMO" "$PARAMS" 0
```

The additional flags in the `srun` command were needed to get hybrid mode to work but may not be necessary for pure MPI...

