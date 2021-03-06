#include <algorithm>

#include "SwappedSystem.hpp"
#include "Lattice.hpp"

// REMEMBER: make sure phibeta's are always updated with copy-on-write

template <typename T>
static int vector_find (const std::vector<T> &vec, const T &val)
{
    for (unsigned int i = 0; i < vec.size(); ++i) {
        if (vec[i] == val)
            return i;
    }
    return -1;
}

SwappedSystem::SwappedSystem (const boost::shared_ptr<const Subsystem> &subsystem_)
    : subsystem(subsystem_),
      phibeta1_dirty(false),
      phibeta2_dirty(false),
      current_state(UNINITIALIZED)
{
}

void SwappedSystem::initialize (const Wavefunction::Amplitude &phialpha1, const Wavefunction::Amplitude &phialpha2)
{
    BOOST_ASSERT(current_state == UNINITIALIZED);

    const PositionArguments &r1 = phialpha1.get_positions();
    const PositionArguments &r2 = phialpha2.get_positions();

    // FIXME: we need a way to assert that phialpha1 and phialpha2 represent
    // the same wave function, just with different amplitudes.  Then again,
    // we're only calling this function from two places in the code where this
    // can be easily verified ...

#if !defined(BOOST_DISABLE_ASSERTS) && !defined(NDEBUG)
    BOOST_ASSERT(r1.get_N_species() == r2.get_N_species());
    for (unsigned int i = 0; i < r1.get_N_species(); ++i)
        BOOST_ASSERT(r1.get_N_filled(i) == r2.get_N_filled(i));
#endif
    BOOST_ASSERT(r1.get_N_sites() == r2.get_N_sites());
    BOOST_ASSERT(subsystem->lattice_makes_sense(phialpha1.get_lattice()));
    BOOST_ASSERT(subsystem->lattice_makes_sense(phialpha2.get_lattice()));
    BOOST_ASSERT(&phialpha1.get_lattice() == &phialpha2.get_lattice());

    const unsigned int N_species = r1.get_N_species();
    copy1_subsystem_indices.resize(N_species);
    copy2_subsystem_indices.resize(N_species);
    for (unsigned int species = 0; species < N_species; ++species) {
        const unsigned int N = r1.get_N_filled(species);
        for (unsigned int i = 0; i < N; ++i) {
            const Particle particle(i, species);
            if (subsystem->position_is_within(r1[particle], phialpha1.get_lattice()))
                copy1_subsystem_indices[species].push_back(i);
            if (subsystem->position_is_within(r2[particle], phialpha2.get_lattice()))
                copy2_subsystem_indices[species].push_back(i);
        }
    }

    BOOST_ASSERT(subsystem_particle_counts_match());
    reinitialize_phibetas(phialpha1, phialpha2);

    current_state = READY;
}

