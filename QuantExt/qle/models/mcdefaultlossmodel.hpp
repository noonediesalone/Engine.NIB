/*
 Copyright (C) 2024 AcadiaSoft Inc.
 All rights reserved.

 This file is part of ORE, a free-software/open-source library
 for transparent pricing and risk analysis - http://opensourcerisk.org

 ORE is free software: you can redistribute it and/or modify it
 under the terms of the Modified BSD License.  You should have received a
 copy of the license along with this program.
 The license is also available online at <http://opensourcerisk.org>

 This program is distributed on the basis that it will form a useful
 contribution to risk analytics and model standardisation, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE. See the license for more details.
*/

#pragma once

#include <functional>
#include <numeric>
#include <ored/utilities/log.hpp>
#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/math/randomnumbers/inversecumulativerng.hpp>
#include <ql/math/randomnumbers/mt19937uniformrng.hpp>
#include <qle/models/defaultlossmodel.hpp>

/* Intended to replace LossDistribution in
    ql/experimental/credit/lossdistribution, not sure its covering all the
    functionality (see mthod below)
*/

namespace QuantExt {
using namespace QuantLib;

/*! Model Class for 1 factor gaussian copula loss model
using monte carlo simulation
*/
class GaussianOneFactorMonteCarloLossModel : public DefaultLossModel, public Observer {

public:
    GaussianOneFactorMonteCarloLossModel(Handle<Quote> baseCorrelation,
                                         const std::vector<std::vector<double>>& recoveryRates,
                                         const std::vector<std::vector<double>>& recoveryProbabilites,
                                         const size_t samples)
        : baseCorrelation_(baseCorrelation), recoveryRates_(recoveryRates),
          recoveryProbabilities_(recoveryProbabilites), nSamples_(samples) {

        registerWith(baseCorrelation);

        QL_REQUIRE(recoveryRates_.empty() || recoveryRates_.size() == recoveryProbabilities_.size(),
                   "Error mismatch vector size, between recoveryRates and their respective probability");

        for (size_t i = 0; i < recoveryRates_.size(); ++i) {
            double totalRecoveryProb =
                std::accumulate(recoveryProbabilities_[i].begin(), recoveryProbabilities_[i].end(), 0.0);
            QL_REQUIRE(QuantLib::close_enough(totalRecoveryProb, 1.0), "Recovery probabilities should sum to 1");
            QL_REQUIRE(recoveryRates_[i].size() == recoveryProbabilities_[i].size(),
                       "All recoveryRates should have the same number of probs");
        }
    }

