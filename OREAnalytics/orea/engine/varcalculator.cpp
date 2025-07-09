/*
 Copyright (C) 2017 Quaternion Risk Management Ltd
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
#include <orea/engine/varcalculator.hpp>
#include <ored/utilities/to_string.hpp>
#include <qle/math/deltagammavar.hpp>

namespace ore {
namespace analytics {

VarReport::VarReport(const std::string& baseCurrency, const QuantLib::ext::shared_ptr<Portfolio>& portfolio,
                     const std::string& portfolioFilter, const vector<Real>& p,
                     boost::optional<ore::data::TimePeriod> period,
                     const QuantLib::ext::shared_ptr<HistoricalScenarioGenerator>& hisScenGen,
                     std::unique_ptr<SensiRunArgs> sensiArgs, std::unique_ptr<FullRevalArgs> fullRevalArgs,
                     const bool breakdown)
    : MarketRiskReport(baseCurrency, portfolio, portfolioFilter, period, hisScenGen, std::move(sensiArgs),
                       std::move(fullRevalArgs), nullptr, breakdown),
      p_(p) {}

void VarReport::createReports(const ext::shared_ptr<MarketRiskReport::Reports>& reports) {
    int s = reports->reports().size();
    QL_REQUIRE(s >= 1 && s <= 2, "We should only report for VAR report");
    QuantLib::ext::shared_ptr<Report> report = reports->reports().at(0);
    // prepare report
    writeHeader(report);

    createAdditionalReports(reports);

    createVarCalculator();
}

void VarReport::writeReports(const ext::shared_ptr<MarketRiskReport::Reports>& reports,
                             const ext::shared_ptr<MarketRiskGroupBase>& riskGroup,
                             const ext::shared_ptr<TradeGroupBase>& tradeGroup) {

    int s = reports->reports().size();
    QL_REQUIRE(s >= 1 && s <= 2, "We should only report for VAR report");
    QuantLib::ext::shared_ptr<Report> report = reports->reports().at(0);

    auto rg = ext::dynamic_pointer_cast<MarketRiskGroup>(riskGroup);
    auto tg = ext::dynamic_pointer_cast<TradeGroup>(tradeGroup);

    std::vector<Real> vars = calcVarsForQuantiles();

    if (!close_enough(QuantExt::detail::absMax(vars), 0.0)) {
        report->next();
        report->add(tg->portfolioId());
        report->add(to_string(rg->riskClass()));
        report->add(to_string(rg->riskType()));
        for (auto const& v : vars)
            report->add(v);
    }

    writeAdditionalReports(reports, riskGroup, tradeGroup);
}

} // namespace analytics
} // namespace ore
