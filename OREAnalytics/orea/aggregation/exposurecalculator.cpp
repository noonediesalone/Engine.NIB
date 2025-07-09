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

#include <orea/aggregation/exposurecalculator.hpp>
#include <orea/cube/inmemorycube.hpp>

#include <ored/portfolio/structuredtradeerror.hpp>
#include <ored/portfolio/trade.hpp>

#include <ql/time/date.hpp>
#include <ql/time/calendars/weekendsonly.hpp>

using namespace std;
using namespace QuantLib;

namespace ore {
namespace analytics {

ExposureCalculator::ExposureCalculator(
    const QuantLib::ext::shared_ptr<Portfolio>& portfolio, const QuantLib::ext::shared_ptr<NPVCube>& cube,
    const QuantLib::ext::shared_ptr<CubeInterpretation> cubeInterpretation,
    const QuantLib::ext::shared_ptr<AggregationScenarioData>& aggregationScenarioData,
    const QuantLib::ext::shared_ptr<Market>& market, bool exerciseNextBreak, const string& baseCurrency,
    const string& configuration, const Real quantile, const CollateralExposureHelper::CalculationType calcType,
    const bool multiPath, const bool flipViewXVA, const bool exposureProfilesUseCloseOutValues, bool continueOnError,
    bool useDoublePrecisionCubes)
    : portfolio_(portfolio), cube_(cube), cubeInterpretation_(cubeInterpretation),
      aggregationScenarioData_(aggregationScenarioData), market_(market), exerciseNextBreak_(exerciseNextBreak),
      baseCurrency_(baseCurrency), configuration_(configuration), quantile_(quantile), calcType_(calcType),
      multiPath_(multiPath), dates_(cube->dates()), today_(market_->asofDate()), dc_(ActualActual(ActualActual::ISDA)),
      flipViewXVA_(flipViewXVA), exposureProfilesUseCloseOutValues_(exposureProfilesUseCloseOutValues),
      continueOnError_(continueOnError) {

    QL_REQUIRE(portfolio_, "portfolio is null");

    // EPE, ENE, allocatedEPE, allocatedENE
    if (useDoublePrecisionCubes) {
        exposureCube_ = QuantLib::ext::make_shared<InMemoryCubeOpt<double>>(
            market->asofDate(), portfolio_->ids(), dates_, multiPath ? cube_->samples() : 1, EXPOSURE_CUBE_DEPTH);
    } else {
        exposureCube_ = QuantLib::ext::make_shared<InMemoryCubeOpt<float>>(
            market->asofDate(), portfolio_->ids(), dates_, multiPath ? cube_->samples() : 1, EXPOSURE_CUBE_DEPTH);
    }

    set<string> nettingSetIdsSet;
    for (const auto& [tradeId, trade] : portfolio->trades())
        nettingSetIdsSet.insert(trade->envelope().nettingSetId());
    nettingSetIds_= vector<string>(nettingSetIdsSet.begin(), nettingSetIdsSet.end());

    times_ = vector<Real>(dates_.size(), 0.0);
    for (Size i = 0; i < dates_.size(); i++)
        times_[i] = dc_.yearFraction(today_, cube_->dates()[i]);

    isRegularCubeStorage_ = !cubeInterpretation_->withCloseOutLag();
}

void ExposureCalculator::build() {
    LOG("Compute trade exposure profiles, " << (flipViewXVA_ ? "inverted (flipViewXVA = Y)" : "regular (flipViewXVA = N)"));
    const Date today = market_->asofDate();
    const DayCounter dc = ActualActual(ActualActual::ISDA);

    vector<Real> times(cube_->dates().size(), 0.0);
    vector<Real> timeDeltas(cube_->dates().size(), 0.0);
    for (Size i = 0; i < cube_->dates().size(); i++) {
        times[i] = dc.yearFraction(today, cube_->dates()[i]);
        timeDeltas[i] = times[i] - (i > 0 ? times[i - 1] : 0.0);
    }
    /*The time average in the EEPE calculation is taken over the first year of the exposure evolution
        (or until maturity if all positions of the netting set mature before one year).
        This one year point is actually taken to be today+1Y+4D, so that the 1Y point on the dateGrid is always
        included.
        This may effect DateGrids with daily data points*/
    const Date baselMaxEEPDate = WeekendsOnly().adjust(today + 1 * Years + 4 * Days);
    for (auto const& [tradeId, trade] : portfolio_->trades()) {
        string nettingSetId = trade->envelope().nettingSetId();
        std::size_t i = cube_->getTradeIndex(tradeId);
        LOG("Aggregate exposure for trade " << tradeId);
        if (nettingSetDefaultValue_.find(nettingSetId) == nettingSetDefaultValue_.end()) {
            nettingSetDefaultValue_[nettingSetId] = vector<vector<Real>>(dates_.size(), vector<Real>(cube_->samples(), 0.0));
            nettingSetCloseOutValue_[nettingSetId] = vector<vector<Real>>(dates_.size(), vector<Real>(cube_->samples(), 0.0));
            nettingSetMporPositiveFlow_[nettingSetId] = vector<vector<Real>>(dates_.size(), vector<Real>(cube_->samples(), 0.0));
            nettingSetMporNegativeFlow_[nettingSetId] = vector<vector<Real>>(dates_.size(), vector<Real>(cube_->samples(), 0.0));
        }

        // Identify the next break date if provided, default is trade maturity.
        Date nextBreakDate = trade->maturity();
        TradeActions ta = trade->tradeActions();
        if (exerciseNextBreak_ && !ta.empty()) {
            // loop over actions and pick next mutual break, if available
            const vector<TradeAction>& actions = ta.actions();
            try {
                for (Size j = 0; j < actions.size(); ++j) {
                    DLOG("TradeAction for " << tradeId << ", actionType " << actions[j].type() << ", actionOwner "
                                            << actions[j].owner());
                    // FIXME: Introduce enumeration and parse text when building trade
                    if (actions[j].type() == "Break" && actions[j].owner() == "Mutual") {
                        QuantLib::Schedule schedule = ore::data::makeSchedule(actions[j].schedule());
                        vector<Date> dates = schedule.dates();
                        std::sort(dates.begin(), dates.end());
                        Date today = Settings::instance().evaluationDate();
                        for (Size k = 0; k < dates.size(); ++k) {
                            if (dates[k] > today && dates[k] < nextBreakDate) {
                                nextBreakDate = dates[k];
                                DLOG("Next break date for trade " << tradeId << ": "
                                                                << QuantLib::io::iso_date(nextBreakDate));
                                break;
                            }
                        }
                    }
                }
            } catch (std::exception& e) {
                if (continueOnError_) {
                    StructuredTradeErrorMessage(tradeId, trade->tradeType(),
                                                "Error processing trade actions",
                                                std::string(e.what()) + ", excluding trade from netting set " + std::string(nettingSetId) + ".")
                        .log();
                    ee_b_[tradeId] = std::vector<Real>(dates_.size() + 1, 0.0);
                    eee_b_[tradeId] = std::vector<Real>(dates_.size() + 1, 0.0);
                    pfe_[tradeId] = std::vector<Real>(dates_.size() + 1, 0.0);
                    epe_b_[tradeId] = 0.0;
                    eepe_b_[tradeId] = 0.0;
                    epe_bTimeWeighted_[tradeId] = std::vector<Real>(dates_.size() + 1, 0.0);
                    eepe_bTimeWeighted_[tradeId] = std::vector<Real>(dates_.size() + 1, 0.0);
                } else {
                    StructuredTradeErrorMessage(tradeId, trade->tradeType(),
                                                "Error processing trade actions",
                                                e.what())
                        .log();
                    throw e;
                }
            }
        }

        Handle<YieldTermStructure> curve = market_->discountCurve(baseCurrency_, configuration_);
        Real npv0;
        if (flipViewXVA_) {
            npv0 = -cube_->getT0(i);
        } else {
            npv0 = cube_->getT0(i);
        }
        Real epe_b_runningSum = 0.0;
        Real eepe_b_runningSum = 0.0;
        vector<Real> epe(dates_.size() + 1, 0.0);
        vector<Real> ene(dates_.size() + 1, 0.0);
        vector<Real> ee_b(dates_.size() + 1, 0.0);
        vector<Real> eee_b(dates_.size() + 1, 0.0);
        vector<Real> pfe(dates_.size() + 1, 0.0);
        vector<Real> epe_b(dates_.size() + 1, 0.0);
        vector<Real> eepe_b(dates_.size() + 1, 0.0);
        epe[0] = std::max(npv0, 0.0);
        ene[0] = std::max(-npv0, 0.0);
        ee_b[0] = epe[0];
        eee_b[0] = ee_b[0];
        epe_b[0] = ee_b[0];
        eepe_b[0] = eee_b[0];
        pfe[0] = std::max(npv0, 0.0);
        exposureCube_->setT0(epe[0], tradeId, ExposureIndex::EPE);
        exposureCube_->setT0(ene[0], tradeId, ExposureIndex::ENE);
        for (Size j = 0; j < dates_.size(); ++j) {
            Date d = cube_->dates()[j];
            vector<Real> distribution(cube_->samples(), 0.0);
            for (Size k = 0; k < cube_->samples(); ++k) {
                // RL 2020-07-17
                // 1) If the calculation type is set to NoLag:
                //    Collateral balances are NOT delayed by the MPoR, but we use the close-out NPV.
                // 2) Otherwise:
                //    Collateral balances are delayed by the MPoR (if possible, i.e. the valuation
                //    grid has MPoR spacing), and we use the default date NPV.
                //    This is the treatment in the ORE releases up to June 2020).
                Real defaultValue =
                    d > nextBreakDate && exerciseNextBreak_ ? 0.0 : cubeInterpretation_->getDefaultNpv(cube_, i, j, k);
                Real closeOutValue;
                if (isRegularCubeStorage_ && j == dates_.size() - 1)
                    closeOutValue = defaultValue;
                else
                    closeOutValue = d > nextBreakDate && exerciseNextBreak_
                                        ? 0.0
                                        : cubeInterpretation_->getCloseOutNpv(cube_, i, j, k, aggregationScenarioData_);

                Real positiveCashFlow = cubeInterpretation_->getMporPositiveFlows(cube_, i, j, k);
                Real negativeCashFlow = cubeInterpretation_->getMporNegativeFlows(cube_, i, j, k);
                // for single trade exposures, the default value is relevant, unless we force
		// using the close out value instead
                Real npv = exposureProfilesUseCloseOutValues_ ? closeOutValue : defaultValue;
                epe[j + 1] += std::max(npv, 0.0) / cube_->samples();
                ene[j + 1] += std::max(-npv, 0.0) / cube_->samples();
                nettingSetDefaultValue_[nettingSetId][j][k] += defaultValue;
                nettingSetCloseOutValue_[nettingSetId][j][k] += closeOutValue;
                nettingSetMporPositiveFlow_[nettingSetId][j][k] += positiveCashFlow;
                nettingSetMporNegativeFlow_[nettingSetId][j][k] += negativeCashFlow;
                distribution[k] = npv;
                if (multiPath_) {
                    exposureCube_->set(std::max(npv, 0.0), tradeId, d, k, ExposureIndex::EPE);
                    exposureCube_->set(std::max(-npv, 0.0), tradeId, d, k, ExposureIndex::ENE);
                }
            }
            if (!multiPath_) {
                exposureCube_->set(epe[j + 1], tradeId, d, 0, ExposureIndex::EPE);
                exposureCube_->set(ene[j + 1], tradeId, d, 0, ExposureIndex::ENE);
            }
            ee_b[j + 1] = epe[j + 1] / curve->discount(cube_->dates()[j]);
            eee_b[j + 1] = std::max(eee_b[j], ee_b[j + 1]);
            if (d <= trade->maturity()) {
                epe_b_runningSum += ee_b[j + 1] * timeDeltas[j];
                eepe_b_runningSum += eee_b[j + 1] * timeDeltas[j];
                epe_b[j + 1] = epe_b_runningSum / times[j];
                eepe_b[j + 1] = eepe_b_runningSum / times[j];
                if(d <= baselMaxEEPDate){
                    epe_b_[tradeId] = epe_b[j + 1];
                    eepe_b_[tradeId] = eepe_b[j + 1];
                }
            }
            std::sort(distribution.begin(), distribution.end());
            Size index = Size(floor(quantile_ * (cube_->samples() - 1) + 0.5));
            pfe[j + 1] = std::max(distribution[index], 0.0);
        }
        ee_b_[tradeId] = ee_b;
        eee_b_[tradeId] = eee_b;
        pfe_[tradeId] = pfe;
        epe_bTimeWeighted_[tradeId] = epe_b;
        eepe_bTimeWeighted_[tradeId] = eepe_b;
    }
}

vector<Real> ExposureCalculator::getMeanExposure(const string& tid, ExposureIndex index) {
    Size tidx = exposureCube_->getTradeIndex(tid);
    vector<Real> exp(dates_.size() + 1, 0.0);
    exp[0] = exposureCube_->getT0(tidx, index);
    for (Size i = 0; i < dates_.size(); i++) {
        for (Size k = 0; k < exposureCube_->samples(); k++) {
            exp[i + 1] += exposureCube_->get(tidx, i, k, index);
        }
        exp[i + 1] /= exposureCube_->samples();
    }
    return exp;
}


} // namespace analytics
} // namespace ore
