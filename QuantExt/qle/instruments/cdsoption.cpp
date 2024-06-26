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

/*
 Copyright (C) 2008 Roland Stamm
 Copyright (C) 2009 Jose Aparicio

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <qle/instruments/cdsoption.hpp>
#include <qle/pricingengines/blackcdsoptionengine.hpp>

#include <ql/exercise.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/math/solvers1d/brent.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>

#include <boost/make_shared.hpp>

namespace QuantExt {
using namespace QuantLib;

namespace {

class ImpliedVolHelper {
public:
    ImpliedVolHelper(const CdsOption& cdsoption, const Handle<DefaultProbabilityTermStructure>& probability,
                     Real recoveryRate, const Handle<YieldTermStructure>& termStructure, Real targetValue)
        : targetValue_(targetValue) {

        vol_ = QuantLib::ext::shared_ptr<SimpleQuote>(new SimpleQuote(0.0));
        Handle<BlackVolTermStructure> h(
            QuantLib::ext::make_shared<BlackConstantVol>(0, NullCalendar(), Handle<Quote>(vol_), Actual365Fixed()));
        engine_ = QuantLib::ext::shared_ptr<PricingEngine>(
            new QuantExt::BlackCdsOptionEngine(probability, recoveryRate, termStructure,
                                               Handle<CreditVolCurve>(QuantLib::ext::make_shared<CreditVolCurveWrapper>(h))));
        cdsoption.setupArguments(engine_->getArguments());

        results_ = dynamic_cast<const Instrument::results*>(engine_->getResults());
    }
    Real operator()(Volatility x) const {
        vol_->setValue(x);
        engine_->calculate();
        return results_->value - targetValue_;
    }

private:
    QuantLib::ext::shared_ptr<PricingEngine> engine_;
    Real targetValue_;
    QuantLib::ext::shared_ptr<SimpleQuote> vol_;
    const Instrument::results* results_;
};
} // namespace

CdsOption::CdsOption(const QuantLib::ext::shared_ptr<CreditDefaultSwap>& swap, const QuantLib::ext::shared_ptr<Exercise>& exercise,
                     bool knocksOut, const Real strike, const StrikeType strikeType)
    : Option(QuantLib::ext::shared_ptr<Payoff>(new NullPayoff), exercise), swap_(swap), knocksOut_(knocksOut),
      strike_(strike == Null<Real>() ? swap_->runningSpread() : strike), strikeType_(strikeType) {
    registerWith(swap_);
}

bool CdsOption::isExpired() const { return detail::simple_event(exercise_->dates().back()).hasOccurred(); }

void CdsOption::setupExpired() const {
    Instrument::setupExpired();
    riskyAnnuity_ = 0.0;
}

void CdsOption::setupArguments(PricingEngine::arguments* args) const {
    swap_->setupArguments(args);
    Option::setupArguments(args);

    CdsOption::arguments* arguments = dynamic_cast<CdsOption::arguments*>(args);

    QL_REQUIRE(arguments != 0, "wrong argument type");

    arguments->swap = swap_;
    arguments->knocksOut = knocksOut_;
    arguments->strike = strike_;
    arguments->strikeType = strikeType_;
}

void CdsOption::fetchResults(const PricingEngine::results* r) const {
    Option::fetchResults(r);
    const CdsOption::results* results = dynamic_cast<const CdsOption::results*>(r);
    QL_ENSURE(results != 0, "wrong results type");
    riskyAnnuity_ = results->riskyAnnuity;
}

Rate CdsOption::atmRate() const { return swap_->fairSpreadClean(); }

Real CdsOption::riskyAnnuity() const {
    calculate();
    QL_REQUIRE(riskyAnnuity_ != Null<Real>(), "risky annuity not provided");
    return riskyAnnuity_;
}

Volatility CdsOption::impliedVolatility(Real targetValue, const Handle<YieldTermStructure>& termStructure,
                                        const Handle<DefaultProbabilityTermStructure>& probability, Real recoveryRate,
                                        Real accuracy, Size maxEvaluations, Volatility minVol,
                                        Volatility maxVol) const {
    calculate();
    QL_REQUIRE(!isExpired(), "instrument expired");

    Volatility guess = 0.10;

    ImpliedVolHelper f(*this, probability, recoveryRate, termStructure, targetValue);
    Brent solver;
    solver.setMaxEvaluations(maxEvaluations);
    return solver.solve(f, accuracy, guess, minVol, maxVol);
}

void CdsOption::arguments::validate() const {
    CreditDefaultSwap::arguments::validate();
    Option::arguments::validate();
    QL_REQUIRE(swap, "CDS not set");
    QL_REQUIRE(exercise, "exercise not set");
}

void CdsOption::results::reset() {
    Option::results::reset();
    riskyAnnuity = Null<Real>();
}
} // namespace QuantExt
