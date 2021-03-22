/*
 Copyright (C) 2020 Quaternion Risk Management Ltd
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

#include <qle/models/carrmadanarbitragecheck.hpp>

#include <ql/errors.hpp>
#include <ql/math/comparison.hpp>

#include <sstream>

namespace QuantExt {

CarrMadanMarginalProbability::CarrMadanMarginalProbability(const std::vector<Real>& moneyness, const Real spot,
                                                           const Real forward, const std::vector<Real>& callPrices)
    : moneyness_(moneyness), spot_(spot), forward_(forward), callPrices_(callPrices) {

    // check input

    QL_REQUIRE(moneyness_.size() == callPrices_.size(), "CarrMadanMarginalProbability: moneyness ("
                                                            << moneyness_.size() << ") inconsistent to callPrices ("
                                                            << callPrices.size() << ")";)

    QL_REQUIRE(moneyness_.empty(), "CarrMadanMarginalProbability: input moneyness is empty");

    for (Size i = 1; i < moneyness_.size(); ++i) {
        QL_REQUIRE(moneyness_[i] > moneyness_[i - 1] && !close_enough(moneyness_[i], moneyness_[i - 1]),
                   "CarrMadanMarginalProbability: moneyness not increasing at index "
                       << (i - 1) << ", " << i << ": " << moneyness_[i - 1] << ", " << moneyness_[i]);
    }

    QL_REQUIRE(moneyness_.front() > 0.0 || close_enough(moneyness_.front(), 0.0),
               "CarrMadanMarginalProbability: all input moneyness must be positive or zero, got "
                   << moneyness_.front());

    // add moneyness 0 and corresponding call price (= forward), if not already present

    if (!close_enough(moneyness_.front(), 0.0)) {
        moneyness_.insert(moneyness_.begin(), 0.0);
        callPrices_.insert(callPrices_.begin(), forward_);
    } else {
        QL_REQUIRE(close_enough(callPrices_.front(), forward_),
                   "CarrMadanMarginalProbability: call price for moneyness 0 ("
                       << callPrices_.front() << ") should match forward (" << forward << ")");
    }

    // check we have two strikes at least

    QL_REQUIRE(moneyness.size() >= 2,
               "CarrMadanMarginalProbability: at least two moneyness levels required (after adding 0)");

    // calculate strikes

    for (auto const& m : moneyness)
        strikes_.push_back(m * forward);

    /* apply the transformation S -> S' := spot / forward * S, so that the assumptions of the paper are fulfilled,
       i.e. zero rates and dividends */

    std::vector<Real> transformedStrikes, transformedCallPrices;

    for (auto const& k : strikes_)
        transformedStrikes.push_back(k * spot / forward);

    for (auto const& p : callPrices_)
        transformedStrikes.push_back(p * spot / forward);

    // compute Q

    std::vector<Real> Q(transformedStrikes.size() - 1);
    for (Size i = 1; i < transformedStrikes.size(); ++i) {
        Q[i - 1] = (transformedCallPrices[i - 1] - transformedCallPrices[i]) /
                   (transformedStrikes[i] - transformedStrikes[i - 1]);
    }

    // compute BS

    std::vector<Real> BS(transformedStrikes.size() - 2);
    for (Size i = 1; i < transformedStrikes.size() - 1; ++i) {
        BS[i - 1] = transformedCallPrices[i - 1] -
                    (transformedStrikes[i + 1] - transformedStrikes[i - 1]) /
                        (transformedStrikes[i + 1] - transformedStrikes[i]) * transformedCallPrices[i] +
                    (transformedStrikes[i] - transformedStrikes[i - 1]) /
                        (transformedStrikes[i + 1] - transformedStrikes[i]) * transformedCallPrices[i + 1];
    }

    // perform the checks 1, 2 from the paper, and populate the set of violations

    violationsType1_.resize(Q.size(), false);
    violationsType2_.resize(BS.size(), false);

    // check 1: Q(i,j) in [0,1]

    for (Size i = 0; i < Q.size(); ++i) {
        if ((Q[i] < 0.0 && !close_enough(Q[i], 0.0)) || (Q[i] > 1.0 && !close_enough(Q[i], 1.0))) {
            violationsType1_[i] = true;
        }
    }

    // check 2: BS(i,j) >= 0

    for (Size i = 0; i < BS.size(); ++i) {
        if (BS[i] < 0.0 && !close_enough(BS[i], 0.0)) {
            violationsType2_[i] = true;
        }
    }

    // compute the density q

    q_.resize(Q.size());
    q_[0] = 1.0 - Q.front();
    for (Size i = 0; i < Q.size() - 1; ++i) {
        q_[i + 1] = Q[i] - Q[i + 1];
    }
}

