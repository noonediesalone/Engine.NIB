/*
 Copyright (C) 2020 Quaternion Risk Management Ltd
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

#include <orea/app/inputparameters.hpp>
#include <orea/aggregation/dynamicdeltavarcalculator.hpp>
#include <ored/utilities/log.hpp>
#include <ored/utilities/vectorutils.hpp>
#include <ql/errors.hpp>
#include <ql/time/calendars/weekendsonly.hpp>
#include <ql/version.hpp>

#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/time/daycounters/actualactual.hpp>

#include <qle/math/nadarayawatson.hpp>
#include <qle/math/stabilisedglls.hpp>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/error_of_mean.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>

using namespace std;
using namespace QuantLib;
using namespace boost::accumulators;
using namespace ore::data;

namespace ore {
namespace analytics {

DynamicDeltaVaRCalculator::DynamicDeltaVaRCalculator(
    const QuantLib::ext::shared_ptr<InputParameters>& inputs,
    const QuantLib::ext::shared_ptr<Portfolio>& portfolio,
    const QuantLib::ext::shared_ptr<NPVCube>& cube,
    const QuantLib::ext::shared_ptr<CubeInterpretation>& cubeInterpretation,
    const QuantLib::ext::shared_ptr<AggregationScenarioData>& scenarioData,
    Real quantile, Size horizonCalendarDays,
    const QuantLib::ext::shared_ptr<DimHelper>& dimHelper, const Size ddvOrder,
    const std::map<std::string, Real>& currentIM)
  : DynamicInitialMarginCalculator(inputs, portfolio, cube, cubeInterpretation, scenarioData, quantile, horizonCalendarDays, currentIM),
    dimHelper_(dimHelper), ddvOrder_(ddvOrder) {}

const map<string, Real>& DynamicDeltaVaRCalculator::unscaledCurrentDIM() const { return currentDIM_; }

void DynamicDeltaVaRCalculator::build() {

    map<string, Real> currentDim = unscaledCurrentDIM();

    Size stopDatesLoop = datesLoopSize_;
    Size samples = cube_->samples();
    Real thetaFactor = static_cast<Real>(horizonCalendarDays_) / 365.25;

    map<string, std::vector<QuantLib::ext::shared_ptr<Trade>>> nettingSetTrades;
    for (const auto& [tradeId, trade] : portfolio_->trades()) {
        string nettingSetId = trade->envelope().nettingSetId();
        nettingSetTrades[nettingSetId].push_back(trade);
    }

    size_t i = 0;
    for (const auto& nid : nettingSetIds_) {
        LOG("Process netting set " << nid);

        if (currentIM_.find(nid) != currentIM_.end()) {
            Real t0im = currentIM_[nid];
            QL_REQUIRE(currentDim.find(nid) != currentDim.end(), "current DIM not found for netting set " << nid);
            Real t0dim = currentDim[nid];
            Real t0scaling = t0im / t0dim;
            LOG("t0 scaling for netting set " << nid << ": t0im" << t0im << " t0dim=" << t0dim
                << " t0scaling=" << t0scaling);
            nettingSetScaling_[nid] = t0scaling;
        }

        Real nettingSetDimScaling =
            nettingSetScaling_.find(nid) == nettingSetScaling_.end() ? 1.0 : nettingSetScaling_[nid];
        LOG("Netting set DIM scaling factor: " << nettingSetDimScaling);

        for (Size j = 0; j < stopDatesLoop; ++j)
            nettingSetExpectedDIM_[nid][j] = 0.0;

        for (Size j = 0; j < stopDatesLoop; ++j) {

            for (Size k = 0; k < samples; ++k) {
                Real num = scenarioData_->get(j, k, AggregationScenarioDataType::Numeraire);
                Real tmp = dimHelper_->var(nid, ddvOrder_, quantile_, thetaFactor, j, k);
                tmp *= nettingSetDimScaling / num;

                nettingSetDIM_[nid][j][k] = tmp;
                nettingSetExpectedDIM_[nid][j] += tmp / samples;
                dimCube_->set(tmp, i, j, k);
            }
            // DLOG("DDV Calculator, date=" << j << ", horizonScaling=" << horizonScaling << ", thetaFactor=" <<
            // thetaFactor);
        }

        i++;
    }

    // current dim

    currentDIM_.clear();
    for (const auto& nid : nettingSetIds_) {
        DLOG("Process netting set " << nid);
        currentDIM_[nid] = dimHelper_->var(nid, ddvOrder_, quantile_, thetaFactor);
        DLOG("T0 IM (DDV) - {" << nid << "} = " << currentDIM_[nid]);
    }
}

} // namespace analytics
} // namespace ore
