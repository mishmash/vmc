#ifndef _BINNED_ESTIMATE_HPP
#define _BINNED_ESTIMATE_HPP

#include <vector>

#include <boost/assert.hpp>

#include "RunningEstimate.hpp"

static inline bool is_just_below_a_power_of_two (unsigned int x)
{
    return !(x & (x + 1));
}

template <typename T>
class BinnedEstimate : public RunningEstimate<T>
{
public:
    void add_value (T value)
        {
            // create a new bin-level if necessary
            if (is_just_below_a_power_of_two(this->get_num_values()))
                binlevel_data.push_back(BinnedSum(this->get_total_value()));

            RunningEstimate<T>::add_value(value);

            // perform the binning
            BOOST_ASSERT(1 << binlevel_data.size() > this->get_num_values());
            BOOST_ASSERT(1 << (binlevel_data.size() - 1) <= this->get_num_values());

            for (unsigned int i = 0; i < binlevel_data.size(); ++i)
                binlevel_data[i].current_sum += value;

            for (unsigned int i = 0; ; ++i) {
                BOOST_ASSERT(i < binlevel_data.size());
                binlevel_data[i].cumulative_sum += binlevel_data[i].current_sum;
                binlevel_data[i].cumulative_sum_squared += binlevel_data[i].current_sum * binlevel_data[i].current_sum;
                binlevel_data[i].current_sum = 0;
                if (this->get_num_values() & (1 << i))
                    break;
            }
        }

protected:
    struct BinnedSum
    {
        T current_sum, cumulative_sum, cumulative_sum_squared;

        BinnedSum (T current_sum_)
            : current_sum(current_sum_),
              cumulative_sum(0),
              cumulative_sum_squared(0)
            {
            }
    };

    std::vector<BinnedSum> binlevel_data;
};

#endif