void SwappedSystem::update (const Particle *particle1, const Particle *particle2, const Wavefunction::Amplitude &phialpha1, const Wavefunction::Amplitude &phialpha2)
{
    // this function should be called *after* the phialpha's have been updated

    BOOST_ASSERT(current_state == READY);
    current_state = UPDATE_IN_PROGRESS;

    const PositionArguments &r1 = phialpha1.get_positions();
    const PositionArguments &r2 = phialpha2.get_positions();

#if !defined(BOOST_DISABLE_ASSERTS) && !defined(NDEBUG)
    BOOST_ASSERT(r1.get_N_species() == r2.get_N_species());
    for (unsigned int i = 0; i < r1.get_N_species(); ++i)
        BOOST_ASSERT(r1.get_N_filled(i) == r2.get_N_filled(i));
#endif

    BOOST_ASSERT(!particle1 || r1.particle_is_valid(*particle1));
    BOOST_ASSERT(!particle2 || r2.particle_is_valid(*particle2));

    const Lattice &lattice = phialpha1.get_lattice();

    // these will be will be >= 0 if the particle was in the subsystem before,
    // -1 if the particle was not in the subsystem before, and -2 if the
    // particle isn't even being moved.
    int pairing_index1 = (!particle1) ? -2 : vector_find(copy1_subsystem_indices[particle1->species], particle1->index);
    int pairing_index2 = (!particle2) ? -2 : vector_find(copy2_subsystem_indices[particle2->species], particle2->index);

    const bool particle1_now_in_subsystem = (particle1 && subsystem->position_is_within(r1[*particle1], lattice));
    const bool particle2_now_in_subsystem = (particle2 && subsystem->position_is_within(r2[*particle2], lattice));

    const int delta1 = (particle1_now_in_subsystem ? 1 : 0) + (pairing_index1 >= 0 ? -1 : 0);
#if !defined(BOOST_DISABLE_ASSERTS) && !defined(NDEBUG)
    const int delta2 = (particle2_now_in_subsystem ? 1 : 0) + (pairing_index2 >= 0 ? -1 : 0);
#endif

    BOOST_ASSERT(particle1 || delta1 == 0);
    BOOST_ASSERT(particle2 || delta2 == 0);

    BOOST_ASSERT(delta1 == delta2);
    const int delta = delta1;

    BOOST_ASSERT(delta == 0 || (particle1 && particle2 && particle1->species == particle2->species));

    BOOST_ASSERT(delta == 0 || particle1_now_in_subsystem == particle2_now_in_subsystem);
    // to ensure only a single update is necessary to the phibeta's, we require
    // that a particle only be moved in one copy if the particle number is not
    // changing
    BOOST_ASSERT(delta != 0 || !(particle1 && particle2));

    // remember a few things in case we need to cancel
    recent_delta = delta;
    if (particle1)
        recent_particle1 = *particle1;
    if (particle2)
        recent_particle2 = *particle2;

    if (delta == -1) {
        // if a particle of the same type leaves each subsystem simultaneously,
        // we need to use some special logic in case we have to re-pair the
        // remaining particles in the subsystem.  (re-pair in the sense of what
        // gets swapped with what)

        // these repeat some logic in the "if" statement, but are a useful
        // sanity check nonetheless (for now)
        BOOST_ASSERT(pairing_index1 >= 0 && pairing_index2 >= 0);
        BOOST_ASSERT(!particle1_now_in_subsystem);
        BOOST_ASSERT(!particle2_now_in_subsystem);

        const unsigned int species = particle1->species;
        std::vector<unsigned int> &c1_s = copy1_subsystem_indices[species];
        std::vector<unsigned int> &c2_s = copy2_subsystem_indices[species];

        // copy-on-write for the phibeta's
        if (!phibeta1.unique())
            phibeta1 = phibeta1->clone();
        if (!phibeta2.unique())
            phibeta2 = phibeta2->clone();

        if (pairing_index1 != pairing_index2) {
            // in order to re-pair the particles left behind, we move the pair
            // that is leaving the subsystem to the max_pairing_index, and move
            // the pair that is staying behind to the min_pairing_index.
            phibeta1->swap_particles(c1_s[pairing_index1], c1_s[pairing_index2], species);
            phibeta2->swap_particles(c2_s[pairing_index1], c2_s[pairing_index2], species);
            if (pairing_index1 < pairing_index2)
                std::swap(c1_s[pairing_index1], c1_s[pairing_index2]);
            else
                std::swap(c2_s[pairing_index1], c2_s[pairing_index2]);
        }

        // update the phibeta's
        const unsigned int max_pairing_index = std::max(pairing_index1, pairing_index2);
        BOOST_ASSERT(!phibeta1_dirty && !phibeta2_dirty);
        {
            Move move;
            move.push_back(SingleParticleMove(Particle(c1_s[max_pairing_index], species), r1[*particle1]));
            phibeta1->perform_move(move);
        }
        {
            Move move;
            move.push_back(SingleParticleMove(Particle(c2_s[max_pairing_index], species), r2[*particle2]));
            phibeta2->perform_move(move);
        }
        phibeta1_dirty = true;
        phibeta2_dirty = true;

        // remove the empty pair in the subsystem indices (yes, these steps
        // make sense whether we had to re-pair or not)
        c1_s[max_pairing_index] = c1_s[c1_s.size() - 1];
        c1_s.pop_back();
        c2_s[max_pairing_index] = c2_s[c2_s.size() - 1];
        c2_s.pop_back();
    } else {
        BOOST_ASSERT(delta == 0 || delta == 1);

        // either both particles moved within their respective subsystems
        // (if they moved at all), or both entered the subsystem and paired
        // with each other immediately

        // update the subsystem indices if necessary
        if (delta == 1) {
            std::vector<unsigned int> &c1_s = copy1_subsystem_indices[particle1->species];
            std::vector<unsigned int> &c2_s = copy2_subsystem_indices[particle2->species];
            c1_s.push_back(particle1->index);
            pairing_index1 = c1_s.size() - 1;
            c2_s.push_back(particle2->index);
            pairing_index2 = c2_s.size() - 1;
        }

        BOOST_ASSERT(subsystem_particle_counts_match());

        // update the phibeta's, performing copy-on-write
        if (particle1) {
            boost::shared_ptr<Wavefunction::Amplitude> &phibeta = particle1_now_in_subsystem ? phibeta2 : phibeta1;
            bool &phibeta_dirty = particle1_now_in_subsystem ? phibeta2_dirty : phibeta1_dirty;
            const Particle phibeta_particle = particle1_now_in_subsystem ? Particle(copy2_subsystem_indices[particle1->species][pairing_index1], particle1->species) : *particle1;
            if (!phibeta.unique())
                phibeta = phibeta->clone();
            BOOST_ASSERT(!phibeta_dirty);
            Move move;
            move.push_back(SingleParticleMove(phibeta_particle, r1[*particle1]));
            phibeta->perform_move(move);
            phibeta_dirty = true;
        }

        if (particle2) {
            boost::shared_ptr<Wavefunction::Amplitude> &phibeta = particle2_now_in_subsystem ? phibeta1 : phibeta2;
            bool &phibeta_dirty = particle2_now_in_subsystem ? phibeta1_dirty : phibeta2_dirty;
            const Particle phibeta_particle = particle2_now_in_subsystem ? Particle(copy1_subsystem_indices[particle2->species][pairing_index2], particle2->species) : *particle2;
            if (!phibeta.unique())
                phibeta = phibeta->clone();
            // the only time both particles will move here is when delta == 1,
            // in which case this phibeta and the phibeta above will be
            // different, so we know that phibeta_dirty will never be true
            // here.
            BOOST_ASSERT(!phibeta_dirty);
            Move move;
            move.push_back(SingleParticleMove(phibeta_particle, r2[*particle2]));
            phibeta->perform_move(move);
            phibeta_dirty = true;
        }
    }
}

