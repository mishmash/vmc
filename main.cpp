#include <iostream>
#include <vector>
#include <list>
#include <memory>
#include <exception>
#include <cstring>

#include <json/json.h>
#include <boost/array.hpp>
#include <boost/assert.hpp>
#include <boost/cast.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

#include "MetropolisSimulation.hpp"
#include "StandardWalk.hpp"
#include "RenyiModMeasurement.hpp"
#include "RenyiModWalk.hpp"
#include "RenyiSignMeasurement.hpp"
#include "RenyiSignWalk.hpp"
#include "DensityDensityMeasurement.hpp"
#include "FilledOrbitals.hpp"
#include "SimpleSubsystem.hpp"
#include "HypercubicLattice.hpp"
#include "FreeFermionWavefunctionAmplitude.hpp"
#include "PositionArguments.hpp"
#include "random-combination.hpp"

class ParseError : public std::exception
{
public:
    ParseError (void)
        : error_message(default_error_message)
        {
        }

    ParseError (const char *error_message_)
        : error_message(error_message_)
        {
        }

    virtual const char * what (void) const throw()
        {
            return error_message;
        }

private:
    static const char *default_error_message;
    const char *error_message;
};

const char *ParseError::default_error_message = "json input error";

static inline void ensure_object (const Json::Value &jsonvalue)
{
    if (!jsonvalue.isObject())
        throw ParseError("object expected");
}

static inline void ensure_array (const Json::Value &jsonvalue)
{
    if (!jsonvalue.isArray())
        throw ParseError("array expected");
}

static void ensure_array (const Json::Value &jsonvalue, unsigned int array_length)
{
    if (!jsonvalue.isArray())
        throw ParseError("array expected");
    if (jsonvalue.size() != array_length)
        throw ParseError("array is not the correct size");
}

static inline void ensure_string (const Json::Value &jsonvalue)
{
    if (!jsonvalue.isString())
        throw ParseError("string expected");
}

static void ensure_required (const Json::Value &jsonvalue, const char * const keys[])
{
    BOOST_ASSERT(jsonvalue.isObject());
    for (const char * const *key = keys; *key != NULL; ++key) {
        if (!jsonvalue.isMember(*key))
            throw ParseError("required keys not all given");
    }
}

static void ensure_only (const Json::Value &jsonvalue, const char * const keys[])
{
    BOOST_ASSERT(jsonvalue.isObject());
    for (Json::Value::const_iterator i = jsonvalue.begin(), e = jsonvalue.end(); i != e; ++i) {
        const char * const *key = keys;
        for (; *key != NULL; ++key) {
            if (strcmp(i.memberName(), *key) == 0)
                break;
        }
        if (key == NULL)
            throw ParseError("too many keys provided");
    }
}

template <unsigned int DIM>
boost::shared_ptr<const OrbitalDefinitions> parse_json_orbitals (const Json::Value &json_orbitals, const boost::shared_ptr<const NDLattice<DIM> > &lattice)
{
    const char * const json_orbitals_required[] = { "filling", "boundary-conditions", NULL };
    ensure_required(json_orbitals, json_orbitals_required);
    ensure_only(json_orbitals, json_orbitals_required);

    // set up the boundary conditions
    const Json::Value &json_bcs = json_orbitals["boundary-conditions"];
    ensure_array(json_bcs, DIM);
    typename NDLattice<DIM>::BoundaryConditions boundary_conditions;
    for (unsigned int i = 0; i < DIM; ++i) {
        if (!(json_bcs[i].isIntegral() && json_bcs[i].asInt() > 0))
            throw ParseError("invalid boundary condition specifier");
        boundary_conditions[i] = BoundaryCondition(json_bcs[i].asUInt());
    }

    // set up the orbitals' filled momenta
    const Json::Value &json_filling = json_orbitals["filling"];
    ensure_array(json_filling);
    std::vector<boost::array<int, DIM> > filled_momenta;
    filled_momenta.reserve(json_filling.size());
    for (unsigned int i = 0; i < json_filling.size(); ++i) {
        const Json::Value &json_current_filling = json_filling[i];
        ensure_array(json_current_filling, DIM);
        boost::array<int, DIM> current_filling;
        for (unsigned int j = 0; j < DIM; ++j) {
            if (!(json_current_filling[j].isIntegral() && json_current_filling[j].asInt() >= 0 && json_current_filling[j].asInt() < lattice->length[j]))
                throw ParseError("invalid momentum index");
            current_filling[j] = json_current_filling[j].asInt();
        }
        filled_momenta.push_back(current_filling);
    }

    return boost::make_shared<FilledOrbitals<DIM> >(filled_momenta, lattice, boundary_conditions);
}

