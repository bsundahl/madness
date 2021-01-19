/*!
   \file TDHF.h
   \brief Header file for the TDHF class, which iteratively solves the linear response HF equations in the Tamm-Dancoff approximation.
   \ingroup response
   \addtogroup response

   \par Introduction

   Building on the work presented in the paper from Yanai:
      Yanai, Fann, Beylkin, Harrison; Phys. Chem. Chem. Phys., 2015, 17, 31405-31416

   Solving equation 37 from Yanai (latex formatting):
   \f$
      \~{x}_p(r) = -2[-\nabla^2 - 2(\epsilon_p^0 + \omega)]^{-1} [\hat{V}^0 x_p(r) + (1 - \hat{\rho}^0) \Gamma_p(r)]
   \f$
      with
   \f$
      \Gamma_p(r) = \{ \frac{\partial \hat{g}}{\partial \rho}[\rho^0] \times (\sum_i^{occ} x_i(r) \phi_i^\dagger(r'))\} \phi_p(r)
   \f$
 
   He lists 12 steps to solve these equations:
      1.  Obtain ground state orbitals {\phi_p} and energies {\epsilon_p}
      2.  Compute a representation of \frac{ \partial^2 E_{xc}}{\partial \rho^2}[\rho^0]	
      3.  Create guess response functions
    
    [ 4.  Compute transition density (sum of products of occupied orbitals with guess response functions)
    [ 5.  Obtain \f$\Gamma_p(r)\f$ for current density
    [ 6.  Compute \f$\hat{V}^0 x_p^{(k)}\f$ (does contain HF potential, but rest is static as its the ground state values)
    [ 7.  Obtain initial eigenvalues \f$\omega^k\f$ from a matrix diagonalization of
    [     \f$   A x = S x \omega \f$
    [     where S is the overlap matrix of the response functions, and A has the form
    [     \f$   A_{ij} = \sum_p \int dr x_p^{(i}}(1 - \hat{\rho}^0)[(\hat{F}^0 - \epsilon_p^0) x_p^{(j)}(r) +
    [                 \Gamma_p^{(j)}(r) \phi_p(r)] \f$
    [     \f$   S_{ij} = \sum_p \int dr x_p^{(i)}(r) x_p^{(j)}(r)\f$
    [ 8.  Rotate the gamma and potential functions according to eigenvectors of the Hamiltonian.
    [ 9.  Apply BSH integral operator to the integral equations (eq. 37)
      10. Repeat steps 4-9 until the residual is within your tolerance
*/

#ifndef MADNESS_APPS_TDHF_H_INCLUDED
#define MADNESS_APPS_TDHF_H_INCLUDED

#include <madness/mra/mra.h>
#include <madness/mra/operator.h>
#include <madness/constants.h>
#include <vector>
#include <math.h>
#include <stdio.h>
#include <iomanip>
#include <complex>
#include <cmath>
#include <random>
#include <algorithm> 
#include "../chem/molecule.h"
#include "ResponseParameters.h"
#include "GroundParameters.h"


using namespace madness;

// Functor from SCF.cc (it wasn't linking right, no idea why, so just copied and renamed here)
// A copy of a MADNESS functor to compute the cartesian moment x^i * y^j * z^k (i, j, k integer and >= 0)
class BS_MomentFunctor : public FunctionFunctorInterface<double,3> {
private:
    const int i, j, k;
public:
    BS_MomentFunctor(int i, int j, int k) : i(i), j(j), k(k) {}
    BS_MomentFunctor(const std::vector<int>& x) : i(x[0]), j(x[1]), k(x[2]) {}
    double operator()(const Vector<double,3>& r) const {
        double xi=1.0, yj=1.0, zk=1.0;
        for (int p=0; p<i; ++p) xi *= r[0];
        for (int p=0; p<j; ++p) yj *= r[1];
        for (int p=0; p<k; ++p) zk *= r[2];
        return xi*yj*zk;
    }
};

/// an N-dimensional real-valued Gaussian function

/// the function looks like
/// \[
/// f(r) = x^i y^j .. z^k exp(-alpha r^2)
/// \]
template<std::size_t NDIM>
class GaussianGuess : public FunctionFunctorInterface<double,NDIM> {
    typedef Vector<double,NDIM> coordT;

public:

    /// ctor

