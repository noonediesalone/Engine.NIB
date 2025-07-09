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

/*! \file models/lgmcg.hpp
    \brief computation graph based lgm model calculations
    \ingroup models
*/

#pragma once

#include <ored/scripting/models/modelcg.hpp>

#include <qle/ad/computationgraph.hpp>
#include <qle/models/irlgm1fparametrization.hpp>

#include <ql/indexes/iborindex.hpp>

namespace ore::data {

using namespace QuantLib;
using namespace QuantExt;

class LgmCG {
public:
    LgmCG(const std::string& qualifier, QuantExt::ComputationGraph& g,
          const std::function<QuantLib::ext::shared_ptr<IrLgm1fParametrization>()>& p,
          std::set<ModelCG::ModelParameter>& modelParameters, std::set<ModelCG::ModelParameter>& cachedParameters)
        : qualifier_(qualifier), g_(g), p_(p), modelParameters_(modelParameters), cachedParameters_(cachedParameters) {}

    QuantLib::ext::shared_ptr<IrLgm1fParametrization> parametrization() const { return p_(); }

    std::size_t numeraire(const Date& d, const std::size_t x,
                          const Handle<YieldTermStructure>& discountCurve = Handle<YieldTermStructure>(),
                          const std::string& discountCurveId = "default") const;

    std::size_t discountBond(const Date& d, const Date& e, const std::size_t x,
                             const Handle<YieldTermStructure>& discountCurve = Handle<YieldTermStructure>(),
                             const std::string& discountCurveId = "default") const;

    std::size_t reducedDiscountBond(const Date& d, Date e, const std::size_t x,
                                    const Handle<YieldTermStructure>& discountCurve = Handle<YieldTermStructure>(),
                                    const std::string& discountCurveId = "default",
                                    const Date& expiryDate = Date()) const;

    /* Handles IborIndex and SwapIndex. Requires observation time t <= fixingDate */
    std::size_t fixing(const QuantLib::ext::shared_ptr<InterestRateIndex>& index, const Date& fixingDate, const Date& t,
                       const std::size_t x) const;

    /* Exact if no cap/floors are present and t <= first value date.
       Approximations are applied for t > first value date or when cap / floors are present. */
    std::size_t compoundedOnRate(const QuantLib::ext::shared_ptr<OvernightIndex>& index,
                                    const std::vector<Date>& fixingDates, const std::vector<Date>& valueDates,
                                    const std::vector<Real>& dt, const Natural rateCutoff, const bool includeSpread,
                                    const Real spread, const Real gearing, const Period lookback, Real cap, Real floor,
                                    const bool localCapFloor, const bool nakedOption, const Date& t,
                                    const std::size_t x) const;

    /* Exact if no cap/floors are present and t <= first value date.
       Approximations are applied for t > first value date or when cap / floors are present. */
    std::size_t averagedOnRate(const QuantLib::ext::shared_ptr<OvernightIndex>& index,
                               const std::vector<Date>& fixingDates, const std::vector<Date>& valueDates,
                               const std::vector<Real>& dt, const Natural rateCutoff, const bool includeSpread,
                               const Real spread, const Real gearing, const Period lookback, Real cap, Real floor,
                               const bool localCapFloor, const bool nakedOption, const Date& t,
                               const std::size_t x) const;

private:
    std::string qualifier_;
    QuantExt::ComputationGraph& g_;
    std::function<QuantLib::ext::shared_ptr<IrLgm1fParametrization>()> p_;
    std::set<ModelCG::ModelParameter>& modelParameters_;
    std::set<ModelCG::ModelParameter>& cachedParameters_;
};

} // namespace ore::data
