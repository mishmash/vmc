#include <set>

#include <boost/assert.hpp>

#include "RandomNumberGenerator.hpp"
#include "random-combination.hpp"

// http://stackoverflow.com/questions/2394246/algorithm-to-select-a-single-random-combination-of-values
void random_combination (std::vector<unsigned int> &v, unsigned int r, unsigned int n, RandomNumberGenerator &rng, unsigned int keep)
{
    // per Jon Bentley's article in CACM, September 1987, Volume 30, Number 9
    BOOST_ASSERT(n > 0);
    BOOST_ASSERT(r > 0);
    BOOST_ASSERT(r <= n);
    BOOST_ASSERT(keep <= n);
    BOOST_ASSERT(v.size() >= keep);

    if (n == r && keep == 0) {
        // the loop below fails if k == 0 is ever true, so here we handle the
        // only special case that could cause that
        v.resize(r);
        for (unsigned int i = 0; i < r; ++i)
            v[i] = i;
        return;
    }

    std::set<int> vs;
    v.resize(keep);
    v.reserve(r);
    for (std::vector<unsigned int>::const_iterator i = v.begin(); i != v.end(); ++i)
        vs.insert(*i);
    BOOST_ASSERT(v.size() == vs.size());

    for (unsigned int k = n - r + keep; k < n; ++k) {
        BOOST_ASSERT(k > 0);
        unsigned int x = rng.random_small_uint(k);
        unsigned int a = (vs.find(x) != vs.end()) ? k : x;
        v.push_back(a);
        vs.insert(a);
    }

    BOOST_ASSERT(v.size() == r);
}
