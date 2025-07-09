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

/*! \file orea/scenario/historicalscenariogenerator.hpp
    \brief scenario generator that builds from historical shifts
    \ingroup scenario
 */

#pragma once

#include <boost/make_shared.hpp>
#include <orea/scenario/historicalscenarioreturn.hpp>
#include <orea/scenario/scenariofactory.hpp>
#include <orea/scenario/scenariogenerator.hpp>
#include <orea/scenario/scenarioloader.hpp>
#include <orea/scenario/scenarioreader.hpp>
#include <orea/scenario/scenariosimmarket.hpp>
#include <ored/marketdata/adjustmentfactors.hpp>
#include <ored/utilities/timeperiod.hpp>

#include <ql/math/randomnumbers/rngtraits.hpp>

namespace ore {
namespace analytics {

//! Historical Scenario Generator
/*! A Scenario Generator that takes historical scenarios and builds new scenarios by applying historical shifts to the
 * base scenario (which typically comes from todays market).
 *
 *  This generator can be used for historical VAR and backtesting calculations, unlike a PRNG based generator it is
 * limited in the number of scenarios it can generate.
 *
 *  The scenarios generated are based on the scenario differences between t and t+mpor, these differences are typically
 * a relative change and this change is then applied to the baseScenario to give a new scenario which is asof Today or
 * Today+mpor.
 */
class HistoricalScenarioGenerator : public ScenarioGenerator {
public:
    struct HistoricalScenarioCalculationDetails {
        Date scenarioDate1;
        Date scenarioDate2;
        RiskFactorKey key;
        double baseValue;
        double adjustmentFactor1;
        double adjustmentFactor2;
        double scenarioValue1;
        double scenarioValue2;
        ReturnConfiguration::ReturnType returnType;
        double displacement;
        double scaling;
        double returnValue;
        double scenarioValue;
    };

    //! Default constructor
    HistoricalScenarioGenerator(
        //! Historical Scenario Loader containing all scenarios
        const QuantLib::ext::shared_ptr<HistoricalScenarioLoader>& historicalScenarioLoader,
        //! Scenario factory to use
        const QuantLib::ext::shared_ptr<ScenarioFactory>& scenarioFactory,
        //! return configuration
        const QuantLib::ext::shared_ptr<ReturnConfiguration>& returnConfiguration,
        //! Calendar to use
        const QuantLib::Calendar& cal,
        //! optional adjustment factors for stock splits etc
        const QuantLib::ext::shared_ptr<ore::data::AdjustmentFactors>& adjFactors = nullptr,
        //! Mpor days or step size
        const Size mporDays = 10,
        //! overlapping scenarios
        const bool overlapping = true,
        //! string prepended to label of all scenarios generated
        const std::string& labelPrefix = "",
        //! indicates if the generated sceanrios will be absolute or difference
        const bool generateDifferenceScenarios = false);

    //! Constructor with no mporDays/Calendar, construct historical shift scenario between each scneario
    HistoricalScenarioGenerator(
        //! Historical Scenario Loader containing all scenarios
        const QuantLib::ext::shared_ptr<HistoricalScenarioLoader>& historicalScenarioLoader,
        //! Scenario factory to use
        const QuantLib::ext::shared_ptr<ScenarioFactory>& scenarioFactory,
        //! return configuration
        const QuantLib::ext::shared_ptr<ReturnConfiguration>& returnConfiguration,
        //! optional adjustment factors for stock splits etc
        const QuantLib::ext::shared_ptr<ore::data::AdjustmentFactors>& adjFactors = nullptr,
        //! string prepended to label of all scenarios generated
        const std::string& labelPrefix = "",
        //! indicates if the generated sceanrios will be absolute or difference
        const bool generateDifferenceScenarios = false);

    //! Set base scenario, this also defines the asof date
    QuantLib::ext::shared_ptr<Scenario>& baseScenario() { return baseScenario_; }
    //! Get base scenario
    virtual const QuantLib::ext::shared_ptr<Scenario>& baseScenario() const { return baseScenario_; }

    //! Get calendar
    const QuantLib::Calendar& cal() const { return cal_; }
    //! Get mpor days
    QuantLib::Size mporDays() const { return mporDays_; }
    //! Are scenarios overlapping
    bool overlapping() const { return overlapping_; }

    //! Return configuration
    const QuantLib::ext::shared_ptr<ReturnConfiguration>& returnConfiguration() const { return returnConfiguration_; }

