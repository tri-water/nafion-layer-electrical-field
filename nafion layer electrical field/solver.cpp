#include "solver.h"
#include <vector>
#include <iostream>

typedef Eigen::Triplet<double> Tt;

solver::solver(mesh& fmembrane, mesh& fsolution, const IonSystem& fmembraneIons, const IonSystem& fsolutionIons, 
	PotentialSignal& fSignal, const nernst_equation& fThermo, const ElectrodeReaction& fElecR, 
	const InterfaceReaction& fCationTransR, const InterfaceReaction& fProductTransR, const InterfaceReaction& fReactantTransR) :
	membrane(fmembrane), solution(fsolution), membraneIons(fmembraneIons), solutionIons(fsolutionIons), Signal(fSignal), Thermo(fThermo), 
	ElecR(fElecR), CationTransR(fCationTransR), ProductTransR(fProductTransR), ReactantTransR(fReactantTransR),
	MatrixLen(fmembrane.m*fmembrane.n*4 + fsolution.m*fsolution.n*5),
	MatrixA(MatrixLen, MatrixLen),
	arrayb(MatrixLen),
	dX(MatrixLen),
	X(MatrixLen),
	F(MatrixLen),
	MemEquationCoefficient(fmembraneIons, fmembrane, fSignal, fThermo),
	SolEquationCoefficient(fsolutionIons, fsolution, fSignal, fThermo)

{
}

void solver::initialise()
{
	/*
	A0 = -1/(dZ1*dZ2)
	A1 = 1/[dZ1(dZ1 + dZ2)], A2 = 1/[dZ2(dZ1 + dZ2)];
	A1_ = 1/[(dZ1 + dZ2)^2], A2_ = 1/[dZ2(dZ1 + dZ2)], A3_ = 1/[dZ1(dZ1 + dZ2)]

	B0 = -1/(dR1*dR2)
	B1 = 1/[dR1(dR1 + dR2)] - 1/R*1/(dR1 + dR2), B2 = 1/[dR2(dR1 + dR2)] + 1/R*1/(dR1 + dR2)
	B1_ = 1/[(dR1 + dR2)^2], B2_ = 1/[dR2(dR1 + dR2)] + 1/[R(dR1 + dR2)], B3_ = 1/[dR1(dR1 + dR2)] - 1/[R(dR1 + dR2)]
	*/

	Eigen::MatrixXd GeoCoeffMemA(7, membrane.n);
	Eigen::MatrixXd GeoCoeffSolA(7, solution.n);
	Eigen::MatrixXd GeoCoeffMemB(7, membrane.m);
	Eigen::MatrixXd GeoCoeffSolB(7, solution.m);
	// membrane
	GeoCoefficientA(membrane, GeoCoeffMemA);
	GeoCoefficientB(membrane, GeoCoeffMemB);
	// solution
	GeoCoefficientA(solution, GeoCoeffSolA);
	GeoCoefficientB(solution, GeoCoeffSolB);

	// initialise equation coefficients
	MemEquationCoefficient.CalculateCoeff(GeoCoeffMemA, GeoCoeffMemB);
	SolEquationCoefficient.CalculateCoeff(GeoCoeffSolA, GeoCoeffSolB);

	// initialise variable vector X
	initialiseX();

	// initialise MatrixA

}

void solver::GeoCoefficientA(mesh& phase, Eigen::MatrixXd& GeoCoeffA) const
{
	double dZ1, dZ2;
	for (unsigned i = 0; i < phase.n; ++i) {
		if (i == 0) {
			dZ1 = 0.5*(phase.dz0 + phase.dz);
			dZ2 = 0.5*(phase.dz0 + phase.dz);
		}
		else if (i == 1) {
			dZ1 = 0.5*(phase.dz0 + phase.dz);
			dZ2 = phase.dz;
		}
		else {
			dZ1 = phase.dz;
			dZ2 = phase.dz;
		}

		GeoCoeffA(0, i) = -1 / (dZ1*dZ2);
		GeoCoeffA(1, i) = 1 / (dZ1*(dZ1 + dZ2));
		GeoCoeffA(2, i) = 1 / (dZ2*(dZ1 + dZ2));
		GeoCoeffA(3, i) = -1 / (dZ1*dZ2);
		GeoCoeffA(4, i) = 1 / ((dZ1 + dZ2)*(dZ1 + dZ2));
		GeoCoeffA(5, i) = 1 / (dZ2*(dZ1 + dZ2));
		GeoCoeffA(6, i) = i, 1 / (dZ1*(dZ1 + dZ2));
	}
}

void solver::GeoCoefficientB(mesh& phase, Eigen::MatrixXd& GeoCoeffB) const
{
	double dR1, dR2, R;
	for (unsigned long i = 0; i < phase.m; ++i) {
		R = phase.RR(0, i);
		if (i == 0) {
			dR1 = 0.5*(phase.dr0 + phase.dr);
			dR2 = 0.5*(phase.dr0 + phase.dr);
		}
		else if (i == 1) {
			dR1 = 0.5*(phase.dr0 + phase.dr);
			dR2 = phase.dr;
		}
		else{
			dR1 = phase.dr;
			dR2 = phase.dr;
		}

		GeoCoeffB(0, i) = -1 / (dR1*dR2);
		GeoCoeffB(1, i) = 1 / (dR1*(dR1 + dR2)) - 1 / R * 1 / (dR1 + dR2);
		GeoCoeffB(2, i) = 1 / (dR2*(dR1 + dR2)) + 1 / R * 1 / (dR1 + dR2);
		GeoCoeffB(3, i) = -1 / (dR1*dR2);
		GeoCoeffB(4, i) = 1 / ((dR1 + dR2) * (dR1 + dR2));
		GeoCoeffB(5, i) = 1 / (dR2*(dR1 + dR2)) + 1 / (R*(dR1 + dR2));
		GeoCoeffB(6, i) = 1 / (dR1*(dR1 + dR2)) - 1 / (R*(dR1 + dR2));
	}
}

void solver::initialiseX()
{
	unsigned long nxiplusj = 0UL;
	unsigned long mxn = membrane.Getmxn();

#pragma omp parallel for private(nxiplusj)
	for (unsigned long int i = 0; i < membrane.m; ++i) {
		for (unsigned long j = 0; j < membrane.n; ++j) {

			nxiplusj = membrane.n*i + j;
			//Reactant
			X(nxiplusj) = membrane.Cren(j, i);
			//Product
			X(nxiplusj + mxn) = membrane.Cprn(j, i);
			/*
			//Anion
			X(nxiplusj + 2 * mxn) = membrane.Cann(j, i);
			*/
			//Cation
			X(nxiplusj + 2 * mxn) = membrane.Ccan(j, i);
			//Potential
			X(nxiplusj + 3 * mxn) = membrane.Ptln(j, i);
		}
	}

	unsigned long int mxnx4 = 4 * mxn;
	mxn = solution.Getmxn();

#pragma omp parallel for private(nxiplusj)
	for (unsigned long i = 0; i < solution.m; ++i) {
		for (unsigned long j = 0; j < solution.n; ++j) {
			
			nxiplusj = solution.n*i + j;
			//Reactant
			X(nxiplusj + mxnx4) = solution.Cren(j, i);
			//Product
			X(nxiplusj + 1 * mxn + mxnx4) = solution.Cprn(j, i);
			//Anion
			X(nxiplusj + 2 * mxn + mxnx4) = solution.Cann(j, i);
			//Cation
			X(nxiplusj + 3 * mxn + mxnx4) = solution.Ccan(j, i);
			//Potential
			X(nxiplusj + 4 * mxn + mxnx4) = solution.Ptln(j, i);
		}
	}
}

