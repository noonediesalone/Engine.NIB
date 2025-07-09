/*
 Copyright (C) 2024 Quaternion Risk Management Ltd
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

#include <qle/termstructures/parametricvolatilitysmilesection.hpp>

namespace QuantExt {

ParametricVolatilitySmileSection::ParametricVolatilitySmileSection(
    const Real optionTime, const Real swapLength, const Real atmLevel,
    const QuantLib::ext::shared_ptr<ParametricVolatility> parametricVolatility,
    const ParametricVolatility::MarketQuoteType outputMarketQuoteType, const Real outputLognormalShift)
    : SmileSection(optionTime, DayCounter(),
                   outputMarketQuoteType == ParametricVolatility::MarketQuoteType::NormalVolatility
                       ? QuantLib::Normal
                       : QuantLib::ShiftedLognormal,
                   outputLognormalShift),
      swapLength_(swapLength), atmLevel_(atmLevel), parametricVolatility_(parametricVolatility),
      outputMarketQuoteType_(outputMarketQuoteType) {}

Real ParametricVolatilitySmileSection::atmLevel() const { return atmLevel_; }

Real ParametricVolatilitySmileSection::volatilityImpl(Rate strike) const {
    if (auto v = cache_.find(strike); v != cache_.end())
        return v->second;
    Real tmp =
        parametricVolatility_->evaluate(exerciseTime(), swapLength_, strike == Null<Real>() ? atmLevel() : strike,
                                        atmLevel_, outputMarketQuoteType_, shift());
    cache_[strike] = tmp;
    return tmp;
}

} // namespace QuantExt