const std::vector<Real>& CarrMadanMarginalProbability::moneyness() const { return moneyness_; }
Real CarrMadanMarginalProbability::spot() const { return spot_; }
Real CarrMadanMarginalProbability::forward() const { return forward_; }
const std::vector<Real>& CarrMadanMarginalProbability::callPrices() const { return callPrices_; }

bool CarrMadanMarginalProbability::arbitrageFree() const {
    return violationsType1_.empty() && violationsType2_.empty();
}

const std::vector<bool>& CarrMadanMarginalProbability::violationsType1() const { return violationsType1_; }

const std::vector<bool>& CarrMadanMarginalProbability::violationsType2() const { return violationsType2_; }

std::string arbitrageViolationsAsString(const CarrMadanMarginalProbability& cm) {
    std::ostringstream out;
    for (Size i = 0; i < cm.strikes().size(); ++i) {
        Size code = 0;
        if (i < cm.strikes().size() - 1 && cm.violationsType1()[i])
            code += 1;
        if (i > 0 && i < cm.strikes().size() - 1 && cm.violationsType2()[i - 1])
            code += 2;
        out << (code > 0 ? std::to_string(code) : ".");
    }
    return out.str();
}

const std::vector<Real>& CarrMadanMarginalProbability::density() const { return q_; }
const std::vector<Real>& CarrMadanMarginalProbability::strikes() const { return strikes_; }

