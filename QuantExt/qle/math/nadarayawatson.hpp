/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2016 Quaternion Risk Management Ltd.
*/

#ifndef quantext_nadaraya_watson_regression_hpp
#define quantext_nadaraya_watson_regression_hpp

#include <ql/math/comparison.hpp>

#include <boost/make_shared.hpp>

/*! \file nadarayawatson.hpp
    \brief Nadaraya-Watson regression
*/

using QuantLib::Real;
using QuantLib::Size;
using QuantLib::Interpolation;

namespace QuantExt {
namespace detail {

class RegressionImpl {
public:
    virtual ~RegressionImpl() {}
    virtual void update() = 0;
    virtual Real value(Real x) const = 0;
    virtual Real standardDeviation(Real x) const = 0;
};

template <class I1, class I2, class Kernel> class NadarayaWatsonImpl : public RegressionImpl {
public:
    /*! \pre the \f$ x \f$ values must be sorted.
        \pre kernel needs a Real operator()(Real x) implementation
    */
    NadarayaWatsonImpl(const I1& xBegin, const I1& xEnd, const I2& yBegin, const Kernel& kernel)
        : xBegin_(xBegin), xEnd_(xEnd), yBegin_(yBegin), kernel_(kernel) {}

    void update() {}

    Real value(Real x) const {

        Real tmp1 = 0.0, tmp2 = 0.0;

        for (Size i = 0; i < xEnd_ - xBegin_; ++i) {
            Real tmp = kernel_(x - xBegin_[i]);
            tmp1 += yBegin_[i] * tmp;
            tmp2 += tmp;
        }

        return QuantLib::close_enough(tmp2, 0.0) ? 0.0 : tmp1 / tmp2;
    }

    Real standardDeviation(Real x) const {

        Real tmp1 = 0.0, tmp1b = 0.0, tmp2 = 0.0;

        for (Size i = 0; i < xEnd_ - xBegin_; ++i) {
            Real tmp = kernel_(x - xBegin_[i]);
            tmp1 += yBegin_[i] * tmp;
            tmp1b += yBegin_[i] * yBegin_[i] * tmp;
            tmp2 += tmp;
        }

        return QuantLib::close_enough(tmp2, 0.0) ? 0.0 : std::sqrt(tmp1b / tmp2 - (tmp1 * tmp1) / (tmp2 * tmp2));
    }

private:
    Kernel kernel_;
    I1 xBegin_, xEnd_;
    I2 yBegin_;
};
} // namespace detail

//! Nadaraya Watgon regression
/*! This implements the estimator

    \f[
    m(x) = \frac{\sum_i y_i K(x-x_i)}{\sum_i K(x-x_i)}
    \f]

    \ingroup interpolations
*/
class NadarayaWatson {
public:
    /*! \pre the \f$ x \f$ values must be sorted.
        \pre kernel needs a Real operator()(Real x) implementation
    */
    template <class I1, class I2, class Kernel>
    NadarayaWatson(const I1& xBegin, const I1& xEnd, const I2& yBegin, const Kernel& kernel) {
        impl_ = boost::make_shared<detail::NadarayaWatsonImpl<I1, I2, Kernel> >(xBegin, xEnd, yBegin, kernel);
    }

    Real operator()(Real x) const { return impl_->value(x); }

    Real standardDeviation(Real x) const { return impl_->standardDeviation(x); }

private:
    boost::shared_ptr<detail::RegressionImpl> impl_;
};

} // namespace QuantExt

#endif
