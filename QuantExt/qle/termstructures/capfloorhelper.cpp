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

#include <qle/termstructures/capfloorhelper.hpp>
#include <ql/instruments/makecapfloor.hpp>
#include <ql/pricingengines/capfloor/bacheliercapfloorengine.hpp>
#include <ql/pricingengines/capfloor/blackcapfloorengine.hpp>
#include <ql/quotes/derivedquote.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

using namespace QuantLib;
using std::ostream;

namespace {
void no_deletion(OptionletVolatilityStructure*) {}
}

namespace QuantExt {

// The argument to RelativeDateBootstrapHelper below is not simply the `quote`. Instead we create a DerivedQuote that,
// each time it is asked for its value, it returns a premium by calling CapFloorHelper::npv with the `quote` value. In 
// this way, the `BootstrapHelper<T>::quoteError()` is always based on the cap floor premium and we do not have imply 
// the volatility. This leads to all kinds of issues when deciding on max and min values in the iterative bootstrap 
// where the quote volatility type input is of one type and the optionlet structure that we are trying to build is of 
// another different type. 
CapFloorHelper::CapFloorHelper(
    CapFloor::Type type,
    const Period& tenor,
    Rate strike,
    const Handle<Quote>& quote,
    const boost::shared_ptr<IborIndex>& iborIndex,
    const Handle<YieldTermStructure>& discountingCurve,
    QuoteType quoteType,
    QuantLib::VolatilityType quoteVolatilityType,
    QuantLib::Real quoteDisplacement,
    bool endOfMonth)
    : RelativeDateBootstrapHelper<OptionletVolatilityStructure>(Handle<Quote>(
        boost::make_shared<DerivedQuote<boost::function<Real(Real)>>>(
            quote, boost::bind(&CapFloorHelper::npv, this, _1)))),
      type_(type),
      tenor_(tenor),
      strike_(strike),
      iborIndex_(iborIndex),
      discountHandle_(discountingCurve),
      quoteType_(quoteType),
      quoteVolatilityType_(quoteVolatilityType),
      quoteDisplacement_(quoteDisplacement),
      endOfMonth_(endOfMonth),
      rawQuote_(quote) {

    registerWith(iborIndex_);
    registerWith(discountHandle_);
    
    initializeDates();
}

void CapFloorHelper::initializeDates() {

    // Initialise the instrument and a copy
    capFloor_ = MakeCapFloor(type_, tenor_, iborIndex_, strike_, 0 * Days).withEndOfMonth(endOfMonth_);
    capFloorCopy_ = MakeCapFloor(type_, tenor_, iborIndex_, strike_, 0 * Days).withEndOfMonth(endOfMonth_);

    // Maturity date is just the maturity date of the cap floor
    maturityDate_ = capFloor_->maturityDate();

    // We need the leg underlying the cap floor to determine the remaining date members
    const Leg& leg = capFloor_->floatingLeg();

    // Earliest date is the first optionlet fixing date
    boost::shared_ptr<CashFlow> cf = leg.front();
    boost::shared_ptr<FloatingRateCoupon> frc = boost::dynamic_pointer_cast<FloatingRateCoupon>(cf);
    QL_REQUIRE(frc, "Expected the first cashflow on the cap floor instrument to be a FloatingRateCoupon");
    earliestDate_ = frc->fixingDate();
    
    // Remaining dates are each equal to the fixing date on the final optionlet
    cf = leg.back();
    frc = boost::dynamic_pointer_cast<FloatingRateCoupon>(cf);
    QL_REQUIRE(frc, "Expected the final cashflow on the cap floor instrument to be a FloatingRateCoupon");
    pillarDate_ = latestDate_ = latestRelevantDate_ = frc->fixingDate();
}

void CapFloorHelper::setTermStructure(OptionletVolatilityStructure* ovts) {
    
    // Set this helper's optionlet volatility structure
    boost::shared_ptr<OptionletVolatilityStructure> temp(ovts, no_deletion);
    ovtsHandle_.linkTo(temp, false);

    // Set the term structure pointer member variable in the base class
    RelativeDateBootstrapHelper<OptionletVolatilityStructure>::setTermStructure(ovts);

    // Set this helper's pricing engine depending on the type of the optionlet volatilities
    if (ovts->volatilityType() == ShiftedLognormal) {
        capFloor_->setPricingEngine(boost::make_shared<BlackCapFloorEngine>(discountHandle_, ovtsHandle_));
    } else {
        capFloor_->setPricingEngine(boost::make_shared<BachelierCapFloorEngine>(discountHandle_, ovtsHandle_));
    }

    // If the quote type is not a premium, we will need to use capFloorCopy_ to return the premium from the volatility
    // quote 
    if (quoteType_ != Premium) {
        if (quoteVolatilityType_ == ShiftedLognormal) {
            capFloorCopy_->setPricingEngine(boost::make_shared<BlackCapFloorEngine>(discountHandle_, rawQuote_,
                ovtsHandle_->dayCounter(), quoteDisplacement_));
        } else {
            capFloorCopy_->setPricingEngine(boost::make_shared<BachelierCapFloorEngine>(discountHandle_, rawQuote_,
                ovtsHandle_->dayCounter()));
        }
    }
}

Real CapFloorHelper::impliedQuote() const {
    QL_REQUIRE(termStructure_ != 0, "CapFloorHelper's optionlet volatility term structure has not been set");
    capFloor_->recalculate();
    return capFloor_->NPV();
}

void CapFloorHelper::accept(AcyclicVisitor& v) {
    if (Visitor<CapFloorHelper>* v1 = dynamic_cast<Visitor<CapFloorHelper>*>(&v))
        v1->visit(*this);
    else
        RelativeDateBootstrapHelper<OptionletVolatilityStructure>::accept(v);
}

Real CapFloorHelper::npv(Real quoteValue) {
    if (quoteType_ == Premium) {
        return quoteValue;
    } else {
        // If the quote value is a volatility, return the premium
        return capFloorCopy_->NPV();
    }
}

ostream& operator<<(ostream& out, CapFloorHelper::QuoteType type) {
    switch (type) {
    case CapFloorHelper::Volatility:
        return out << "Volatility";
    case CapFloorHelper::Premium:
        return out << "Premium";
    default:
        QL_FAIL("Unknown CapFloorHelper::QuoteType (" << Integer(type) << ")");
    }
}

}
