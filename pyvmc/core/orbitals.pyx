import six

from cython.operator cimport dereference as deref

import abc
import numbers
import collections
import logging

import numpy

from pyvmc.core.boundary_conditions import valid_closed_boundary_conditions, periodic, antiperiodic
from pyvmc.utils.immutable import Immutable

logger = logging.getLogger(__name__)

two_pi_i = 2j * numpy.pi

def allowed_momentum(momentum_site, lattice, boundary_conditions):
    """Given a tuple representing a momentum site (along with some boundary
    conditions), return the corresponding momentum vector divided by 2pi

    A brief explanation of how momentum sites are addressed: If the tuple of
    all zeroes is passed as momentum_site, this will return the first momentum
    site in either the first quadrant, or, in the case of all periodic boundary
    conditions, at the origin.  Other tuples are considered as an offset in
    relation to this site.
    """
    from fractions import Fraction
    from pyvmc.core.lattice import LatticeSite
    assert LatticeSite(momentum_site) in lattice
    return tuple((ms + (bc.p % 1)) * Fraction(1, ll)
                 for ms, bc, ll in zip(momentum_site,
                                       boundary_conditions,
                                       lattice.dimensions))

cdef shared_ptr[const_CppOrbitalDefinitions] orbitals_to_orbitaldefinitions(orbitals, Lattice lattice) except *:
    assert isinstance(orbitals, Orbitals)
    m = orbitals.get_orbitals_matrix()
    cdef unsigned int rows, cols
    rows, cols = m.shape
    cdef auto_ptr[CppOrbitalMatrix] mat
    mat.reset(new CppOrbitalMatrix(rows, cols))
    cdef unsigned int i, j
    cdef complex_t c
    for i in xrange(rows):
        for j in xrange(cols):
            c_ = m[(i, j)]
            c = complex_t(c_.real, c_.imag)
            set_matrix_coeff(deref(mat.get()), i, j, c)
    cdef shared_ptr[const_CppOrbitalDefinitions] rv
    rv.reset(new CppOrbitalDefinitions(deref(mat), lattice.sharedptr))
    return rv

class OrbitalsDescription(six.with_metaclass(abc.ABCMeta)):
    @abc.abstractmethod
    def get_orbitals(self, lattice):
        raise NotImplementedError

class Orbitals(Immutable, collections.Sized):
    __slots__ = ("lattice",)

    def init_validate(self, lattice):
        assert isinstance(lattice, Lattice)
        return (lattice,)

    @abc.abstractmethod
    def to_json(self):
        return None

    @abc.abstractmethod
    def get_orbitals_matrix(self):
        """N_filled rows by len(lattice) columns"""
        return None

    @staticmethod
    def from_description(orbitals_or_description, lattice):
        if isinstance(orbitals_or_description, Orbitals):
            assert orbitals_or_description.lattice == lattice
            return orbitals_or_description
        elif isinstance(orbitals_or_description, OrbitalsDescription):
            return orbitals_or_description.get_orbitals(lattice)
        else:
            raise TypeError

class MomentaOrbitals(Orbitals):
    """takes momentum (k) vectors to make its orbitals."""

    __slots__ = ("lattice", "momentum_sites", "boundary_conditions", "_orbitals_matrix")
    _immutable_slots = ("lattice", "momentum_sites", "boundary_conditions")

    def init_validate(self, lattice, momentum_sites, boundary_conditions):
        (lattice,) = super(MomentaOrbitals, self).init_validate(lattice)
        assert isinstance(momentum_sites, collections.Sequence)
        lattice_dimensions = lattice.dimensions
        n_dimensions = len(lattice_dimensions)
        assert all([isinstance(ms, tuple) and
                    len(ms) == n_dimensions and
                    all(isinstance(x, numbers.Integral) and
                        x >= 0 and x < ld
                        for x, ld in zip(ms, lattice_dimensions))
                    for ms in momentum_sites])
        assert len(momentum_sites) == len(frozenset(momentum_sites))
        momentum_sites = tuple(sorted(momentum_sites))
        assert valid_closed_boundary_conditions(boundary_conditions, n_dimensions)
        object.__setattr__(self, "_orbitals_matrix", None)
        return lattice, momentum_sites, tuple(boundary_conditions)

    def __len__(self):
        return len(self.momentum_sites)

    def get_orbitals_matrix(self):
        if self._orbitals_matrix is None:
            orbital_defs = []
            for momentum_site in self.momentum_sites:
                k_site = allowed_momentum(momentum_site, self.lattice, self.boundary_conditions)
                orbital_defs.append([numpy.exp(two_pi_i * numpy.dot(k_site, r.bs))
                                     for r in self.lattice])
            object.__setattr__(self, "_orbitals_matrix", numpy.array(orbital_defs, dtype=complex))
        return self._orbitals_matrix

    def to_json(self):
        return collections.OrderedDict([
            ("type", self.__class__.__name__),
            ("momentum_sites", self.momentum_sites),
            ("boundary_conditions", self.boundary_conditions),
        ])

class Bands(OrbitalsDescription):
    """Used for a 2d, quasi-1d system

    Takes a number of particles for each band
    """

    target_class = MomentaOrbitals

    __slots__ = ("particles_by_band", "boundary_conditions")

    def __init__(self, particles_by_band, boundary_conditions):
        assert isinstance(particles_by_band, collections.Sequence)
        assert all(isinstance(n, numbers.Integral) and n >= 0 for n in particles_by_band)
        self.particles_by_band = tuple(particles_by_band)
        n_dimensions = 1 if len(particles_by_band) == 1 else 2
        assert valid_closed_boundary_conditions(boundary_conditions, n_dimensions)
        self.boundary_conditions = tuple(boundary_conditions)
        if boundary_conditions[0] in (periodic, antiperiodic):
            # check for bad bands
            even_or_odd = 1 if (boundary_conditions[0] == periodic) else 0
            bad_bands = [f for f in particles_by_band if f != 0 and f % 2 != even_or_odd]
            if bad_bands:
                bc_type_string = "periodic" if (boundary_conditions[0] == periodic) else "antiperiodic"
                logger.warning("Bad band(s): %s (%s)", bad_bands, bc_type_string)

    @staticmethod
    def _single_band_orbitals(post_tuple, n_particles, lattice):
        rv = [((((n + 1) // 2) * ((n & 1) * -2 + 1)) % lattice.dimensions[0],) + post_tuple
              for n in xrange(n_particles)]
        # make sure there are no duplicates
        assert len(set(rv)) == len(rv)
        return rv

    def get_orbitals(self, lattice):
        assert len(lattice.dimensions) == len(self.boundary_conditions)
        if len(self.particles_by_band) == 1:
            # one dimension
            orbitals = self._single_band_orbitals((), self.particles_by_band[0], lattice)
        else:
            # two dimensions
            orbitals = []
            for i, b in enumerate(self.particles_by_band):
                orbitals.extend(self._single_band_orbitals((i,), b, lattice))

        return self.target_class(lattice, orbitals, self.boundary_conditions)

    def __repr__(self):
        return "%s(%s, %s)" % (
            self.__class__.__name__,
            repr(self.particles_by_band),
            repr(self.boundary_conditions)
        )
