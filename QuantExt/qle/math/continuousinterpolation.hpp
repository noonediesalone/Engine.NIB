/*
 Copyright (C) 2021 Quaternion Risk Management Ltd
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

/*! \file continuousinterpolation.hpp
    \brief continuous interpolation between discrete points
    \ingroup math
*/

#pragma once

#include <ql/math/interpolation.hpp>
#include <vector>

namespace QuantExt {
    using namespace QuantLib;

    namespace detail {
        template <class I1, class I2>
        class ContinuousInterpolationImpl;
    }

    //! %Continuous interpolation between discrete points
    /*! \ingroup interpolations
        \warning See the Interpolation class for information about the
                 required lifetime of the underlying data.
    */
    class ContinuousInterpolation : public Interpolation {
    public:
        /*! \pre the \f$ x \f$ values must be sorted. */
        template <class I1, class I2> ContinuousInterpolation(const I1& xBegin, const I1& xEnd, const I2& yBegin) {
            impl_ = ext::shared_ptr<Interpolation::Impl>(
                new detail::ContinuousInterpolationImpl<I1, I2>(xBegin, xEnd, yBegin));
            impl_->update();
        }
    };

    //! %Constant continuous forward rate interpolation factory and traits
    /*! \ingroup math */
    class ContinuousForward {
    public:
        template <class I1, class I2>
        Interpolation interpolate(const I1& xBegin, const I1& xEnd, const I2& yBegin) const {
            return ContinuousInterpolation(xBegin, xEnd, yBegin);
        }
        static const bool global = false;
        static const Size requiredPoints = 2;
    };

    namespace detail {

        template <class I1, class I2>
        class ContinuousInterpolationImpl : public Interpolation::templateImpl<I1, I2> {
        public:
            ContinuousInterpolationImpl(const I1& xBegin, const I1& xEnd, const I2& yBegin)
                : Interpolation::templateImpl<I1, I2>(xBegin, xEnd, yBegin, ContinuousForward::requiredPoints),
              primitiveConst_(xEnd - xBegin), a_(xEnd - xBegin), b_(xEnd - xBegin) {}

            void update() override {
                primitiveConst_[0] = 0.0;
                for (Size i = 1; i < Size(this->xEnd_ - this->xBegin_); ++i) {
                    Real x1 = Real(this->xBegin_[i - 1]);
                    Real x2 = Real(this->xBegin_[i]);
                    Real y1 = Real(this->yBegin_[i - 1]);
                    Real y2 = Real(this->yBegin_[i]);
                    Real dx = x2 - x1;
                    Real dy = y2 - y1;
                    a_[i - 1] = (x2 * y2 - x1 * y1) / dx;
                    b_[i - 1] = x1 * x2 * dy / dx;
                    primitiveConst_[i] =
                        primitiveConst_[i - 1] + dx * a_[i - 1] - std::log(dx) * b_[i - 1];
                }
            }

            Real value(Real x) const override {
                Size i = this->locate(x);
                return a_[i] - b_[i] / x;
            }

            Real primitive(Real x) const override {
                Size i = this->locate(x);
                Real dx = x - this->xBegin_[i];
                return primitiveConst_[i] + dx * a_[i] - std::log(dx)  * b_[i];
            }

            Real derivative(Real x) const override {
                Size i = this->locate(x);
                return b_[i] / (x * x);
            }

            Real secondDerivative(Real x) const override {
                return -2 * derivative(x) / x;
            }
        
        private:
            std::vector<Real> primitiveConst_, a_, b_;
        };

    }

}
