/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2016 Quaternion Risk Management Ltd.
 All rights reserved.
*/

/*! \file lgmimpliedyieldtermstructure.hpp
    \brief yield term structure implied by a LGM model
*/

#ifndef quantext_statically_corrected_yts_hpp
#define quantext_statically_corrected_yts_hpp

#include <qle/termstructures/dynamicstype.hpp>

#include <ql/termstructures/yieldtermstructure.hpp>

#include <boost/unordered_map.hpp>

using namespace QuantLib;

namespace QuantExt {

/*! This termstructure takes a floating reference date term structure
  and two fixed reference date term structures, applying a static
  correction to the floating ts implied by the two fixed ones.
  Usually the floating term structure will coincide with
  the first fixed at construction time. Also, the two fixed
  termstructures should have the same reference date and all three
  termstructures should have the same day counter. */
class StaticallyCorrectedYieldTermStructure : public YieldTermStructure {
  public:
    StaticallyCorrectedYieldTermStructure(
        const Handle<YieldTermStructure> &floatingTermStructure,
        const Handle<YieldTermStructure> &fixedSourceTermStructure,
        const Handle<YieldTermStructure> &fixedTargetTermStructure,
        const YieldCurveRollDown &rollDown = ForwardForward)
        : YieldTermStructure(floatingTermStructure->dayCounter()),
          x_(floatingTermStructure), source_(fixedSourceTermStructure),
          target_(fixedTargetTermStructure), rollDown_(rollDown) {
        registerWith(floatingTermStructure);
        registerWith(fixedSourceTermStructure);
        registerWith(fixedTargetTermStructure);
    }

    Date maxDate() const { return x_->maxDate(); }
    void update() {}
    const Date &referenceDate() const { return x_->referenceDate(); }

    Calendar calendar() const { return x_->calendar(); }
    Natural settlementDays() const { return x_->settlementDays(); }

    void flushCache() { cache_c_.clear(); }

  protected:
    Real discountImpl(Time t) const;

  private:
    //FIXME: remove cache
    // cache for source and target forwards
    struct cache_key {
        double t0, t;
        bool operator==(const cache_key &o) const {
            return (t0 == o.t0) && (t == o.t);
        }
    };
    struct cache_hasher : std::unary_function<cache_key, std::size_t> {
        std::size_t operator()(cache_key const &x) const {
            std::size_t seed = 0;
            boost::hash_combine(seed, x.t0);
            boost::hash_combine(seed, x.t);
            return seed;
        }
    };
    mutable boost::unordered_map<cache_key, Real, cache_hasher> cache_c_;
    // end cache
    const Handle<YieldTermStructure> x_, source_, target_;
    const YieldCurveRollDown rollDown_;
};

// inline

inline Real StaticallyCorrectedYieldTermStructure::discountImpl(Time t) const {
    Real c = 1.0;
    if (rollDown_ == ForwardForward) {
        Real t0 = source_->timeFromReference(referenceDate());
        // roll down = ForwardForward
        // cache lookup
        cache_key k = {t0, t};
        boost::unordered_map<cache_key, Real>::const_iterator i =
            cache_c_.find(k);
        if (i == cache_c_.end()) {
            c = source_->discount(t0) / source_->discount(t0 + t) *
                target_->discount(t0 + t) / target_->discount(t0);
            cache_c_.insert(std::make_pair(k, c));
        } else {
            c = i->second;
        }
    } else {
        // roll down = ConstantDiscount
        // cache lookup
        cache_key k = {0.0, t};
        boost::unordered_map<cache_key, Real>::const_iterator i =
            cache_c_.find(k);
        if (i == cache_c_.end()) {
            c = target_->discount(t) / source_->discount(t);
            cache_c_.insert(std::make_pair(k, c));
        } else {
            c = i->second;
        }
    }
    return x_->discount(t) * c;
}

} // namesapce QuantExt

#endif
