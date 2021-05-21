// Copyright 2021 Adrian Hurtado

/// \file ResponseParameters
/// \brief Input parameters for a response calculation.

#ifndef SRC_APPS_MOLRESPONSE_RESPONSE_PARAMETERS_H_
#define SRC_APPS_MOLRESPONSE_RESPONSE_PARAMETERS_H_

#include <chem/QCCalculationParametersBase.h>
#include <chem/molecule.h>
#include <chem/xcfunctional.h>
#include <madness/mra/mra.h>
#include <madness/world/parallel_archive.h>
#include <molresponse/ground_parameters.h>

#include <functional>
#include <numeric>
#include <string>
#include <vector>
namespace madness {

struct ResponseParameters : public QCCalculationParametersBase {
  ResponseParameters(const ResponseParameters &other) = default;

  ResponseParameters() {
    initialize<std::string>("archive", "restartdata", "file to read ground parameters from");
    initialize<std::string>("nwchem", "", "Root name of nwchem files for intelligent starting guess");
    initialize<size_t>("states", 1, "Number of excited states requested");
    initialize<int>("print_level", 3, "0: no output; 1: final energy; 2: iterations; 3: timings; 10: debug");
    initialize<bool>("tda", false, "turn on Tam-Danchof approximation (excitations energy");
    initialize<bool>("plot", false, "turn on plotting of final orbitals. Output format is .vts");
    initialize<bool>("plot_range", false, "controls which orbitals will be plotted");
    initialize<std::vector<int>>("plot_data", std::vector<int>{0}, "Orbitals to plot");
    initialize<std::vector<double>>(
        "plot_cell", std::vector<double>(), "lo hi in each dimension for plotting (default is all space)");
    initialize<double>("plot_L", -1.0, "Controls the plotting box size");
    initialize<size_t>("plot_pts", 201, "Controls number of points in plots");
    initialize<bool>("plot_all_orbitals", false, "Turn on 2D plotting of response orbitals ");

    initialize<size_t>("maxiter", 25, "maximum number of iterations");

    initialize<double>("dconv", 3.e-4, "recommended values: 1.e-4 < dconv < 1.e-8");
    initialize<bool>("dconv_set", false, "Convergence flage for the orbtial density");

    initialize<bool>("guess_xyz", false, "TODO : check what this is for");

    initialize<double>("small", 1.e10, "smallest length scale we need to resolve");
    initialize<std::vector<double>>("protocol_data", {1.e-4, 1.e-6}, "calculation protocol");

    initialize<size_t>(
        "larger_subspace", 0, "Number of iterations to diagonalize in a subspace consisting of old and new vectors");
    initialize<int>("k", 7, "polynomial order");

    initialize<bool>("random", false, "Use random guess for initial response functions");
    initialize<bool>("store_potential", true, "Store the potential instead of computing each iteration");
    initialize<bool>("e_range", false, "Use an energy range to excite from");
    initialize<double>("e_range_lo", 0, "Energy range (lower end) for orbitals to excite from");
    initialize<double>("e_range_hi", 1, "Energy range (upper end) for orbitals to excite from");
    initialize<bool>("plot_initial", false, "Flag to plot the ground state orbitals read in from archivie");
    // Restart Parameters
    initialize<bool>("restart", false, "Flag to restart scf loop from file");
    initialize<std::string>("restart_file", "", "file to read ground parameters from");
    // kain
    initialize<bool>("kain", false, "Turn on Krylov Accelarated Inexact Newton Solver");
    initialize<double>("maxrotn", 1.0, "Max orbital rotation per iteration");
    initialize<size_t>("maxsub", 10, "size of iterative subspace ... set to 0 or 1 to disable");
    initialize<std::string>("xc", "hf", "XC input line");
    initialize<bool>("save", false, "if true save orbitals to disk");
    initialize<std::string>("save_file", "", "File name to save orbitals for restart");
    initialize<bool>("save_density", false, "Flag to save density at each iteration");
    initialize<std::string>("save_density_file", "", "File name to save density for restart");
    initialize<bool>("load_density", false, "Flag to load density for restart");
    initialize<std::string>("load_density_file", "", "File name to load density for restart");
    initialize<size_t>("guess_max_iter", 5, "maximum number of guess iterations");
    // properties
    initialize<bool>("property", false, "Flag to turn on frequency dependent property calc");
    initialize<std::string>("response_type", "excited_state", "dipole,nuclear,order2,order3");
    initialize<bool>("dipole", false, "Flag to turn on frequency dependent property calc");
    initialize<bool>("nuclear", false, "Flag to turn on frequency dependent property calc");
    initialize<bool>("order2", false, "Flag to turn on frequency dependent property calc");
    initialize<bool>("order3", false, "Flag to turn on frequency dependent property calc");
    initialize<std::string>("d2_types", "", "possible values are: dd nd dn nn");
    initialize<double>("omega", 0.0, "Incident energy for dynamic response");
    initialize<double>("l", 20, "user coordinates box size");
    // ground-state stuff
    initialize<size_t>("num_orbitals", 0, "number of groun_state orbtials");
    initialize<bool>("spinrestricted", true, "is spinrestricted calculation");
  }

 public:
  using QCCalculationParametersBase::read;

