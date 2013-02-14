#ifndef _RUNNING_ESTIMATE_HPP
#define _RUNNING_ESTIMATE_HPP

#ifndef BOOST_NUMERIC_FUNCTIONAL_STD_COMPLEX_SUPPORT
#define BOOST_NUMERIC_FUNCTIONAL_STD_COMPLEX_SUPPORT
#endif

#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_integral.hpp>
#include <boost/assert.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/sum.hpp>
#include <boost/accumulators/statistics/count.hpp>

#include "vmc-typedefs.hpp"

template <typename T>
class RunningEstimate
{
public:
    // define result_t, making use of the template specialization above
    typedef typename boost::numeric::functional::average<T, std::size_t>::result_type result_t;

    virtual ~RunningEstimate (void)
        {
        }

    virtual void add_value (T value)
        {
            m_recent_acc(value);
            m_cumulative_acc(value);
        }

    /**
     * Returns the average of all measurements since the most recent reset
     */
    result_t get_recent_result (void) const
        {
            BOOST_ASSERT(boost::accumulators::count(m_recent_acc) > 0);
            return boost::accumulators::mean(m_recent_acc);
        }

    /**
     * Returns the average of all measurements, regardless of whether the
     * simulation has been reset
     */
    result_t get_cumulative_result (void) const
        {
            BOOST_ASSERT(boost::accumulators::count(m_cumulative_acc) > 0);
            return boost::accumulators::mean(m_cumulative_acc);
        }

    /**
     * Returns the number of samples since the last reset
     */
    unsigned int get_num_recent_values (void) const
        {
            return boost::accumulators::count(m_recent_acc);
        }

    /**
     * Returns the cumulative number of samples in the history of this
     * estimator
     */
    unsigned int get_num_cumulative_values (void) const
        {
            return boost::accumulators::count(m_cumulative_acc);
        }

    /**
     * Resets the estimator
     */
    void reset (void)
        {
            m_recent_acc = accumulator_t();
        }

protected:
    T get_cumulative_total_value (void) const
        {
            return boost::accumulators::sum(m_cumulative_acc);
        }

private:
    typedef boost::accumulators::accumulator_set<T, boost::accumulators::stats<boost::accumulators::tag::mean, boost::accumulators::tag::sum, boost::accumulators::tag::count> > accumulator_t;

    accumulator_t m_recent_acc, m_cumulative_acc;
};

#endif