void solver::CalculateF()
{
	// define reference
	auto& M = MemEquationCoefficient;
	auto& S = SolEquationCoefficient;
	auto& MI = membraneIons;
	auto& SI = solutionIons;
	//Reactant index
	unsigned long rea_j_i = 0UL;
	unsigned long rea_jm1_i = 0UL;
	unsigned long rea_jp1_i = 0UL;
	unsigned long rea_j_im1 = 0UL;
	unsigned long rea_j_ip1 = 0UL;
	//Product index
	unsigned long pro_j_i = 0UL;
	unsigned long pro_jm1_i = 0UL;
	unsigned long pro_jp1_i = 0UL;
	unsigned long pro_j_im1 = 0UL;
	unsigned long pro_j_ip1 = 0UL;
	//Anion index
	unsigned long ani_j_i = 0UL;
	unsigned long ani_jm1_i = 0UL;
	unsigned long ani_jp1_i = 0UL;
	unsigned long ani_j_im1 = 0UL;
	unsigned long ani_j_ip1 = 0UL;
	//Cation index
	unsigned long cat_j_i = 0UL;
	unsigned long cat_jm1_i = 0UL;
	unsigned long cat_jp1_i = 0UL;
	unsigned long cat_j_im1 = 0UL;
	unsigned long cat_j_ip1 = 0UL;
	//Potential index
	unsigned long pot_j_i = 0UL;
	unsigned long pot_jm1_i = 0UL;
	unsigned long pot_jp1_i = 0UL;
	unsigned long pot_j_im1 = 0UL;
	unsigned long pot_j_ip1 = 0UL;

	unsigned long mxn = membrane.Getmxn();

	// Calculate membrane
#pragma omp parallel for private(rea_j_i, rea_jm1_i, rea_jp1_i, rea_j_im1, rea_j_ip1, pro_j_i, pro_jm1_i, pro_jp1_i, pro_j_im1, X(pro_j_ip1), ani_j_i, ani_jm1_i, ani_jp1_i, ani_j_im1, ani_j_ip1,cat_j_i, cat_jm1_i, cat_jp1_i, cat_j_im1, cat_j_ip1, pot_j_i, pot_jm1_i, pot_jp1_i, pot_j_im1, pot_j_ip1)
	for (unsigned long i = 0; i < membrane.m - 1; ++i) {
		for (unsigned long j = 0; j < membrane.n - 1; ++j) {
			// Reactant index
			rea_j_i = membrane.n*i + j;
			rea_jm1_i = rea_j_i - 1;
			rea_jp1_i = rea_j_i + 1;
			rea_j_im1 = rea_j_i - membrane.n;
			rea_j_ip1 = rea_j_i + membrane.n;
			//Product index
			pro_j_i = rea_j_i + mxn;
			pro_jm1_i = pro_j_i - 1;
			pro_jp1_i = pro_j_i + 1;
			pro_j_im1 = pro_j_i - membrane.n;
			pro_j_ip1 = pro_j_i + membrane.n;
			/*
			//Anion index
			ani_j_i = pro_j_i + mxn;
			ani_jm1_i = ani_j_i - 1;
			ani_jp1_i = ani_j_i + 1;
			ani_j_im1 = ani_j_i - membrane.n;
			ani_j_ip1 = ani_j_i + membrane.n;
			*/
			//Cation index
			cat_j_i = ani_j_i + mxn;
			cat_jm1_i = cat_j_i - 1;;
			cat_jp1_i = cat_j_i + 1;
			cat_j_im1 = cat_j_i - membrane.n;
			cat_j_ip1 = cat_j_i + membrane.n;
			//Potential index
			pot_j_i = cat_j_i + mxn;
			pot_jm1_i = pot_j_i - 1;
			pot_jp1_i = pot_j_i + 1;
			pot_j_im1 = pot_j_i - membrane.n;
			pot_j_ip1 = pot_j_i + membrane.n;

			if (i > 1 && j > 1 && i < membrane.m - 1 && i < membrane.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_jm1_i), X(rea_j_ip1), X(rea_j_im1), 
							   X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_jm1_i), X(pro_j_ip1), X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);
				/*
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_jm1_i), X(ani_j_ip1), X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_jm1_i), X(cat_j_ip1), X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffPotentialA, M.CoeffPotentialB, MI);

			}
			else if (j == 0 && i > 1 && i < membrane.m - 1) {
				
				double DrivingPotential = ElecR.DrivingPotential((X(pot_jp1_i) - X(pot_j_i)) / membrane.dz);
				double kf = ElecR.kf(DrivingPotential);
				double kb = ElecR.kb(DrivingPotential);
				double reactionRate = kf*X(pro_j_i) - kb*X(rea_j_i); // production is O, reatant is R

				// Reactant:
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_j_i), X(rea_j_ip1), X(rea_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);
				F(rea_j_i) += reactionRate / membrane.dz*Signal.dt;
				// Product:
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_j_i), X(pro_j_ip1), X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);
				F(pro_j_i) -= reactionRate / membrane.dz*Signal.dt;
				/*
				// Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_j_i), X(ani_j_ip1), X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				// Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_j_i), X(cat_j_ip1), X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				// Potential
				F(pot_j_i) = Signal.AppliedPotential() - X(pot_j_i) - ElecR.E_formal - DrivingPotential;
			}
			else if (i == 0 && j > 0 && j < membrane.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_jm1_i), X(rea_j_ip1), X(rea_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_jm1_i), X(pro_j_ip1), X(pro_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);
				/*
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_jm1_i), X(ani_j_ip1), X(ani_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_jm1_i), X(cat_j_ip1), X(cat_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffPotentialA, M.CoeffPotentialB, MI);
			}
			else if (j == membrane.n - 1 && i > 0 && i < membrane.m - 1) {
				// index in the solution phase
				// Reactant index
				unsigned long srea_jp1_i = solution.n*i + 4 * membrane.Getmxn();
				//Product index
				unsigned long spro_jp1_i = srea_jp1_i + mxn;
				/*
				//Anion index
				unsigned long sani_jp1_i = spro_jp1_i + mxn;
				*/
				//Cation index
				unsigned long scat_jp1_i = spro_jp1_i + 2*mxn;
				//Potential index
				unsigned long spot_jp1_i = scat_jp1_i + mxn;

				double dE = X(spot_jp1_i) - X(pot_j_i);
				//Cation transfer rate
				double kf = CationTransR.kf(dE);
				double kb = CationTransR.kb(dE);
				double CationTransRate = kf*X(scat_jp1_i) - kb*X(cat_j_i);
				//Product transfer rate
				kf = ProductTransR.kf(dE);
				kb = ProductTransR.kb(dE);
				double ProductTransRate = kf*X(spro_jp1_i) - kb*X(pro_j_i);
				//Reactant transfer rate
				kf = ReactantTransR.kf(0);
				kb = ReactantTransR.kb(0);
				double ReactantTransRate = kf*X(srea_jp1_i) - kb*X(rea_j_i);

				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_j_i), X(rea_jm1_i), X(rea_j_ip1), X(rea_j_im1),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);
				F(rea_j_i) += ReactantTransRate / membrane.dz*Signal.dt;
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_j_i), X(pro_jm1_i), X(pro_j_ip1), X(pro_j_im1),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);
				F(pro_j_i) += ProductTransRate / membrane.dz*Signal.dt;
				/*
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_j_i), X(ani_jm1_i), X(ani_j_ip1), X(ani_j_im1),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_j_i), X(cat_jm1_i), X(cat_j_ip1), X(cat_j_im1),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				F(cat_j_i) += CationTransRate/membrane.dz*Signal.dt;
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i, 
					X(cat_j_i)), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffPotentialA, M.CoeffPotentialB, MI);


			}
			else if (i == membrane.m - 1 && j > 0 && j < membrane.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_jm1_i), X(rea_j_i), X(rea_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_jm1_i), X(pro_j_i), X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);
				/*
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_jm1_i), X(ani_j_i), X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_jm1_i), X(cat_j_i), X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffPotentialA, M.CoeffPotentialB, MI);
			}
			else if (i == 0 && j == 0) {
				
				double DrivingPotential = ElecR.DrivingPotential((X(pot_jp1_i) - X(pot_j_i)) / membrane.dz);
				double kf = ElecR.kf(DrivingPotential);
				double kb = ElecR.kb(DrivingPotential);
				double reactionRate = kf*X(pro_j_i) - kb*X(rea_j_i); // production is O, reatant is R
				// Reactant:
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_j_i), X(rea_j_ip1), X(rea_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_i), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);

				F(rea_j_i) += reactionRate / membrane.dz*Signal.dt;
				// Product:
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_j_i), X(pro_j_ip1), X(pro_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_i), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);

				F(pro_j_i) -= reactionRate / membrane.dz*Signal.dt;
				/*
				// Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_j_i), X(ani_j_ip1), X(ani_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_i), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				// Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_j_i), X(cat_j_ip1), X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				// Potential
				F(pot_j_i) = Signal.AppliedPotential() - X(pot_j_i) - ElecR.E_formal - DrivingPotential;
			}
			else if (i == membrane.m - 1 && j == 0) {
				// Reactant:
				double DrivingPotential = ElecR.DrivingPotential((X(pot_jp1_i) - X(pot_j_i)) / membrane.dz);
				double kf = ElecR.kf(DrivingPotential);
				double kb = ElecR.kb(DrivingPotential);
				double reactionRate = kf*X(pro_j_i) - kb*X(rea_j_i); // production is O, reatant is R

				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_j_i), X(rea_j_i), X(rea_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_i), X(pot_j_im1), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);
				F(rea_j_i) += reactionRate / membrane.dz*Signal.dt;
				// Product:
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_j_i), X(pro_j_i), X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_i), X(pot_j_im1), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);
				F(pro_j_i) -= reactionRate / membrane.dz*Signal.dt;
				/*
				// Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_j_i), X(ani_j_i), X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_i), X(pot_j_im1), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				// Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_j_i), X(cat_j_i), X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_i), X(pot_j_im1), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				// Potential
				F(pot_j_i) = Signal.AppliedPotential() - X(pot_j_i) - ElecR.E_formal - DrivingPotential;
			}
			else if (j == membrane.n - 1 && i == 0) {
				unsigned long srea_jp1_i = solution.n*i + 4 * membrane.Getmxn();
				//Product index
				unsigned long spro_jp1_i = srea_jp1_i + mxn;
				/*
				//Anion index
				unsigned long sani_jp1_i = spro_jp1_i + mxn;
				*/
				//Cation index
				unsigned long scat_jp1_i = spro_jp1_i + 2*mxn;
				//Potential index
				unsigned long spot_jp1_i = scat_jp1_i + mxn;

				double dE = X(spot_jp1_i) - X(pot_j_i);
				//Cation transfer rate
				double kf = CationTransR.kf(dE);
				double kb = CationTransR.kb(dE);
				double CationTransRate = kf*X(scat_jp1_i) - kb*X(cat_j_i);
				//Product transfer rate
				kf = ProductTransR.kf(dE);
				kb = ProductTransR.kb(dE);
				double ProductTransRate = kf*X(spro_jp1_i) - kb*X(pro_j_i);
				//Reactant transfer rate
				kf = ReactantTransR.kf(0);
				kb = ReactantTransR.kb(0);
				double ReactantTransRate = kf*X(srea_jp1_i) - kb*X(rea_j_i);

				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_j_i), X(rea_jm1_i), X(rea_j_ip1), X(rea_j_i),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);
				F(rea_j_i) += ReactantTransRate / membrane.dz*Signal.dt;
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_j_i), X(pro_jm1_i), X(pro_j_ip1), X(pro_j_i),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);
				F(pro_j_i) += ProductTransRate / membrane.dz*Signal.dt;
				/*
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_j_i), X(ani_jm1_i), X(ani_j_ip1), X(ani_j_i),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_j_i), X(cat_jm1_i), X(cat_j_ip1), X(cat_j_i),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				F(cat_j_i) += CationTransRate / membrane.dz*Signal.dt;
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i,
					X(cat_j_i)), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), M.CoeffPotentialA, M.CoeffPotentialB, MI);
			}
			else if (j == membrane.n - 1 && i == membrane.m - 1) {
				unsigned long srea_jp1_i = solution.n*i + 4 * membrane.Getmxn();
				//Product index
				unsigned long spro_jp1_i = srea_jp1_i + mxn;
				/*
				//Anion index
				unsigned long sani_jp1_i = spro_jp1_i + mxn;
				*/
				//Cation index
				unsigned long scat_jp1_i = spro_jp1_i + mxn;
				//Potential index
				unsigned long spot_jp1_i = scat_jp1_i + mxn;

				double dE = X(spot_jp1_i) - X(pot_j_i);
				//Cation transfer rate
				double kf = CationTransR.kf(dE);
				double kb = CationTransR.kb(dE);
				double CationTransRate = kf*X(scat_jp1_i) - kb*X(cat_j_i);
				//Product transfer rate
				kf = ProductTransR.kf(dE);
				kb = ProductTransR.kb(dE);
				double ProductTransRate = kf*X(spro_jp1_i) - kb*X(pro_j_i);
				//Reactant transfer rate
				kf = ReactantTransR.kf(0);
				kb = ReactantTransR.kb(0);
				double ReactantTransRate = kf*X(srea_jp1_i) - kb*X(rea_j_i);

				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_j_i), X(rea_jm1_i), X(rea_j_i), X(rea_j_im1),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffReactantA, M.CoeffReactantB, membrane.Cren);
				F(rea_j_i) += ReactantTransRate / membrane.dz*Signal.dt;
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_j_i), X(pro_jm1_i), X(pro_j_i), X(pro_j_im1),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffProductA, M.CoeffProductB, membrane.Cprn);
				F(pro_j_i) += ProductTransRate / membrane.dz*Signal.dt;
				/*
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_j_i), X(ani_jm1_i), X(ani_j_i), X(ani_j_im1),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffAnionA, M.CoeffAnionB, membrane.Cann);
					*/
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_j_i), X(cat_jm1_i), X(cat_j_i), X(cat_j_im1),
					X(pot_j_i), X(pot_j_i), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffCationA, M.CoeffCationB, membrane.Ccan);
				F(cat_j_i) += CationTransRate / membrane.dz*Signal.dt;
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i,
					X(cat_j_i)), X(pot_jm1_i), X(pot_j_i), X(pot_j_im1), M.CoeffPotentialA, M.CoeffPotentialB, MI);
			}
		}
	}

	mxn = solution.Getmxn();
	// Solution
	for (unsigned long i = 0; i < solution.m - 1; ++i) {
		for (unsigned long j = 0; j < solution.n - 1; ++j) {

			// Reactant index
			rea_j_i = solution.n*i + j + 4*membrane.Getmxn();
			rea_jm1_i = rea_j_i - 1;
			rea_jp1_i = rea_j_i + 1;
			rea_j_im1 = rea_j_i - solution.n;
			rea_j_ip1 = rea_j_i + solution.n;
			//Product index
			pro_j_i = rea_j_i + mxn;
			pro_jm1_i = pro_j_i - 1;
			pro_jp1_i = pro_j_i + 1;
			pro_j_im1 = pro_j_i - solution.n;
			pro_j_ip1 = pro_j_i + solution.n;
			//Anion index
			ani_j_i = pro_j_i + mxn;
			ani_jm1_i = ani_j_i - 1;
			ani_jp1_i = ani_j_i + 1;
			ani_j_im1 = ani_j_i - solution.n;
			ani_j_ip1 = ani_j_i + solution.n;
			//Cation index
			cat_j_i = ani_j_i + mxn;
			cat_jm1_i = cat_j_i - 1;;
			cat_jp1_i = cat_j_i + 1;
			cat_j_im1 = cat_j_i - solution.n;
			cat_j_ip1 = cat_j_i + solution.n;
			//Potential index
			pot_j_i = cat_j_i + mxn;
			pot_jm1_i = pot_j_i - 1;
			pot_jp1_i = pot_j_i + 1;
			pot_j_im1 = pot_j_i - solution.n;
			pot_j_ip1 = pot_j_i + solution.n;

			if (i > 0 && i < solution.m - 1 && j > 0 && j < solution.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_jm1_i), X(rea_j_ip1), X(rea_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_jm1_i), X(pro_j_ip1), X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_jm1_i), X(ani_j_ip1), X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_jm1_i), X(cat_j_ip1), X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (i > 0 && i < solution.m - 1 && j == solution.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), SI.Reactant.Cinitial, X(rea_jm1_i), X(rea_j_ip1), X(rea_j_im1),
					X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), SI.Product.Cinitial, X(pro_jm1_i), X(pro_j_ip1), X(pro_j_im1),
					X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), SI.SupportAnion.Cinitial, X(ani_jm1_i), X(ani_j_ip1), X(ani_j_im1),
					X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), SI.SupportCation.Cinitial, X(cat_jm1_i), X(cat_j_ip1), X(cat_j_im1),
					X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (i == 0 && j > 0 && j < solution.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_jm1_i), X(rea_j_ip1), X(rea_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_jm1_i), X(pro_j_ip1), X(pro_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_jm1_i), X(ani_j_ip1), X(ani_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_jm1_i), X(cat_j_ip1), X(cat_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (i == solution.m - 1 && j > 0 && j < solution.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_jm1_i), SI.Reactant.Cinitial, X(rea_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_jm1_i), SI.Product.Cinitial, X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_jm1_i), SI.SupportAnion.Cinitial, X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_jm1_i), SI.SupportAnion.Cinitial, X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (i == 0 && j == solution.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), SI.Reactant.Cinitial, X(rea_jm1_i), X(rea_j_ip1), X(rea_j_i),
					X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), SI.Product.Cinitial, X(pro_jm1_i), X(pro_j_ip1), X(pro_j_i),
					X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), SI.SupportAnion.Cinitial, X(ani_jm1_i), X(ani_j_ip1), X(ani_j_i),
					X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), SI.SupportCation.Cinitial, X(cat_jm1_i), X(cat_j_ip1), X(cat_j_i),
					X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), 0, X(pot_jm1_i), X(pot_j_ip1), X(pot_j_i), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (i = solution.m - 1 && j == solution.n - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), SI.Reactant.Cinitial, X(rea_jm1_i), SI.Reactant.Cinitial, X(rea_j_im1),
					X(pot_j_i), 0, X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), SI.Product.Cinitial, X(pro_jm1_i), SI.Product.Cinitial, X(pro_j_im1),
					X(pot_j_i), 0, X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), SI.SupportAnion.Cinitial, X(ani_jm1_i), SI.SupportAnion.Cinitial, X(ani_j_im1),
					X(pot_j_i), 0, X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), SI.SupportCation.Cinitial, X(cat_jm1_i), SI.SupportCation.Cinitial, X(cat_j_im1),
					X(pot_j_i), 0, X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), 0, X(pot_jm1_i), 0, X(pot_j_im1), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (i > 0 && i < membrane.m && j == 0) {

				// index in the membrane phase
				// Reactant index
				unsigned long mrea_jm1_i = membrane.n*i + membrane.n - 1;
				//Product index
				unsigned long mpro_jm1_i = mrea_jm1_i + membrane.Getmxn();
				/*
				//Anion index
				unsigned long mani_jm1_i = mpro_jm1_i + membrane.Getmxn();
				*/
				//Cation index
				unsigned long mcat_jm1_i = mpro_jm1_i + membrane.Getmxn();
				//Potential index
				unsigned long mpot_jm1_i = mcat_jm1_i + membrane.Getmxn();

				double dE = X(pot_j_i) - X(mpot_jm1_i);
				//Cation transfer rate
				double kf = CationTransR.kf(dE);
				double kb = CationTransR.kb(dE);
				double CationTransRate = kf*X(cat_j_i) - kb*X(mcat_jm1_i);
				//Product transfer rate
				kf = ProductTransR.kf(dE);
				kb = ProductTransR.kb(dE);
				double ProductTransRate = kf*X(pro_j_i) - kb*X(mpro_jm1_i);
				//Reactant transfer rate
				kf = ReactantTransR.kf(0);
				kb = ReactantTransR.kb(0);
				double ReactantTransRate = kf*X(rea_j_i) - kb*X(mrea_jm1_i);

				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_j_i), X(rea_j_ip1), X(rea_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				F(rea_j_i) -= ReactantTransRate / solution.dz0*Signal.dt;
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_j_i), X(pro_j_ip1), X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				F(pro_j_i) -= ProductTransRate / solution.dz0*Signal.dt;
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_j_i), X(ani_j_ip1), X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_j_i), X(cat_j_ip1), X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				F(cat_j_i) -= CationTransRate / solution.dz0*Signal.dt;
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (j == 0 && i > membrane.m - 1 && i < solution.m - 1) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_j_i), X(rea_j_ip1), X(rea_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_j_i), X(pro_j_ip1), X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_j_i), X(ani_j_ip1), X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_j_i), X(cat_j_ip1), X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_im1), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (i == 0 && j == 0) {
				// index in the membrane phase
				// Reactant index
				unsigned long mrea_jm1_i = membrane.n*i + membrane.n - 1;
				//Product index
				unsigned long mpro_jm1_i = mrea_jm1_i + membrane.Getmxn();
				/*
				//Anion index
				unsigned long mani_jm1_i = mpro_jm1_i + membrane.Getmxn();
				*/
				//Cation index
				unsigned long mcat_jm1_i = mpro_jm1_i + membrane.Getmxn();
				//Potential index
				unsigned long mpot_jm1_i = mcat_jm1_i + membrane.Getmxn();

				double dE = X(pot_j_i) - X(mpot_jm1_i);
				//Cation transfer rate
				double kf = CationTransR.kf(dE);
				double kb = CationTransR.kb(dE);
				double CationTransRate = kf*X(cat_j_i) - kb*X(mcat_jm1_i);
				//Product transfer rate
				kf = ProductTransR.kf(dE);
				kb = ProductTransR.kb(dE);
				double ProductTransRate = kf*X(pro_j_i) - kb*X(mpro_jm1_i);
				//Reactant transfer rate
				kf = ReactantTransR.kf(0);
				kb = ReactantTransR.kb(0);
				double ReactantTransRate = kf*X(rea_j_i) - kb*X(mrea_jm1_i);

				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_j_i), X(rea_j_ip1), X(rea_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_i), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				F(rea_j_i) -= ReactantTransRate / solution.dz0*Signal.dt;
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_j_i), X(pro_j_ip1), X(pro_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_i), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				F(pro_j_i) -= ProductTransRate / solution.dz0*Signal.dt;
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_j_i), X(ani_j_ip1), X(ani_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_i), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_j_i), X(cat_j_ip1), X(cat_j_i),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_i), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				F(cat_j_i) -= CationTransRate / solution.dz0*Signal.dt;
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_j_i), X(pot_j_ip1), X(pot_j_i), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			else if (i == solution.m - 1 && j == 0) {
				//Reactant
				F(rea_j_i) = BulkMTEquation(i, j, X(rea_j_i), X(rea_jp1_i), X(rea_j_i), SI.Reactant.Cinitial, X(rea_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), 0, X(pot_j_im1), S.CoeffReactantA, S.CoeffReactantB, solution.Cren);
				//Product
				F(pro_j_i) = BulkMTEquation(i, j, X(pro_j_i), X(pro_jp1_i), X(pro_j_i), SI.Product.Cinitial, X(pro_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), 0, X(pot_j_im1), S.CoeffProductA, S.CoeffProductB, solution.Cprn);
				//Anion
				F(ani_j_i) = BulkMTEquation(i, j, X(ani_j_i), X(ani_jp1_i), X(ani_j_i), SI.SupportAnion.Cinitial, X(ani_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), 0, X(pot_j_im1), S.CoeffAnionA, S.CoeffAnionB, solution.Cann);
				//Cation
				F(cat_j_i) = BulkMTEquation(i, j, X(cat_j_i), X(cat_jp1_i), X(cat_j_i), SI.SupportCation.Cinitial, X(cat_j_im1),
					X(pot_j_i), X(pot_jp1_i), X(pot_j_i), 0, X(pot_j_im1), S.CoeffCationA, S.CoeffCationB, solution.Ccan);
				//Potential
				F(pot_j_i) = BulkPotEquation(i, j, X(rea_j_i), X(pro_j_i), X(ani_j_i), X(cat_j_i), X(pot_j_i), X(pot_jp1_i), X(pot_j_i), 0, X(pot_j_im1), S.CoeffPotentialA, S.CoeffPotentialB, SI);
			}
			
			
		}
	}
}