    //! Scaling
    virtual QuantLib::Real scaling(const RiskFactorKey& key, const QuantLib::Real& keyReturn) { return 1; };

    //! Return the next scenario for the given date.
    /*! Date should be asof or asof+mporDays, this class only checks that date is >= the asof date.
        Whatever you put in here will be in the returned scenario.

        Generator returns scenarios in order of shifts and throws if we have run out of historicals

        If Mpor > 1 than the scenarios will overlap.
    */
    QuantLib::ext::shared_ptr<Scenario> next(const QuantLib::Date& d) override;

    //! Return the calculation details of the last generated scenario */
    const std::vector<HistoricalScenarioCalculationDetails>& lastHistoricalScenarioCalculationDetails() const;

    //! Reset the generator so calls to next() return the first scenario.
    /*! This allows re-generation of scenarios if required. */
    void reset() override { i_ = 0; }

    //! Number of scenarios this generator can generate
    virtual QuantLib::Size numScenarios() const;

    //! set the start and end dates, can be overwritten in derived class
    virtual void setDates();

    //! start dates from which the scenarios were generated from
    const std::vector<QuantLib::Date>& startDates() const { return startDates_; }
    //! end dates from which the scenarios were generated from
    const std::vector<QuantLib::Date>& endDates() const { return endDates_; }

    //! Get the HistoricalScenarioLoader
    const QuantLib::ext::shared_ptr<HistoricalScenarioLoader>& scenarioLoader() const {
        return historicalScenarioLoader_;
    }

    //! Get the ScenarioFactory
    const QuantLib::ext::shared_ptr<ScenarioFactory>& scenarioFactory() const { return scenarioFactory_; }

    //! Get the adj factors
    const QuantLib::ext::shared_ptr<ore::data::AdjustmentFactors>& adjFactors() const { return adjFactors_; }

    //! Get (start, end) scenario date pairs filtered on the given period
    std::vector<std::pair<QuantLib::Date, QuantLib::Date>>
    filteredScenarioDates(const ore::data::TimePeriod& period) const;

    //! Get the scenario label prefix
    const std::string& labelPrefix() const { return labelPrefix_; }

    void setGenerateDifferenceScenarios(const bool b) { generateDifferenceScenarios_ = b; }
    const bool generateDifferenceScenarios() const { return generateDifferenceScenarios_; }

protected:
    // to be managed in derived classes, if next is overwritten
    Size i_;

    QuantLib::ext::shared_ptr<HistoricalScenarioLoader> historicalScenarioLoader_;
    std::vector<QuantLib::Date> startDates_, endDates_;

    QuantLib::ext::shared_ptr<ScenarioFactory> scenarioFactory_;
    QuantLib::ext::shared_ptr<Scenario> baseScenario_;

    //! The Scenario Pairs for a given index
    std::pair<QuantLib::ext::shared_ptr<Scenario>, QuantLib::ext::shared_ptr<Scenario>> scenarioPair();

    //! Returns the adjusted price
    /*! Scenarios may contian unadjusted market prices e.g equity spot prices,
        apply adjustment factors to ensure no jumps between 2 scenarios
        Only handles equity spot adjustments at the moment */
    QuantLib::Real adjustedPrice(RiskFactorKey key, QuantLib::Date d, QuantLib::Real price);

    // details on the last generated scenario
    std::vector<HistoricalScenarioCalculationDetails> calculationDetails_;

    QuantLib::Calendar cal_;
    QuantLib::Size mporDays_ = 10;

private:
    QuantLib::ext::shared_ptr<ore::data::AdjustmentFactors> adjFactors_;
    bool overlapping_ = true;
    QuantLib::ext::shared_ptr<ReturnConfiguration> returnConfiguration_;
    std::string labelPrefix_;
    bool generateDifferenceScenarios_ = false;
};

//! Historical scenario generator generating random scenarios, for testing purposes
class HistoricalScenarioGeneratorRandom : public HistoricalScenarioGenerator {
public:
    HistoricalScenarioGeneratorRandom(
        //! Historical Scenario Loader containing all scenarios
        const QuantLib::ext::shared_ptr<HistoricalScenarioLoader>& historicalScenarioLoader,
        /*! Scenario factory to use */
        const QuantLib::ext::shared_ptr<ScenarioFactory>& scenarioFactory,
        //! return configuration
        const QuantLib::ext::shared_ptr<ReturnConfiguration>& returnConfiguration,
        //! Calendar to use
        const QuantLib::Calendar& cal,
        //! optional adjustment factors for stock splits etc
        const QuantLib::ext::shared_ptr<ore::data::AdjustmentFactors>& adjFactors = nullptr,
        //! MPOR days
        const QuantLib::Size mporDays = 10,
        //! overlapping scenarios
        const bool overlapping = true)
        : HistoricalScenarioGenerator(historicalScenarioLoader, scenarioFactory, returnConfiguration, cal, adjFactors,
                                      mporDays, overlapping) {
        normalrng_ = QuantLib::ext::make_shared<QuantLib::PseudoRandom::rng_type>(MersenneTwisterUniformRng(42));
    };

