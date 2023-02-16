/*
  Copyright (C) 2022 Quaternion Risk Management Ltd.
  All rights reserved.
 */

/*! \file qle/cashflows/prdccoupon.hpp
    \brief Power reverse dual-currency note
*/

#pragma once

#include <ql/cashflows/couponpricer.hpp>
#include <ql/cashflows/fixedratecoupon.hpp>
#include <ql/time/schedule.hpp>
#include <qle/cashflows/fxlinkedcashflow.hpp>
#include <qle/indexes/fxindex.hpp>

#include <qle/methods/multipathgeneratorbase.hpp>
#include <qle/models/crossassetmodel.hpp>

namespace QuantExt {
using namespace QuantLib;

class PrdcFixedCoupon : public QuantLib::Observer, public FixedRateCoupon, public FXLinked {
public:
    //! PrdcCoupon
    PrdcFixedCoupon(boost::shared_ptr<FxIndex> fxIndex, const boost::shared_ptr<FixedRateCoupon>& underlying,
                    const Date& fxFixingDate, Real foreignAmount, Real domesticAmount, Rate cap = Null<Rate>(),
                    Rate floor = Null<Rate>());

    //! \name FXLinked interface
    //@{
    boost::shared_ptr<FXLinked> clone(boost::shared_ptr<FxIndex> fxIndex) override;
    //@}

    //@}
    //! \name CashFlow interface
    //@{
    Real amount() const override;

    //! \name Coupon interface
    //@{
    Rate nominal() const override;
    Rate rate() const override;
    Real accruedAmount(const Date&) const override;
    //@}

    //! \name Observer interface
    //@{
    void update() override;
    //@}

    //! \name Visitability
    //@{
    void accept(AcyclicVisitor&) override;
    //@}

    //! \name Inspectors
    inline boost::shared_ptr<FixedRateCoupon> underlying() const { return underlying_; };
    inline Real domesticAmount() const { return domesticAmount_; };
    inline Rate cap() const { return cap_; };
    inline Rate floor() const { return floor_; };

    //! \name PRDC interface
    virtual Rate couponRate() const;

private:
    const boost::shared_ptr<FixedRateCoupon> underlying_;
    Real domesticAmount_;
    Rate cap_;
    Rate floor_;
};

//! helper class building a sequence of capped/floored PRDC coupons
class PrdcLeg {
public:
    PrdcLeg(Schedule schedule, const boost::shared_ptr<QuantExt::FxIndex>& fxIndex);
    PrdcLeg& withNotionals(Real notional);
    PrdcLeg& withNotionals(const std::vector<Real>& notionals);
    PrdcLeg& withPaymentDayCounter(const DayCounter& dayCounter);
    PrdcLeg& withFixingAdjustment(BusinessDayConvention convention);
    PrdcLeg& withFixingCalendar(const Calendar& cal);
    PrdcLeg& withFixingDays(Natural fixingDays);
    PrdcLeg& withInArrears(bool inArrearsFixing = true);
    PrdcLeg& withDomesticRates(Rate rate);
    PrdcLeg& withDomesticRates(const std::vector<Rate>& rates);
    PrdcLeg& withForeignRates(Rate rate);
    PrdcLeg& withForeignRates(const std::vector<Rate>& rates);
    PrdcLeg& withCaps(Rate cap);
    PrdcLeg& withCaps(const std::vector<Rate>& caps);
    PrdcLeg& withFloors(Rate floor);
    PrdcLeg& withFloors(const std::vector<Rate>& floors);
    inline void simulate() { simulate_ = true; }
    operator Leg() const;

private:
    bool simulate_;
    Schedule schedule_;
    boost::shared_ptr<QuantExt::FxIndex> fxIndex_;
    std::vector<Real> notionals_;
    DayCounter paymentDayCounter_;
    BusinessDayConvention fixingAdjustment_;
    Calendar fixingCalendar_;
    Period fixingPeriod_;
    bool inArrearsFixing_;
    std::vector<Rate> domesticRates_;
    std::vector<Rate> foreignRates_;
    std::vector<Rate> caps_;
    std::vector<Rate> floors_;
};

//! base pricer for PRDC coupons
class CAMPricer : public FloatingRateCouponPricer {
public:
    explicit CAMPricer(boost::shared_ptr<QuantExt::CrossAssetModel> model,
                       boost::shared_ptr<QuantExt::MultiPathGeneratorBase> pathGen)
        : pathGen_(pathGen), model_(model) {}

    void initialize(const FloatingRateCoupon& coupon) override;
    Real swapletPrice() const override;
    Rate swapletRate() const override;
    Real capletPrice(Rate effectiveCap) const override;
    Rate capletRate(Rate effectiveCap) const override;
    Real floorletPrice(Rate effectiveFloor) const override;
    Rate floorletRate(Rate effectiveFloor) const override;

private:
    boost::shared_ptr<QuantExt::CrossAssetModel> model_;
    boost::shared_ptr<QuantExt::MultiPathGeneratorBase> pathGen_;
};

} // namespace QuantExt