static inline double jsoncpp_real_cast (real_t v)
{
    // it would be really nice if jsoncpp supported "long double" directly...
    return v;
}

static Json::Value complex_to_json_array (const complex_t &v)
{
    Json::Value rv(Json::arrayValue);
    rv.append(Json::Value(jsoncpp_real_cast(std::real(v))));
    rv.append(Json::Value(jsoncpp_real_cast(std::imag(v))));
    return rv;
}

static Json::Value renyi_mod_measurement_json_repr (const RenyiModMeasurement &measurement)
{
    return Json::Value(measurement.get());
}

static Json::Value renyi_sign_measurement_json_repr (const RenyiSignMeasurement &measurement)
{
    return complex_to_json_array(measurement.get());
}

template<unsigned int DIM>
static Json::Value density_density_measurement_json_repr (const DensityDensityMeasurement<DIM> &measurement)
{
    Json::Value rv(Json::arrayValue);
    for (unsigned int i = 0; i < measurement.basis_indices(); ++i) {
        Json::Value a(Json::arrayValue);
        for (unsigned int j = 0; j < measurement.get_N_sites(); ++j)
            a.append(Json::Value(jsoncpp_real_cast(measurement.get(j, i))));
        rv.append(a);
    }
    return rv;
}

template <unsigned int DIM>
static int do_simulation (const Json::Value &json_input, rng_class &rng);