void solver::initialiseMatrixA()
{
	//Reactant index
	unsigned long rea_j_i(0UL), rea_jm1_i(0UL), rea_jp1_i(0UL), rea_j_im1(0UL), rea_j_ip1(0UL);
	//Product index
	unsigned long pro_j_i(0UL), pro_jm1_i(0UL), pro_jp1_i(0UL), pro_j_im1(0UL), pro_j_ip1(0UL);
	//Anion index
	unsigned long ani_j_i(0UL), ani_jm1_i(0UL), ani_jp1_i(0UL), ani_j_im1(0UL), ani_j_ip1(0UL);
	//Cation index
	unsigned long cat_j_i(0UL), cat_jm1_i(0UL), cat_jp1_i(0UL), cat_j_im1(0UL), cat_j_ip1(0UL);
	//Potential index
	unsigned long pot_j_i(0UL), pot_jm1_i(0UL), pot_jp1_i(0UL), pot_j_im1(0UL), pot_j_ip1(0UL);

	unsigned long mxn = membrane.Getmxn();
	vector<Tt> MatrixAlist;
	
	//Calculate membrane
	for (unsigned long i = 0; i < membrane.m - 1; ++i) {
		for (unsigned long j = 0; j < membrane.n - 1; ++j) {
			// Reactant index
			rea_j_i = membrane.n*i + j;
			rea_jm1_i = rea_j_i - 1;
			rea_jp1_i = rea_j_i + 1;
			rea_j_im1 = rea_j_i - membrane.n;
			rea_j_ip1 = rea_j_i + membrane.n;
			//Product index
			pro_j_i = rea_j_i + mxn;
			pro_jm1_i = pro_j_i - 1;
			pro_jp1_i = pro_j_i + 1;
			pro_j_im1 = pro_j_i - membrane.n;
			pro_j_ip1 = pro_j_i + membrane.n;
			/*
			//Anion index
			ani_j_i = pro_j_i + mxn;
			ani_jm1_i = ani_j_i - 1;
			ani_jp1_i = ani_j_i + 1;
			ani_j_im1 = ani_j_i - membrane.n;
			ani_j_ip1 = ani_j_i + membrane.n;
			*/
			//Cation index
			cat_j_i = ani_j_i + mxn;
			cat_jm1_i = cat_j_i - 1;;
			cat_jp1_i = cat_j_i + 1;
			cat_j_im1 = cat_j_i - membrane.n;
			cat_j_ip1 = cat_j_i + membrane.n;
			//Potential index
			pot_j_i = cat_j_i + mxn;
			pot_jm1_i = pot_j_i - 1;
			pot_jp1_i = pot_j_i + 1;
			pot_j_im1 = pot_j_i - membrane.n;
			pot_j_ip1 = pot_j_i + membrane.n;
		

			if (i != 0 && j != 0 && i != membrane.m - 1 && j != membrane.n - 1) {
				// Reactant
				MembraneMTDerivativeInit(MatrixAlist, i, j, rea_j_i, rea_jp1_i, rea_jm1_i, rea_j_ip1, rea_j_im1, pot_j_i, pot_jp1_i, pot_jm1_i, pot_j_ip1, pot_j_im1,
										MemEquationCoefficient.CoeffReactantA, MemEquationCoefficient.CoeffReactantB, membrane.Cren, solver::bulk);
				// Product
				MembraneMTDerivativeInit(MatrixAlist, i, j, pro_j_i, pro_jp1_i, pro_jm1_i, pro_j_ip1, pro_j_im1, pot_j_i, pot_jp1_i, pot_jm1_i, pot_j_ip1, pot_j_im1,
										MemEquationCoefficient.CoeffProductA, MemEquationCoefficient.CoeffProductB, membrane.Cprn, solver::bulk);
				// Cation
				MembraneMTDerivativeInit(MatrixAlist, i, j, cat_j_i, cat_jp1_i, cat_jm1_i, cat_j_ip1, cat_j_im1, pot_j_i, pot_jp1_i, pot_jm1_i, pot_j_ip1, pot_j_im1,
										MemEquationCoefficient.CoeffCationA, MemEquationCoefficient.CoeffCationB, membrane.Ccan, solver::bulk);
				// Potential
			}
			else if (j == 0 && i != 0 && i != membrane.m - 1) {
				// Reactant
				MembraneMTDerivativeInit(MatrixAlist, i, j, rea_j_i, rea_jp1_i, rea_j_i, rea_j_ip1, rea_j_im1, pot_j_i, pot_jp1_i, pot_j_i, pot_j_ip1, pot_j_im1,
										MemEquationCoefficient.CoeffReactantA, MemEquationCoefficient.CoeffReactantB, membrane.Cren, solver::bottom);


				// Product
				MembraneMTDerivativeInit(MatrixAlist, i, j, pro_j_i, pro_jp1_i, pro_j_i, pro_j_ip1, pro_j_im1, pot_j_i, pot_jp1_i, pot_j_i, pot_j_ip1, pot_j_im1,
										MemEquationCoefficient.CoeffProductA, MemEquationCoefficient.CoeffProductB, membrane.Cprn, solver::bottom);
				// Cation
				MembraneMTDerivativeInit(MatrixAlist, i, j, cat_j_i, cat_jp1_i, cat_j_i, cat_j_ip1, cat_j_im1, pot_j_i, pot_jp1_i, pot_j_i, pot_j_ip1, pot_j_im1,
										MemEquationCoefficient.CoeffCationA, MemEquationCoefficient.CoeffCationB, membrane.Ccan, solver::bottom);
				// Potential
			}
			else if (j == membrane.n - 1 && i != 0 && i != membrane.m - 1) {

			}
			else if (i == 0 && j != 0 && j != membrane.n - 1) {

			}
			else if (i == membrane.n - 1 && j != 0 && j != membrane.n - 1) {

			}
			else if (i == 0 && j == 0) {

			}
			else if (j == 0 && i == membrane.m - 1) {

			}
			else if (i == 0 && j == membrane.n - 1) {

			}
			else if (j == membrane.n - 1 && i == membrane.m - 1) {

			}

		}
	}
}

