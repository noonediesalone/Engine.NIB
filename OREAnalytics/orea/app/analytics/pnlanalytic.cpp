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

#include <orea/app/analytics/pnlanalytic.hpp>
#include <orea/app/analytics/scenarioanalytic.hpp>
#include <orea/app/reportwriter.hpp>
#include <orea/engine/observationmode.hpp>
#include <orea/scenario/simplescenario.hpp>
#include <orea/scenario/scenariowriter.hpp>
#include <orea/scenario/scenarioutilities.hpp>

#include <ored/marketdata/structuredcurveerror.hpp>

using RFType = ore::analytics::RiskFactorKey::KeyType;

namespace ore {
namespace analytics {

void PnlAnalyticImpl::setUpConfigurations() {
    analytic()->configurations().simulationConfigRequired = true;

    analytic()->configurations().todaysMarketParams = inputs_->todaysMarketParams();
    analytic()->configurations().simMarketParams = inputs_->scenarioSimMarketParams();

    setGenerateAdditionalResults(true);
}

void PnlAnalyticImpl::buildDependencies() {
    auto mporAnalytic = AnalyticFactory::instance().build("SCENARIO", inputs_, analytic()->analyticsManager(), false);
    if (mporAnalytic.second) {
        mporAnalytic.second->configurations().curveConfig = inputs_->curveConfigs().get("mpor");
        auto sai = static_cast<ScenarioAnalyticImpl*>(mporAnalytic.second->impl().get());
        sai->setUseSpreadedTermStructures(true);
        addDependentAnalytic(mporLookupKey, mporAnalytic.second);
    }
}

void PnlAnalyticImpl::runAnalytic(const QuantLib::ext::shared_ptr<ore::data::InMemoryLoader>& loader,
    const std::set<std::string>& runTypes) {
    Settings::instance().evaluationDate() = inputs_->asof();
    analytic()->configurations().asofDate = inputs_->asof();
    ObservationMode::instance().setMode(inputs_->observationModel());

    QL_REQUIRE(inputs_->portfolio(), "PnlAnalytic::run: No portfolio loaded.");
    QL_REQUIRE(inputs_->portfolio()->size() > 0, "PnlAnalytic::run: Portfolio is empty.");

    std::string effectiveResultCurrency =
        inputs_->resultCurrency().empty() ? inputs_->baseCurrency() : inputs_->resultCurrency();

    /*******************************
     *
     * 0. Build market and portfolio
     *
     *******************************/

    analytic()->buildMarket(loader);

    /****************************************************
     *
     * 1. Write the t0 NPV and Additional Results reports
     *
     ****************************************************/

    auto marketConfig = inputs_->marketConfig("pricing");

    // Build a simMarket on the asof date
    QL_REQUIRE(analytic()->configurations().simMarketParams, "scenario sim market parameters not set");
    QL_REQUIRE(analytic()->configurations().todaysMarketParams, "today's market parameters not set");
    
    t0SimMarket_ = QuantLib::ext::make_shared<ScenarioSimMarket>(
        analytic()->market(), analytic()->configurations().simMarketParams, marketConfig,
        *analytic()->configurations().curveConfig, *analytic()->configurations().todaysMarketParams,
        inputs_->continueOnError(), useSpreadedTermStructures(), false, false, *inputs_->iborFallbackConfig());
    auto sgen = QuantLib::ext::make_shared<StaticScenarioGenerator>();
    t0SimMarket_->scenarioGenerator() = sgen;

    analytic()->setMarket(t0SimMarket_);
    analytic()->buildPortfolio();

    analytic()->enrichIndexFixings(analytic()->portfolio());

    QuantLib::ext::shared_ptr<InMemoryReport> t0NpvReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
    ReportWriter(inputs_->reportNaString())
        .writeNpv(*t0NpvReport, effectiveResultCurrency, analytic()->market(), marketConfig,
                  analytic()->portfolio());
    analytic()->addReport(LABEL, "pnl_npv_t0", t0NpvReport);

    if (inputs_->outputAdditionalResults()) {
        CONSOLEW("Pricing: Additional t0 Results");
        QuantLib::ext::shared_ptr<InMemoryReport> t0AddReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
        ReportWriter(inputs_->reportNaString())
            .writeAdditionalResultsReport(*t0AddReport, analytic()->portfolio(), analytic()->market(),
                                          marketConfig, effectiveResultCurrency);
        analytic()->addReport(LABEL, "pnl_additional_results_t0", t0AddReport);
        CONSOLE("OK");
    }

    /****************************************************
     *
     * 2. Write cash flow report for the clean actual P&L 
     *
     ****************************************************/

    QuantLib::ext::shared_ptr<InMemoryReport> t0CashFlowReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
    ReportWriter(inputs_->reportNaString())
      .writeCashflow(*t0CashFlowReport, effectiveResultCurrency, analytic()->portfolio(),
		     analytic()->market(), marketConfig, inputs_->includePastCashflows());
    analytic()->addReport(LABEL, "pnl_cashflow", t0CashFlowReport);
    
    /*******************************************************************************************
     *
     * 3. Prepare "NPV lagged" calculations by creating shift scenarios
     *    - to price the t0 portfolio as of t0 using the t1 market (risk hypothetical clean P&L)
     *    - to price the t0 portfolio as of t1 using the t0 market (theta, time decay)
     *
     *******************************************************************************************/

    // Set evaluationDate to t1 > t0
    Settings::instance().evaluationDate() = mporDate();

    // Set the configurations asof date for the mpor analytic to t1, too
    auto mporAnalytic = dependentAnalytic(mporLookupKey);
    mporAnalytic->configurations().asofDate = mporDate();
    mporAnalytic->configurations().todaysMarketParams = analytic()->configurations().todaysMarketParams;
    mporAnalytic->configurations().simMarketParams = analytic()->configurations().simMarketParams;

    // Run the mpor analytic to generate the market scenario as of t1
    mporAnalytic->runAnalytic(loader);

    // Set the evaluation date back to t0
    Settings::instance().evaluationDate() = inputs_->asof();
        
    QuantLib::ext::shared_ptr<Scenario> asofBaseScenario = t0SimMarket_->baseScenarioAbsolute();
    auto sai = static_cast<ScenarioAnalyticImpl*>(mporAnalytic->impl().get());
    QuantLib::ext::shared_ptr<Scenario> mporBaseScenario = sai->scenarioSimMarket()->baseScenarioAbsolute();

    QuantLib::ext::shared_ptr<ore::analytics::Scenario> t0Scenario =
        getDifferenceScenario(asofBaseScenario, mporBaseScenario, inputs_->asof(), 1.0); 
    setT0Scenario(asofBaseScenario);

    // Create the inverse shift scenario as spread between t0 market and t1 market, to be applied to t1

    QuantLib::ext::shared_ptr<ore::analytics::Scenario> t1Scenario =
        getDifferenceScenario(mporBaseScenario, asofBaseScenario, mporDate(), 1.0); 
    setT1Scenario(mporBaseScenario);

    /********************************************************************************************
     *
     * 4. Price the t0 portfolio as of t0 using the t1 market for the risk-hypothetical clean P&L
     *
     ********************************************************************************************/

    // Now update simMarket on asof date t0, with the t0 shift scenario
    sgen->setScenario(t0Scenario);
    t0SimMarket_->update(t0SimMarket_->asofDate());
    analytic()->setMarket(t0SimMarket_);

    // Build the portfolio, linked to the shifted market
    analytic()->buildPortfolio();

    analytic()->enrichIndexFixings(analytic()->portfolio());

    // This hook allows modifying the portfolio in derived classes before running the analytics below
    analytic()->modifyPortfolio();
    
    QuantLib::ext::shared_ptr<InMemoryReport> t0NpvLaggedReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
    ReportWriter(inputs_->reportNaString())
        .writeNpv(*t0NpvLaggedReport, effectiveResultCurrency, analytic()->market(), marketConfig,
                  analytic()->portfolio());

    if (inputs_->outputAdditionalResults()) {
        CONSOLEW("Pricing: Additional Results, t0 lagged");
        QuantLib::ext::shared_ptr<InMemoryReport> t0LaggedAddReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
        ReportWriter(inputs_->reportNaString())
            .writeAdditionalResultsReport(*t0LaggedAddReport, analytic()->portfolio(), analytic()->market(),
                                          marketConfig, effectiveResultCurrency);
        analytic()->addReport(LABEL, "pnl_additional_results_lagged_t0", t0LaggedAddReport);
        CONSOLE("OK");
    }
    analytic()->addReport(LABEL, "pnl_npv_lagged_t0", t0NpvLaggedReport);

    /***********************************************************************************************
     *
     * 5. Price the t0 portfolio as of t1 using the t0 market for the theta / time decay calculation
     *    Reusing the mpor analytic setup here which is as of t1 already.
     *
     ***********************************************************************************************/

    Date d1 = mporDate();
    Settings::instance().evaluationDate() = d1;
    analytic()->configurations().asofDate = d1;
    auto simMarket1 = sai->scenarioSimMarket();
    auto sgen1 = QuantLib::ext::make_shared<StaticScenarioGenerator>();
    analytic()->setMarket(simMarket1);
    sgen1->setScenario(t1Scenario);
    simMarket1->scenarioGenerator() = sgen1;
    simMarket1->update(d1);
    analytic()->buildPortfolio();

    analytic()->enrichIndexFixings(analytic()->portfolio());

    QuantLib::ext::shared_ptr<InMemoryReport> t1NpvLaggedReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
    ReportWriter(inputs_->reportNaString())
        .writeNpv(*t1NpvLaggedReport, effectiveResultCurrency, analytic()->market(), marketConfig,
                  analytic()->portfolio());

    analytic()->addReport(LABEL, "pnl_npv_lagged_t1", t1NpvLaggedReport);

    if (inputs_->outputAdditionalResults()) {
        CONSOLEW("Pricing: Additional Results t1");
        QuantLib::ext::shared_ptr<InMemoryReport> t1AddReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
        ReportWriter(inputs_->reportNaString())
            .writeAdditionalResultsReport(*t1AddReport, analytic()->portfolio(), analytic()->market(),
                                          marketConfig, effectiveResultCurrency);
        analytic()->addReport(LABEL, "pnl_additional_results_lagged_t1", t1AddReport);
        CONSOLE("OK");
    }

    /***************************************************************************************
     *
     * 6. Price the t0 portfolio as of t1 using the t1 market for the actual P&L calculation
     *
     ***************************************************************************************/
        
    sgen1->setScenario(sai->scenarioSimMarket()->baseScenario());
    simMarket1->scenarioGenerator() = sgen1;
    simMarket1->update(d1);

    analytic()->buildPortfolio();

    analytic()->enrichIndexFixings(analytic()->portfolio());

    QuantLib::ext::shared_ptr<InMemoryReport> t1Npvt0PortReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
    ReportWriter(inputs_->reportNaString())
        .writeNpv(*t1Npvt0PortReport, effectiveResultCurrency, analytic()->market(), marketConfig,
                  analytic()->portfolio());

    analytic()->addReport(LABEL, "pnl_npv_t1_port_t0", t1Npvt0PortReport);

    QuantLib::ext::shared_ptr<InMemoryReport> t1AddReport;
    if (inputs_->outputAdditionalResults()) {
        CONSOLEW("Pricing: Additional t1 Results");
        t1AddReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
        ReportWriter(inputs_->reportNaString())
            .writeAdditionalResultsReport(*t1AddReport, analytic()->portfolio(), analytic()->market(),
                                          marketConfig, effectiveResultCurrency);
        analytic()->addReport(LABEL, "pnl_additional_results_t1_port_t0", t1AddReport);
        CONSOLE("OK");
    }

    /***************************************************************************************
     *
     * 7. Price the t1 portfolio as of t1 using the t1 market for the actual P&L calculation
     *
     ***************************************************************************************/

    sgen1->setScenario(sai->scenarioSimMarket()->baseScenario());
    simMarket1->scenarioGenerator() = sgen1;
    simMarket1->update(d1);

    QuantLib::ext::shared_ptr<InMemoryReport> t1NpvReport;
    if (inputs_->mporPortfolio()) {

        analytic()->setPortfolio(inputs_->mporPortfolio());
        analytic()->buildPortfolio();

        analytic()->enrichIndexFixings(analytic()->portfolio());

        t1NpvReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
        ReportWriter(inputs_->reportNaString())
            .writeNpv(*t1NpvReport, effectiveResultCurrency, analytic()->market(), marketConfig,
                      analytic()->portfolio());

        if (inputs_->outputAdditionalResults()) {
            CONSOLEW("Pricing: Additional t1 Results");
            t1AddReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
            ReportWriter(inputs_->reportNaString())
                .writeAdditionalResultsReport(*t1AddReport, analytic()->portfolio(), analytic()->market(),
                                              marketConfig, effectiveResultCurrency);            
            CONSOLE("OK");
        }
    } else
        t1NpvReport = t1Npvt0PortReport;

    analytic()->addReport(LABEL, "pnl_npv_t1", t1NpvReport);
    if (inputs_->outputAdditionalResults())
        analytic()->addReport(LABEL, "pnl_additional_results_t1", t1AddReport);

    /****************************
     *
     * 8. Generate the P&L report
     *
     ****************************/

    // FIXME: check which market and which portfolio to pass to the report writer
    QuantLib::ext::shared_ptr<InMemoryReport> pnlReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
    ReportWriter(inputs_->reportNaString())
        .writePnlReport(*pnlReport, t0NpvReport, t0NpvLaggedReport, t1NpvLaggedReport, t1Npvt0PortReport, t1NpvReport,
			t0CashFlowReport, inputs_->asof(), mporDate(), effectiveResultCurrency, analytic()->market(), 
            marketConfig, analytic()->portfolio());
    analytic()->addReport(LABEL, "pnl", pnlReport);

    /***************************
     *
     * 9. Write Scenario Reports
     *
     ***************************/
    QuantLib::ext::shared_ptr<InMemoryReport> scenarioReport = QuantLib::ext::make_shared<InMemoryReport>(inputs_->reportBufferSize());
    auto sw = ScenarioWriter(nullptr, scenarioReport);
    sw.writeScenario(asofBaseScenario, true);
    sw.writeScenario(mporBaseScenario, false);
    analytic()->addReport(label(), "zero_scenarios", scenarioReport);
}

} // namespace analytics
} // namespace ore
