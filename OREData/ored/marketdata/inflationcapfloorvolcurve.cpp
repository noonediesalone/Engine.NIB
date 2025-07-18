/*
 Copyright (C) 2018 Quaternion Risk Management Ltd
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

#include <ored/marketdata/inflationcapfloorvolcurve.hpp>
#include <ored/utilities/indexparser.hpp>
#include <ored/utilities/inflationstartdate.hpp>
#include <ored/utilities/log.hpp>

#include <ql/experimental/inflation/interpolatedyoyoptionletstripper.hpp>
#include <ql/math/comparison.hpp>
#include <ql/math/interpolations/bilinearinterpolation.hpp>
#include <qle/math/flatextrapolation.hpp>
#include <ql/math/matrix.hpp>
#include <ql/termstructures/volatility/capfloor/capfloortermvolcurve.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>

#include <qle/indexes/inflationindexwrapper.hpp>
#include <qle/termstructures/interpolatedcpivolatilitysurface.hpp>
#include <qle/termstructures/interpolatedyoycapfloortermpricesurface.hpp>
#include <qle/termstructures/kinterpolatedyoyoptionletvolatilitysurface.hpp>
#include <qle/termstructures/yoyoptionletsurfacestripper.hpp>
#include <qle/termstructures/yoypricesurfacefromvols.hpp>

#include <qle/pricingengines/cpibacheliercapfloorengine.hpp>
#include <qle/termstructures/inflation/cpipricevolatilitysurface.hpp>


#include <qle/models/carrmadanarbitragecheck.hpp>

using namespace QuantLib;
using namespace QuantExt;
using namespace std;

namespace ore {
namespace data {

InflationCapFloorVolCurve::InflationCapFloorVolCurve(Date asof, InflationCapFloorVolatilityCurveSpec spec,
                                                     const Loader& loader, const CurveConfigurations& curveConfigs,
                                                     map<string, QuantLib::ext::shared_ptr<YieldCurve>>& yieldCurves,
                                                     map<string, QuantLib::ext::shared_ptr<InflationCurve>>& inflationCurves) {
    try {
        const QuantLib::ext::shared_ptr<InflationCapFloorVolatilityCurveConfig>& config =
            curveConfigs.inflationCapFloorVolCurveConfig(spec.curveConfigID());

        auto it = yieldCurves.find(config->yieldTermStructure());
        if (it != yieldCurves.end()) {
            discountCurve_ = it->second->handle(config->yieldTermStructure());
        } else {
            QL_FAIL("The yield term structure, " << config->yieldTermStructure()
                                                 << ", required in the building "
                                                    "of the curve, "
                                                 << spec.name() << ", was not found.");
        }

        switch (config->quoteType()) {
        case InflationCapFloorVolatilityCurveConfig::QuoteType::Price:
            buildFromPrices(asof, spec, loader, config, yieldCurves, inflationCurves);
            break;
        case InflationCapFloorVolatilityCurveConfig::QuoteType::Volatility:
            buildFromVolatilities(asof, spec, loader, config, yieldCurves, inflationCurves);
            break;
        default:
            QL_FAIL("unexpected quote type");
        }

        if (config->type() == InflationCapFloorVolatilityCurveConfig::Type::ZC)
            setCalibrationInfo(asof, spec, config, curveConfigs, inflationCurves);
    } catch (std::exception& e) {
        QL_FAIL("inflation cap/floor vol curve building failed :" << e.what());
    } catch (...) {
        QL_FAIL("inflation cap/floor vol curve building failed: unknown error");
    }
}

void InflationCapFloorVolCurve::buildFromVolatilities(
    Date asof, InflationCapFloorVolatilityCurveSpec spec, const Loader& loader,
    const QuantLib::ext::shared_ptr<InflationCapFloorVolatilityCurveConfig>& config,
    map<string, QuantLib::ext::shared_ptr<YieldCurve>>& yieldCurves,
    map<string, QuantLib::ext::shared_ptr<InflationCurve>>& inflationCurves) {

    DLOG("Build InflationCapFloorVolCurve " << spec.name() << " from vols");
    
    // Volatility type
    MarketDatum::QuoteType volatilityType;
    VolatilityType quoteVolatilityType;

    switch (config->volatilityType()) {
    case InflationCapFloorVolatilityCurveConfig::VolatilityType::Lognormal:
        volatilityType = MarketDatum::QuoteType::RATE_LNVOL;
        quoteVolatilityType = ShiftedLognormal;
        break;
    case InflationCapFloorVolatilityCurveConfig::VolatilityType::Normal:
        volatilityType = MarketDatum::QuoteType::RATE_NVOL;
        quoteVolatilityType = Normal;
        break;
    case InflationCapFloorVolatilityCurveConfig::VolatilityType::ShiftedLognormal:
        volatilityType = MarketDatum::QuoteType::RATE_SLNVOL;
        quoteVolatilityType = ShiftedLognormal;
        break;
    default:
        QL_FAIL("unexpected volatility type");
    }

    // Read in quotes matrix
    DLOG("Read quotes matrix");
    vector<Period> tenors = parseVectorOfValues<Period>(config->tenors(), &parsePeriod);
    vector<double> strikes = parseVectorOfValues<Real>(config->strikes(), &parseReal);
    QL_REQUIRE(!strikes.empty(), "Strikes should not be empty - expect a cap matrix");
    Matrix vols(tenors.size(), strikes.size());
    vector<vector<bool>> found(tenors.size(), vector<bool>(strikes.size()));
    Size remainingQuotes = tenors.size() * strikes.size();
    Size quotesRead = 0;

    // Quotes index can differ from the index for which we are building the surface.
    string quoteIndex = config->quoteIndex().empty() ? config->index() : config->quoteIndex();

    // We take the first capfloor shift quote that we find in the file matching the
    // currency and index tenor

    std::ostringstream ss1;
    ss1 << MarketDatum::InstrumentType::ZC_INFLATIONCAPFLOOR << "/*";
    Wildcard w1(ss1.str());
    auto data1 = loader.get(w1, asof);

    std::ostringstream ss2;
    ss2 << MarketDatum::InstrumentType::YY_INFLATIONCAPFLOOR << "/*";
    Wildcard w2(ss2.str());
    auto data2 = loader.get(w2, asof);

    data1.merge(data2);

    for (const auto& md : data1) {
        QL_REQUIRE(md->asofDate() == asof, "MarketDatum asofDate '" << md->asofDate() << "' <> asof '" << asof << "'");

        QuantLib::ext::shared_ptr<InflationCapFloorQuote> q;

        if (config->type() == InflationCapFloorVolatilityCurveConfig::Type::ZC) {
            q = QuantLib::ext::dynamic_pointer_cast<ZcInflationCapFloorQuote>(md);
        } else {
            q = QuantLib::ext::dynamic_pointer_cast<YyInflationCapFloorQuote>(md);
        }

        if (q && q->index() == quoteIndex && q->quoteType() == volatilityType) {

            quotesRead++;

            Real strike = parseReal(q->strike());
            Size i = std::find(tenors.begin(), tenors.end(), q->term()) - tenors.begin();
            Size j = std::find_if(strikes.begin(), strikes.end(),
                                  [&strike](const double& s) { return close_enough(s, strike); }) -
                     strikes.begin();

            if (i < tenors.size() && j < strikes.size()) {
                vols[i][j] = q->quote()->value();

                if (!found[i][j]) {
                    found[i][j] = true;
                    --remainingQuotes;
                }
            }
        }
    }

    LOG("InflationCapFloorVolCurve: read " << quotesRead << " vols");

    // Check that we have all the data we need
    size_t filledValues = 0;
    if (remainingQuotes != 0) {
        // Fill missing quotes
        for (size_t i = 0; i < tenors.size(); ++i) {
            std::vector<double> xs;
            std::vector<double> ys;
            std::vector<size_t> missingIds;
            for (size_t j = 0; j < strikes.size(); ++j) {
                if (found[i][j]) {
                    xs.push_back(strikes[j]);
                    ys.push_back(vols[i][j]);
                } else {
                    missingIds.push_back(j);
                }
            }
            if (!missingIds.empty() && xs.size() >= 1) {
                if (xs.size() == 1) {
                    xs.push_back(xs.front() + 0.01);
                    ys.push_back(ys.front());
                }
                auto interpolation = LinearFlat().interpolate(xs.begin(), xs.end(), ys.begin());
                for (auto j : missingIds) {
                    auto value = interpolation(strikes[j], true);

                    WLOG("vol for cap floor price surface, strike " << strikes[j] << ", term " << tenors[i]
                                                                    << ", not found. Replace NULL with " << value);
                    vols[i][j] = value;
                    filledValues++;
                }
            }
        }
    }

    if (remainingQuotes - filledValues != 0){
        
        ostringstream m;
        m << "Not all quotes found for spec " << spec << endl;
        if (remainingQuotes != 0) {
            m << "Found vol data (*) and missing data (.):" << std::endl;
            for (Size i = 0; i < tenors.size(); ++i) {
                for (Size j = 0; j < strikes.size(); ++j) {
                    m << (found[i][j] ? "*" : ".");
                }
                if (i < tenors.size() - 1)
                    m << endl;
            }
        }
        DLOGGERSTREAM(m.str());
        QL_FAIL("could not build inflation cap/floor vol curve");
    }

    if (config->type() == InflationCapFloorVolatilityCurveConfig::Type::YY) {

        // Non-ATM cap/floor volatility surface
        QuantLib::ext::shared_ptr<QuantLib::CapFloorTermVolSurface> capVol =
            QuantLib::ext::make_shared<QuantLib::CapFloorTermVolSurface>(0, config->calendar(), config->businessDayConvention(),
                                                                 tenors, strikes, vols, config->dayCounter());

        QuantLib::ext::shared_ptr<YoYInflationIndex> index;
        auto it2 = inflationCurves.find(config->indexCurve());
        if (it2 != inflationCurves.end()) {
            QuantLib::ext::shared_ptr<InflationTermStructure> ts = it2->second->inflationTermStructure();
            // Check if the Index curve is a YoY curve - if not it must be a zero curve
            QuantLib::ext::shared_ptr<YoYInflationTermStructure> yyTs =
                QuantLib::ext::dynamic_pointer_cast<YoYInflationTermStructure>(ts);
            QL_REQUIRE(yyTs, "YoY Inflation curve required for vol surface " << index->name());
            index = QuantLib::ext::make_shared<QuantExt::YoYInflationIndexWrapper>(
                parseZeroInflationIndex(config->index(), Handle<ZeroInflationTermStructure>()),
                true, Handle<YoYInflationTermStructure>(yyTs));
        }

        YoYPriceSurfaceFromVolatilities volToPriceConverter;

        auto priceSurface = volToPriceConverter(capVol, index, discountCurve_, quoteVolatilityType, 0.0);

        // Get configuration values for bootstrap
        Real accuracy = config->bootstrapConfig().accuracy();
        Real globalAccuracy = config->bootstrapConfig().globalAccuracy();
        bool dontThrow = config->bootstrapConfig().dontThrow();
        Size maxAttempts = config->bootstrapConfig().maxAttempts();
        Real maxFactor = config->bootstrapConfig().maxFactor();
        Real minFactor = config->bootstrapConfig().minFactor();
        Size dontThrowSteps = config->bootstrapConfig().dontThrowSteps();

        YoYOptionletSurfaceStripper optionletStripper;

        yoyVolSurface_ = optionletStripper(priceSurface, index, discountCurve_, accuracy, globalAccuracy, maxAttempts,
                                           maxFactor, minFactor, dontThrow, dontThrowSteps);

    } else if (config->type() == InflationCapFloorVolatilityCurveConfig::Type::ZC) {

        DLOG("Building InflationCapFloorVolatilityCurveConfig::Type::ZC");
        std::vector<std::vector<Handle<Quote>>> quotes(tenors.size(),
                                                       std::vector<Handle<Quote>>(strikes.size(), Handle<Quote>()));
        for (Size i = 0; i < tenors.size(); ++i) {
            for (Size j = 0; j < strikes.size(); ++j) {
                QuantLib::ext::shared_ptr<SimpleQuote> q(new SimpleQuote(vols[i][j]));
                quotes[i][j] = Handle<Quote>(q);
            }
        }

        QuantLib::ext::shared_ptr<ZeroInflationIndex> index = getIndex(spec, config, inflationCurves);
        
        DLOG("Building surface");
        Date startDate = Date();
        const QuantLib::ext::shared_ptr<Conventions>& conventions = InstrumentConventions::instance().conventions();
        bool interpolated = false;
        if (!config->conventions().empty() && conventions->has(config->conventions())) {
            QuantLib::ext::shared_ptr<InflationSwapConvention> conv =
                QuantLib::ext::dynamic_pointer_cast<InflationSwapConvention>(conventions->get(config->conventions()));
            startDate = getStartAndLag(asof, *conv).first;
            interpolated = conv->interpolated();
        }

        cpiVolSurface_ = QuantLib::ext::make_shared<InterpolatedCPIVolatilitySurface<Bilinear>>(
            tenors, strikes, quotes, index, interpolated, config->settleDays(), config->calendar(),
            config->businessDayConvention(), config->dayCounter(), config->observationLag(), startDate, Bilinear(),
            quoteVolatilityType, 0.0);
        if (config->extrapolate())
            cpiVolSurface_->enableExtrapolation();
        DLOG("Building surface done");
    } else {
        QL_FAIL("InflationCapFloorVolatilityCurveConfig::Type not covered");
    }
}

void InflationCapFloorVolCurve::buildFromPrices(Date asof, InflationCapFloorVolatilityCurveSpec spec,
                                                const Loader& loader,
                                                const QuantLib::ext::shared_ptr<InflationCapFloorVolatilityCurveConfig>& config,
                                                map<string, QuantLib::ext::shared_ptr<YieldCurve>>& yieldCurves,
                                                map<string, QuantLib::ext::shared_ptr<InflationCurve>>& inflationCurves) {

    DLOG("Build InflationCapFloorVolCurve " << spec.name() << " from prices");

    QL_REQUIRE((config->type() == InflationCapFloorVolatilityCurveConfig::Type::ZC) ||
               (config->type() == InflationCapFloorVolatilityCurveConfig::Type::YY),
               "Inflation cap floor pricevolatility surfaces must be of type 'ZC' or 'YY'");

    // Volatility type
    VolatilityType quoteVolatilityType;

    switch (config->volatilityType()) {
    case InflationCapFloorVolatilityCurveConfig::VolatilityType::Lognormal:
        quoteVolatilityType = ShiftedLognormal;
        break;
    case InflationCapFloorVolatilityCurveConfig::VolatilityType::Normal:
        quoteVolatilityType = Normal;
        break;
    case InflationCapFloorVolatilityCurveConfig::VolatilityType::ShiftedLognormal:
        quoteVolatilityType = ShiftedLognormal;
        break;
    default:
        QL_FAIL("unexpected volatility type: '" << config->volatilityType() << "'");
    }

    // Required by QuantLib price surface constructors but apparently not used
    std::vector<Period> terms = parseVectorOfValues<Period>(config->tenors(), &parsePeriod);
    std::vector<Real> capStrikes = parseVectorOfValues<Real>(config->capStrikes(), &parseReal);
    std::vector<Real> floorStrikes = parseVectorOfValues<Real>(config->floorStrikes(), &parseReal);

    Matrix cPrice(capStrikes.size(), capStrikes.size() == 0 ? 0 : terms.size(), Null<Real>()),
        fPrice(floorStrikes.size(), floorStrikes.size() == 0 ? 0 : terms.size(), Null<Real>());

    // Quotes index can differ from the index for which we are building the surface.
    string quoteIndex = config->quoteIndex().empty() ? config->index() : config->quoteIndex();

    // We loop over all market data, looking for quotes that match the configuration

    std::ostringstream ss1;
    ss1 << MarketDatum::InstrumentType::ZC_INFLATIONCAPFLOOR << "/*";
    Wildcard w1(ss1.str());
    auto data1 = loader.get(w1, asof);

    std::ostringstream ss2;
    ss2 << MarketDatum::InstrumentType::YY_INFLATIONCAPFLOOR << "/*";
    Wildcard w2(ss2.str());
    auto data2 = loader.get(w2, asof);

    data1.merge(data2);

    for (const auto& md : data1) {
        QL_REQUIRE(md->asofDate() == asof, "MarketDatum asofDate '" << md->asofDate() << "' <> asof '" << asof << "'");

        QuantLib::ext::shared_ptr<InflationCapFloorQuote> q;

        if (config->type() == InflationCapFloorVolatilityCurveConfig::Type::ZC) {
            q = QuantLib::ext::dynamic_pointer_cast<ZcInflationCapFloorQuote>(md);
        } else {
            q = QuantLib::ext::dynamic_pointer_cast<YyInflationCapFloorQuote>(md);
        }

        if (q && q->index() == quoteIndex && md->quoteType() == MarketDatum::QuoteType::PRICE) {
            auto it1 = std::find(terms.begin(), terms.end(), q->term());
            Real strike = parseReal(q->strike());
            Size strikeIdx = Null<Size>();
            if (q->isCap()) {
                for (Size i = 0; i < capStrikes.size(); ++i) {
                    if (close_enough(capStrikes[i], strike))
                        strikeIdx = i;
                }
            } else {
                for (Size i = 0; i < floorStrikes.size(); ++i) {
                    if (close_enough(floorStrikes[i], strike))
                        strikeIdx = i;
                }
            }
            if (it1 != terms.end() && strikeIdx != Null<Size>()) {
                if (q->isCap()) {
                    cPrice[strikeIdx][it1 - terms.begin()] = q->quote()->value();
                } else {
                    fPrice[strikeIdx][it1 - terms.begin()] = q->quote()->value();
                }
            }
        }
    }
    
    ostringstream capStrikesString, floorStrikesString;
    for (Size i = 0; i < floorStrikes.size(); ++i)
        floorStrikesString << floorStrikes[i] << ",";
    for (Size i = 0; i < capStrikes.size(); ++i)
        capStrikesString << capStrikes[i] << ",";
    DLOG("Building inflation cap floor price surface:");
    DLOG("Cap Strikes are: " << capStrikesString.str());
    DLOG("Floor Strikes are: " << floorStrikesString.str());
    DLOGGERSTREAM("Cap Price Matrix:\n" << cPrice << "Floor Price Matrix:\n" << fPrice);

    if (config->type() == InflationCapFloorVolatilityCurveConfig::Type::ZC) {
        // ZC Curve
        QuantLib::ext::shared_ptr<ZeroInflationIndex> index = getIndex(spec, config, inflationCurves);

        QuantLib::ext::shared_ptr<QuantExt::CPICapFloorEngine> engine;

        bool isLogNormalVol = quoteVolatilityType == QuantLib::ShiftedLognormal;

        if (isLogNormalVol) {
            engine = QuantLib::ext::make_shared<QuantExt::CPIBlackCapFloorEngine>(
                discountCurve_, QuantLib::Handle<QuantLib::CPIVolatilitySurface>(),
                config->useLastAvailableFixingDate());
        } else {
            engine = QuantLib::ext::make_shared<QuantExt::CPIBachelierCapFloorEngine>(
                discountCurve_, QuantLib::Handle<QuantLib::CPIVolatilitySurface>(),
                config->useLastAvailableFixingDate());
        }

        try {
            const QuantLib::ext::shared_ptr<Conventions>& conventions = InstrumentConventions::instance().conventions();
            Date startDate = Date();
            bool interpolated = false;
            if (!config->conventions().empty() && conventions->has(config->conventions())) {
                QuantLib::ext::shared_ptr<InflationSwapConvention> conv =
                    QuantLib::ext::dynamic_pointer_cast<InflationSwapConvention>(conventions->get(config->conventions()));
                startDate = getStartAndLag(asof, *conv).first;
                interpolated = conv->interpolated();
            }
            QuantLib::ext::shared_ptr<QuantExt::CPIPriceVolatilitySurface<Linear, Linear>> cpiCapFloorVolSurface;
            
            // We ignore missing prices and convert all available prices to vols and interpolate misisng vols linear and 
            // extrapolate them flat
            bool ignoreMissingPrices = true;

            cpiCapFloorVolSurface = QuantLib::ext::make_shared<QuantExt::CPIPriceVolatilitySurface<Linear, Linear>>(
                QuantExt::PriceQuotePreference::CapFloor, config->observationLag(), config->calendar(),
                config->businessDayConvention(), config->dayCounter(), index, discountCurve_, capStrikes, floorStrikes,
                terms, cPrice, fPrice, engine, interpolated, startDate, ignoreMissingPrices, true, true,
                quoteVolatilityType, 0.0, CPIPriceVolatilitySurfaceDefaultValues::upperVolBound,
                CPIPriceVolatilitySurfaceDefaultValues::lowerVolBound,
                CPIPriceVolatilitySurfaceDefaultValues::solverTolerance);

            // cast
            cpiVolSurface_ = cpiCapFloorVolSurface;

            cpiVolSurface_->enableExtrapolation();   

            for (Size i = 0; i < cpiCapFloorVolSurface->strikes().size(); ++i) {
                for (Size j = 0; j < cpiCapFloorVolSurface->maturities().size(); ++j) {
                    DLOG("Implied CPI CapFloor BlackVol,strike," << cpiCapFloorVolSurface->strikes()[i] << ",maturity,"
                                                                 << cpiCapFloorVolSurface->maturities()[j] << ",index,"
                                                                 << index->name() << ",Vol,"
                                                                 << cpiCapFloorVolSurface->volData()[i][j]);
                    if (cpiCapFloorVolSurface->missingValues()[i][j]) {
                        WLOG("Implied CPI CapFloor Surface, price misisng for strike "
                             << cpiCapFloorVolSurface->strikes()[i] << ", maturity, "
                             << cpiCapFloorVolSurface->maturities()[j] << ",index," << index->name()
                             << ", ignore missing point and try interpolate the missing vol.")
                    }
                    if (cpiCapFloorVolSurface->pricesFailedToConvert()[i][j]) {
                        WLOG("Implied CPI CapFloor Surface, couldn't imply vol from price for strike "
                             << cpiCapFloorVolSurface->strikes()[i] << ", maturity, "
                             << cpiCapFloorVolSurface->maturities()[j] << ",index," << index->name()
                             << "ignore missing point and try to interpolate the missing vol.");
                    }
                }
            }
            DLOG("CPIVolSurfaces built for spec " << spec.name());
        } catch (std::exception& e) {
            QL_FAIL("Building CPIVolSurfaces failed for spec " << spec.name() << ": " << e.what());
        }
    }
    if (config->type() == InflationCapFloorVolatilityCurveConfig::Type::YY) {

        for (Size j = 0; j < terms.size(); ++j) {
            for (Size i = 0; i < capStrikes.size(); ++i)
                QL_REQUIRE(cPrice[i][j] != Null<Real>(), "quote for cap floor price surface, type cap, strike "
                                                             << capStrikes[i] << ", term " << terms[j]
                                                             << ", not found.");
            for (Size i = 0; i < floorStrikes.size(); ++i)
                QL_REQUIRE(fPrice[i][j] != Null<Real>(), "quote for cap floor price surface, type floor, strike "
                                                             << floorStrikes[i] << ", term " << terms[j]
                                                             << ", not found.");
        }

        // The strike grids have some minimum requirements which we fulfill here at
        // least technically; note that the extrapolated prices will not be sensible,
        // instead only the given strikes for the given option type may be sensible
        // in the end

        static const Real largeStrike = 1.0;
        static const Real largeStrikeFactor = 0.99;

        Size addFloor = 0, addCap = 0;
        if (floorStrikes.size() == 0) {
            floorStrikes.push_back(-largeStrike);
            floorStrikes.push_back(-(largeStrike * largeStrikeFactor));
            addFloor = 2;
        }
        if (floorStrikes.size() == 1) {
            floorStrikes.insert(floorStrikes.begin(), -largeStrike);
            addFloor = 1;
        }
        if (capStrikes.size() == 0) {
            capStrikes.push_back(largeStrike * largeStrikeFactor);
            capStrikes.push_back(largeStrike);
            addCap = 2;
        }
        if (capStrikes.size() == 1) {
            capStrikes.push_back(largeStrike);
            addCap = 1;
        }
        if (addFloor > 0) {
            Matrix tmp(fPrice.rows() + addFloor, terms.size(), 1E-10);
            for (Size i = addFloor; i < fPrice.rows() + addFloor; ++i) {
                for (Size j = 0; j < fPrice.columns(); ++j) {
                    tmp[i][j] = fPrice[i - addFloor][j];
                }
            }
            fPrice = tmp;
        }
        if (addCap > 0) {
            Matrix tmp(cPrice.rows() + addCap, terms.size(), 1E-10);
            for (Size i = 0; i < cPrice.rows(); ++i) {
                for (Size j = 0; j < cPrice.columns(); ++j) {
                    tmp[i][j] = cPrice[i][j];
                }
            }
            cPrice = tmp;
        }

        QuantLib::ext::shared_ptr<YoYInflationIndex> index;
        auto it2 = inflationCurves.find(config->indexCurve());
        if (it2 == inflationCurves.end()) {
            QL_FAIL("The inflation curve, " << config->indexCurve()
                                            << ", required in building the inflation cap floor price surface "
                                            << spec.name() << ", was not found");
        } else {
            QuantLib::ext::shared_ptr<InflationTermStructure> ts = it2->second->inflationTermStructure();
            // Check if the Index curve is a YoY curve - if not it must be a zero curve
            QuantLib::ext::shared_ptr<YoYInflationTermStructure> yyTs =
                QuantLib::ext::dynamic_pointer_cast<YoYInflationTermStructure>(ts);

            if (yyTs) {
                useMarketYoyCurve_ = true;
                index = QuantLib::ext::make_shared<QuantExt::YoYInflationIndexWrapper>(
                    parseZeroInflationIndex(config->index(), Handle<ZeroInflationTermStructure>()),
                    true, Handle<YoYInflationTermStructure>(yyTs));
            } else {
                useMarketYoyCurve_ = false;
                QuantLib::ext::shared_ptr<ZeroInflationTermStructure> zeroTs =
                    QuantLib::ext::dynamic_pointer_cast<ZeroInflationTermStructure>(ts);
                QL_REQUIRE(zeroTs,
                           "Inflation term structure " << config->indexCurve() << "must be of type YoY or Zero");
                index = QuantLib::ext::make_shared<QuantExt::YoYInflationIndexWrapper>(
                    parseZeroInflationIndex(config->index(), Handle<ZeroInflationTermStructure>(zeroTs)),
                    true, Handle<YoYInflationTermStructure>());
            }
        }
        // Required by the QL pricesurface but not used
        Rate startRate = 0.0;
        // Build the term structure
        QL_DEPRECATED_DISABLE_WARNING
        QuantLib::ext::shared_ptr<QuantExt::InterpolatedYoYCapFloorTermPriceSurface<QuantLib::Bilinear, QuantLib::Linear>>
            yoySurface = QuantLib::ext::make_shared<
                QuantExt::InterpolatedYoYCapFloorTermPriceSurface<QuantLib::Bilinear, QuantLib::Linear>>(
                0, config->observationLag(), index, startRate, discountCurve_, config->dayCounter(), config->calendar(),
                config->businessDayConvention(), capStrikes, floorStrikes, terms, cPrice, fPrice);
        QL_DEPRECATED_ENABLE_WARNING
        std::vector<Period> optionletTerms = {yoySurface->maturities().front()};
        while (optionletTerms.back() != terms.back()) {
            optionletTerms.push_back(optionletTerms.back() + Period(1, Years));
        }
        yoySurface->setMaturities(optionletTerms);

        // Get configuration values for bootstrap
        Real accuracy = config->bootstrapConfig().accuracy();
        Real globalAccuracy = config->bootstrapConfig().globalAccuracy();
        bool dontThrow = config->bootstrapConfig().dontThrow();
        Size maxAttempts = config->bootstrapConfig().maxAttempts();
        Real maxFactor = config->bootstrapConfig().maxFactor();
        Real minFactor = config->bootstrapConfig().minFactor();
        Size dontThrowSteps = config->bootstrapConfig().dontThrowSteps();

        YoYOptionletSurfaceStripper optionletStripper;

        yoyVolSurface_ = optionletStripper(yoySurface, index, discountCurve_, accuracy, globalAccuracy, maxAttempts,
                                           maxFactor, minFactor, dontThrow, dontThrowSteps);
    }
}

QuantLib::ext::shared_ptr<ZeroInflationIndex> InflationCapFloorVolCurve::getIndex(
    InflationCapFloorVolatilityCurveSpec spec,
    const QuantLib::ext::shared_ptr<InflationCapFloorVolatilityCurveConfig>& config,
    const map<string, QuantLib::ext::shared_ptr<InflationCurve>>& inflationCurves) {
    DLOG("Building zero inflation index");
    auto it = inflationCurves.find(config->indexCurve());
    if (it == inflationCurves.end()) {
        QL_FAIL("The zero inflation curve, " << config->indexCurve()
            << ", required in building the inflation cap floor vol surface "
            << spec.name() << ", was not found");
    } else {
        QuantLib::ext::shared_ptr<ZeroInflationTermStructure> ts =
            QuantLib::ext::dynamic_pointer_cast<ZeroInflationTermStructure>(it->second->inflationTermStructure());
        QL_REQUIRE(ts,
            "inflation term structure " << config->indexCurve() << " was expected to be zero, but is not");
        return parseZeroInflationIndex(config->index(), Handle<ZeroInflationTermStructure>(ts));
    }
}

void InflationCapFloorVolCurve::setCalibrationInfo(
    Date asof,
    InflationCapFloorVolatilityCurveSpec spec,
    const QuantLib::ext::shared_ptr<InflationCapFloorVolatilityCurveConfig>& config,
    const CurveConfigurations& curveConfigs,
    map<string, QuantLib::ext::shared_ptr<InflationCurve>>& inflationCurves) {

    QL_REQUIRE(cpiVolSurface_, "CPI vol surface cannot be null");

    ReportConfig rc = effectiveReportConfig(curveConfigs.reportConfigInflationCapFloorVols(), config->reportConfig());

    std::vector<Real> strikes = *rc.strikes();
    std::vector<Period> expiries = *rc.expiries();

    calibrationInfo_ = QuantLib::ext::make_shared<CpiVolCalibrationInfo>();
    calibrationInfo_->strikes = strikes;
    calibrationInfo_->dayCounter = config->dayCounter().empty() ? "na" : config->dayCounter().name();
    calibrationInfo_->calendar = config->calendar().empty() ? "na" : config->calendar().name();

    std::vector<Real> times, optionLifeTimes, forwards;
    for (auto const& p : expiries) {
        Date d = cpiVolSurface_->optionDateFromTenor(p);
        calibrationInfo_->expiryDates.push_back(d);
        Date optionObservationDate = QuantExt::ZeroInflation::fixingDate(d, cpiVolSurface_->observationLag(), cpiVolSurface_->frequency(), cpiVolSurface_->indexIsInterpolated());
        calibrationInfo_->optionObservationDates.push_back(optionObservationDate);
        Time optionLifeTime = inflationYearFraction(cpiVolSurface_->frequency(), cpiVolSurface_->indexIsInterpolated(), cpiVolSurface_->dayCounter(), cpiVolSurface_->baseDate(), optionObservationDate);
        optionLifeTimes.push_back(optionLifeTime);
        times.push_back(cpiVolSurface_->dayCounter().empty() ? Actual365Fixed().yearFraction(asof, optionObservationDate)
            : cpiVolSurface_->timeFromReference(optionObservationDate));
        forwards.push_back(cpiVolSurface_->atmStrike(d));
    }

    calibrationInfo_->times = times;
    calibrationInfo_->optionLifeTimes = optionLifeTimes;
    calibrationInfo_->forwards = forwards;

    std::vector<std::vector<Real>> callPricesStrike(times.size(), std::vector<Real>(strikes.size(), 0.0));

    calibrationInfo_->isArbitrageFree = true;

    calibrationInfo_->strikeGridProb =
        std::vector<std::vector<Real>>(times.size(), std::vector<Real>(strikes.size(), 0.0));
    calibrationInfo_->strikeGridImpliedVolatility =
        std::vector<std::vector<Real>>(times.size(), std::vector<Real>(strikes.size(), 0.0));
    calibrationInfo_->strikeGridCallSpreadArbitrage =
        std::vector<std::vector<bool>>(times.size(), std::vector<bool>(strikes.size(), true));
    calibrationInfo_->strikeGridButterflyArbitrage =
        std::vector<std::vector<bool>>(times.size(), std::vector<bool>(strikes.size(), true));
    std::vector<Real> atmGrowths =
        std::vector<Real>(times.size(), 0.0);
    std::vector<std::vector<Real>> strikeGrowths =
        std::vector<std::vector<Real>>(times.size(), std::vector<Real>(strikes.size(), 0.0));
    calibrationInfo_->forwardCPI =
        std::vector<Real>(times.size(), 0.0);
    calibrationInfo_->strikeCPI =
        std::vector<std::vector<Real>>(times.size(), std::vector<Real>(strikes.size(), 0.0));
    QuantLib::ext::shared_ptr<ZeroInflationIndex> index = getIndex(spec, config, inflationCurves);
    auto lastKnownFixingDate = ZeroInflation::lastAvailableFixing(*index, cpiVolSurface_->referenceDate());
    auto baseCPI = ZeroInflation::cpiFixing(index, cpiVolSurface_->baseDate(), 0 * Days, cpiVolSurface_->indexIsInterpolated());
    for (Size i = 0; i < times.size(); ++i) {
        Real t = times[i];
        bool validSlice = true;
        Time ttm = optionLifeTimes[i];
        atmGrowths[i] = std::pow(1 + forwards[i], ttm);
        calibrationInfo_->forwardCPI[i] = atmGrowths[i] * baseCPI;
        for (Size j = 0; j < strikes.size(); ++j) {
            try {
                Real strike = strikes[j];
                Real vol = cpiVolSurface_->volatility(calibrationInfo_->optionObservationDates[i], strike, 0 * Days);
                Real stddev;
                if (config->useLastAvailableFixingDate()) {
                    auto ttm = inflationYearFraction(cpiVolSurface_->frequency(), cpiVolSurface_->indexIsInterpolated(),
                        cpiVolSurface_->dayCounter(), lastKnownFixingDate, calibrationInfo_->optionObservationDates[i]);
                    stddev = std::sqrt(ttm * vol * vol);
                } else {
                    stddev = std::sqrt(cpiVolSurface_->totalVariance(calibrationInfo_->optionObservationDates[i], strike, 0 * Days));
                }

                strikeGrowths[i][j] = std::pow(1 + strike, ttm);
                calibrationInfo_->strikeCPI[i][j] = strikeGrowths[i][j] * baseCPI;
                callPricesStrike[i][j] = blackFormula(Option::Call, strikeGrowths[i][j], atmGrowths[i], stddev);

                calibrationInfo_->strikeGridImpliedVolatility[i][j] = stddev / std::sqrt(t);
            } catch (const std::exception& e) {
                validSlice = false;
                TLOG("error for time " << t << " strikes " << strikes[j] << ": " << e.what());
            }
        }
        if (validSlice) {
            try {
                QuantExt::CarrMadanMarginalProbability cm(strikeGrowths[i],
                    atmGrowths[i], callPricesStrike[i]);
                calibrationInfo_->strikeGridCallSpreadArbitrage[i] = cm.callSpreadArbitrage();
                calibrationInfo_->strikeGridButterflyArbitrage[i] = cm.butterflyArbitrage();
                if (!cm.arbitrageFree())
                    calibrationInfo_->isArbitrageFree = false;
                calibrationInfo_->strikeGridProb[i] = cm.density();
                TLOGGERSTREAM(arbitrageAsString(cm));
            } catch (const std::exception& e) {
                TLOG("error for time " << t << ": " << e.what());
                calibrationInfo_->isArbitrageFree = false;
                TLOGGERSTREAM("..(invalid slice)..");
            }
        } else {
            calibrationInfo_->isArbitrageFree = false;
            TLOGGERSTREAM("..(invalid slice)..");
        }
    }
}

} // namespace data
} // namespace ore