inline double solver::BulkMTEquation(unsigned long i, unsigned long j, double Xj_i, double Xjp1_i, double Xjm1_i, double Xj_ip1, double Xj_im1,
	double Xpot_j_i, double Xpot_jp1_i, double Xpot_jm1_i, double Xpot_j_ip1, double Xpot_j_im1,
	const Eigen::MatrixXd& CA, const Eigen::MatrixXd& CB, const Eigen::MatrixXd& Cn)
{
	/*
	Example Equation:
	F(rea_j_i) =
		M.CoeffReactantA(1, j) * X(rea_jm1_i) + M.CoeffReactantA(2, j) * X(rea_jp1_i)
		+ M.CoeffReactantB(1, i) * X(rea_j_im1) + M.CoeffReactantB(2, i) * X(rea_j_ip1)
		+ (M.CoeffReactantA(0, j) + M.CoeffReactantB(0, i))*X(rea_j_i)

		+ M.CoeffReactantA(4, j)*(X(rea_jp1_i) - X(rea_jm1_i))*(X(pot_jp1_i) - X(pot_jm1_i))

		+ (M.CoeffReactantA(5, j) * X(pot_jp1_i) + M.CoeffReactantA(6, j) * X(pot_jm1_i)
			+ M.CoeffReactantB(5, i) * X(pot_j_ip1) + M.CoeffReactantB(6, i) * X(pot_j_im1)
			+ (M.CoeffReactantA(3, j) + M.CoeffReactantB(3, i))*X(pot_j_i))*X(rea_j_i)

		+ M.CoeffReactantB(4, i) * (X(rea_j_ip1) - X(rea_j_im1))*(X(pot_j_ip1) - X(pot_j_im1))
		+ membrane.Cren(j, i);
		*/

	return
		CA(1, j)*Xjm1_i + CA(2, j)*Xjp1_i
		+ CB(1, i)*Xj_im1 + CB(2, i)*Xj_ip1
		+ (CA(0, j) + CB(0, i))*Xj_i

		+ CA(4, j)*(Xjp1_i - Xjm1_i)*(Xpot_jp1_i - Xpot_jm1_i)

		+ (CA(5, j)*Xpot_jp1_i + CA(6, j)*Xpot_jm1_i 
		+ CB(5, i)*Xpot_j_ip1 + CB(6, i)*Xpot_j_im1 
		+ (CA(3, j) + CB(3, i))*Xpot_j_i)*Xj_i

		+ CB(4, i)*(Xj_ip1 - Xj_im1)*(Xpot_j_ip1 - Xpot_j_im1)
		+ Cn(j, i);
}

