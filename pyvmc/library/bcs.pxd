from libcpp.vector cimport vector
from pyvmc.includes.boost.shared_ptr cimport shared_ptr
from pyvmc.core cimport complex_t

from pyvmc.core.wavefunction cimport CppWavefunction, WavefunctionWrapper
from pyvmc.core.lattice cimport CppLattice, Lattice

cdef extern from "BCSWavefunction.hpp":
    cdef cppclass CppPhiMatrix "Eigen::Matrix<complex_t, Eigen::Dynamic, Eigen::Dynamic>":
        CppPhiMatrix(unsigned int, unsigned int)
        CppPhiMatrix(CppPhiMatrix&)

    cdef cppclass CppBCSWavefunction "BCSWavefunction" (CppWavefunction):
        CppBCSWavefunction(shared_ptr[CppLattice]&, CppPhiMatrix&, unsigned int)

    cdef void set_matrix_coeff "BCSWavefunction::set_matrix_coeff" (CppPhiMatrix&, unsigned int, unsigned int, complex_t)
