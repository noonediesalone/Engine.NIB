/*
 Copyright (C) 2022 Quaternion Risk Management Ltd
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
#include <orea/cube/inmemorycube.hpp>
#include <orea/cube/cube_io.hpp>
#include <orea/engine/observationmode.hpp>
#include <orea/engine/sensitivityfilestream.hpp>
#include <orea/engine/sacvasensitivityloader.hpp>
#include <orea/scenario/scenariofilereader.hpp>
#include <orea/scenario/shiftscenariogenerator.hpp>
#include <orea/scenario/simplescenariofactory.hpp>
#include <orea/simm/simmbucketmapperbase.hpp>
#include <ored/configuration/currencyconfig.hpp>
#include <ored/utilities/calendaradjustmentconfig.hpp>
#include <ored/utilities/parsers.hpp>
#include <ored/portfolio/scriptedtrade.hpp>
#include <orea/simm/crifloader.hpp>

namespace ore {
namespace analytics {

void scaleUpPortfolio(QuantLib::ext::shared_ptr<ore::data::Portfolio>& p) {
    if (auto param_N = getenv("PORTFOLIO_SCALE_UP")) {
        LOG("Scaling up portfolio using factor PORTFOLIO_SCALE_UP = " << param_N << " from environment variables.");
        std::cerr << "\n\n*** Scaling up portfolio using factor PORTFOLIO_SCALE_UP = " << param_N
                  << " from environment variables.\n\n"
                  << std::endl;
        std::string pfxml = p->toXMLString();
        p = QuantLib::ext::make_shared<ore::data::Portfolio>();
        for (Size i = 0; i < atoi(param_N); ++i) {
            auto tmp = QuantLib::ext::make_shared<ore::data::Portfolio>();
            tmp->fromXMLString(pfxml);
            for (auto const& [id, t] : tmp->trades()) {
                t->id() += "_" + std::to_string(i + 1);
                p->add(t);
            }
        }
    }
}

vector<string> getFileNames(const string& fileString, const std::filesystem::path& path) {
    vector<string> fileNames;
    boost::split(fileNames, fileString, boost::is_any_of(",;"), boost::token_compress_on);
    for (auto it = fileNames.begin(); it < fileNames.end(); it++) {
        boost::trim(*it);
        *it = (path / *it).generic_string();
    }
    return fileNames;
}

InputParameters::InputParameters() {
    iborFallbackConfig_ = QuantLib::ext::make_shared<IborFallbackConfig>(IborFallbackConfig::defaultConfig());
    simmBucketMapper_ = QuantLib::ext::make_shared<SimmBucketMapperBase>();
    loadParameters();
}

void InputParameters::setAsOfDate(const std::string& s) {
    asof_ = parseDate(s);
    Settings::instance().evaluationDate() = asof_;
}

void InputParameters::setMarketConfig(const std::string& config, const std::string& context) {
    auto it = marketConfigs_.find(context);
    QL_REQUIRE(it == marketConfigs_.end(),
               "market config " << it->second << " already set for context " << it->first);
    marketConfigs_[context] = config;
}

void InputParameters::setRefDataManager(const std::string& xml) {
    refDataManager_ = QuantLib::ext::make_shared<BasicReferenceDataManager>();
    refDataManager_->fromXMLString(xml);
}

void InputParameters::setRefDataManagerFromFile(const std::string& fileName) {
    refDataManager_ = QuantLib::ext::make_shared<BasicReferenceDataManager>(fileName);
}

void InputParameters::setBaselTrafficLightConfig(const std::string& xml) {
    baselTrafficLightConfig_ = QuantLib::ext::make_shared<BaselTrafficLightData>();
    baselTrafficLightConfig_->fromXMLString(xml);
}

void InputParameters::setBaselTrafficLightFromFile(const std::string& fileName) {
    baselTrafficLightConfig_ = QuantLib::ext::make_shared<BaselTrafficLightData>(fileName);
}

void InputParameters::setScriptLibrary(const std::string& xml) {
    scriptLibrary_ = xml;
    ScriptLibraryData data;
    data.fromXMLString(xml);
    ScriptLibraryStorage::instance().set(std::move(data));
}

void InputParameters::setScriptLibraryFromFile(const std::string& fileName) {
    ScriptLibraryData data;
    data.fromFile(fileName);
    scriptLibrary_ = data.toXMLString();
    ScriptLibraryStorage::instance().set(std::move(data));
}

void InputParameters::loadScriptLibrary() {
    if (!scriptLibrary_.empty())
        setScriptLibrary(scriptLibrary_);
}


void InputParameters::setConventions(const std::string& xml) {
    conventions_ = QuantLib::ext::make_shared<Conventions>();
    conventions_->fromXMLString(xml);
    InstrumentConventions::instance().setConventions(conventions_);
}
    
void InputParameters::setConventionsFromFile(const std::string& fileName) {
    conventions_ = QuantLib::ext::make_shared<Conventions>();
    conventions_->fromFile(fileName);
    InstrumentConventions::instance().setConventions(conventions_);
}

void InputParameters::setCurveConfigs(const std::string& xml) {
    auto curveConfig = QuantLib::ext::make_shared<CurveConfigurations>();
    curveConfig->fromXMLString(xml);
    curveConfigs_.add(curveConfig);
}

void InputParameters::setCurveConfigsFromFile(const std::string& fileName, std::string id) {
    auto curveConfig = QuantLib::ext::make_shared<CurveConfigurations>();
    curveConfig->fromFile(fileName);
    curveConfigs_.add(curveConfig, id);
}

void InputParameters::setCalendarAdjustment(const std::string& xml) {
    calendarAdjustment_ = QuantLib::ext::make_shared<CalendarAdjustmentConfig>();
    calendarAdjustment_->fromXMLString(xml);
}

void InputParameters::setCalendarAdjustmentFromFile(const std::string& fileName) {
    calendarAdjustment_ = QuantLib::ext::make_shared<CalendarAdjustmentConfig>();
    calendarAdjustment_->fromFile(fileName);
}

void InputParameters::setCurrencyConfig(const std::string& xml) {
    currencyConfig_ = QuantLib::ext::make_shared<CurrencyConfig>();
    currencyConfig_->fromXMLString(xml);
}

void InputParameters::setCurrencyConfigFromFile(const std::string& fileName) {
    currencyConfig_ = QuantLib::ext::make_shared<CurrencyConfig>();
    currencyConfig_->fromFile(fileName);
}

void InputParameters::setCounterpartyManager(const std::string& xml) {
    counterpartyManager_ = QuantLib::ext::make_shared<CounterpartyManager>();
    counterpartyManager_->fromXMLString(xml);
}

void InputParameters::setCounterpartyManagerFromFile(const std::string& fileName) {
    counterpartyManager_ = QuantLib::ext::make_shared<CounterpartyManager>();
    counterpartyManager_->fromFile(fileName);
}

void InputParameters::setIborFallbackConfig(const std::string& xml) {
    iborFallbackConfig_= QuantLib::ext::make_shared<IborFallbackConfig>();
    iborFallbackConfig_->fromXMLString(xml);
}

void InputParameters::setIborFallbackConfigFromFile(const std::string& fileName) {
    iborFallbackConfig_= QuantLib::ext::make_shared<IborFallbackConfig>();
    iborFallbackConfig_->fromFile(fileName);
}

void InputParameters::setPricingEngine(const std::string& xml) {
    pricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    pricingEngine_->fromXMLString(xml);
}

void InputParameters::setPricingEngineFromFile(const std::string& fileName) {
    pricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    pricingEngine_->fromFile(fileName);
}

void InputParameters::setTodaysMarketParams(const std::string& xml) {
    todaysMarketParams_ = QuantLib::ext::make_shared<TodaysMarketParameters>();
    todaysMarketParams_->fromXMLString(xml);
}

void InputParameters::setTodaysMarketParamsFromFile(const std::string& fileName) {
    todaysMarketParams_ = QuantLib::ext::make_shared<TodaysMarketParameters>();
    todaysMarketParams_->fromFile(fileName);
}

void InputParameters::setPortfolio(const QuantLib::ext::shared_ptr<Portfolio>& portfolio) {
    portfolio_ = portfolio;
    scaleUpPortfolio(portfolio_);
}

void InputParameters::setPortfolio(const std::string& xml) {
    portfolio_ = QuantLib::ext::make_shared<Portfolio>(buildFailedTrades_);
    portfolio_->fromXMLString(xml);
    scaleUpPortfolio(portfolio_);
}

void InputParameters::setPortfolioFromFile(const std::string& fileNameString, const std::filesystem::path& inputPath) {
    vector<string> files = getFileNames(fileNameString, inputPath);
    portfolio_ = QuantLib::ext::make_shared<Portfolio>(buildFailedTrades_);
    for (auto file : files) {
        LOG("Loading portfolio from file: " << file);
        portfolio_->fromFile(file);
    }
    scaleUpPortfolio(portfolio_);
}

void InputParameters::setMporPortfolio(const std::string& xml) {
    mporPortfolio_ = QuantLib::ext::make_shared<Portfolio>(buildFailedTrades_);
    mporPortfolio_->fromXMLString(xml);
    scaleUpPortfolio(mporPortfolio_);
}

void InputParameters::setMporPortfolioFromFile(const std::string& fileNameString, const std::filesystem::path& inputPath) {
    vector<string> files = getFileNames(fileNameString, inputPath);
    mporPortfolio_ = QuantLib::ext::make_shared<Portfolio>(buildFailedTrades_);
    for (auto file : files) {
        LOG("Loading mpor portfolio from file: " << file);
        mporPortfolio_->fromFile(file);
    }
    scaleUpPortfolio(mporPortfolio_);
}

void InputParameters::setMarketConfigs(const std::map<std::string, std::string>& m) {
    marketConfigs_ = m;
}
    
void InputParameters::setMporCalendar(const std::string& s) {
    mporCalendar_ = parseCalendar(s);
}

void InputParameters::setSensiSimMarketParams(const std::string& xml) {
    sensiSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    sensiSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setSensiSimMarketParamsFromFile(const std::string& fileName) {
    sensiSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    sensiSimMarketParams_->fromFile(fileName);
}
    
void InputParameters::setSensiScenarioData(const std::string& xml) {
    sensiScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    sensiScenarioData_->fromXMLString(xml);
}

void InputParameters::setSensiScenarioDataFromFile(const std::string& fileName) {
    sensiScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    sensiScenarioData_->fromFile(fileName);
}

void InputParameters::setSensiPricingEngine(const std::string& xml) {
    sensiPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    sensiPricingEngine_->fromXMLString(xml);
}

void InputParameters::setScenarioSimMarketParams(const std::string& xml) {
    scenarioSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    scenarioSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setScenarioSimMarketParamsFromFile(const std::string& fileName) {
    scenarioSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    scenarioSimMarketParams_->fromFile(fileName);
}

void InputParameters::setHistVarSimMarketParams(const std::string& xml) {
    histVarSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    histVarSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setHistVarSimMarketParamsFromFile(const std::string& fileName) {
    histVarSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    histVarSimMarketParams_->fromFile(fileName);
}

void InputParameters::setSensiPricingEngineFromFile(const std::string& fileName) {
    sensiPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    sensiPricingEngine_->fromFile(fileName);
}

void InputParameters::setStressSimMarketParams(const std::string& xml) {
    stressSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    stressSimMarketParams_->fromXMLString(xml);
}
    
void InputParameters::setStressSimMarketParamsFromFile(const std::string& fileName) {
    stressSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    stressSimMarketParams_->fromFile(fileName);
}
    
void InputParameters::setStressScenarioData(const std::string& xml) {
    stressScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    stressScenarioData_->fromXMLString(xml);
}
    
void InputParameters::setStressScenarioDataFromFile(const std::string& fileName) {
    stressScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    stressScenarioData_->fromFile(fileName);
}

void InputParameters::setStressSensitivityScenarioData(const std::string& xml) {
    stressSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    stressSensitivityScenarioData_->fromXMLString(xml);
}

void InputParameters::setStressSensitivityScenarioDataFromFile(const std::string& fileName) {
    stressSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    stressSensitivityScenarioData_->fromFile(fileName);
}

void InputParameters::setStressPricingEngine(const std::string& xml) {
    stressPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    stressPricingEngine_->fromXMLString(xml);
}

void InputParameters::setStressPricingEngineFromFile(const std::string& fileName) {
    stressPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    stressPricingEngine_->fromFile(fileName);
}

void InputParameters::setExposureSimMarketParams(const std::string& xml) {
    exposureSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    exposureSimMarketParams_->fromXMLString(xml);
}
    
void InputParameters::setExposureSimMarketParamsFromFile(const std::string& fileName) {
    exposureSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    exposureSimMarketParams_->fromFile(fileName);
}

void InputParameters::setScenarioGeneratorData(const std::string& xml) {
    scenarioGeneratorData_ = QuantLib::ext::make_shared<ScenarioGeneratorData>();
    scenarioGeneratorData_->fromXMLString(xml);
}

void InputParameters::setScenarioGeneratorDataFromFile(const std::string& fileName) {
    scenarioGeneratorData_ = QuantLib::ext::make_shared<ScenarioGeneratorData>();
    scenarioGeneratorData_->fromFile(fileName);
}

void InputParameters::setCrossAssetModelData(const std::string& xml) {
    crossAssetModelData_ = QuantLib::ext::make_shared<CrossAssetModelData>();
    crossAssetModelData_->fromXMLString(xml);
}

void InputParameters::setCrossAssetModelDataFromFile(const std::string& fileName) {
    crossAssetModelData_ = QuantLib::ext::make_shared<CrossAssetModelData>();
    crossAssetModelData_->fromFile(fileName);
}

void InputParameters::setSimulationPricingEngine(const std::string& xml) {
    simulationPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    simulationPricingEngine_->fromXMLString(xml);
}

void InputParameters::setSimulationPricingEngineFromFile(const std::string& fileName) {
    simulationPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    simulationPricingEngine_->fromFile(fileName);
}

void InputParameters::setAmcPricingEngine(const std::string& xml) {
    amcPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    amcPricingEngine_->fromXMLString(xml);
}

void InputParameters::setAmcCgPricingEngine(const std::string& xml) {
    amcCgPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    amcCgPricingEngine_->fromXMLString(xml);
}

void InputParameters::setXvaCgSensiScenarioData(const std::string& xml) {
    xvaCgSensiScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    xvaCgSensiScenarioData_->fromXMLString(xml);
}

void InputParameters::setXvaCgSensiScenarioDataFromFile(const std::string& fileName) {
    xvaCgSensiScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    xvaCgSensiScenarioData_->fromFile(fileName);
}

void InputParameters::setXvaStressSimMarketParams(const std::string& xml) {
    xvaStressSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    xvaStressSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setXvaStressSimMarketParamsFromFile(const std::string& fileName) {
    xvaStressSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    xvaStressSimMarketParams_->fromFile(fileName);
}

void InputParameters::setXvaStressScenarioData(const std::string& xml) {
    xvaStressScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    xvaStressScenarioData_->fromXMLString(xml);
}

void InputParameters::setXvaStressScenarioDataFromFile(const std::string& fileName) {
    xvaStressScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    xvaStressScenarioData_->fromFile(fileName);
}

void InputParameters::setXvaStressSensitivityScenarioData(const std::string& xml) {
    xvaStressSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    xvaStressSensitivityScenarioData_->fromXMLString(xml);
}

void InputParameters::setXvaStressSensitivityScenarioDataFromFile(const std::string& fileName) {
    xvaStressSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    xvaStressSensitivityScenarioData_->fromFile(fileName);
}

void InputParameters::setSensitivityStressSimMarketParams(const std::string& xml) {
    sensitivityStressSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    sensitivityStressSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setSensitivityStressSimMarketParamsFromFile(const std::string& fileName) {
    sensitivityStressSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    sensitivityStressSimMarketParams_->fromFile(fileName);
}

void InputParameters::setSensitivityStressScenarioData(const std::string& xml) {
    sensitivityStressScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    sensitivityStressScenarioData_->fromXMLString(xml);
}

void InputParameters::setSensitivityStressScenarioDataFromFile(const std::string& fileName) {
    sensitivityStressScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    sensitivityStressScenarioData_->fromFile(fileName);
}

void InputParameters::setSensitivityStressSensitivityScenarioData(const std::string& xml) {
    sensitivityStressSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    sensitivityStressSensitivityScenarioData_->fromXMLString(xml);
}

void InputParameters::setSensitivityStressSensitivityScenarioDataFromFile(const std::string& fileName) {
    sensitivityStressSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    sensitivityStressSensitivityScenarioData_->fromFile(fileName);
}

void InputParameters::setXvaSensiSimMarketParams(const std::string& xml) {
    xvaSensiSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    xvaSensiSimMarketParams_->fromXMLString(xml);
}
void InputParameters::setXvaSensiSimMarketParamsFromFile(const std::string& fileName) {
    xvaSensiSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    xvaSensiSimMarketParams_->fromFile(fileName);
}
void InputParameters::setXvaSensiScenarioData(const std::string& xml) {
    xvaSensiScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    xvaSensiScenarioData_->fromXMLString(xml);
}
void InputParameters::setXvaSensiScenarioDataFromFile(const std::string& fileName) {
    xvaSensiScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    xvaSensiScenarioData_->fromFile(fileName);
}
void InputParameters::setXvaSensiPricingEngine(const std::string& xml) {
    xvaSensiPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    xvaSensiPricingEngine_->fromXMLString(xml);
}
void InputParameters::setXvaSensiPricingEngineFromFile(const std::string& fileName) {
    xvaSensiPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    xvaSensiPricingEngine_->fromFile(fileName);
}

// SA-CVA

void InputParameters::setSaCvaNetSensitivitiesFromFile(const std::string& fileName) {
    SaCvaSensitivityLoader cvaLoader;
    cvaLoader.load(fileName, '\n', ',');
    saCvaNetSensitivities_ = cvaLoader.netRecords();

    LOG("InputParameters::setSaCvaNetSensitivitiesFromFile: " << saCvaNetSensitivities_.size() << " records");
}

void InputParameters::setCvaSensitivitiesFromFile(const std::string& fileName) {
    // FIXME:
    // I would prefer to use the SensitivityFileStream here for reading SensitivityRecords, but the XVA sensitivity
    // file format has an extra column (nettingSetId), to cover both trade-level and nettingSet-level sensitivites.
    // Is that really necessary?
    // If we could reuse the SensitivityFileStream here we could pass the SensitivityRecords to the
    // SaCvaSensitivityLoader to map to CvaSensitivityRecords, very similar to the
    // SaCvaSensitivityLoader::loadFromRawSensis method that uses a ParSensitivityCubeStream.

    cvaSensitivities_.clear();

    std::ifstream iss(fileName);
    if (!iss.is_open()) {
        ALOG("Error opening cva sensitivity file " << fileName);
	return;
    }

    std::string line;
    while (std::getline(iss, line)) {
        vector<string> tokens;
        boost::trim(line);
        boost::split(tokens, line, boost::is_any_of(",;\t"));

        // We expect the format of XVA Sensitivity analytic reports with 11 columns:
        // #NettingSetId,TradeId,IsPar,Factor_1,ShiftSize_1,Factor_2,ShiftSize_2,Currency,Base XVA,Delta,Gamma
        QL_REQUIRE(tokens.size() == 11,
                   "11 tokens expected in CVA Sensitivities, found " << tokens.size() << " in: " << to_string(tokens));

        // Skip header, comments and lines with a non-empty tradeId column, since we want nettingSet-level sensi only
        if (tokens[0] == "NettingSetId" || line[0] == '#' || tokens[1] != "")
            continue;

        // We expect par sensitivities
        bool isPar = parseBool(tokens[2]);
        QL_REQUIRE(isPar == true, "CVA par sensitivity expected as input into SA-CVA");

        CvaSensitivityRecord r;
        r.nettingSetId = tokens[0];
        // RiskFactorKey needs translation into CvaRiskFactorKey
        r.key = mapRiskFactorKeyToCvaRiskFactorKey(tokens[3]);
        // shiftType is not in the sensitivity report.
	// FIXME: Since it is not used in the sacvasensitivityloader- can we remove it from the CvaSensitivityRecord?
	//r.shiftType = parseShiftType(tokens[2]);
        r.shiftSize = parseReal(tokens[4]);
        r.currency = tokens[7];
        r.baseCva = parseReal(tokens[8]);
        r.delta = parseReal(tokens[9]);

        cvaSensitivities_.push_back(r);
    }

    SaCvaSensitivityLoader cvaLoader;
    cvaLoader.loadFromRawSensis(cvaSensitivities_, baseCurrency_, counterpartyManager_);
    saCvaNetSensitivities_ = cvaLoader.netRecords();

    if (saCvaNetSensitivities().size() == 0) {
        ALOG("Loaded " << saCvaNetSensitivities().size() << " sacva net sensitivities");
    }
}

// XVA Explain

void InputParameters::setXvaExplainSimMarketParams(const std::string& xml) {
    xvaExplainSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    xvaExplainSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setXvaExplainSimMarketParamsFromFile(const std::string& fileName) {
    xvaExplainSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    xvaExplainSimMarketParams_->fromFile(fileName);
}

void InputParameters::setXvaExplainSensitivityScenarioData(const std::string& xml) {
    xvaExplainSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    xvaExplainSensitivityScenarioData_->fromXMLString(xml);
}

void InputParameters::setXvaExplainSensitivityScenarioDataFromFile(const std::string& fileName) {
    xvaExplainSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    xvaExplainSensitivityScenarioData_->fromFile(fileName);
}

void InputParameters::setAmcPricingEngineFromFile(const std::string& fileName) {
    amcPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    amcPricingEngine_->fromFile(fileName);
}

void InputParameters::setAmcCgPricingEngineFromFile(const std::string& fileName) {
    amcCgPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    amcCgPricingEngine_->fromFile(fileName);
}

void InputParameters::setNettingSetManager(const std::string& xml) {
    nettingSetManager_ = QuantLib::ext::make_shared<NettingSetManager>();
    nettingSetManager_->fromXMLString(xml);
}

void InputParameters::setNettingSetManagerFromFile(const std::string& fileName) {
    nettingSetManager_ = QuantLib::ext::make_shared<NettingSetManager>();
    nettingSetManager_->fromFile(fileName);
}

void InputParameters::setCollateralBalances(const std::string& xml) {
    collateralBalances_ = QuantLib::ext::make_shared<CollateralBalances>();
    collateralBalances_->fromXMLString(xml);
}

void InputParameters::setCollateralBalancesFromFile(const std::string& fileName) {
    collateralBalances_ = QuantLib::ext::make_shared<CollateralBalances>();
    collateralBalances_->fromFile(fileName);
}

void InputParameters::setCubeFromFile(const std::string& file) {
    auto r = ore::analytics::loadCube(file);
    cube_ = r.cube;
    if(r.scenarioGeneratorData)
        scenarioGeneratorData_ = r.scenarioGeneratorData;
    if(r.storeFlows)
        storeFlows_ = *r.storeFlows;
    if(r.storeCreditStateNPVs)
        storeCreditStateNPVs_ = *r.storeCreditStateNPVs;
}

void InputParameters::setCube(const ext::shared_ptr<NPVCube>& cube) {
    cube_ = cube;
}

void InputParameters::setNettingSetCubeFromFile(const std::string& file) {
    nettingSetCube_ = ore::analytics::loadCube(file).cube;
}

void InputParameters::setCptyCubeFromFile(const std::string& file) {
    cptyCube_ = ore::analytics::loadCube(file).cube;
}

void InputParameters::setMarketCubeFromFile(const std::string& file) { mktCube_ = loadAggregationScenarioData(file); }

void InputParameters::setMarketCube(const QuantLib::ext::shared_ptr<AggregationScenarioData>& cube) { mktCube_ = cube; }

void InputParameters::setVarQuantiles(const std::string& s) {
    // parse to vector<Real>
    varQuantiles_ = parseListOfValues<Real>(s, &parseReal);
}

void InputParameters::setCovarianceDataFromFile(const std::string& fileName) {
    ore::data::CSVFileReader reader(fileName, false);
    std::vector<std::string> dummy;
    while (reader.next()) {
        covarianceData_[std::make_pair(*parseRiskFactorKey(reader.get(0), dummy),
                                       *parseRiskFactorKey(reader.get(1), dummy))] =
            ore::data::parseReal(reader.get(2));
    }
    LOG("Read " << covarianceData_.size() << " valid covariance data lines from " << fileName);
}

void InputParameters::setCovarianceData(ore::data::CSVReader& reader) {
    std::vector<std::string> dummy;
    while (reader.next()) { 
        covarianceData_[std::make_pair(*parseRiskFactorKey(reader.get(0), dummy),
                                       *parseRiskFactorKey(reader.get(1), dummy))] =
            ore::data::parseReal(reader.get(2));
    }
    LOG("Read " << covarianceData_.size() << " valid covariance data lines");
}

void InputParameters::setCovarianceDataFromBuffer(const std::string& xml) {
    ore::data::CSVBufferReader reader(xml, false);
    setCovarianceData(reader);
}

void InputParameters::setSensitivityStreamFromFile(const std::string& fileName) {
    sensitivityStream_ = QuantLib::ext::make_shared<SensitivityFileStream>(
        fileName, csvSeparator_, csvCommentCharacter_, csvQuoteChar_, csvEscapeChar_);
}

void InputParameters::setSensitivityStreamFromBuffer(const std::string& buffer) {
    sensitivityStream_ = QuantLib::ext::make_shared<SensitivityBufferStream>(
        buffer, csvSeparator_, csvCommentCharacter_, csvQuoteChar_, csvEscapeChar_);
}

void InputParameters::setBenchmarkVarPeriod(const std::string& period) { 
    benchmarkVarPeriod_ = period;
}

void InputParameters::setScenarioReader(const std::string& fileName) {
    boost::filesystem::path baseScenarioPath(fileName);
    QL_REQUIRE(exists(baseScenarioPath), "The provided base scenario file, " << baseScenarioPath << ", does not exist");
    QL_REQUIRE(is_regular_file(baseScenarioPath),
               "The provided base scenario file, " << baseScenarioPath << ", is not a file");
    scenarioReader_ = QuantLib::ext::make_shared<ScenarioFileReader>(
        fileName, QuantLib::ext::make_shared<SimpleScenarioFactory>(false));
}

void InputParameters::setAmcTradeTypes(const std::string& s) {
    // parse to set<string>
    auto v = parseListOfValues(s);
    amcTradeTypes_ = std::set<std::string>(v.begin(), v.end());
}

void InputParameters::setAmcPathDataInput(const std::string& s) { amcPathDataInput_ = s; }

void InputParameters::setAmcPathDataOutput(const std::string& s) { amcPathDataOutput_ = s; }

void InputParameters::setCvaSensiGrid(const std::string& s) {
    // parse to vector<Period>
    cvaSensiGrid_ = parseListOfValues<Period>(s, &parsePeriod);
}
    
void InputParameters::setDeterministicInitialMarginFromFile(const std::string& fileName) {
    // Minimum requirement: The file has a header line and contains at least
    // columns "Date", "NettingSet" and "InitialMargin".
    // We don't assume that data is sorted by netting set or date.
    ore::data::CSVFileReader reader(fileName, true);
    std::map<std::string, std::map<Date, Real>> data;
    while (reader.next()) {
        Date date = parseDate(reader.get("Date"));
        std::string nettingSet = reader.get("NettingSet");
        Real initialMargin = parseReal(reader.get("InitialMargin"));        
        // LOG("IM Evolution NettingSet " << nettingSet << ": "
        //     << io::iso_date(date) << " " << initialMargin);
        if (data.find(nettingSet) == data.end())
            data[nettingSet] = std::map<Date,Real>();
        std::map<Date,Real>& evolution = data[nettingSet];
        evolution[date] = initialMargin;
    }
    for (auto d : data) {
        std::string n = d.first;
        LOG("Loading IM evolution for netting set " << n << ", size " << d.second.size());
        vector<Real> im;
        vector<Date> date;
        for (auto row : d.second) {
            im.push_back(row.second);
            date.push_back(row.first);
        }
        TimeSeries<Real> ts(date.begin(), date.end(), im.begin());
        // for (auto d : ts.dates())
        //     LOG("TimeSeries " << io::iso_date(d) << " " << ts[d]);
        setDeterministicInitialMargin(n, ts);
        WLOG("External IM evolution for NettingSet " << n << " loaded");
    }
}

void InputParameters::setDimRegressors(const std::string& s) {
    // parse to vector<string>
    dimRegressors_ = parseListOfValues(s);
}
    
void InputParameters::setDimOutputGridPoints(const std::string& s) {
    // parse to vector<Size>
    dimOutputGridPoints_ = parseListOfValues<Size>(s, &parseInteger);
}
    
void InputParameters::setCashflowHorizon(const std::string& s) {
    // parse to Date
    cashflowHorizon_ = parseDate(s);
}
    
void InputParameters::setPortfolioFilterDate(const std::string& s) {
    // parse to Date
    portfolioFilterDate_ = parseDate(s);
}

void InputParameters::setCreditSimulationParametersFromFile(const std::string& fileName) {
    creditSimulationParameters_ = QuantLib::ext::make_shared<CreditSimulationParameters>();
    creditSimulationParameters_->fromFile(fileName);
}

void InputParameters::setCreditSimulationParametersFromBuffer(const std::string& xml) {
    creditSimulationParameters_ = QuantLib::ext::make_shared<CreditSimulationParameters>();
    creditSimulationParameters_->fromXMLString(xml);
} 
    
void InputParameters::setCrifFromFile(const std::string& fileName, char eol, char delim, char quoteChar, char escapeChar) {
    bool updateMappings = true;
    bool aggregateTrades = false;
    bool allowUseCounterpartyTrade = true;
    auto crifLoader = CsvFileCrifLoader(fileName, getSimmConfiguration(), CrifRecord::additionalHeaders, updateMappings,
                                        aggregateTrades, allowUseCounterpartyTrade, eol, delim, quoteChar, escapeChar, reportNaString());
    crif_ = crifLoader.loadCrif();
}

void InputParameters::setCrifFromBuffer(const std::string& csvBuffer, char eol, char delim, char quoteChar, char escapeChar) {
    bool updateMappings = true;
    bool aggregateTrades = false;
    bool allowUseCounterpartyTrade = true;
    auto crifLoader =
        CsvBufferCrifLoader(csvBuffer, getSimmConfiguration(), CrifRecord::additionalHeaders, updateMappings,
                            aggregateTrades, allowUseCounterpartyTrade, eol, delim, quoteChar, escapeChar, reportNaString());
    crif_ = crifLoader.loadCrif();
}

void InputParameters::setSimmNameMapper(const std::string& xml) {
    simmNameMapper_ = QuantLib::ext::make_shared<SimmBasicNameMapper>();
    simmNameMapper_->fromXMLString(xml);    
}
    
void InputParameters::setSimmNameMapperFromFile(const std::string& fileName) {
    simmNameMapper_ = QuantLib::ext::make_shared<SimmBasicNameMapper>();
    simmNameMapper_->fromFile(fileName);    
}

void InputParameters::setSimmBucketMapper(const std::string& xml) {
    QL_REQUIRE(simmVersion_ != "", "SIMM version not set");
    QL_REQUIRE(simmBucketMapper_ != nullptr, "SIMMbucket mapper not set");
    QuantLib::ext::shared_ptr<SimmBucketMapperBase> sbm = QuantLib::ext::dynamic_pointer_cast<SimmBucketMapperBase>(simmBucketMapper_);
    sbm->fromXMLString(xml);
}
    
void InputParameters::setSimmBucketMapperFromFile(const std::string& fileName) {
    QL_REQUIRE(simmVersion_ != "", "SIMM version not set");
    QL_REQUIRE(simmBucketMapper_ != nullptr, "SIMMbucket mapper not set");
    QuantLib::ext::shared_ptr<SimmBucketMapperBase> sbm = QuantLib::ext::dynamic_pointer_cast<SimmBucketMapperBase>(simmBucketMapper_);
    sbm->fromFile(fileName);    
}

void InputParameters::setSimmCalibrationDataFromFile(const std::string& fileName) {
    simmCalibrationData_ = QuantLib::ext::make_shared<SimmCalibrationData>();
    simmCalibrationData_->fromFile(fileName);
}

void InputParameters::setAnalytics(const std::string& s) {
    // parse to set<string>
    auto v = parseListOfValues(s);
    analytics_ = std::set<std::string>(v.begin(), v.end());
}

void InputParameters::insertAnalytic(const std::string& s) {
    analytics_.insert(s);
}

void InputParameters::removeAnalytic(const std::string& s) { analytics_.erase(s); }

OutputParameters::OutputParameters(const QuantLib::ext::shared_ptr<Parameters>& params) {
    LOG("OutputFileNameMap called");
    npvOutputFileName_ = params->get("npv", "outputFileName", false);
    cashflowOutputFileName_ = params->get("cashflow", "outputFileName", false);
    curvesOutputFileName_ = params->get("curves", "outputFileName", false);
    scenarioDumpFileName_ = params->get("simulation", "scenariodump", false);
    scenarioOutputName_ = params->get("scenario", "scenarioOutputFile", false);
    if (scenarioOutputName_.empty())
        scenarioOutputName_ = params->get("scenarioStatistics", "scenarioOutputFile", false);
    cubeFileName_ = params->get("simulation", "cubeFile", false);
    mktCubeFileName_ = params->get("simulation", "aggregationScenarioDataFileName", false);
    rawCubeFileName_ = params->get("xva", "rawCubeOutputFile", false);
    netCubeFileName_ = params->get("xva", "netCubeOutputFile", false);
    timeAveragedNettedExposureFileName_ = params->get("xva", "timeAveragedNettedExposureOutputFile", false);
    dimEvolutionFileName_ = params->get("xva", "dimEvolutionFile", false);    
    std::string tmp = params->get("xva", "dimRegressionFiles", false);
    if (tmp != "")
        dimRegressionFileNames_ = parseListOfValues(tmp);
    sensitivityFileName_ = params->get("sensitivity", "sensitivityOutputFile", false);    
    parSensitivityFileName_ = params->get("sensitivity", "parSensitivityOutputFile", false);    
    jacobiFileName_ = params->get("sensitivity", "jacobiOutputFile", false);    
    jacobiInverseFileName_ = params->get("sensitivity", "jacobiInverseOutputFile", false);    
    sensitivityScenarioFileName_ = params->get("sensitivity", "scenarioOutputFile", false);    
    stressTestFileName_ = params->get("stress", "scenarioOutputFile", false);
    stressTestCashflowFileName_ = params->get("stress", "scenarioCashflowOutputFile", false);
    stressZeroScenarioDataFileName_ = params->get("stress", "stressZeroScenarioDataFile", false);
    xvaStressTestFileName_ = params->get("xvaStress", "scenarioOutputFile", false);
    sensitivityStressTestFileName_ = params->get("sensitivityStress", "scenarioOutputFile", false);
    varFileName_ = params->get("parametricVar", "outputFile", false);
    if (varFileName_.empty())
        varFileName_ = params->get("historicalSimulationVar", "outputFile", false);
    parConversionOutputFileName_ = params->get("zeroToParSensiConversion", "outputFile", false);
    parConversionJacobiFileName_ = params->get("zeroToParSensiConversion", "jacobiOutputFile", false);
    parConversionJacobiInverseFileName_ = params->get("zeroToParSensiConversion", "jacobiInverseOutputFile", false);
    pnlOutputFileName_ = params->get("pnl", "outputFileName", false);
    parStressTestConversionFile_ = params->get("parStressConversion", "stressZeroScenarioDataFile", false);
    pnlExplainOutputFileName_ = params->get("pnlExplain", "outputFileName", false);
    riskFactorsOutputFileName_ = params->get("portfolioDetails", "riskFactorFileName", false);
    marketObjectsOutputFileName_ = params->get("portfolioDetails", "marketObjectFileName", false);
    calibrationOutputFileName_ = params->get("calibration", "outputFile", false);

    zeroToParShiftFile_ = params->get("zeroToParShift", "parShiftsFile", false);
    xvaSensiJacobiFileName_ = params->get("xvaSensitivity", "jacobiOutputFile", false);    
    xvaSensiJacobiInverseFileName_ = params->get("xvaSensitivity", "jacobiInverseOutputFile", false);    
    // map internal report name to output file name
    fileNameMap_["npv"] = npvOutputFileName_;
    fileNameMap_["cashflow"] = cashflowOutputFileName_;
    fileNameMap_["curves"] = curvesOutputFileName_;
    fileNameMap_["cube"] = cubeFileName_;
    fileNameMap_["scenariodata"] = mktCubeFileName_;
    fileNameMap_["scenario"] = !scenarioOutputName_.empty() ? scenarioOutputName_ : scenarioDumpFileName_;
    fileNameMap_["rawcube"] = rawCubeFileName_;
    fileNameMap_["netcube"] = netCubeFileName_;
    fileNameMap_["timeAveragedNettedExposure"] = timeAveragedNettedExposureFileName_;
    fileNameMap_["dim_evolution"] = dimEvolutionFileName_;
    fileNameMap_["sensitivity"] = sensitivityFileName_;
    fileNameMap_["sensitivity_scenario"] = sensitivityScenarioFileName_;
    fileNameMap_["par_sensitivity"] = parSensitivityFileName_;
    fileNameMap_["jacobi"] = jacobiFileName_;
    fileNameMap_["jacobi_inverse"] = jacobiInverseFileName_;
    fileNameMap_["stress"] = stressTestFileName_;
    fileNameMap_["stress_cashflows"] = stressTestCashflowFileName_;
    fileNameMap_["stress_ZeroStressData"] = stressZeroScenarioDataFileName_;
    fileNameMap_["xva_stress"] = xvaStressTestFileName_;
    fileNameMap_["sensitivity_stress"] = sensitivityStressTestFileName_;
    fileNameMap_["var"] = varFileName_;
    fileNameMap_["parConversionSensitivity"] = parConversionOutputFileName_;
    fileNameMap_["parConversionJacobi"] = parConversionJacobiFileName_;
    fileNameMap_["parConversionJacobi_inverse"] = parConversionJacobiInverseFileName_;
    fileNameMap_["pnl"] = pnlOutputFileName_;
    fileNameMap_["parStress_ZeroStressData"] = parStressTestConversionFile_;
    fileNameMap_["pnl_explain"] = pnlExplainOutputFileName_;
    fileNameMap_["risk_factors"] = riskFactorsOutputFileName_;
    fileNameMap_["market_objects"] = marketObjectsOutputFileName_;
    fileNameMap_["calibration"] = calibrationOutputFileName_;
    fileNameMap_["xva_sensi_jacobi"] = jacobiFileName_;
    fileNameMap_["xva_sensi_jacobi_inverse"] = jacobiInverseFileName_;
    fileNameMap_["parshifts"] = zeroToParShiftFile_;
    vector<Size> dimOutputGridPoints;
    tmp = params->get("xva", "dimOutputGridPoints", false);
    if (tmp != "")
        dimOutputGridPoints = parseListOfValues<Size>(tmp, parseInteger);
    QL_REQUIRE(dimOutputGridPoints.size() == dimRegressionFileNames_.size(),
               "dim regression output grid points size (" << dimOutputGridPoints.size() << ") "
               << "and file names size (" << dimRegressionFileNames_.size() << ") do not match");
    for (Size i = 0; i < dimRegressionFileNames_.size(); ++i)
        fileNameMap_["dim_regression_" + std::to_string(i)] = dimRegressionFileNames_[i];

    tmp = params->get("xva", "creditMigrationTimeSteps", false);
    if (tmp != "") {
        auto ts = parseListOfValues<Size>(tmp, &parseInteger);
        for (auto const& t : ts) {
            fileNameMap_["credit_migration_" + std::to_string(t)] =
                params->get("xva", "creditMigrationOutputFiles") + "_" + std::to_string(t);
        }
    }

    LOG("OutputFileNameMap complete");
}
    
std::string OutputParameters::outputFileName(const std::string& internalName, const std::string& suffix) {
    auto it = fileNameMap_.find(internalName);
    if (it == fileNameMap_.end() || it->second == "")
        return internalName + "." + suffix;
    else        
        return it->second; // contains suffix
}


void InputParameters::setParConversionSimMarketParams(const std::string& xml) {
    parConversionSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    parConversionSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setParConversionSimMarketParamsFromFile(const std::string& fileName) {
    parConversionSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    parConversionSimMarketParams_->fromFile(fileName);
}

void InputParameters::setParConversionScenarioData(const std::string& xml) {
    parConversionScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    parConversionScenarioData_->fromXMLString(xml);
}

void InputParameters::setParConversionScenarioDataFromFile(const std::string& fileName) {
    parConversionScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    parConversionScenarioData_->fromFile(fileName);
}
void InputParameters::setParConversionPricingEngine(const std::string& xml) {
    parConversionPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    parConversionPricingEngine_->fromXMLString(xml);
}

void InputParameters::setParConversionPricingEngineFromFile(const std::string& fileName) {
    parConversionPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    parConversionPricingEngine_->fromFile(fileName);
}

void InputParameters::setParStressSimMarketParams(const std::string& xml) {
    parStressSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    parStressSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setParStressSimMarketParamsFromFile(const std::string& fileName) {
    parStressSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    parStressSimMarketParams_->fromFile(fileName);
}

void InputParameters::setParStressScenarioData(const std::string& xml) {
    parStressScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    parStressScenarioData_->fromXMLString(xml);
}

void InputParameters::setParStressScenarioDataFromFile(const std::string& fileName) {
    parStressScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    parStressScenarioData_->fromFile(fileName);
}

void InputParameters::setParStressSensitivityScenarioData(const std::string& xml) {
    parStressSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    parStressSensitivityScenarioData_->fromXMLString(xml);
}

void InputParameters::setParStressSensitivityScenarioDataFromFile(const std::string& fileName) {
    parStressSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    parStressSensitivityScenarioData_->fromFile(fileName);
}

void InputParameters::setParStressPricingEngine(const std::string& xml) {
    parStressPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    parStressPricingEngine_->fromXMLString(xml);
}

void InputParameters::setParStressPricingEngineFromFile(const std::string& fileName) {
    parStressPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    parStressPricingEngine_->fromFile(fileName);
}

void InputParameters::setZeroToParShiftSimMarketParams(const std::string& xml) {
    zeroToParShiftSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    zeroToParShiftSimMarketParams_->fromXMLString(xml);
}

void InputParameters::setZeroToParShiftSimMarketParamsFromFile(const std::string& fileName) {
    zeroToParShiftSimMarketParams_ = QuantLib::ext::make_shared<ScenarioSimMarketParameters>();
    zeroToParShiftSimMarketParams_->fromFile(fileName);
}

void InputParameters::setZeroToParShiftScenarioData(const std::string& xml) {
    zeroToParShiftScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    zeroToParShiftScenarioData_->fromXMLString(xml);
}

void InputParameters::setZeroToParShiftScenarioDataFromFile(const std::string& fileName) {
    zeroToParShiftScenarioData_ = QuantLib::ext::make_shared<StressTestScenarioData>();
    zeroToParShiftScenarioData_->fromFile(fileName);
}

void InputParameters::setZeroToParShiftSensitivityScenarioData(const std::string& xml) {
    zeroToParShiftSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    zeroToParShiftSensitivityScenarioData_->fromXMLString(xml);
}

void InputParameters::setZeroToParShiftSensitivityScenarioDataFromFile(const std::string& fileName) {
    zeroToParShiftSensitivityScenarioData_ = QuantLib::ext::make_shared<SensitivityScenarioData>();
    zeroToParShiftSensitivityScenarioData_->fromFile(fileName);
}

void InputParameters::setZeroToParShiftPricingEngine(const std::string& xml) {
    zeroToParShiftPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    zeroToParShiftPricingEngine_->fromXMLString(xml);
}

void InputParameters::setZeroToParShiftPricingEngineFromFile(const std::string& fileName) {
    zeroToParShiftPricingEngine_ = QuantLib::ext::make_shared<EngineData>();
    zeroToParShiftPricingEngine_->fromFile(fileName);
}

Date InputParameters::mporDate() {
    if (mporDate_ == Date()) {
        QL_REQUIRE(asof() != Date(), "Asof date is required for mpor date");
        QL_REQUIRE(!mporCalendar().empty(), "MporCalendar or BaseCurrency is required for mpor date");
        QL_REQUIRE(mporDays() != Null<Size>(), "mporDays is required for mpor date");

        int effectiveMporDays = mporForward()
            ? static_cast<int>(mporDays())
                               : -static_cast<int>(mporDays());

        mporDate_ = mporCalendar().advance(asof(), effectiveMporDays, QuantExt::Days);
    }
    return mporDate_;
}

QuantLib::ext::shared_ptr<SimmConfiguration> InputParameters::getSimmConfiguration() {
    QL_REQUIRE(simmBucketMapper() != nullptr,
               "Internal error, load simm bucket mapper before retrieving simmconfiguration");
    return buildSimmConfiguration(simmVersion(), simmBucketMapper(), simmCalibrationData(), mporDays());
}

} // namespace analytics
} // namespace ore