void SwappedSystem::finish_update (const Wavefunction::Amplitude &phialpha1, const Wavefunction::Amplitude &phialpha2)
{
    BOOST_ASSERT(current_state == UPDATE_IN_PROGRESS);
    current_state = READY;

    BOOST_ASSERT(subsystem_particle_counts_match());

    if (phibeta1_dirty) {
        phibeta1->finish_move();
    }
    phibeta1_dirty = false;

    if (phibeta2_dirty) {
        phibeta2->finish_move();
    }
    phibeta2_dirty = false;

#ifdef VMC_CAREFUL
    verify_phibetas(phialpha1, phialpha2);
#else
    (void) phialpha1;
    (void) phialpha2;
#endif
}

void SwappedSystem::cancel_update (const Wavefunction::Amplitude &phialpha1, const Wavefunction::Amplitude &phialpha2)
{
    BOOST_ASSERT(current_state == UPDATE_IN_PROGRESS);
    current_state = READY;

    BOOST_ASSERT(subsystem_particle_counts_match());

    if (phibeta1_dirty)
        phibeta1->cancel_move();
    phibeta1_dirty = false;

    if (phibeta2_dirty)
        phibeta2->cancel_move();
    phibeta2_dirty = false;

    if (recent_delta != 0) {
        // we must revert our changes to copy[1|2]_subsystem_indices
        BOOST_ASSERT(recent_particle1.species == recent_particle2.species);
        const unsigned int species = recent_particle1.species;
        if (recent_delta == 1) {
            // the particles paired with each other immediately, so we get rid
            // of them from the subsystem_indices
            copy1_subsystem_indices[species].pop_back();
            copy2_subsystem_indices[species].pop_back();
        } else {
            BOOST_ASSERT(recent_delta == -1);
            // we must re-pair the particles since they returned to the
            // subsystem
            copy1_subsystem_indices[species].push_back(recent_particle1.index);
            copy2_subsystem_indices[species].push_back(recent_particle2.index);
        }
    }

#ifdef VMC_CAREFUL
    verify_phibetas(phialpha1, phialpha2);
#else
    (void) phialpha1;
    (void) phialpha2;
#endif
}