    void update() override { notifyObservers(); }

protected:
    Real expectedTrancheLoss(const Date& d, Real recoveryRate = Null<Real>()) const override {

        const std::vector<double> pds = basket_->remainingProbabilities(d);
        const std::vector<double> notionals = basket_->notionals();
        const std::vector<std::string> names = basket_->remainingNames();
        TLOG("Compute expectedTrancheLoss with MC for " << d);
        TLOG("Basket Information");
        TLOG("Basket attachment amount " << std::fixed << std::setprecision(2) << basket_->attachmentAmount());
        TLOG("Basket dettachment Amount " << std::fixed << std::setprecision(2) << basket_->detachmentAmount());
        TLOG("Basket remaining attachment Amount " << std::fixed << std::setprecision(2)
                                                   << basket_->remainingAttachmentAmount(d));
        TLOG("Basket remaining dettachment Amount " << std::fixed << std::setprecision(2)
                                                    << basket_->remainingDetachmentAmount(d));
        TLOG("BaseCorrelation " << baseCorrelation_->value());
        TLOG("Constituents");
        TLOG("i,name,notional,pd,recoveryRates");
        for (size_t i = 0; i < pds.size(); ++i) {
            TLOG(i << "," << names[i] << "," << io::iso_date(d) << "," << pds[i] << "," << notionals[i]);
            for (size_t j = 0; j < recoveryRates_[i].size(); ++j) {
                TLOG("RR " << recoveryRates_[i][j] << " with prob " << recoveryProbabilities_[i][j]);
            }
        }
        InverseCumulativeRng<MersenneTwisterUniformRng, InverseCumulativeNormal> normal(MersenneTwisterUniformRng(123));

        // Compute determistic LGD case
        // if(recoveryRates_.front().size() == 1){

        InverseCumulativeNormal ICN;
        std::vector<std::vector<double>> thresholds;
        TLOG("DefaultThresholdholds");
        for (size_t id = 0; id < pds.size(); id++) {
            thresholds.push_back(std::vector<double>(recoveryRates_[id].size(), 0.0));
            thresholds[id][0] = (ICN(pds[id]));
            double cumRecoveryProb = 0.0;
            for (size_t j = 0; j < recoveryProbabilities_[id].size() - 1; ++j) {
                cumRecoveryProb += recoveryProbabilities_[id][j];
                thresholds[id][j + 1] = (ICN(pds[id] * (1.0 - cumRecoveryProb)));
            }
            thresholds[id].push_back(QL_MIN_REAL);
            for (size_t j = 0; j < recoveryProbabilities_[id].size(); ++j) {
                TLOG("id " << id << " Threshold " << j << " " << thresholds[id][j])
            }
        }

        const double sqrtRho = std::sqrt(baseCorrelation_->value());
        const double sqrtOneMinusRho = std::sqrt(1.0 - baseCorrelation_->value());
        const double n = static_cast<double>(nSamples_);
        double trancheLoss = 0.0;
        double zeroTrancheLoss = 0.0;
        double expectedLossIndex = 0.0;
        const size_t nConstituents = pds.size();
        std::vector<double> xs(nConstituents * nSamples_, 0.0);
        // std::vector<double> ys(nConstituents * nSamples_, 0.0);
        /*
        for (size_t i = 0; i < nSamples_; i++) {
            const double marketFactor = normal.next().value * sqrtRho;
            for (size_t id = 0; id < nConstituents; id++) {
                xs[i * nConstituents + id] = marketFactor + sqrtOneMinusRho * normal.next().value;
            }
        }
        */
        std::vector<double> simPD(pds.size(), 0.0);
        for (size_t i = 0; i < nSamples_; i++) {
            double loss = 0.0;
            double loss_zero_recovery = 0.0;
            const double marketFactor = normal.next().value * sqrtRho;
            for (size_t id = 0; id < pds.size(); id++) {
                const double x = marketFactor + sqrtOneMinusRho * normal.next().value;
                // TLOG("Sim " << i << " x= " << x);
                for (size_t lgd = 1; lgd < thresholds[id].size(); lgd++) {

                    // TLOG("Threshold " << lgd << " rrThreshold= " << thresholds[id][lgd - 1] << " RecoveryRate "
                    //                  << recoveryRates_[id][lgd - 1]);
                    if (x > thresholds[id][lgd] && x <= thresholds[id][lgd - 1]) {
                        // default reovery rate
                        simPD[id] += 1;
                        loss += notionals[id] * (1.0 - recoveryRates_[id][lgd - 1]);
                        loss_zero_recovery += notionals[id];
                        break;
                    }
                }
            }
            expectedLossIndex += loss;

            trancheLoss +=
                std::max(loss - basket_->attachmentAmount(), 0.0) - std::max(loss - basket_->detachmentAmount(), 0.0);

            zeroTrancheLoss += std::max(loss_zero_recovery - basket_->attachmentAmount(), 0.0) -
                               std::max(loss_zero_recovery - basket_->detachmentAmount(), 0.0);
        }
        TLOG("Valid")
        for (size_t i = 0; i < pds.size(); ++i) {
            TLOG(i << "," << std::fixed << std::setprecision(8) << pds[i] << "," << simPD[i] / n);
        }
        TLOG("Expected Tranche Loss = " << trancheLoss / n);
        TLOG("Expected Index Loss " << expectedLossIndex / n);
        if (recoveryRate != Null<Real>()) {
            return zeroTrancheLoss / n;
        }
        return trancheLoss / n;
    }

    QuantLib::Real correlation() const override { return baseCorrelation_->value(); }
    // the call order matters, which is the reason for the parent to be the
    //   sole caller.
    //! Concrete models do now any updates/inits they need on basket reset
private:
    void resetModel() override {};
    Handle<Quote> baseCorrelation_;
    std::vector<std::vector<double>> recoveryRates_;
    std::vector<std::vector<double>> recoveryProbabilities_;

    size_t nSamples_;
};

} // namespace QuantExt
