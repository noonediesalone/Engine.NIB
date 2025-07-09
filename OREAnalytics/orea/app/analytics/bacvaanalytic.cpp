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

#include <orea/app/analytics/bacvaanalytic.hpp>
#include <orea/app/reportwriter.hpp>

using namespace ore::data;
using namespace boost::filesystem;

namespace ore {
namespace analytics {

void BaCvaAnalyticImpl::setUpConfigurations() {
    analytic()->configurations().simulationConfigRequired = false;
    analytic()->configurations().todaysMarketParams = inputs_->todaysMarketParams();
}

void BaCvaAnalyticImpl::buildDependencies() {
    auto saccrAnalytic =
        AnalyticFactory::instance().build(saccrLookupKey, inputs_, analytic()->analyticsManager(), true).second;
    addDependentAnalytic(saccrLookupKey, saccrAnalytic);
}

void BaCvaAnalyticImpl::runAnalytic(const QuantLib::ext::shared_ptr<InMemoryLoader>& loader,
                                const std::set<std::string>& runTypes) {
    LOG("BaCvaAnalytic::runAnalytic called");

    analytic()->buildMarket(loader);

    auto bacvaAnalytic = static_cast<BaCvaAnalytic*>(analytic());
    QL_REQUIRE(bacvaAnalytic, "Analytic must be of type BaCvaAnalytic");

    auto saccrAnalytic = dependentAnalytic<SaCcrAnalytic>(saccrLookupKey);
    saccrAnalytic->runAnalytic(loader);

    LOG("Running BA-CVA calculation");
    QL_REQUIRE(inputs_->nettingSetManager() != nullptr, "No netting set configuration provided for BA-CVA calculation");

    // build BA-CVA calculator
    QuantLib::ext::shared_ptr<BaCvaCalculator> baCvaCalculator =
        QuantLib::ext::make_shared<BaCvaCalculator>(saccrAnalytic->saccr(), inputs_->baseCurrency());
    analytic()->addTimer("BaCvaCalculator", baCvaCalculator->timer());

    // generate report
    QuantLib::ext::shared_ptr<InMemoryReport> baCvaReport = QuantLib::ext::make_shared<InMemoryReport>();
    ReportWriter(inputs_->reportNaString()).writeBaCvaReport(baCvaCalculator, *baCvaReport);
    LOG("BA-CVA Calculation - Completed");

    analytic()->addReport(label(), "bacva", baCvaReport);
}

} // namespace analytics
} // namespace ore