void solver::MembraneMTDerivativeInit(vector<Tt>& MatrixAlist, unsigned long i, unsigned long j, unsigned long j_i, unsigned long jp1_i, unsigned long jm1_i, unsigned long j_ip1, unsigned long j_im1,
	unsigned long pot_j_i, unsigned long pot_jp1_i, unsigned long pot_jm1_i, unsigned long pot_j_ip1, unsigned long pot_j_im1,
	const Eigen::MatrixXd& CA, const Eigen::MatrixXd& CB, const Eigen::MatrixXd& Cn, Boundary boundary, Species species) const
{
	double Djm1_i = CA(1, j) - CA(4, j)*(X(pot_jp1_i) - X(pot_jm1_i));
	double Djp1_i = CA(2, j) + CA(4, j)*(X(pot_jp1_i) - X(pot_jm1_i));
	double Dj_im1 = CB(1, i) - CB(4, i)*(X(pot_j_ip1) - X(pot_j_im1));
	double Dj_ip1 = CB(2, i) + CB(4, i)*(X(pot_j_ip1) - X(pot_j_im1));
	double Dj_i = (CA(0, j) + CB(0, i)) + (CA(5, j)*X(pot_jp1_i) + CA(6, j)*X(pot_jm1_i) + CB(5, i)*X(pot_j_ip1) + CB(6, i)*X(pot_j_im1) + (CA(3, j) + CB(3, i))*X(pot_j_i));
	double Dpot_jm1_i = -CA(4, j)*(X(jp1_i) - X(jm1_i)) + CA(6, j)*X(j_i);
	double Dpot_jp1_i = CA(4, j)*(X(jp1_i) - X(jm1_i)) + CA(5, j)*X(j_i);
	double Dpot_j_im1 = CB(6, i)*X(j_i) - CB(4, i)*(X(j_ip1) - X(j_im1));
	double Dpot_j_ip1 = CB(5, i)*X(j_i) + CB(4, i)*(X(j_ip1) - X(j_im1));
	double Dpot_j_i = CB(3, i)*X(j_i);


	switch (boundary)
	{
	case solver::bulk:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i));
		break;
	case solver::bottom:
		double DrivingPotential = ElecR.DrivingPotential((X(pot_jp1_i) - X(pot_j_i)) / membrane.dz);
		double kf = ElecR.kf(DrivingPotential);
		double kb = ElecR.kb(DrivingPotential);
		double erDpot_jp1_i = kf*ElecR.minusAlfaNF_R_T*ElecR.DrivingPotentialCoeff / membrane.dz;
		double erDpot_j_i = -Dpot_jp1_i;

		switch (species)
		{
		case solver::Reactant:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i - kb / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i + erDpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i + erDpot_j_i));

			MatrixAlist.push_back(Tt(j_i, j_i + membrane.Getmxn(), kf / membrane.dz*Signal.dt));
			break;
		case solver::Product:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i + kb / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i - erDpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i - erDpot_j_i));

			MatrixAlist.push_back(Tt(j_i, j_i - membrane.Getmxn(), -kf / membrane.dz*Signal.dt));
			break;
		case solver::Cation:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i));
			break;
		default:
			std::cout << "No " << species;
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case solver::top:
		//Potential index
		unsigned long spot_jp1_i = solution.n*i + 4*solution.Getmxn() + 4 * membrane.Getmxn();
		double dE = X(spot_jp1_i) - X(pot_j_i);

		switch (species)
		{
		case solver::Reactant:
			//Reactant index
			unsigned long srea_jp1_i = solution.n*i + 4 * membrane.Getmxn();
			//Reactant transfer rate
			double kf = ReactantTransR.kf(0);
			double kb = ReactantTransR.kb(0);
			double ReactantTransRate = kf*X(srea_jp1_i) - kb*X(j_i);

			MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djp1_i - kb/membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jp1_i));

			MatrixAlist.push_back(Tt(j_i, srea_jp1_i, kf / membrane.dz*Signal.dt));
			break;
		case solver::Product:
			//Product index
			unsigned long spro_jp1_i = solution.n*i + solution.Getmxn() + 4 * membrane.Getmxn();
			//Product transfer rate
			double kf = ProductTransR.kf(dE);
			double kb = ProductTransR.kb(dE);
			double ProductTransRate = kf*X(spro_jp1_i) - kb*X(j_i);
			double inDspot_jp1_i = ProductTransR.minusAlfaNF_R_T*kf*X(spro_jp1_i) - ProductTransR.AlfaMinusOneNF_R_T*kb*X(j_i);
			double inDpot_j_i = -inDspot_jp1_i;

			MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djp1_i - kb / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jp1_i + inDpot_j_i /membrane.dz*Signal.dt));

			MatrixAlist.push_back(Tt(j_i, spro_jp1_i, kf / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, spot_jp1_i, inDspot_jp1_i / membrane.dz*Signal.dt));
			break;

		case solver::Cation:
			//Cation index
			unsigned long scat_jp1_i = solution.n*i + 3 * solution.Getmxn() + 4 * membrane.Getmxn();
			//Cation transfer rate
			double kf = CationTransR.kf(dE);
			double kb = CationTransR.kb(dE);
			double CationTransRate = kf*X(scat_jp1_i) - kb*X(j_i);
			double inDspot_jp1_i = CationTransR.minusAlfaNF_R_T*kf*X(spro_jp1_i) - CationTransR.AlfaMinusOneNF_R_T*kb*X(j_i);
			double inDpot_j_i = -inDspot_jp1_i;

			MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djp1_i - kb / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jp1_i + inDpot_j_i / membrane.dz*Signal.dt));

			MatrixAlist.push_back(Tt(j_i, spro_jp1_i, kf / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, spot_jp1_i, inDspot_jp1_i / membrane.dz*Signal.dt));

			break;
		default:
			break;
		}
		break;
	case solver::left:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Dj_im1));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_j_im1));
		break;
	case solver::right:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_j_ip1));
		break;
	case solver::left_bottom_corner:
		switch (species)
		{
		case solver::Reactant:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i + Dj_im1 - kb / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i + erDpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i + Dpot_j_im1 + erDpot_j_i));

			MatrixAlist.push_back(Tt(j_i, j_i + membrane.Getmxn(), kf / membrane.dz*Signal.dt));
			break;
		case solver::Product:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i + Dj_im1 + kb / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i - erDpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i + Dpot_j_im1 - erDpot_j_i));

			MatrixAlist.push_back(Tt(j_i, j_i - membrane.Getmxn(), -kf / membrane.dz*Signal.dt));
			break;
		case solver::Cation:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i + Dj_im1));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i + Dpot_j_im1));
			break;
		default:
			std::cout << "No " << species;
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case solver::right_bottom_corner:
		switch (species)
		{
		case solver::Reactant:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i + Dj_ip1 - kb / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i + erDpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i + Dpot_j_ip1 + erDpot_j_i));

			MatrixAlist.push_back(Tt(j_i, j_i + membrane.Getmxn(), kf / membrane.dz*Signal.dt));
			break;
		case solver::Product:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i + Dj_ip1 + kb / membrane.dz*Signal.dt));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i - erDpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i + Dpot_j_ip1 - erDpot_j_i));

			MatrixAlist.push_back(Tt(j_i, j_i - membrane.Getmxn(), -kf / membrane.dz*Signal.dt));
			break;
		case solver::Cation:
			MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
			MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
			MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i + Dj_ip1));
			MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
			MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
			MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i + Dpot_j_ip1));
			break;
		default:
			std::cout << "No " << species;
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case solver::left_upper_corner:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djp1_i + Dj_im1));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jp1_i + Dpot_j_im1));
		break;
	case solver::right_upper_corner:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djp1_i + Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jp1_i + Dpot_j_ip1));
		break;
	default:
		std::cout << "miss membrane phase (" << i << ", " << j << ")\n";
		exit(EXIT_FAILURE);
		break;
	}
}