int main ()
{
    // take json input and perform a simulation

    Json::Value json_input;
    std::cin >> json_input;

    try {
        ensure_object(json_input);
        const char * const json_input_required[] = { "rng", "system", NULL };
        ensure_required(json_input, json_input_required);
        ensure_only(json_input, json_input_required);

        // initialize random number generator
        unsigned int seed;
        const Json::Value &json_rng = json_input["rng"];
        ensure_object(json_rng);
        const char * const json_rng_allowed[] = { "seed", NULL };
        ensure_only(json_rng, json_rng_allowed);
        if (json_rng.isMember("seed")) {
            if (!json_rng["seed"].isIntegral())
                throw ParseError("seed must be correct data type");
            seed = json_rng["seed"].asUInt();
        } else {
            throw ParseError("seed must be given");
        }
        rng_class rng(seed);

        // begin setting up the physical system
        const Json::Value &json_system = json_input["system"];
        ensure_object(json_system);
        const char * const json_system_required[] = { "lattice", "wavefunction", NULL };
        ensure_required(json_system, json_system_required);
        ensure_only(json_system, json_system_required);

        // begin setting up the lattice
        const Json::Value &json_lattice = json_system["lattice"];
        ensure_object(json_lattice);
        const char * const json_lattice_required[] = { "size", NULL };
        ensure_required(json_lattice, json_lattice_required);
        ensure_only(json_lattice, json_lattice_required);

        // determine the lattice size/dimension
        const Json::Value &json_lattice_size = json_lattice["size"];
        ensure_array(json_lattice_size);
        unsigned int ndimensions = json_lattice_size.size();
        for (unsigned int i = 0; i < ndimensions; ++i) {
            if (!(json_lattice_size[i].isIntegral() && json_lattice_size[i].asInt() > 0))
                throw ParseError("lattice dimensions must be positive integers");
        }

        // dispatch the remainder of the simulation based on the number of
        // dimensions in the system
        switch (ndimensions) {
        case 1:
            return do_simulation<1>(json_input, rng);
        case 2:
            return do_simulation<2>(json_input, rng);
        default:
            throw ParseError("lattice given has a number of dimensions that is not supported by this build");
        }
    } catch (ParseError e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}

template <unsigned int DIM>
static int do_simulation (const Json::Value &json_input, rng_class &rng)
{
    // finish setting up the lattice
    const Json::Value &json_lattice_size = json_input["system"]["lattice"]["size"];
    boost::array<int, DIM> lattice_size_array;
    for (unsigned int i = 0; i < DIM; ++i)
        lattice_size_array[i] = json_lattice_size[i].asInt();
    const boost::shared_ptr<const NDLattice<DIM> > lattice(new NDLattice<DIM>(lattice_size_array));

    // set up the wavefunction
    boost::shared_ptr<WavefunctionAmplitude> wf;
    const Json::Value &json_wavefunction = json_input["system"]["wavefunction"];
    ensure_object(json_wavefunction);
    const char * const json_wavefunction_required[] = { "type", NULL };
    ensure_required(json_wavefunction, json_wavefunction_required);
    ensure_string(json_wavefunction["type"]);
    const char *json_wavefunction_type_cstr = json_wavefunction["type"].asCString();
    if (strcmp(json_wavefunction_type_cstr, "free-fermion") == 0) {
        // free fermion wavefunction
        const char * const json_free_fermion_wavefunction_required[] = { "type", "orbitals", NULL };
        ensure_required(json_wavefunction, json_free_fermion_wavefunction_required);
        ensure_only(json_wavefunction, json_free_fermion_wavefunction_required);
        boost::shared_ptr<const OrbitalDefinitions> orbitals = parse_json_orbitals<DIM>(json_wavefunction["orbitals"], lattice);
        /* begin fixme */
        std::vector<unsigned int> v;
        random_combination(v, orbitals->get_N_filled(), lattice->total_sites(), rng);
        PositionArguments r(v, lattice->total_sites());
        /* end fixme */
        wf.reset(new FreeFermionWavefunctionAmplitude(r, orbitals));
    } else {
        throw ParseError("invalid wavefunction type");
    }

    // example API usage

    StandardWalk walk(wf);
    boost::shared_ptr<DensityDensityMeasurement<DIM> > density_measurement(new DensityDensityMeasurement<DIM>);
    MetropolisSimulation<StandardWalk> sim(walk, density_measurement, 8, rng());

    std::list<boost::shared_ptr<Measurement<RenyiModWalk> > > mod_measurements;
    mod_measurements.push_back(boost::make_shared<RenyiModMeasurement>(boost::make_shared<SimpleSubsystem<DIM> >(2)));

    RenyiModWalk mod_walk(wf, rng);
    MetropolisSimulation<RenyiModWalk> mod_sim(mod_walk, mod_measurements, 8, rng());

    boost::shared_ptr<Subsystem> subsystem(new SimpleSubsystem<DIM>(2));
    RenyiSignWalk sign_walk(wf, subsystem, rng);
    boost::shared_ptr<RenyiSignMeasurement> sign_measurement(new RenyiSignMeasurement);
    MetropolisSimulation<RenyiSignWalk> sign_sim(sign_walk, sign_measurement, 8, rng());

    for (unsigned int i = 0; i < 100; ++i) {
        sim.iterate(12);
        std::cout << density_density_measurement_json_repr<DIM>(*density_measurement) << std::endl;
        std::cerr << "density-density " << (100.0 * sim.steps_accepted() / sim.steps_completed()) << "%\t";
        for (unsigned int i = 0; i < lattice->total_sites(); ++i)
            std::cerr << "  " << density_measurement->get(i);
        std::cerr << std::endl;

        mod_sim.iterate(12);
        std::cout << renyi_mod_measurement_json_repr(*boost::polymorphic_downcast<RenyiModMeasurement *>(&**mod_measurements.begin())) << std::endl;
        std::cerr << "swap,mod " << (100.0 * mod_sim.steps_accepted() / mod_sim.steps_completed()) << "%\t" << double(boost::polymorphic_downcast<RenyiModMeasurement *>(&**mod_measurements.begin())->get()) << std::endl;

        sign_sim.iterate(12);
        std::cout << renyi_sign_measurement_json_repr(*sign_measurement) << std::endl;
        std::cerr << "swap,sign " << (100.0 * sign_sim.steps_accepted() / sign_sim.steps_completed()) << "%\t" << sign_measurement->get() << std::endl;
    }

    return 0;
}
