#ifndef _STANDARD_WALK_HPP
#define _STANDARD_WALK_HPP

#include <boost/shared_ptr.hpp>

#include "vmc-typedefs.hpp"

class WavefunctionAmplitude;

class StandardWalk
{
public:
    StandardWalk (boost::shared_ptr<WavefunctionAmplitude> &wf_);
    probability_t compute_probability_ratio_of_random_transition (rng_class &rng);
    void accept_transition (void);

private:
    boost::shared_ptr<WavefunctionAmplitude> wf; // treat this as copy on write

#ifndef BOOST_DISABLE_ASSERTS
    bool transition_in_progress;
#endif

    // disable the default constructor
    StandardWalk (void);
};

#endif