    /// @param[in]  origin  the origin of the Gauss function
    /// @param[in]  alpha   the exponent exp(-alpha r^2)
    /// @param[in]  ijk     the monomial x^i y^j z^k exp(-alpha r^2) (for NDIM)
    GaussianGuess(const coordT& origin, const double alpha,
            const std::vector<int> ijk=std::vector<int>(NDIM))
            : origin(origin), exponent(alpha), ijk(ijk) {
    }

    coordT origin;
    double exponent;        ///< exponent of the guess
    std::vector<int> ijk;   ///< cartesian exponents

    double operator()(const coordT& xyz) const {
        double arg=0.0, prefac=1.0;
        for (std::size_t i=0; i<NDIM;++i) {
            arg+=(xyz[i]-origin[i])*(xyz[i]-origin[i]);
            prefac*=pow(xyz[i],ijk[i]);
        }
        const double e=exponent*arg;
        return prefac*exp(-e);
    }
};





/// Given a molecule and ground state orbitals, solve the response equations
/// in the Tamm-Danchoff approximation.
class TDHF 
{
   private:
      // Member variables

      // ResponseParameter object to hold all user input variables
      ResponseParameters Rparams;

      // GroundParameter object to hold all variables needed from
      // ground state calculation. Read from an archive
      GroundParameters Gparams;

      // Tensors for holding energies 
      // residuals, and shifts
      Tensor<double> x_omega;        // Energies of response functions
      Tensor<double> y_omega;        // Energies of response functions
      Tensor<double> x_e_residuals;  // Residuals of energies
      Tensor<double> y_e_residuals;  // Residuals of energies

      // Information that is inferred from input file
      std::vector<real_function_3d> act_orbitals;     // Ground state orbitals being used in calculation
      Tensor<double> act_ground_energies;             // Ground state energies being used for calculation
      Tensor<double> hamiltonian;                     // Ground state hamiltonian tensor 
      Tensor<double> ham_no_diag;                     // Ground state ham. without diagonal (Used when localized orbitals are given)
      std::vector<int> active;                        // The labels of orbitals selected as "active"
      unsigned int act_num_orbitals;                  // Number of ground state orbitals being used in calculation

      // Mask function to handle boundary conditions
      real_function_3d mask;

      // Functions
      std::vector<std::vector<real_function_3d>> x_response;   // Excited states to be solved for. 
                                                               //    Note on storage: The response functions are calculated
                                                               //    by calculating each transition of occupied --> virtual,
                                                               //    and thus the actual response function is a sum of of all
                                                               //    contributions to a specific virtual.
       
      std::vector<std::vector<real_function_3d>> y_response;   // De-excitation states to be solved for. 
                                                               //    Note on storage: The response functions are calculated
                                                               //    by calculating each transition of occupied --> virtual,
                                                               //    and thus the actual response function is a sum of of all
                                                               //    contributions to a specific virtual.
                                                                   
      std::vector<std::vector<real_function_3d>> stored_potential;   // The ground state potential, stored only if store_potential
                                                                     // is true (default is false). Holds the integrals 
                                                                     //   \int dr \frac{\phi_i^\dagger phi_j}{\left| r - r' \right|}

   public:

      // Collective constructor for response uses contents of file \c filename and broadcasts to all nodes
      TDHF(World & world,            // MADNESS world object
          const char* input_file);  // Input file 

      // Collective constructor for Response uses contens of steream \c input and broadcasts to all nodes
      TDHF(World & world,                       // MADNESS world object
          std::shared_ptr<std::istream> input); // Pointer to input stream

      // Saves a response calculation
      void save(World & world);

      // Loads a response calculation
      //void load(World & world,
      //          std::string archive);

      // Normalizes in the response sense
      void normalize(World & world,
                     std::vector<std::vector<real_function_3d>> & f);

      // Prints norms of the given vector 
      void print_norms(World & world,
                       std::vector<std::vector<real_function_3d>> function);

      // Prints molecule geometry
      void print_molecule(World & world);

      // Returns a set of vector of vector of real_function_3d of proper size, initialized to zero
      std::vector<std::vector<real_function_3d>> response_zero_functions(World & world,
                                                                         int m,
                                                                         int n);

      // Returns a list of symmetry related functions for correct
      // pointgroup of the provided molecule
      std::vector<real_function_3d> symmetry(World & world);

