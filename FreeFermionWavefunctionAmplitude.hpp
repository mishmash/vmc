#ifndef _FREE_FERMION_WAVEFUNCTION_AMPLITUDE_HPP
#define _FREE_FERMION_WAVEFUNCTION_AMPLITUDE_HPP

#include "WavefunctionAmplitude.hpp"
#include "PositionArguments.hpp"
#include "CeperlyMatrix.hpp"
#include "Chain1dOrbitals.hpp"

// i.e. single determinant w/o Jastrow factor

class FreeFermionWavefunctionAmplitude : public WavefunctionAmplitude
{
private:
    CeperlyMatrix<amplitude_t> cmat;
    Chain1dOrbitals orbital_def;

public:
    FreeFermionWavefunctionAmplitude (const PositionArguments &r_);

private:
    void move_particle_ (unsigned int particle, unsigned int new_site_index);

    amplitude_t psi_ (void) const;

    void finish_particle_moved_update_ (void);

    void reset_ (const PositionArguments &r_);

    boost::shared_ptr<WavefunctionAmplitude> clone_ (void) const;

    void reinitialize (void);
};

#endif