bool SwappedSystem::subsystem_particle_counts_match (void) const
{
    BOOST_ASSERT(copy1_subsystem_indices.size() == copy2_subsystem_indices.size());

    for (unsigned int i = 0; i < copy1_subsystem_indices.size(); ++i) {
        if (copy1_subsystem_indices[i].size() != copy2_subsystem_indices[i].size())
            return false;
    }
    return true;
}

void SwappedSystem::reinitialize_phibetas (const Wavefunction::Amplitude &phialpha1, const Wavefunction::Amplitude &phialpha2)
{
    BOOST_ASSERT(subsystem_particle_counts_match());

#if defined(DEBUG_VMC_SWAPPED_SYSTEM) || defined(DEBUG_VMC_ALL)
    for (unsigned int species = 0; species < copy1_subsystem_indices.size(); ++species)
        std::cerr << "swapping " << copy1_subsystem_indices[species].size() << " particles of species " << species << std::endl;
    std::cerr << std::endl;
#endif

    PositionArguments swapped_r1(phialpha1.get_positions()), swapped_r2(phialpha2.get_positions());
    swap_positions(swapped_r1, swapped_r2);

    phibeta1 = phialpha1.clone();
    phibeta1->reset(swapped_r1);
    phibeta1_dirty = false;

    phibeta2 = phialpha2.clone();
    phibeta2->reset(swapped_r2);
    phibeta2_dirty = false;

#ifdef VMC_CAREFUL
    verify_phibetas(phialpha1, phialpha2);
#endif
}

void SwappedSystem::verify_phibetas (const Wavefunction::Amplitude &phialpha1, const Wavefunction::Amplitude &phialpha2) const
{
#if defined(BOOST_DISABLE_ASSERTS) || defined(NDEBUG)
    (void) phialpha1;
    (void) phialpha2;
#else
    const PositionArguments &r1 = phialpha1.get_positions();
    const PositionArguments &r2 = phialpha2.get_positions();

    BOOST_ASSERT(r1.get_N_species() == r2.get_N_species());
    BOOST_ASSERT(r1.get_N_sites() == r2.get_N_sites());

    BOOST_ASSERT(copy1_subsystem_indices.size() == r1.get_N_species());
    BOOST_ASSERT(copy2_subsystem_indices.size() == r1.get_N_species());

    const Lattice &lattice = phialpha1.get_lattice();

    for (unsigned int species = 0; species < r1.get_N_species(); ++species) {
        const unsigned int N = r1.get_N_filled(species);
        BOOST_ASSERT(N == r2.get_N_filled(species));

        // verify that the subsystem index arrays have everything they need (and no duplicates!)
        unsigned int c1 = 0, c2 = 0;
        for (unsigned int i = 0; i < N; ++i) {
            const Particle particle(i, species);
            const bool b1 = vector_find(copy1_subsystem_indices[species], i) != -1;
            const bool b2 = vector_find(copy2_subsystem_indices[species], i) != -1;
            if (b1)
                ++c1;
            if (b2)
                ++c2;
            BOOST_ASSERT(b1 == subsystem->position_is_within(r1[particle], lattice));
            BOOST_ASSERT(b2 == subsystem->position_is_within(r2[particle], lattice));
        }
        BOOST_ASSERT(c1 == c2);
        BOOST_ASSERT(c1 == copy1_subsystem_indices[species].size());
        BOOST_ASSERT(c2 == copy2_subsystem_indices[species].size());
    }

    BOOST_ASSERT(phibeta1 != 0);
    BOOST_ASSERT(phibeta2 != 0);

    // verify that the positions in the phibeta's are correct
    PositionArguments swapped_r1(phialpha1.get_positions()), swapped_r2(phialpha2.get_positions());
    swap_positions(swapped_r1, swapped_r2);

    for (unsigned int species = 0; species < r1.get_N_species(); ++species) {
        for (unsigned int i = 0; i < r1.get_N_filled(species); ++i) {
            const Particle particle(i, species);
            BOOST_ASSERT(swapped_r1[particle] == phibeta1->get_positions()[particle]);
            BOOST_ASSERT(swapped_r2[particle] == phibeta2->get_positions()[particle]);
        }
    }
#endif
}