void solver::SolutionMTDerivativeInit(vector<Tt>& MatrixAlist, unsigned long i, unsigned long j, unsigned long j_i, unsigned long jp1_i, unsigned long jm1_i, unsigned long j_ip1, unsigned long j_im1,
	unsigned long pot_j_i, unsigned long pot_jp1_i, unsigned long pot_jm1_i, unsigned long pot_j_ip1, unsigned long pot_j_im1,
	const Eigen::MatrixXd& CA, const Eigen::MatrixXd& CB, const Eigen::MatrixXd& Cn, Boundary boundary, Species species) const
{
	double Djm1_i = CA(1, j) - CA(4, j)*(X(pot_jp1_i) - X(pot_jm1_i));
	double Djp1_i = CA(2, j) + CA(4, j)*(X(pot_jp1_i) - X(pot_jm1_i));
	double Dj_im1 = CB(1, i) - CB(4, i)*(X(pot_j_ip1) - X(pot_j_im1));
	double Dj_ip1 = CB(2, i) + CB(4, i)*(X(pot_j_ip1) - X(pot_j_im1));
	double Dj_i = (CA(0, j) + CB(0, i)) + (CA(5, j)*X(pot_jp1_i) + CA(6, j)*X(pot_jm1_i) + CB(5, i)*X(pot_j_ip1) + CB(6, i)*X(pot_j_im1) + (CA(3, j) + CB(3, i))*X(pot_j_i));
	double Dpot_jm1_i = -CA(4, j)*(X(jp1_i) - X(jm1_i)) + CA(6, j)*X(j_i);
	double Dpot_jp1_i = CA(4, j)*(X(jp1_i) - X(jm1_i)) + CA(5, j)*X(j_i);
	double Dpot_j_im1 = CB(6, i)*X(j_i) - CB(4, i)*(X(j_ip1) - X(j_im1));
	double Dpot_j_ip1 = CB(5, i)*X(j_i) + CB(4, i)*(X(j_ip1) - X(j_im1));
	double Dpot_j_i = CB(3, i)*X(j_i);

	switch (boundary)
	{
	case solver::bulk:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i));
		break;
	case solver::bottom:
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i));
		break;
	case solver::top:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i));
	case solver::left:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Dj_im1));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_j_im1));
		break;
	case solver::right:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i));
		break;
	case solver::left_bottom_corner:
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i + Dj_im1));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i + Dpot_j_im1));
		break;
	case solver::right_bottom_corner:
		MatrixAlist.push_back(Tt(j_i, jp1_i, Djp1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Djm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_jp1_i, Dpot_jp1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_jm1_i));
		break;
	case solver::left_upper_corner:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, j_ip1, Dj_ip1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i + Dj_im1));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_ip1, Dpot_j_ip1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i + Dpot_j_im1));
		break;
	case solver::right_upper_corner:
		MatrixAlist.push_back(Tt(j_i, jm1_i, Djm1_i));
		MatrixAlist.push_back(Tt(j_i, j_im1, Dj_im1));
		MatrixAlist.push_back(Tt(j_i, j_i, Dj_i));
		MatrixAlist.push_back(Tt(j_i, pot_jm1_i, Dpot_jm1_i));
		MatrixAlist.push_back(Tt(j_i, pot_j_im1, Dpot_j_im1));
		MatrixAlist.push_back(Tt(j_i, pot_j_i, Dpot_j_i));
		break;
	default:
		std::cout << "miss solution phase (" << i << ", " << j << ")\n";
		exit(EXIT_FAILURE);
		break;
	}
}

