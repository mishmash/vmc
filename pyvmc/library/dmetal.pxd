from pyvmc.includes.boost.shared_ptr cimport shared_ptr

from pyvmc.core.orbitals cimport CppOrbitalDefinitions
from pyvmc.core.wavefunction cimport CppWavefunction, WavefunctionWrapper

cdef extern from "DMetalWavefunction.hpp":
    cdef cppclass CppDMetalWavefunction "DMetalWavefunction" (CppWavefunction):
        CppDMetalWavefunction(shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&)
        CppDMetalWavefunction(shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&, double, double)
        CppDMetalWavefunction(shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&, shared_ptr[CppOrbitalDefinitions]&, double, double, double, double)
