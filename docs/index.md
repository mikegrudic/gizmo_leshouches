# Stellar Feedback Experiments with GIZMO

[![Documentation](https://readthedocs.org/projects/gizmo-leshouches/badge/?version=latest)](https://gizmo-leshouches.readthedocs.io/en/latest/)

![HII region simulation](images/Rad_Cool.gif)

## What is GIZMO?

`GIZMO` is a multi-physics, multi-method radiation MHD code for astrophysics that is designed mainly around a set of mesh-free weighted-partition finite-volume methods. These methods essentially generalize the way a Voronoi tessellation moving-mesh code solves conservation laws through exchange of fluxes across the moving faces between domains (as in e.g. [Arepo](https://arepo-code.org/wp-content/userguide/index.html)). Think of it like a moving mesh, but where each point in space is assigned only a certain *weight* associated with each neighboring mesh-generating point. The discretization looks kind of like a Voronoi tessellation with blurry boundaries; the Voronoi partition is recovered in the limit of a very sharply-peaked (e.g. Dirac-) kernel function.

[**It's not SPH**](https://starforge-tools.readthedocs.io/en/latest/data.html#what-is-a-gas-cell-in-a-gizmo-mfm-mfv-simulation). A kernel spline function is involved, and there are many structural similarities between the algorithms used by GIZMO and SPH codes, but the method for solving conservation laws is fundamentally distinct. It is best to think of the discrete simulation elements as finite-volume "cells", not particles. However, the terms are often used interchangeably.

For us, the important thing is that these methods are often advantageous for star formation problems, and `GIZMO` implements a wide variety of stellar feedback processes including protostellar jets, stellar winds, radiation, and supernovae, as well as the key ISM chemical and thermal processes that determine the impact that stellar feedback has. [Here](https://youtu.be/LeX5e51UkzI) is an example of a star formation calculation carried out with GIZMO. We will use `GIZMO` as a laboratory to experiment with stellar feedback.




```{toctree}
:maxdepth: 2
:caption: Contents

setup
running
visualization
experiments
```
