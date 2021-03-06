#ifndef _VMC_BOUNDARY_CONDITION_HPP
#define _VMC_BOUNDARY_CONDITION_HPP

#include <cmath>

#include <boost/assert.hpp>
#include <boost/rational.hpp>
#include <boost/math/constants/constants.hpp>

#include "lw_vector.hpp"
#include "vmc-typedefs.hpp"

// fixme: make this class not be inline?

/**
 * Represents a boundary condition in one dimension for a system that exists on
 * an N-dimensional torus.  Both periodic and antiperiodic boundary conditions
 * are supported, as well as a variety of "twisted" boundary conditions, in
 * which the relevant complex quantity advances by some arbitrary phase (given
 * by a rational number) when one wraps around the system a single time.
 */
class BoundaryCondition
{
public:
    /**
     * Constructor.
     *
     * @param p_ specifies what fraction (of \f$2\pi\f$) the phase is increased
     * when moving once through the system in the relevant direction.  1
     * corresponds to periodic boundary conditions; 1/2 corresponds to
     * antiperiodic; etc.  0 corresponds to open boundary conditions.
     */
    explicit BoundaryCondition (const boost::rational<int> &p_)
        : m_p(p_),
          m_phase(calculate_phase(p_))
        {
            BOOST_ASSERT(p_ >= 0 && p_ <= 1);
        }

    /**
     * Uninitialized default constructor
     */
    BoundaryCondition (void)
        : m_p(-1),
          m_phase(0)
        {
        }

    /**
     * Returns a value in [0, 1]
     */
    boost::rational<int> p (void) const
        {
            BOOST_ASSERT(m_p != -1); // otherwise it is uninitialized
            return m_p;
        }

    /**
     * Returns the phase change when one crosses the boundary in the positive
     * direction.  This will be zero for open boundary conditions, or will be
     * along the unit circle for any type of periodic boundary conditions.
     */
    phase_t phase (void) const
        {
            BOOST_ASSERT(m_p != -1); // otherwise it is uninitialized
            return m_phase;
        }

    bool is_initialized (void) const
        {
            return m_p != -1;
        }

    bool operator== (const BoundaryCondition &other) const
        {
            return (m_p == other.m_p);
        }

    bool operator!= (const BoundaryCondition &other) const
        {
            return (m_p != other.m_p);
        }

private:
    /**
     * This function is called to initialize the data member m_phase during
     * object construction
     */
    static phase_t calculate_phase (const boost::rational<int> &p)
        {
            // consider open boundary conditions as a special case
            if (p == 0)
                return phase_t(0);

            // if we can return an exact value, do so
            if (p == boost::rational<int>(1))
                return phase_t(1, 0);
            else if (p == boost::rational<int>(1, 2))
                return phase_t(-1, 0);
            else if (p == boost::rational<int>(1, 4))
                return phase_t(0, 1);
            else if (p == boost::rational<int>(3, 4))
                return phase_t(0, -1);
            else
                // if not, fall back using the exponential function
                return std::exp(complex_t(0, 1) * complex_t(2 * boost::math::constants::pi<real_t>() * boost::rational_cast<real_t>(p)));
        }

    boost::rational<int> m_p;
    phase_t m_phase;
};

/** boundary conditions in each direction */
typedef lw_vector<BoundaryCondition, MAX_DIMENSION> BoundaryConditions;

// fixme: initialize these once only?
static const BoundaryCondition open_bc(boost::rational<int>(0));
static const BoundaryCondition periodic_bc(boost::rational<int>(1, 1));
static const BoundaryCondition antiperiodic_bc(boost::rational<int>(1, 2));

#endif
