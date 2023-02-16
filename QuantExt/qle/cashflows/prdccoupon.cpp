/*
 Copyright (C) 2022 Quaternion Risk Management Ltd.
 All rights reserved.
*/

#include <ql/cashflows/capflooredcoupon.hpp>
#include <ql/cashflows/cashflowvectors.hpp>
#include <ql/utilities/vectors.hpp>
#include <qle/cashflows/prdccoupon.hpp>
#include <utility>

#include <qle/models/crossassetmodel.hpp>
#include <qle/models/fxbspiecewiseconstantparametrization.hpp>
#include <qle/models/parametrization.hpp>

namespace QuantExt {

PrdcFixedCoupon::PrdcFixedCoupon(boost::shared_ptr<FxIndex> fxIndex,
                                 const boost::shared_ptr<FixedRateCoupon>& underlying, const Date& fxFixingDate,
                                 Real foreignAmount, Real domesticAmount, Rate cap, Rate floor)
    : FixedRateCoupon(underlying->date(), foreignAmount, underlying->rate(), underlying->dayCounter(),
                      underlying->accrualStartDate(), underlying->accrualEndDate(), underlying->referencePeriodStart(),
                      underlying->referencePeriodEnd(), underlying->exCouponDate()),
      FXLinked(fxFixingDate, foreignAmount, fxIndex), underlying_(underlying), domesticAmount_(domesticAmount),
      cap_(cap), floor_(floor) {
    registerWith(FXLinked::fxIndex());
    registerWith(underlying_);
}

Rate PrdcFixedCoupon::couponRate() const {
    auto r = foreignAmount() * rate() - domesticAmount();

    if (cap_ != Null<Rate>())
        r = std::min(cap_, r);

    if (floor_ != Null<Rate>())
        r = std::max(floor_, r);

    return r;
}

Real PrdcFixedCoupon::amount() const {
    return couponRate() * nominal() *
           (interestRate().compoundFactor(accrualStartDate(), accrualEndDate(), referencePeriodStart(),
                                          referencePeriodEnd()) -
            1.0);
}

Rate PrdcFixedCoupon::nominal() const { return underlying_->nominal(); }

Rate PrdcFixedCoupon::rate() const { return fxRate(); }

Real PrdcFixedCoupon::accruedAmount(const Date& d) const {
    if (d <= accrualStartDate() || d > paymentDate_) {
        // out of coupon range
        return 0.0;
    } else {
        auto df = (interestRate().compoundFactor(d, std::max(d, accrualEndDate()), referencePeriodStart(),
                                                 referencePeriodEnd()) -
                   1.0);
        if (tradingExCoupon(d)) {
            return -nominal() * df;
        } else {
            // usual case
            return nominal() * df;
        }
    }
}

void PrdcFixedCoupon::update() { notifyObservers(); }

void PrdcFixedCoupon::accept(AcyclicVisitor& v) {
    Visitor<PrdcFixedCoupon>* v1 = dynamic_cast<Visitor<PrdcFixedCoupon>*>(&v);
    if (v1 != 0)
        v1->visit(*this);
    else
        FixedRateCoupon::accept(v);
}

boost::shared_ptr<FXLinked> PrdcFixedCoupon::clone(boost::shared_ptr<FxIndex> fxIndex) {
    return boost::make_shared<PrdcFixedCoupon>(fxIndex, underlying(), fxFixingDate(), foreignAmount(), domesticAmount(),
                                               cap(), floor());
}

PrdcLeg::PrdcLeg(Schedule schedule, const boost::shared_ptr<QuantExt::FxIndex>& fxIndex)
    : schedule_(std::move(schedule)), fxIndex_(fxIndex), fixingAdjustment_(Following), simulate_(false),
      inArrearsFixing_(true) {}

PrdcLeg& PrdcLeg::withNotionals(Real notional) {
    notionals_ = std::vector<Real>(1, notional);
    return *this;
}

PrdcLeg& PrdcLeg::withNotionals(const std::vector<Real>& notionals) {
    notionals_ = notionals;
    return *this;
}

PrdcLeg& PrdcLeg::withPaymentDayCounter(const DayCounter& dayCounter) {
    paymentDayCounter_ = dayCounter;
    return *this;
}

PrdcLeg& PrdcLeg::withFixingAdjustment(BusinessDayConvention convention) {
    fixingAdjustment_ = convention;
    return *this;
}

PrdcLeg& PrdcLeg::withFixingCalendar(const Calendar& cal) {
    fixingCalendar_ = cal;
    return *this;
}

PrdcLeg& PrdcLeg::withFixingDays(Natural fixingDays) {
    fixingPeriod_ = Period(fixingDays, TimeUnit::Days);
    return *this;
}

PrdcLeg& PrdcLeg::withInArrears(bool inArrearsFixing) {
    inArrearsFixing_ = inArrearsFixing;
    return *this;
}

PrdcLeg& PrdcLeg::withDomesticRates(Rate rate) {
    domesticRates_ = std::vector<Rate>(1, rate);
    return *this;
}

PrdcLeg& PrdcLeg::withDomesticRates(const std::vector<Rate>& rates) {
    domesticRates_ = rates;
    return *this;
}

PrdcLeg& PrdcLeg::withForeignRates(Rate rate) {
    foreignRates_ = std::vector<Rate>(1, rate);
    return *this;
}

PrdcLeg& PrdcLeg::withForeignRates(const std::vector<Rate>& rates) {
    foreignRates_ = rates;
    return *this;
}

PrdcLeg& PrdcLeg::withCaps(Rate cap) {
    caps_ = std::vector<Rate>(1, cap);
    return *this;
}

PrdcLeg& PrdcLeg::withCaps(const std::vector<Rate>& caps) {
    caps_ = caps;
    return *this;
}

PrdcLeg& PrdcLeg::withFloors(Rate floor) {
    floors_ = std::vector<Rate>(1, floor);
    return *this;
}

PrdcLeg& PrdcLeg::withFloors(const std::vector<Rate>& floors) {
    floors_ = floors;
    return *this;
}

PrdcLeg::operator Leg() const {
    Leg leg;

    auto paymentCalendar = schedule_.calendar();
    auto paymentAdjustment = schedule_.businessDayConvention();
    auto fixingCalendar = fixingCalendar_.empty() ? paymentCalendar : fixingCalendar_;
    const Rate fixed_rate = 1;

    for (Size i = 0; i < schedule_.size() - 1; i++) {
        Date startDate = schedule_[i];
        Date endDate = schedule_[i + 1];
        Date paymentDate = paymentCalendar.adjust(endDate, paymentAdjustment);
        Date fixingDate = inArrearsFixing_ ? endDate : startDate;
        fixingDate = fixingCalendar.advance(fixingDate, fixingPeriod_);
        fixingDate = fixingCalendar.adjust(fixingDate, fixingAdjustment_);
        auto foreignRate = QuantLib::detail::get(foreignRates_, i, 1.0);
        auto domesticRate = QuantLib::detail::get(domesticRates_, i, 0);
        auto notional = QuantLib::detail::get(notionals_, i, 0);
        auto cap = QuantLib::detail::get(caps_, i, Null<Rate>());
        auto floor = QuantLib::detail::get(floors_, i, Null<Rate>());

        if (!simulate_) {
            auto fixedCoupon = boost::make_shared<FixedRateCoupon>(
                paymentDate, notional, fixed_rate, paymentDayCounter_, startDate, endDate, Date(), Date());
            boost::shared_ptr<PrdcFixedCoupon> coupon = boost::make_shared<PrdcFixedCoupon>(
                fxIndex_, fixedCoupon, fixingDate, foreignRate, domesticRate, cap, floor);
            leg.push_back(coupon);
        } else {
            QL_ASSERT(false, "PRDC with simulation not implemented");
        }
    }
    return leg;
}

void CAMPricer::initialize(const FloatingRateCoupon& coupon) {}
Real CAMPricer::swapletPrice() const { return 0; }
Rate CAMPricer::swapletRate() const { return 0; }
Real CAMPricer::capletPrice(Rate effectiveCap) const { return 0; }
Rate CAMPricer::capletRate(Rate effectiveCap) const { return 0; }
Real CAMPricer::floorletPrice(Rate effectiveFloor) const { return 0; }
Rate CAMPricer::floorletRate(Rate effectiveFloor) const { return 0; }

} // namespace QuantExt