// this is a private utility method, called from both reinitialize_phibetas()
// and verify_phibetas()
void SwappedSystem::swap_positions (PositionArguments &r1, PositionArguments &r2) const
{
    BOOST_ASSERT(r1.get_N_species() == r2.get_N_species());
    BOOST_ASSERT(r1.get_N_species() == copy1_subsystem_indices.size());
    BOOST_ASSERT(r1.get_N_species() == copy2_subsystem_indices.size());

#if !defined(BOOST_DISABLE_ASSERTS) && !defined(NDEBUG)
    BOOST_ASSERT(r1.get_N_species() == r2.get_N_species());
    for (unsigned int i = 0; i < r1.get_N_species(); ++i)
        BOOST_ASSERT(r1.get_N_filled(i) == r2.get_N_filled(i));
#endif

    bool some_particles_have_been_swapped = false;

    std::vector<std::vector<unsigned int> > v1, v2;

    for (unsigned int species = 0; species < r1.get_N_species(); ++species) {
        BOOST_ASSERT(copy1_subsystem_indices[species].size() == copy2_subsystem_indices[species].size());

        v1.push_back(r1.r_vector(species));
        v2.push_back(r2.r_vector(species));

        const unsigned int particles_to_swap = copy1_subsystem_indices[species].size();

        if (particles_to_swap == 0)
            continue;

        some_particles_have_been_swapped = true;

        for (unsigned int i = 0; i < particles_to_swap; ++i) {
            unsigned int i1 = copy1_subsystem_indices[species][i], i2 = copy2_subsystem_indices[species][i];
            // perform the swap
            unsigned int tmp_pos = v1[species][i1];
            v1[species][i1] = v2[species][i2];
            v2[species][i2] = tmp_pos;
        }

    }

    if (some_particles_have_been_swapped) {
        r1.reset(v1);
        r2.reset(v2);
    }
}

bool count_subsystem_particle_counts_for_match (const Wavefunction::Amplitude &wf1, const Wavefunction::Amplitude &wf2,
                                                const Subsystem &subsystem)
{
    BOOST_ASSERT(subsystem.lattice_makes_sense(wf1.get_lattice()));
    BOOST_ASSERT(subsystem.lattice_makes_sense(wf2.get_lattice()));
    // (we are also assuming that the lattices are in fact equivalent)

    const PositionArguments &r1 = wf1.get_positions();
    const PositionArguments &r2 = wf2.get_positions();

    BOOST_ASSERT(r1.get_N_species() == r2.get_N_species());
    BOOST_ASSERT(r1.get_N_sites() == r2.get_N_sites());

    for (unsigned int species = 0; species < r1.get_N_species(); ++species) {
        BOOST_ASSERT(r1.get_N_filled(species) == r2.get_N_filled(species));

        unsigned int count1 = 0, count2 = 0;

        for (unsigned int i = 0; i < r1.get_N_filled(species); ++i) {
            const Particle particle(i, species);
            if (subsystem.position_is_within(r1[particle], wf1.get_lattice()))
                ++count1;
            if (subsystem.position_is_within(r2[particle], wf2.get_lattice()))
                ++count2;
        }

        if (count1 != count2)
            return false;
    }

    return true;
}
