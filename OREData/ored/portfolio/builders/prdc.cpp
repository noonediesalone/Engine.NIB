/*
Copyright (C) 2016 Quaternion Risk Management Ltd
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

#include <ored/portfolio/builders/prdc.hpp>
#include <qle/cashflows/prdccoupon.hpp>

#include <ored/model/crossassetmodelbuilder.hpp>
#include <ored/model/irlgmdata.hpp>
#include <ored/utilities/dategrid.hpp>
#include <qle/methods/multipathgeneratorbase.hpp>

using namespace QuantLib;

namespace ore {
namespace data {

boost::shared_ptr<FloatingRateCouponPricer> CamPrdcCouponPricerBuilder::engineImpl(const string& key) {
    auto fxIndex = market_->fxIndex(key);
    auto domesticCcy = fxIndex->sourceCurrency().code();
    auto foreignCcy = fxIndex->targetCurrency().code();

    vector<string> qualifiers = { key, fxIndex->name() };

    // From config file
    BigNatural seed = parseInteger(engineParameter("Seed", qualifiers, true));
    Size paths = parseInteger(engineParameter("Paths", qualifiers, true));
    bool antithetic = parseBool(engineParameter("UseAntithetic", qualifiers, false, "false"));
    CalibrationType calibrationType = parseCalibrationType(modelParameter("Calibration", qualifiers, false, "Bootstrap"));
    LgmData::ReversionType revType = parseReversionType(modelParameter("ReversionType", qualifiers, false, "HullWhite"));
    LgmData::VolatilityType volType = parseVolatilityType(modelParameter("VolatilityType", qualifiers, false, "Hagan"));

    // From Market
    vector<Time> hTimes = {};
    vector<Time> aTimes = {};
    vector<string> swaptionExpiries = {"1Y", "2Y", "3Y", "5Y", "7Y", "10Y", "15Y", "20Y", "30Y"};
    vector<string> swaptionTerms = {"5Y", "5Y", "5Y", "5Y", "5Y", "5Y", "5Y", "5Y", "5Y"};
    vector<string> swaptionStrikes(swaptionExpiries.size(), "ATM");
    vector<string> optionExpiries = {"1Y", "2Y", "3Y", "5Y", "7Y", "10Y"};
    vector<string> optionStrikes(optionExpiries.size(), "ATMF");
    vector<Time> sigmaTimes = {};

    // IR factors
    std::vector<boost::shared_ptr<IrModelData>> irConfigs;

    vector<Real> hValues = {0.02};
    vector<Real> aValues = {0.008};
    irConfigs.push_back(boost::make_shared<IrLgmData>(
        "EUR", calibrationType, revType, volType, false, ParamType::Constant, hTimes, hValues, true,
        ParamType::Piecewise, aTimes, aValues, 0.0, 1.0, swaptionExpiries, swaptionTerms, swaptionStrikes));

    irConfigs.push_back(boost::make_shared<IrLgmData>(
        "JPY", calibrationType, revType, volType, false, ParamType::Constant, hTimes, hValues, true,
        ParamType::Piecewise, aTimes, aValues, 0.0, 1.0, swaptionExpiries, swaptionTerms, swaptionStrikes));
    
    // FX factors
    std::vector<boost::shared_ptr<FxBsData>> fxConfigs;
    vector<Real> sigmaValues = {0.15};
    fxConfigs.push_back(boost::make_shared<FxBsData>("JPY", "EUR", calibrationType, true, ParamType::Piecewise,
        sigmaTimes, sigmaValues, optionExpiries, optionStrikes));

    // Correlations
    map<CorrelationKey, Handle<Quote>> corr;
    CorrelationFactor f_1{QuantExt::CrossAssetModel::AssetType::IR, "EUR", 0};
    CorrelationFactor f_2{QuantExt::CrossAssetModel::AssetType::IR, "JPY", 0};
    corr[std::make_pair(f_1, f_2)] = Handle<Quote>(boost::make_shared<SimpleQuote>(0.6));

    boost::shared_ptr<CrossAssetModelData> config(boost::make_shared<CrossAssetModelData>(irConfigs, fxConfigs, corr));
    boost::shared_ptr<CrossAssetModelBuilder> modelBuilder(new CrossAssetModelBuilder(market_, config));
    boost::shared_ptr<QuantExt::CrossAssetModel> model = *modelBuilder->model();
    boost::shared_ptr<StochasticProcess> process = model->stateProcess();
    
    string dateGridStr = "80,3M"; // 20 years
    boost::shared_ptr<DateGrid> dg = boost::make_shared<DateGrid>(dateGridStr);

    // Path generator
    boost::shared_ptr<QuantExt::MultiPathGeneratorBase> pathGen =
        boost::make_shared<QuantExt::MultiPathGeneratorMersenneTwister>(process, dg->timeGrid(), seed, antithetic);

    auto times = dg->times().size();
    vector<Rate> ir1(times, 0);
    vector<Rate> ir2(times, 0);
    vector<Rate> fx_rates(times, 0);

    for (Size j = 0; j < paths; ++j) {
        Sample<MultiPath> path = pathGen->next();

        Size l = path.value[0].length();
        for (auto i = 0; i < l; i++)
        {
            ir1[i] += path.value[0][i];
            ir2[i] += path.value[1][i];
            fx_rates[i] += path.value[2][i];
        }        
    }

    for (auto i = 0; i < times; ++i)
    {
        auto d = dg->dates()[i];
        auto r1 = ir1[i] / times, r2 = ir2[i] / times;
        auto fx = fx_rates[i];
        std::cout << d << " : " << std::to_string(r1 / times) << " ; " << 
            std::to_string(r2 / times) << " ; " << 
            std::to_string(fx / times) << " ; " << 
            fxIndex->fixing(d) << std::endl;
    }

    boost::shared_ptr<FloatingRateCouponPricer> pricer = boost::make_shared<QuantExt::CAMPricer>(model, pathGen);

    // Return the cached pricer
    return pricer;
}

} // namespace data
} // namespace ore