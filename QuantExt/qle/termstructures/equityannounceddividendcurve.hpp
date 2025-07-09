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

/*! \file qle/termstructures/equityannounceddividendcurve.hpp
    \brief holds future announced dividends
    \ingroup termstructures
*/

#ifndef quantext_equity_announced_dividend_curve_hpp
#define quantext_equity_announced_dividend_curve_hpp

#include <ql/termstructure.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>

namespace QuantExt {

class EquityAnnouncedDividendCurve : public QuantLib::TermStructure {

public:
    EquityAnnouncedDividendCurve(const QuantLib::Date& referenceDate, const std::set<QuantExt::Dividend>& dividends,
                                const Handle<YieldTermStructure>& discountCurve,
                                const QuantLib::Calendar& cal = QuantLib::Calendar(),
                                const QuantLib::DayCounter& dc = QuantLib::DayCounter())
        : QuantLib::TermStructure(referenceDate, cal, dc), discountCurve_(discountCurve) {
        times_.push_back(0.0);
        discountedDivs_.push_back(0.0);
        for (auto& d : dividends) {
            if (d.payDate <= referenceDate)
                continue;
            QuantLib::Time time = timeFromReference(d.payDate);
            for (Size i = 0; i < times_.size(); ++i) {
                discountedDivs_[i] += d.rate * discountCurve->discount(time);
            }
            times_.push_back(time);
            discountedDivs_.push_back(0.0);
        }
    }

    //! \name TermStructure interface
    //@{
    Date maxDate() const override { return Date::maxDate(); }
    //@}

    QuantLib::Real discountedFutureDividends(Time t) {
        if (discountedDivs_.size() > 1) {
            std::vector<Time>::const_iterator it = std::upper_bound(times_.begin(), times_.end(), t);
            if (it == times_.end())
                return 0.0;
            QuantLib::Size i = it - times_.begin();
            auto discountDiv = discountedDivs_[i - 1];
            return discountDiv / discountCurve_->discount(t);
        } else
            return discountedDivs_[0];
    }

private:
    Handle<YieldTermStructure> discountCurve_;
    std::vector<QuantLib::Time> times_;
    std::vector<QuantLib::Real> discountedDivs_;
};

} // namespace QuantExt
#endif
