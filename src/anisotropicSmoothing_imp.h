#ifndef __ANISOTROPIC_SMOOTHING_IMP_H__
#define __ANISOTROPIC_SMOOTHING_IMP_H__

#include <utility>
#include <algorithm>
#include "solver/lbfgsbsolver.h"
#include "j.h"

#include <chrono>

template <class Derived, typename InputHandler, typename Integrator, UInt ORDER>
std::pair<const std::vector<VectorXr>, const typename H<InputHandler, Integrator, ORDER>::TVector> AnisotropicSmoothingBase<Derived, InputHandler, Integrator, ORDER>::smooth() const {
    const std::vector<Real>::size_type n_lambda = lambda_.size();
    std::vector<TVector, Eigen::aligned_allocator<TVector>> anisoParamSmooth(n_lambda, TVector(M_PI_2, 5.));
    std::vector<Eigen::Index> crossValSmoothInd(n_lambda);
    std::vector<Real> gcvSmooth(n_lambda);

    auto start1 = std::chrono::high_resolution_clock::now();

    //#pragma omp parallel for
    for(std::vector<Real>::size_type i = 0U; i < n_lambda; i++) {
        // Optimization of H
        ///InputHandler dataUniqueLambda = createRegressionData(regressionData_.getLambda()[i]);
        regressionData_.setLambda(std::vector<Real>(1U, lambda_[i]));
        Rprintf("lambda_[%2d] = %f\n", i, lambda_[i]);
        H<InputHandler, Integrator, ORDER> h(mesh_, meshLoc_, regressionData_);

        if (i != 0U) {
            anisoParamSmooth[i] = anisoParamSmooth[i-1];
        }
        auto start = std::chrono::high_resolution_clock::now();
        cppoptlib::LbfgsbSolver<H<InputHandler, Integrator, ORDER>> solver;
        solver.minimize(h, anisoParamSmooth[i]);
        // Dealing with angle periodicity
        if (anisoParamSmooth[i](0) == M_PI) {
            anisoParamSmooth[i](0) = 0.;
            solver.minimize(h, anisoParamSmooth[i]);
            REprintf("Angle equal to pi at iteration = %3u\n", i);
        }
        if (anisoParamSmooth[i](0) == 0.) {
            anisoParamSmooth[i](0) = M_PI;
            solver.minimize(h, anisoParamSmooth[i]);
            REprintf("Angle equal to 0 at iteration = %3u\n", i);
        }
        
        if (!h.isValid(anisoParamSmooth[i])) {
            REprintf("Optimization failed (value is out of range): (%f, %f)\n", anisoParamSmooth[i](0), anisoParamSmooth[i](1));
            anisoParamSmooth[i] = anisoParamSmooth[i].cwiseMax(TVector(0., 1.)).cwiseMin(TVector(M_PI, 1000.));
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        Rprintf("Final anisoParam [%2d]: (%f, %f)\n", i, anisoParamSmooth[i](0), anisoParamSmooth[i](1));
        Rprintf("Time to optimize [%2d]: %f\n", i, diff);
        start = std::chrono::high_resolution_clock::now();
    
        // Computation of the GCV for current anisoParamSmooth[i]
        ///InputHandler dataSelectedK = createRegressionData(anisoParamSmooth[i]);
        regressionData_.setLambda(lambdaCrossVal_);
        regressionData_.setComputeDOF(true);
        J<InputHandler, Integrator, ORDER> j(mesh_, meshLoc_, regressionData_);
        regressionData_.setComputeDOF(dof_);
        
        VectorXr gcvSeq = j.getGCV();

        Eigen::Index lambdaCrossValIndex;
        Real gcv = gcvSeq.minCoeff(&lambdaCrossValIndex);

        crossValSmoothInd[i] = lambdaCrossValIndex;
        gcvSmooth[i] = gcv;

        end = std::chrono::high_resolution_clock::now();
        diff = end - start;
        Rprintf("Time to compute GCV [%2d]: %f\n", i, diff);
    }
    auto start2 = std::chrono::high_resolution_clock::now();

    // Choosing the best anisotropy matrix and lambda coefficient
    std::vector<Real>::const_iterator minIterator = std::min_element(gcvSmooth.cbegin(), gcvSmooth.cend());
    std::vector<Real>::difference_type optIndex = std::distance(gcvSmooth.cbegin(), minIterator);

    ///InputHandler regressionDataFinal = createRegressionData(lambdaCrossVal_[crossValSmoothInd[optIndex]], anisoParamSmooth[optIndex]);
    regressionData_.setLambda(std::vector<Real>(1U, lambdaCrossVal_[crossValSmoothInd[optIndex]]));
    regressionData_.setK(H<InputHandler, Integrator, ORDER>::buildKappa(anisoParamSmooth[optIndex]));
    MixedFERegression<InputHandler, Integrator, ORDER, 2, 2> regressionFinal(mesh_, regressionData_);
    
    regressionFinal.apply();

    auto end2 = std::chrono::high_resolution_clock::now();
    auto end1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff2 = end2 - start2;
    std::chrono::duration<double> diff1 = end1 - start1;

    Rprintf("Time to compute final regression: %f\n", diff2);
    Rprintf("Total time: %f\n", diff1);

    return std::make_pair(regressionFinal.getSolution(), anisoParamSmooth[optIndex]);
}

template <typename Integrator, UInt ORDER>
struct AnisotropicSmoothing<RegressionDataElliptic, Integrator, ORDER> : public AnisotropicSmoothingBase<AnisotropicSmoothing<RegressionDataElliptic, Integrator, ORDER>, RegressionDataElliptic, Integrator, ORDER> {
        using typename AnisotropicSmoothingBase<AnisotropicSmoothing, RegressionDataElliptic, Integrator, ORDER>::TVector;
        AnisotropicSmoothing(RegressionDataElliptic & regressionData, const MeshHandler<ORDER, 2, 2> & mesh) : AnisotropicSmoothingBase<AnisotropicSmoothing, RegressionDataElliptic, Integrator, ORDER>(regressionData, mesh) {}

        RegressionDataElliptic createRegressionData(const Real & lambda) const;
        RegressionDataElliptic createRegressionData(const TVector & anisoParam) const;
        RegressionDataElliptic createRegressionData(const Real & lambda, const TVector & anisoParam) const;
};

template <typename Integrator, UInt ORDER>
RegressionDataElliptic AnisotropicSmoothing<RegressionDataElliptic, Integrator, ORDER>::createRegressionData(const Real & lambda) const {
    std::vector<Point> locations = AnisotropicSmoothing::regressionData_.getLocations();
    VectorXr observations = AnisotropicSmoothing::regressionData_.getObservations();
    std::vector<Real> uniqueLambda(1U, lambda);
    Eigen::Matrix<Real, 2, 2> kappa;
    Eigen::Matrix<Real, 2, 1> beta = AnisotropicSmoothing::regressionData_.getBeta();
    MatrixXr covariates = AnisotropicSmoothing::regressionData_.getCovariates();
    std::vector<UInt> dirichletIndices = AnisotropicSmoothing::regressionData_.getDirichletIndices();
    std::vector<Real> dirichletValues = AnisotropicSmoothing::regressionData_.getDirichletValues();

    const RegressionDataElliptic result(
            locations,
            observations,
            AnisotropicSmoothing::regressionData_.getOrder(),
            uniqueLambda,
            kappa,
            beta,
            AnisotropicSmoothing::regressionData_.getC(),
            covariates,
            dirichletIndices,
            dirichletValues,
            AnisotropicSmoothing::regressionData_.computeDOF(),
            AnisotropicSmoothing::regressionData_.getGCVmethod(),
            AnisotropicSmoothing::regressionData_.getNrealizations());

    return result;
}

template <typename Integrator, UInt ORDER>
RegressionDataElliptic AnisotropicSmoothing<RegressionDataElliptic, Integrator, ORDER>::createRegressionData(const TVector & anisoParam) const {
    std::vector<Point> locations = AnisotropicSmoothing::regressionData_.getLocations();
    VectorXr observations = AnisotropicSmoothing::regressionData_.getObservations();
    Eigen::Matrix<Real, 2, 2> kappa = H<RegressionDataElliptic, Integrator, ORDER>::buildKappa(anisoParam);
    Eigen::Matrix<Real, 2, 1> beta = AnisotropicSmoothing::regressionData_.getBeta();
    MatrixXr covariates = AnisotropicSmoothing::regressionData_.getCovariates();
    std::vector<UInt> dirichletIndices = AnisotropicSmoothing::regressionData_.getDirichletIndices();
    std::vector<Real> dirichletValues = AnisotropicSmoothing::regressionData_.getDirichletValues();

    const RegressionDataElliptic result(
            locations,
            observations,
            AnisotropicSmoothing::regressionData_.getOrder(),
            AnisotropicSmoothing::lambdaCrossVal_, // Uses the vector of lambdaCrossVal
            kappa,
            beta,
            AnisotropicSmoothing::regressionData_.getC(),
            covariates,
            dirichletIndices,
            dirichletValues,
            true, // Sets DOF_ = true to compute the GCV
            AnisotropicSmoothing::regressionData_.getGCVmethod(),
            AnisotropicSmoothing::regressionData_.getNrealizations());

    return result;
}

template <typename Integrator, UInt ORDER>
RegressionDataElliptic AnisotropicSmoothing<RegressionDataElliptic, Integrator, ORDER>::createRegressionData(const Real & lambda, const TVector & anisoParam) const {
    std::vector<Point> locations = AnisotropicSmoothing::regressionData_.getLocations();
    VectorXr observations = AnisotropicSmoothing::regressionData_.getObservations();
    std::vector<Real> uniqueLambda(1U, lambda);
    Eigen::Matrix<Real, 2, 2> kappa = H<RegressionDataElliptic, Integrator, ORDER>::buildKappa(anisoParam);
    Eigen::Matrix<Real, 2, 1> beta = AnisotropicSmoothing::regressionData_.getBeta();
    MatrixXr covariates = AnisotropicSmoothing::regressionData_.getCovariates();
    std::vector<UInt> dirichletIndices = AnisotropicSmoothing::regressionData_.getDirichletIndices();
    std::vector<Real> dirichletValues = AnisotropicSmoothing::regressionData_.getDirichletValues();

    const RegressionDataElliptic result(
            locations,
            observations,
            AnisotropicSmoothing::regressionData_.getOrder(),
            uniqueLambda,
            kappa,
            beta,
            AnisotropicSmoothing::regressionData_.getC(),
            covariates,
            dirichletIndices,
            dirichletValues,
            AnisotropicSmoothing::regressionData_.computeDOF(),
            AnisotropicSmoothing::regressionData_.getGCVmethod(),
            AnisotropicSmoothing::regressionData_.getNrealizations());

    return result;
}


template <class Derived, typename InputHandler, typename Integrator, UInt ORDER>
std::vector<Real> AnisotropicSmoothingBase<Derived, InputHandler, Integrator, ORDER>::seq(const UInt & n_obs, const Real & area) const {
    std::vector<Real> result{1.000000e-07, 1.258925e-07, 1.584893e-07, 1.995262e-07, 2.511886e-07, 3.162278e-07, 3.981072e-07, 5.011872e-07, 6.309573e-07, 7.943282e-07, 1.000000e-06, 1.258925e-06, 1.584893e-06, 1.995262e-06, 2.511886e-06, 3.162278e-06, 3.981072e-06, 5.011872e-06, 6.309573e-06, 7.943282e-06, 1.000000e-05, 1.258925e-05, 1.584893e-05, 1.995262e-05, 2.511886e-05, 3.162278e-05, 3.981072e-05, 5.011872e-05, 6.309573e-05, 7.943282e-05, 1.000000e-04, 1.258925e-04, 1.584893e-04, 1.995262e-04, 2.511886e-04, 3.162278e-04, 3.981072e-04, 5.011872e-04, 6.309573e-04, 7.943282e-04, 1.000000e-03, 1.258925e-03, 1.584893e-03, 1.995262e-03, 2.511886e-03, 3.162278e-03, 3.981072e-03, 5.011872e-03, 6.309573e-03, 7.943282e-03, 1.000000e-02, 1.258925e-02, 1.584893e-02, 1.995262e-02, 2.511886e-02, 3.162278e-02, 3.981072e-02, 5.011872e-02, 6.309573e-02, 7.943282e-02, 1.000000e-01, 1.258925e-01, 1.584893e-01, 1.995262e-01, 2.511886e-01, 3.162278e-01, 3.981072e-01, 5.011872e-01, 6.309573e-01, 7.943282e-01};
    std::transform(result.begin(), result.end(), result.begin(), [&n_obs, &area] (const Real & el) -> Real { return el/(1-el)*(n_obs/area); });
    return result;
}

#endif
