# Setting up your GIZMO stack

## Prerequisites

You will need a python environment in a reasonably-modern version (e.g. 3.10+) to use most of the supported tools and GIZMO's automatic testing system. 

## Required packages
At minimum, building GIZMO requires two widely-available packages:
- HDF5
- GSL (GNU Scientific Library)

To use the FFT-based gravity solver you also need `FFTW3`, but it is not required for many problems.

The paths to these libraries must be known by the build system. This is set up for a variety of pre-set system configurations in `Makefile` (including common setups like a Macbook with Homebrew packages (`MacBookCellar`), and PSMN).


## Getting the code

``git clone https://github.com/mikegrudic/gizmo_leshouches``

This is a branch of GIZMO managed for the purposes of this workshop.

Some other things you will probably want:
``git clone https://github.com/mikegrudic/MakeCloud``
``git clone https://github.com/mikegrudic/meshoid``
``git clone https://github.com/mikegrudic/CrunchSnaps``

All of these (including the GIZMO repo) are installable as python packages using pip.

## Building GIZMO

GIZMO's build system works as follows:

0. Specify the system environment setup you wish to build with. Different setups for different implemented HPC environments are found in different cases in the `Makefile`. For example, there is a `PSMN` environment. If you are running on a Mac with packages installed via homebrew, you can use `MacbookCellar`. To tell it which to use, modify `Makefile.systype` in the source directory, uncommenting the `SYSTYPE=...` line corresponding to your system, or, more conveniently, create a `.gizmo` file in your root home directory with a single line `SYSTYPE=...`.
1. Specify the options to compile the binary with in a file `Config.sh`. At baseline, essentially everything we would want to run for our stellar feedback experiments would include the following:
```
SINGLE_STAR_STARFORGE_DEFAULTS
BOX_PERIODIC
SELFGRAVITY_OFF
COOLING
OUTPUT_COOLRATE_DETAIL
```

For full description of the different flags you can enable, see the [gizmo documentation](http://www.tapir.caltech.edu/~phopkins/Site/GIZMO_files/gizmo_documentation.html) or have a scroll through `Template_Config.sh`.

Note that `SINGLE_STAR_STARFORGE_DEFAULTS` enables a whole host of modules for the problems we will be running (see `declarations/precompiler_logic.h` for details.)

2. run `make -j` to run the build in parallel (leave out the `-j` if using all the cores doesn't happen to be good citizenship on the system you are on). This will produce a `GIZMO` binary.


## Config flags for different feedback mechanisms

* Radiation: to enable the full, 5-band radiative transfer treatment, enable `SINGLE_STAR_FB_RAD`. This will account for photons in the EUV (i.e. H-ionizing), FUV, NUV, Optical/Near-IR, and far-IR bands. To get more granular control over which radiation bands you include, see e.g. `test/HII_region/Config.sh`. Another important parameter is the speed-of-light-reduction factor, which allows us to take larger timesteps by slowing down light. A good setting to start with is `RT_SPEEDOFLIGHT_REDUCTION=1e-4`, which sets it to $30 \rm km;s^{-1}$: very slow, but fast enough to get HII region dynamics mostly right. To disable radiation pressure and isolated the pure effects of thermal gas pressure in your HII region, you can use `RT_DISABLE_RAD_PRESSURE`.
* Winds: `SINGLE_STAR_FB_WINDS=2` is the default setting. The value is a bitflag: the least-significant bit toggles whether the Vink 2001 mass-loss prescription is used, otherwise the weaker STARFORGE prescription is used. The next-to-least significant bit toggles whether to use the more-powerful Sabhahit 2022 prescription for very massive stars. The default `2` setting combines the STARFORGE prescription and the Sabhahit prescription (taking the larger of two $\dot{M}$'s).
* Supernovae: `SINGLE_STAR_FB_SNE` makes stars explode in a $10^51 \rm erg$ supernova at the end of their lifetime. Note that you will need to initialize the star's age appropriately if you want a SN to go off at the beginning of your simulation.


