# README #

This is a multi-objective integer programming (MOIP) algorithm which I call boxsplit, based off the following paper:

Kerstin Dächert, Kathrin Klamroth (2015). A linear bound on the number of scalarizations needed to solve discrete tricriteria optimization problems. Journal of Global Optimization, 61(4), 643--676

### What is this repository for? ###

This software can be used to optimise various multi-objective integer programming problems. Currently, work is not yet stabilised and no guarantees can be offered on the correctness of various versions of the implementation. Official releases will hopefully be coming shortly.

### How do I get set up? ###

The implementation works on Linux operating systems, and requires IBM ILOG CPLEX 12.6.3 (or possibly greater) and Boost. It currently uses CMake for configuration.

It uses an extended LP file format where multiple objectives are defined as additional constraints after the original problem's constraints. The right-hand-side value of the last constraint defines the number of objectives. Example LP files are provided under a separate folder.

### Who do I talk to? ###

Dr William Pettersson (william@ewpettersson.se) is the lead developer of this particular implementation of this algorithm. For more details on the original algorithm, you may also wish to contact the authors of the above paper.