    QuantLib::ext::shared_ptr<Scenario> next(const QuantLib::Date& d) override;
    void reset() override;

private:
    QuantLib::ext::shared_ptr<QuantLib::PseudoRandom::rng_type> normalrng_;
};

//! Historical scenario generator transform
/*! This class allows scenarios within a historical scenario generator to be transformed from discount rates to
 *  zero rates and survival probabilities to hazard rates
 *  WARNING: This transform function should only be used for backtesting statistics reports requiring the transforms
 *  listed above.
 */
class HistoricalScenarioGeneratorTransform : public HistoricalScenarioGenerator {
public:
    HistoricalScenarioGeneratorTransform(QuantLib::ext::shared_ptr<HistoricalScenarioGenerator>& hsg,
                                         const QuantLib::ext::shared_ptr<ScenarioSimMarket>& simMarket,
                                         const QuantLib::ext::shared_ptr<ScenarioSimMarketParameters>& simMarketConfig)
        : HistoricalScenarioGenerator(hsg->scenarioLoader(), hsg->scenarioFactory(), hsg->returnConfiguration(),
                                      hsg->cal(), hsg->adjFactors(), hsg->mporDays(), hsg->overlapping(),
                                      hsg->labelPrefix()),
          simMarket_(simMarket), simMarketConfig_(simMarketConfig) {
        baseScenario_ = hsg->baseScenario();
    }

    QuantLib::ext::shared_ptr<Scenario> next(const QuantLib::Date& d) override;

private:
    QuantLib::ext::shared_ptr<ScenarioSimMarket> simMarket_;
    QuantLib::ext::shared_ptr<ScenarioSimMarketParameters> simMarketConfig_;
};

// Historical scenario generator with filtered scenario dates
/*! This class will only generate the subset of scenarios with start / end dates contained in one of the given time
   periods. Warning: the base scenario in the passed historical scenario generator must be set on construction of this
   class, later changes in the base scenario will not be reflected in an instance of this class. */
class HistoricalScenarioGeneratorWithFilteredDates : public HistoricalScenarioGenerator {
public:
    HistoricalScenarioGeneratorWithFilteredDates(const std::vector<ore::data::TimePeriod>& filter,
                                                 const QuantLib::ext::shared_ptr<HistoricalScenarioGenerator>& gen);
    void reset() override;
    QuantLib::ext::shared_ptr<Scenario> next(const QuantLib::Date& d) override;

private:
    QuantLib::ext::shared_ptr<HistoricalScenarioGenerator> gen_;
    std::vector<bool> isRelevantScenario_;
    QuantLib::Size i_orig_;
};

QuantLib::ext::shared_ptr<HistoricalScenarioGenerator> buildHistoricalScenarioGenerator(
    const QuantLib::ext::shared_ptr<ScenarioReader>& hsr,
    const QuantLib::ext::shared_ptr<ore::data::AdjustmentFactors>& adjFactors, const TimePeriod& period,
    Calendar calendar, Size mporDays, const QuantLib::ext::shared_ptr<ScenarioSimMarketParameters>& simParams,
    const QuantLib::ext::shared_ptr<TodaysMarketParameters>& marketParam,
    const QuantLib::ext::shared_ptr<ReturnConfiguration>& returnConfiguration, const bool overlapping = true);

QuantLib::ext::shared_ptr<HistoricalScenarioGenerator>
buildHistoricalScenarioGenerator(const QuantLib::ext::shared_ptr<ScenarioReader>& hsr,
                                 const QuantLib::ext::shared_ptr<ore::data::AdjustmentFactors>& adjFactors,
                                 const std::set<QuantLib::Date>& dates,
                                 const QuantLib::ext::shared_ptr<ScenarioSimMarketParameters>& simParams,
                                 const QuantLib::ext::shared_ptr<TodaysMarketParameters>& marketParam,
                                 const QuantLib::ext::shared_ptr<ReturnConfiguration>& returnConfiguration);

} // namespace analytics
} // namespace ore
