/*
 Copyright (C) 2024 Quaternion Risk Management Ltd
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

#include <orea/app/analytics/stresstestanalytic.hpp>
#include <orea/app/reportwriter.hpp>
#include <orea/engine/observationmode.hpp>
#include <orea/engine/parsensitivityanalysis.hpp>
#include <orea/engine/parstressconverter.hpp>
#include <orea/engine/stresstest.hpp>
#include <orea/scenario/scenariosimmarket.hpp>
#include <orea/app/structuredanalyticserror.hpp>

using namespace ore::data;
using namespace boost::filesystem;

namespace ore {
namespace analytics {

void StressTestAnalyticImpl::setUpConfigurations() {
    const auto stressData =  inputs_->stressScenarioData();
    analytic()->configurations().simulationConfigRequired = true;
    if (stressData != nullptr) {
        analytic()->configurations().sensitivityConfigRequired = stressData->hasScenarioWithParShifts();
    } else {
        analytic()->configurations().sensitivityConfigRequired = false;
    }
    analytic()->configurations().todaysMarketParams = inputs_->todaysMarketParams();
    analytic()->configurations().simMarketParams = inputs_->stressSimMarketParams();
    analytic()->configurations().sensiScenarioData = inputs_->stressSensitivityScenarioData();
    if (inputs_->stressGenerateCashflows())
        setGenerateAdditionalResults(true);
}

void StressTestAnalyticImpl::runAnalytic(const QuantLib::ext::shared_ptr<ore::data::InMemoryLoader>& loader,
                                         const std::set<std::string>& runTypes) {
    LOG("StressTestAnalytic::runAnalytic called");

    Settings::instance().evaluationDate() = inputs_->asof();
    ObservationMode::instance().setMode(inputs_->observationModel());
    QL_REQUIRE(inputs_->portfolio(), "StressTestAnalytic::run: No portfolio loaded.");

    CONSOLEW("StressTestAnalytic: Build Market");
    analytic()->buildMarket(loader);
    CONSOLE("OK");

    CONSOLEW("StressTestAnalytic: Build Portfolio");
    analytic()->buildPortfolio();
    CONSOLE("OK");

    analytic()->enrichIndexFixings(analytic()->portfolio());

    // This hook allows modifying the portfolio in derived classes before running the analytics below,
    // e.g. to apply SIMM exemptions.
    analytic()->modifyPortfolio();

    CONSOLEW("Risk: Stress Test Report");
    LOG("Stress Test Analysis called");

    QuantLib::ext::shared_ptr<StressTestScenarioData> scenarioData = inputs_->stressScenarioData();
    if (scenarioData != nullptr && scenarioData->hasScenarioWithParShifts()) {
        try{
            ParStressTestConverter converter(
            inputs_->asof(), analytic()->configurations().todaysMarketParams,
            analytic()->configurations().simMarketParams, analytic()->configurations().sensiScenarioData,
            analytic()->configurations().curveConfig, analytic()->market(), inputs_->iborFallbackConfig());
            scenarioData = converter.convertStressScenarioData(scenarioData);
            analytic()->stressTests()[label()]["stress_ZeroStressData"] = scenarioData;
        } catch(const std::exception& e){
            StructuredAnalyticsErrorMessage(label(), "ParConversionFailed", e.what()).log();
        }
    }

    Settings::instance().evaluationDate() = inputs_->asof();

    std::string marketConfig = inputs_->marketConfig("pricing");

    QuantLib::ext::shared_ptr<InMemoryReport> report =
        QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
    QuantLib::ext::shared_ptr<InMemoryReport> cfReport =
        inputs_->stressGenerateCashflows() ? QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize())
                                           : nullptr;

    runStressTest(analytic()->portfolio(), analytic()->market(), marketConfig, inputs_->pricingEngine(),
                  analytic()->configurations().simMarketParams, scenarioData, report, cfReport,
                  inputs_->stressThreshold(), inputs_->stressPrecision(), inputs_->includePastCashflows(),
                  *analytic()->configurations().curveConfig, *analytic()->configurations().todaysMarketParams, nullptr,
                  inputs_->refDataManager(), *inputs_->iborFallbackConfig(), inputs_->continueOnError());

    analytic()->addReport(label(), "stress", report);
    if (cfReport) {
        analytic()->addReport(label(), "stress_cashflows", cfReport);
    }

    CONSOLE("OK");
}

} // namespace analytics
} // namespace ore
