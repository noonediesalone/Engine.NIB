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

#include <orea/app/analytics/xvastressanalytic.hpp>

#include <orea/app/analytics/xvaanalytic.hpp>
#include <orea/app/analytics/analyticfactory.hpp>
#include <orea/app/structuredanalyticserror.hpp>
#include <orea/app/structuredanalyticswarning.hpp>
#include <orea/cube/cube_io.hpp>
#include <orea/engine/parstressconverter.hpp>
#include <orea/scenario/clonescenariofactory.hpp>
#include <orea/scenario/scenariosimmarket.hpp>
#include <orea/scenario/stressscenariogenerator.hpp>
#include <ored/report/utilities.hpp>
namespace ore {
namespace analytics {

void XvaStressAnalyticImpl::writeCubes(const std::string& label,
                                       const QuantLib::ext::shared_ptr<Analytic>& xvaAnalytic) {
    if (!inputs_->xvaStressWriteCubes() || xvaAnalytic == nullptr) {
        return;
    }

    if (inputs_->rawCubeOutput()) {
        DLOG("Write raw cube under scenario " << label);
        xvaAnalytic->getReport("XVA", "rawcube")->toFile(inputs_->resultsPath().string() + "/rawcube_" + label +
                                                         ".csv");
    }

    if (inputs_->netCubeOutput()) {
        DLOG("Write raw cube under scenario " << label);
        xvaAnalytic->getReport("XVA", "netcube")->toFile(inputs_->resultsPath().string() + "/netcube_" + label +
                                                         ".csv");
    }

    if (inputs_->writeCube()) {
        const auto& cubes = xvaAnalytic->npvCubes()["XVA"];
        for (const auto& [name, cube] : cubes) {
            DLOG("Write cube under scenario " << name << " for scenario" << label);
            // analytic()->npvCubes()["XVA_STRESS"][name + "_" + label] = cube;
            NPVCubeWithMetaData r;
            r.cube = cube;
            if (name == "cube") {
                // store meta data together with npv cube
                r.scenarioGeneratorData = inputs_->scenarioGeneratorData();
                r.storeFlows = inputs_->storeFlows();
                r.storeCreditStateNPVs = inputs_->storeCreditStateNPVs();
            }
            saveCube(inputs_->resultsPath().string() + "/" + name + "_" + label + ".csv.gz", r);
        }
    }

    if (inputs_->writeScenarios()) {
        DLOG("Write scenario report under scenario " << label);
        // analytic()->addReport("XVA_STRESS"]["scenario" + label] = xvaAnalytic->reports()["XVA"]["scenario"];
        xvaAnalytic->getReport("XVA", "scenario")->toFile(inputs_->resultsPath().string() + "/scenario" + label +
                                                          ".csv");
    }
}

XvaStressAnalyticImpl::XvaStressAnalyticImpl(const QuantLib::ext::shared_ptr<InputParameters>& inputs,
                                             const boost::optional<QuantLib::ext::shared_ptr<StressTestScenarioData>>& scenarios)
    : Analytic::Impl(inputs), stressScenarios_(scenarios.get_value_or(inputs->xvaStressScenarioData())) {
    setLabel(LABEL);
}

void XvaStressAnalyticImpl::buildDependencies() {
}

void XvaStressAnalyticImpl::runAnalytic(const QuantLib::ext::shared_ptr<ore::data::InMemoryLoader>& loader,
                                        const std::set<std::string>& runTypes) {

    // basic setup

    LOG("Running XVA Stress analytic.");

    SavedSettings settings;
    
    optional<bool> localIncTodaysCashFlows = inputs_->exposureIncludeTodaysCashFlows();
    Settings::instance().includeTodaysCashFlows() = localIncTodaysCashFlows;
    LOG("Simulation IncludeTodaysCashFlows is defined: " << (localIncTodaysCashFlows ? "true" : "false"));
    if (localIncTodaysCashFlows) {
        LOG("Exposure IncludeTodaysCashFlows is set to " << (*localIncTodaysCashFlows ? "true" : "false"));
    }
    
    bool localIncRefDateEvents = inputs_->exposureIncludeReferenceDateEvents();
    Settings::instance().includeReferenceDateEvents() = localIncRefDateEvents;
    LOG("Simulation IncludeReferenceDateEvents is set to " << (localIncRefDateEvents ? "true" : "false"));
    
    Settings::instance().evaluationDate() = inputs_->asof();

    QL_REQUIRE(inputs_->portfolio(), "XvaStressAnalytic::run: No portfolio loaded.");
    
    std::string marketConfig = inputs_->marketConfig("pricing"); // FIXME

    // build t0, sim market, stress scenario generator

    CONSOLEW("XVA_STRESS: Build T0 and Sim Markets and Stress Scenario Generator");

    analytic()->buildMarket(loader);

    QuantLib::ext::shared_ptr<StressTestScenarioData> scenarioData = stressScenarios_;
    if (scenarioData != nullptr && scenarioData->hasScenarioWithParShifts()) {
        try {
            ParStressTestConverter converter(
                inputs_->asof(), analytic()->configurations().todaysMarketParams,
                analytic()->configurations().simMarketParams, analytic()->configurations().sensiScenarioData,
                analytic()->configurations().curveConfig, analytic()->market(), inputs_->iborFallbackConfig());
            scenarioData = converter.convertStressScenarioData(scenarioData);
            analytic()->stressTests()[label()]["stress_ZeroStressData"] = scenarioData;
        } catch (const std::exception& e) {
            StructuredAnalyticsErrorMessage(label(), "ParConversionFailed", e.what()).log();
        }
    }

    LOG("XVA Stress: Build SimMarket and StressTestScenarioGenerator")
    auto simMarket = QuantLib::ext::make_shared<ScenarioSimMarket>(
        analytic()->market(), analytic()->configurations().simMarketParams, marketConfig,
        *analytic()->configurations().curveConfig, *analytic()->configurations().todaysMarketParams,
        inputs_->continueOnError(), scenarioData->useSpreadedTermStructures(), false, false,
        *inputs_->iborFallbackConfig(), true);

    auto baseScenario = simMarket->baseScenario();
    auto scenarioFactory = QuantLib::ext::make_shared<CloneScenarioFactory>(baseScenario);
    auto scenarioGenerator = QuantLib::ext::make_shared<StressScenarioGenerator>(
        scenarioData, baseScenario, analytic()->configurations().simMarketParams, simMarket, scenarioFactory,
        simMarket->baseScenarioAbsolute());
    simMarket->scenarioGenerator() = scenarioGenerator;

    CONSOLE("OK");

    // generate the stress scenarios and run dependent xva analytic under each of them

    CONSOLE("XVA_STRESS: Running stress scenarios");

    // run stress test
    LOG("Run XVA Stresstest")
    runStressTest(scenarioGenerator, loader);

    LOG("Running XVA Stress analytic finished.");
}

void XvaStressAnalyticImpl::runStressTest(const QuantLib::ext::shared_ptr<StressScenarioGenerator>& scenarioGenerator,
                                          const QuantLib::ext::shared_ptr<ore::data::InMemoryLoader>& loader) {

    std::map<std::string, std::vector<QuantLib::ext::shared_ptr<ore::data::InMemoryReport>>> xvaReports;
    for (size_t i = 0; i < scenarioGenerator->samples(); ++i) {
        auto scenario = scenarioGenerator->next(inputs_->asof());
        const std::string& label = scenario != nullptr ? scenario->label() : std::string();
        try {
            DLOG("Calculate XVA for scenario " << label);
            CONSOLE("XVA_STRESS: Apply scenario " << label);
            auto newAnalytic = AnalyticFactory::instance().build("XVA", inputs_, analytic()->analyticsManager(), false).second;
            auto xvaImpl = static_cast<XvaAnalyticImpl*>(newAnalytic->impl().get());
            xvaImpl->setOffsetScenario(scenario);
            xvaImpl->setOffsetSimMarketParams(analytic()->configurations().simMarketParams);

            CONSOLE("XVA_STRESS: Calculate Exposure and XVA")
            newAnalytic->runAnalytic(loader, {"EXPOSURE", "XVA"});
            // Collect exposure and xva reports
            auto rpts = newAnalytic->reports();
            auto it = rpts.find("XVA");
            QL_REQUIRE(it != rpts.end(), "XVA report not found in XVA analytic reports");
            for (auto [name, rpt] : it->second) { 
                // add scenario column to report and copy it, concat it later
                if (boost::starts_with(name, "exposure") || boost::starts_with(name, "xva")) {
                    DLOG("Save and extend report " << name);
                    xvaReports[name].push_back(addColumnToExisitingReport("Scenario", label, rpt));
                }
            }
            writeCubes(label, newAnalytic);
            // FIXME: If the XVA analytic above is a dependent analytic, then we do not have to add this timer,
            // otherwise we have to manually add the XvaAnalytic::timer
            analytic()->addTimer("XVA analytic", newAnalytic->getTimer());
        } catch (const std::exception& e) {
            StructuredAnalyticsErrorMessage("XvaStress", "XVACalc",
                                            "Error during XVA calc under scenario " + label + ", got " + e.what() +
                                                ". Skip it")
                .log();
        }
    }
    concatReports(xvaReports);
}

void XvaStressAnalyticImpl::concatReports(
    const std::map<std::string, std::vector<QuantLib::ext::shared_ptr<ore::data::InMemoryReport>>>& xvaReports) {
    DLOG("Concat exposure and xva reports");
    for (auto& [name, reports] : xvaReports) {
        auto report = concatenateReports(reports);
        if (report != nullptr) {
            analytic()->addReport(label(), name, report);
        }
    }
}

void XvaStressAnalyticImpl::setUpConfigurations() {
    analytic()->configurations().todaysMarketParams = inputs_->todaysMarketParams();
    analytic()->configurations().simMarketParams = inputs_->xvaStressSimMarketParams();
    analytic()->configurations().sensiScenarioData = inputs_->xvaStressSensitivityScenarioData();
}

} // namespace analytics
} // namespace ore
