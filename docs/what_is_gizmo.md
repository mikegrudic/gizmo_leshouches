# What is GIZMO?

`GIZMO` is a multi-physics, multi-method radiation MHD code for astrophysics that is designed mainly around a set of mesh-free weighted-partition finite-volume methods. These methods essentially generalize the way a Voronoi tessellation moving-mesh code solves conservation laws through exchange of fluxes across the moving faces between domains (as in e.g. [Arepo](https://arepo-code.org/wp-content/userguide/index.html)). Think of it like a moving mesh, but where each point in space is assigned only a certain *weight* associated with each neighboring mesh-generating point. The discretization looks kind of like a Voronoi tessellation with blurry boundaries; the Voronoi partition is recovered in the limit of a very sharply-peaked (e.g. Dirac-) kernel function.

[**It's not SPH**](https://starforge-tools.readthedocs.io/en/latest/data.html#what-is-a-gas-cell-in-a-gizmo-mfm-mfv-simulation). A kernel spline function is involved, and there are many structural similarities between the algorithms used by GIZMO and SPH codes, but the method for solving conservation laws is fundamentally distinct. It is best to think of the discrete simulation elements as finite-volume "cells", not particles. However, the terms are often used interchangeably.

## Why GIZMO?

For us, the important thing is that **the numerical methods that GIZMO implements are often advantageous for star and galaxy formation problems**. One major advantage of is that the resolution elements follow the flow of the fluid. This makes the operation of advection much less **diffusive** than Eulerian simulations (e.g. RAMSES, ATHENA, FLASH). This allows structures formed in supersonic flows to be generally better-preserved, and a lesser degree of resolution is often required to converge on certain properties of the flow: you can often do less with more.

![png](images/ramses_sph_mfm_comparison.png)
*Comparison of an isothermal galaxy disk simulated with RAMSES AMR, SPH, and GIZMO's MFM and MFV methods at comparable resolution. Note the finer structure that is preserved by the Lagrangian methods in this highly supersonic flow ([Few 2016](https://ui.adsabs.harvard.edu/abs/2016MNRAS.460.4382F/abstract)).*

The trade-off is that mesh-free codes will typically be intrinsically slower than grid codes if the timesteps and numerical resolution are the same: the necessity of neighbor searching and tree traversals tends to make the computation more limited by moving data around than actual FLOPS. For the same reason it's also much harder to put on a GPU!

Another tradeoff is that you do not have the same freedom to arbitrarily specify your spatial resolution.

`GIZMO` also implements a wide variety of stellar feedback processes including protostellar jets, stellar winds, radiation, and supernovae, as well as the key ISM chemical and thermal processes that determine the impact that stellar feedback has. [Here](https://youtu.be/LeX5e51UkzI) is an example of a star formation calculation carried out with GIZMO. We will use `GIZMO` as a laboratory to experiment with stellar feedback.