inline double solver::BulkPotEquation(unsigned long i, unsigned long j, double Xrea_j_i, double Xpro_j_i, double Xani_j_i, double Xcat_j_i,
	double Xpot_j_i, double Xpot_jp1_i, double Xpot_jm1_i, double Xpot_j_ip1, double Xpot_j_im1,
	const Eigen::MatrixXd& CA, const Eigen::MatrixXd& CB, const IonSystem& I)
{
	/*
	Example Equation:
	F(pot_j_i) =
		M.CoeffPotentialA(1, j) * X(pot_jm1_i) + M.CoeffPotentialA(2, j) * X(pot_jp1_i)
		+ M.CoeffPotentialB(1, i) * X(pot_j_im1) + M.CoeffPotentialB(2, i) * X(pot_j_ip1)
		+ (M.CoeffPotentialA(0, j) + M.CoeffPotentialB(0, i)) * X(pot_j_i)
		+ (MI.Reactant.Z*X(rea_j_i) + MI.Product.Z*X(pro_j_i)
			+ MI.SupportAnion.Z*X(ani_j_i) + MI.SupportCation.Z*X(cat_j_i)
			+ MI.CxZImmobileCharge)*MI.ReciprocalEpsilon_rEpsilon_0*Thermo.F;
	*/

	return
		CA(1, j)*Xpot_jm1_i + CA(2, j)*Xpot_jp1_i
		+ CB(1, i)*Xpot_j_im1 + CB(2, i)*Xpot_j_ip1
		+ (CA(0, j) + CB(0, i))*Xpot_j_i
		+ (I.Reactant.Z*Xrea_j_i + I.Product.Z*Xpro_j_i 
		+ I.SupportAnion.Z*Xani_j_i + I.SupportCation.Z*Xcat_j_i 
		+ I.CxZImmobileCharge)*I.ReciprocalEpsilon_rEpsilon_0*Thermo.F;

}

