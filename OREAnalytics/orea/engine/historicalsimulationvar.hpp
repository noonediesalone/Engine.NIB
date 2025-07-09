/*
 Copyright (C) 2023 Quaternion Risk Management Ltd
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

/*! \file engine/historicalsimulationvar.hpp
    \brief Perform historical simulation var calculation for a given portfolio
    \ingroup engine
*/

#pragma once

#include <orea/engine/historicalpnlgenerator.hpp>
#include <orea/engine/sensitivityaggregator.hpp>
#include <orea/engine/sensitivitystream.hpp>
#include <orea/engine/varcalculator.hpp>
#include <orea/scenario/historicalscenariogenerator.hpp>
#include <orea/scenario/scenariosimmarketparameters.hpp>
#include <orea/scenario/sensitivityscenariodata.hpp>

#include <ored/report/report.hpp>
#include <ored/utilities/timeperiod.hpp>

#include <qle/math/covariancesalvage.hpp>

#include <ql/math/array.hpp>
#include <ql/math/matrix.hpp>

#include <map>
#include <set>

namespace ore {
namespace analytics {
using QuantLib::Array;
using QuantLib::Matrix;

class HistoricalSimulationVarCalculator : public VarCalculator {
public:
    explicit HistoricalSimulationVarCalculator(const std::vector<QuantLib::Real>& pnls) : pnls_(pnls) {}

    QuantLib::Real var(QuantLib::Real confidence, const bool isCall = true,
        const std::set<std::pair<std::string, QuantLib::Size>>& tradeIds = {}) const override;

    QuantLib::Real expectedShortfall(QuantLib::Real confidence, const bool isCall = true,
                                     const std::set<std::pair<std::string, QuantLib::Size>>& tradeIds = {}) const;

private:
    const std::vector<QuantLib::Real>& pnls_;
};

//! HistoricalSimulation VaR Calculator
/*! This class takes sensitivity data and a covariance matrix as an input and computes a Historical Simulation value at risk. The
 * output can be broken down by portfolios, risk classes (IR, FX, EQ, ...) and risk types (delta-gamma, vega, ...). */
class HistoricalSimulationVarReport : public VarReport {
public:
    ~HistoricalSimulationVarReport() override = default;

    HistoricalSimulationVarReport(const std::string& baseCurrency,
                                  const QuantLib::ext::shared_ptr<Portfolio>& portfolio,
                                  const std::string& portfolioFilter, const vector<Real>& p,
                                  boost::optional<ore::data::TimePeriod> period,
                                  const QuantLib::ext::shared_ptr<HistoricalScenarioGenerator>& hisScenGen = nullptr,
                                  std::unique_ptr<FullRevalArgs> fullRevalArgs = nullptr, const bool breakdown = false,
                                  const bool includeExpectedShortfall = false, const bool tradePnl = false);

    void createAdditionalReports(const QuantLib::ext::shared_ptr<MarketRiskReport::Reports>& reports) override;

    /*! Check if the given scenario \p filter turns off all risk factors in the
        historical scenario generator
    */
    bool disablesAll(const QuantLib::ext::shared_ptr<ore::analytics::ScenarioFilter>& filter) const override;

protected:
    void createVarCalculator() override;
    void writeHeader(const QuantLib::ext::shared_ptr<Report>& report) const override;
    std::vector<Real> calcVarsForQuantiles() const override;
    void handleFullRevalResults(const QuantLib::ext::shared_ptr<MarketRiskReport::Reports>& reports,
                                const QuantLib::ext::shared_ptr<MarketRiskGroupBase>& riskGroup,
                                const QuantLib::ext::shared_ptr<TradeGroupBase>& tradeGroup) override;

    void writeAdditionalReports(const QuantLib::ext::shared_ptr<MarketRiskReport::Reports>& reports,
                                const QuantLib::ext::shared_ptr<MarketRiskGroupBase>& riskGroup,
                                const QuantLib::ext::shared_ptr<TradeGroupBase>& tradeGroup) override;

private:
    std::vector<QuantLib::Real> pnls_;
    ore::analytics::TradePnLStore tradePnls_;
    bool includeExpectedShortfall_ = false;
    bool tradePnl_ = false;
};

} // namespace analytics
} // namespace ore