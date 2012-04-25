#ifndef _BASE_SWAP_POSSIBLE_WALK_HPP
#define _BASE_SWAP_POSSIBLE_WALK_HPP

#include <boost/assert.hpp>
#include <boost/shared_ptr.hpp>

#include "vmc-typedefs.hpp"
#include "Subsystem.hpp"
#include "SwappedSystem.hpp"
#include "WavefunctionAmplitude.hpp"

/**
 * Base class for Renyi walks that require the same number/types of particles
 * in the subsystem for each copy (i.e., a swap is always possible)
 *
 * See Y. Zhang et. al., PRL 107, 067202 (2011) for explanation
 *
 * @see RenyiSignWalk
 * @see RenyiModPossibleWalk
 */
class BaseSwapPossibleWalk
{
public:
    /**
     * Constructor.
     *
     * It is essential that both wf and wf_copy have the same number of
     * particles in the subsystem.
     */
    BaseSwapPossibleWalk (const boost::shared_ptr<WavefunctionAmplitude> &wf, const boost::shared_ptr<WavefunctionAmplitude> &wf_copy, boost::shared_ptr<const Subsystem> subsystem, bool update_swapped_system_before_accepting_=true);
    probability_t compute_probability_ratio_of_random_transition (rng_class &rng);
    void accept_transition (void);

    const WavefunctionAmplitude & get_phialpha1 (void) const
        {
            return *phialpha1;
        }

    const WavefunctionAmplitude & get_phialpha2 (void) const
        {
            return *phialpha2;
        }

    const WavefunctionAmplitude & get_phibeta1 (void) const
        {
            return swapped_system->get_phibeta1();
        }

    const WavefunctionAmplitude & get_phibeta2 (void) const
        {
            return swapped_system->get_phibeta2();
        }

private:
    virtual probability_t probability_ratio (amplitude_t phialpha1_ratio, amplitude_t phialpha2_ratio, amplitude_t phibeta1_ratio, amplitude_t phibeta2_ratio) const = 0;

    static unsigned int count_subsystem_sites (const Subsystem &subsystem, const Lattice &lattice);

    boost::shared_ptr<WavefunctionAmplitude> phialpha1, phialpha2;
    boost::shared_ptr<SwappedSystem> swapped_system;
    Particle chosen_particle_A, chosen_particle_B;
    const Particle *chosen_particle1, *chosen_particle2;
    unsigned int N_subsystem_sites; // remains constant after initialization
    bool update_swapped_system_before_accepting; // remains constant after initialization
    bool transition_in_progress;
};

#endif