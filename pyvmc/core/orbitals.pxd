from pyvmc.includes.boost.shared_ptr cimport shared_ptr
from pyvmc.includes.libcpp.memory cimport auto_ptr
from pyvmc.core cimport complex_t

from pyvmc.core.lattice cimport Lattice, CppLattice

cdef extern from "OrbitalDefinitions.hpp":
    cdef cppclass CppOrbitalMatrix "Eigen::Matrix<amplitude_t, Eigen::Dynamic, Eigen::Dynamic>":
        CppOrbitalMatrix(unsigned int, unsigned int)
        CppOrbitalMatrix(CppOrbitalMatrix&)

    cdef cppclass CppOrbitalDefinitions "OrbitalDefinitions":
        CppOrbitalDefinitions(CppOrbitalMatrix&, shared_ptr[CppLattice])

    ctypedef CppOrbitalDefinitions const_CppOrbitalDefinitions "const OrbitalDefinitions"

    cdef void set_matrix_coeff "OrbitalDefinitions::set_matrix_coeff" (CppOrbitalMatrix&, unsigned int, unsigned int, complex_t)

cdef shared_ptr[const_CppOrbitalDefinitions] orbitals_to_orbitaldefinitions(orbitals, Lattice lattice) except *
