===============
Hermes Examples
===============

.. raw:: latex
 
   \floatplacement{figure}{H}

Thank you for your interest in Hermes!

This document contains benchmarks and examples for Hermes. The first part 
of the document shows how Hermes performs on benchmarks (examples with known exact solutions). 
Several of them come from the National Institute for Standards and Technology (NIST). 
In the second part we preset examples from various application areas such as acoustics, 
fluid and solid mechanics, heat transfer, electromagnetics, neutronics, quantum chemistry, 
ground-water flow, and others. 

This document is under continuous development. If you find bugs, typos, dead links 
and such, please report them to the 
`Hermes2D mailing list <http://groups.google.com/group/hermes2d/>`_.

Installation
--------------------
- installation is very simple and has the following steps:

| 1) clone the repository
| 2) on Windows (and on other platforms as well if necessary), copy CMake.vars.example to CMake.vars and adjust it.
| 3) HERMES_DIRECTORY and HERMES_INCLUDE_PATH should point to lib and include directories where you installed HERMES.
|    typically, these will be "lib" and "include" subdirectories of the directory specified by TARGET_ROOT in your library repository.
| 4) the DEP_ROOT variable should in most cases point to the same variable in CMake.vars in your library repository.
| 5) the same for the PTHREAD_ROOT variable
| 6!!) important step is to set WITH_TRILINOS to the same value as you set it in your library repository. Otherwise, linking issues will occur.

Benchmarks - General
--------------------

.. toctree::
    :maxdepth: 1

    src/hermes2d/benchmarks-general

Benchmarks - NIST
-----------------

.. toctree::
    :maxdepth: 1

    src/hermes2d/benchmarks-nist

Advanced Examples
-----------------

.. toctree::
    :maxdepth: 1

    src/hermes2d/examples/acoustics.rst
    src/hermes2d/examples/advection-diffusion-reaction.rst
    src/hermes2d/examples/euler.rst
    src/hermes2d/examples/flame-propagation.rst
    src/hermes2d/examples/heat-transfer.rst
    src/hermes2d/examples/helmholtz.rst
    src/hermes2d/examples/linear-elasticity.rst
    src/hermes2d/examples/maxwell.rst
    src/hermes2d/examples/navier-stokes.rst
    src/hermes2d/examples/nernst-planck.rst
    src/hermes2d/examples/neutronics.rst
    src/hermes2d/examples/poisson.rst
    src/hermes2d/examples/richards.rst
    src/hermes2d/examples/schroedinger.rst
    src/hermes2d/examples/thermoelasticity.rst
    src/hermes2d/examples/wave-equation.rst
    src/hermes2d/examples/miscellaneous.rst
    
1D Examples
-----------

Hermes2D can be used to solve 1D problems. Several examples of this are
collected in the directory 1d/. 
