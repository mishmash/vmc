from libcpp.vector cimport vector
from pyvmc.includes.boost.shared_ptr cimport shared_ptr

from pyvmc.core.rng cimport RandomNumberGenerator, CppRandomNumberGenerator
from pyvmc.core.lattice cimport Lattice, CppLattice
from pyvmc.core.orbitals cimport CppOrbitalDefinitions, const_CppOrbitalDefinitions

cdef extern from "Wavefunction.hpp":
    cdef cppclass CppWavefunctionAmplitude "Wavefunction::Amplitude":
        shared_ptr[CppWavefunctionAmplitude] clone()

    cdef cppclass CppWavefunction "Wavefunction":
        shared_ptr[CppWavefunctionAmplitude] create_nonzero_wavefunctionamplitude(shared_ptr[CppWavefunction]&, CppRandomNumberGenerator&)
        shared_ptr[CppWavefunctionAmplitude] create_nonzero_wavefunctionamplitude(shared_ptr[CppWavefunction]&, CppRandomNumberGenerator&, unsigned int)

cdef shared_ptr[CppWavefunctionAmplitude] create_wfa(wf) except *

cdef extern from "FreeFermionWavefunction.hpp":
    cdef cppclass CppFreeFermionWavefunction "FreeFermionWavefunction" (CppWavefunction):
        CppFreeFermionWavefunction(vector[shared_ptr[const_CppOrbitalDefinitions]]&, shared_ptr[CppJastrowFactor]&)

cdef class WavefunctionWrapper(object):
    cdef shared_ptr[CppWavefunction] sharedptr

cdef extern from "JastrowFactor.hpp":
    cdef cppclass CppJastrowFactor "JastrowFactor":
        pass

cdef extern from "NoDoubleOccupancyProjector.hpp":
    cdef cppclass CppNoDoubleOccupancyProjector "NoDoubleOccupancyProjector" (CppJastrowFactor):
        CppNoDoubleOccupancyProjector()

cdef class JastrowFactor(object):
    cdef shared_ptr[CppJastrowFactor] sharedptr