      // Returns initial response functions
      std::vector<std::vector<real_function_3d>> create_trial_functions(World & world,
                                                                        int k,
                                                                        std::vector<real_function_3d> & orbitals,
                                                                        int print_level);

      // Returns the derivative of the coulomb operator, applied to ground state orbitals
      std::vector<std::vector<real_function_3d>> create_coulomb_derivative(World & world,
                                                                           std::vector<std::vector<real_function_3d>> & f,
                                                                           std::vector<real_function_3d> & orbitals,
                                                                           double small,
                                                                           double thresh);

      // Returns the derivative of the exchange operator, applied to the ground state orbitals
      // This is the function for TDA only
      std::vector<std::vector<real_function_3d>> create_exchange_derivative(World & world,
                                                                            std::vector<std::vector<real_function_3d>> & f,
                                                                            std::vector<real_function_3d> & orbitals,
                                                                            double small,
                                                                            double thresh);

      // Returns the off diagonal (letter B) elements of response matrix       
      std::vector<std::vector<real_function_3d>> create_B(World & world,
                                                          std::vector<std::vector<real_function_3d>> & f,
                                                          std::vector<real_function_3d> & orbitals,
                                                          double small,
                                                          double thresh);

      // Returns gamma (the perturbed 2 electron piece) 
      std::vector<std::vector<real_function_3d>> create_gamma(World & world,
                                                              std::vector<std::vector<real_function_3d>> & f,
                                                              std::vector<real_function_3d> & orbitals,
                                                              double small,
                                                              double thresh,
                                                              int print_level,
                                                              std::string xy);

      // Returns the coulomb potential of the ground state
      // Note: No post multiplication involved here
      real_function_3d coulomb(World & world);

      // Returns the result of ground state exchange applied to response functions
      std::vector<std::vector<real_function_3d>> exchange(World & world,
                                                          std::vector<std::vector<real_function_3d>> & f);

      // Returns the ground state potential applied to response functions
      std::vector<std::vector<real_function_3d>> create_potential(World & world,
                                                                  std::vector<std::vector<real_function_3d>> & f,
                                                                  int print_level,
                                                                  std::string xy);

      // Returns a tensor, where entry (i,j) = inner(a[i], b[j]).sum()
      Tensor<double> expectation(World & world,
                                 std::vector<std::vector<real_function_3d>> & a,
                                 std::vector<std::vector<real_function_3d>> & b);

      // Returns the overlap matrix of the given response functions
      Tensor<double> create_overlap(World & world,
                                    std::vector<std::vector<real_function_3d>> & f,
                                    std::vector<std::vector<real_function_3d>> & g,
                                    int print_level,
                                    std::string xy);

      // Returns the ground state fock operator applied to response functions
      std::vector<std::vector<real_function_3d>> create_fock(World & world,
                                                             std::vector<std::vector<real_function_3d>> & V,
                                                             std::vector<std::vector<real_function_3d>> & f,
                                                             int print_level,
                                                             std::string xy);

      // Returns the hamiltonian matrix, equation 45 from the paper
      Tensor<double> create_response_matrix(World & world,
                                            std::vector<std::vector<real_function_3d>> & fe,
                                            std::vector<std::vector<real_function_3d>> & gamma,
                                            std::vector<std::vector<real_function_3d>> & V,
                                            std::vector<std::vector<real_function_3d>> & f,
                                            std::vector<real_function_3d> & ground_orbitals,
                                            Tensor<double> & energies,
                                            int print_level,
                                            std::string xy);

      // Constructs full response matrix of
      // [ A  B ] [ X ] = w [ X ]
      // [-B -A ] [ Y ]     [ Y ]
      Tensor<double> create_full_response_matrix(World & world, 
                                                 std::vector<std::vector<real_function_3d>> x_b,
                                                 std::vector<std::vector<real_function_3d>> Vx,
                                                 std::vector<std::vector<real_function_3d>> x,
                                                 std::vector<std::vector<real_function_3d>> y_b,
                                                 std::vector<std::vector<real_function_3d>> Vy,
                                                 std::vector<std::vector<real_function_3d>> y,
                                                 std::vector<real_function_3d> ground_orbitals,
                                                 Tensor<double> ground_ham,
                                                 double small,
                                                 double thresh,
                                                 int print_level);

