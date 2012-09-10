#include <vector>
#include <algorithm>

#include <boost/assert.hpp>
#include <boost/make_shared.hpp>

#include "RVBWavefunction.hpp"
#include "random-configuration.hpp"
#include "random-move.hpp"

RVBWavefunction::Amplitude::Amplitude (const PositionArguments &r_, const boost::shared_ptr<const Lattice> &lattice_, const std::vector<complex_t> &phi)
    : Wavefunction::Amplitude(r_, lattice_),
      m_update_in_progress(false),
      m_phi(phi)
{    
    reinitialize();
}

void RVBWavefunction::Amplitude::perform_move_ (const Move &move)
{
    // first assert that it's a swap
    BOOST_ASSERT(move.size() == 2);
    BOOST_ASSERT(move[0].particle.species != move[1].particle.species);
    BOOST_ASSERT(r[move[0].particle] == move[1].destination);
    BOOST_ASSERT(r[move[1].particle] == move[0].destination);

    const unsigned int M = r.get_N_filled(0);
    BOOST_ASSERT(M == r.get_N_filled(1));

    // the next four lines are a bit opaque, but correct
    const unsigned int moved_up_particle_index = move[move[0].particle.species].particle.index;
    const unsigned int moved_down_particle_index = move[move[1].particle.species].particle.index;
    const LatticeSite new_site_for_up(lattice->site_from_index(move[move[0].particle.species].destination));
    const LatticeSite new_site_for_down(lattice->site_from_index(move[move[1].particle.species].destination));

    Eigen::Matrix<complex_t, Eigen::Dynamic, 1> new_row(M);
    const std::vector<unsigned int> & down_pos = r.r_vector(1);
    for (unsigned int i = 0; i < M; ++i) {
        LatticeSite rup_minus_rdown(new_site_for_up);
        lattice->asm_subtract_site_vector(rup_minus_rdown, lattice->site_from_index(down_pos[i]).bravais_site());
        lattice->enforce_boundary(rup_minus_rdown);
        new_row[i] = m_phi[lattice->site_to_index(rup_minus_rdown)];
    }

    m_update_in_progress = true;
    m_new_cmat = m_cmat;

    m_new_cmat.update_row(moved_up_particle_index, new_row);
    m_new_cmat.finish_row_update();
    
    Eigen::Matrix<complex_t, Eigen::Dynamic, 1> new_col(M);
    const std::vector<unsigned int> & up_pos = r.r_vector(0);
    for (unsigned int i = 0; i < M; ++i) {
        LatticeSite rup_minus_rdown(lattice->site_from_index(up_pos[i]));
        lattice->asm_subtract_site_vector(rup_minus_rdown, new_site_for_down.bravais_site());
        lattice->enforce_boundary(rup_minus_rdown);
        new_col[i] = m_phi[lattice->site_to_index(rup_minus_rdown)];
    }

    m_new_cmat.update_column(moved_down_particle_index, new_col);
}

amplitude_t RVBWavefunction::Amplitude::psi_ (void) const
{
    return (m_update_in_progress ? m_new_cmat : m_cmat).get_determinant();
}

void RVBWavefunction::Amplitude::finish_move_ (void)
{
    m_new_cmat.finish_column_update();
    m_cmat = m_new_cmat;
    m_update_in_progress = false;
}

void RVBWavefunction::Amplitude::cancel_move_ (void)
{
    m_update_in_progress = false;
}

void RVBWavefunction::Amplitude::swap_particles_ (unsigned int particle1_index, unsigned int particle2_index, unsigned int species)
{
    if (species == 0) {
        m_cmat.swap_rows(particle1_index, particle2_index);
    } else {
        BOOST_ASSERT(species == 1);
        m_cmat.swap_columns(particle1_index, particle2_index);
    }
}

void RVBWavefunction::Amplitude::reset_ (const PositionArguments &r_)
{
    r = r_;
    reinitialize();
}

void RVBWavefunction::Amplitude::reinitialize (void)
{
    BOOST_ASSERT(r.get_N_species() == 2);

    BOOST_ASSERT(r.get_N_sites() == lattice->total_sites());

    // Each species is at half-filling and assume unpolarized wave function
    BOOST_ASSERT(2 * r.get_N_filled(0) == lattice->total_sites());
    BOOST_ASSERT(2 * r.get_N_filled(1) == lattice->total_sites());

    BOOST_ASSERT(r.get_N_sites() == m_phi.size());

    const unsigned int M = r.get_N_filled(0);

    Eigen::Matrix<complex_t, Eigen::Dynamic, Eigen::Dynamic> mat_phi(M, M);
    const std::vector<unsigned int> & up_pos = r.r_vector(0);
    const std::vector<unsigned int> & down_pos = r.r_vector(1);
    for (unsigned int i = 0; i < M; ++i) {
        const LatticeSite rup(lattice->site_from_index(up_pos[i]));
        for (unsigned int j = 0; j < M; ++j) {
            LatticeSite rup_minus_rdown(rup);
            lattice->asm_subtract_site_vector(rup_minus_rdown, lattice->site_from_index(down_pos[j]).bravais_site());
            lattice->enforce_boundary(rup_minus_rdown);
            mat_phi(i, j) = m_phi[lattice->site_to_index(rup_minus_rdown)];
        }
    }

    m_cmat = mat_phi;
}

boost::shared_ptr<Wavefunction::Amplitude> RVBWavefunction::Amplitude::clone_ (void) const
{
    return boost::make_shared<RVBWavefunction::Amplitude>(*this);
}

void RVBWavefunction::Amplitude::reset_with_random_configuration (RandomNumberGenerator &rng)
{
    BOOST_ASSERT(r.get_N_species() == 2);

    const unsigned int M = r.get_N_filled(0);
    BOOST_ASSERT(M == r.get_N_filled(1));

    const unsigned int N_sites = lattice->total_sites();
    BOOST_ASSERT(N_sites == r.get_N_sites());

    BOOST_ASSERT(r.get_N_filled_total() == N_sites);  // Assert a spin wave function!

    // Take into account Gutzwiller projection [exactly one spinon ("particle") per site]
    std::vector<std::vector<unsigned int> > vv(2);
    vv[0] = some_random_configuration(M, *lattice, rng);
    // NOTE: this method requires O(N ^ 2) time, but this could technically be
    // done in O(N) time.  Not a big deal here.
    for (unsigned int i = 0; i < N_sites; ++i) {
        std::vector<unsigned int>::iterator it = std::find(vv[0].begin(), vv[0].end(), i);
        if (it == vv[0].end())
            vv[1].push_back(i);
    }

    BOOST_ASSERT(vv[0].size() == vv[1].size());
    reset(PositionArguments(vv, N_sites));
}

Move RVBWavefunction::Amplitude::propose_move (RandomNumberGenerator &rng) const
{
    // choose a particle of each species and swap their positions
    Move move;
    const Particle particle(choose_random_particle(r, rng));
    const unsigned int proposed_site_index = plan_particle_move_to_nearby_empty_site(particle, r, *lattice, rng);
    if (proposed_site_index != r[particle]) {
        const unsigned int other_species = particle.species ^ 1;
        const int other_particle_index = r.particle_index_at_pos(proposed_site_index, other_species);
        BOOST_ASSERT(other_particle_index >= 0);
        const Particle other_particle(other_particle_index, other_species);

        move.push_back(SingleParticleMove(particle, proposed_site_index));
        move.push_back(SingleParticleMove(other_particle, r[particle]));
    }
    return move;
}