CarrMadanSurface::CarrMadanSurface(const std::vector<Real>& times, const std::vector<Real>& moneyness, const Real spot,
                                   const Handle<YieldTermStructure>& r, const Handle<YieldTermStructure>& q,
                                   const std::vector<std::vector<Real>>& callPrices)
    : times_(times), moneyness_(moneyness), spot_(spot), r_(r), q_(q), callPrices_(callPrices) {

    // checks

    QL_REQUIRE(times.size() == callPrices.size(),
               "CarrMadanSurface: times size (" << times.size() << ") does not match callPrices outer vector size ("
                                                << callPrices.size() << ")");

    QL_REQUIRE(times_.empty(), "CarrMadanSurface: times are empty");

    for (Size i = 1; i < times_.size(); ++i) {
        QL_REQUIRE(times_[i] > times_[i - 1] && !close_enough(times_[i], times_[i - 1]),
                   "CarrMadanSurface: times not increasing at index " << (i - 1) << ", " << i << ": " << times_[i - 1]
                                                                      << ", " << times_[i]);
    }

    QL_REQUIRE(times_.front() > 0.0 || close_enough(times_.front(), 0.0),
               "CarrMadanSurface: all input times must be positive or zero, got " << times_.front());

    for (Size i = 0; i < times_.size(); ++i) {
        QL_REQUIRE(callPrices_[i].size() == moneyness_.size(), "CarrMadanSurface: callPrices at time "
                                                                   << times_[i] << "(" << callPrices_[i].size()
                                                                   << ") should match moneyness size ("
                                                                   << moneyness_.size() << ")");
    }

    /*  add zero moneyness, if not present, further checks on the moneyness vector are done later
        when constructing the time slices */

    if (moneyness_.empty() || !close_enough(moneyness_.front(), 0.0)) {
        moneyness_.insert(moneyness_.begin(), 0.0);
        for (Size i = 0; i < callPrices_.size(); ++i) {
            callPrices_[i].insert(callPrices_[i].begin(), forward(times_[i]));
        }
    }

    // add time 0 and corresponding call prices (= spot - strike), if not already present

    if (!close_enough(times_.front(), 0.0)) {
        times_.insert(times_.begin(), 0.0);
        callPrices_.insert(callPrices_.begin(), std::vector<Real>());
        for (Size i = 0; i < moneyness_.size(); ++i) {
            callPrices_.front().push_back(std::max(0.0, spot - moneyness_[i] * spot));
        }
    } else {
        for (Size i = 0; i < moneyness_.size(); ++i) {
            QL_REQUIRE(close_enough(callPrices_.front()[i], std::max(0.0, spot - moneyness_[i] * spot)),
                       "CarrMadanSurface: call price for time 0, strike "
                           << moneyness_[i] * spot << " (" << callPrices_.front()[i] << ") should match spot (" << spot
                           << ") minus strike (" << spot - moneyness_[i] * spot << ")");
        }
    }

    // construct the time slices

    surfaceIsArbitrageFree_ = true;
    for (Size i = 0; i < times_.size(); ++i) {
        timeSlices_.push_back(CarrMadanMarginalProbability(moneyness_, spot_, forward(times_[i]), callPrices_[i]));
        surfaceIsArbitrageFree_ = surfaceIsArbitrageFree_ && timeSlices_.back().arbitrageFree();
    }

    // check for calendar arbitrage

    calendarArbitrage_ = std::vector<std::vector<bool>>(times_.size() - 1, std::vector<bool>(moneyness_.size()));
    for (Size i = 0; i < moneyness_.size(); ++i) {
        for (Size j = 0; j < times_.size() - 1; ++j) {
            Real c2 = callPrices_[j + 1][i] * spot_ / forward(times[j + 1]);
            Real c1 = callPrices_[j][i] * spot_ / forward(times[j]);
            bool af = c2 > c1 || close_enough(c1, c2);
            calendarArbitrage_[j][i] = !af;
            surfaceIsArbitrageFree_ = surfaceIsArbitrageFree_ && af;
        }
    }
}

Real CarrMadanSurface::forward(const Real t) const { return spot_ / r_->discount(t) * q_->discount(t); }

const std::vector<Real>& CarrMadanSurface::times() const { return times_; }
const std::vector<Real>& CarrMadanSurface::moneyness() const { return moneyness_; }
Real CarrMadanSurface::spot() const { return spot_; }
const Handle<YieldTermStructure>& CarrMadanSurface::r() const { return r_; }
const Handle<YieldTermStructure>& CarrMadanSurface::q() const { return q_; }
const std::vector<std::vector<Real>>& CarrMadanSurface::callPrices() const { return callPrices_; }

bool CarrMadanSurface::arbitrageFree() const { return surfaceIsArbitrageFree_; }
const std::vector<std::vector<bool>>& CarrMadanSurface::calendarArbitrage() const { return calendarArbitrage_; }

const std::vector<CarrMadanMarginalProbability>& CarrMadanSurface::timeSlices() const { return timeSlices_; }

std::string arbitrageViolationsAsString(const CarrMadanSurface& cm) {
    std::ostringstream out;
    for (Size j = 0; j < cm.times().size() - 1; ++j) {
        Size code = 0;
        for (Size i = 0; i < cm.moneyness().size(); ++i) {
            if (i < cm.moneyness().size() - 1 && cm.timeSlices()[j].violationsType1()[i])
                code += 1;
            if (i > 0 && i < cm.moneyness().size() - 1 && cm.timeSlices()[j].violationsType2()[i - 1])
                code += 2;
            if (cm.calendarArbitrage()[j][i])
                code += 4;
            out << (code > 0 ? std::to_string(code) : ".");
        }
        out << "\n";
    }
    return out.str();
}

} // namespace QuantExt