      // Returns the shift needed for each orbital to make sure
      // -2.0 * (ground_state_energy + excited_state_energy) is positive
      Tensor<double> create_shift(World & world,
                                  Tensor<double> & ground,
                                  Tensor<double> & omega,
                                  int print_level,
                                  std::string xy);

      // Returns the given shift applied to the given potentials
      std::vector<std::vector<real_function_3d>> apply_shift(World & world,
                                                             Tensor<double> & shifts,
                                                             std::vector<std::vector<real_function_3d>> & V,
                                                             std::vector<std::vector<real_function_3d>> & f);


      // Returns a vector of BSH operators
      std::vector<std::vector<std::shared_ptr<real_convolution_3d>>> create_bsh_operators(World & world,
                                                                                          Tensor<double> & shift,
                                                                                          Tensor<double> & ground,
                                                                                          Tensor<double> & omega,
                                                                                          double small,
                                                                                          double thresh);

      // Returns the second order update to the energy
      Tensor<double> calculate_energy_update(World & world,
                                             std::vector<std::vector<real_function_3d>> & gamma,
                                             std::vector<std::vector<real_function_3d>> & f_residuals,
                                             std::vector<std::vector<real_function_3d>> & new_f,
                                             int print_level,
                                             std::string xy);

      // Returns response functions that have been orthonormalized via
      // modified Gram-Schmidt. Note: This is specifically designed for
      // response functions only
      std::vector<std::vector<real_function_3d>> gram_schmidt(World & world,
                                                              std::vector<std::vector<real_function_3d>> & f);

      // Returns the max norm of the given vector of functions
      double calculate_max_residual(World & world,
                                    std::vector<std::vector<real_function_3d>> & f);

      // Selects the 'active' orbitals from ground state orbitals to be used in the calculation (based on energy distance
      // from the HOMO.) Function needs knowledge of Gparams.orbitals and Gparams.ground_energies. Function sets act_orbitals
      // and num_act_orbitals.
      void select_active_subspace(World & world);

      // Selects from a list of functions and energies the k functions with the lowest 
      // energy
      std::vector<std::vector<real_function_3d>> select_functions(World & world,
                                                                  std::vector<std::vector<real_function_3d>> & f,
                                                                  Tensor<double> & energies,
                                                                  int k,
                                                                  int print_level);

      // Calculates the exponentiation of a matrix through first order (I think)
      Tensor<double> matrix_exponential(const Tensor<double> & A);

      // Computes the unitary transformation that diagonalizes the fock matrix
      Tensor<double> get_fock_transformation(World & world,
                                             Tensor<double> & overlap, 
                                             Tensor<double> & fock,
                                             Tensor<double> & evals,
                                             const double thresh_degenerate);

      // Diagonalizes the fock matrix, taking care of degerate states
      Tensor<int> diag_fock_matrix(World & world,
                                   Tensor<double> & fock,
                                   std::vector<std::vector<real_function_3d>> & psi,
                                   std::vector<std::vector<real_function_3d>> & Vpsi,
                                   std::vector<std::vector<real_function_3d>> & gamma,
                                   std::vector<std::vector<real_function_3d>> & fe,
                                   Tensor<double> & evals,
                                   Tensor<double> & overlap,
                                   const double thresh);

      // Transforms the given matrix of functions according to the given
      // transformation matrix. Used to update orbitals / potentials
      std::vector<std::vector<real_function_3d>> transform(World & world,
                                                           std::vector<std::vector<real_function_3d>> & f,
                                                           Tensor<double> & U);

      // If using a larger subspace to diagonalize in, this will put everything in the right spot
      void augment(World & world,
                   Tensor<double> & S_x,     
                   Tensor<double> & A_x,     
                   std::vector<std::vector<real_function_3d>> & x_gamma,
                   std::vector<std::vector<real_function_3d>> & x_response,
                   std::vector<std::vector<real_function_3d>> & V_x_response,
                   std::vector<std::vector<real_function_3d>> & x_fe, 
                   Tensor<double> & old_S_x, 
                   Tensor<double> & old_A_x, 
                   std::vector<std::vector<real_function_3d>> & old_x_gamma, 
                   std::vector<std::vector<real_function_3d>> & old_x_resopnse, 
                   std::vector<std::vector<real_function_3d>> & old_V_x_response, 
                   std::vector<std::vector<real_function_3d>> & old_x_fe,
                   int print_level);

