from fractions import Fraction
import collections
import numbers

from pyvmc.core.lattice import Lattice

periodic = 1
antiperiodic = Fraction(1, 2)

def enforce_boundary(bravais_site, lattice, boundary_conditions=None):
    """Enforce the boundary of a site which may be outside the lattice

    Any quantum amplitudes should be multiplied by
    exp(2 * pi * i * phase_adjustment)
    """
    assert isinstance(lattice, Lattice)
    lattice_dimensions = lattice.dimensions
    assert isinstance(bravais_site, collections.Sequence)
    assert len(bravais_site) == len(lattice_dimensions)
    assert all(isinstance(x, numbers.Integral) for x in bravais_site)
    new_bravais_site = tuple(x % length for x, length in zip(bravais_site, lattice_dimensions))
    if boundary_conditions is None:
        return new_bravais_site
    else:
        assert isinstance(boundary_conditions, collections.Sequence)
        assert len(boundary_conditions) == len(lattice_dimensions)
        assert all(isinstance(bc, numbers.Real) and bc != 0
                   for bc in boundary_conditions)
        phase_adjustment = sum((x // length) * bc for x, length, bc
                               in zip(bravais_site, lattice_dimensions, boundary_conditions)) % 1
        return new_bravais_site, phase_adjustment
