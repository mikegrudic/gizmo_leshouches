# Baseline experiments

To start, try to obtain 3 basic solutions for a **stellar wind bubble**, an **HII region** with only radiative feedback, and a **supernova remnant** in a uniform-density medium. For each of these, try at least 3 different numerical resolutions and assess the numerical convergence.

Some good quantities to plot are:
- Kinetic, thermal, and magnetic (if applicable) energies
- $n_{\rm H}$ vs. $T$ phase diagram at different times. What is happening in various regions of the phase diagram?
- Post-shock temperature: is it consistent with analytic expectations for the velocity of the shock?
- The bubble shell radius versus time.
- Total radial momentum versus time.
- Total cooling rate (make sure to include the `OUTPUT_COOLRATE_DETAIL` flag in your Config.sh).
- Mass in different temperature/density phases versus time.
- Radial temperature structure at different times.
- Surface density maps: does the bubble remain spherical? If not, what structures develop?

Try to understand these results: do they make sense for the physical processes at work? Where applicable, compare these quantities with analytic expectations (we will discuss these in detail in feedback lecture 1).


# Further Explorations
Feedback-driven flows are an active area of research, and most cases that are more complex than the simple uniform-density bubbles above are essentially open problems: many have been explored, but there is generally no comprehensive theory. Here are some ideas for additional questions to explore. If there is another question that interests you, it may also be possible to explore with `GIZMO`, so have fun with it!

In general, try to compare your solutions with the most-applicable analytical bubble solutions. In all instances, try to run with multiple levels of numerical resolution so you know whether or not your result is converged.


## Interplay of different feedback mechanisms
To what extent do radiation, winds, and supernovae work together as more than the sum of their parts? You can run a simulation combining some or all of these feedback channels and compare the solution with your baseline solutions: does one dominant feedback mechanism operate independently of the others, or do they interfere constructively?

## Clustered feedback
Stars form in a clustered configuration, so in general feedback bubbles overlap. What happens when multiple massive stars are present, going supernova at different times? Can we model this analytically somehow?

## Effect of turbulent density and velocity structure
The first-order difference between our uniform-density cloud model and reality is the fact that the ISM is in a state of trans- to super-sonic turbulence, and has rich density structure. In principle this can affect the way feedback bubbles evolve, by changing the porosity to venting warm/hot gas or changing the transport of energy by facilitating mixing.

### Initializing turbulence
To add turbulent density structure to the simulation, we must do an initialization run. We can *initialize* the turbulent velocities by-hand and run the simulation to allow the density structure to develop, just by setting `--alpha_turb` > 0 in `MakeCloud`. 

Alternately or complementarily, we can *drive* the turbulence continuously by enabling `TURB_DRIVING`, whose parameters are controlled in the parameters file. The default parameters written by `MakeCloud` are sensible for implementing the "TURBSPHERE" setup described in [Lane et al. 2022](https://academic.oup.com/mnras/article/510/4/4767/6482854), which gives you an isolated, localized cloud embedded in a diffuse box. This is more realistic than a simple periodic box setup (also available via `--makebox`) for feedback experiments because the low-density boundary conditions can allow the pressurized gas to vent out of the cloud, changing the dynamics. To get the artificial potential that confines the cloud to the center of the box, enable `STARFORGE_GMC_TURBINIT=1`. Then, run for a few crossing times until a statistical steady state has been achieved. A good plot to check is the gas half-mass radius versus time during the stirring phase. 

You may have to tune `TurbDrive_ApproxRMSVturb` to get the level of turbulence you want: make sure it looks good at low resolution before committing to a high-resolution stirring run. The steady-state RMS velocity dispersion should scale roughly proportionally to this driving parameter, but there is a calibration factor that varies by setup.

Once the stirring is complete, you can use the final snapshot of the stirring run as the initial condition of your feedback run - just remember to disable `STARFORGE_GMC_TURBINIT` for the feedback setup.

## Magnetic Fields

Enabling `MAGNETIC` will build GIZMO with MHD enabled. Magnetic pressure and tension can affect the way feedback bubbles evolve in various ways, notably by pressure-confining the expansion (e.g. Krumholz 2006, Kim & Ostriker 2014), or stabilizing phase interfaces and suppressing energy transport through mixing (e.g. Lancaster 2024).

## Conduction

Thermal conduction is potentially an important energy transport mechanism for hot feedback bubbles produced by winds and supernovae. This can be accounted for by adding the following flags:

```
CONDUCTION
CONDUCTION_SPITZER
DIFFUSION_OPTIMIZERS
```

Note that these are enabled by default if `SINGLE_STAR_STARFORGE_DEFAULTS` and `MAGNETIC` are enabled. In the MHD case, the full anisotropic heat conduction along magnetic field lines will be solved.

## Eccentric feedback sources
Another complication is that the star or star cluster will not generally be in the exact center of the cloud, breaking out from the edge everywhere simultaneously. Instead, a "blister" can form. How does the evolution compare to the centered case?

## Distributed versus point-source feedback

What if 2 or more stars are distributed throughout the cloud instead of being clustered together? Does this change the bubble evolution or form any new structures?

## Metallicity
Most of the cooling processes governing the evolution of feedback-driven flows are due to metals. How are the evolution of HII regions, wind bubbles, and supernova remnants affected when we vary things from the default Solar metallicity?

## Effects of numerics

`GIZMO` implements a variety of solvers including different SPH flavors and its signature Meshless Finite Mass (MFM) and Meshless Finite Volume (MFV) methods. All solvers are wrong in their own peculiar ways, and the trick is to find the one best-suited to your problem.

How does your solution change when you switch the numerical solver? What is robust to these choices, and what is a numerical artifact?


## Protostellar jets
We haven't mentioned these yet because they require simulations with on-the-fly star formation, but protostellar jets can also be an important process for regulating the formation of individual stars and the state of turbulent gas in protostellar clusters.

Run a low-mass cloud/clump at a mass resolution at least as fine as $10^{-2}M_\odot$ (to resolve at least some of the IMF) with self-gravity, and compare the baseline run with a run that enables `SINGLE_STAR_FB_JETS`. What do the jets do to the properties of the cloud? The rate of star formation? The mass distribution of stars?