      // If using a larger subspace to diagonalize in, after diagonalization this will put everything in the right spot
      void unaugment(World & world,
                     int m,
                     int iter,
                     Tensor<int> & selected,
                     Tensor<double> & x_omega,
                     Tensor<double> & S_x,     
                     Tensor<double> & A_x,     
                     std::vector<std::vector<real_function_3d>> & x_gamma,
                     std::vector<std::vector<real_function_3d>> & x_response,
                     std::vector<std::vector<real_function_3d>> & V_x_response,
                     std::vector<std::vector<real_function_3d>> & x_fe,
                     Tensor<double> & old_S_x, 
                     Tensor<double> & old_A_x, 
                     std::vector<std::vector<real_function_3d>> & old_x_gamma, 
                     std::vector<std::vector<real_function_3d>> & old_x_resopnse, 
                     std::vector<std::vector<real_function_3d>> & old_V_x_response, 
                     std::vector<std::vector<real_function_3d>> & old_x_fe,
                     int print_level);

      // Diagonalize the full response matrix, taking care of degenerate states
      Tensor<double> diag_full_response(World & world,
                                        Tensor<double> & full_response,
                                        std::vector<std::vector<real_function_3d>> & x,
                                        std::vector<std::vector<real_function_3d>> & Vx,
                                        std::vector<std::vector<real_function_3d>> & x_g,
                                        std::vector<std::vector<real_function_3d>> & y,
                                        std::vector<std::vector<real_function_3d>> & Vy,
                                        std::vector<std::vector<real_function_3d>> & y_g,
                                        Tensor<double> & x_evals, 
                                        Tensor<double> & y_evals,
                                        const double thresh,
                                        int print_level);

      // Similar to what robert did above in "get_fock_transformation"
      Tensor<double> get_full_response_transformation(World& world,
                                                      Tensor<double>& overlap,
                                                      Tensor<double>& full_response,
                                                      Tensor<double>& evals,
                                                      const double thresh)   ; 

      // Sorts the given Tensor and vector of functions in place
      Tensor<int> sort(World & world,
                       Tensor<double> & vals,
                       Tensor<double> & vals_residuals,
                       std::vector<std::vector<real_function_3d>> & f,
                       Tensor<double> & f_diff);

      // Sorts the given Tensors 
      Tensor<int> sort_eigenvalues(World & world,
                                   Tensor<double> & vals,
                                   Tensor<double> & vecs);

      // Iterates the trial functions until covergence or it runs out of iterations
      void iterate(World & world);

      // Constructs and prints a more detailed analysis of response functions
      // Uses member variables
      void analysis(World & world);

      // Diagonalizes the given functions
      void diagonalize_guess(World & world,
                             std::vector<std::vector<real_function_3d>> & f,
                             Tensor<double> & omega,
                             std::vector<real_function_3d> & orbitals,
                             Tensor<double> & energies,
                             double thresh,
                             double small,
                             int print_level,
                             std::string xy);

      // Adds random noise to function f
      std::vector<std::vector<real_function_3d>> add_randomness(World & world,
                                                                std::vector<std::vector<real_function_3d>> & f);

      // Creates the transition density
      std::vector<real_function_3d> transition_density(World & world);

      // Creates the ground state hamiltonian for the orbitals in the active subspace
      // (aka the orbitals in tda_act_orbitals) 
      void create_ground_hamiltonian(World &world,
                                     std::vector<real_function_3d> f,
                                     int print_level);

      // Sets the different k/thresh levels
      template<std::size_t NDIM>
      void set_protocol(World & world, double thresh);

      // Verifies that correct order of polynomial is in use for all
      void check_k(World & world,
                   double thresh);

      // Creates random guess functions semi-intelligently(?)
      std::vector<std::vector<real_function_3d>> create_random_guess(World & world, 
                                                                     int m, 
                                                                     int n,
                                                                     std::vector<real_function_3d> & grounds,
                                                                     Molecule & molecule);

      // Creates an initial guess using NWChem outputs from a ground state calculation
      // Requires:
      //    1. nwchem output file (named as "base_name.out")
      //    2. nwchem movecs file (named as "base_name.movecs")
      std::vector<std::vector<real_function_3d>> create_nwchem_guess(World & world,
                                                                     int m);

      // Solves the response equations
      void solve(World & world);

};


#endif

// Deuces