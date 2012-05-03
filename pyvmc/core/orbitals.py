import abc
import numbers
import collections
import logging

from pyvmc.core.lattice import Lattice
from pyvmc.core.boundary_conditions import valid_boundary_conditions, enforce_boundary, periodic, antiperiodic

logger = logging.getLogger(__name__)

class OrbitalsDescription(object):
    __metaclass__ = abc.ABCMeta

    @abc.abstractmethod
    def get_orbitals(self, lattice):
        raise NotImplementedError

class Orbitals(object):
    __metaclass__ = abc.ABCMeta

    def __init__(self, lattice):
        assert isinstance(lattice, Lattice)
        object.__setattr__(self, "lattice", lattice)

    @abc.abstractmethod
    def to_json(self):
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

    __slots__ = ("lattice", "momentum_sites", "boundary_conditions")

    def __init__(self, lattice, momentum_sites, boundary_conditions):
        super(MomentaOrbitals, self).__init__(lattice)
        assert isinstance(momentum_sites, collections.Sequence)
        lattice_dimensions = lattice.dimensions
        n_dimensions = len(lattice_dimensions)
        assert all([isinstance(ms, tuple) and
                    len(ms) == n_dimensions and
                    all(isinstance(x, numbers.Integral) and
                        x >= 0 and x < ld
                        for x, ld in zip(ms, lattice_dimensions))
                    for ms in momentum_sites])
        assert len(momentum_sites) == len(set(momentum_sites))
        object.__setattr__(self, "momentum_sites", tuple(momentum_sites))
        assert valid_boundary_conditions(boundary_conditions, n_dimensions)
        object.__setattr__(self, "boundary_conditions", tuple(boundary_conditions))

    def to_json(self):
        return {
            'filling': self.momentum_sites,
            'boundary-conditions': self.boundary_conditions,
        }

    def __eq__(self, other):
        return (self.__class__ == other.__class__ and
                self.lattice == other.lattice and
                set(self.momentum_sites) == set(other.momentum_sites) and
                self.boundary_conditions == other.boundary_conditions)

    def __ne__(self, other):
        return (self is not other) and not self.__eq__(other)

    def __setattr__(self, name, value):
        raise TypeError

    def __delattr__(self, name):
        raise TypeError

    def __hash__(self):
        return hash(self.lattice) | hash(self.momentum_sites) | hash(self.boundary_conditions)

    def __repr__(self):
        return "%s(%s, %s, %s)" % (
            self.__class__.__name__,
            repr(self.lattice),
            repr(self.momentum_sites),
            repr(self.boundary_conditions)
        )

class Bands(OrbitalsDescription):
    """Used for a 2d, quasi-1d system

    Takes a number of particles for each band
    """

    __slots__ = ("particles_by_band", "boundary_conditions")

    def __init__(self, particles_by_band, boundary_conditions):
        assert isinstance(particles_by_band, collections.Sequence)
        assert all(isinstance(n, numbers.Integral) and n >= 0 for n in particles_by_band)
        self.particles_by_band = tuple(particles_by_band)
        n_dimensions = 1 if len(particles_by_band) == 1 else 2
        assert valid_boundary_conditions(boundary_conditions, n_dimensions)
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

        return MomentaOrbitals(lattice, orbitals, self.boundary_conditions)

    def __repr__(self):
        return "%s(%s, %s)" % (
            self.__class__.__name__,
            repr(self.particles_by_band),
            repr(self.boundary_conditions)
        )