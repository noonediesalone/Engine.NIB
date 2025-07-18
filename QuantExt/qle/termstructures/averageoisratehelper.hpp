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

/*! \file averageoisratehelper.hpp
    \brief Rate helpers to facilitate usage of AverageOIS in bootstrapping
    \ingroup termstructures
*/

#ifndef quantext_average_ois_rate_helper_hpp
#define quantext_average_ois_rate_helper_hpp

#include <ql/termstructures/yield/ratehelpers.hpp>

#include <qle/instruments/averageois.hpp>

namespace QuantExt {
using namespace QuantLib;

//! Average OIS Rate Helper
/*! Rate helper to facilitate the usage of an AverageOIS instrument in
    bootstrapping. This instrument pays a fixed leg vs. a leg that
    pays the arithmetic average of an ON index plus a spread.

            \ingroup termstructures
*/
class AverageOISRateHelper : public RelativeDateRateHelper {
public:
    AverageOISRateHelper(const Handle<Quote>& fixedRate, const Period& spotLagTenor, const Period& swapTenor,
                         // Fixed leg
                         const Period& fixedTenor, const DayCounter& fixedDayCounter, const Calendar& fixedCalendar,
                         BusinessDayConvention fixedConvention, BusinessDayConvention fixedPaymentAdjustment,
                         // ON leg
                         const QuantLib::ext::shared_ptr<OvernightIndex>& overnightIndex, const bool onIndexGiven,
                         const Period& onTenor, const Handle<Quote>& onSpread, Natural rateCutoff,
                         // Exogenous discount curve
                         const Handle<YieldTermStructure>& discountCurve = Handle<YieldTermStructure>(),
                         const bool discountCurveGiven = false, const bool telescopicValueDates = false);

    //! \name RateHelper interface
    //@{
    Real impliedQuote() const override;
    void setTermStructure(YieldTermStructure*) override;
    //@}
    //! \name AverageOISRateHelper inspectors
    //@{
    Spread onSpread() const;
    QuantLib::ext::shared_ptr<AverageOIS> averageOIS() const;
    //@}
    //! \name Visitability
    //@{
    void accept(AcyclicVisitor&) override;
    //@}
protected:
    void initializeDates() override;
    QuantLib::ext::shared_ptr<AverageOIS> averageOIS_;
    // Swap
    Period spotLagTenor_;
    Period swapTenor_;
    // Fixed leg
    Period fixedTenor_;
    DayCounter fixedDayCounter_;
    Calendar fixedCalendar_;
    BusinessDayConvention fixedConvention_;
    BusinessDayConvention fixedPaymentAdjustment_;
    // ON leg
    QuantLib::ext::shared_ptr<OvernightIndex> overnightIndex_;
    bool onIndexGiven_;
    Period onTenor_;
    Handle<Quote> onSpread_;
    Natural rateCutoff_;
    // Curves
    RelinkableHandle<YieldTermStructure> termStructureHandle_;
    Handle<YieldTermStructure> discountHandle_;
    bool discountCurveGiven_;
    RelinkableHandle<YieldTermStructure> discountRelinkableHandle_;
    bool telescopicValueDates_;
};

} // namespace QuantExt

#endif