EquationCoefficient::EquationCoefficient(const IonSystem& fIons, const mesh& phase, const PotentialSignal& fSignal, const nernst_equation& fThermo) :
	Ions(fIons), Signal(fSignal), F_R_T(fThermo.F_R_T),
	CoeffProductA(7, phase.GetMeshSize()[1]), CoeffProductB(7, phase.GetMeshSize()[0]),
	CoeffReactantA(7, phase.GetMeshSize()[1]), CoeffReactantB(7, phase.GetMeshSize()[0]),
	CoeffAnionA(7, phase.GetMeshSize()[1]), CoeffAnionB(7, phase.GetMeshSize()[0]),
	CoeffCationA(7, phase.GetMeshSize()[1]), CoeffCationB(7, phase.GetMeshSize()[0]),
	CoeffPotentialA(3, phase.GetMeshSize()[1]), CoeffPotentialB(3, phase.GetMeshSize()[0])
{}

void EquationCoefficient::CalculateCoeff(Eigen::MatrixXd& GeoCoeffA, Eigen::MatrixXd GeoCoeffB)
{
	//initialise coefficients from geometric coefficients
	CoeffProductA = GeoCoeffA;
	CoeffProductB = GeoCoeffB;
	CoeffReactantA = GeoCoeffA;
	CoeffReactantB = GeoCoeffB;
	CoeffAnionA = GeoCoeffA;
	CoeffAnionB= GeoCoeffB;
	CoeffCationA = GeoCoeffA;
	CoeffCationB = GeoCoeffB;

	CoeffPotentialA = GeoCoeffA.topRows(3);
	CoeffPotentialB = GeoCoeffB.topRows(3);
	// adjust the coefficients of diffusion components
	CoeffProductA.topRows(3) *= Ions.Product.D*Signal.dt;
	CoeffProductB.topRows(3) *= Ions.Product.D*Signal.dt;
	CoeffReactantA.topRows(3) *= Ions.Reactant.D*Signal.dt;
	CoeffReactantB.topRows(3) *= Ions.Reactant.D*Signal.dt;
	CoeffAnionA.topRows(3) *= Ions.SupportAnion.D*Signal.dt;
	CoeffAnionB.topRows(3) *= Ions.SupportAnion.D*Signal.dt;
	CoeffCationA.topRows(3) *= Ions.SupportCation.D*Signal.dt;
	CoeffCationB.topRows(3) *= Ions.SupportCation.D*Signal.dt;

	CoeffProductA.row(0).array() += -1;
	CoeffReactantA.row(0).array() += -1;
	CoeffAnionA.row(0).array() += -1;
	CoeffCationA.row(0).array() += -1;
	// adjust the coefficients of migration components
	CoeffProductA.bottomRows(4) *= Ions.Product.D*Signal.dt*Ions.Product.Z*F_R_T;
	CoeffProductB.bottomRows(4) *= Ions.Product.D*Signal.dt*Ions.Product.Z*F_R_T;
	CoeffReactantA.bottomRows(4) *= Ions.Reactant.D*Signal.dt*Ions.Reactant.Z*F_R_T;
	CoeffReactantB.bottomRows(4) *= Ions.Reactant.D*Signal.dt*Ions.Reactant.Z*F_R_T;
	CoeffAnionA.bottomRows(4) *= Ions.SupportAnion.D*Signal.dt*Ions.SupportAnion.Z*F_R_T;
	CoeffAnionB.bottomRows(4) *= Ions.SupportAnion.D*Signal.dt*Ions.SupportAnion.Z*F_R_T;
	CoeffCationA.bottomRows(4) *= Ions.SupportCation.D*Signal.dt*Ions.SupportCation.Z*F_R_T;
	CoeffCationB.bottomRows(4) *= Ions.SupportCation.D*Signal.dt*Ions.SupportCation.Z*F_R_T;
}

