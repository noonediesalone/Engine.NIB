/*
  Copyright (C) 2019 Quaternion Risk Management Ltd
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

#include <ored/portfolio/barrieroptionwrapper.hpp>
#include <ored/portfolio/builders/fxdoublebarrieroption.hpp>
#include <ored/portfolio/fxdoublebarrieroption.hpp>
#include <ored/utilities/parsers.hpp>

#include <boost/make_shared.hpp>
#include <ored/portfolio/builders/fxoption.hpp>
#include <ored/portfolio/enginefactory.hpp>
#include <ored/portfolio/fxoption.hpp>
#include <ored/utilities/indexparser.hpp>
#include <ored/utilities/log.hpp>
#include <ql/errors.hpp>
#include <ql/exercise.hpp>
#include <ql/instruments/barrieroption.hpp>
#include <ql/instruments/barriertype.hpp>
#include <ql/instruments/compositeinstrument.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <qle/indexes/fxindex.hpp>

using namespace QuantLib;

namespace ore {
namespace data {

void FxDoubleBarrierOption::checkBarriers() {
    QL_REQUIRE(barrier().levels().size() == 2, "Invalid number of barrier levels. Must have two.");
    QL_REQUIRE(barrier().style().empty() || barrier().style() == "American", "Only american barrier style suppported");
}

QuantLib::ext::shared_ptr<QuantLib::PricingEngine>
FxDoubleBarrierOption::vanillaPricingEngine(const QuantLib::ext::shared_ptr<EngineFactory>& ef,
                                              const QuantLib::Date& expiryDate, const QuantLib::Date& paymentDate) {

    if (paymentDate > expiryDate) {
        QuantLib::ext::shared_ptr<EngineBuilder> builder = ef->builder("FxOptionEuropeanCS");
        QL_REQUIRE(builder, "No builder found for FxOptionEuropeanCS");

        QuantLib::ext::shared_ptr<FxEuropeanCSOptionEngineBuilder> fxOptBuilder =
            QuantLib::ext::dynamic_pointer_cast<FxEuropeanCSOptionEngineBuilder>(builder);
        QL_REQUIRE(fxOptBuilder, "No FxEuropeanOptionEngineBuilder found");

        setSensitivityTemplate(*fxOptBuilder);
        addProductModelEngine(*fxOptBuilder);

        return fxOptBuilder->engine(parseCurrency(boughtCurrency_), parseCurrency(soldCurrency_), envelope().additionalField("discount_curve", false, std::string()), paymentDate);
    } else {
        QuantLib::ext::shared_ptr<EngineBuilder> builder = ef->builder("FxOption");
        QL_REQUIRE(builder, "No builder found for FxOption");

        QuantLib::ext::shared_ptr<FxEuropeanOptionEngineBuilder> fxOptBuilder =
            QuantLib::ext::dynamic_pointer_cast<FxEuropeanOptionEngineBuilder>(builder);
        QL_REQUIRE(fxOptBuilder, "No FxEuropeanOptionEngineBuilder found");

        return fxOptBuilder->engine(parseCurrency(boughtCurrency_), parseCurrency(soldCurrency_), envelope().additionalField("discount_curve", false, std::string()), expiryDate);
    }
}

QuantLib::ext::shared_ptr<QuantLib::PricingEngine>
FxDoubleBarrierOption::barrierPricingEngine(const QuantLib::ext::shared_ptr<EngineFactory>& ef,
                                              const QuantLib::Date& expiryDate, const QuantLib::Date& paymentDate) {

    QuantLib::ext::shared_ptr<EngineBuilder> builder = ef->builder(tradeType_);
    QL_REQUIRE(builder, "No builder found for " << tradeType_);

    QuantLib::ext::shared_ptr<FxDoubleBarrierOptionEngineBuilder> fxBarrierOptBuilder =
        QuantLib::ext::dynamic_pointer_cast<FxDoubleBarrierOptionEngineBuilder>(builder);
    QL_REQUIRE(fxBarrierOptBuilder, "No fxBarrierOptBuilder found");

    setSensitivityTemplate(*fxBarrierOptBuilder);
    addProductModelEngine(*fxBarrierOptBuilder);

    auto engine = fxBarrierOptBuilder->engine(parseCurrency(boughtCurrency_), parseCurrency(soldCurrency_), paymentDate);
    return engine;
}

} // namespace data
} // namespace oreplus