  std::string archive() const { return get<std::string>("archive"); }
  std::string nwchem() const { return get<std::string>("nwchem"); }
  size_t n_states() const { return get<size_t>("states"); }
  size_t num_orbitals() const { return get<size_t>("states"); }
  int print_level() const { return get<int>("print_level"); }
  bool tda() const { return get<bool>("tda"); }
  bool plot() const { return get<bool>("plot"); }
  bool plot_range() const { return get<bool>("plot_range"); }
  std::vector<int> plot_data() const { return get<std::vector<int>>("plot_data"); }
  std::vector<double> plot_cell() const { return get<std::vector<double>>("plot_cell"); }

  double plot_L() const { return get<double>("plot_L"); }
  size_t plot_pts() const { return get<size_t>("plot_pts"); }
  bool plot_all_orbitals() const { return get<bool>("plot_all_orbitals"); }
  size_t maxiter() const { return get<size_t>("maxiter"); }
  double dconv() const { return get<double>("dconv"); }
  bool dconv_set() const { return get<bool>("dconv_set"); }
  bool guess_xyz() const { return get<bool>("guess_xyz"); }
  double small() const { return get<double>("small"); }
  std::vector<double> protocol() const { return get<std::vector<double>>("protocol_data"); }
  size_t larger_subspace() const { return get<size_t>("larger_subspace"); }
  int k() const { return get<int>("k"); }
  bool random() const { return get<bool>("random"); }
  bool store_potential() const { return get<bool>("store_potential"); }
  bool e_range() const { return get<bool>("e_range"); }

  double e_range_lo() const { return get<double>("e_range_lo"); }
  double e_range_hi() const { return get<double>("e_range_hi"); }

  bool plot_initial() const { return get<bool>("plot_initial"); }
  bool restart() const { return get<bool>("restart"); }
  std::string restart_file() const { return get<std::string>("restart_file"); }
  bool kain() const { return get<bool>("kain"); }
  double maxrotn() const { return get<double>("maxrotn"); }
  size_t maxsub() const { return get<size_t>("maxsub"); }
  std::string xc() const { return get<std::string>("xc"); }
  bool save() const { return get<bool>("save"); }
  std::string save_file() const { return get<std::string>("save_file"); }
  bool save_density() const { return get<bool>("save_density"); }
  std::string save_density_file() const { return get<std::string>("save_density_file"); }
  bool load_density() const { return get<bool>("load_density"); }
  std::string load_density_file() const { return get<std::string>("load_density_file"); }
  size_t guess_max_iter() const { return get<size_t>("guess_max_iter"); }
  bool property() const { return get<bool>("property"); }
  std::string response_type() const { return get<std::string>("response_type"); }
  bool dipole() const { return get<bool>("dipole"); }
  bool nuclear() const { return get<bool>("nuclear"); }
  bool order2() const { return get<bool>("order2"); }
  bool order3() const { return get<bool>("order3"); }
  std::string d2_types() const { return get<std::string>("d2_types"); }
  double omega() const { return get<double>("omega"); }
  double L() const { return get<double>("l"); }

  bool spinrestricted() const { return get<bool>("spinrestricted"); }

  void read_and_set_derived_values(World &world, std::string inputfile, std::string tag) {
    // read the parameters from file and brodcast
    // tag
    read(world, inputfile, tag);
    GroundParameters g_params;
    std::string ground_file = archive();

    g_params.read(world, ground_file);
    g_params.print_params();
    // Ground state params
    set_derived_value<size_t>("num_orbitals", g_params.n_orbitals());
    set_derived_value<bool>("spinrestricted", g_params.is_spinrestricted());
    set_derived_value<double>("l", g_params.get_L());
    set_derived_value<int>("k", g_params.get_k());
    set_derived_value<std::string>("xc", g_params.get_xc());

    Molecule molecule = g_params.molecule();
    vector<std::string> calculation_type;
    vector<bool> calc_flags;

    if (dipole()) {
      set_derived_value<size_t>("states", 3);
      set_derived_value<std::string>("response_type","dipole");
    } else if (nuclear()) {
      set_derived_value<size_t>("states", 3 * molecule.natom());
      set_derived_value<std::string>("response_type","nuclear");
    } else if (order2()) {
      set_derived_value<std::string>("response_type","order2");
      vector<int> nstates;  // states 1
      for (size_t i = 0; i < 2; i++) {
        if (d2_types().at(i) == 'd') {
          nstates.push_back(3);
        } else if (d2_types().at(i) == 'n') {
          nstates.push_back(3 * molecule.natom());
        } else {
          MADNESS_EXCEPTION("not a valid response state ", 0);
        }
      }
      size_t states;
      states = std::accumulate(nstates.begin(), nstates.end(), 1, std::multiplies<>());
      set_derived_value<size_t>("states", states);
    } else if (order3()) {
      set_derived_value<std::string>("response_type","order3");
      vector<int> nstates;  // states 1
      for (size_t i = 0; i < 3; i++) {
        if (d2_types()[i] == 'd') {
          nstates.push_back(3);
        } else if (d2_types()[i] == 'n') {
          nstates.push_back(3 * molecule.natom());
        } else {
          MADNESS_EXCEPTION("not a valid response state ", 0);
        }
      }
      size_t states;
      states = std::accumulate(nstates.begin(), nstates.end(), 1, std::multiplies<>());
      set_derived_value<size_t>("states", states);
    }
  }

  // convenience getters
  double econv() const { return get<double>("econv"); }
  bool localize() const { return get<bool>("localize"); }
  std::string local() const { return get<std::string>("local"); }
};  // namespace madness
}  // namespace madness

#endif  // SRC_APPS_MOLRESPONSE_RESPONSE_PARAMETERS_H_
