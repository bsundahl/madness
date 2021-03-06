
#include <madness/constants.h>
#include <madness/mra/mra.h>
#include <madness/mra/nonlinsol.h>  // The kain solver
#include <madness/mra/operator.h>
#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../chem/SCFOperators.h"
#include "../chem/molecule.h"
#include "../chem/xcfunctional.h"
#include "molresponse/TDDFT.h"
#include "molresponse/basic_operators.h"
#include "molresponse/ground_parameters.h"
#include "molresponse/load_balance.h"
#include "molresponse/property.h"
#include "molresponse/response_functions.h"
#include "molresponse/response_parameters.h"
#include "molresponse/response_potential.h"
#include "molresponse/timer.h"
#include "molresponse/x_space.h"

X_space TDDFT::Compute_Theta_X(World& world, X_space& Chi, XCOperator<double, 3> xc, std::string calc_type) {
  bool compute_Y = calc_type.compare("full") == 0;
  X_space Theta_X = X_space(world, Chi.num_states(), Chi.num_orbitals());
  // compute

  X_space V0X = compute_V0X(world, Chi, xc, compute_Y);
  V0X.truncate();

  X_space E0X = Chi.copy();
  E0X.X = E0X.X * ham_no_diag;
  if (compute_Y) {
    E0X.Y = E0X.Y * ham_no_diag;
  }

  X_space gamma;
  // compute
  if (calc_type.compare("full") == 0) {
    gamma = compute_gamma_full(world, Chi, xc);
  } else if (calc_type.compare("static") == 0) {
    gamma = compute_gamma_static(world, Chi, xc);
  } else {
    gamma = compute_gamma_tda(world, Chi, xc);
  }

  Theta_X = (V0X - E0X) + gamma;
  Theta_X.truncate();

  return Theta_X;
}
