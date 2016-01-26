/*
 * CCOperators.cc
 *
 *  Created on: Jul 6, 2015
 *      Author: kottmanj
 */


#include "CCOperators.h"

#include <cmath>
#include "../../madness/constants.h"
#include "../../madness/mra/derivative.h"
#include "../../madness/mra/funcdefaults.h"
#include "../../madness/mra/funcimpl.h"
#include "../../madness/mra/funcplot.h"
#include "../../madness/mra/function_factory.h"
#include "../../madness/mra/functypedefs.h"
#include "../../madness/mra/mra.h"
#include "../../madness/mra/operator.h"
#include "../../madness/mra/vmra.h"
#include "../../madness/tensor/srconf.h"
#include "../../madness/tensor/tensor.h"
#include "../../madness/world/madness_exception.h"
#include "../../madness/world/parallel_archive.h"
#include "../../madness/world/print.h"
#include "../../madness/world/world.h"
#include "electronic_correlation_factor.h"
#include "TDA.h"

namespace madness{

/// save a function
template<typename T, size_t NDIM>
void CC_Operators::save_function(const Function<T, NDIM>& f,
		const std::string name) const {
	if (world.rank() == 0)
		print("saving function", name);
	f.print_size(name);
	archive::ParallelOutputArchive ar(world, name.c_str(), 1);
	ar & f;
}

real_function_3d CC_Intermediates::make_density(const CC_vecfunction &bra,
		const CC_vecfunction &ket) const {
	if (bra.size()==0) error("error in make_density: bra_element is empty");
	if (ket.size()==0) error("error in make_density: ket_element is empty");
	// make the density
	real_function_3d density = real_factory_3d(world);
	for (auto x:ket.functions){

		density += (bra(x.first).function * x.second.function);
	}
	density.truncate(FunctionDefaults<3>::get_thresh()*0.01);
	return density;
}

intermediateT CC_Intermediates::make_exchange_intermediate(const CC_vecfunction &bra,
		const CC_vecfunction &ket) const {
	intermediateT xim;
	for(auto tmpk:bra.functions){
		const CC_function & k = tmpk.second;
		for(auto tmpl:ket.functions){
			const CC_function& l=tmpl.second;
			real_function_3d kl = (bra(k).function*l.function);
			real_function_3d result = ((*poisson)(kl)).truncate();
			xim.insert(k.i,l.i,result);
		}
	}
	return xim;
}

intermediateT CC_Intermediates::make_f12_exchange_intermediate(const CC_vecfunction &bra,
		const CC_vecfunction &ket)const{
	intermediateT xim;
	for(auto tmpk:bra.functions){
		const CC_function & k = tmpk.second;
		for(auto tmpl:ket.functions){
			CC_function l=tmpl.second;
			real_function_3d kl = (bra(k).function*l.function);
			real_function_3d result = ((*f12op)(kl)).truncate();
			xim.insert(k.i,l.i,result);
		}
	}
	return xim;
}

real_function_6d CC_Operators::make_cc2_coulomb_parts(const CC_function &taui, const CC_function &tauj, const CC_vecfunction &singles) const {
	const CC_function ti = make_t_intermediate(taui);
	const CC_function tj = make_t_intermediate(tauj);
	real_convolution_6d G = BSHOperator<6>(world, sqrt(-2.0 * get_epsilon(taui.i,tauj.i)),parameters.lo, parameters.thresh_bsh_6D);
	G.destructive()=true;
	// first do the O1 and O2 parts which are
	// Otau1(g|titj) = |tauk><k|(1)g|titj> = kgti(2)|tauktj>
	// same for Otau2 = kgtj(1)|titauk>
	real_function_6d G_O1tau_part = real_factory_6d(world);
	real_function_6d G_O2tau_part = real_factory_6d(world);
	for(const auto& ktmp:singles.functions){
		const size_t k=ktmp.first;
		const CC_function &tauk=ktmp.second;

		real_function_3d kgti_tj = apply_g12(mo_bra_(k),ti)*tj.function;
		real_function_3d kgtj_ti = apply_g12(mo_bra_(k),tj)*ti.function;
		Q(kgti_tj);
		Q(kgtj_ti);

		real_function_3d tauk_tmp = copy(tauk.function);
		G_O1tau_part +=-2.0*G(tauk_tmp,kgti_tj);
		tauk_tmp = copy(tauk.function);
		G_O2tau_part +=-2.0*G(kgtj_ti,tauk_tmp);
	}


	// GOtau12_part
	// make <kl|g|titj>*G(tauk,taul)
	real_function_6d G_O12tau_part = real_factory_6d(world);
	for(const auto& ktmp:singles.functions){
		const size_t k=ktmp.first;
		const CC_function brak = mo_bra_(k);
		for(const auto& ltmp:singles.functions){
			const size_t l=ltmp.first;
			const CC_function bral = mo_bra_(l);
			const real_function_3d taul = copy(ltmp.second.function); // copy because greens is destructive
			const real_function_3d tauk = copy(ktmp.second.function); // copy because greens is destructive
			const double kgftitj=make_integral(k,l,ti,tj);

			G_O12tau_part += -2.0*kgftitj*G(tauk,taul);

		}
	}

	G_O1tau_part.print_size( "G(|tauk><k|g|titj>_2)");
	G_O2tau_part.print_size( "G(|tauk><k|g|titj>_1)");
	G_O12tau_part.print_size("G(|tauk,taul><kl|g|titj>)");
	return G_O1tau_part + G_O2tau_part - G_O12tau_part;
}

real_function_6d CC_Operators::make_cc2_residue_sepparated(const CC_function &taui, const CC_function &tauj)const{
	calctype ctype = CC2_;
	const bool symmetric = (taui.i==tauj.i);
	if(make_norm(taui)<parameters.thresh_3D and make_norm(tauj)<parameters.thresh_3D){
		output("Singles are zero: Current Calculation is MP2");
		ctype = MP2_;
	}
	const CC_function ti = make_t_intermediate(taui);
	const CC_function tj = make_t_intermediate(tauj);
	const double epsij =  get_epsilon(taui.i,tauj.i);
	const double epsi =  get_orbital_energies()[taui.i];
	const double epsj =  get_orbital_energies()[tauj.i];
	if((epsi+epsj!=epsij)) warning("Error in epsilon values: (epsi+epsj-epsij)="+stringify(epsi+epsj-epsij));
	// Greens operator to apply later:
	real_convolution_6d G = BSHOperator<6>(world, sqrt(-2.0*epsij),parameters.lo, parameters.thresh_bsh_6D);
	G.destructive()=true;
	// Greens operator to screen
	real_convolution_6d Gscreen = BSHOperator<6>(world,sqrt(-2.0*epsij),parameters.lo, parameters.thresh_bsh_6D);
	Gscreen.modified() = true;


	real_function_3d F_ti = real_factory_3d(world);
	real_function_3d F_tj = real_factory_3d(world);
	if(ctype == CC2_){
		F_ti= (apply_F(ti)-epsi*ti.function).truncate();
		if(symmetric) F_tj=copy(F_ti);
		else F_tj= (apply_F(tj)-epsj*tj.function).truncate();
	}


	output_section("CC2-Residue-Unprojected-Part");
	CC_Timer time_unprojected(world,"CC2-Residue:Unprojected-Part");
	real_function_6d unprojected_result;
	real_function_6d unprojected_potential;
	{
		real_function_6d fFeij_part = real_factory_6d(world);
		if(ctype == CC2_){
			fFeij_part =make_f_xy_screened(F_ti,tj,Gscreen) + make_f_xy_screened(ti,F_tj,Gscreen);
		}
		const real_function_6d Uepot_part = apply_transformed_Ue(ti,tj);
		const real_function_6d Kf_fK_part = apply_exchange_commutator(ti,tj);

		const real_function_6d V = (fFeij_part + Uepot_part - Kf_fK_part).truncate().reduce_rank();
		unprojected_potential = copy(V); // necessary because G is detructive
		Kf_fK_part.print_size("[K,f]"+ti.name()+tj.name()+"   ");
		Uepot_part.print_size("Ue"+ti.name()+tj.name()+"      ");
		fFeij_part.print_size("f(F-eij)"+ti.name()+tj.name()+"");
		V.print_size(      "-2.0(F-eij+Ue-[K,f])"+ti.name()+tj.name());
		const real_function_6d tmp = G(-2.0*V);
		unprojected_result = tmp;
		unprojected_result.print_size("G(-2.0(F-eij+Ue-[K,f]))"+ti.name()+tj.name());
	}
	time_unprojected.info();

	output_section("CC2-Residue-Projected-Part");
	CC_Timer time_projected(world,"CC2-Residue:Projected-Part");
	const double tight_thresh = parameters.tight_thresh_6D;
	real_function_6d projected_result=real_factory_6d(world);
	projected_result.set_thresh(tight_thresh);
	output("Tighten thresh to "+stringify(tight_thresh));
	FunctionDefaults<6>::set_thresh(tight_thresh);
	{
		// the f(F-eij+K) operator is of type A12 = f12(A1+A2)
		// (O1+O1-O12)(A12) = k(1)*[(<k|A|x>(2)*y(2) - 1/2 <kl|A|xy> l(2)] + []*l(2)
		// 					= |k> (x) (kAxy_1 - 1/2 im_k) + (kAxy_2 - 1/2 im_k)(x)|k>
		// im_k = \sum_l <kl|A|xy> |l>
		//
		vecfuncT kAxy_1;
		vecfuncT kAxy_2;
		vecfuncT im_k1;
		vecfuncT im_k2;
		for(const auto & ktmp:mo_bra_.functions){
			const CC_function & k=ktmp.second;
			const real_function_3d kAxy1 = unprojected_potential.project_out(k.function,0);
			const real_function_3d kAxy2 = unprojected_potential.project_out(k.function,1);
			real_function_3d imk1 = real_factory_3d(world);
			real_function_3d imk2 = real_factory_3d(world);
			for(const auto & ltmp:mo_bra_.functions){
				const CC_function & l=ltmp.second;
				imk1 += l.inner(kAxy1)*mo_ket_(l).function;
				imk2 += l.inner(kAxy2)*mo_ket_(l).function;
			}
			kAxy_1.push_back(kAxy1);
			kAxy_2.push_back(kAxy2);
			imk1.truncate();
			imk2.truncate();
			im_k1.push_back(imk1);
			im_k2.push_back(imk2);
		}

		for(const auto & ktmp:mo_ket_.functions){
			const CC_function & k=ktmp.second;
			const real_function_3d k1 = copy(k.function);// necessary because G is detructive
			const real_function_3d k2 = copy(k.function);// necessary because G is detructive
			const real_function_3d tmp1  = kAxy_1[k.i] - 0.5*im_k1[k.i];
			const real_function_6d part1 = G(-2.0*k1,tmp1);
			const real_function_3d tmp2  = kAxy_2[k.i] - 0.5*im_k2[k.i];
			const real_function_6d part2 = G(tmp2,-2.0*k2);
			projected_result += (part1+part2).truncate(tight_thresh);
		}
		projected_result.print_size("-2.0G[(O1+O2-O12)(fF-feij+Ue-[K,f])|"+ti.name()+tj.name()+">]");
	}
	time_projected.info();
	output("Lowering thresh back to "+stringify(parameters.thresh_6D));
	FunctionDefaults<6>::set_thresh(parameters.thresh_6D);
	real_function_6d cc2_residue = unprojected_result - projected_result;
	cc2_residue.print_size("cc2_residue");
	apply_Q12(cc2_residue,"cc2_residue");
	cc2_residue.print_size("Q12cc2_residue");
	return cc2_residue;
}


double CC_Operators::compute_mp2_pair_energy(CC_Pair &pair)const{

	const size_t i = pair.i;
	const size_t j = pair.j;

	// this will be the bra space
	real_function_6d eri = TwoElectronFactory(world).dcut(parameters.lo);
	real_function_6d ij_g =CompositeFactory<double, 6, 3>(world).particle1(copy(mo_bra_(i).function)).particle2(copy(mo_bra_(j).function)).g12(eri);
	real_function_6d ji_g =CompositeFactory<double, 6, 3>(world).particle1(copy(mo_bra_(j).function)).particle2(copy(mo_bra_(i).function)).g12(eri);

	// compute < ij | g12 | psi >
	const double ij_g_uij = inner(pair.function, ij_g);
	if (world.rank() == 0)
		printf("<ij | g12       | psi^1>  %12.8f\n", ij_g_uij);

	if(parameters.debug){
		if(world.rank()==0){
			std::cout << "Debugging make_ijgu function with mp2 pair energy\n";
		}
		const double ijguij = make_ijgu(pair.i,pair.j,pair);
		if(fabs(ijguij-ij_g_uij)>FunctionDefaults<6>::get_thresh()) warning("make_ijgu and mp2 pair energy function give not the same value "+ stringify(ijguij) + " vs " + stringify(ij_g_uij));
		else if(world.rank()==0) std::cout << "make_ijgu function seems to be fine values are: " << ijguij << " and " << ij_g_uij << std::endl;
	}

	// compute < ji | g12 | psi > if (i/=j)
	const double ji_g_uij = (pair.i == pair.j) ? 0 : inner(pair.function, ji_g);
	if (world.rank() == 0)
		printf("<ji | g12       | psi^1>  %12.8f\n", ji_g_uij);

	// the singlet and triplet triplet pair energies
	if (pair.i == pair.j) {
		pair.e_singlet = ij_g_uij + pair.ij_gQf_ij;
		pair.e_triplet = 0.0;
	} else {
		pair.e_singlet = (ij_g_uij + pair.ij_gQf_ij)+ (ji_g_uij + pair.ji_gQf_ij);
		pair.e_triplet = 3.0* ((ij_g_uij - ji_g_uij) + (pair.ij_gQf_ij - pair.ji_gQf_ij));
	}

	// print the pair energies
	if (world.rank() == 0) {
		printf("current energy %2d %2d %12.8f %12.8f\n", pair.i, pair.j,
				pair.e_singlet, pair.e_triplet);
	}

	// return the total energy of this pair
	return pair.e_singlet + pair.e_triplet;
}

// The Fock operator is partitioned into F = T + Vn + R
// the fock residue R= 2J-K+Un for closed shell is computed here
// J_i = \sum_k <k|r12|k> |tau_i>
// K_i = \sum_k <k|r12|tau_i> |k>
vecfuncT CC_Operators::fock_residue_closed_shell(const CC_vecfunction &singles) const {
	//	vecfuncT tau = singles.get_vecfunction();
	//CC_Timer timer_J(world,"J");
	//	vecfuncT J = mul(world, intermediates_.get_hartree_potential(), tau);
	vecfuncT J;
	for(const auto& tmpi:singles.functions){
		const CC_function& i=tmpi.second;
		const real_function_3d Ji = intermediates_.get_hartree_potential()*i.function;
		J.push_back(Ji);
	}
	truncate(world, J);
	scale(world, J, 2.0);
	//timer_J.info();
	//CC_Timer timer_K(world,"K");
	vecfuncT vK;
	for(const auto& tmpi:singles.functions){
		const CC_function& taui=tmpi.second;
		const real_function_3d Ki = K(taui);
		vK.push_back(Ki);
	}
	scale(world, vK, -1.0);
	//timer_K.info();

	// apply nuclear potential
	Nuclear Uop(world,&nemo);
	vecfuncT Upot = Uop(singles.get_vecfunction());
	vecfuncT KU = add(world,vK,Upot);

	return add(world, J, KU);
}

vecfuncT CC_Operators::ccs_potential(const CC_vecfunction &tau) const {
	// first form the intermediate t-functions: ti = i + taui
	const CC_vecfunction tfunctions = make_t_intermediate(tau);
	vecfuncT result;

	// get the perturbed hartree_potential: kgtk = sum_k <k|g|\tau_k>
	const real_function_3d kgtauk =
			intermediates_.get_perturbed_hartree_potential();

	for (const auto& ttmp : tfunctions.functions) {
		const CC_function& ti = ttmp.second;
		real_function_3d resulti = real_factory_3d(world);

		const real_function_3d kgtauk_ti = kgtauk * ti.function;
		real_function_3d kgti_tauk = real_factory_3d(world);
		for (const auto &ktmp : tau.functions) {
			const CC_function& tauk = ktmp.second;
			const real_function_3d kgti = (intermediates_.get_pEX(tauk, ti)
					+ intermediates_.get_EX(tauk, ti));
			kgti_tauk += kgti * tauk.function;
		}

		real_function_3d l_kgtauk_ti_taul = real_function_3d(world);
		real_function_3d l_kgti_tauk_taul = real_function_3d(world);
		for (const auto &ltmp : tau.functions) {
			const CC_function& taul = ltmp.second;
			l_kgtauk_ti_taul += mo_bra_(taul).inner(kgtauk_ti)
											* taul.function;
			l_kgti_tauk_taul += mo_bra_(taul).inner(kgti_tauk)
											* taul.function;
		}

		resulti = 2.0 * kgtauk_ti - kgti_tauk - 2.0 * l_kgtauk_ti_taul
				+ l_kgti_tauk_taul;
		result.push_back(resulti);
	}
	return result;
}

vecfuncT CC_Operators::S2b_u_part(const Pairs<CC_Pair> &doubles,
		const CC_vecfunction &singles) const {
	vecfuncT result;
	if (current_s2b_u_part.empty()) {
		for (const auto& itmp : singles.functions) {
			const size_t i = itmp.first;
			real_function_3d resulti = real_factory_3d(world);
			for (const auto& ktmp : singles.functions) {
				const size_t k = ktmp.first;
				const real_function_6d uik = get_pair_function(doubles, i,
						k);
				// S2b u-part
				{
					const real_function_6d kuik = multiply(copy(uik),
							copy(mo_bra_(k).function), 2);
					poisson->particle() = 2;
					const real_function_6d kguik = (*poisson)(kuik);
					resulti += 2.0 * kguik.dirac_convolution<3>();
				}
				// S2b u-part-exchange
				{
					const real_function_6d kuik = multiply(copy(uik),
							copy(mo_bra_(k).function), 1);
					poisson->particle() = 1;
					const real_function_6d kguik = (*poisson)(kuik);
					resulti -= kguik.dirac_convolution<3>();
				}
			}
			result.push_back(resulti);
		}
		current_s2b_u_part = copy(world,result);
	} else {
		output("found previously calculated S2b-u-part");
		result = copy(world,current_s2b_u_part);
	}
	return result;
}

vecfuncT CC_Operators::S2c_u_part(const Pairs<CC_Pair> &doubles,
		const CC_vecfunction &singles) const {
	vecfuncT result;
	if (current_s2c_u_part.empty()) {
		for (const auto& itmp : singles.functions) {
			const size_t i = itmp.first;
			real_function_3d resulti = real_factory_3d(world);
			for (const auto& ktmp : singles.functions) {
				const size_t k = ktmp.first;
				const real_function_3d kgi = intermediates_.get_EX(k, i);

				for (const auto& ltmp : singles.functions) {
					const size_t l = ltmp.first;
					const real_function_6d ukl = get_pair_function(doubles,
							k, l);
					const real_function_3d l_kgi = mo_bra_(l).function
							* kgi;
					resulti += -2.0 * ukl.project_out(l_kgi, 1); // 1 means second particle
					resulti += ukl.project_out(l_kgi, 0);
				}
			}
			result.push_back(resulti);
		}
		current_s2c_u_part = copy(world,result);
	} else {
		output("found previously calculated S2c-u-part");
		result = copy(world,current_s2c_u_part);
	}
	return result;
}

/// The Part of the CC2 singles potential which depends on singles and doubles (S4a, S4b, S4c)
vecfuncT CC_Operators::S4a_u_part(const Pairs<CC_Pair> &doubles,
		const CC_vecfunction &singles) const {
	// S4a can be computed from the S2b potential
	// (-2<lk|g|uik> + <kl|g|uik>)|tau_l> =( <l( (-2)*<k|g|uik>_2) + <l| (<k|g|uik>_1) )|tau_l> = <l|s2b_u_part>*|tau_l> = - \sum_l <l|s2b_i> |l> important: minus sign and the fact that the s2b potential needs to be unprojected
	vecfuncT s4a;
	for (const auto& itmp : singles.functions) {
		const size_t i = itmp.first;
		real_function_3d s4ai = real_factory_3d(world);
		real_function_3d s4ai_consistency = real_factory_3d(world); // here the unprojected s2b result will be used to check consistency since this is not expensive this will be used everytime the s2b part was stored
		for (const auto& ltmp : singles.functions) {
			const size_t l = ltmp.first;
			const CC_function& taul = ltmp.second;
			for (const auto& ktmp : singles.functions) {
				const size_t k = ktmp.first;
				s4ai += (-2.0
						* make_ijgu(l, k,
								get_pair_function(doubles, i, k))
				+ make_ijgu(k, l,
						get_pair_function(doubles, i, k)))
													* taul.function;
			}
			if(not current_s2b_u_part.empty()){
				s4ai_consistency -= (mo_bra_(l).function.inner(current_s2b_u_part[i-parameters.freeze]))*taul.function;
				std::cout << "||current_s2b_u_part[" << i-parameters.freeze << "]||=" << current_s2b_u_part[i-parameters.freeze].norm2() << std::endl;
				std::cout << "<l|current_s2b_u_part[" << i-parameters.freeze << "]="<< mo_bra_(l).function.inner(current_s2b_u_part[i-parameters.freeze]) << std::endl;
				std::cout << "||taul||=||" << taul.name() << "||=" << taul.function.norm2() << std::endl;
			}
		}
		if(not current_s2b_u_part.empty()){
			const double consistency = (s4ai - s4ai_consistency).norm2();
			if(world.rank()==0){
				std::cout << "||s4a||_" << i << " = " << s4ai.norm2() << std::endl;
				std::cout << "||-sum_l <l|s2b>|taul>||_" << i << " = " << s4ai_consistency.norm2() << std::endl;
				std::cout << "||s4a + sum_l <l|s2b>|taul>||_" << i << " = " << consistency << std::endl;
			}
			if(consistency>FunctionDefaults<6>::get_thresh()) warning("S4a Consistency Check above the 6D thresh");
		}
		s4a.push_back(s4ai);
	}
	return s4a;
}

// result: -\sum_k( <l|kgtaui|ukl>_2 - <l|kgtaui|ukl>_1) | kgtaui = <k|g|taui>
vecfuncT CC_Operators::S4b_u_part(const Pairs<CC_Pair> &doubles,
		const CC_vecfunction &singles) const {
	vecfuncT result;
	for (const auto& itmp : singles.functions) {
		const size_t i = itmp.first;
		real_function_3d resulti = real_factory_3d(world);
		for (const auto& ktmp : singles.functions) {
			const size_t k = ktmp.first;
			const real_function_3d kgi = intermediates_.get_pEX(k, i);

			for (const auto& ltmp : singles.functions) {
				const size_t l = ltmp.first;
				const real_function_6d ukl = get_pair_function(doubles, k,
						l);
				const real_function_3d l_kgi = mo_bra_(l).function * kgi;
				resulti += -2.0 * ukl.project_out(l_kgi, 1); // 1 means second particle
				resulti += ukl.project_out(l_kgi, 0);
			}
		}
		result.push_back(resulti);
	}
	return result;
}

vecfuncT CC_Operators::S4c_u_part(const Pairs<CC_Pair> &doubles,
		const CC_vecfunction &singles) const {
	vecfuncT result;
	// DEBUG
	const CC_vecfunction t = make_t_intermediate(singles);
	// DEBUG END
	for (const auto& itmp : singles.functions) {
		const size_t i = itmp.first;
		real_function_3d resulti = real_factory_3d(world);
		real_function_3d part1 = real_factory_3d(world);
		real_function_3d part2 = real_factory_3d(world);
		real_function_3d part3 = real_factory_3d(world);
		real_function_3d part4 = real_factory_3d(world);
		const real_function_3d kgtauk =intermediates_.get_perturbed_hartree_potential();

		for (const auto& ltmp : singles.functions) {
			const size_t l = ltmp.first;
			const real_function_3d l_kgtauk = mo_bra_(l).function * kgtauk;
			const real_function_6d uil = get_pair_function(doubles, i, l);
			part1 += uil.project_out(l_kgtauk, 1);
			part2 += uil.project_out(l_kgtauk, 0);

			for (const auto& ktmp : singles.functions) {
				const size_t k = ktmp.first;
				const real_function_3d k_lgtauk = mo_bra_(k).function
						* intermediates_.get_pEX(l, k);
				part3 += uil.project_out(k_lgtauk, 1);
				part4 += uil.project_out(k_lgtauk, 0);
			}
		}
		resulti = 4.0*part1-2.0*part2-2.0*part3+part4;
		result.push_back(resulti);
	}
	return result;
}

vecfuncT CC_Operators::S2b_reg_part(const CC_vecfunction &singles) const {
	vecfuncT result;
	const CC_vecfunction tfunction = make_t_intermediate(singles);
	const real_function_3d ktk = intermediates_.make_density(mo_bra_,tfunction); // the case that tfunction is smaller than mo_bra_ (freeze!=0) is considered
	const real_function_3d kgftk = apply_gf(ktk);
	for (const auto& itmp : tfunction.functions) {			// convenience
		const size_t i = itmp.first;						// convenience
		const CC_function& ti = itmp.second;				// convenience
		real_function_3d resulti = real_factory_3d(world);// this will be the result
		real_function_3d Ipart    = real_factory_3d(world);
		real_function_3d Ipartx   = real_factory_3d(world);
		real_function_3d O1part   = real_factory_3d(world);
		real_function_3d O1partx  = real_factory_3d(world);
		real_function_3d O2part   = real_factory_3d(world);
		real_function_3d O2partx  = real_factory_3d(world);
		real_function_3d O12part  = real_factory_3d(world);
		real_function_3d O12partx = real_factory_3d(world);
		Ipart += 2.0 * kgftk * ti.function; // part1
		for (const auto& ktmp : tfunction.functions) {
			const size_t k = ktmp.first;
			const CC_function& tk = ktmp.second;
			const real_function_3d kti = mo_bra_(k).function * ti.function;
			const real_function_3d kgfti = apply_gf(kti);
			Ipartx += -1.0 * kgfti * tk.function; // part1x

			for (const auto& mtmp : mo_ket_.functions) {
				const size_t m = mtmp.first;
				const CC_function& mom = mtmp.second;
				const real_function_3d mftk = intermediates_.get_fEX(m, k)
												+ intermediates_.get_pfEX(m, k);
				const real_function_3d mfti = intermediates_.get_fEX(m, i)
												+ intermediates_.get_pfEX(m, i);
				const real_function_3d kgm = intermediates_.get_EX(k, m);
				const real_function_3d mfti_tk = mfti * tk.function;
				const real_function_3d mftk_ti = mftk * ti.function;
				O2part -= (2.0 * kgm * mftk_ti); //part3
				O2partx-= (-1.0*kgm * mfti_tk);
				const real_function_3d k_mfti_tk = mo_bra_(k).function
						* mfti_tk;
				const real_function_3d k_gmfti_tk = (*poisson)(k_mfti_tk);
				const real_function_3d k_mftk_ti = mo_bra_(k).function
						* mftk_ti;
				const real_function_3d k_gmftk_ti = (*poisson)(k_mftk_ti);
				O1part -= (2.0 * k_gmfti_tk * mom.function); //part2
				O1partx-=(-1.0*k_gmftk_ti * mom.function);
				for (const auto& ntmp : mo_ket_.functions) {
					const CC_function& mon = ntmp.second;
					const double nmftitk = mo_bra_(mon).inner(mftk_ti);
					const double nmftkti = mo_bra_(mon).inner(mfti_tk);
					O12part  += (2.0 * nmftitk * kgm * mon.function);
					O12partx += (-1.0*nmftkti * kgm * mon.function);
				} // end n
			} // end m
		} // end k
		resulti = Ipart + Ipartx + O1part + O1partx + O2part + O2partx + O12part + O12partx;
		result.push_back(resulti);
	} // end i
	return result;
}

vecfuncT CC_Operators::S2c_reg_part(const CC_vecfunction &singles) const {
	vecfuncT result;
	const CC_vecfunction tfunctions = make_t_intermediate(singles);
	for (const auto& itmp : singles.functions) {
		const CC_function taui = itmp.second;
		real_function_3d resulti = real_factory_3d(world);

		for (const auto& ktmp : tfunctions.functions) {
			const CC_function tk = ktmp.second;
			for (const auto& ltmp : tfunctions.functions) {
				const CC_function tl = ltmp.second;
				const real_function_3d l_kgi_tmp = mo_bra_(tl).function
						* intermediates_.get_EX(tk, taui);
				const CC_function l_kgi(l_kgi_tmp, 99, UNDEFINED);
				resulti -= (2.0 * convolute_x_Qf_yz(l_kgi, tk, tl)
				- convolute_x_Qf_yz(l_kgi, tl, tk));
			}
		}
		result.push_back(resulti);
	}
	return result;
}

vecfuncT CC_Operators::S4a_reg_part(const CC_vecfunction &singles) const {
        vecfuncT result;
        const CC_vecfunction tfunctions = make_t_intermediate(singles);
        for (const auto& itmp : tfunctions.functions) {
                const CC_function& ti = itmp.second;
                real_function_3d resulti = real_factory_3d(world);

                for (const auto& ktmp : tfunctions.functions) {
                        const CC_function& tk = ktmp.second;
                        const size_t k = ktmp.first;

                        for (const auto& ltmp : singles.functions) {
                                const CC_function& taul = ltmp.second;
                                const size_t l = ltmp.first;

                                const double lkgQftitk = make_ijgQfxy(l, k, ti, tk);
                                const double klgQftitk = make_ijgQfxy(k, l, ti, tk);
                                resulti -= (2.0 * lkgQftitk - klgQftitk) * taul.function;
                        }
                }
                result.push_back(resulti);
        }
        return result;
}

/// result: -\sum_{kl}( 2 <l|kgtaui|Qftktl> - <l|kgtaui|Qftltk>
/// this is the same as S2c with taui instead of i
vecfuncT CC_Operators::S4b_reg_part(const CC_vecfunction &singles) const {
        vecfuncT result;
        const CC_vecfunction tfunctions = make_t_intermediate(singles);
        for (const auto& itmp : singles.functions) {
                const CC_function taui = itmp.second;
                real_function_3d resulti = real_factory_3d(world);

                for (const auto& ktmp : tfunctions.functions) {
                        const CC_function tk = ktmp.second;
                        for (const auto& ltmp : tfunctions.functions) {
                                const CC_function tl = ltmp.second;
                                const real_function_3d l_kgi_tmp = mo_bra_(tl).function
                                                * intermediates_.get_pEX(tk, taui);
                                const CC_function l_kgi(l_kgi_tmp, 99, UNDEFINED);
                                resulti -= (2.0 * convolute_x_Qf_yz(l_kgi, tk, tl)
                                - convolute_x_Qf_yz(l_kgi, tl, tk));
                        }
                }
                result.push_back(resulti);
        }
        return result;
}


/// result: 4<l|kgtauk|Qftitl> - 2<l|kgtauk|Qftlti> - 2<k|lgtauk|Qftitl> + <k|lgtauk|Qftlti>
vecfuncT CC_Operators::S4c_reg_part(const CC_vecfunction &singles) const {
	vecfuncT result;
	const CC_vecfunction tfunctions = make_t_intermediate(singles);
	for (const auto& itmp : tfunctions.functions) {
		const CC_function& ti = itmp.second;
		real_function_3d resulti = real_factory_3d(world);

		const real_function_3d kgtauk =
				intermediates_.get_perturbed_hartree_potential();

		// first two parts
		real_function_3d part1 = real_factory_3d(world);
		real_function_3d part2 = real_factory_3d(world);
		for (const auto& ltmp : tfunctions.functions) {
			const CC_function& tl = ltmp.second;
			const size_t l = ltmp.first;
			const real_function_3d l_kgtauk =(mo_bra_(l).function * kgtauk);
			part1 += convolute_x_Qf_yz(CC_function(l_kgtauk, 99, UNDEFINED),
					ti, tl);
			part2 += convolute_x_Qf_yz(CC_function(l_kgtauk, 99, UNDEFINED),
					tl, ti);
		}

		// second two parts
		real_function_3d part3 = real_factory_3d(world);
		real_function_3d part4 = real_factory_3d(world);
		for (const auto& ktmp : singles.functions) {
			const CC_function& tauk = ktmp.second;
			const size_t k = ktmp.first;

			for (const auto& ltmp : tfunctions.functions) {
				const CC_function& tl = ltmp.second;
				const size_t l = ltmp.first;

				const real_function_3d k_lgtauk = (mo_bra_(k).function
						* apply_g12(mo_bra_(l), tauk));
				part3 += convolute_x_Qf_yz(
						CC_function(k_lgtauk, 99, UNDEFINED), ti, tl);
				part4 += convolute_x_Qf_yz(
						CC_function(k_lgtauk, 99, UNDEFINED), tl, ti);
			}
		}
		resulti = 4.0 * part1 - 2.0 * part2 - 2.0 * part3 + part4;
		result.push_back(resulti);
	}
	return result;
}

// The two brillouin terms S1 and S5a of the singles potential
vecfuncT CC_Operators::S1(const CC_vecfunction &tau) const {
	vecfuncT result;
	for (auto tmpi : tau.functions) {
		CC_function& i = tmpi.second;
		real_function_3d resulti = real_factory_3d(world);
		resulti = apply_F(CC_function(mo_ket_(i.i).function,i.i,UNDEFINED)); // undefined for the testing case where the mos are not converged
		result.push_back(resulti);
	}
	return result;
}

vecfuncT CC_Operators::S5a(const CC_vecfunction &tau) const {
	vecfuncT result;
	for (auto tmpi : tau.functions) {
		CC_function& i = tmpi.second;
		real_function_3d resulti = real_factory_3d(world);
		for (auto tmpk : tau.functions) {
			CC_function& k = tmpk.second;
			real_function_3d tmp = apply_F(
					CC_function(i.function, i.i, UNDEFINED)); // undefined for the test case where the moi are not converged yet
			const double a = mo_bra_(k.i).function.inner(tmp);
			resulti -= a * k.function;
		}
		result.push_back(resulti);
	}
	Q(result);
	return result;
}

/// Make the CC2 Residue which is:  Q12f12(T-eij + 2J -K +Un )|titj> + Q12Ue|titj> - [K,f]|titj>  with |ti> = |\taui>+|i>
/// @param[in] \tau_i which will create the |t_i> = |\tau_i>+|i> intermediate
/// @param[in] \tau_j
/// @param[in] u, the uij pair structure which holds the consant part of MP2
/// @param[out] Q12f12(F-eij)|titj> + Q12Ue|titj> - [K,f]|titj>  with |ti> = |\taui>+|i>
/// Right now Calculated in the decomposed form: |titj> = |i,j> + |\taui,\tauj> + |i,\tauj> + |\taui,j>
/// The G_Q_Ue and G_Q_KffK part which act on |ij> are already calculated and stored as constant_term in u (same as for MP2 calculations) -> this should be the biggerst (faster than |titj> form)
real_function_6d CC_Operators::make_cc2_residue(const CC_function &taui, const CC_function &tauj)const{
	const CC_function ti = make_t_intermediate(taui);
	const CC_function tj = make_t_intermediate(tauj);
	const real_function_3d Fti = apply_F(ti)-get_orbital_energies()[ti.i]*ti.function;
	const real_function_3d Ftj = apply_F(tj)-get_orbital_energies()[tj.i]*tj.function;

	// make the Fock operator part:  f(F-eij)|titj> = (F1+F2-ei-ej)|titj> = (F1-ei)|ti>|tj> + |ti>(F2-ei)|tj>
	const real_function_6d fF_titj = make_f_xy(Fti,tj) + make_f_xy(ti,Ftj);


	output("Making the CC2 Residue");

	// make the (U-[K,f])|titj> part
	// first the U Part
	const real_function_6d U_titj = apply_transformed_Ue(ti,tj);
	// then the [K,f] part
	const real_function_6d KffK_titj = apply_exchange_commutator(ti,tj);

	real_function_6d V = (fF_titj + U_titj - KffK_titj);
	V.scale(-2.0);
	V.print_size("V");
	apply_Q12(V,"CC2-Residue:Potential");
	V.print_size("Q12V");
	V.truncate().reduce_rank();
	V.print_size("Q12V.truncate");
	fF_titj.print_size(   "CC2-Residue: f12(F-eij)|"+ti.name()+tj.name()+">");
	U_titj.print_size(    "CC2-Residue:          U|"+ti.name()+tj.name()+">");
	KffK_titj.print_size( "CC2-Residue:      [K,f]|"+ti.name()+tj.name()+">");

	real_convolution_6d G = BSHOperator<6>(world, sqrt(-2.0 * get_epsilon(taui.i,tauj.i)),parameters.lo, parameters.thresh_bsh_6D);
	G.destructive()=true;
	real_function_6d GV = G(V);
	apply_Q12(GV,"CC2-Residue:G(V)");
	return GV;
}

// apply the kinetic energy operator with cusp to a decomposed 6D function
/// @param[in] a 3d function x (will be particle 1 in the decomposed 6d function)
/// @param[in] a 3d function y (will be particle 2 in the decomposed 6d function)
/// @param[out] a 6d function: G(f12*T*|xy>)
real_function_6d CC_Operators::make_GQfT_xy(const real_function_3d &x, const real_function_3d &y, const size_t &i, const size_t &j)const{
	error("make_GQfT should not be used");
	// construct the greens operator
	real_convolution_6d G = BSHOperator<6>(world, sqrt(-2.0*get_epsilon(i,j)),parameters.lo, parameters.thresh_bsh_6D);

	std::vector < std::shared_ptr<real_derivative_3d> > gradop;
	gradop = gradient_operator<double, 3>(world);
	vecfuncT gradx,grady;
	vecfuncT laplacex, laplacey;
	for(size_t axis=0;axis<3;axis++){
		real_function_3d gradxi = (*gradop[axis])(x);
		real_function_3d gradyi = (*gradop[axis])(y);
		gradx.push_back(gradxi);
		grady.push_back(gradyi);
		real_function_3d grad2xi = (*gradop[axis])(gradxi);
		real_function_3d grad2yi = (*gradop[axis])(gradyi);
		laplacex.push_back(grad2xi);
		laplacey.push_back(grad2yi);
	}
	real_function_3d laplace_x = laplacex[0]+laplacex[1]+laplacex[2];
	real_function_3d laplace_y = laplacey[0]+laplacey[1]+laplacey[2];
	real_function_3d Tx = laplace_x.scale(-0.5);
	real_function_3d Ty = laplace_y.scale(-0.5);
	// make the two screened 6D functions
	// fTxy = f12 |(\Delta x)y> , fxTy = f12 |x\Delta y> (delta = laplace_operator)
	real_function_6d fTxy = CompositeFactory<double,6,3>(world).g12(corrfac.f()).particle1(copy(Tx)).particle2(copy(y));
	real_function_6d fxTy = CompositeFactory<double,6,3>(world).g12(corrfac.f()).particle1(copy(x)).particle2(copy(Ty));
	// for now construct explicitly and project out Q12 later: use BSH operator to screen
	if(world.rank()==0) std::cout << "Constructing fTxy with G as screening operator\n";
	CC_Timer fTxy_construction_time(world,"Screened 6D construction of fTxy");
	{
		real_convolution_6d screenG = BSHOperator<6>(world, sqrt(-2.0*get_epsilon(i,j)),parameters.lo, parameters.thresh_bsh_6D);
		screenG.modified()=true;
		fTxy.fill_tree(screenG).truncate().reduce_rank();
	}{
		real_convolution_6d screenG = BSHOperator<6>(world, sqrt(-2.0*get_epsilon(i,j)),parameters.lo, parameters.thresh_bsh_6D);
		screenG.modified()=true;
		fxTy.fill_tree(screenG).truncate().reduce_rank();
	}
	fTxy_construction_time.info();
	CC_Timer addition_time(world,"f(Tx)y + fxTy");
	real_function_6d result = (fTxy + fxTy).truncate();
	apply_Q12(result,"fT|xy>");

	CC_Timer apply_G(world,"G(fTxy)");
	real_function_6d G_result = G(result);
	G_result.truncate();
	apply_G.info();
	return G_result;
}



/// The 6D Fock residue on the cusp free pair function u_{ij}(1,2) is: (2J - Kn - Un)|u_{ij}>
real_function_6d CC_Operators::fock_residue_6d(const CC_Pair &u) const {
	const double eps = get_epsilon(u.i, u.j);
	// make the coulomb and local Un part with the composite factory
	real_function_3d local_part = (2.0
			* intermediates_.get_hartree_potential()
			+ nemo.nuclear_correlation->U2());
	local_part.print_size("vlocal");
	u.function.print_size("u");

	// Contruct the BSH operator in order to screen

	real_convolution_6d op_mod = BSHOperator<6>(world, sqrt(-2.0 * eps),parameters.lo, parameters.thresh_bsh_6D);
	op_mod.modified() = true;
	// Make the CompositeFactory
	real_function_6d vphi =
			CompositeFactory<double, 6, 3>(world).ket(copy(u.function)).V_for_particle1(
					copy(local_part)).V_for_particle2(copy(local_part));
	// Screening procedure
	vphi.fill_tree(op_mod);

	vphi.print_size("vlocal|u>");

	// the part with the derivative operators: U1
	for (int axis = 0; axis < 6; ++axis) {
		real_derivative_6d D = free_space_derivative<double, 6>(world,axis);
		// Partial derivative of the pari function
		const real_function_6d Du = D(u.function).truncate();

		// % operator gives division rest (modulo operator)
		if (world.rank() == 0)
			print("axis, axis^%3, axis/3+1", axis, axis % 3, axis / 3 + 1);
		const real_function_3d U1_axis = nemo.nuclear_correlation->U1(
				axis % 3);

		double tight_thresh = parameters.tight_thresh_6D;
		if(tight_thresh>1.e-4) warning("tight_thresh_6D is too low for Un potential");
		real_function_6d x;
		if (axis / 3 + 1 == 1) {
			x =
					CompositeFactory<double, 6, 3>(world).ket(copy(Du)).V_for_particle1(
							copy(U1_axis)).thresh(tight_thresh);

		} else if (axis / 3 + 1 == 2) {
			x =
					CompositeFactory<double, 6, 3>(world).ket(copy(Du)).V_for_particle2(
							copy(U1_axis)).thresh(tight_thresh);
		}
		x.fill_tree(op_mod);
		x.set_thresh(FunctionDefaults<6>::get_thresh());
		x.print_size("Un_axis_"+stringify(axis));
		vphi += x;
		vphi.truncate().reduce_rank();
	}

	vphi.print_size("(Un + J1 + J2)|u>");

	// Exchange Part
	vphi = (vphi - K(u.function, u.i == u.j)).truncate().reduce_rank();
	vphi.print_size("(Un + J - K)|u>");
	return vphi;

}

/// Echange Operator on 3D function
/// !!!!Prefactor (-1) is not included
real_function_3d CC_Operators::K(const CC_function &x)const{
	return apply_K(x);
}
real_function_3d CC_Operators::K(const real_function_3d &x)const{
	const CC_function tmp(x,99,UNDEFINED);
	return apply_K(tmp);
}

/// Exchange Operator on Pair function: -(K(1)+K(2))u(1,2)
/// if i==j in uij then the symmetry will be exploited
/// !!!!Prefactor (-1) is not included here!!!!
real_function_6d CC_Operators::K(const real_function_6d &u,
		const bool symmetric) const {
	real_function_6d result = real_factory_6d(world).compressed();
	// K(1) Part
	result += apply_K(u, 1);
	// K(2) Part
	if (symmetric)
		result += swap_particles(result);
	else
		result += apply_K(u, 2);

	return (result.truncate());
}

/// Exchange Operator on Pair function: -(K(1)+K(2))u(1,2)
/// K(1)u(1,2) = \sum_k <k(3)|g13|u(3,2)> |k(1)>
/// 1. X(3,2) = bra_k(3)*u(3,2)
/// 2. Y(1,2) = \int X(3,2) g13 d3
/// 3. result = Y(1,2)*ket_k(1)
/// !!!!Prefactor (-1) is not included here!!!!
real_function_6d CC_Operators::apply_K(const real_function_6d &u,
		const size_t &particle) const {
	MADNESS_ASSERT(particle == 1 or particle == 2);
	poisson->particle() = particle;
	real_function_6d result = real_factory_6d(world).compressed();
	for (size_t k = 0; k < mo_ket_.size(); k++) {
		real_function_6d X = (multiply(copy(u), copy(mo_bra_(k).function), particle)).truncate();
		real_function_6d Y = (*poisson)(X);
		result += (multiply(copy(Y), copy(mo_ket_(k).function), particle)).truncate();
	}
	return result;
}

// the K operator runs over ALL orbitals (also the frozen ones)
real_function_3d CC_Operators::apply_K(const CC_function &f)const{
	if(parameters.debug and world.rank()==0)  std::cout << "apply K on " << assign_name(f.type) << " function" << std::endl;
	if(parameters.debug and world.rank()==0) std::cout << "K" << f.name() << "=";
	real_function_3d result = real_factory_3d(world);
	switch(f.type){
	case HOLE:
		for(auto k_iterator:mo_ket_.functions){
			const CC_function& k = k_iterator.second;
			const real_function_3d tmp=intermediates_.get_EX(k,f);
			result += (tmp*k.function);
			if(parameters.debug and world.rank()==0) std::cout << "+ <" << k.name() << "|g|" <<f.name() <<">*"<<k.name();
		}
		break;
	case PARTICLE:
		for(auto k_iterator:mo_ket_.functions){
			const CC_function& k = k_iterator.second;
			result += (intermediates_.get_pEX(k,f)*k.function);
			if(parameters.debug and world.rank()==0) std::cout << "+ <" << k.name() << "|g|" <<f.name() <<">*"<<k.name();
		}
		break;
	case MIXED:
		for(auto k_iterator:mo_ket_.functions){
			const CC_function& k = k_iterator.second;
			result += (intermediates_.get_EX(k,f)+intermediates_.get_pEX(k,f))*k.function;
			if(parameters.debug and world.rank()==0) std::cout << "+ <" << k.name() << "|g|t" <<f.i <<">*"<<k.name();
		}
		break;
	default:
		for(auto k_iterator:mo_ket_.functions){
			const CC_function& k = k_iterator.second;
			real_function_3d tmp = ((*poisson)(mo_bra_(k).function*f.function)).truncate();
			result += tmp*k.function;
			if(parameters.debug and world.rank()==0) std::cout << "+ poisson(mo_bra_"<<k.i<<"*"<< f.name() <<")|mo_ket_"<<k.i<<">" << std::endl;
		}
		break;
	}
	return result;
}


/// Apply Ue on a tensor product of two 3d functions: Ue(1,2) |x(1)y(2)> (will be either |ij> or |\tau_i\tau_j> or mixed forms)
/// The Transformed electronic regularization potential (Kutzelnigg) is R_{12}^{-1} U_e R_{12} with R_{12} = R_1*R_2
/// It is represented as: R_{12}^{-1} U_e R_{12} = U_e + R^-1[Ue,R]
/// where R^-1[Ue,R] = R^-1 [[T,f],R] (see: Regularizing the molecular potential in electronic structure calculations. II. Many-body
/// methods, F.A.Bischoff)
/// The double commutator can be evaluated as follows:  R^-1[[T,f],R] = -Ue_{local}(1,2)*(Un_{local}(1) - Un_{local}(2))
/// @param[in] x the 3D function for particle 1
/// @param[in] y the 3D function for particle 2
/// @param[in] i the first index of the current pair function (needed to construct the BSH operator for screening)
/// @param[in] j the second index of the current pair function
/// @param[out]  R^-1U_eR|x,y> the transformed electronic smoothing potential applied on |x,y> :
real_function_6d CC_Operators::apply_transformed_Ue(const CC_function &x, const CC_function &y) const {
	// make shure the thresh is high enough
	CC_Timer time_Ue(world,"Ue|"+x.name()+y.name()+">");
	const size_t i = x.i;
	const size_t j = y.i;
	double tight_thresh = parameters.tight_thresh_6D;
	output("Applying transformed Ue with 6D thresh = " +stringify(tight_thresh));

	real_function_6d Uxy = real_factory_6d(world);
	Uxy.set_thresh(tight_thresh);
	// Apply the untransformed U Potential
	const double eps = get_epsilon(i, j);
	Uxy = corrfac.apply_U(x.function, y.function, eps);
	Uxy.set_thresh(tight_thresh);

	// Get the 6D BSH operator in modified-NS form for screening
	real_convolution_6d op_mod = BSHOperator<6>(world, sqrt(-2.0 * eps),
			parameters.lo,
			parameters.thresh_bsh_6D);
	op_mod.modified() = true;



	// Apply the double commutator R^{-1}[[T,f,R]
	for (size_t axis = 0; axis < 3; axis++) {
		// Make the local parts of the Nuclear and electronic U potentials
		const real_function_3d Un_local = nemo.nuclear_correlation->U1(
				axis);
		const real_function_3d Un_local_x = (Un_local * x.function).truncate();
		const real_function_3d Un_local_y = (Un_local * y.function).truncate();
		const real_function_6d Ue_local = corrfac.U1(axis);
		// Now add the Un_local_x part to the first particle of the Ue_local potential
		real_function_6d UeUnx = CompositeFactory<double, 6, 3>(world).g12(
				Ue_local).particle1(Un_local_x).particle2(copy(y.function)).thresh(
						tight_thresh);
		// Fill the Tree were it will be necessary
		UeUnx.fill_tree(op_mod);
		// Set back the thresh
		UeUnx.set_thresh(FunctionDefaults<6>::get_thresh());

		//UeUnx.print_size("UeUnx");

		// Now add the Un_local_y part to the second particle of the Ue_local potential
		real_function_6d UeUny = CompositeFactory<double, 6, 3>(world).g12(
				Ue_local).particle1(copy(x.function)).particle2(Un_local_y).thresh(
						tight_thresh);
		// Fill the Tree were it will be necessary
		UeUny.fill_tree(op_mod);
		// Set back the thresh
		UeUny.set_thresh(FunctionDefaults<6>::get_thresh());

		//UeUny.print_size("UeUny");

		// Construct the double commutator part and add it to the Ue part
		real_function_6d diff = (UeUnx - UeUny).scale(-1.0);
		diff.truncate();
		Uxy = (Uxy+diff).truncate();
	}
	time_Ue.info();

	// sanity check: <xy|R2 [T,g12] |xy> = <xy |R2 U |xy> - <xy|R2 g12 | xy> = 0
	CC_Timer time_sane(world,"Ue-Sanity-Check");
	real_function_6d tmp = CompositeFactory<double, 6, 3>(world).particle1(copy(x.function*nemo.nuclear_correlation -> square())).particle2(copy(y.function*nemo.nuclear_correlation -> square()));
	const double a = inner(Uxy, tmp);
	const real_function_3d xx = (x.function * x.function*nemo.nuclear_correlation -> square());
	const real_function_3d yy = (y.function * y.function*nemo.nuclear_correlation -> square());
	const real_function_3d gxx = (*poisson)(xx);
	const double aa = inner(yy, gxx);
	const double error = std::fabs(a - aa);
	time_sane.info();
	if (world.rank() == 0 and error > FunctionDefaults<6>::get_thresh()) {
		printf("<xy| U_R |xy>  %12.8f\n", a);
		printf("<xy|1/r12|xy>  %12.8f\n", aa);
		warning("Ue Potential Inaccurate!");
		if (error > FunctionDefaults<6>::get_thresh() * 10.0) warning("Ue Potential wrong !!!!");
	}else output("Ue seems to be sane");
	return Uxy;
}

/// Apply the Exchange Commutator [K,f]|xy>
real_function_6d CC_Operators::apply_exchange_commutator(const CC_function &x, const CC_function &y)const{
	CC_Timer time(world,"[K,f]|"+x.name()+y.name()+">");
	// make first part of commutator
	CC_Timer part1_time(world,"Kf"+x.name()+y.name()+">");
	real_function_6d Kfxy = apply_Kf(x,y);
	part1_time.info();
	// make the second part of the commutator
	CC_Timer part2_time(world,"fK"+x.name()+y.name()+">");
	real_function_6d fKxy = apply_fK(x,y).truncate();
	part2_time.info();
	real_function_6d result = (Kfxy - fKxy);

	time.info();
	// sanity check
	// <psi|[A,B]|psi> = <psi|AB|psi> - <psi|BA|psi> = <Apsi|Bpsi> - <Bpsi|Apsi> = 0 (if A,B hermitian)
	{
		CC_Timer sanity(world,"[K,f] sanity check");
		// make the <xy| bra state which is <xy|R2
		const real_function_3d brax = x.function * nemo.nuclear_correlation->square();
		const real_function_3d bray = y.function * nemo.nuclear_correlation->square();
		const real_function_6d xy = make_xy(CC_function(brax,x.i,x.type),CC_function(bray,y.i,y.type));
		const double xyfKxy = xy.inner(fKxy);
		const double xyKfxy = xy.inner(Kfxy);
		const double diff = xyfKxy - xyKfxy;
		if(world.rank()==0 and fabs(diff)>FunctionDefaults<6>::get_thresh()){
			std::cout << std::setprecision(parameters.output_prec);
			std::cout << "<" << x.name() << y.name() << "|fK|" << x.name() << y.name() << "> =" << xyfKxy << std::endl;
			std::cout << "<" << x.name() << y.name() << "|Kf|" << x.name() << y.name() << "> =" << xyKfxy << std::endl;
			std::cout << "difference = " << diff << std::endl;
			warning("Exchange Commutator Plain Wrong");
		}
		else if(fabs(diff)>FunctionDefaults<6>::get_thresh()*0.1) warning("Exchange Commutator critical");
		else output("Exchange Commutator seems to be sane");

		sanity.info();

	}
	return result;
}

/// Apply the Exchange operator on a tensor product multiplied with f12
/// !!! Prefactor of (-1) is not inclued in K here !!!!
real_function_6d CC_Operators::apply_Kf(const CC_function &x, const CC_function &y) const {
	bool symmetric = false;
	if((x.type == y.type) and (x.i == y.i)) symmetric = true;

	// First make the 6D function f12|x,y>
	real_function_6d f12xy = make_f_xy(x,y);
	// Apply the Exchange Operator
	real_function_6d result = K(f12xy, symmetric);
	return result;
}

/// Apply fK on a tensor product of two 3D functions
/// fK|xy> = fK_1|xy> + fK_2|xy>
/// @param[in] x, the first 3D function in |xy>, structure holds index i and type (HOLE, PARTICLE, MIXED, UNDEFINED)
/// @param[in] y, the second 3D function in |xy>  structure holds index i and type (HOLE, PARTICLE, MIXED, UNDEFINED)
real_function_6d CC_Operators::apply_fK(const CC_function &x, const CC_function &y) const {

	const real_function_3d Kx = K(x);
	const real_function_3d Ky = K(y);
	const real_function_6d fKphi0a = make_f_xy(x,CC_function(Ky,y.i,UNDEFINED));
	const real_function_6d fKphi0b = make_f_xy(CC_function(Kx,x.i,UNDEFINED),y);
	const real_function_6d fKphi0 = (fKphi0a + fKphi0b);
	return fKphi0;

}

real_function_3d CC_Operators::apply_laplacian(const real_function_3d &x) const {
	// make gradient operator for new k and with new thresh
	size_t high_k = 8;
	double high_thresh = 1.e-6;
	std::vector < std::shared_ptr<Derivative<double, 3> > > gradop(3);
	for (std::size_t d = 0; d < 3; ++d) {
		gradop[d].reset(
				new Derivative<double, 3>(world, d,
						FunctionDefaults<3>::get_bc(),
						Function<double, 3>(), Function<double, 3>(),
						high_k));
	}

	// project the function to higher k grid
	real_function_3d f = project(x, high_k);
	f.set_thresh(high_thresh);
	f.refine();

	// apply laplacian
	real_function_3d empty = real_factory_3d(world);
	real_function_3d laplace_f = project(empty, high_k);
	laplace_f.set_thresh(high_thresh);
	for (size_t i = 0; i < gradop.size(); i++) {
		real_function_3d tmp = (*gradop[i])(f);
		real_function_3d tmp2 = (*gradop[i])(tmp);
		laplace_f += tmp2;
	}

	// project laplace_f back to the normal grid
	real_function_3d result = project(laplace_f,
			FunctionDefaults<3>::get_k());
	result.set_thresh(FunctionDefaults<3>::get_thresh());

	// debug and failsafe: make inverse of laplacian and apply
	real_convolution_3d G = BSHOperator<3>(world, 0.0, parameters.lo,
			parameters.thresh_bsh_3D);
	real_function_3d Gresult = -1.0 * G(result);
	real_function_3d difference = x - Gresult;
	double diff = difference.norm2();
	plot_plane(world, difference,
			"Laplacian_error_iteration_"
			+ stringify(performance_D.current_iteration));
	if (world.rank() == 0)
		std::cout << "Apply Laplace:\n" << "||x - G(Laplace(x))||=" << diff
		<< std::endl;
	if (diff > FunctionDefaults<6>::get_thresh())
		warning("Laplacian Error above 6D thresh");

	return result;
}

vecfuncT CC_Operators::apply_F(const CC_vecfunction &x) const {
	vecfuncT result;
	for(const auto itmp:x.functions){
		const CC_function& xi = itmp.second;
		result.push_back(apply_F(xi));
	}
	return result;
}

real_function_3d CC_Operators::apply_F(const CC_function &x) const {

	if (x.type == HOLE) {
		return get_orbital_energies()[x.i] * x.function;
	} else if (x.type == PARTICLE and not current_singles_potential.empty()) {
		const real_function_3d singles_potential = current_singles_potential[x.i-parameters.freeze];
		return (get_orbital_energies()[x.i] * x.function - singles_potential);
	} else if (x.type == MIXED and not current_singles_potential.empty()) {
		const real_function_3d singles_potential = current_singles_potential[x.i-parameters.freeze];
		return (get_orbital_energies()[x.i] * x.function - singles_potential); // for mixed: eps(i)*x.i = epsi*(moi + taui)
	} else {
		real_function_3d refined_x = copy(x.function).refine();
		// kinetic part
		CC_Timer T_time(world, "apply_T");
		std::vector < std::shared_ptr<real_derivative_3d> > gradop;
		gradop = gradient_operator<double, 3>(world);
		real_function_3d laplace_x = apply_laplacian(x.function);
		real_function_3d Tx = laplace_x.scale(-0.5).truncate();
		T_time.info();

		CC_Timer J_time(world, "apply_J");
		real_function_3d Jx = (intermediates_.get_hartree_potential()
				* x.function).truncate();
		J_time.info();

		CC_Timer K_time(world, "apply_K");
		real_function_3d Kx = K(x);
		K_time.info();

		CC_Timer U_time(world, "apply_U");
		real_function_3d U2x =
				(nemo.nuclear_correlation->U2() * x.function).truncate();
		real_function_3d U1x = real_factory_3d(world);
		for (size_t axis = 0; axis < 3; axis++) {
			const real_function_3d U1_axis = nemo.nuclear_correlation->U1(
					axis);
			const real_function_3d dx = (*gradop[axis])(x.function);
			U1x += (U1_axis * dx).truncate();
		}
		U_time.info();

		return (Tx + 2.0 * Jx - Kx + U2x + U1x).truncate();
	}
	error("apply_F: should not end up here");
	return real_factory_3d(world);
}

/// swap particles 1 and 2

/// param[in]	f	a function of 2 particles f(1,2)
/// return	the input function with particles swapped g(1,2) = f(2,1)
real_function_6d CC_Operators::swap_particles(const real_function_6d& f) const {
	CC_Timer timer_swap(world,"swap particles");
	// this could be done more efficiently for SVD, but it works decently
	std::vector<long> map(6);
	map[0] = 3;
	map[1] = 4;
	map[2] = 5;	// 2 -> 1
	map[3] = 0;
	map[4] = 1;
	map[5] = 2;	// 1 -> 2
	timer_swap.info();
	return mapdim(f, map);
}

// Calculate the CC2 energy equation which is
// \omega = \sum_{ij} 2<ij|g|\tau_{ij}> - <ij|g|\tau_{ji}> + 2 <ij|g|\tau_i\tau_j> - <ij|g|\tau_j\tau_i>
// with \tau_{ij} = u_{ij} + Q12f12|ij> + Q12f12|\tau_i,j> + Q12f12|i,\tau_j> + Q12f12|\tau_i\tau_j>
double CC_Operators::get_CC2_correlation_energy() const {
	MADNESS_EXCEPTION("get_cc2_correlation_energy not implemented yet",1);
	return 0.0;
}

double CC_Operators::compute_ccs_correlation_energy(const CC_function &taui, const CC_function &tauj)const{
	if(taui.i!=tauj.i)warning("ccs energy fock parts only defined for one orbital molecules");
	double omega = 0.0;
	// fock operator parts (zero when HF converged)
	double omega_f = 2.0*mo_bra_(taui.i).inner(apply_F(CC_function(taui.function,taui.i,UNDEFINED)));
	output("CCS Energy Fock part: 2.0*<i|F|taui>="+stringify(omega_f));
	//omega += 2.0*intermediates_.get_integrals_t1()(u.i,u.j,u.i,u.j); //<ij|g|\taui\tauj>
	omega += 2.0*make_ijgxy(taui.i,tauj.i,taui.function,tauj.function);
	//omega -= intermediates_.get_integrals_t1()(u.i,u.j,u.j,u.i);     //<ij|g|\tauj\taui>
	omega -= make_ijgxy(taui.i,tauj.i,tauj.function,taui.function);
	output("CCS Energy Coulomb part: 2.0<ij|g|\taui\tauj> - <ji|g|\taui\tauj>="+stringify(omega));
	return omega+omega_f;
}
double CC_Operators::compute_cc2_pair_energy(const CC_Pair &u,
		const CC_function &taui, const CC_function &tauj) const {
	double omega = 0.0;
	const size_t i = u.i;
	const size_t j = u.j;
	MADNESS_ASSERT(i==taui.i);
	MADNESS_ASSERT(j==tauj.i);
	double u_part = 0.0;
	double ij_part = 0.0;
	double mixed_part = 0.0;
	double titj_part = 0.0;
	double singles_part = 0.0;
	double tight_thresh = parameters.thresh_Ue;
	// Contribution from u itself, we will calculate <uij|g|ij> instead of <ij|g|uij> and then just make the inner product (see also mp2.cc)
	{
		real_function_6d coulomb = TwoElectronFactory(world).dcut(tight_thresh);
		real_function_6d g_ij = CompositeFactory<double, 6, 3>(world).particle1(copy(mo_bra_(i).function)).particle2(copy(mo_bra_(j).function)).g12(coulomb).thresh(tight_thresh);

		real_function_6d g_ji = CompositeFactory<double, 6, 3>(world).particle1(copy(mo_bra_(j).function)).particle2(copy(mo_bra_(i).function)).g12(coulomb).thresh(tight_thresh);
		const double uij_g_ij = inner(u.function, g_ij);
		const double uij_g_ji = inner(u.function, g_ji); // =uji_g_ij
		u_part = 2.0 * uij_g_ij - uij_g_ji;
	}
	// Contribution from the mixed f12(|\tau_i,j>+|i,\tau_j>) part
	{
		mixed_part += 2.0*make_ijgQfxy(u.i,u.j,mo_ket_(i),tauj);
		mixed_part += 2.0*make_ijgQfxy(u.i,u.j,taui,mo_ket_(j));
		mixed_part -= make_ijgQfxy(u.j,u.i,mo_ket_(i),tauj);
		mixed_part -= make_ijgQfxy(u.j,u.i,taui.function,mo_ket_(j));
	}
	// Contribution from the f12|ij> part, this should be calculated in the beginning
	{
		ij_part += (2.0*u.ij_gQf_ij - u.ji_gQf_ij );
	}
	// Contribution from the f12|\tau_i\tau_j> part
	{
		titj_part += 2.0*make_ijgQfxy(u.i,u.j,taui,tauj);
		titj_part -= make_ijgQfxy(u.i,u.j,tauj,taui);
	}
	// Singles Contribution
	{
		// I should use intermediates later because the t1 integrals are also needed for the CC2 potential
		//omega += 2.0*intermediates_.get_integrals_t1()(u.i,u.j,u.i,u.j); //<ij|g|\taui\tauj>
		singles_part += 2.0*make_ijgxy(u.i,u.j,taui.function,tauj.function);
		//omega -= intermediates_.get_integrals_t1()(u.i,u.j,u.j,u.i);     //<ij|g|\tauj\taui>
		singles_part -= make_ijgxy(u.i,u.j,tauj.function,taui.function);
	}

	omega = u_part + ij_part + mixed_part + titj_part + singles_part;
	if(world.rank()==0){
		std::cout << "\n\nEnergy Contributions to the correlation energy of pair " << i << j << "\n";
		std::cout << std::fixed << std::setprecision(parameters.output_prec);
		std::cout << "from   |u" << i << j << "            |: " << u_part << "\n";
		std::cout << "from Qf|HH" << i << j << "           |: " << ij_part << "\n";
		std::cout << "from Qf|HP" << i << j << "           |: " << mixed_part << "\n";
		std::cout << "from Qf|PPu" << i << j << "          |: " << titj_part << "\n";
		std::cout << "from   |tau" << i << ",tau" << j << "|: " << singles_part << "\n";
		std::cout << "all together = " << omega << "\n";
		std::cout << "\n\n";
	}
	return omega;
}

/// General Function to make the intergral <ij|gQf|xy>
double CC_Operators::make_ijgQfxy(const size_t &i, const size_t &j, const CC_function &x, const CC_function &y)const{
	// convenience
	const real_function_3d brai = mo_bra_(i).function;
	const real_function_3d braj = mo_bra_(j).function;
	// part 1, no projector: <ij|gf|xy>
	const real_function_3d jy = (braj*y.function).truncate();
	const real_function_3d ix = (brai*x.function).truncate();
	const real_function_3d jgfy = apply_gf(jy);
	const double part1 = ix.inner(jgfy);
	// part 2, projector on particle 1 <j|igm*mfx|y> = jy.inner(igm*mfx)
	double part2 = 0.0;
	for(const auto& mtmp:mo_ket_.functions){
		const CC_function& mom = mtmp.second;
		const size_t m = mtmp.first;
		const real_function_3d igm = apply_g12(mo_bra_(i),mom);
		const real_function_3d mfx = apply_f12(mo_bra_(m),x);
		part2 -= jy.inner(igm*mfx);
	}
	// part3, projector on particle 2 <i|jgn*nfy|x>
	double part3 = 0.0;
	for(const auto& ntmp:mo_ket_.functions){
		const CC_function& mon = ntmp.second;
		const size_t n = ntmp.first;
		const real_function_3d jgn = apply_g12(mo_bra_(j),mon);
		const real_function_3d nfy = apply_f12(mo_bra_(n),y);
		part2 -= ix.inner(jgn*nfy);
	}
	// part4, projector on both particles <ij|g|mn><mn|f|xy>
	double part4 = 0.0;
	for(const auto& mtmp:mo_ket_.functions){
		const CC_function& mom = mtmp.second;
		const size_t m = mtmp.first;
		const real_function_3d igm = apply_g12(mo_bra_(i),mom);
		const real_function_3d mfx = apply_f12(mo_bra_(m),x);
		for(const auto& ntmp:mo_ket_.functions){
			const CC_function& mon = ntmp.second;
			const size_t n = ntmp.first;
			const real_function_3d jn = braj*mon.function;
			const real_function_3d ny = mo_bra_(n).function*y.function;
			const double ijgmn = jn.inner(igm);
			const double mnfxy = ny.inner(mfx);
			part4 += ijgmn*mnfxy;
		}
	}

	return part1+part2+part3+part4;

	//	// Q12 = I12 - O1 - O2 + O12
	//	real_function_3d jy = mo_bra_(j).function*y;
	//	real_function_3d ix = mo_bra_(i).function*x;
	//	// I12 Part:
	//	double ijgfxy = (ix).inner(apply_gf(jy));
	//	// O1 Part
	//	double ijgO1fxy =0.0;
	//	for(size_t k=0;k<mo_ket_.size();k++){
	//		real_function_3d igk = intermediates_.get_EX(i,k);
	//		real_function_3d kfx = (*f12op)(mo_bra_(k).function*x);
	//		real_function_3d igkkfx = (igk*kfx).truncate();
	//		ijgO1fxy += jy.inner(igkkfx);
	//	}
	//	// O2 Part
	//	double ijgO2fxy =0.0;
	//	for(size_t k=0;k<mo_ket_.size();k++){
	//		real_function_3d jgk = intermediates_.get_EX(j,k);
	//		real_function_3d kfy = (*f12op)(mo_bra_(k).function*y);
	//		real_function_3d jgkkfy = (jgk*kfy).truncate();
	//		ijgO2fxy += ix.inner(jgkkfy);
	//	}
	//	// O12 Part
	//	double ijgO12fxy = 0.0;
	//	for(size_t k=0;k<mo_ket_.size();k++){
	//		real_function_3d igk = intermediates_.get_EX(i,k);
	//		real_function_3d kfx = (*f12op)(mo_bra_(k).function*x);
	//		for(size_t l=0;l<mo_ket_.size();l++){
	//			double ijgkl = igk.inner(mo_bra_(j).function*mo_ket_(l).function);
	//			double klfxy = kfx.inner(mo_bra_(l).function*y);
	//			ijgO12fxy += ijgkl*klfxy;
	//		}
	//	}
	//
	//	return (ijgfxy - ijgO1fxy - ijgO2fxy + ijgO12fxy);
}

double CC_Operators::make_ijgfxy(const size_t &i, const size_t &j, const real_function_3d &x, const real_function_3d &y)const{
	real_function_3d jy = mo_bra_(j).function*y;
	real_function_3d ix = mo_bra_(j).function*x;
	// I12 Part:
	double ijgfxy = (ix).inner(apply_gf(jy));
	return ijgfxy;
}

/// General Function to make the two electron integral <ij|g|xy>
/// For Debugging -> Expensive without intermediates
double CC_Operators::make_ijgxy(const size_t &i, const size_t &j, const real_function_3d &x, const real_function_3d &y)const{
	real_function_3d igx = (*poisson)(mo_bra_(i).function*x).truncate();
	real_function_3d jy = (mo_bra_(j).function*y).truncate();
	return jy.inner(igx);
}

double CC_Operators::make_integral(const size_t &i, const size_t &j, const CC_function &x,
		const CC_function&y) const {
	if (x.type == HOLE) {
		real_function_3d igx_y =
				(intermediates_.get_EX(i, x.i) * y.function).truncate();
		return mo_bra_(j).function.inner(igx_y);
	} else if (x.type == PARTICLE) {
		if (y.type == HOLE) {
			real_function_3d jgy_x = (intermediates_.get_EX(j, y.i)
					* x.function);
			return mo_bra_(i).function.inner(jgy_x);
		} else if (y.type == PARTICLE) {
			real_function_3d jgy_x = (intermediates_.get_pEX(j, y.i)
					* x.function);
			return mo_bra_(i).function.inner(jgy_x);
		}
	} else if (x.type == MIXED or y.type == MIXED) {
		real_function_3d igx =
				((*poisson)(mo_bra_(i).function * x.function)).truncate();
		double result = mo_bra_(j).function.inner(igx * y.function);
		return result;
	} else if (x.type == UNDEFINED or y.type == UNDEFINED) {
		real_function_3d igx =
				((*poisson)(mo_bra_(i).function * x.function)).truncate();
		double result = mo_bra_(j).function.inner(igx * y.function);
		return result;
	} else {
		error("ERROR in make_integrals ... should not end up here");
		return 0.0;
	}
	error("ERROR in make_integrals ... should not end up here");
	return 0.0;
}

/// General Function to make two electron integrals with pair functions (needed for energy)
double CC_Operators::make_ijgu(const size_t &i, const size_t &j, const CC_Pair &u)const{
	return make_ijgu(i,j,u.function);
}
double CC_Operators::make_ijgu(const size_t &i, const size_t &j, const real_function_6d &u)const{
	real_function_6d g = TwoElectronFactory(world).dcut(parameters.lo);
	real_function_6d ij_g =
			CompositeFactory<double, 6, 3>(world).particle1(
					copy(mo_bra_(i).function)).particle2(
							copy(mo_bra_(j).function)).g12(g);

	// compute < ij | g12 | u >
	const double ij_g_u = inner(u, ij_g);
	return ij_g_u;
}

/// General Function to make two electorn integrals with pair function and orbitals and the BSH Operator (needed for gf = g - bsh)
double CC_Operators::make_ijGu(const size_t &i, const size_t &j, const CC_Pair &u)const{
	real_function_6d G = TwoElectronFactory(world).BSH().gamma(corrfac.gamma()).dcut(parameters.lo);
	double bsh_prefactor = 4.0 * constants::pi;
	real_function_6d ij_G =
			CompositeFactory<double, 6, 3>(world).particle1(
					copy(mo_bra_(i).function)).particle2(
							copy(mo_bra_(j).function)).g12(G);

	// compute < ij | g12 | u >
	const double ij_G_u = inner(u.function, ij_G);
	return bsh_prefactor*ij_G_u;
}

real_function_3d CC_Operators::convolute_x_Qf_yz(const CC_function &x,
		const CC_function &y, const CC_function &z) const {

	const real_function_3d xfz = (*f12op)(x.function * z.function);
	const real_function_3d xfz_y = (xfz * y.function).truncate();
	const real_function_3d part1 = xfz * y.function;

	real_function_3d part2 = real_factory_3d(world);
	real_function_3d part3tmp = real_factory_3d(world);
	real_function_3d part4 = real_factory_3d(world);
	for (const auto& mtmp : mo_ket_.functions) {
		const CC_function& mom = mtmp.second;
		const double mxfyz = mo_bra_(mom).function.inner(xfz_y);
		part2 -= mxfyz * mom.function;

		const double xm = x.function.inner(mom.function);

		const real_function_3d mfz = apply_f12(mo_bra_(mom), z);
		const real_function_3d mfz_y = mfz * y.function;

		part3tmp -= xm * mfz;

		for (const auto& ntmp : mo_ket_.functions) {
			const CC_function& mon = ntmp.second;
			const double nmfyz = mo_bra_(mon).function.inner(mfz_y);
			part4 += xm * nmfyz * mon.function;
		}

	}
	const real_function_3d part3 = part3tmp * y.function;
	real_function_3d result = part1 + part2 + part3 + part4;
	result.truncate();
	return result;
}


/// apply the operator gf = 1/(2\gamma)*(Coulomb - 4\pi*BSH_\gamma)
/// works only if f = (1-exp(-\gamma*r12))/(2\gamma)
real_function_3d CC_Operators::apply_gf(const real_function_3d &f)const{
	double bsh_prefactor = 4.0 * constants::pi;
	double prefactor = 1.0/(2.0*corrfac.gamma());
	return prefactor*((*poisson)(f) - bsh_prefactor*(*fBSH)(f)).truncate();
}

real_function_6d CC_Operators::make_xy(const CC_function &x, const CC_function &y) const {
	double thresh = guess_thresh(x, y);
	if(thresh < parameters.thresh_6D) thresh = parameters.tight_thresh_6D;
	else thresh = parameters.thresh_6D;
	CC_Timer timer(world,
			"Making |" + x.name() + "," + y.name() + "> with 6D thresh="
			+ stringify(thresh));
	real_function_6d xy = CompositeFactory<double, 6, 3>(world).particle1(
			copy(x.function)).particle2(copy(y.function)).thresh(thresh);
	xy.fill_tree().truncate().reduce_rank();
	timer.info();
	return xy;
}

real_function_6d CC_Operators::make_f_xy(const CC_function &x,
		const CC_function &y) const {
	double thresh = guess_thresh(x, y);
	if(thresh < parameters.thresh_6D) thresh = parameters.tight_thresh_6D;
	else thresh = parameters.thresh_6D;
	CC_Timer timer(world,
			"Making f|" + x.name() + "," + y.name() + "> with 6D thresh="
			+ stringify(thresh));
	real_function_6d fxy = CompositeFactory<double, 6, 3>(world).g12(
			corrfac.f()).particle1(copy(x.function)).particle2(
					copy(y.function)).thresh(thresh);
	fxy.fill_tree().truncate().reduce_rank();
	timer.info();
	return fxy;
}

real_function_6d CC_Operators::make_f_xy_screened(const CC_function &x,
		const CC_function &y, const real_convolution_6d &screenG) const {
	double thresh = guess_thresh(x, y);
	if(thresh < parameters.thresh_6D) thresh = parameters.tight_thresh_6D;
	else thresh = parameters.thresh_6D;
	CC_Timer timer(world,
			"Making f|" + x.name() + "," + y.name() + "> with 6D thresh="
			+ stringify(thresh));
	real_function_6d fxy = CompositeFactory<double, 6, 3>(world).g12(
			corrfac.f()).particle1(copy(x.function)).particle2(
					copy(y.function)).thresh(thresh);
	fxy.fill_tree(screenG).truncate().reduce_rank();
	timer.info();
	return fxy;
}


/// Calculation is done in 4 steps over: Q12 = 1 - O1 - O2 + O12
/// 1. <x|f12|z>*|y>
/// 2. -\sum_m <x|m> <m|f12|z>*|y> = -(\sum_m <x|m> <m|f12|z>) *|y>
/// 3. -\sum_n <nx|f12|zy> * |n>
/// 4. +\sum_{mn} <x|n> <mn|f12|yz> * |m>
/// Metric from nuclear cusp is not taken into account -> give the right bra elements to the function
//real_function_3d CC_Operators::convolute_x_Qf_yz(const real_function_3d &x, const real_function_3d &y, const real_function_3d &z)const{
//	// make intermediates
//	vecfuncT moz = mul(world,z,mo_bra_);
//	vecfuncT moy = mul(world,y,mo_bra_);
//	// Do <x|f12|z>*|y>
//	real_function_3d part1_tmp = (*f12op)(x*z);
//	real_function_3d part1 = (part1_tmp*y).truncate();
//	// Do -\sum_m <x|m> <m|f12|z>*|y>
//	real_function_3d part2 = real_factory_3d(world);
//	{
//		Tensor<double> xm = inner(world,x,mo_ket_);
//		vecfuncT f12mz = apply(world,*f12op,moz);
//		real_function_3d tmp = real_factory_3d(world);
//		for(size_t m=0;m<mo_bra_.size();m++) tmp += xm[m]*f12mz[m];
//		part2 = tmp*y;
//	}
//	// Do -\sum_n <nx|f12|zy> * |n> |  <nx|f12|zy> = <n| xf12y |z>
//	real_function_3d part3 = real_factory_3d(world);
//	{
//		real_function_3d xf12y = (*f12op)((x*y).truncate());
//		for(size_t n=0;n<mo_bra_.size();n++){
//			double nxfzy = xf12y.inner(mo_bra_[n]*z);
//			part3 += nxfzy*mo_ket_[n];
//		}
//	}
//	// Do +\sum_{mn} <x|n> <mn|f12|yz> * |m>
//	real_function_3d part4 = real_factory_3d(world);
//	{
//		Tensor<double> xn = inner(world,x,mo_ket_);
//		vecfuncT nf12z = apply(world,*f12op,moz);
//		Tensor<double> mnfyz = inner(world,moy,nf12z);
//		for(size_t m=0;m<mo_bra_.size();m++){
//			for(size_t n=0;n<mo_ket_.size();n++){
//				part4 += xn(n)*mnfyz(m,n)*mo_ket_[m];
//			}
//		}
//	}
//	real_function_3d result = part1 - part2 - part3 + part4;
//	result.truncate();
//
//	if(parameters.debug){
//		CC_Timer function_debug(world,"Debug-Time for <k|Qf|xy>");
//		real_function_6d test_tmp = CompositeFactory<double,6,3>(world).g12(corrfac.f()).particle1(copy(y)).particle2(copy(z));
//		test_tmp.fill_tree().truncate().reduce_rank();
//		real_function_6d test_1 = copy(test_tmp);
//		real_function_6d test_4 = copy(Q12(test_tmp));
//		real_function_3d test1 = test_1.project_out(x,1);
//		real_function_3d test4 = test_4.project_out(x,1);
//		double check1 = (test1 - part1).norm2();
//		double check4 = (test4 - result).norm2();
//		if(world.rank()==0) std::cout << std::setprecision(parameters.output_prec) << "<k|Qf|xy> debug, difference to check1 value is: " << check1 << std::endl;
//		if(world.rank()==0) std::cout << std::setprecision(parameters.output_prec) << "<k|Qf|xy> debug, difference to check4 value is: " << check4 << std::endl;
//		if(check1 > FunctionDefaults<6>::get_thresh()) warning("<k|Qf|xy> check1 failed");
//		if(check4 > FunctionDefaults<6>::get_thresh()) warning("<k|Qf|xy> check4 failed");
//		function_debug.info();
//	}
//
//	return result;
//}

void CC_Operators::test_singles_potential(){

	output_section("Singles Potential Consistency Check with r*|i> singles and Q12f12|ij> doubles");
	// make test singles from mos: |taui> = r*|i>
	// make test doubles from mos: |uij>  = Q12f12|titj>
	real_function_3d r = real_factory_3d(world).f(f_r);
	vecfuncT singles_tmp;
	for(size_t i=parameters.freeze;i<mo_ket_.size();i++){
		real_function_3d tmp = r*mo_ket_(i).function;
		Q(tmp);
		double norm = tmp.norm2();
		tmp.scale(1.0/norm);
		tmp.scale(0.5);
		tmp.print_size("TestSingle: r|" + stringify(i) + ">" );
		singles_tmp.push_back(tmp);
	}
	CC_vecfunction singles(singles_tmp,PARTICLE,parameters.freeze);
	Pairs<CC_Pair> doubles = make_reg_residues(singles);

	update_intermediates(singles);

	CC_data dummy;

	output("\n\n Checking u-parts and r-parts of singles potentials with doubles\n\n");
	const potentialtype_s u_parts_tmp[] = {pot_S4a_u_, pot_S4b_u_, pot_S4c_u_, pot_S2b_u_, pot_S2c_u_};
	const potentialtype_s r_parts_tmp[] = {pot_S4a_r_, pot_S4b_r_, pot_S4c_r_, pot_S2b_r_, pot_S2c_r_};
	std::vector<std::pair<std::string,double> > results;
	for(size_t pot=0;pot<5;pot++){
		const potentialtype_s current_u = u_parts_tmp[pot];
		const potentialtype_s current_r = r_parts_tmp[pot];
		const std::string name = assign_name(current_u);
		double largest_error=0.0;
		output("\n\nConsistency Check of Singles Potential " + assign_name(current_u) + " with " + assign_name(current_r));
		const vecfuncT u = potential_singles(doubles,singles,current_u);
		const vecfuncT r = potential_singles(doubles,singles,current_r);
		const vecfuncT diff = sub(world,u,r);
		const double normdiff = norm2(world,u)-norm2(world,r);
		if(world.rank()==0) std::cout<< std::setw(20) << "||"+assign_name(current_u)+"||-||"+assign_name(current_r)+"||" << std::setfill(' ') << "=" << normdiff << std::endl;
		for(const auto d:diff){
			const double norm = d.norm2();
			if(norm>largest_error) largest_error=norm;
			if(world.rank()==0) std::cout<< std::setw(20) << "||"+assign_name(current_u)+"-"+assign_name(current_r)+"||" << std::setfill(' ') << "=" << norm << std::endl;
		}
		results.push_back(std::make_pair(name,largest_error));
		if(current_u == pot_S2b_u_){
			output("Making Integration Test for S2b potential:");
			// integrate the s2b potential against a function which not in the hole space = \sum_k 2<X,k|g|uik> - <k,X|g|uik>, with X=QX
			real_function_3d X = real_factory_3d(world);
			for(const auto&s: singles.functions){
				X+=s.second.function;
			}
			Q(X);
			X=X*nemo.nuclear_correlation -> square();
			Tensor<double> xs2b = inner(world,X,u);
			Tensor<double> xs2b_reg = inner(world,X,r);
			std::vector<double> control_6D;
			for(auto& itmp:singles.functions){
				const size_t i=itmp.first;
				double controli_6D=0.0;
				for(auto& ktmp:singles.functions){
					const size_t k=ktmp.first;
					real_function_6d g = TwoElectronFactory(world).dcut(parameters.lo);
					real_function_6d Xk_g =CompositeFactory<double, 6, 3>(world).particle1(copy(X)).particle2(copy(mo_bra_(k).function)).g12(g);
					real_function_6d g2 = TwoElectronFactory(world).dcut(parameters.lo);
					real_function_6d kX_g =CompositeFactory<double, 6, 3>(world).particle1(copy(mo_bra_(k).function)).particle2(copy(X)).g12(g2);
					const double tmp_6D = 2.0*Xk_g.inner(get_pair_function(doubles,i,k)) - kX_g.inner(get_pair_function(doubles,i,k));
					controli_6D += tmp_6D;
				}
				control_6D.push_back(controli_6D);
			}
			for(size_t i=0;i<control_6D.size();i++){
				const double diff = xs2b(i) - control_6D[i];
				const double diff2= xs2b_reg(i) - control_6D[i];
				std::cout << "||diffu||_" << i << "=" << fabs(diff) << std::endl;
				std::cout << "||diffr||_" << i << "=" << fabs(diff2) << std::endl;
				if(fabs(diff)>FunctionDefaults<6>::get_thresh()) warning("Integration Test of S2b failed !!!!!");
			}
		}
	}

	bool problems=false;
	for(const auto res:results){
		std::string status = "... passed!";
		if(res.second > FunctionDefaults<6>::get_thresh()){
			status="... failed!";
			problems=true;
		}
		if(world.rank()==0) std::cout << std::setw(10) << res.first << status << " largest error was " << res.second <<std::endl;
	}
	if(problems) warning("Inconsistencies in Singles Potential detected!!!!");
	else output("Singles Potentials seem to be consistent");
	output("\n\n Ending Testing Section\n\n");
}




}
