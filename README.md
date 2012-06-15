vmc
===

Performs variational Monte Carlo (VMC) calcuations on lattice systems
that are of interest to the authors of this code.

Requires a recent [boost](http://www.boost.org/) (headers only) and
[eigen3](http://eigen.tuxfamily.org/).  The input mechanism for
declaring calculations also requires
[jsoncpp](http://jsoncpp.sourceforge.net/) 0.6.0-rc2 or later.

A file named vmc-core/Makefile-vmc.local can be created to override
any variables in the Makefile. For example, it could say:

    EIGEN3_CFLAGS = -I/path/to/eigen3/include
    BOOST_CFLAGS = -I/path/to/boost/include
    CXX = clang++

To compile and run:

    $ cd vmc-core
    $ make && ./vmc-core < sample-input.json

It should compile on recent versions of g++, clang++, and icc.

Documentation for users
-----------------------

The "vmc-core" program uses JSON for input and output.  A sample input
file is given as sample-input.json.  More complete documentation about
its structure is coming soon.

API Documentation
-----------------

If doxygen is installed, documentation can be generated by running

    $ make docs

Afterwards, HTML documentation will exist at
vmc-core/docs/generated/html/index.html

If PDF output is desired, change directory to
vmc-core/docs/generated/latex/ and run "make".

Caveats
-------

Only wavefunctions whose (slave) particles obey Pauli exclusions are
currently supported (i.e. fermions and hard-core bosons).

Renyi can only be calculated for wavefunctions that move at most one
particle at a time.
