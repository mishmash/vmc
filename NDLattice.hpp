#ifndef _N_D_LATTICE_HPP
#define _N_D_LATTICE_HPP

#include <cstddef>
#include <vector>

#include <boost/array.hpp>
#include <boost/assert.hpp>
#include <boost/random/uniform_smallint.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/random.hpp>

#include "Lattice.hpp"
#include "BoundaryCondition.hpp"
#include "PositionArguments.hpp"
#include "vmc-typedefs.hpp"

template<std::size_t DIM>
class NDLattice : public Lattice
{
public:
    static const unsigned int dimensions = DIM;

    typedef boost::array<BoundaryCondition, DIM> BoundaryConditions;

    typedef boost::array<int, DIM> BravaisSite;

    struct Site
    {
    private:
        BravaisSite bs;
    public:
        int basis_index;

        const BravaisSite & bravais_site (void) const
            {
                return bs;
            }

        int operator[] (std::size_t index) const
            {
                return bs[index];
            }

        int & operator[] (std::size_t index)
            {
                return bs[index];
            }

        bool operator== (const Site &other) const
            {
                return (bs == other.bs) && (basis_index == other.basis_index);
            }

        bool operator!= (const Site &other) const
            {
                return (bs != other.bs) || (basis_index != other.basis_index);
            }
    };

protected:
    // an axis by which we might want to move in configuration space.  Each
    // member here represents some step size.
    struct Move {
        boost::array<int, DIM> bravais_site;
        int basis_index;
    };

public:
    NDLattice (const boost::array<int, DIM> &length_, int basis_indices_=1)
        : Lattice(count_total_sites(length_, basis_indices_)),
          length(length_),
          basis_indices(basis_indices_)
        {
            // set up offsets
            unsigned int c = 1;
            for (unsigned int i = 0; i < DIM; ++i) {
                offset[i] = c;
                c *= length[i];
            }
            basis_offset = c;

            // set up default move axes
            for (unsigned int i = 0; i < DIM; ++i) {
                Move m;
                m.bravais_site.assign(0);
                m.bravais_site[i] = 1;
                m.basis_index = 0;
                move_axes.push_back(m);
            }
            if (basis_indices > 1) {
                Move m;
                m.bravais_site.assign(0);
                m.basis_index = 1;
                move_axes.push_back(m);
            }
        }

    Site site_from_index (unsigned int n) const
        {
            BOOST_ASSERT(n < total_sites());
            Site rv;
            for (unsigned int i = 0; i < DIM; ++i) {
                rv[i] = n % length[i];
                n /= length[i];
            }
            rv.basis_index = n;
            BOOST_ASSERT(site_is_valid(rv));
            return rv;
        }

    unsigned int site_to_index (const Site &site) const
        {
            BOOST_ASSERT(site_is_valid(site));

            unsigned int n = 0;
            for (unsigned int i = 0; i < DIM; ++i) {
                n += site[i] * offset[i];
            }
            n += site.basis_index * basis_offset;
            BOOST_ASSERT(site == site_from_index(n));
            return n;
        }

    bool site_is_valid (const Site &site) const
        {
            for (unsigned int i = 0; i < DIM; ++i) {
                if (site[i] >= length[i] || site[i] < 0)
                    return false;
            }
            if (site.basis_index >= basis_indices || site.basis_index < 0)
                return false;
            return true;
        }

    phase_t asm_add_site_vector (Site &site, const BravaisSite &other, const BoundaryConditions *bcs=0) const
        {
            for (unsigned int i = 0; i < DIM; ++i)
                site[i] += other[i];
            return enforce_boundary(site, bcs);
        }

    phase_t asm_subtract_site_vector (Site &site, const BravaisSite &other, const BoundaryConditions *bcs=0) const
        {
            for (unsigned int i = 0; i < DIM; ++i)
                site[i] -= other[i];
            return enforce_boundary(site, bcs);
        }

    phase_t enforce_boundary (Site &site, const BoundaryConditions *bcs=0) const
        {
            phase_t phase_change = 1;
            for (unsigned int dim = 0; dim < DIM; ++dim) {
                while (site[dim] >= length[dim]) {
                    site[dim] -= length[dim];
                    if (bcs)
                        phase_change *= (*bcs)[dim].phase();
                }
                while (site[dim] < 0) {
                    site[dim] += length[dim];
                    if (bcs)
                        phase_change /= (*bcs)[dim].phase();
                }
            }

            // this is often unnecessary ... should it be in a separate
            // function to be called before this one when needed?
            while (site.basis_index < 0)
                site.basis_index += basis_indices;
            site.basis_index %= basis_indices;

            BOOST_ASSERT(site_is_valid(site));
            return phase_change;
        }

    unsigned int move_axes_count (void) const
        {
            return move_axes.size();
        }

    void move_site (typename NDLattice<DIM>::Site &site, unsigned int move_axis, int step_direction) const
        {
            BOOST_ASSERT(move_axis < move_axes.size());
            BOOST_ASSERT(step_direction == -1 || step_direction == 1);
            const Move &m = move_axes[move_axis];
            for (unsigned int i = 0; i < DIM; ++i)
                site[i] += step_direction * m.bravais_site[i];
            site.basis_index += step_direction * m.basis_index;
            enforce_boundary(site);
        }

    unsigned int plan_particle_move_to_nearby_empty_site_virtual (unsigned int particle, const PositionArguments &r, rng_class &rng) const
        {
            unsigned int move_axis;
            if (this->move_axes_count() == 1) {
                move_axis = 0;
            } else {
                boost::uniform_smallint<> axis_distribution(0, this->move_axes_count() - 1);
                boost::variate_generator<rng_class&, boost::uniform_smallint<> > axis_gen(rng, axis_distribution);
                move_axis = axis_gen();
            }

            boost::uniform_smallint<> direction_distribution(0, 1);
            boost::variate_generator<rng_class&, boost::uniform_smallint<> > direction_gen(rng, direction_distribution);
            int step_direction = direction_gen() * 2 - 1;

            Site site = this->site_from_index(r[particle]);
            unsigned int site_index;
            do {
                this->move_site(site, move_axis, step_direction);
                site_index = this->site_to_index(site);
            } while (r.is_occupied(site_index) && site_index != r[particle]);

            return site_index;
        }

private:
    static inline unsigned int count_total_sites (const boost::array<int, DIM> &length, int basis_indices)
        {
            unsigned int rv = 1;
            for (unsigned int i = 0; i < DIM; ++i) {
                BOOST_ASSERT(length[i] > 0);
                rv *= length[i];
            }
            BOOST_ASSERT(basis_indices > 0);
            rv *= basis_indices;
            return rv;
        }

public:
    const boost::array<int, DIM> length;
    const int basis_indices;

private:
    // these both remain constant after initialization as well
    boost::array<int, DIM> offset;
    int basis_offset;

protected:
    // this can be modified at will until the object is fully instantiated, but
    // after that it should not be changed
    std::vector<struct Move> move_axes;
};

#endif
